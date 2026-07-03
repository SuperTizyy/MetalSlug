// Copyright (c) 2026.

#include "Systems/AI/Tasks/BTTask_PlayAttackMontage.h"

#include "BehaviorTree/BehaviorTreeComponent.h"
#include "AIController.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "GameFramework/Pawn.h"

// 基类
#include "Characters/BaseCharacter.h"
#include "Systems/BaseAIController.h"
#include "Systems/AI/AIBehaviorTypes.h"

UBTTask_PlayAttackMontage::UBTTask_PlayAttackMontage()
{
	NodeName = TEXT("Play Attack Montage");
}

FString UBTTask_PlayAttackMontage::GetStaticDescription() const
{
	return TEXT("触发 AI 攻击 — 调 BaseCharacter::OnAIRequestAttack_Simple。\n"
		"同步完成 — 蒙太奇播放由下游 BTTask_WaitMontageFinish 接管。\n"
		"距离/冷却/目标空 全部由上游 Decorator 接管 (不重做)。");
}

EBTNodeResult::Type UBTTask_PlayAttackMontage::ExecuteTask(
	UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	// 1. 拿 AI Controller
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
	if (!AIChar || AIChar->IsDead())
	{
		return EBTNodeResult::Failed;
	}

	// 2. 检查武器 — 与旧 v18 防御一致
	if (!AIChar->GetCurrentWeapon())
	{
		UE_LOG(LogTemp, Error,
			TEXT("[BTTask_PlayAttackMontage] AI=%s 没有 CurrentWeapon! 攻击失败"),
			*AIChar->GetName());
		return EBTNodeResult::Failed;
	}

	// 3. 触发攻击 — 调 BaseCharacter 已实现的 OnAIRequestAttack_Simple
	//    - 内部播蒙太奇
	//    - 内部走 ConfigSO 伤害 + ApplyPointDamage
	//    - 内部绑 OnMontageEnded 回调 → OnAIAttackMontageEnded
	const bool bFired = AIChar->OnAIRequestAttack_Simple();
	if (!bFired)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[BTTask_PlayAttackMontage] OnAIRequestAttack_Simple 失败, AI=%s"),
			*AIChar->GetName());
		return EBTNodeResult::Failed;
	}

	// 4. 通知 C++ 状态 — 对称设 Attacking + Cooldown
	if (ABaseAIController* BaseAIC = Cast<ABaseAIController>(AIC))
	{
		BaseAIC->SetCurrentlyAttacking(true);
		BaseAIC->SetInAttackCooldown(true);

	// 5. 写 BB.CooldownEndTime = CurrentTime + AttackCooldown
	//    Decorator_CooldownReady 实时读这个判断冷却是否结束 (无 Token, 无 Service 中间态)
	//
	// 设计原则 (P0 2026.07.09 大厂方案):
	//   - CooldownEndTime 是"事件型" BB 值 (BTTask 一次性写)
	//   - Decorator_CooldownReady 是"决策层" (实时读 World.Time 对比)
	//   - 删除 bHasAttackToken: 是 BTService_UpdateBlackboard 上帝类残留的中间态
	//     完全冗余, Decorator 直接读 CooldownEndTime + World.Time 即可判定
	if (UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent())
	{
		UWorld* World = AIPawn->GetWorld();
		if (World)
		{
			const float CurrentTime = World->GetTimeSeconds();
			const float CooldownDuration = BaseAIC->GetEffectiveAttackInterval();
			const float CooldownEndTime = CurrentTime + CooldownDuration;

			// 仅写"事件型" BB 值 (冷却截止时间)
			BB->SetValueAsFloat(FName(AIBlackboardKeyNames::CooldownEndTime), CooldownEndTime);
		}
	}
	}

	// 6. 同步 Succeeded — 蒙太奇等待交给 WaitMontageFinish
	return EBTNodeResult::Succeeded;
}