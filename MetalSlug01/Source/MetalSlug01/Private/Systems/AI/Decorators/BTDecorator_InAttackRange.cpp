// Copyright (c) 2026.
//
// 【P0 v22 BT 决策库】BTDecorator — 距离判断：可攻击？

#include "Systems/AI/Decorators/BTDecorator_InAttackRange.h"

#include "AIController.h"
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

/**
 * @brief 生成 BT 节点描述 — 显示攻击区间判断表达式与迟滞边界
 * @return 多行描述,包含 BB Key 与 AR ± Margin 区间,用于 BT 编辑器可视化
 */
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

/**
 * @brief 攻击距离判定 — 距离是否在 [AR-Margin, AR+Margin] 区间内
 * @param OwnerComp BT 组件引用, 用于获取 BB / AIController
 * @param NodeMemory Decorator 节点内存(本类未使用)
 * @return 在区间内且目标有效 → true(允许进入 Attack 分支)
 *
 * 单点决策: 无 Tick, 距离变化由 FlowAbortMode::Self 在 BB.Distance 变化时重算.
 * 三层防御: BB 无效/Distance<0(无目标)/不在区间 → 全部返 false 拒判.
 * v40.5 节点级可观测性:Verbose Log 默认隐藏, 调试时开启可看到决策过程.
 */
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
		// 【v40.5 P0 诊断】Distance 无效 → 拒判
		//   这是用户反馈"AI 卡在 PlayAttackMontage"的常见根因:
		//   TargetActor 空 → BTService_UpdateDistance 写 -1 → 装饰器永远拒判
		UE_LOG(LogTemp, Verbose,
			TEXT("[BTDecorator_InAttackRange] %s: D=%.1f < 0 (无目标) → 拒判 Attack 分支"),
			*GetNameSafe(OwnerComp.GetAIOwner()), Distance);
		return false;
	}

	// 决策: (AR - Margin) <= D <= (AR + Margin)
	const float RangeMin = AttackRange - HysteresisMargin;
	const float RangeMax = AttackRange + HysteresisMargin;
	const bool bInRange = Distance >= RangeMin && Distance <= RangeMax;

	// 【v40.5 P0 诊断】距离判断 — Verbose (高频, 默认不刷屏)
	UE_LOG(LogTemp, Verbose,
		TEXT("[BTDecorator_InAttackRange] %s: D=%.0f vs [AR=%.0f - %.0f, AR=%.0f + %.0f] → %s"),
		*GetNameSafe(OwnerComp.GetAIOwner()),
		Distance, AttackRange, HysteresisMargin, AttackRange, HysteresisMargin,
		bInRange ? TEXT("PASS") : TEXT("FAIL"));

	return bInRange;
}
