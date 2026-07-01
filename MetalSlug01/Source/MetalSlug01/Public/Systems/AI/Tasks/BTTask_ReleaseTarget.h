// Copyright (c) 2026.
//
// 【Phase 3 大厂架构】BTTask — AI 死亡/退出时释放目标锁定
//
// 定位:
//   - 挂在 BT 的"死亡"叶节点，或 AI 的 OnDeath 回调
//   - 通知 RoomGameMode 从猎人账本里移除本 AI 的记录
//
// 职责边界（绝对不写）:
//   - 不做死亡动画/死亡清理（那些在 BaseCharacter::Die 里）
//   - 不做复活（复活后 Service 自然重新申请目标）
//
// 使用场景:
//   1. BT 里的死亡 Sequence（叶子节点）
//   2. BaseCharacter 死亡回调里通过 GameMode 调用（非 BT 路径）

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTTask_ReleaseTarget.generated.h"

class ARoomGameMode;

/**
 * UBTTask_ReleaseTarget
 *
 * AI 退出目标锁定的唯一出口
 * 在 BT 死亡分支的叶子节点调用，或在 Character 死亡事件中调用
 */
UCLASS(Blueprintable, meta = (DisplayName = "Release Target (放弃目标)"))
class METALSLUG01_API UBTTask_ReleaseTarget : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_ReleaseTarget();

protected:
	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp,
		uint8* NodeMemory) override;

	virtual FString GetStaticDescription() const override;
};
