// Copyright (c) 2026.
//
// 【P0 v23.2 BT 原子库】BTTask — 退一步 (面朝敌人后退)
//
// v23.2 重大修复 "回头走":
//   v23 之前的实现: MoveToLocation → AI 朝向 = 移动方向 → "回头走"
//   根本原因 (UE Character Movement):
//     - BP_MeleeGrunt 的 CharacterMovement 默认 OrientRotationToMovement = ✔
//     - OrientRotationToMovement = ✔ 时, MoveTo 会强制 AI 朝向 = 移动方向
//     - RetreatPoint 在 AI 后方, AI 朝 RetreatPoint 走 → "回头走"
//
//   修复方案 (v23.2): 在 MoveAway 期间临时关 OrientRotationToMovement + 开 UseControllerDesiredRotation
//     - 关掉 OrientRotationToMovement 后, AI 朝向由 UseControllerDesiredRotation / FocalPoint 控制
//     - 配合 AIC->SetFocalPoint(TargetActor) 强制 AI 朝向 Target
//     - 任务结束时恢复原值 (避免污染其他 BT 分支)
//     - 这是"面朝敌人后退"的标准实现, 大厂 Uncharted/Last of Us 用同样手法
//
// v23.1: TooClose 改成纯 <
// v23.2: 修复回头走 (临时调 Pawn Movement 设置)

#include "Systems/AI/Tasks/BTTask_MoveAwayFromTarget.h"

#include "AIController.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Pawn.h"
#include "Navigation/PathFollowingComponent.h"
#include "NavigationSystem.h"

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
		"v23.2 修复回头走: 临时开 UseControllerDesiredRotation + 关 OrientRotationToMovement,\n"
		"配合 AIC->SetFocalPoint 强制 AI 朝向目标。\n"
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
	Mem.bMovementSettingsSaved = false;

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
	// v23.2: 临时调整 Pawn 的 Movement 设置 + FocalPoint
	// ============================================
	// 目的: MoveTo 期间 AI 朝向固定朝 Target (不退步时转向)
	// 退出任务时恢复原值, 不污染其他分支
	if (ACharacter* AICharacter = Cast<ACharacter>(AIPawn))
	{
		if (UCharacterMovementComponent* MoveComp = AICharacter->GetCharacterMovement())
		{
			// 保存原值, 用于 AbortTask/完成时恢复
			Mem.bSavedOrientRotationToMovement = MoveComp->bOrientRotationToMovement;
			Mem.bSavedUseControllerDesiredRotation = MoveComp->bUseControllerDesiredRotation;
			Mem.bMovementSettingsSaved = true;

			// 关 OrientRotationToMovement (不让 MoveTo 抢 AI 朝向)
			MoveComp->bOrientRotationToMovement = false;

			// 开 UseControllerDesiredRotation (让 FocalPoint 控制朝向)
			MoveComp->bUseControllerDesiredRotation = true;
		}
	}

	// 用 SetFocalPoint 强制 AI 朝向 Target (Gameplay 优先级最高, 压过 MoveTo 的 MoveFocus)
	AIC->SetFocus(TargetActor, EAIFocusPriority::Gameplay);

	// ============================================
	// 退步逻辑
	// ============================================
	const FVector StepBackLoc = ComputeStepBackLocation(AIPawn, TargetActor);

	if (!StartMoveTo(OwnerComp, StepBackLoc))
	{
		// MoveTo 启动失败, 恢复 + 清理
		RestoreMovementSettings(Mem, AIC);
		AIC->ClearFocus(EAIFocusPriority::Gameplay);
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
		AIC->ClearFocus(EAIFocusPriority::Gameplay);

		// 恢复 Pawn 原 Movement 设置
		FTaskMemory& Mem = GetTaskMemory(NodeMemory);
		RestoreMovementSettings(Mem, AIC);
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
		// v23.2: 任务完成, 恢复 Pawn Movement 设置 + 清理 Focus
		FTaskMemory& Mem = GetTaskMemory(NodeMemory);
		RestoreMovementSettings(Mem, AIC);
		AIC->ClearFocus(EAIFocusPriority::Gameplay);
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

			// 超时也要恢复设置
			RestoreMovementSettings(Mem, AIC);
			AIC->ClearFocus(EAIFocusPriority::Gameplay);
			Mem.bMoveStarted = false;

			FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
		}
	}
}

void UBTTask_MoveAwayFromTarget::RestoreMovementSettings(
	FTaskMemory& Mem, AAIController* AIC) const
{
	if (!Mem.bMovementSettingsSaved)
	{
		return;
	}

	if (!AIC)
	{
		return;
	}

	if (APawn* AIPawn = AIC->GetPawn())
	{
		if (ACharacter* AICharacter = Cast<ACharacter>(AIPawn))
		{
			if (UCharacterMovementComponent* MoveComp = AICharacter->GetCharacterMovement())
			{
				// 恢复执行任务前的原值
				MoveComp->bOrientRotationToMovement = Mem.bSavedOrientRotationToMovement;
				MoveComp->bUseControllerDesiredRotation = Mem.bSavedUseControllerDesiredRotation;
			}
		}
	}

	Mem.bMovementSettingsSaved = false;
}
