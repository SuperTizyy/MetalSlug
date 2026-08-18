// Copyright (c) 2026.
//
// 【P0 v22 BT 决策库】BTDecorator — 距离判断：太近？

#include "Systems/AI/Decorators/BTDecorator_TooClose.h"

#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"

UBTDecorator_TooClose::UBTDecorator_TooClose()
{
	NodeName = TEXT("Too Close?");

	bNotifyBecomeRelevant = false;
	bNotifyTick = false;

	// FlowAbortMode: Self
	// 当 BT 已在 Attack/Chase 分支运行时, 如果 Distance <= AR, 需要自我中断去 Retreat
	FlowAbortMode = EBTFlowAbortMode::Self;

	// BB Key 类型过滤 (大厂标准做法)
	DistanceKey.AddFloatFilter(this,
		GET_MEMBER_NAME_CHECKED(UBTDecorator_TooClose, DistanceKey));

	AttackRangeKey.AddFloatFilter(this,
		GET_MEMBER_NAME_CHECKED(UBTDecorator_TooClose, AttackRangeKey));
}

/**
 * @brief 生成 BT 节点描述 — 显示太近判定表达式
 * @return 多行描述,包含 BB Key 与 "距离 < AR" 判断
 */
FString UBTDecorator_TooClose::GetStaticDescription() const
{
	return FString::Printf(TEXT("太近? (%s < %s)\n"
		"→ true:  AI 太近, 进入 Retreat 分支\n"
		"→ false: 安全距离, 不进入 Retreat"),
		*DistanceKey.SelectedKeyName.ToString(),
		*AttackRangeKey.SelectedKeyName.ToString());
}

bool UBTDecorator_TooClose::CalculateRawConditionValue(
	UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) const
{
	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	if (!BB)
	{
		return false;
	}

	// 读 BB Key (策划在 BT 资产 Details 面板绑定的 Key 名)
	const float Distance = BB->GetValueAsFloat(DistanceKey.SelectedKeyName);
	const float AttackRange = BB->GetValueAsFloat(AttackRangeKey.SelectedKeyName);

	// 防御: Distance < 0 表示"无目标", 不应触发撤退
	if (Distance < 0.f)
	{
		return false;
	}

	// 决策 v23.1: Distance < AttackRange → 太近 (严格小于, 边界值走 Attack)
	return Distance < AttackRange;
}
