// Copyright (c) 2026.
//
// 【P0 2026.07.08 BT 原子库】BTTask — 等待蒙太奇结束 (异步)
//
// 定位:
//   - 异步任务 — 返回 InProgress, 等蒙太奇 OnMontageEnded 回调
//   - 与 PlayAttackMontage 配对使用
//
// 异步机制 (关键 — 旧版容易踩坑):
//   - ExecuteTask 返回 InProgress 时, BT 框架把 NodeMemory 标识为 "latent task"
//   - 必须调 FinishLatentTask(OwnerComp, Result) 才返回结果
//   - 否则 BT 永远卡 InProgress
//
// 大厂实践 — 异步 Task 必须绑回调:
//   - 不能用 TickTask 轮询 (浪费 CPU + 时序不稳定)
//   - 必须用 FOnMontageEnded 委托 (引擎提供, 蒙太奇播完自动触发)

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BehaviorTree/BehaviorTreeTypes.h"
#include "Engine/TimerHandle.h"
#include "BTTask_WaitMontageFinish.generated.h"

class UAnimMontage;

/**
 * UBTTask_WaitMontageFinish
 * 等待 AI 当前播放的蒙太奇结束 (异步)
 *
 * 使用方式 (BT 编辑器):
 *   Sequence "Attack"
 *   ├─ BTTask_FaceTarget
 *   ├─ BTTask_PlayAttackMontage (同步触发)
 *   └─ BTTask_WaitMontageFinish (异步等结束)
 *
 * 流程:
 *   1. ExecuteTask: 拿到当前蒙太奇引用 + 实例名, 绑 OnMontageEnded 委托
 *      返回 InProgress
 *   2. 蒙太奇播完: 委托触发, FinishLatentTask(Succeeded)
 *   3. 超时 (默认 10s): 防卡死, 强制 FinishLatentTask(Failed)
 *   4. AbortTask: BT 中断时清理 (绑委托)
 */
UCLASS(Blueprintable, meta = (DisplayName = "Wait Montage Finish (等蒙太奇结束)"))
class METALSLUG01_API UBTTask_WaitMontageFinish : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_WaitMontageFinish();

	/** @brief BT 编辑器静态描述 (显示 TimeoutSeconds 默认 10s) */
	virtual FString GetStaticDescription() const override;

	/**
	 * 超时秒数 — 超过这个时间强制 Failed, 防卡死
	 * 默认 10s (覆盖最长连招蒙太奇)
	 */
	UPROPERTY(EditAnywhere, Category = "Time",
		meta = (ClampMin = "0.5", ClampMax = "30.0"))
	float TimeoutSeconds = 10.f;

protected:
	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp,
		uint8* NodeMemory) override;

	virtual EBTNodeResult::Type AbortTask(UBehaviorTreeComponent& OwnerComp,
		uint8* NodeMemory) override;

	virtual uint16 GetInstanceMemorySize() const override
	{
		return sizeof(FTaskMemory);
	}

	/** TickTask — 不再每帧检查, 只用作 Timer 兜底 */
	virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory,
		float DeltaSeconds) override;

	/** OnInstanceDestroyed — 清理 Timer */
	virtual void OnInstanceDestroyed(UBehaviorTreeComponent& OwnerComp) override;

private:
	/**
	 * Per-instance 状态
	 *
	 * 重要: UBTTaskNode 单例, TimerHandle 必须 per-instance 否则多 AI 串扰
	 */
	struct FTaskMemory
	{
		FTimerHandle TimeoutTimerHandle;
		TWeakObjectPtr<UBehaviorTreeComponent> OwnerCompRef;
		bool bFinished = false;
	};

	FORCEINLINE FTaskMemory& GetTaskMemory(uint8* NodeMemory) const
	{
		return *reinterpret_cast<FTaskMemory*>(NodeMemory);
	}

	/** Timer 到点回调 — 强制 FinishFailed 防卡死 */
	void OnTimeoutReached(UBehaviorTreeComponent* OwnerCompPtr);

	/** 清理 Timer + 引用 (内部辅助, 不重写 UBTNode::CleanupMemory) */
	void CleanupTimer(UBehaviorTreeComponent& OwnerComp, FTaskMemory& Mem);
};