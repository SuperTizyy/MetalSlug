// Copyright (c) 2026.

#include "Systems/AI/Tasks/BTTask_WaitMontageFinish.h"

#include "BehaviorTree/BehaviorTreeComponent.h"
#include "AIController.h"
#include "GameFramework/Pawn.h"
#include "Animation/AnimInstance.h"
#include "TimerManager.h"
#include "Engine/World.h"

#include "Characters/BaseCharacter.h"
#include "Systems/BaseAIController.h"

UBTTask_WaitMontageFinish::UBTTask_WaitMontageFinish()
{
	NodeName = TEXT("Wait Montage Finish");

	// 必须开启 Tick — TickTask 里检查蒙太奇结束状态
	bNotifyTick = true;
}

FString UBTTask_WaitMontageFinish::GetStaticDescription() const
{
	return FString::Printf(TEXT("异步等当前蒙太奇播完 (timeout %.1fs)。\n"
		"判定方式: AIC.IsCurrentlyAttacking()==false"), TimeoutSeconds);
}

EBTNodeResult::Type UBTTask_WaitMontageFinish::ExecuteTask(
	UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	// 1. 初始化 NodeMemory
	new (NodeMemory) FTaskMemory();

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

	ABaseCharacter* AIChar = Cast<ABaseCharacter>(AIPawn);
	if (!AIChar)
	{
		return EBTNodeResult::Failed;
	}

	// 2. 拿当前蒙太奇 — 没在播就 Succeeded
	UAnimInstance* AnimInst = AIChar->GetMesh() ? AIChar->GetMesh()->GetAnimInstance() : nullptr;
	if (!AnimInst)
	{
		return EBTNodeResult::Succeeded;
	}

	UAnimMontage* CurrentMontage = AnimInst->GetCurrentActiveMontage();
	if (!CurrentMontage)
	{
		return EBTNodeResult::Succeeded;
	}

	// 3. 启动超时 Timer
	FTaskMemory& Mem = GetTaskMemory(NodeMemory);
	Mem.OwnerCompRef = &OwnerComp;
	Mem.bFinished = false;

	if (UWorld* World = AIPawn->GetWorld())
	{
		FTimerManager& TM = World->GetTimerManager();
		TM.ClearTimer(Mem.TimeoutTimerHandle);
		TM.SetTimer(Mem.TimeoutTimerHandle,
			FTimerDelegate::CreateUObject(this,
				&UBTTask_WaitMontageFinish::OnTimeoutReached,
				&OwnerComp),
			TimeoutSeconds + 1.f,
			/*bLoop=*/false);
	}

	return EBTNodeResult::InProgress;
}

void UBTTask_WaitMontageFinish::TickTask(
	UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	FTaskMemory& Mem = GetTaskMemory(NodeMemory);

	if (Mem.bFinished)
	{
		return;
	}

	AAIController* AIC = OwnerComp.GetAIOwner();
	if (!AIC)
	{
		CleanupTimer(OwnerComp, Mem);
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		return;
	}

	APawn* AIPawn = AIC->GetPawn();
	if (!AIPawn)
	{
		CleanupTimer(OwnerComp, Mem);
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		return;
	}

	ABaseCharacter* AIChar = Cast<ABaseCharacter>(AIPawn);
	if (!AIChar || AIChar->IsDead())
	{
		CleanupTimer(OwnerComp, Mem);
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		return;
	}

	// 检查 AI 攻击状态 — IsCurrentlyAttacking 由 OnMontageEnded 清
	// 这是简化的判定, 不直接绑委托
	if (ABaseAIController* BaseAIC = Cast<ABaseAIController>(AIC))
	{
		if (!BaseAIC->IsCurrentlyAttacking())
		{
			// 蒙太奇结束 — 恢复攻击状态标志
			BaseAIC->SetCurrentlyAttacking(false);
			// 注意 (P0 2026.07.09): 删除 bHasAttackToken / BTService 冷却分支 / BTDecorator_HasAttackToken
			// 冷却由 BTDecorator_CooldownReady 直接实时读 World.Time vs BB.CooldownEndTime 决策
			// BTService 不再维护任何冷却状态 (它只派生距离/HP 等世界事实)

			CleanupTimer(OwnerComp, Mem);
			FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
			return;
		}
	}
}

EBTNodeResult::Type UBTTask_WaitMontageFinish::AbortTask(
	UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	FTaskMemory& Mem = GetTaskMemory(NodeMemory);

	// Abort 时明确清攻击状态 — BT 中断意味着 AI 停止攻击
	if (AAIController* AIC = OwnerComp.GetAIOwner())
	{
		if (ABaseAIController* BaseAIC = Cast<ABaseAIController>(AIC))
		{
			BaseAIC->SetCurrentlyAttacking(false);
		}
	}

	CleanupTimer(OwnerComp, Mem);
	return EBTNodeResult::Aborted;
}

void UBTTask_WaitMontageFinish::OnInstanceDestroyed(
	UBehaviorTreeComponent& OwnerComp)
{
	// BT 实例销毁时清理残留 Timer + 攻击状态 (AI 死亡时关键)
	// 【关键】只清状态，不调 FinishLatentTask — OwnerComp 已在析构，FinishLatentTask 无效
	uint8* NodeMemory = OwnerComp.GetNodeMemory(this, GetInstanceMemorySize());
	if (NodeMemory)
	{
		FTaskMemory& Mem = GetTaskMemory(NodeMemory);

		// AI 销毁时清攻击状态 — 防 CurrentlyAttacking=true 残留
		if (AAIController* AIC = OwnerComp.GetAIOwner())
		{
			if (ABaseAIController* BaseAIC = Cast<ABaseAIController>(AIC))
			{
				BaseAIC->SetCurrentlyAttacking(false);
			}
		}

		CleanupTimer(OwnerComp, Mem);
		Mem.bFinished = true; // 防止 Timer/Tick 回调再进来
	}
}

void UBTTask_WaitMontageFinish::OnTimeoutReached(
	UBehaviorTreeComponent* OwnerCompPtr)
{
	if (!OwnerCompPtr)
	{
		return;
	}

	uint8* NodeMemory = OwnerCompPtr->GetNodeMemory(this, GetInstanceMemorySize());
	if (NodeMemory)
	{
		FTaskMemory& Mem = GetTaskMemory(NodeMemory);
		if (!Mem.bFinished)
		{
			CleanupTimer(*OwnerCompPtr, Mem);

			// 超时 = 蒙太奇该结束但状态残留,强制清攻击状态让 AI 恢复追逐
			if (AAIController* AIC = OwnerCompPtr->GetAIOwner())
			{
				if (ABaseAIController* BaseAIC = Cast<ABaseAIController>(AIC))
				{
					BaseAIC->SetCurrentlyAttacking(false);
				}
			}

			FinishLatentTask(*OwnerCompPtr, EBTNodeResult::Failed);

			UE_LOG(LogTemp, Warning,
				TEXT("[BTTask_WaitMontageFinish] 超时 (%.1fs), 强制 FinishLatentTask(Failed)"),
				TimeoutSeconds + 1.f);
		}
	}
}

void UBTTask_WaitMontageFinish::CleanupTimer(
	UBehaviorTreeComponent& OwnerComp, FTaskMemory& Mem)
{
	Mem.bFinished = true;

	if (UWorld* World = OwnerComp.GetWorld())
	{
		World->GetTimerManager().ClearTimer(Mem.TimeoutTimerHandle);
	}

	// CurrentlyAttacking 在以下三个出口都会清:
	// 1. TickTask: 蒙太奇正常播完 (IsCurrentlyAttacking==false)
	// 2. AbortTask: BT 中断 (AI 停止攻击)
	// 3. OnTimeoutReached: 超时兜底 (防蒙太奇卡死)
	// CleanupTimer 只清 Timer Handle 和 bFinished, 不清状态
}