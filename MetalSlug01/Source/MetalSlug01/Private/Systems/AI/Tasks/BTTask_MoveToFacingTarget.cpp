// Copyright (c) 2026.
//
// 【大厂架构 v40.10 2026.07.31】BTTask_MoveToFacingTarget 实现
//
// 详见头文件注释. 实现要点:
//   1. BB Key 双重检查 (SelectedKeyName + GetKeyID != InvalidKey)
//   2. UAIFacingMoveHelper::ConfigureFacingMove 处理朝向机制
//   3. MoveToLocation 异步启动
//   4. TickTask 检查 PathFollowingComponent 状态 (Idle / Waiting)
//   5. 完成/超时/Abort 时 RestoreFacingMove (Helper 幂等)

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

#include "Systems/AI/AIFacingMoveHelper.h"

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

FString UBTTask_MoveToFacingTarget::GetStaticDescription() const
{
	return FString::Printf(TEXT("【v40.11 大厂重构】边移动边面向目标的通用 Task.\n"
		"目标 Actor: BB.%s (面朝)\n"
		"目的地: BB.%s (Vector 或 Object AActor, 运行时按 Key 类型分支)\n"
		"AcceptanceRadius: %.0fcm\n"
		"被阻挡超时: %.1fs → 强制 Succeeded\n"
		"朝向机制: 复用 UAIFacingMoveHelper (单一真理源).\n"
		"【修复回头走】移动期间关 OrientRotationToMovement."),
		*TargetActorKey.SelectedKeyName.ToString(),
		*MoveLocationKey.SelectedKeyName.ToString(),
		AcceptanceRadius,
		MaxWaitTime);
}

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

	// 【零兜底 2/2】TargetActor 必须有效
	UObject* TargetObj = BB->GetValueAsObject(TargetActorKey.SelectedKeyName);
	AActor* TargetActor = Cast<AActor>(TargetObj);
	if (!TargetActor || TargetActor->IsActorBeingDestroyed())
	{
		UE_LOG(LogBehaviorTree, Error,
			TEXT("[BTTask_MoveToFacingTarget] AIC='%s' TargetActor '%s' 失效. "
			     "【修复路径】1) 在 BT 序列前置 Decorator: TargetActor Is Set "
			     "2) 检查 BB.TargetActor Key 的写入者."),
			*AIC->GetName(),
			*TargetActorKey.SelectedKeyName.ToString());
		return EBTNodeResult::Failed;
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
	// ============================================
	const bool bFacingConfigured = UAIFacingMoveHelper::ConfigureFacingMove(
		Cast<ACharacter>(AIPawn),
		AIC,
		TargetActor,
		Mem.FacingSnapshot);

	if (!bFacingConfigured)
	{
		// Helper 失败 (Character/Target 无效) → 拒绝 MoveTo, 否则回头走
		// Helper 内部已 Log Error 解释根因
		return EBTNodeResult::Failed;
	}

	// ============================================
	// 启动 MoveTo
	// ============================================
	if (!StartMoveTo(OwnerComp, MoveLocation))
	{
		// MoveTo 启动失败, 恢复朝向 (Helper 内幂等)
		UAIFacingMoveHelper::RestoreFacingMove(
			Cast<ACharacter>(AIPawn), AIC, Mem.FacingSnapshot);
		return EBTNodeResult::Failed;
	}

	Mem.bMoveStarted = true;

	UE_LOG(LogBehaviorTree, Log,
		TEXT("[BTTask_MoveToFacingTarget] AIC='%s' Start MoveTo. "
		     "Target='%s' (face), MoveLocation=%s, AcceptanceRadius=%.0f."),
		*AIC->GetName(),
		*TargetActor->GetName(),
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
		FTaskMemory& Mem = GetTaskMemory(NodeMemory);
		UAIFacingMoveHelper::RestoreFacingMove(
			Cast<ACharacter>(AIC->GetPawn()), AIC, Mem.FacingSnapshot);

		Mem.bMoveStarted = false;
	}

	return EBTNodeResult::Aborted;
}

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
		// 到达目的地 — 恢复朝向 (Helper 内幂等)
		FTaskMemory& Mem = GetTaskMemory(NodeMemory);
		UAIFacingMoveHelper::RestoreFacingMove(
			Cast<ACharacter>(AIC->GetPawn()), AIC, Mem.FacingSnapshot);
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

			// 超时也要恢复朝向
			UAIFacingMoveHelper::RestoreFacingMove(
				Cast<ACharacter>(AIC->GetPawn()), AIC, Mem.FacingSnapshot);
			Mem.bMoveStarted = false;

			UE_LOG(LogBehaviorTree, Warning,
				TEXT("[BTTask_MoveToFacingTarget] AIC='%s' 被阻挡 %.1fs 超时 → 强制 Succeeded."),
				*AIC->GetName(), MaxWaitTime);

			FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
		}
	}
}
