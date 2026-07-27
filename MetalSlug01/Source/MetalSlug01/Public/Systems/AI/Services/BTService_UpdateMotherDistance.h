// Copyright (c) 2026.
//
// 【v110 2026.07.30 生化模式 AI】BTService — 更新人类与母体距离
//
// 职责:
//   - 读取 BB.NearestMotherTarget (母体目标)
//   - 计算 AI 与母体的平面距离 (忽略 Z 轴)
//   - 写入 BB.DistanceToMother (Float)
//   - 暴露 UpdateInterval 给 UE 编辑器配置
//
// 大厂原则 — BT 为主 C++ 为辅:
//   - 决策 (何时后退) → BTDecorator_MotherTooClose (读取 BB)
//   - 原子能力 (后退执行) → BTTask_MoveAwayFromTarget (复用已有)
//
// 历史:
//   - v110: 新建此 Service, 支持 "Human: Last Mother Pursuit" 分支的后退检测
//           替代原有的"距离判断"在 Service 内部硬编码 (不符合可配置原则)
//
// 配套:
//   - BTDecorator_MotherTooClose (同模块, 读取 DistanceToMother 判断是否后退)
//   - BTTask_MoveAwayFromTarget (复用 Melee 的后退原子能力)

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTService.h"
#include "BehaviorTree/BehaviorTreeTypes.h"
#include "BTService_UpdateMotherDistance.generated.h"

class ABaseCharacter;

/**
 * 生化模式人类 AI — 更新与母体距离的 BTService
 *
 * 编辑器配置 (BT 根节点挂):
 *   - MotherTargetKey: BB.NearestMotherTarget (Object, 母体目标)
 *   - DistanceKey: BB.DistanceToMother (Float, 写入距离)
 *   - UpdateIntervalKey: (不写 BB) 直接用本 Service 的 Interval 属性
 *
 * 使用方式:
 *   1. 在 BT_ZombieModeAI 根节点挂本 Service
 *   2. 设置 Interval 为需要的查询间隔 (例如 0.2s)
 *   3. BTDecorator_MotherTooClose 读取 DistanceKey 判断是否后退
 *
 * 不做:
 *   - 不写 BB.AttackRange (那是 BTService_UpdateZombieTargets 的职责)
 *   - 不做决策 (只派生事实, 决策归 Decorator)
 */
UCLASS(Blueprintable, meta = (DisplayName = "Update Mother Distance (母体距离)"))
class METALSLUG01_API UBTService_UpdateMotherDistance : public UBTService
{
	GENERATED_BODY()

public:
	UBTService_UpdateMotherDistance();

	virtual FString GetStaticDescription() const override;

protected:
	virtual void TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory,
		float DeltaSeconds) override;

private:
	/**
	 * 平面距离平方 (Z 轴忽略) — 避免楼层差误判
	 * 返回距离值 (cm)
	 */
	float ComputeFlatDistanceToMother(const FVector& AILocation, const FVector& MotherLocation) const;

	/** BB.NearestMotherTarget (Object Key) — 读取 */
	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector MotherTargetKey;

	/** BB.DistanceToMother (Float Key) — 写入 */
	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector DistanceKey;
};
