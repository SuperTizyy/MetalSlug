// Copyright (c) 2026.
//
// 【P0 2026.07.08 BT 原子库】BTTask — 死亡 (播放死亡动画 + Destroy)
//
// 定位:
//   - 终结节点 — 走完死亡逻辑, AI 销毁
//   - 由 Decorator "HP == 0" 上游触发
//
// 行为:
//   - ExecuteTask: 停止所有蒙太奇 + 播放 Death Montage + Destroy
//   - 同步完成 — Destroy 后 BT 自动停止, 无需 InProgress

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTTask_PlayDeath.generated.h"

/**
 * UBTTask_PlayDeath
 * 终止 — 死亡播放 + 销毁 Actor
 */
UCLASS(Blueprintable, meta = (DisplayName = "Play Death (死亡)"))
class METALSLUG01_API UBTTask_PlayDeath : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_PlayDeath();

	virtual FString GetStaticDescription() const override;

protected:
	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp,
		uint8* NodeMemory) override;
};