// Copyright (c) 2026.
//
// 【v40.8.4 2026.07.13】BTService — 初始化 BB.WanderHome (AI 漫游中心)
//
// 职责:
//   - BT 启动后第一次 Tick 时, 把 AI Pawn 当前位置写入 BB.WanderHome
//   - 单次执行 (写完即不再写), 由 bNotifyActivate=true 实现
//
// 大厂原则:
//   - BT 负责 BB 写入 (与 BTService_RefreshTarget 写入 BB.TargetActor 同一模式)
//   - 不在 OnPossess 写入 — UE 5.6 GetBlackboardComponent() 在 RunBehaviorTree 前返回 nullptr
//   - 单次执行: 必须 bNotifyActivate=true (OnBecomeRelevant 回调) — 避免每 Tick 重写覆盖

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTService.h"
#include "BehaviorTree/BehaviorTreeTypes.h"
#include "BTService_InitWanderHome.generated.h"

/**
 * UBTService_InitWanderHome
 * 单次执行 Service: BT 启动时把 AI 当前位置写入 BB.WanderHome (漫游中心)
 *
 * 挂载位置: BT 根 Selector 或 Wander 分支
 *
 * 为什么需要:
 *   - BB.WanderHome 是漫游随机选点的中心
 *   - 不能在 OnPossess 写 (BB 未实例化)
 *   - 不能每 Tick 重写 (会覆盖 AI 跑远时的"假出生点")
 *   - 必须首次 Tick 时一次写入
 */
UCLASS(Blueprintable, meta = (DisplayName = "Init Wander Home (单次写入出生点)"))
class METALSLUG01_API UBTService_InitWanderHome : public UBTService
{
	GENERATED_BODY()

public:
	UBTService_InitWanderHome();

	/** @brief BT 编辑器静态描述 (显示 WanderHomeKey) */
	virtual FString GetStaticDescription() const override;

	/**
	 * 漫游中心 BB Key (Vector)
	 *
	 * 默认写到 Key 名 = "WanderHome"
	 * 实战上 BT 编辑器应该把这个 Field 绑到 BB.WanderHome
	 * 也可绑其他 Vector Key — 由调用方决定
	 */
	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector WanderHomeKey;

protected:
	/** @brief 单次执行入口: BT 启动时把 AI 当前位置写入 BB.WanderHome (bNotifyActivate=true) */
	virtual void OnBecomeRelevant(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	/** @brief Tick 守卫: 首次写入后 bAlreadyInitialized=true, 后续 Tick 直接跳过 */
	virtual void TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
};