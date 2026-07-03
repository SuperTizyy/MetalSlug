// Copyright (c) 2026.
//
// 【P0 v22 BT 决策库】BTDecorator — 距离判断：可攻击？

#include "Systems/AI/Decorators/BTDecorator_InAttackRange.h"

#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"

UBTDecorator_InAttackRange::UBTDecorator_InAttackRange()
{
	NodeName = TEXT("In Attack Range?");

	bNotifyBecomeRelevant = false;
	bNotifyTick = false;

	// FlowAbortMode: Self
	// 当 AI 离开攻击区间时, 需要自我中断, 让 BT 重新评估该进哪个分支
	FlowAbortMode = EBTFlowAbortMode::Self;

	DistanceKey.AddFloatFilter(this,
		GET_MEMBER_NAME_CHECKED(UBTDecorator_InAttackRange, DistanceKey));

	AttackRangeKey.AddFloatFilter(this,
		GET_MEMBER_NAME_CHECKED(UBTDecorator_InAttackRange, AttackRangeKey));
}

FString UBTDecorator_InAttackRange::GetStaticDescription() const
{
	return FString::Printf(TEXT("可攻击? (%s-%.0f <= %s <= %s+%.0f)\n"
		"→ true:  在攻击区间内, 进入 Attack 分支\n"
		"→ false: 太近(T<retreat) 或太远(T<chase), 不攻击"),
		*AttackRangeKey.SelectedKeyName.ToString(),
		HysteresisMargin,
		*DistanceKey.SelectedKeyName.ToString(),
		*AttackRangeKey.SelectedKeyName.ToString(),
		HysteresisMargin);
}

bool UBTDecorator_InAttackRange::CalculateRawConditionValue(
	UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) const
{
	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	if (!BB)
	{
		return false;
	}

	// 读 BB Key
	const float Distance = BB->GetValueAsFloat(DistanceKey.SelectedKeyName);
	const float AttackRange = BB->GetValueAsFloat(AttackRangeKey.SelectedKeyName);

	// 防御: Distance < 0 表示"无目标", 不应触发攻击
	if (Distance < 0.f)
	{
		return false;
	}

	// 决策: (AR - Margin) <= D <= (AR + Margin)
	const float RangeMin = AttackRange - HysteresisMargin;
	const float RangeMax = AttackRange + HysteresisMargin;

	return Distance >= RangeMin && Distance <= RangeMax;
}
