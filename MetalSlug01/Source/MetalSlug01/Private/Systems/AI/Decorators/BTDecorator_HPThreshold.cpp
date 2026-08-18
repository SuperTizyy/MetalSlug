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

/**
 * @brief HP 阈值判定 — 比较 BB.HealthPercent 与配置阈值, 按 Less/Greater 模式返回
 * @param OwnerComp BT 组件引用, 用于获取 BB
 * @param NodeMemory Decorator 节点内存(本类未使用)
 * @return 血量百分比与阈值按模式比较的结果, BB 无效默认 false
 *
 * 单点决策: BT 每次重算分支时调用, 无 Tick. 血量变化通过 FlowAbortMode::Self
 * 在受击/死亡时自动重算, 不需要 Service 推 BB.
 */
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
