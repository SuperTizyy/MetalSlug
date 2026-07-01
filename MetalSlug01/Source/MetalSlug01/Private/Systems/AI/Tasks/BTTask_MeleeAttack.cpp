// Copyright (c) 2026.

#include "Systems/AI/Tasks/BTTask_MeleeAttack.h"

#include "AIController.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "TimerManager.h"

#include "Characters/BaseCharacter.h"
#include "Systems/AI/AIRuntimeConfigComponent.h"
#include "Systems/AI/AIBehaviorTypes.h"

EBTNodeResult::Type UBTTask_MeleeAttack::ExecuteTask(
	UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	// 读取 BB 里的目标
	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	if (!BB)
	{
		return EBTNodeResult::Failed;
	}

	// 通过 BlackboardKey.SelectedKeyName 直接拿到绑定的 BB Key 名
	const FName TargetKeyName = BlackboardKey.SelectedKeyName;
	UObject* TargetObj = BB->GetValueAsObject(TargetKeyName);
	if (!TargetObj)
	{
		return EBTNodeResult::Failed;
	}

	// 【类型安全】TargetObj 是 UObject*, 必须 Cast 到 AActor* 才能调 GetActorLocation
	AActor* TargetActor = Cast<AActor>(TargetObj);
	if (!TargetActor)
	{
		return EBTNodeResult::Failed;
	}

	AActor* Pawn = OwnerComp.GetAIOwner() ? OwnerComp.GetAIOwner()->GetPawn() : nullptr;
	if (!Pawn)
	{
		return EBTNodeResult::Failed;
	}

	ABaseCharacter* AIChar = Cast<ABaseCharacter>(Pawn);
	if (!AIChar || AIChar->IsDead())
	{
		return EBTNodeResult::Failed;
	}

	// 检查距离：是否已进入攻击范围
	const float AttackRange = GetAttackRange(OwnerComp);
	const float Distance = FVector::Dist(
		TargetActor->GetActorLocation(), Pawn->GetActorLocation());

	if (Distance > AttackRange)
	{
		// 距离不够, 追上去再打, 返回 InProgress
		// BT 树上层的 MoveTo 节点会继续执行
		return EBTNodeResult::InProgress;
	}

	// 距离够, 执行攻击
	if (PerformAttack(OwnerComp))
	{
		return EBTNodeResult::InProgress; // 攻击动画播放中, 等待 Tick 完成
	}

	return EBTNodeResult::Failed;
}

void UBTTask_MeleeAttack::TickTask(UBehaviorTreeComponent& OwnerComp,
	uint8* NodeMemory, float DeltaSeconds)
{
	// ExecuteTask 返回 InProgress 后, 每帧这里检查:
	// 1. 攻击动画是否播完 (bIsAttacking == false)
	// 2. 距离是否仍在 AttackRange 内
	// 两个条件同时满足时执行攻击

	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	if (!BB)
	{
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		return;
	}

	AActor* Pawn = OwnerComp.GetAIOwner() ? OwnerComp.GetAIOwner()->GetPawn() : nullptr;
	if (!Pawn)
	{
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		return;
	}

	ABaseCharacter* AIChar = Cast<ABaseCharacter>(Pawn);
	if (!AIChar || AIChar->IsDead())
	{
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		return;
	}

	const FName TargetKeyName = BlackboardKey.SelectedKeyName;
	UObject* TargetObj = BB->GetValueAsObject(TargetKeyName);
	AActor* TargetActor = Cast<AActor>(TargetObj);
	if (!TargetActor)
	{
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		return;
	}

	const float AttackRange = GetAttackRange(OwnerComp);
	const float Distance = FVector::Dist(
		TargetActor->GetActorLocation(), Pawn->GetActorLocation());

	if (Distance <= AttackRange && PerformAttack(OwnerComp))
	{
		// 攻击已触发, 等待下一次可攻击窗口
	}

	// 持续等待, 不主动退出, BT 会自然重跑
	FinishLatentTask(OwnerComp, EBTNodeResult::InProgress);
}

bool UBTTask_MeleeAttack::PerformAttack(UBehaviorTreeComponent& OwnerComp)
{
	// 检查 Token (防抖)
	if (!IsAttackTokenReady(OwnerComp))
	{
		return false;
	}

	AActor* Pawn = OwnerComp.GetAIOwner() ? OwnerComp.GetAIOwner()->GetPawn() : nullptr;
	ABaseCharacter* AIChar = Cast<ABaseCharacter>(Pawn);
	if (!AIChar || AIChar->IsDead())
	{
		return false;
	}

	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	const FName TargetKeyName = BlackboardKey.SelectedKeyName;
	UObject* TargetObj = BB ? BB->GetValueAsObject(TargetKeyName) : nullptr;
	if (!TargetObj)
	{
		return false;
	}

	// 【核心】执行轻击攻击
	// LightAttack_Pressed() 是 protected, 但 AIChar 是同包类的访问是允许的
	// (本类和 BaseCharacter 都在 MetalSlug01 模块内, protected 跨类访问需要 friend 或 public)
	// 解决: 调用 public 接口 OnAIRequestAttack (在 BaseCharacter 加)
	AIChar->OnAIRequestAttack();

	// 消耗 Token
	ConsumeAttackToken(OwnerComp);

	return true;
}

void UBTTask_MeleeAttack::ConsumeAttackToken(UBehaviorTreeComponent& OwnerComp)
{
	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	if (BB)
	{
		BB->SetValueAsBool(FName(AIBlackboardKeyNames::bHasAttackToken), true);
	}

	// 设置冷却计时器
	AActor* Pawn = OwnerComp.GetAIOwner() ? OwnerComp.GetAIOwner()->GetPawn() : nullptr;
	if (Pawn)
	{
		UWorld* World = Pawn->GetWorld();
		if (World)
		{
			const float Cooldown = GetAttackCooldown(OwnerComp);
			FTimerHandle Handle;
			TWeakObjectPtr<UBlackboardComponent> WeakBB = BB;
			World->GetTimerManager().SetTimer(Handle,
				[WeakBB]
				{
					if (WeakBB.IsValid())
					{
						WeakBB->SetValueAsBool(FName(AIBlackboardKeyNames::bHasAttackToken), false);
					}
				}, Cooldown, false);
		}
	}
}

float UBTTask_MeleeAttack::GetAttackRange(UBehaviorTreeComponent& OwnerComp) const
{
	AActor* Pawn = OwnerComp.GetAIOwner() ? OwnerComp.GetAIOwner()->GetPawn() : nullptr;
	ABaseCharacter* AIChar = Cast<ABaseCharacter>(Pawn);
	if (!AIChar)
	{
		return 180.f;
	}

	if (UAIRuntimeConfigComponent* Config =
		AIChar->GetController() ?
		AIChar->GetController()->FindComponentByClass<UAIRuntimeConfigComponent>() : nullptr)
	{
		if (Config->GetConfig())
		{
			return Config->GetScaledCombat().AttackRange;
		}
	}

	return 180.f;
}

float UBTTask_MeleeAttack::GetAttackCooldown(UBehaviorTreeComponent& OwnerComp) const
{
	AActor* Pawn = OwnerComp.GetAIOwner() ? OwnerComp.GetAIOwner()->GetPawn() : nullptr;
	ABaseCharacter* AIChar = Cast<ABaseCharacter>(Pawn);
	if (!AIChar)
	{
		return 1.2f;
	}

	if (UAIRuntimeConfigComponent* Config =
		AIChar->GetController() ?
		AIChar->GetController()->FindComponentByClass<UAIRuntimeConfigComponent>() : nullptr)
	{
		if (Config->GetConfig())
		{
			return Config->GetScaledCombat().AttackCooldown;
		}
	}

	return 1.2f;
}

bool UBTTask_MeleeAttack::IsAttackTokenReady(UBehaviorTreeComponent& OwnerComp) const
{
	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	if (!BB)
	{
		return true;
	}

	return !BB->GetValueAsBool(FName(AIBlackboardKeyNames::bHasAttackToken));
}

UBTTask_MeleeAttack::UBTTask_MeleeAttack()
{
	NodeName = TEXT("Melee Attack (刀战攻击)");

	// 本节点监控 TargetActor BB Key
	// BlackboardKey 是本类加的 FBlackboardKeySelector 成员
	BlackboardKey.AddObjectFilter(this,
		GET_MEMBER_NAME_CHECKED(UBTTask_MeleeAttack, BlackboardKey),
		ABaseCharacter::StaticClass());

	// 允许 Tick
	bNotifyTick = true;

	// 不需要 tick 以外的通知
	bNotifyTaskFinished = false;
}

FString UBTTask_MeleeAttack::GetStaticDescription() const
{
	return FString(TEXT("检查距离够近后调用 OnAIRequestAttack(), "
		"Tick 等待攻击动画完成。用 BB bHasAttackToken 防抖, "
		"冷却时长由 AIBehaviorConfigSO.Combat.AttackCooldown 配置。"));
}