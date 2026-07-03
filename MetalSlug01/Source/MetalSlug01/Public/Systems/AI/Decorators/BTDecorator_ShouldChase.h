// Copyright (c) 2026.
//
// 【P0 v22 BT 决策库】BTDecorator — 距离判断：需要追击？
//
// 决策逻辑:
//   DistanceToTarget > (AttackRange + HysteresisMargin)
//   → true (太远, 追击)
//   → false (在攻击/撤退范围)

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTDecorator.h"
#include "BehaviorTree/BehaviorTreeTypes.h"
#include "BTDecorator_ShouldChase.generated.h"

UCLASS(Blueprintable, meta = (DisplayName = "Should Chase? (需要追击?)"))
class METALSLUG01_API UBTDecorator_ShouldChase : public UBTDecorator
{
	GENERATED_BODY()

public:
	UBTDecorator_ShouldChase();

	virtual FString GetStaticDescription() const override;

	/** 距离 BB Key */
	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector DistanceKey;

	/** 攻击距离 BB Key */
	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector AttackRangeKey;

	/**
	 * 迟滞半宽度 (cm) — 必须与 BTDecorator_InAttackRange 的 HysteresisMargin 保持一致
	 * 两者共用同一个 Margin, 才能保证 Attack 区间和 Chase 触发条件无缝衔接
	 */
	UPROPERTY(EditAnywhere, Category = "Config",
		meta = (ClampMin = "0.0", ClampMax = "100.0"))
	float HysteresisMargin = 10.f;

protected:
	virtual bool CalculateRawConditionValue(
		UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) const override;
};
