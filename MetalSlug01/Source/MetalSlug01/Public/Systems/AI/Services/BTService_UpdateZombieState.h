// Copyright (c) 2026.
//
// 【v107 2026.07.28 生化模式 AI】BTService — 身份 + 人数快照
//
// 职责:
//   - 读 ABaseCharacter::bIsMother / bIsHuman → 写 BB.bIsMother / BB.bIsHuman
//   - 读 URoomMotherMutationSubsystem::GetAliveMotherCount → 写 BB.AliveMotherCount
//   - 读 URoomMotherMutationSubsystem::GetAliveHumanCount  → 写 BB.AliveHumanCount
//   - 身份切换时清理不再合法的目标和移动请求 (大厂原则 — 模式切换一致性)
//
// 大厂原则:
//   - 派生事实 (Service 写 BB), 不做决策 (Decorator 决定)
//   - 频率: ConfigSO.ZombieTargetRefreshIntervalSeconds (默认 0.25s = 4Hz)
//   - 身份切换时必须清理 BB.TargetActor / BB.NearestHumanTarget / BB.NearestMotherTarget
//     防止 BT 在切换分支时拿到"上一个身份的旧目标"
//
// 数据流:
//   BTService_UpdateZombieState.Tick (0.25s)
//     ├─ 读 Pawn.bIsMother / bIsHuman (真理源 = ABaseCharacter 字段, 已 Replicated)
//     ├─ 读 MotherSubsystem.GetAliveMotherCount / GetAliveHumanCount (业务账本)
//     ├─ 写 BB.bIsMother / bIsHuman / AliveMotherCount / AliveHumanCount
//     └─ 身份切换 → 清 BB.TargetActor + NearestHuman/MotherTarget

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTService.h"
#include "BehaviorTree/BehaviorTreeTypes.h"
#include "BTService_UpdateZombieState.generated.h"

struct FBlackboardKeySelector;

/**
 * 生化模式身份 + 人数快照 Service
 *
 * 编辑器配置 (BT 根节点挂):
 *   - bIsMotherKey: BB.bIsMother
 *   - bIsHumanKey:  BB.bIsHuman
 *   - AliveMotherCountKey: BB.AliveMotherCount
 *   - AliveHumanCountKey:  BB.AliveHumanCount
 *   - TargetActorKey (清理用): BB.TargetActor
 *   - NearestHumanTargetKey / NearestMotherTargetKey (清理用): BB 对应 Key
 *   - Interval: ConfigSO.ZombieTargetRefreshIntervalSeconds 默认 0.25s
 */
UCLASS(Blueprintable, meta = (DisplayName = "Update Zombie State (身份+人数快照)"))
class METALSLUG01_API UBTService_UpdateZombieState : public UBTService
{
	GENERATED_BODY()

public:
	UBTService_UpdateZombieState();

	/** @brief BT 编辑器静态描述 (显示 7 个 BB Key 名 + Tick 频率) */
	virtual FString GetStaticDescription() const override;

	/** BB.bIsMother 写入目标 */
	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector bIsMotherKey;

	/** BB.bIsHuman 写入目标 */
	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector bIsHumanKey;

	/** BB.AliveMotherCount 写入目标 */
	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector AliveMotherCountKey;

	/** BB.AliveHumanCount 写入目标 */
	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector AliveHumanCountKey;

	/** 身份切换时清理的 TargetActor Key (防止旧身份目标残留) */
	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector TargetActorKey;

	/** 身份切换时清理的 NearestHumanTarget Key */
	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector NearestHumanTargetKey;

	/** 身份切换时清理的 NearestMotherTarget Key */
	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector NearestMotherTargetKey;

protected:
	virtual void TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory,
		float DeltaSeconds) override;

	/** 缓存上次身份, 用于检测切换 */
	bool bLastIsMother = false;
	bool bLastIsHuman = true;
	bool bHasLastIdentity = false;
};