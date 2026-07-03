// Copyright (c) 2026.

#include "Systems/AI/Services/BTService_UpdateHealth.h"

#include "BehaviorTree/BehaviorTreeComponent.h"
#include "AIController.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "GameFramework/Pawn.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Float.h"

#include "Characters/BaseCharacter.h"
#include "Components/HealthComponent.h"

UBTService_UpdateHealth::UBTService_UpdateHealth()
{
	NodeName = TEXT("Update Health");

	Interval = 0.1f;
	RandomDeviation = 0.f;
	bTickIntervals = true;

	HealthPercentKey.AddFloatFilter(this,
		GET_MEMBER_NAME_CHECKED(UBTService_UpdateHealth, HealthPercentKey));
}

FString UBTService_UpdateHealth::GetStaticDescription() const
{
	return FString::Printf(
		TEXT("每 %.2fs 派生 BB.HealthPercent (Float, 0~1)。\n"
			 "数据源: BaseCharacter::HealthComponent。"),
		Interval);
}

void UBTService_UpdateHealth::TickNode(
	UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	Super::TickNode(OwnerComp, NodeMemory, DeltaSeconds);

	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	if (!BB)
	{
		return;
	}

	AAIController* AIC = OwnerComp.GetAIOwner();
	if (!AIC)
	{
		return;
	}

	APawn* AIPawn = AIC->GetPawn();
	if (!AIPawn)
	{
		// Pawn 丢失 → 写 0 (Decorator 自然 Fail, AI 进入死亡分支)
		BB->SetValueAsFloat(HealthPercentKey.SelectedKeyName, 0.f);
		return;
	}

	// 算 HP 百分比 — 单点真理: BaseCharacter + HealthComponent
	ABaseCharacter* AIChar = Cast<ABaseCharacter>(AIPawn);
	if (!AIChar || !AIChar->HealthComponent)
	{
		// 没血量组件 → 写 0 (防御)
		BB->SetValueAsFloat(HealthPercentKey.SelectedKeyName, 0.f);
		return;
	}

	const float Current = AIChar->HealthComponent->GetCurrent();
	const float Max = AIChar->HealthComponent->GetMax();

	// 防御: Max <= 0 (配置错误) → 视为满血, 避免除零
	float HPPercent = (Max > 0.f) ? (Current / Max) : 1.f;
	HPPercent = FMath::Clamp(HPPercent, 0.f, 1.f);

	BB->SetValueAsFloat(HealthPercentKey.SelectedKeyName, HPPercent);
}
