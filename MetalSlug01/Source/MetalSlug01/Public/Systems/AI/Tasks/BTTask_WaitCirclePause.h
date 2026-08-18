// Copyright (c) 2026.
//
// 【v40.9 2026.07.13】BTTask — 攻击后环绕停顿 (Wait Circle Pause)
//
// 职责:
//   - 异步等待 ConfigSO.Combat.CirclePauseSeconds 秒后返回 Succeeded
//   - 给 AI 一个"调整呼吸/换脚"的拟人停顿
//
// 大厂原则:
//   - BT 决策"是否停顿", C++ 原子能力 (Timer 等待)
//   - UE 原生 Wait Task 没有"读 ConfigSO 暂停秒数"的能力, 必须自建 (符合原则)
//   - 单一真理源: CirclePauseSeconds 来自 ConfigSO.Combat (与 StrafeRadius 同一链路)
//   - 零兜底: ConfigSO 缺失/Config 缺失/<=0 → Failed (拒绝 fallback 到 0 = 没停顿)

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTTask_WaitCirclePause.generated.h"

class UBehaviorTreeComponent;
class AAIController;

/**
 * UBTTask_WaitCirclePause
 * 异步等待 ConfigSO.Combat.CirclePauseSeconds 秒后 Succeeded.
 * 用 bNotifyTick=true + TickTask 计时 — 比 TimerHandle 简单, 避免 BT 内存管理陷阱.
 */
UCLASS(Blueprintable, meta = (DisplayName = "Wait Circle Pause (环绕停顿原子)"))
class METALSLUG01_API UBTTask_WaitCirclePause : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_WaitCirclePause();

	/** @brief BT 编辑器静态描述 (显示 CirclePauseSeconds 来源 ConfigSO) */
	virtual FString GetStaticDescription() const override;
	/** @brief ExecuteTask 返回 InProgress, TickTask 累计 ElapsedSeconds >= TargetSeconds → Succeeded */
	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	/** @brief Tick: 累加 ElapsedSeconds, 达到 TargetSeconds 调 FinishLatentTask(Succeeded) */
	virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
	virtual EBTNodeResult::Type AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;

	/** 每个实例的内存 — 累计 Tick 已等待的秒数 */
	struct FWaitCircleMemory
	{
		float ElapsedSeconds = 0.f;
		float TargetSeconds = 0.f;
	};
	virtual uint16 GetInstanceMemorySize() const override { return sizeof(FWaitCircleMemory); }
};
