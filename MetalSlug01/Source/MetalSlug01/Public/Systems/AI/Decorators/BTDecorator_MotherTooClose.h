// Copyright (c) 2026.
//
// 【v110 2026.07.30 生化模式 AI】BTDecorator — 母体太近则后退
//
// 职责:
//   - 读取 BB.DistanceToMother (Float)
//   - 比较 RetreatDistanceThreshold (从 ConfigSO 派生)
//   - 返回 true (允许进入后退分支) 当 DistanceToMother < RetreatDistanceThreshold
//   - 返回 false (阻止进入后退分支) 当 DistanceToMother >= RetreatDistanceThreshold
//
// 大厂原则 — BT 为主 C++ 为辅:
//   - 何时后退 ← Decorator 决策 (基于 BB 事实 + ConfigSO 参数)
//   - 如何后退 ← BTTask_MoveAwayFromTarget (原子能力)
//
// 使用方式 (UE 编辑器):
//   1. 在 "Human: Last Mother Pursuit" 分支的 "后退守卫" 装饰器中配置
//   2. DistanceKey = BB.DistanceToMother (由 BTService_UpdateMotherDistance 写入)
//   3. ConfigSO 中的 RetreatDistanceThreshold 自动生效 (无需手动配置)

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTDecorator.h"
#include "BehaviorTree/BehaviorTreeTypes.h"
#include "Data/AI/AIBehaviorConfigSO.h"
#include "BTDecorator_MotherTooClose.generated.h"

class ABaseCharacter;
class UAIRuntimeConfigComponent;

/**
 * 生化模式人类 AI — 母体太近则后退的 Decorator
 *
 * 编辑器配置:
 *   - DistanceKey: BB.DistanceToMother (Float)
 *   - RetreatDistanceThreshold: 从 ConfigSO 派生 (Zombie|Retreat 段)
 *
 * 语义:
 *   - Distance < RetreatDistanceThreshold → 返回 true (允许后退)
 *   - Distance >= RetreatDistanceThreshold → 返回 false (阻止后退)
 *   - 无目标 (Dist<0) → 返回 false (不后退)
 *   - ConfigSO 为空 → Log Error + 阻止后退
 */
UCLASS(Blueprintable, meta = (DisplayName = "Mother Too Close (母体太近后退)"))
class METALSLUG01_API UBTDecorator_MotherTooClose : public UBTDecorator
{
	GENERATED_BODY()

public:
	UBTDecorator_MotherTooClose();

	virtual FString GetStaticDescription() const override;

protected:
	/**
	 * 计算装饰器条件值
	 *
	 * @param OwnerComp BT 所有者组件
	 * @param NodeMemory 本节点内存
	 * @return true: 允许进入分支; false: 阻止进入分支
	 *
	 * 逻辑:
	 *   1. 读取 BB.DistanceToMother
	 *   2. 从 AIController->RuntimeConfig->GetConfig() 读取 RetreatDistanceThreshold
	 *   3. 若无目标 (Distance < 0) → 返回 false (不后退)
	 *   4. 若 Distance < Threshold → 返回 true (后退)
	 *   5. 若 Distance >= Threshold → 返回 false (不后退)
	 */
	virtual bool CalculateRawConditionValue(
		UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) const override;

	/** BB.DistanceToMother (Float Key) — 读取 */
	UPROPERTY(EditAnywhere, Category = "Blackboard",
		meta = (DisplayName = "Distance Key (距离 Key)"))
	FBlackboardKeySelector DistanceKey;
};
