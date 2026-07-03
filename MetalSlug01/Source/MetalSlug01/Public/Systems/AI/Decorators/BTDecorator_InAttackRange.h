// Copyright (c) 2026.
//
// 【P0 v22 BT 决策库】BTDecorator — 距离判断：可攻击？
//
// 决策逻辑:
//   (AttackRange - HysteresisMargin) <= DistanceToTarget <= (AttackRange + HysteresisMargin)
//   → true (在攻击区间内, 攻击)

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTDecorator.h"
#include "BehaviorTree/BehaviorTreeTypes.h"
#include "BTDecorator_InAttackRange.generated.h"

UCLASS(Blueprintable, meta = (DisplayName = "In Attack Range? (可攻击?)"))
class METALSLUG01_API UBTDecorator_InAttackRange : public UBTDecorator
{
	GENERATED_BODY()

public:
	UBTDecorator_InAttackRange();

	virtual FString GetStaticDescription() const override;

	/** 距离 BB Key */
	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector DistanceKey;

	/** 攻击距离 BB Key */
	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector AttackRangeKey;

	/**
	 * 迟滞半宽度 (cm) — 策划可调 (BT 资源层调)
	 *
	 * 作用: 扩大"可攻击"区间, 避免 AI 在 AR 边界上来回抖动
	 *   - 1 个 BT 资源里所有 Decorator 手动保持一致
	 *   - 必须与 BTDecorator_ShouldChase 的 HysteresisMargin 保持一致
	 *     才能让 Attack 区间和 Chase 触发条件无缝衔接
	 */
	UPROPERTY(EditAnywhere, Category = "Config",
		meta = (ClampMin = "0.0", ClampMax = "100.0"))
	float HysteresisMargin = 10.f;

protected:
	virtual bool CalculateRawConditionValue(
		UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) const override;
};
