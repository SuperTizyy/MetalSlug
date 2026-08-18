// Copyright (c) 2026.
//
// 【P0 v22 BT 决策库】BTDecorator — 距离判断：需要追击？

#include "Systems/AI/Decorators/BTDecorator_ShouldChase.h"

#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"

UBTDecorator_ShouldChase::UBTDecorator_ShouldChase()
{
	NodeName = TEXT("Should Chase?");

	bNotifyBecomeRelevant = false;
	bNotifyTick = false;

	// FlowAbortMode: Self
	// 当 AI 进入攻击区间时, ShouldChase 变 false, BT 中断 Chase 分支
	FlowAbortMode = EBTFlowAbortMode::Self;

	DistanceKey.AddFloatFilter(this,
		GET_MEMBER_NAME_CHECKED(UBTDecorator_ShouldChase, DistanceKey));

	AttackRangeKey.AddFloatFilter(this,
		GET_MEMBER_NAME_CHECKED(UBTDecorator_ShouldChase, AttackRangeKey));
}

/**
 * @brief 生成 BT 节点描述 — 显示追击判定表达式与迟滞阈值
 * @return 多行描述,包含 BB Key 与 "距离 > AR + Margin" 判断
 */
FString UBTDecorator_ShouldChase::GetStaticDescription() const
{
	return FString::Printf(TEXT("需要追击? (%s > %s+%.0f)\n"
		"→ true:  太远, 进入 Chase 分支\n"
		"→ false: 在攻击/撤退范围内, 不进入 Chase"),
		*DistanceKey.SelectedKeyName.ToString(),
		*AttackRangeKey.SelectedKeyName.ToString(),
		HysteresisMargin);
}

/**
 * @brief 追击距离判定 — 距离是否大于 (AR + Margin)
 * @param OwnerComp BT 组件引用, 用于获取 BB
 * @param NodeMemory Decorator 节点内存(本类未使用)
 * @return 距离大于阈值 → true (进入 Chase 分支), 否则 false
 *
 * 单点决策: FlowAbortMode::Self, AI 进入攻击区间后会自动中断 Chase 分支切换到 Attack.
 * 防御: BB 无效/Distance<0(无目标) → 拒判.
 */
bool UBTDecorator_ShouldChase::CalculateRawConditionValue(
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

	// 防御: Distance < 0 表示"无目标", 不应触发追击
	if (Distance < 0.f)
	{
		return false;
	}

	// 决策: Distance > (AttackRange + HysteresisMargin)
	const float ChaseThreshold = AttackRange + HysteresisMargin;

	return Distance > ChaseThreshold;
}
