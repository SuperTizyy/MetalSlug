// Copyright (c) 2026.
//
// 【P0 2026.07.08 BT 原子库】BTService — 目标刷新 (感知失效兜底)
//
// 定位:
//   - 周期检查 BB.TargetActor 是否还有效
//   - 失效时调 AIController::ScanForNearestEnemy 重新找
//   - 兜底: AIPerception 漏触发时 (玩家走出视野又走回来), BT 仍能恢复追踪
//
// 频率: 默认 0.3s — 不需要每 0.1s, 玩家不会 0.3s 内消失又出现
// 降 CPU: 比 AIPerception 自带 OnTargetPerceptionUpdated 更鲁棒 (扫描兜底)

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTService.h"
#include "BehaviorTree/BehaviorTreeTypes.h"
#include "BTService_RefreshTarget.generated.h"

struct FBlackboardKeySelector;

/**
 * UBTService_RefreshTarget
 * 周期性检查目标, 失效则重新扫描
 */
UCLASS(Blueprintable, meta = (DisplayName = "Refresh Target (刷新目标)"))
class METALSLUG01_API UBTService_RefreshTarget : public UBTService
{
	GENERATED_BODY()

public:
	UBTService_RefreshTarget();

	virtual FString GetStaticDescription() const override;

	/** 目标 BB Key */
	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector TargetKey;

	/** 扫描半径 (cm) — 超过这个距离的目标不重新选 */
	UPROPERTY(EditAnywhere, Category = "Perception",
		meta = (ClampMin = "100.0", ClampMax = "10000.0"))
	float ScanRadius = 3000.f;

protected:
	virtual void TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory,
		float DeltaSeconds) override;
};