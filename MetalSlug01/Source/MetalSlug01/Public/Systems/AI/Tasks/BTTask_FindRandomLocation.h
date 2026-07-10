// Copyright (c) 2026.
//
// 【v40.8 2026.07.13】BTTask — 漫游原子能力 (NavMesh 随机点)
//
// 职责:
//   - 在 NavMesh 上以 OriginLocation 为中心, WanderRadius 为半径随机选一个可达点
//   - 写入 BB.WanderTarget
//   - 同步返回 Succeeded (不阻塞 BT)
//
// 大厂原则:
//   - BT 负责决策 (何时漫游), C++ 提供原子能力 (NavMesh API)
//   - UE 没有内置 BTTask_FindRandomLocation, 必须自建 (符合"功能无法实现再考虑 C++"原则)
//   - 单一真理源: WanderRadius 来自 DataAsset → AIRuntimeConfigComponent (在 BTTask 内读)
//
// 边界:
//   - Owner 无效 → Log Error + Failed (零兜底)
//   - NavSystem 无效 → Log Error + Failed
//   - BB 无效 → Log Error + Failed
//   - 找点失败 (NavMesh 半径内无有效点) → Log Error + Failed (不允许 fallback 到当前位置)

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BehaviorTree/BehaviorTreeTypes.h"
#include "BTTask_FindRandomLocation.generated.h"

/**
 * UBTTask_FindRandomLocation
 * 在 NavMesh 上以 OriginLocation (BB Vector Key) 为中心, WanderRadius (来自 ConfigSO) 为半径
 * 随机选可达点, 写入 WanderTarget (BB Vector Key)
 */
UCLASS(Blueprintable, meta = (DisplayName = "Find Random Location (漫游原子)"))
class METALSLUG01_API UBTTask_FindRandomLocation : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_FindRandomLocation();

	virtual FString GetStaticDescription() const override;

	/**
	 * 漫游中心 BB Key (Vector) — 通常 = WanderHome (出生点) 或 SelfActor.Location
	 *
	 * 设计: 用出生点作为漫游中心 (而不是当前位置) — AI 会"回家附近", 行为更自然
	 *      如果 WanderHome 未初始化, BT 编辑器应配置为 GetActorLocation (SelfActor)
	 */
	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector OriginLocationKey;

	/**
	 * 漫游目标 BB Key (Vector) — 输出目标点
	 * BTTask_MoveTo 应绑定此 Key
	 */
	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector WanderTargetKey;

protected:
	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
};
