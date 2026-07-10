// Copyright (c) 2026.
//
// 【2026.07.13 v40.6 反扎堆重构】BTService — 反扎堆目标申请
//
// 职责:
//   - 周期调 URoomTargetingSubsystem::RequestTargetForAI 申请锁定目标
//   - 账本 (AIHuntingMap) 自动反扎堆: 已锁定的敌人不会被其他 AI 再锁定
//   - 账本单一真理源: TargetingSubsystem.AIHuntingMap
//
// 大厂原则:
//   - BT 为主, C++ 为辅: 原子能力在 BT 节点里, 不用 Script 决策
//   - 零兜底: Subsystem 不可用 / 无候选 → Log Error + Failed
//   - 单一真理源: 账本由 TargetingSubsystem 统一管理, Service 只读不写账本
//
// 频率: 默认 0.3s — 跟随战场变化但不抖动 (账本稳定锁定)

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTService.h"
#include "BehaviorTree/BehaviorTreeTypes.h"
#include "BTService_RefreshTarget.generated.h"

struct FBlackboardKeySelector;

/**
 * UBTService_RefreshTarget
 * 周期调账本申请锁定目标 (反扎堆账本驱动)
 */
UCLASS(Blueprintable, meta = (DisplayName = "Refresh Target (反扎堆账本)"))
class METALSLUG01_API UBTService_RefreshTarget : public UBTService
{
	GENERATED_BODY()

public:
	UBTService_RefreshTarget();

	virtual FString GetStaticDescription() const override;

	/** 目标 BB Key */
	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector TargetKey;

protected:
	virtual void TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory,
		float DeltaSeconds) override;
};
