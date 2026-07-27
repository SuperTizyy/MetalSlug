// Copyright (c) 2026.
//
// 【P0 v23.2 BT 原子库 + v40.10 重构】BTTask — 退一步 (面朝敌人后退)
//
// v23.2 重大修复 "回头走":
//   v23 之前的实现: MoveToLocation → AI 朝向 = 移动方向 → "回头走"
//   根本原因 (UE Character Movement):
//     - BP_MeleeGrunt 的 CharacterMovement 默认 OrientRotationToMovement = ✔
//     - OrientRotationToMovement = ✔ 时, MoveTo 会强制 AI 朝向 = 移动方向
//     - RetreatPoint 在 AI 后方, AI 朝 RetreatPoint 走 → "回头走"
//
// v23.2 标准实现:
//   ExecuteTask:
//     1. 保存 Pawn 原 Movement 设置 (OrientRotationToMovement / UseControllerDesiredRotation)
//     2. 临时:
//        - OrientRotationToMovement = false (不让 MoveTo 抢 AI 朝向)
//        - UseControllerDesiredRotation = true (让 Controller 控制朝向)
//     3. AIC->SetFocus(TargetActor, EAIFocusPriority::Gameplay)
//        (Gameplay 优先级最高, 压过 MoveTo 默认 MoveFocus, AI 必定朝 Target)
//     4. MoveToLocation(RetreatPoint)
//   TickTask 完成/Abort:
//     1. ClearFocus(EAIFocusPriority::Gameplay)
//     2. 恢复原 Movement 设置
//   退出条件:
//     - MoveTo 到达 AcceptanceRadius → Succeeded (恢复设置)
//     - MoveTo Waiting 超 MaxWaitTime → 强制 Succeeded (恢复设置)
//     - AbortTask → Aborted (恢复设置)
//
// v40.10 大厂重构:
//   抽离 SaveMovementSettings / RestoreMovementSettings / SetFocus 到 UAIFacingMoveHelper
//   - 单一真理源: Movement 配置策略集中在 Helper, 任何"边移动边面向"的 BTTask 复用
//   - DRY: 不再私有函数 Copy-Paste
//   - 零行为变化: API 等价, 行为完全一致

#include "Systems/AI/Tasks/BTTask_MoveAwayFromTarget.h"

#include "AIController.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/Pawn.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationSystem.h"

#include "Systems/AI/AIFacingMoveHelper.h"

UBTTask_MoveAwayFromTarget::UBTTask_MoveAwayFromTarget()
{
	NodeName = TEXT("Move Away From Target");

	bNotifyTick = true;

	TargetKey.AddObjectFilter(this,
		GET_MEMBER_NAME_CHECKED(UBTTask_MoveAwayFromTarget, TargetKey),
		AActor::StaticClass());
}

FString UBTTask_MoveAwayFromTarget::GetStaticDescription() const
{
	return FString::Printf(TEXT("退一步 (面向敌人后退) — %.0fcm。\n"
		"算法: AI 位置 + (AI-目标)方向 × %.0fcm。\n"
		"v40.10: 通过 UAIFacingMoveHelper 复用朝向机制 (单一真理源)。\n"
		"到达 AcceptanceRadius=%.0fcm 后 Succeeded。\n"
		"被阻挡 %.1fs 强制 Succeeded。"),
		StepDistance, StepDistance, AcceptanceRadius, MaxWaitTime);
}

EBTNodeResult::Type UBTTask_MoveAwayFromTarget::ExecuteTask(
	UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	FTaskMemory& Mem = GetTaskMemory(NodeMemory);
	new (&Mem) FTaskMemory();
	Mem.bMoveStarted = false;
	Mem.WaitTime = 0.f;

	AAIController* AIC = OwnerComp.GetAIOwner();
	if (!AIC)
	{
		return EBTNodeResult::Failed;
	}

	APawn* AIPawn = AIC->GetPawn();
	if (!AIPawn)
	{
		return EBTNodeResult::Failed;
	}

	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	if (!BB)
	{
		return EBTNodeResult::Failed;
	}

	UObject* TargetObj = BB->GetValueAsObject(TargetKey.SelectedKeyName);
	AActor* TargetActor = Cast<AActor>(TargetObj);
	if (!TargetActor)
	{
		return EBTNodeResult::Failed;
	}

	// ============================================
	// v40.10: 朝向机制下沉到 UAIFacingMoveHelper
	//   之前 30+ 行 (Save + 改 Movement + SetFocus) → 1 行调用
	//   Helper 内部: 保存 Movement + 改朝向方式 + SetFocus
	// ============================================
	const bool bFacingConfigured = UAIFacingMoveHelper::ConfigureFacingMove(
		Cast<ACharacter>(AIPawn),
		AIC,
		TargetActor,
		Mem.FacingSnapshot);

	if (!bFacingConfigured)
	{
		// 【零兜底】Helper 失败 (Character/Target 无效) → 拒绝 MoveTo, 否则回头走
		// Helper 内部已 Log Error 解释根因
		return EBTNodeResult::Failed;
	}

	// ============================================
	// 退步逻辑
	// ============================================
	const FVector StepBackLoc = ComputeStepBackLocation(AIPawn, TargetActor);

	if (!StartMoveTo(OwnerComp, StepBackLoc))
	{
		// MoveTo 启动失败, 恢复朝向 (Helper 内幂等)
		UAIFacingMoveHelper::RestoreFacingMove(
			Cast<ACharacter>(AIPawn), AIC, Mem.FacingSnapshot);
		return EBTNodeResult::Failed;
	}

	Mem.bMoveStarted = true;
	return EBTNodeResult::InProgress;
}

void UBTTask_MoveAwayFromTarget::TickTask(
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

EBTNodeResult::Type UBTTask_MoveAwayFromTarget::AbortTask(
	UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	AAIController* AIC = OwnerComp.GetAIOwner();
	if (AIC)
	{
		AIC->StopMovement();

		// v40.10: 恢复朝向 (Helper 内幂等 — Snapshot 无效就跳过)
		FTaskMemory& Mem = GetTaskMemory(NodeMemory);
		UAIFacingMoveHelper::RestoreFacingMove(
			Cast<ACharacter>(AIC->GetPawn()), AIC, Mem.FacingSnapshot);

		Mem.bMoveStarted = false;
	}

	return EBTNodeResult::Aborted;
}

FVector UBTTask_MoveAwayFromTarget::ComputeStepBackLocation(
	APawn* AIPawn, AActor* TargetActor) const
{
	const FVector AIPos = AIPawn->GetActorLocation();
	const FVector TargetPos = TargetActor->GetActorLocation();

	// (AI - Target) 方向 (AI 远离 Target 的方向)
	const FVector StepDir = (AIPos - TargetPos).GetSafeNormal();

	// 目标撤退点
	FVector StepBackLoc = AIPos + StepDir * StepDistance;

	// NavMesh 投影
	if (UNavigationSystemV1* NavSys = UNavigationSystemV1::GetCurrent(AIPawn->GetWorld()))
	{
		FNavLocation OutLocation;
		if (NavSys->ProjectPointToNavigation(StepBackLoc, OutLocation,
			FVector(0.f, 0.f, 100.f)))
		{
			StepBackLoc = OutLocation.Location;
		}
	}

	return StepBackLoc;
}

bool UBTTask_MoveAwayFromTarget::StartMoveTo(
	UBehaviorTreeComponent& OwnerComp, const FVector& Dest)
{
	AAIController* AIC = OwnerComp.GetAIOwner();
	if (!AIC)
	{
		return false;
	}

	const EPathFollowingRequestResult::Type Result =
		AIC->MoveToLocation(Dest, AcceptanceRadius, false, true, true, true, nullptr, true);

	return Result == EPathFollowingRequestResult::RequestSuccessful
		|| Result == EPathFollowingRequestResult::AlreadyAtGoal;
}

void UBTTask_MoveAwayFromTarget::CheckArrival(
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
		// v40.10: 完成 — 恢复朝向 (Helper 内幂等)
		FTaskMemory& Mem = GetTaskMemory(NodeMemory);
		UAIFacingMoveHelper::RestoreFacingMove(
			Cast<ACharacter>(AIC->GetPawn()), AIC, Mem.FacingSnapshot);
		Mem.bMoveStarted = false;

		FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
	}
	else if (MoveStatus == EPathFollowingStatus::Waiting)
	{
		// 被阻挡
		FTaskMemory& Mem = GetTaskMemory(NodeMemory);
		Mem.WaitTime += OwnerComp.GetWorld()->GetDeltaSeconds();
		if (Mem.WaitTime > MaxWaitTime)
		{
			Mem.WaitTime = 0.f;

			// 超时也要恢复朝向
			UAIFacingMoveHelper::RestoreFacingMove(
				Cast<ACharacter>(AIC->GetPawn()), AIC, Mem.FacingSnapshot);
			Mem.bMoveStarted = false;

			FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
		}
	}
}
