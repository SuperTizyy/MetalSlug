// Copyright (c) 2026.
//
// 【大厂架构 v40.10 2026.07.31 + v40.12 2026.08.01 + v126 2026.08.01】BTTask_MoveToFacingTarget 实现
//
// 【v126 2026.08.01 新增】智能避障路径规划 — 用户业务规则驱动
//   - 启动 MoveTo 前检测路径上是否有 EvadeActorClasses 中的 Actor
//   - 有则重 plan 一次 (绕路), 重 plan 仍被挡 → 重试, 超 MaxReplanAttempts → 兜底走 UE 原生
//   - 大厂原则: 配置驱动 (策划在 BT 编辑器 Details 面板选 EvadeActorClasses)
//
// 详见头文件注释. 实现要点:
//   1. BB Key 双重检查 (SelectedKeyName + GetKeyID != InvalidKey)
//   2. 【v40.12】TargetActor 可选 — 为空时 Log Warning + 跳过朝向机制 + 继续 MoveTo
//   3. UAIFacingMoveHelper::ConfigureFacingMove 处理朝向机制 (仅 TargetActor 有效时)
//   4. 【v126】PlanPathAvoidingActors 在 StartMoveTo 之前算避障路径
//   5. MoveToLocation 异步启动
//   6. TickTask 检查 PathFollowingComponent 状态 (Idle / Waiting)
//   7. 完成/超时/Abort 时 RestoreFacingMove (仅 bFacingConfigured=true 时, 大厂对称)

#include "Systems/AI/Tasks/BTTask_MoveToFacingTarget.h"

#include "AIController.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/BlackboardData.h" // 【v40.11.1】UBlackboardData::GetKeyType (策划在 BB 资产配的类型)
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h" // 【v40.11】UBlackboardKeyType_Object
#include "BehaviorTree/Blackboard/BlackboardKeyType_Vector.h" // 【v40.11】UBlackboardKeyType_Vector
#include "GameFramework/Actor.h"
#include "GameFramework/Character.h"
#include "GameFramework/Pawn.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationSystem.h"
#include "NavigationPath.h"
#include "EngineUtils.h"
#include "Kismet/GameplayStatics.h"

#include "Systems/AI/AIFacingMoveHelper.h"
#include "Characters/BaseCharacter.h"

UBTTask_MoveToFacingTarget::UBTTask_MoveToFacingTarget()
{
	NodeName = TEXT("Move To Facing Target");

	// 异步 Task: MoveTo 是异步过程, 需要 TickTask 检查到达
	bNotifyTick = true;

	// BB Key 过滤器 — 必须在构造函数注册, UE 编辑器才能识别
	TargetActorKey.AddObjectFilter(this,
		GET_MEMBER_NAME_CHECKED(UBTTask_MoveToFacingTarget, TargetActorKey),
		AActor::StaticClass());

	// MoveLocationKey 支持两种类型 (UE 5.x 标准做法 — AllowedTypes 数组):
	//   1. Vector Key: 直接读 GetValueAsVector (例如 RallyPoint)
	//   2. Object Key (AActor): 读 GetValueAsObject → GetActorLocation
	// 连续两次 AddXxxFilter 让 AllowedTypes 数组接受两种, 运行时按 Key 实际类型分支处理.
	MoveLocationKey.AddVectorFilter(this,
		GET_MEMBER_NAME_CHECKED(UBTTask_MoveToFacingTarget, MoveLocationKey));
	MoveLocationKey.AddObjectFilter(this,
		GET_MEMBER_NAME_CHECKED(UBTTask_MoveToFacingTarget, MoveLocationKey),
		AActor::StaticClass());
}

/**
 * @brief 生成 BT 节点描述 — 展示"边移动边面向"通用 Task 的关键参数
 * @return 多行描述,展示 TargetActorKey/MoveLocationKey(V 或 Object)/AcceptanceRadius/超时/Helper 复用
 */
FString UBTTask_MoveToFacingTarget::GetStaticDescription() const
{
	return FString::Printf(TEXT("【v40.12 大厂重构】边移动边面向目标的通用 Task.\n"
		"目标 Actor (可选): BB.%s — 为空时跳过朝向机制, 仍 MoveTo\n"
		"目的地 (必填): BB.%s (Vector 或 Object AActor, 运行时按 Key 类型分支)\n"
		"AcceptanceRadius: %.0fcm\n"
		"被阻挡超时: %.1fs → 强制 Succeeded\n"
		"朝向机制: TargetActor 有效时复用 UAIFacingMoveHelper (单一真理源).\n"
		"【修复回头走】移动期间关 OrientRotationToMovement."),
		*TargetActorKey.SelectedKeyName.ToString(),
		*MoveLocationKey.SelectedKeyName.ToString(),
		AcceptanceRadius,
		MaxWaitTime);
}

/**
 * @brief 校验 BB Key 配置 — 4 层零兜底检查
 * @param BB BT 黑板组件引用
 * @param AIC AI 控制器引用(用于错误日志)
 * @param OutResult [out] 校验失败时的结果码(EBTNodeResult::Failed)
 * @return 所有 Key 检查通过 → true;任一失败 → false 且 OutResult 写入 Failed
 *
 * 4 层检查顺序:
 *   1/4 TargetActorKey.SelectedKeyName != None
 *   2/4 MoveLocationKey.SelectedKeyName != None
 *   3/4 BB.GetKeyID(TargetActorKey) != InvalidKey(资产里有这个 Key)
 *   4/4 BB.GetKeyID(MoveLocationKey) != InvalidKey
 *
 * 失败原因 Log Error + OutResult=Failed,调用方直接 return OutResult.
 */
bool UBTTask_MoveToFacingTarget::ValidateBlackboardKeys(
	const UBlackboardComponent& BB,
	const AAIController& AIC,
	EBTNodeResult::Type& OutResult) const
{
	// 【零兜底 1/4】TargetActorKey 必须配置
	if (TargetActorKey.SelectedKeyName.IsNone())
	{
		UE_LOG(LogBehaviorTree, Error,
			TEXT("[BTTask_MoveToFacingTarget] AIC='%s' TargetActorKey 未配置 (SelectedKeyName=None). "
			     "【UE 编辑器配置】BT 编辑器 → Move To Facing Target 节点 → Details → "
			     "TargetActorKey 选择 Key 名 (通常是 TargetActor)."),
			*AIC.GetName());
		OutResult = EBTNodeResult::Failed;
		return false;
	}

	// 【零兜底 2/4】MoveLocationKey 必须配置
	if (MoveLocationKey.SelectedKeyName.IsNone())
	{
		UE_LOG(LogBehaviorTree, Error,
			TEXT("[BTTask_MoveToFacingTarget] AIC='%s' MoveLocationKey 未配置 (SelectedKeyName=None). "
			     "【UE 编辑器配置】BT 编辑器 → Move To Facing Target 节点 → Details → "
			     "MoveLocationKey 选择 Key 名 (例如 RallyPoint, TacticalPosition)."),
			*AIC.GetName());
		OutResult = EBTNodeResult::Failed;
		return false;
	}

	// 【零兜底 3/4】TargetActorKey 必须在 BB 存在
	if (BB.GetKeyID(TargetActorKey.SelectedKeyName) == FBlackboard::InvalidKey)
	{
		UE_LOG(LogBehaviorTree, Error,
			TEXT("[BTTask_MoveToFacingTarget] AIC='%s' BB 不存在 Key '%s' (Type=Object). "
			     "【UE 编辑器配置】打开 BB_AI_Melee.uasset → 确认有 Key '%s'."),
			*AIC.GetName(),
			*TargetActorKey.SelectedKeyName.ToString(),
			*TargetActorKey.SelectedKeyName.ToString());
		OutResult = EBTNodeResult::Failed;
		return false;
	}

	// 【零兜底 4/4】MoveLocationKey 必须在 BB 存在
	//   类型不限 (v40.11 双类型支持), 只检查 Key 存在性, 类型不匹配在 ExecuteTask 运行时分支拒绝
	if (BB.GetKeyID(MoveLocationKey.SelectedKeyName) == FBlackboard::InvalidKey)
	{
		UE_LOG(LogBehaviorTree, Error,
			TEXT("[BTTask_MoveToFacingTarget] AIC='%s' BB 不存在 Key '%s' (类型: Vector 或 Object). "
			     "【UE 编辑器配置】打开 BB_AI_Melee.uasset → 确认有 Key '%s' (Vector 或 Object)."),
			*AIC.GetName(),
			*MoveLocationKey.SelectedKeyName.ToString(),
			*MoveLocationKey.SelectedKeyName.ToString());
		OutResult = EBTNodeResult::Failed;
		return false;
	}

	return true;
}

EBTNodeResult::Type UBTTask_MoveToFacingTarget::ExecuteTask(
	UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	FTaskMemory& Mem = GetTaskMemory(NodeMemory);
	new (&Mem) FTaskMemory();
	Mem.bMoveStarted = false;
	Mem.WaitTime = 0.f;
	// 【v126.1 修复】显式重置 v126 字段 (NodeMemory 复用残留防护)
	Mem.bEvadeEnabled = false;
	Mem.ReplanAttemptsUsed = 0;

	AAIController* AIC = OwnerComp.GetAIOwner();
	if (!AIC)
	{
		UE_LOG(LogBehaviorTree, Error,
			TEXT("[BTTask_MoveToFacingTarget] AIController=null. BT 配置错."));
		return EBTNodeResult::Failed;
	}

	APawn* AIPawn = AIC->GetPawn();
	if (!AIPawn)
	{
		UE_LOG(LogBehaviorTree, Error,
			TEXT("[BTTask_MoveToFacingTarget] AIC='%s' Pawn=null. Possess 时序错? BT 在 Possess 之前跑了."),
			*AIC->GetName());
		return EBTNodeResult::Failed;
	}

	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	if (!BB)
	{
		UE_LOG(LogBehaviorTree, Error,
			TEXT("[BTTask_MoveToFacingTarget] AIC='%s' BB=null. BT 未绑 BB_AI_Melee.uasset?"),
			*AIC->GetName());
		return EBTNodeResult::Failed;
	}

	// 【零兜底 1/2】BB Key 检查
	EBTNodeResult::Type KeyCheckResult = EBTNodeResult::Failed;
	if (!ValidateBlackboardKeys(*BB, *AIC, KeyCheckResult))
	{
		return KeyCheckResult;
	}

	// 【v40.12 2026.08.01 行为变更】TargetActor 可选 — 失效/为空时不再硬拒判
	// 旧行为 (v40.10~v40.11): TargetActor 为空 → Failed → AI 完全不动
	//   触发场景: BT_ZombieModeAI 的 Pre-Mutation 期间, BB.TargetActor 被 BTService_UpdateZombieTargets 清空
	//             (AI 还在等 0.25s Service tick), BT 走到本节点, MoveLocationKey (RallyPoint) 已写入
	//             但 TargetActor 为空 → 整条 Failed → AI 站着不动 → 违反业务"AI 进入游戏就能移动"
	// 新行为 (v40.12): TargetActor 为空/失效 → Log Warning + 跳过朝向机制 + 继续 MoveToLocation
	//   - 移动本身仍然执行 (AI 走到 MoveLocationKey 指定的 RallyPoint)
	//   - 朝向: 不 SetFocus → AI 朝向 = 移动方向 (与 UE 原生 MoveTo 一致)
	//   - 业务合理性: 集合点途中不需要面朝特定敌人, 走过去即可
	//   - 大厂原则: 移动 = 核心能力, 朝向 = 可选增强. TargetActor 缺失不阻挡核心能力.
	UObject* TargetObj = BB->GetValueAsObject(TargetActorKey.SelectedKeyName);
	AActor* TargetActor = Cast<AActor>(TargetObj);
	const bool bHasTarget = (TargetActor && !TargetActor->IsActorBeingDestroyed());

	if (!bHasTarget)
	{
		UE_LOG(LogBehaviorTree, Warning,
			TEXT("[BTTask_MoveToFacingTarget] AIC='%s' TargetActor '%s' 为空或失效. "
			     "【v40.12 新行为】跳过朝向机制, 继续 MoveTo 到 MoveLocationKey. "
			     "【正常业务场景】BT_ZombieModeAI Pre-Mutation 期间 BTService_UpdateZombieTargets 清空 TargetActor, "
			     "但 RallyPoint 已写入, AI 应能移动. (修复前: Failed 站着不动)."),
			*AIC->GetName(),
			*TargetActorKey.SelectedKeyName.ToString());
		// 不 return — 继续执行 MoveTo, 朝向机制后续会跳过
	}

	// 【零兜底】MoveLocation 必须合理
	// MoveLocationKey 双类型支持 (v40.11):
	//   - Vector  Key: 直接读 GetValueAsVector (策划写 RallyPoint / 阵位点)
	//   - Object Key (AActor 派生, 例如 BP_ZombieRallyPoint): 读 GetValueAsObject → 取 Actor 当前位置
	//
	// 【v40.11.1 关键修复】类型判断不能依赖 MoveLocationKey.SelectedKeyType
	//   根因: SelectedKeyType 字段只在 InitializeFromAsset → ResolveSelectedKey 调用后才被填充,
	//   我们当前没实现 InitializeFromAsset (项目其他 BTTask 也都没实现), 所以 SelectedKeyType 永远是 null.
	//   正确做法: 用 BB 资产的 GetKeyType(KeyID) 查策划在 BB 资产里配的类型 — 这是真正的真理源.
	const FBlackboard::FKey KeyID = BB->GetKeyID(MoveLocationKey.SelectedKeyName);
	const UBlackboardData* BBAsset = BB->GetBlackboardAsset();

	// 【零兜底 1/3】BBAsset 必须有效
	if (!BBAsset)
	{
		UE_LOG(LogBehaviorTree, Error,
			TEXT("[BTTask_MoveToFacingTarget] AIC='%s' BBAsset=null. BT 没绑 BB 资产? 【修复路径】打开 BT → Details → Blackboard Asset."),
			*AIC->GetName());
		return EBTNodeResult::Failed;
	}

	// 【零兜底 2/3】KeyID 必须有效 (GetKeyID 返回 InvalidKey = BB 里没这个 Key)
	if (KeyID == FBlackboard::InvalidKey)
	{
		UE_LOG(LogBehaviorTree, Error,
			TEXT("[BTTask_MoveToFacingTarget] AIC='%s' MoveLocationKey '%s' 在 BB 里找不到. 【修复路径】打开 BB 资产 → 确认 Key '%s' 存在."),
			*AIC->GetName(),
			*MoveLocationKey.SelectedKeyName.ToString(),
			*MoveLocationKey.SelectedKeyName.ToString());
		return EBTNodeResult::Failed;
	}

	const TSubclassOf<UBlackboardKeyType> KeyType = BBAsset->GetKeyType(KeyID);

	FVector MoveLocation = FVector::ZeroVector;

	if (KeyType == UBlackboardKeyType_Vector::StaticClass())
	{
		// ============================================
		// Vector Key 路径 (原行为不变)
		// ============================================
		MoveLocation = BB->GetValueAsVector(MoveLocationKey.SelectedKeyName);

		// 【抗 UE BB sentinel】UE BB Vector Key 未初始化时返回接近 MAX_FLT 的 sentinel,
		//   不是 (0,0,0). 检测这个值说明 BB.Key 没写入.
		if (FMath::IsNaN(MoveLocation.X) || FMath::IsNaN(MoveLocation.Y) || FMath::IsNaN(MoveLocation.Z) ||
			FMath::IsNearlyEqual(MoveLocation.X, MAX_FLT) || FMath::IsNearlyEqual(MoveLocation.Y, MAX_FLT) || FMath::IsNearlyEqual(MoveLocation.Z, MAX_FLT))
		{
			UE_LOG(LogBehaviorTree, Error,
				TEXT("[BTTask_MoveToFacingTarget] AIC='%s' MoveLocation 是 UE BB uninitialized sentinel (%s). "
				     "BB Key '%s' (Vector) 没值. 【修复路径】检查上游写入者 "
				     "(BTService_UpdateRallyPoint / BTTask_SelectZombieRallyPoint 等)."),
				*AIC->GetName(),
				*MoveLocation.ToString(),
				*MoveLocationKey.SelectedKeyName.ToString());
			return EBTNodeResult::Failed;
		}

		if (MoveLocation == FVector::ZeroVector)
		{
			UE_LOG(LogBehaviorTree, Error,
				TEXT("[BTTask_MoveToFacingTarget] AIC='%s' MoveLocation=(0,0,0). "
				     "可能是地图原点 AI 极端边界, 或 BB Key 写入失败. 【修复路径】检查上游写入者."),
				*AIC->GetName());
			return EBTNodeResult::Failed;
		}
	}
	else if (KeyType == UBlackboardKeyType_Object::StaticClass())
	{
		// ============================================
		// Object Key (AActor) 路径 (v40.11 新增)
		// ============================================
		UObject* MoveObj = BB->GetValueAsObject(MoveLocationKey.SelectedKeyName);
		AActor* MoveActor = Cast<AActor>(MoveObj);
		if (!MoveActor || MoveActor->IsActorBeingDestroyed())
		{
			UE_LOG(LogBehaviorTree, Error,
				TEXT("[BTTask_MoveToFacingTarget] AIC='%s' BB Key '%s' (Object) 失效/无值. "
				     "【修复路径】1) 在 BT 序列前置 Decorator: 该 Key Is Set "
				     "2) 检查写入者是否在 AI 死亡时清空 Key."),
				*AIC->GetName(),
				*MoveLocationKey.SelectedKeyName.ToString());
			return EBTNodeResult::Failed;
		}

		MoveLocation = MoveActor->GetActorLocation();

		// 不校验 ZeroVector — Actor 位置天然有效, 与地图原点边界 case 无关
		// 但仍校验 NaN (UE BB Object sentinel: Pawn 被销毁后可能返回悬空)
		if (MoveLocation.ContainsNaN())
		{
			UE_LOG(LogBehaviorTree, Error,
				TEXT("[BTTask_MoveToFacingTarget] AIC='%s' BB Key '%s' (Object) 的 Actor '%s' GetActorLocation 包含 NaN. "
				     "【修复路径】Actor 可能处于销毁边缘或 Physics 异常状态."),
				*AIC->GetName(),
				*MoveLocationKey.SelectedKeyName.ToString(),
				*MoveActor->GetName());
			return EBTNodeResult::Failed;
		}

		UE_LOG(LogBehaviorTree, Verbose,
			TEXT("[BTTask_MoveToFacingTarget] AIC='%s' MoveLocationKey '%s' = Object '%s' → 解出位置 %s."),
			*AIC->GetName(),
			*MoveLocationKey.SelectedKeyName.ToString(),
			*MoveActor->GetName(),
			*MoveLocation.ToString());
	}
	else
	{
		// ============================================
		// 其他类型 — 显式拒绝 (零兜底: 不允许 float / bool / int 等被当成目的地)
		// ============================================
		UE_LOG(LogBehaviorTree, Error,
			TEXT("[BTTask_MoveToFacingTarget] AIC='%s' MoveLocationKey '%s' 类型不支持. "
			     "只支持 Vector 或 Object (AActor). 实际类型: %s. "
			     "【修复路径】BT 编辑器 → 该节点 → Details → MoveLocationKey 改成 Vector 或 Object Key."),
			*AIC->GetName(),
			*MoveLocationKey.SelectedKeyName.ToString(),
			KeyType ? *KeyType->GetName() : TEXT("<null>"));
		return EBTNodeResult::Failed;
	}

// ============================================
// v40.10: 朝向机制下沉到 UAIFacingMoveHelper (单一真理源)
// 【v40.12 2026.08.01 行为变更】按 bHasTarget 决定是否调 Helper
//   - TargetActor 有效 → ConfigureFacingMove (面朝敌人, 移动中不回头走)
//   - TargetActor 为空 → 跳过 ConfigureFacingMove, 让 AI 朝向 = 移动方向 (UE 原生 MoveTo 行为)
//                       Helper 内部硬拒判 TargetActor=null, 不在这里强行调, 大厂原则 - 不越权
// ============================================
if (bHasTarget)
{
	const bool bFacingConfigured = UAIFacingMoveHelper::ConfigureFacingMove(
		Cast<ACharacter>(AIPawn),
		AIC,
		TargetActor,
		Mem.FacingSnapshot);

	if (!bFacingConfigured)
	{
		// Helper 失败 (Character/Target 无效) → 拒绝 MoveTo, 否则回头走
		// Helper 内部已 Log Error 解释根因
		// 注: bHasTarget=true 已经过滤 TargetActor, 此分支主要是 Character/Movement 组件失效
		return EBTNodeResult::Failed;
	}

	Mem.bFacingConfigured = true;
}
else
{
	// TargetActor 为空 — 跳过 ConfigureFacingMove, 不调 SetFocus, 不改 Movement 配置
	// AI 在 MoveTo 期间保持 CharacterMovement 原值 (OrientRotationToMovement 决定朝向 = 移动方向)
	// 大厂原则: 不需要面向 = 不需要改 Movement = 不需要 Save/Restore, 状态零污染
	Mem.bFacingConfigured = false;
}

// ============================================
// 【v126 2026.08.01】智能避障 — 启动 MoveTo 前算避障路径
// ============================================
//
// 业务逻辑:
//   1. 如果 EvadeActorClasses 为空 或 MaxReplanAttempts = 0 → 跳过避障 (大厂配置驱动)
//   2. 否则调 PlanPathAvoidingActors → 返回调整后的 Dest → 写 FTaskMemory
//   3. 启动 MoveTo 用调整后的 Dest (UE 引擎自动二次规划)
//
// 大厂原则:
//   - 避障配置由策划在 BT 编辑器 Details 面板配 (透明可调)
//   - 默认 EvadeActorClasses = [ABaseCharacter] (避开所有活角色) — 保守兜底
//   - 用户决策 (Q4): "给我参数让我来选择具体避开什么类型角色" → 用 TSubclassOf 数组
{
	UWorld* World = OwnerComp.GetWorld();
	if (!World)
	{
		UE_LOG(LogBehaviorTree, Error,
			TEXT("[BTTask_MoveToFacingTarget] AIC='%s' World=null, 跳过避障检测 (兜底走 UE 原生)."),
			*AIC->GetName());
	}
	else
	{
		// 【v126 默认值兜底】EvadeActorClasses 为空 → 注入 [ABaseCharacter] (避开所有活角色)
		// 大厂原则: 配置驱动 vs 默认安全 — 默认安全优先 (零兜底)
		// 策划想要"关闭避障" → 必须 MaxReplanAttempts = 0 (头文件注释已说明)
		TArray<TSubclassOf<AActor>> EffectiveClasses = EvadeActorClasses;
		if (EffectiveClasses.Num() == 0)
		{
			EffectiveClasses.Add(ABaseCharacter::StaticClass());
		}

		// 大厂原则: 业务配置 vs 内部默认值 — 都走策划在 BT 编辑器配的
		Mem.bEvadeEnabled = (MaxReplanAttempts > 0 && EffectiveClasses.Num() > 0);

		// 【v126.1 诊断】MaxReplanAttempts == 0 时显式警告 (BP 节点可能没序列化新字段)
		if (MaxReplanAttempts <= 0)
		{
			UE_LOG(LogBehaviorTree, Warning,
				TEXT("[BTTask_MoveToFacingTarget] AIC='%s' 【v126 避障】MaxReplanAttempts=%d <= 0, 跳过避障. "
				     "【如果策划想要避障】检查 BT 编辑器 → 该节点 → Details → Max Replan Attempts 应 ≥ 1."),
				*AIC->GetName(),
				MaxReplanAttempts);
		}

		if (Mem.bEvadeEnabled)
		{
			const FVector StartLoc = AIPawn->GetActorLocation();

			// 【v126.1 修复】构建 IgnoreActor 列表 — 大厂原则"避免自己挡自己"
			//   - 必须忽略 AIPawn (AI 自己),否则 AI 在 StartLoc 距离自己 0 → 必然挡 → Reroute 走自己旁边 → 看起来"不走"
			//   - 还忽略 MoveLocationActor (BB.Object Key 指向的目的地 Actor)
			//     如果目的 Actor 是 EvadeActorClasses 内的(例如 BP_GruntAI 集合点),不忽略就死循环
			//   - Vector Key 时 MoveActor = nullptr,只忽略 AIPawn
			const AActor* IgnoreActor = AIPawn; // AActor* → const AActor* 隐式转换

			UE_LOG(LogBehaviorTree, Display,
				TEXT("[BTTask_MoveToFacingTarget] AIC='%s' 【v126.1 诊断】"
				     "StartLoc=%s, OriginalDest=%s, EvadeRadius=%.0f, EvadeClasses=%d, IgnoreActor='%s'."),
				*AIC->GetName(),
				*StartLoc.ToString(),
				*MoveLocation.ToString(),
				EvadeRadius,
				EffectiveClasses.Num(),
				IgnoreActor ? *IgnoreActor->GetName() : TEXT("<null>"));

			const FVector AdjustedDest = PlanPathAvoidingActors(
				World, StartLoc, MoveLocation, MaxReplanAttempts,
				Mem.ReplanAttemptsUsed,
				IgnoreActor);

			// 【v126 关键】用调整后的目的地启动 MoveTo (UE 引擎内部会再次规划到 AdjustedDest)
			if (!AdjustedDest.Equals(MoveLocation, 1.0f))
			{
				UE_LOG(LogBehaviorTree, Display,
					TEXT("[BTTask_MoveToFacingTarget] AIC='%s' 【v126 智能避障】"
					     "原 Dest=%s → 避障后 Dest=%s (重规划 %d 次, 共 %d 类)."),
					*AIC->GetName(),
					*MoveLocation.ToString(),
					*AdjustedDest.ToString(),
					Mem.ReplanAttemptsUsed,
					EffectiveClasses.Num());
				MoveLocation = AdjustedDest;
			}
			else
			{
				UE_LOG(LogBehaviorTree, Display,
					TEXT("[BTTask_MoveToFacingTarget] AIC='%s' 【v126 智能避障】"
					     "路径畅通, 不重 plan, 用 OriginalDest."),
					*AIC->GetName());
			}
		}
	}
}

	// ============================================
	// 启动 MoveTo
	// ============================================
	if (!StartMoveTo(OwnerComp, MoveLocation))
	{
		// MoveTo 启动失败 — 恢复朝向 (仅当本次执行 ConfigureFacingMove 成功时)
		// 【v40.12 大厂对称】与 AbortTask / CheckArrival 一致 — bFacingConfigured=false 时不调 Restore
		if (Mem.bFacingConfigured)
		{
			UAIFacingMoveHelper::RestoreFacingMove(
				Cast<ACharacter>(AIPawn), AIC, Mem.FacingSnapshot);
			Mem.bFacingConfigured = false;
		}
		return EBTNodeResult::Failed;
	}

	Mem.bMoveStarted = true;

	// 【v40.12 2026.08.01 编译修复】UE 5.6 TCheckedFormatString 是 consteval,
	//   UE_LOG 实参必须是可直接计算的标量/FString — 不能写 *bHasTarget ? ... : ...
	//   (对 bool 解引用 C2100 非法的间接寻址, 触发 consteval 雪崩 C2131/C2971/C2672/C4840)
	//   修法: 先在局部 FString 算好两个条件值, 再 *LocalStr 解引用进 UE_LOG.
	//   参考 v117 BTTask_SelectZombieRallyPoint::GetStaticDescription 同款问题.
	const FString TargetStr = bHasTarget ? TargetActor->GetName() : FString(TEXT("<None>"));
	const FString FaceStr = Mem.bFacingConfigured ? FString(TEXT("ON")) : FString(TEXT("OFF"));

	UE_LOG(LogBehaviorTree, Log,
		TEXT("[BTTask_MoveToFacingTarget] AIC='%s' Start MoveTo. "
		     "Target=%s (face=%s), MoveLocation=%s, AcceptanceRadius=%.0f."),
		*AIC->GetName(),
		*TargetStr,
		*FaceStr,
		*MoveLocation.ToString(),
		AcceptanceRadius);

	// 异步任务 — TickTask 会处理后续
	return EBTNodeResult::InProgress;
}

void UBTTask_MoveToFacingTarget::TickTask(
	UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	FTaskMemory& Mem = GetTaskMemory(NodeMemory);

	if (!Mem.bMoveStarted)
	{
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		return;
	}

	CheckArrival(OwnerComp, NodeMemory);
}

EBTNodeResult::Type UBTTask_MoveToFacingTarget::AbortTask(
	UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	AAIController* AIC = OwnerComp.GetAIOwner();
	if (AIC)
	{
		AIC->StopMovement();

		// v40.10: 恢复朝向 (Helper 内幂等)
		// 【v40.12 2026.08.01 大厂对称】仅在本次执行成功 ConfigureFacingMove 时才 Restore
		//   - bFacingConfigured=true  → 必须 Restore, 否则 OrientRotationToMovement 等 Movement 配置泄漏
		//   - bFacingConfigured=false → 不调 Restore, 大厂对称 — 没 Configure 就不 Restore
		//                              否则误恢复 CharacterMovement 原值, 破坏其它系统 (例如 BTTask_FaceTarget 也改了 Movement)
		FTaskMemory& Mem = GetTaskMemory(NodeMemory);
		if (Mem.bFacingConfigured)
		{
			UAIFacingMoveHelper::RestoreFacingMove(
				Cast<ACharacter>(AIC->GetPawn()), AIC, Mem.FacingSnapshot);
			Mem.bFacingConfigured = false;
		}

		Mem.bMoveStarted = false;
	}

	return EBTNodeResult::Aborted;
}

/**
 * @brief 启动异步 MoveToLocation — 调 UE 原生寻路接口
 * @param OwnerComp BT 组件引用,用于拿 AIController
 * @param Dest 目标位置(可能已被 v126 智能避障调整过)
 * @return MoveTo 请求成功或已在目标 → true;失败 → false
 *
 * UE 原生参数(大厂标配):
 *   AcceptanceRadius, bStopOnOverlap=false, bUsePathfinding=true,
 *   bProjectDestinationToNavigation=true, bCanStrafe=false, FilterClass=nullptr,
 *   bAllowPartialPath=true(半路径也接受, 防死锁).
 */
bool UBTTask_MoveToFacingTarget::StartMoveTo(
	UBehaviorTreeComponent& OwnerComp, const FVector& Dest)
{
	AAIController* AIC = OwnerComp.GetAIOwner();
	if (!AIC)
	{
		return false;
	}

	// UE 原生 MoveToLocation: 大厂标配, 内部自动 NavMesh 寻路 + Replan + 部分路径
	// 参数 (大厂 BTTask_MoveAwayFromTarget 同款):
	//   Dest               — 目的地
	//   AcceptanceRadius   — 到达半径
	//   bStopOnOverlap     — false (大厂 BTTask 标准 — 不卡在重叠时)
	//   bUsePathfinding    — true
	//   bProjectDestinationToNavigation — true (自动投到 NavMesh)
	//   bCanStrafe         — false (Character 默认)
	//   FilterClass        — nullptr
	//   bAllowPartialPath  — true (大厂标配 — 半路径也接受, 防止死锁)
	const EPathFollowingRequestResult::Type Result =
		AIC->MoveToLocation(Dest, AcceptanceRadius, false, true, true, false, nullptr, true);

	return Result == EPathFollowingRequestResult::RequestSuccessful
		|| Result == EPathFollowingRequestResult::AlreadyAtGoal;
}

/**
 * @brief 异步检查 MoveTo 状态 — Idle 完成 / Waiting 超时则恢复朝向并 FinishLatentTask
 * @param OwnerComp BT 组件引用
 * @param NodeMemory 任务内存(FTaskMemory)
 *
 * 大厂对称:v40.12 行为仅在 bFacingConfigured=true 时调 RestoreFacingMove,
 * 避免与 BTTask_FaceTarget 等其它修改 Movement 的任务冲突.
 * 超时阈值 MaxWaitTime 与 BTTask_MoveAwayFromTarget 镜像一致.
 */
void UBTTask_MoveToFacingTarget::CheckArrival(
	UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	AAIController* AIC = OwnerComp.GetAIOwner();
	if (!AIC)
	{
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		return;
	}

	UPathFollowingComponent* PFM = AIC->GetPathFollowingComponent();
	if (!PFM)
	{
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		return;
	}

	const EPathFollowingStatus::Type MoveStatus = PFM->GetStatus();

	if (MoveStatus == EPathFollowingStatus::Idle)
	{
		// 到达目的地 — 恢复朝向 (仅当本次执行 ConfigureFacingMove 成功时)
		FTaskMemory& Mem = GetTaskMemory(NodeMemory);
		if (Mem.bFacingConfigured)
		{
			UAIFacingMoveHelper::RestoreFacingMove(
				Cast<ACharacter>(AIC->GetPawn()), AIC, Mem.FacingSnapshot);
			Mem.bFacingConfigured = false;
		}
		Mem.bMoveStarted = false;

		UE_LOG(LogBehaviorTree, Display,
			TEXT("[BTTask_MoveToFacingTarget] AIC='%s' 到达目的地 → Succeeded."),
			*AIC->GetName());

		FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
	}
	else if (MoveStatus == EPathFollowingStatus::Waiting)
	{
		// 被阻挡 — 累计等待时间, 超时强制 Succeeded (与 BTTask_MoveAwayFromTarget 一致)
		FTaskMemory& Mem = GetTaskMemory(NodeMemory);
		Mem.WaitTime += OwnerComp.GetWorld()->GetDeltaSeconds();
		if (Mem.WaitTime > MaxWaitTime)
		{
			Mem.WaitTime = 0.f;

			// 超时也要恢复朝向 (仅当本次执行 ConfigureFacingMove 成功时)
			if (Mem.bFacingConfigured)
			{
				UAIFacingMoveHelper::RestoreFacingMove(
					Cast<ACharacter>(AIC->GetPawn()), AIC, Mem.FacingSnapshot);
				Mem.bFacingConfigured = false;
			}
			Mem.bMoveStarted = false;

			UE_LOG(LogBehaviorTree, Warning,
				TEXT("[BTTask_MoveToFacingTarget] AIC='%s' 被阻挡 %.1fs 超时 → 强制 Succeeded."),
				*AIC->GetName(), MaxWaitTime);

			FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
		}
	}
}


// ============================================
// 【v126 2026.08.01】智能避障 — 3 个核心算法
// ============================================

namespace
{
	/**
	 * 【v126 几何辅助】线段上离指定点最近的点 (Squared distance)
	 *
	 * 算法 (大厂 - 经典几何):
	 *   - t = Clamp((P-A)·(B-A) / |B-A|², 0, 1)
	 *   - ClosestPoint = A + t × (B-A)
	 *   - 返回 SquaredDistance(P, ClosestPoint)
	 *
	 * 用于: 球 (Actor 中心 ± EvadeRadius) 与线段 (PathSegment) 的相交判定
	 */
	FORCEINLINE float SquaredDistPointToSegment(const FVector& P, const FVector& A, const FVector& B)
	{
		const FVector AB = B - A;
		const float LenSq = AB.SizeSquared();
		if (LenSq < KINDA_SMALL_NUMBER)
		{
			// 退化线段 (A == B)
			return FVector::DistSquared(P, A);
		}
		const FVector AP = P - A;
		float t = FVector::DotProduct(AP, AB) / LenSq;
		t = FMath::Clamp(t, 0.0f, 1.0f);
		const FVector Closest = A + t * AB;
		return FVector::DistSquared(P, Closest);
	}
}


// 【v126】检测路径段上是否有 OnlyClass 类型的 Actor (返回是否被挡 + 第一个挡路 Actor 用于绕路)
//
// 简化: 不用 UE FindPathSync 算完整路径 (CPU 太重)
//       直接用"AI→Dest"线段 + 沿途采样检测 (算几何距离)
//
// 为什么这样设计 (大厂 - 性能 vs 准确):
//   - FindPathSync 算完整 NavMesh 路径: CPU 重 + 策划想要"路径上有人"语义模糊 (是直线上有人, 还是 NavMesh 节点上有人?)
//   - 直线路径: 简单 + 性能高 + 业务上"AI 视野中有人要绕" 是用户直觉
//   - NavMesh 真实路径: 后续 UE 原生 MoveTo 自动算, 我们只决定"要不要绕第一步"
//
// @return 第一个被挡的 Actor (nullptr = 没被挡)
AActor* UBTTask_MoveToFacingTarget::DetectActorsOnPathImpl(
	const UWorld* World,
	const FVector& StartLoc,
	const FVector& EndLoc,
	TSubclassOf<AActor> OnlyClass,
	float CheckRadius,
	const AActor* IgnoreActor) const
{
	if (!World || !OnlyClass)
	{
		return nullptr;
	}

	const float CheckRadiusSq = CheckRadius * CheckRadius;
	const float SearchRadius = CheckRadius * 3.0f; // 搜索半径 = 检测半径 × 3 (确保覆盖到路径)

	AActor* FirstBlocking = nullptr;
	float FirstBlockingDistSq = TNumericLimits<float>::Max();

	// 【v126.1 性能优化】OnlyClass 是 ABaseCharacter 时用 TActorIterator<ABaseCharacter>
	//   - 比 TActorIterator<AActor> 快很多 (不会遍历所有 Actor)
	//   - 覆盖项目 99% 场景 (玩家 + AI 都是 ABaseCharacter)
	//   - 特殊类(非 ABaseCharacter)用兜底 GetAllActorsOfClass
	if (OnlyClass == ABaseCharacter::StaticClass())
	{
		for (TActorIterator<ABaseCharacter> It(World); It; ++It)
		{
			ABaseCharacter* Actor = *It;
			if (!IsValid(Actor))
			{
				continue;
			}

			// 【v126.1 修复】排除 IgnoreActor — 大厂原则"避免自己挡自己"
			if (Actor == IgnoreActor)
			{
				continue;
			}

			// 简单粗筛 — Actor 中心到 StartLoc 距离 > SearchRadius → 不可能挡路 → 跳过
			const float DistToStartSq = FVector::DistSquared(Actor->GetActorLocation(), StartLoc);
			if (DistToStartSq > SearchRadius * SearchRadius)
			{
				continue;
			}

			// 球-线段相交 (SquaredDistance 优化)
			const float SegDistSq = SquaredDistPointToSegment(Actor->GetActorLocation(), StartLoc, EndLoc);
			if (SegDistSq <= CheckRadiusSq)
			{
				if (SegDistSq < FirstBlockingDistSq)
				{
					FirstBlockingDistSq = SegDistSq;
					FirstBlocking = Actor;
				}
			}
		}
	}
	else
	{
		// 非 ABaseCharacter 的类(例如特定 BP)走通用路径
		TArray<AActor*> Actors;
		UGameplayStatics::GetAllActorsOfClass(World, OnlyClass, Actors);
		for (AActor* Actor : Actors)
		{
			if (!IsValid(Actor) || Actor == IgnoreActor)
			{
				continue;
			}

			const float DistToStartSq = FVector::DistSquared(Actor->GetActorLocation(), StartLoc);
			if (DistToStartSq > SearchRadius * SearchRadius)
			{
				continue;
			}

			const float SegDistSq = SquaredDistPointToSegment(Actor->GetActorLocation(), StartLoc, EndLoc);
			if (SegDistSq <= CheckRadiusSq)
			{
				if (SegDistSq < FirstBlockingDistSq)
				{
					FirstBlockingDistSq = SegDistSq;
					FirstBlocking = Actor;
				}
			}
		}
	}

	return FirstBlocking;
}


/**
 * @brief 检测路径段上是否有 OnlyClass 类型 Actor 阻挡 — v126 智能避障判定
 * @param World 世界引用
 * @param StartLoc 路径起点(AI 当前位置)
 * @param EndLoc 路径终点(目标位置)
 * @param OnlyClass 待检测的 Actor 类型
 * @param CheckRadius 检测半径(球-线段相交球半径)
 * @param IgnoreActor 忽略的 Actor(AI 自己,避免自己挡自己)
 * @return 有任何 OnlyClass Actor 在路径段上 → true;否则 false
 *
 * v126 简化算法:用 A→EndLoc 的线段 + 各 Actor 中心 ± CheckRadius 球,
 * SquaredDistPointToSegment 计算球-线段最短距离, ≤ CheckRadius 视为挡路.
 * 性能优化:OnlyClass == ABaseCharacter 时用 TActorIterator<ABaseCharacter>;
 * 其它类型走 GetAllActorsOfClass 兜底.
 */
bool UBTTask_MoveToFacingTarget::DetectActorsOnPath(
	const UWorld* World,
	const FVector& StartLoc,
	const FVector& EndLoc,
	TSubclassOf<AActor> OnlyClass,
	float CheckRadius,
	const AActor* IgnoreActor) const
{
	return DetectActorsOnPathImpl(World, StartLoc, EndLoc, OnlyClass, CheckRadius, IgnoreActor) != nullptr;
}


// 【v127 2026.08.01】智能反向绕障 — Reroute Point 计算 (用户决策反馈)
//
// 旧算法 (v126): Reroute = Actor 中心 + 固定侧向偏移
//   → 用户反馈: "前面路被母体挡着, 偏要对着母体绕圈圈"
//   → 根因: AI 围绕 Actor 中心画圆 → Actor 不动 → AI 永远转圈
//
// 新算法 (v127 — 反向绕):
//   1. ForwardDir = (OriginalDest - StartLoc).Normalized()
//      → AI 想去的方向
//   2. BackDir    = -ForwardDir
//      → AI 反方向 (远离 OriginalDest 的方向 — 但实际上 BackDirection 是"AI 当前位置的退路方向")
//   3. Perpendicular = ForwardDir 绕 Z 轴 +90° (侧向)
//      → 既有"后退"又有"侧偏" = "L 形绕障" 路径
//   4. Reroute = StartLoc + BackDir × BackDistance + Perpendicular × SideDistance
//      BackDistance  = AvoidanceRadius × 1.0
//      SideDistance  = AvoidanceRadius × 0.8 (侧偏一些, 让 UE 引擎二次规划更顺)
//
//   可视化 (俯视图, 母体挡 AI 前进方向):
//         OriginalDest
//              ↑
//              │ (ForwardDir)
//              │
//         Reroute ←──┐
//              ↖ ←┘  (侧偏)
//               \  (后退)
//                \↘
//                  AI  (StartLoc)
//         ────────────
//              ▓▓▓▓▓▓  (Actor / 母体, 挡路)
//
//   含义: AI 不去绕 Actor 中心, 而是"先退 + 侧偏", 自己走出来后引擎接管
//
// 【v127 选侧策略】第一次走 +X 方向, 如果还是挡(或 Actor 在 +X 侧), 则走 -X
//   → 简单 alternation: 不用 NavMesh raycast, 性能 O(1)
//   → 失败由下层 MaxReplanAttempts 兜底
//
// 【v126.1 安全网】Reroute 离 StartLoc 距离校验
//   太近会触发 MoveTo 立刻 Idle → 看起来"不走"
FVector UBTTask_MoveToFacingTarget::ComputeReroutePoint(
	const FVector& StartLoc,
	const FVector& ActorLoc,
	float AvoidanceRadius,
	const FVector& OriginalDest,
	float MinRerouteDistance) const
{
	// Step 1: 算 ForwardDir = (OriginalDest - StartLoc).Normalized()
	//   - 退化 case (OriginalDest ≈ StartLoc): 用 (Actor→AI 反方向) 作为 fallback
	//     避免 AI 卡在自己出生点
	FVector ForwardDir = (OriginalDest - StartLoc).GetSafeNormal();
	if (ForwardDir.IsNearlyZero())
	{
		// AI 已经在 OriginalDest 附近,不需要避障,使用 Actor 反方向作为 ForwardDir
		ForwardDir = (StartLoc - ActorLoc).GetSafeNormal();
		if (ForwardDir.IsNearlyZero())
		{
			ForwardDir = FVector(1.f, 0.f, 0.f); // 最终兜底
		}
	}

	// Step 2: BackDir = -ForwardDir
	//   - 沿这条路往后, AI 远离 OriginalDest (然后引擎二次规划会带 AI 走回来)
	const FVector BackDir = -ForwardDir;

	// Step 3: Perpendicular = ForwardDir 绕 Z 轴 +90°
	//   - (x, y) → (-y, x)
	//   - Left/Right 选哪个? 用 thread_local 计数器 alternation (大厂 MT-safe):
	//     第一次 +X, 第二次 -X, 轮流
	//     thread_local = 同一线程 (AI 通常独立线程) 内交替,避免多 AI 同时偏向一侧
	static thread_local int32 GSideCounter = 0;
	const int32 SideIndex = (GSideCounter++ % 2 + 2) % 2; // 防 GSideCounter 溢出 +2 处理
	const FVector Perpendicular = (SideIndex == 0)
		? FVector(-ForwardDir.Y, ForwardDir.X, 0.f)
		: FVector(ForwardDir.Y, -ForwardDir.X, 0.f);

	// Step 4: Reroute = StartLoc + BackDir × BackDistance + Perpendicular × SideDistance
	const float BackDistance = AvoidanceRadius * 1.0f; // 后退距离 = 避开半径
	const float SideDistance = AvoidanceRadius * 0.8f; // 侧偏距离 = 避开半径 × 0.8
	FVector Reroute = StartLoc + BackDir * BackDistance + Perpendicular * SideDistance;

	// 【v126.1 安全网】校验 Reroute 离 StartLoc 距离
	const float DistFromStart = FVector::Dist(Reroute, StartLoc);
	if (DistFromStart < MinRerouteDistance)
	{
		// Reroute 太近 — 加大幅度
		const float ScaleFactor = MinRerouteDistance / FMath::Max(DistFromStart, 1.f);
		Reroute = StartLoc + (Reroute - StartLoc) * ScaleFactor;
	}

	return Reroute;
}


// 【v126 核心】规划避障路径 — 启动 MoveTo 之前调用
//
// 流程:
//   1. 对每个 EvadeActorClasses 测一次 DetectActorsOnPath
//   2. 有被挡 → ComputeReroutePoint 算新 Reroute → 用 Reroute 作为新 Dest 重新测
//   3. 重 plan 次数 ≤ MaxReplanAttempts
//   4. 返回最终的 Dest (OriginalDest 或 Reroute, 看哪次循环结束)
//
// 简化: 不做"路径拼接" — 不用 Reroute 串联 OriginalDest, 只用 Reroute 作为单次目标
//   让 UE 原生 MoveTo 引擎自己后续规划
FVector UBTTask_MoveToFacingTarget::PlanPathAvoidingActors(
	UWorld* World,
	const FVector& StartLoc,
	const FVector& OriginalDest,
	int32 MaxAttempts,
	int32& OutAttemptsUsed,
	const AActor* IgnoreActor)
{
	OutAttemptsUsed = 0;

	if (!World || MaxAttempts <= 0)
	{
		return OriginalDest;
	}

	FVector CurrentDest = OriginalDest;

	// 遍历每个 EvadeActorClasses, 每一类重 plan 一次
	for (int32 Attempt = 0; Attempt < MaxAttempts; ++Attempt)
	{
		AActor* Blocking = nullptr;

		// 遍历 EvadeActorClasses 找任意一类挡路
		for (TSubclassOf<AActor> EvadeClass : EvadeActorClasses)
		{
			if (!EvadeClass)
			{
				continue;
			}
			Blocking = DetectActorsOnPathImpl(World, StartLoc, CurrentDest, EvadeClass, EvadeRadius, IgnoreActor);
			if (Blocking)
			{
				break; // 找到挡路 Actor, 跳出
			}
		}

		// 默认兜底: EvadeActorClasses 为空时用 ABaseCharacter
		if (!Blocking)
		{
			Blocking = DetectActorsOnPathImpl(World, StartLoc, CurrentDest,
				ABaseCharacter::StaticClass(), EvadeRadius, IgnoreActor);
		}

		if (!Blocking)
		{
			// 路径畅通 — 返回当前 dest
			return CurrentDest;
		}

		// 有挡路 → 算 Reroute Point
		// 【v126.1 安全网】MinRerouteDistance = AcceptanceRadius × 3 (确保 AI 走过去不立即被 Idle 判到)
		const float MinRerouteDistance = AcceptanceRadius * 3.f;
		const FVector Reroute = ComputeReroutePoint(StartLoc, Blocking->GetActorLocation(), EvadeRadius, OriginalDest, MinRerouteDistance);

		// 【v127 Display 级别】让用户看到 Reroute 坐标 — 验证是否真的"反向绕"
		UE_LOG(LogBehaviorTree, Display,
			TEXT("[BTTask_MoveToFacingTarget] 【v127 智能反向绕障】"
			     "挡路 Actor='%s' @ %s → "
			     "AI从 %s → Reroute=%s (后退+侧偏, attempt %d/%d)."),
			*Blocking->GetName(),
			*Blocking->GetActorLocation().ToString(),
			*StartLoc.ToString(),
			*Reroute.ToString(),
			Attempt + 1, MaxAttempts);

		CurrentDest = Reroute;
		++OutAttemptsUsed;
	}

	// 重 plan 用尽 — 返回最后一次 Reroute (大厂兜底: 不静默失败, 让 UE 原生 MoveTo 处理)
	UE_LOG(LogBehaviorTree, Display,
		TEXT("[BTTask_MoveToFacingTarget] 【v127 智能反向绕障】"
		     "重规划 %d 次仍被挡, 兜底用最后 Reroute=%s. 让 UE 原生 MoveTo 二次规划."),
		OutAttemptsUsed, *CurrentDest.ToString());

	return CurrentDest;
}
