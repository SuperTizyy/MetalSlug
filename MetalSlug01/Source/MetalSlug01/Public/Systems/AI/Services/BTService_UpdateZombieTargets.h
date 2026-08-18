// Copyright (c) 2026.
//
// 【v107 2026.07.28 生化模式 AI】BTService — 母体/人类目标选择
//
// 职责:
//   - 母体: 选最近存活人类 (AI 或 玩家), 写入 BB.NearestHumanTarget + BB.TargetActor
//   - 人类: 选最近存活母体, 写入 BB.NearestMotherTarget + BB.TargetActor
//
// 大厂原则 (用户明确):
//   - 不引入仇恨值 / 评分 / AIHuntingMap 反扎堆
//   - 严格按距离最近选 — 业务简单, 不复杂化
//   - 母体攻击距离 (ConfigSO.ZombiePrimaryFireRange) 控制最大扫描范围
//   - 人类攻击距离 (ConfigSO.ZombiePrimaryFireRange) 控制最大扫描范围 — 同字段复用
//
// 不做:
//   - 不接管 ABaseAIController::RequestTargetForAI (那是 Melee 用)
//   - 不写 BB.DistanceToTarget (那是 BTService_UpdateDistance 的职责, 由原生 BT 用)
//   - 不重选"已经被锁"的目标 (无反扎堆)

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTService.h"
#include "BehaviorTree/BehaviorTreeTypes.h"
#include "BTService_UpdateZombieTargets.generated.h"

struct FBlackboardKeySelector;

/**
 * 生化模式目标选择 Service (按距离最近)
 *
 * 编辑器配置 (BT 根节点挂):
 *   - NearestHumanTargetKey: BB.NearestHumanTarget (Object, 母体专用)
 *   - NearestMotherTargetKey: BB.NearestMotherTarget (Object, 人类专用)
 *   - TargetActorKey: BB.TargetActor (写入"当前身份应追的目标", 镜像上面任一)
 *   - MaxRangeKey: BB.PrimaryFireRange (Float, 可选, 派生 ConfigSO.ZombiePrimaryFireRange)
 */
UCLASS(Blueprintable, meta = (DisplayName = "Update Zombie Targets (目标选择)"))
class METALSLUG01_API UBTService_UpdateZombieTargets : public UBTService
{
	GENERATED_BODY()

public:
	UBTService_UpdateZombieTargets();

	/** @brief BT 编辑器静态描述 (显示 5 个 BB Key + Tick 频率) */
	virtual FString GetStaticDescription() const override;

	/** 母体 → 最近人类 (Object Key) */
	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector NearestHumanTargetKey;

	/** 人类 → 最近母体 (Object Key) */
	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector NearestMotherTargetKey;

	/** BB.TargetActor 写入目标 (通用, 镜像当前身份的目标) */
	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector TargetActorKey;

	/** BB.PrimaryFireRange (Float, 可选) — 派生 ConfigSO.ZombiePrimaryFireRange */
	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector PrimaryFireRangeKey;

	/**
	 * 【v108 大厂架构】BB.HomeLocation 写入目标 (未知身份时的默认移动目标)
	 *
	 * 业务规则:
	 *   - 当 bIsMother=false && bIsHuman=false (变异前/过渡期)
	 *     时, 无有效目标, 应写入 HomeLocation 让 AI 待在原地或返回出生点
	 *   - 未知身份时写入 SelfPawn 的初始位置 (出生点)
	 *   - 已确定身份时清零 (不影响正常行为)
	 *
	 * 调用方 (BT):
	 *   - Pre-Mutation 序列的 Move To TargetActor 失败时,
	 *     用 HomeLocation 作为后备移动目标
	 *   - BB.HomeLocation 需要被标记为 "Allow BlackBoard Key Activation" (UE 默认)
	 */
	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector HomeLocationKey;

protected:
	/** @brief Tick: 母体选最近人类 / 人类选最近母体 → 写 BB.TargetActor + NearestXxx + HomeLocation */
	virtual void TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory,
		float DeltaSeconds) override;
};