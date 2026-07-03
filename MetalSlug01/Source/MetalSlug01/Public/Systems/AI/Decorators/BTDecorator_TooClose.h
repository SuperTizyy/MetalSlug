// Copyright (c) 2026.
//
// 【P0 v22 BT 决策库】BTDecorator — 距离判断：太近？
//
// 架构定位:
//   - 决策节点 (做条件判断), 读事实 (DistanceToTarget / AttackRange)
//   - 用 FBlackboardKeySelector (大厂标准) 让 BT 编辑器手动绑定 Key
//
// 决策逻辑:
//   DistanceToTarget <= AttackRange → true (太近, 撤退)
//
// 使用方式 (BT 编辑器):
//   Details 面板:
//     - DistanceKey   → "DistanceToTarget"
//     - AttackRangeKey → "AttackRange"
//   FlowAbortMode: Self

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTDecorator.h"
#include "BehaviorTree/BehaviorTreeTypes.h"
#include "BTDecorator_TooClose.generated.h"

UCLASS(Blueprintable, meta = (DisplayName = "Too Close? (太近?)"))
class METALSLUG01_API UBTDecorator_TooClose : public UBTDecorator
{
	GENERATED_BODY()

public:
	UBTDecorator_TooClose();

	virtual FString GetStaticDescription() const override;

	/** 距离 BB Key — 派生量, Service 0.1s 写入 */
	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector DistanceKey;

	/** 攻击距离 BB Key — 派生自 ConfigSO (含难度缩放), Service 0.1s 写入 */
	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector AttackRangeKey;

protected:
	virtual bool CalculateRawConditionValue(
		UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) const override;
};
