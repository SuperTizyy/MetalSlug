// Copyright (c) 2026.
//
// 【Phase 3 大厂架构】BTService — AI 目标刷新服务节点
//
// 定位:
//   - 挂在行为树根节点下方，作为 BT 生命周期内的"持续刷新层"
//   - 每隔 Interval 秒，自动调 RoomGameMode::RequestTargetForAI 获取仲裁分配的目标
//   - 把结果写入 BB 的 TargetActor Key，让 BT Selector 感知到目标变化
//
// 职责边界（绝对不写）:
//   - 不做寻路（MoveTo 交给 BTTask）
//   - 不做攻击判定（MeleeAttack 交给 UBTTask_MeleeAttack）
//   - 不做评分/排序（这些在 RoomGameMode::RequestTargetForAI）
//
// 刷新策略:
//   - Interval=0.3s（平衡：足够快响应目标切换，足够慢避免过度调用）
//   - bNotifyOnSearchOnly=false（保证每 interval 都执行）
//   - 若 GameMode 为空，写入 nullptr，BT 自然退到巡逻
//
// 依赖:
//   - ARoomGameMode::RequestTargetForAI 已就绪
//   - BB 资产中有 TargetActor (Object) Key

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/Services/BTService_BlackboardBase.h"
#include "BTService_RefreshTarget.generated.h"

class ARoomGameMode;

/**
 * UBTService_RefreshTarget
 *
 * UE AI 系统三层架构的核心桥梁：
 *   C++ 仲裁层（RoomGameMode） → BTService 写 BB → BT Selector 读 BB 决策
 *
 * 使用方式（UE 编辑器）：
 *   在 BT 蓝图根节点下挂本节点，设置：
 *     - BlackboardKey = TargetActor（必须指向 Object 类型的 BB Key）
 *     - Interval = 0.3s（默认）
 */
UCLASS(Blueprintable, meta = (DisplayName = "Refresh Target (GameMode Arbitration)"))
class METALSLUG01_API UBTService_RefreshTarget : public UBTService_BlackboardBase
{
	GENERATED_BODY()

public:
	UBTService_RefreshTarget();

protected:
	/**
	 * 每隔 Interval 秒执行一次
	 * 内部调用流程：
	 *   1. 获取 AIC -> Pawn -> World -> RoomGameMode
	 *   2. 调 RequestTargetForAI(Pawn) 获取仲裁结果
	 *   3. 把结果写入 BB->TargetActor
	 */
	virtual void OnBecomeRelevant(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;

	/**
	 * 节点离开树时（AI 死亡/切换 BT）：如果持有目标，通知 GameMode 释放锁定
	 */
	virtual void OnCeaseRelevant(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;

private:
	/**
	 * 内部刷新目标的核心逻辑
	 * 从 Pawn 出发逐级获取 World -> RoomGameMode
	 */
	void RefreshTargetForAI(UBehaviorTreeComponent& OwnerComp, AActor* Pawn);

	/**
	 * 释放当前持有的目标锁定（防止目标一直挂着）
	 */
	void ReleaseCurrentTarget(UBehaviorTreeComponent& OwnerComp, AActor* Pawn);

	/**
	 * 判定是否应该放弃当前目标
	 * 条件：目标死亡 / 超出最大追击距离 / 丢失超过一定时间
	 */
	bool ShouldAbandonTarget(UBehaviorTreeComponent& OwnerComp, AActor* Pawn,
		UBlackboardComponent* BB, const FName TargetKey) const;

	/**
	 * 获取本次刷新后 BB 里应该写入的目标 Actor
	 * 会自动处理目标失效的情况（死亡/超出范围）
	 */
	UObject* GetValidTargetOrNull(UBehaviorTreeComponent& OwnerComp, AActor* Pawn,
		UBlackboardComponent* BB, const FName TargetKey) const;
};
