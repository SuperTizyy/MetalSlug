// Copyright (c) 2026.
//
// 【P0 v22 BT 原子库】BTService — 距离观察 (派生原始事实)
//
// 架构定位 (重要):
//   - 本 Service 是"派生量周期写入器", 只做世界事实的派生
//   - 不做决策 (距离区间判断 → BTDecorator_C++ 决策节点)
//   - 不写事件型 BB 值 (冷却 → BTTask_PlayAttackMontage 写一次)
//
// 数据流:
//   BTService_UpdateDistance.Tick (0.1s)
//     ├─ 读 BB.TargetActor
//     ├─ ComputeActorCenterDistance (C++ 原子)
//     ├─ 写 BB.DistanceToTarget  (cm, -1 = 无目标)
//     ├─ 写 BB.bHasTarget        (派生 Bool: TargetActor != nullptr)
//     └─ 写 BB.AttackRange       (派生 ConfigSO.AttackRange 含难度缩放)
//
// 决策节点的喂给:
//   - BTDecorator_TooClose:        读 DistanceToTarget vs AttackRange → D ≤ AR → Retreat
//   - BTDecorator_InAttackRange:   读 DistanceToTarget vs AttackRange → AR-M ≤ D ≤ AR+M → Attack
//   - BTDecorator_ShouldChase:    读 DistanceToTarget vs AttackRange → D > AR+M → Chase
//   - BTDecorator_CooldownReady:  读 CooldownEndTime vs WorldTime → 冷却判断
//   - BTDecorator_HPThreshold:     读 HealthPercent → 血量判断
//
// 大厂原则:
//   - 单一职责 (SRP): 1 个 Service 只派生事实
//   - 周期合理: 0.1s (10Hz) 对距离足够, 比 AIPerception 更实时, 比 Tick 节流
//   - 派生频率统一: 派生事实与决策节点同频刷新, 无状态延迟

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTService.h"
#include "BehaviorTree/BehaviorTreeTypes.h"
#include "BTService_UpdateDistance.generated.h"

struct FBlackboardKeySelector;

/**
 * UBTService_UpdateDistance
 * 派生量周期写入器 — 只派生世界事实 (DistanceToTarget + bHasTarget + AttackRange)
 *
 * 使用方式 (BT 编辑器):
 *   BT_Grunt 根节点 Selector 上挂本 Service
 *   在 Details 面板配 BB Key 字段:
 *     - TargetKey:    BB.TargetActor
 *     - DistanceKey:  BB.DistanceToTarget
 *     - HasTargetKey: BB.bHasTarget   (派生 Bool, 可选)
 *     - AttackRangeKey: BB.AttackRange (派生 ConfigSO, 可选)
 *   Interval = 0.1s (默认)
 */
UCLASS(Blueprintable, meta = (DisplayName = "Update Distance (距离观察)"))
class METALSLUG01_API UBTService_UpdateDistance : public UBTService
{
	GENERATED_BODY()

public:
	UBTService_UpdateDistance();

	/** @brief BT 编辑器静态描述 (显示 TargetKey + DistanceKey + Tick 频率) */
	virtual FString GetStaticDescription() const override;

	/** 目标 BB Key — 服务从这里读目标 Actor */
	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector TargetKey;

	/** 距离 BB Key — 服务把算出的距离写到这里 (cm, -1 = 无目标) */
	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector DistanceKey;

	/** 是否有目标 BB Key — 派生 Bool (TargetActor != nullptr), 可选 */
	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector HasTargetKey;

	/** 攻击距离 BB Key — Float, cm。派生 ConfigSO.AttackRange 含难度缩放 */
	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector AttackRangeKey;

protected:
	virtual void TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory,
		float DeltaSeconds) override;
};
