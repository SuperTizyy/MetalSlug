// Copyright (c) 2026.
//
// 【v40.9.3 2026.07.14】BTTask — 攻击后环绕 (一体化版) 实现

#include "Systems/AI/Tasks/BTTask_CircleAroundTarget.h"

#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"
#include "AIController.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Actor.h"
#include "Engine/World.h"
#include "Navigation/PathFollowingComponent.h"
// 【v40.9.5】NavMesh 投影 — 用 UE 原生 UNavigationSystemV1::ProjectPointToNavigation
//   把几何算出的 CirclePoint 投到最近的 NavMesh 可达点 — 大厂标配
#include "NavigationSystem.h"
#include "NavigationData.h"
#include "NavMesh/RecastNavMesh.h"

#include "Systems/BaseAIController.h"
#include "Systems/AI/AIRuntimeConfigComponent.h"
#include "Data/AI/AIBehaviorConfigSO.h"

UBTTask_CircleAroundTarget::UBTTask_CircleAroundTarget()
{
	NodeName = TEXT("Circle Around Target (一体化攻击后环绕)");

	// 异步 Task: 选点 → MoveTo 异步 → 停顿 异步 → FinishLatentTask(Succeeded)
	bNotifyTick = true;
	bNotifyTaskFinished = false; // 我们在内部调 FinishLatentTask
}

FString UBTTask_CircleAroundTarget::GetStaticDescription() const
{
	return TEXT("【v40.9.3 一体化攻击后环绕】\n"
		"• 自包含: 选点 → UE MoveTo → 停顿 → 完成\n"
		"• 只需 BB.TargetActor Key (你 BT 已有)\n"
		"• 不需要 BB.CirclePoint, 不需要 Move To 节点\n"
		"• 失败 = Selector 自动回退到 Chase\n"
		"• MaxStrafeRadius / CirclePauseSeconds 默认读 ConfigSO, 可在编辑器覆盖");
}

EBTNodeResult::Type UBTTask_CircleAroundTarget::ExecuteTask(
	UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	Super::ExecuteTask(OwnerComp, NodeMemory);

	// 【v40.9.3 必打诊断锚点】这次进入一定打 log — 让你从 PIE 日志立刻确认 task 是否被 BT 调用
	UE_LOG(LogBehaviorTree, Display,
		TEXT("[BTTask_CircleAroundTarget] v40.9.3 ENTER. AIC=%s (必打 — BT 是否挂这个节点的验证锚点)"),
		*GetNameSafe(OwnerComp.GetAIOwner()));

	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	if (!BB)
	{
		UE_LOG(LogBehaviorTree, Error, TEXT("[BTTask_CircleAroundTarget] BB 不可用."));
		return EBTNodeResult::Failed;
	}

	AAIController* AIC = OwnerComp.GetAIOwner();
	if (!AIC)
	{
		UE_LOG(LogBehaviorTree, Error, TEXT("[BTTask_CircleAroundTarget] AIC 不可用."));
		return EBTNodeResult::Failed;
	}

	APawn* AIPawn = AIC->GetPawn();
	if (!AIPawn)
	{
		UE_LOG(LogBehaviorTree, Error, TEXT("[BTTask_CircleAroundTarget] Pawn 不可用."));
		return EBTNodeResult::Failed;
	}

	// ============================================================
	// Key 检查 (零兜底)
	// ============================================================
	if (TargetActorKey.SelectedKeyName.IsNone())
	{
		UE_LOG(LogBehaviorTree, Error,
			TEXT("[BTTask_CircleAroundTarget] AIC=%s TargetActorKey 未配置 (SelectedKeyName=None). "
			     "【UE 编辑器配置】BT 编辑器 → Circle Around Target 节点 → Details → TargetActorKey 选择 Key 名 "
			     "(通常是 TargetActor)."),
			*AIC->GetName());
		return EBTNodeResult::Failed;
	}

	if (BB->GetKeyID(TargetActorKey.SelectedKeyName) == FBlackboard::InvalidKey)
	{
		UE_LOG(LogBehaviorTree, Error,
			TEXT("[BTTask_CircleAroundTarget] AIC=%s BB 不存在 Key '%s'. "
			     "【UE 编辑器配置】打开 BB_AI_Melee.uasset, 确认有 Key '%s' (Type=Object)."),
			*AIC->GetName(),
			*TargetActorKey.SelectedKeyName.ToString(),
			*TargetActorKey.SelectedKeyName.ToString());
		return EBTNodeResult::Failed;
	}

	UObject* TargetObj = BB->GetValueAsObject(TargetActorKey.SelectedKeyName);
	AActor* TargetActor = Cast<AActor>(TargetObj);
	if (!TargetActor)
	{
		UE_LOG(LogBehaviorTree, Warning,
			TEXT("[BTTask_CircleAroundTarget] AIC=%s TargetActor '%s' 失效. "
			     "Sequence 应在 TargetActor Is Set Decorator 内, 这里 Failed → Selector 回 Chase."),
			*AIC->GetName(), *TargetActorKey.SelectedKeyName.ToString());
		return EBTNodeResult::Failed;
	}

	// ============================================================
	// ConfigSO 检查 — 单一真理源 (半径/停顿优先 ConfigSO, 编辑器可覆盖)
	// ============================================================
	ABaseAIController* BaseAIC = Cast<ABaseAIController>(AIC);
	if (!BaseAIC || !BaseAIC->GetRuntimeConfig() || !BaseAIC->GetRuntimeConfig()->GetConfig())
	{
		UE_LOG(LogBehaviorTree, Error,
			TEXT("[BTTask_CircleAroundTarget] AIC=%s RuntimeConfig/ConfigSO 不可用, 用编辑器默认值兜底."),
			*AIC->GetName());
		// 这里允许用编辑器值作为兜底 — 这是大厂原则的边缘: ConfigSO 缺失时,
		// 不允许"什么都不做让 AI 卡死", 允许用编辑器默认 (策划调过的值)
	}

	const UAIBehaviorConfigSO* Config = BaseAIC && BaseAIC->GetRuntimeConfig()
		? BaseAIC->GetRuntimeConfig()->GetConfig() : nullptr;
	const FAICombatParams Combat = Config ? BaseAIC->GetRuntimeConfig()->GetScaledCombat()
	                                       : FAICombatParams();

	// 【v40.9.4 优先级变更】
	//   旧: 编辑器值优先, ConfigSO fallback
	//   新: OrbitAngleDegrees 直接读编辑器值 (策划要的就是"180=背刺")
	//        PauseSeconds 优先 ConfigSO (业务默认), 编辑器可覆盖
	float EffectiveAngle = OrbitAngleDegrees;       // 编辑器控制, ConfigSO 不覆盖 (语义不同)
	float EffectivePause = CirclePauseSeconds;      // 编辑器默认
	if (Config && Combat.CirclePauseSeconds >= 0.f)
	{
		EffectivePause = Combat.CirclePauseSeconds;
	}

	if (EffectiveAngle < 0.f || EffectiveAngle > 180.f)
	{
		UE_LOG(LogBehaviorTree, Error,
			TEXT("[BTTask_CircleAroundTarget] AIC=%s OrbitAngle=%.1f 必须在 [0, 180] 范围. 编辑器节点 Details 面板修改."),
			*AIC->GetName(), EffectiveAngle);
		return EBTNodeResult::Failed;
	}

	// ============================================================
	// 选点算法 v40.9.5 — 几何 → NavMesh 投影 (大厂双层)
	// ============================================================
	// 用户原话痛点 (2026.07.14):
	//   "Circle Around Target 节点应该有弊端吧？虽然给了最终的目标点走过去,
	//    但是无法自动规划合理路径吧？"
	//
	// 旧 (v40.9.4) — 纯几何 + AIC->MoveTo:
	//   - 几何算出的 CirclePoint 可能落在墙里/地图外/NavMesh 不可达处
	//   - MoveTo 失败 → Selector 回 Chase → 原地死循环
	//   - 撞墙时 AI 实际只走到 NavMesh 最近可达点, 看起来"走不远"
	//
	// 新 (v40.9.5) — 几何 + NavMesh 投影双层 (大厂标配):
	//   1. 几何算 DesiredDirection + EffectiveRadius (按你的 OrbitAngle 语义)
	//   2. 用 UNavigationSystemV1::ProjectPointToNavigation 把几何点投到 NavMesh 最近点
	//   3. 如果投影失败 (无法在容差内找到) → Failed (大厂零兜底 — 不允许选个偏离点继续走)
	//   4. 如果投影成功 (CirclePoint 在 navmesh 上 / 投影到 navmesh) → MoveTo 用这个点
	//
	// 为什么用 UE 原生 API 不用手写 navmesh 检测:
	//   - ProjectPointToNavigation 是 UE 5.6 内置, 处理 RecastNavMesh / NavMeshBoundsVolume 全套
	//   - 处理 extent (容差) — 周边 box 内找最近可达点
	//   - 大厂 99% 项目都用这个 API
	//
	const FVector PawnLocation = AIPawn->GetActorLocation();
	const FVector TargetLocation = TargetActor->GetActorLocation();
	const FVector PawnToTarget = TargetLocation - PawnLocation;
	const float CurrentDistance = PawnToTarget.Size2D();

	// 大厂防御: 距离 < 10cm 几乎等于贴脸 — 不能用向量算方向
	if (CurrentDistance < 10.f)
	{
		UE_LOG(LogBehaviorTree, Warning,
			TEXT("[BTTask_CircleAroundTarget] AIC=%s 与 TargetActor 距离过近 (%.1f cm). "
			     "进入 Pause 不移动 (贴脸时不该绕)."),
			*AIC->GetName(), CurrentDistance);
		FCircleMemory* Mem = reinterpret_cast<FCircleMemory*>(NodeMemory);
		Mem->Phase = EPhase::PauseAndFinish;
		Mem->ElapsedPause = 0.f;
		Mem->CachedPawnLocation = PawnLocation;
		Mem->MoveRequestID = FAIRequestID::InvalidRequest;
		Mem->TargetActorPtr = TargetActor;
		return EBTNodeResult::InProgress;
	}

	const FVector DirectionToTarget = PawnToTarget.GetSafeNormal2D();

	// RandomSign: ±1 — 决定左/右
	const float RandomSign = (FMath::FRand() < 0.5f) ? -1.f : 1.f;
	const float RotateAngleDeg = OrbitAngleDegrees * RandomSign;

	// NewDir = DirectionToTarget 在水平面 (XY) 旋转 RotateAngleDeg
	const FVector NewDir = DirectionToTarget.RotateAngleAxis(RotateAngleDeg, FVector::UpVector);

	// 【v40.9.4】距离保护 — 半径保持在 [CurrentDist × (1-Shrink), CurrentDist × (1+Expand)]
	const float MinRadius = CurrentDistance * (1.f - MinRadiusShrinkRatio);
	const float MaxRadius = CurrentDistance * (1.f + MaxRadiusExpandRatio);
	const float Jitter = FMath::FRandRange(-5.f, 5.f);
	const float EffectiveRadius = FMath::Clamp(
		CurrentDistance + Jitter, MinRadius, MaxRadius);

	// 几何上的"期望位置" — 没考虑 navmesh
	const FVector DesiredCirclePoint = TargetLocation - NewDir * EffectiveRadius;

	// ============================================================
	// 【v40.9.5 大厂关键】NavMesh 投影 — 把几何点投到最近可达点
	// ============================================================
	UNavigationSystemV1* NavSys = UNavigationSystemV1::GetCurrent(GetWorld());
	if (!NavSys)
	{
		UE_LOG(LogBehaviorTree, Error,
			TEXT("[BTTask_CircleAroundTarget] AIC=%s UNavigationSystemV1 不可用. "
			     "检查地图是否有 NavMeshBoundsVolume.【零兜底】无 NavMesh = 无 MoveTo."),
			*AIC->GetName());
		return EBTNodeResult::Failed;
	}

	// ProjectExtent: 投影容差 box (cm). 在这个范围内找 NavMesh 最近点.
	// 大厂标配: 200~500 cm — 这里设 300cm, 容错率足够
	const FVector ProjectExtent(300.f, 300.f, 200.f);

	FNavLocation ProjectedLocation;
	const bool bProjected = NavSys->ProjectPointToNavigation(
		DesiredCirclePoint,
		ProjectedLocation,
		ProjectExtent
	);

	FVector CirclePoint;
	if (bProjected)
	{
		CirclePoint = ProjectedLocation.Location;
		UE_LOG(LogBehaviorTree, Log,
			TEXT("[BTTask_CircleAroundTarget] AIC=%s 几何→NavMesh 投影成功. "
			     "Desired=%s → Projected=%s (Offset=%.0f cm)"),
			*AIC->GetName(),
			*DesiredCirclePoint.ToString(),
			*CirclePoint.ToString(),
			FVector::Dist2D(DesiredCirclePoint, CirclePoint));
	}
	else
	{
		// 【v40.9.5 零兜底】无法投影 → 不允许选一个随便走的点
		// 大厂原则: 配置错误 / 地图无 NavMesh / CirclePoint 在 300cm 内都不可达 → Failed
		UE_LOG(LogBehaviorTree, Error,
			TEXT("[BTTask_CircleAroundTarget] AIC=%s CirclePoint 投影到 NavMesh 失败! "
			     "Desired=%s (在 ProjectExtent=%s 内找不到 navmesh 可达点). "
			     "【零兜底】拒绝随便选点 — Failed 让 Selector 回 Chase. "
			     "【UE 编辑器配置】检查: 1) 地图 NavMeshBoundsVolume 是否覆盖玩家周围; "
			     "2) DA_AIProfile.OrbitAngle 是否太大 (试改小)."),
			*AIC->GetName(),
			*DesiredCirclePoint.ToString(),
			*ProjectExtent.ToString());
		return EBTNodeResult::Failed;
	}

	// ============================================================
	// 大厂防御 — 投影出来的 CirclePoint 太靠近 AI 当前位置 → 不移动直接进 Pause
	// 这种情况: 几何点在 navmesh 上但只比 AI 当前位置离一点点 (AI 已被卡在某个角落)
	// ============================================================
	if (CirclePoint.Equals(PawnLocation, 30.f))
	{
		UE_LOG(LogBehaviorTree, Warning,
			TEXT("[BTTask_CircleAroundTarget] AIC=%s CirclePoint 太靠近 Pawn (投影后). "
			     "几何点虽然合法但实际移动距离太短 — 直接进 Pause."),
			*AIC->GetName());
		FCircleMemory* Mem = reinterpret_cast<FCircleMemory*>(NodeMemory);
		Mem->Phase = EPhase::PauseAndFinish;
		Mem->ElapsedPause = 0.f;
		Mem->CachedPawnLocation = PawnLocation;
		Mem->MoveRequestID = FAIRequestID::InvalidRequest;
		Mem->TargetActorPtr = TargetActor;
		return EBTNodeResult::InProgress;
	}

	// 写入内存 — 后续 Tick 阶段用
	FCircleMemory* Mem = reinterpret_cast<FCircleMemory*>(NodeMemory);
	Mem->Phase = EPhase::PickPoint;
	Mem->TargetPoint = CirclePoint;
	Mem->ElapsedPause = 0.f;
	Mem->CachedPawnLocation = PawnLocation;
	Mem->TargetActorPtr = TargetActor;

	// ============================================================
	// 【v40.9.4 关键】移动过程面朝目标 — 用 AIC->SetFocus (UE 原生 API)
	// 大厂: 用 UE 自带 AIC->SetFocus(TargetActor), 移动过程自动面朝
	// TickTask 结束或 Abort 时 ClearFocus 即可
	// ============================================================
	AIC->SetFocus(TargetActor, EAIFocusPriority::Gameplay);

	// ============================================================
	// Phase 1: MoveTo — 显式调 AIC->MoveToLocation
	// ============================================================
	// 大厂原则: 只有当 AI 当前位置到 CirclePoint 距离够远才 MoveTo
	if (CirclePoint.Equals(PawnLocation, 10.f))
	{
		UE_LOG(LogBehaviorTree, Verbose,
			TEXT("[BTTask_CircleAroundTarget] AIC=%s CirclePoint ≈ PawnLocation, 跳过 MoveTo 直接 Pause."),
			*AIC->GetName());
		Mem->Phase = EPhase::PauseAndFinish;
		return EBTNodeResult::InProgress;
	}

	// 真实 Move — UE 原生 API, 失败有 navmesh 检测, 自动 Failed
	FAIMoveRequest MoveReq;
	MoveReq.SetGoalLocation(CirclePoint);
	MoveReq.SetAcceptanceRadius(40.f);
	MoveReq.SetReachTestIncludesAgentRadius(true);
	MoveReq.SetUsePathfinding(true);
	MoveReq.SetAllowPartialPath(true);

	const FPathFollowingRequestResult MoveResult = AIC->MoveTo(MoveReq);

	if (MoveResult.Code == EPathFollowingRequestResult::Failed)
	{
		UE_LOG(LogBehaviorTree, Warning,
			TEXT("[BTTask_CircleAroundTarget] AIC=%s MoveTo 立即失败 (NavMesh 等原因). CirclePoint=%s. "
			     "Selector 会兜底回 Chase. 这是大厂允许的失败模式."),
			*AIC->GetName(), *CirclePoint.ToString());
		// 清理 Focus (已经 Set 了, 失败时清掉)
		AIC->ClearFocus(EAIFocusPriority::Gameplay);
		return EBTNodeResult::Failed;
	}

	if (MoveResult.Code == EPathFollowingRequestResult::AlreadyAtGoal)
	{
		// AI 已经站在点上 → 直接进 Pause 阶段
		UE_LOG(LogBehaviorTree, Verbose,
			TEXT("[BTTask_CircleAroundTarget] AIC=%s AlreadyAtGoal → 直接进 Pause 阶段."),
			*AIC->GetName());
		Mem->Phase = EPhase::PauseAndFinish;
		return EBTNodeResult::InProgress;
	}

	// InProgress / RequestSuccessful → 进入 Tick 阶段
	Mem->Phase = EPhase::MoveToPoint;
	Mem->MoveRequestID = MoveResult.MoveId;

	UE_LOG(LogBehaviorTree, Log,
		TEXT("[BTTask_CircleAroundTarget] AIC=%s SELECT POINT + Start MoveTo. "
		     "CurrentDist=%.0f, OrbitAngle=%.0f° (%s), Side=%s, EffectiveRadius=%.0f, "
		     "CirclePoint=%s, PauseSeconds=%.2f"),
		*AIC->GetName(),
		CurrentDistance,
		OrbitAngleDegrees,
		*CirclePoint.ToString(),
		(RandomSign < 0.f ? TEXT("LEFT") : TEXT("RIGHT")),
		EffectiveRadius,
		*CirclePoint.ToString(),
		EffectivePause);

	// 异步任务 — TickTask 会处理后续
	return EBTNodeResult::InProgress;
}

void UBTTask_CircleAroundTarget::TickTask(
	UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	Super::TickTask(OwnerComp, NodeMemory, DeltaSeconds);

	FCircleMemory* Mem = reinterpret_cast<FCircleMemory*>(NodeMemory);
	if (!Mem)
	{
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		return;
	}

	AAIController* AIC = OwnerComp.GetAIOwner();
	APawn* AIPawn = AIC ? AIC->GetPawn() : nullptr;
	if (!AIC || !AIPawn)
	{
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		return;
	}

	switch (Mem->Phase)
	{
	case EPhase::PickPoint:
	case EPhase::MoveToPoint:
	{
		// 检查 MoveTo 是否完成 — 用 PathFollowingComponent 状态
		// 【C++ 优先级陷阱】!PFC == Moving 会解析为 (!PFC) == Moving → bool 与枚举比较永远为 false
		//   正确: 必须加括号 (PFC == nullptr) || (PFC->GetStatus() != Moving)
		UPathFollowingComponent* PFC = AIC->GetPathFollowingComponent();
		if (PFC == nullptr || PFC->GetStatus() != EPathFollowingStatus::Moving)
		{
			// Move 完成 (Idle) 或 PFC 失效 → 进入 Pause
			UE_LOG(LogBehaviorTree, Verbose,
				TEXT("[BTTask_CircleAroundTarget] AIC=%s MoveTo 完成 → 进入 Pause."),
				*AIC->GetName());
			Mem->Phase = EPhase::PauseAndFinish;
			Mem->ElapsedPause = 0.f;
			break;
		}

		// 检查卡死 — AI 在 Tick 内没怎么动 60+ Tick (3 秒)
		const float DistanceFromStart = FVector::Dist2D(
			AIPawn->GetActorLocation(), Mem->CachedPawnLocation);
		if (DistanceFromStart < 5.f)
		{
			// 累计卡死 → 跳过 Move, 直接进 Pause
			// 这是大厂抗抖动: 不能让 AI 卡在墙上等死
			static thread_local int32 StuckTicks = 0;
			StuckTicks++;
			if (StuckTicks > 180) // 3 秒 @ 60Hz
			{
				UE_LOG(LogBehaviorTree, Warning,
					TEXT("[BTTask_CircleAroundTarget] AIC=%s MoveTo 卡死 > 3s, 跳过 → Pause."),
					*AIC->GetName());
				StuckTicks = 0;
				Mem->Phase = EPhase::PauseAndFinish;
				Mem->ElapsedPause = 0.f;
			}
		}
		else
		{
			static thread_local int32 StuckTicks = 0;
			StuckTicks = 0;
		}
		break;
	}

	case EPhase::PauseAndFinish:
	{
		// 短暂停顿
		Mem->ElapsedPause += DeltaSeconds;

		// 读 ConfigSO 停顿秒数 (运行时也能调)
		const UAIRuntimeConfigComponent* RuntimeConfig = Cast<ABaseAIController>(AIC)
			? Cast<ABaseAIController>(AIC)->GetRuntimeConfig() : nullptr;
		const UAIBehaviorConfigSO* Config = RuntimeConfig ? RuntimeConfig->GetConfig() : nullptr;

		float TargetPause = CirclePauseSeconds;
		if (Config)
		{
			const FAICombatParams C = RuntimeConfig->GetScaledCombat();
			if (C.CirclePauseSeconds >= 0.f) TargetPause = C.CirclePauseSeconds;
		}

		UE_LOG(LogBehaviorTree, Verbose,
			TEXT("[BTTask_CircleAroundTarget] AIC=%s Pause: %.3f / %.3f"),
			*AIC->GetName(), Mem->ElapsedPause, TargetPause);

		if (Mem->ElapsedPause >= TargetPause)
		{
			UE_LOG(LogBehaviorTree, Display,
				TEXT("[BTTask_CircleAroundTarget] AIC=%s COMPLETE (Pause done). Sequence → Selector → 重扫整棵树 → 距离仍 < AR → 再 Attack. 这就是 '打一下走两步再打' 的循环."),
				*AIC->GetName());

			if (UPathFollowingComponent* PFC = AIC->GetPathFollowingComponent())
			{
				PFC->AbortMove(*this, FPathFollowingResultFlags::OwnerFinished);
			}

			// 【v40.9.4】Pause 完成 — 清 Focus (避免影响后续 Sequence 的面朝)
			// AIC->SetFocus 在 Pause 阶段一直维持, Pause 完就清
			AIC->ClearFocus(EAIFocusPriority::Gameplay);

			FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
		}
		break;
	}

	default:
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		break;
	}
}

EBTNodeResult::Type UBTTask_CircleAroundTarget::AbortTask(
	UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	if (AAIController* AIC = OwnerComp.GetAIOwner())
	{
		if (UPathFollowingComponent* PFC = AIC->GetPathFollowingComponent())
		{
			PFC->AbortMove(*this, FPathFollowingResultFlags::OwnerFinished);
		}

		// 【v40.9.4】Abort 时也清 Focus
		AIC->ClearFocus(EAIFocusPriority::Gameplay);
	}

	if (NodeMemory)
	{
		FCircleMemory* Mem = reinterpret_cast<FCircleMemory*>(NodeMemory);
		Mem->Phase = EPhase::PickPoint;
		Mem->ElapsedPause = 0.f;
	}
	return Super::AbortTask(OwnerComp, NodeMemory);
}
