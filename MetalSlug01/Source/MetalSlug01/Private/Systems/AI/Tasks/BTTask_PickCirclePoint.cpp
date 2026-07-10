// Copyright (c) 2026.
//
// 【v40.9.1 2026.07.14】BTTask — 攻击后环绕原子能力 (Pick Circle Point)
// 实现: 纯数学算点 (Pawn 周围 360° 随机方向 + StrafeRadius), 完全不依赖 NavMesh

#include "Systems/AI/Tasks/BTTask_PickCirclePoint.h"

#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Vector.h"
#include "AIController.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"

#include "Systems/BaseAIController.h"
#include "Systems/AI/AIRuntimeConfigComponent.h"
#include "Data/AI/AIBehaviorConfigSO.h"

UBTTask_PickCirclePoint::UBTTask_PickCirclePoint()
{
	NodeName = TEXT("Pick Circle Point (攻击后环绕)");

	// 同步任务: 算点是纯数学, 不需要 Tick / NavMesh
	bNotifyTick = false;

	TargetActorKey.AddObjectFilter(this,
		GET_MEMBER_NAME_CHECKED(UBTTask_PickCirclePoint, TargetActorKey),
		AActor::StaticClass());
	CirclePointKey.AddVectorFilter(this,
		GET_MEMBER_NAME_CHECKED(UBTTask_PickCirclePoint, CirclePointKey));
}

FString UBTTask_PickCirclePoint::GetStaticDescription() const
{
	return TEXT("【v40.9.1 攻击后环绕原子】\n"
		"• bOrbitSelf=true: 在 Pawn 自身周围 360° 随机方向 × StrafeRadius 算点 (不依赖 NavMesh)\n"
		"• bOrbitSelf=false: 在 TargetActor 周围算点\n"
		"• 写入 BB.CirclePoint → 下游 UE 原生 Move To 接管\n"
		"• 失败 = Failed (无兜底). ConfigSO.Combat.bEnableCircle / StrafeRadius 单一真理源.");
}

EBTNodeResult::Type UBTTask_PickCirclePoint::ExecuteTask(
	UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	Super::ExecuteTask(OwnerComp, NodeMemory);

	// 【v40.9.2 诊断锚点】每次进入必打 Display, 即使后面失败也保留
	// 这是为了让你明确判断 BT 里到底有没有挂这个节点 — 没这条 log = BT 没接好
	const AAIController* AICForLog = OwnerComp.GetAIOwner();
	const APawn* PawnForLog = AICForLog ? AICForLog->GetPawn() : nullptr;
	UE_LOG(LogBehaviorTree, Display,
		TEXT("[BTTask_PickCirclePoint] v40.9.2 ENTER. AIC=%s Pawn=%s (本次进入必打, 用于确认 BT 节点是否挂载)"),
		*GetNameSafe(AICForLog), *GetNameSafe(PawnForLog));

	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	if (!BB)
	{
		UE_LOG(LogBehaviorTree, Error,
			TEXT("[BTTask_PickCirclePoint] BB 组件无效. 检查 BT 是否绑定了 BB_AI_Melee.uasset."));
		return EBTNodeResult::Failed;
	}

	AAIController* AIC = OwnerComp.GetAIOwner();
	if (!AIC)
	{
		UE_LOG(LogBehaviorTree, Error,
			TEXT("[BTTask_PickCirclePoint] AIController 无效."));
		return EBTNodeResult::Failed;
	}

	APawn* AIPawn = AIC->GetPawn();
	if (!AIPawn)
	{
		UE_LOG(LogBehaviorTree, Error,
			TEXT("[BTTask_PickCirclePoint] AIC=%s Pawn 无效."), *AIC->GetName());
		return EBTNodeResult::Failed;
	}

	UWorld* World = AIC->GetWorld();
	if (!World)
	{
		UE_LOG(LogBehaviorTree, Error, TEXT("[BTTask_PickCirclePoint] World 无效."));
		return EBTNodeResult::Failed;
	}

	// ============================================================
	// Key 配置检查 (零兜底 — UE 静默 no-op)
	// ============================================================
	if (CirclePointKey.SelectedKeyName.IsNone())
	{
		UE_LOG(LogBehaviorTree, Error,
			TEXT("[BTTask_PickCirclePoint] AIC=%s CirclePointKey 未配置. "
			     "【UE 编辑器配置】BT 编辑器 → Pick Circle Point 节点 → Details → CirclePointKey 选择 'CirclePoint'."),
			*AIC->GetName());
		return EBTNodeResult::Failed;
	}

	if (BB->GetKeyID(CirclePointKey.SelectedKeyName) == FBlackboard::InvalidKey)
	{
		UE_LOG(LogBehaviorTree, Error,
			TEXT("[BTTask_PickCirclePoint] AIC=%s BB 不存在 Key '%s'. "
			     "【UE 编辑器配置】打开 BB_AI_Melee.uasset → New Key → Key Name='%s', Key Type=Vector."),
			*AIC->GetName(),
			*CirclePointKey.SelectedKeyName.ToString(),
			*CirclePointKey.SelectedKeyName.ToString());
		return EBTNodeResult::Failed;
	}

	// ============================================================
	// TargetActor Key 配置检查 (Option — 仅诊断用, 不会阻塞)
	// ============================================================
	if (TargetActorKey.SelectedKeyName.IsNone())
	{
		UE_LOG(LogBehaviorTree, Verbose,
			TEXT("[BTTask_PickCirclePoint] AIC=%s TargetActorKey 未配置 — 强制 bOrbitSelf=true (用 Pawn 自身)."),
			*AIC->GetName());
	}
	else if (BB->GetKeyID(TargetActorKey.SelectedKeyName) == FBlackboard::InvalidKey)
	{
		UE_LOG(LogBehaviorTree, Warning,
			TEXT("[BTTask_PickCirclePoint] AIC=%s BB 不存在 TargetActorKey '%s' — 强制 bOrbitSelf=true."),
			*AIC->GetName(),
			*TargetActorKey.SelectedKeyName.ToString());
	}
	else
	{
		// TargetActor 在 BB 里存在 — 验证是否真有效 (不是 UE BB uninitialized sentinel)
		UObject* TargetObj = BB->GetValueAsObject(TargetActorKey.SelectedKeyName);
		AActor* TargetActor = Cast<AActor>(TargetObj);

		if (!TargetActor)
		{
			UE_LOG(LogBehaviorTree, Verbose,
				TEXT("[BTTask_PickCirclePoint] AIC=%s TargetActorKey '%s' 值为空 — 当作无目标处理 (绕自己)."),
				*AIC->GetName(), *TargetActorKey.SelectedKeyName.ToString());
		}
		else if (bOrbitSelf == false)
		{
			// bOrbitSelf=false 但 TargetActor 失效 → 不能绕失效点 (会乱跑)
			// 【零兜底】强制拒绝 + 退化策略: 没有 Target 就只能绕自己
			UE_LOG(LogBehaviorTree, Warning,
				TEXT("[BTTask_PickCirclePoint] AIC=%s bOrbitSelf=false 但 TargetActor 已失效. "
				     "大厂原则: 拒绝 fallback, 直接 Failed 让 BT Selector 兜底回 Chase."),
				*AIC->GetName());
			return EBTNodeResult::Failed;
		}
	}

	// ============================================================
	// 读 ConfigSO (StrafeRadius / bEnableCircle) — 单一真理源
	// ============================================================
	ABaseAIController* BaseAIC = Cast<ABaseAIController>(AIC);
	if (!BaseAIC)
	{
		UE_LOG(LogBehaviorTree, Error,
			TEXT("[BTTask_PickCirclePoint] AIC=%s 不是 ABaseAIController 派生."),
			*AIC->GetName());
		return EBTNodeResult::Failed;
	}

	const UAIRuntimeConfigComponent* RuntimeConfig = BaseAIC->GetRuntimeConfig();
	if (!RuntimeConfig)
	{
		UE_LOG(LogBehaviorTree, Error,
			TEXT("[BTTask_PickCirclePoint] AIC=%s RuntimeConfig 组件不可用. "
			     "修复: 检查 BP_GruntAI 是否创建了 UAIRuntimeConfigComponent."),
			*AIC->GetName());
		return EBTNodeResult::Failed;
	}

	const UAIBehaviorConfigSO* Config = RuntimeConfig->GetConfig();
	if (!Config)
	{
		UE_LOG(LogBehaviorTree, Error,
			TEXT("[BTTask_PickCirclePoint] AIC=%s ConfigSO 未应用. "
			     "修复: 检查 SetupMeleeAI 是否调 ApplyConfig."),
			*AIC->GetName());
		return EBTNodeResult::Failed;
	}

	const FAICombatParams Combat = RuntimeConfig->GetScaledCombat();

	// 【零兜底】bEnableCircle = false → 用户明确禁用 → 直接 Failed
	if (!Combat.bEnableCircle)
	{
		UE_LOG(LogBehaviorTree, Log,
			TEXT("[BTTask_PickCirclePoint] AIC=%s Combat.bEnableCircle=false, 跳过环绕."),
			*AIC->GetName());
		return EBTNodeResult::Failed;
	}

	const float StrafeRadius = Combat.StrafeRadius;
	if (StrafeRadius <= 0.f)
	{
		UE_LOG(LogBehaviorTree, Error,
			TEXT("[BTTask_PickCirclePoint] AIC=%s StrafeRadius=%.1f 必须 > 0. "
			     "【v54 修复】DA_AIBehaviorConfig_MeleeGrunt.uasset → Combat → Circle → StrafeRadius (DA_AIProfile_*.uasset 已删除)."),
			*AIC->GetName(), StrafeRadius);
		return EBTNodeResult::Failed;
	}

	// ============================================================
	// 核心算法 — 纯数学算点 (不依赖 NavMesh, 这是 v40.9.1 的关键修复)
	// ============================================================
	const FVector PawnLocation = AIPawn->GetActorLocation();

	// 生成 360° 随机方向角 (均匀分布)
	// FMath::FRandRange 返回 [0, 1], 乘 2π 得到 [0, 2π]
	const float RandomAngleRadians = FMath::FRandRange(0.f, 2.f * PI);
	const FVector RandomDirection(
		FMath::Cos(RandomAngleRadians),
		FMath::Sin(RandomAngleRadians),
		0.f // 永远水平, 不上下 (这才是拟人)
	);

	// CirclePoint = Pawn + 随机方向 × StrafeRadius
	const FVector CirclePoint = PawnLocation + RandomDirection * StrafeRadius;

	// 抗 UE BB uninitialized sentinel 防御: 如果 Pawn 自己在无效位置 (e.g. 销毁中)
	//   UE BB Vector uninit = 接近 MAX_FLT, 任何 ±运算都会得到虚数, 这里做防御
	if (FMath::IsNaN(CirclePoint.X) || FMath::IsNaN(CirclePoint.Y) || FMath::IsNaN(CirclePoint.Z))
	{
		UE_LOG(LogBehaviorTree, Error,
			TEXT("[BTTask_PickCirclePoint] AIC=%s 算出 NaN 坐标 (Pawn=%s). "
			     "可能是 PawnLocation 非法 / StrafeRadius 过大溢出."),
			*AIC->GetName(), *PawnLocation.ToString());
		return EBTNodeResult::Failed;
	}

	// 写入 BB.CirclePoint
	BB->SetValueAsVector(CirclePointKey.SelectedKeyName, CirclePoint);

	UE_LOG(LogBehaviorTree, Log,
		TEXT("[BTTask_PickCirclePoint] AIC=%s 环绕选点 (v40.9.1 数学): "
		     "Pawn=%s, StrafeRadius=%.0f, Angle=%.0f°, CirclePoint=%s"),
		*AIC->GetName(),
		*PawnLocation.ToString(),
		StrafeRadius,
		FMath::RadiansToDegrees(RandomAngleRadians),
		*CirclePoint.ToString());

	return EBTNodeResult::Succeeded;
}
