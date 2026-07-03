// Copyright (c) 2026.

#include "Systems/AI/Decorators/BTDecorator_HPThreshold.h"

#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Float.h"

UBTDecorator_HPThreshold::UBTDecorator_HPThreshold()
{
	NodeName = TEXT("HP Threshold");

	HealthPercentKey.AddFloatFilter(this,
		GET_MEMBER_NAME_CHECKED(UBTDecorator_HPThreshold, HealthPercentKey));

	bNotifyBecomeRelevant = false;
	bNotifyTick = false;

	// 血量变化时重算 (受击 / 死亡)
	FlowAbortMode = EBTFlowAbortMode::Self;
}

FString UBTDecorator_HPThreshold::GetStaticDescription() const
{
	return FString::Printf(TEXT("BB.%s %s %.2f"),
		*HealthPercentKey.SelectedKeyName.ToString(),
		Mode == EBTHPCheckMode::LessThan ? TEXT("<") : TEXT(">"),
		Threshold);
}

bool UBTDecorator_HPThreshold::CalculateRawConditionValue(
	UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) const
{
	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	if (!BB)
	{
		return false;
	}

	const float HPPercent = BB->GetValueAsFloat(HealthPercentKey.SelectedKeyName);

	switch (Mode)
	{
	case EBTHPCheckMode::LessThan:
		return HPPercent < Threshold;
	case EBTHPCheckMode::GreaterThan:
		return HPPercent > Threshold;
	default:
		return false;
	}
}
