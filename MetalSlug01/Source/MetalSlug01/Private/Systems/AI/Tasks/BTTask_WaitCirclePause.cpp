// Copyright (c) 2026.
//
// 【v40.9 2026.07.13】BTTask — 攻击后环绕停顿 (Wait Circle Pause)
// 实现: 用 TickTask 计时 (同步内存 + 累加 DeltaSeconds), 不依赖 TimerHandle (避免 BT 内存管理陷阱)

#include "Systems/AI/Tasks/BTTask_WaitCirclePause.h"

#include "BehaviorTree/BehaviorTreeComponent.h"
#include "AIController.h"
#include "Engine/World.h"

#include "Systems/BaseAIController.h"
#include "Systems/AI/AIRuntimeConfigComponent.h"
#include "Data/AI/AIBehaviorConfigSO.h"

UBTTask_WaitCirclePause::UBTTask_WaitCirclePause()
{
	NodeName = TEXT("Wait Circle Pause (环绕停顿)");

	// 异步任务 — 用 Tick 计时
	bNotifyTick = true;
	bNotifyTaskFinished = false; // 我们在 Tick 内部调 FinishLatentTask, 不需要 Finished 回调
}

FString UBTTask_WaitCirclePause::GetStaticDescription() const
{
	return TEXT("【攻击后环绕原子能力】异步等待 ConfigSO.Combat.CirclePauseSeconds 秒后 Succeeded.\n"
		"用 TickTask 计时, 无 TimerHandle 复杂度.\n"
		"失败 = Failed (无兜底). ConfigSO.Combat.CirclePauseSeconds 单一真理源.");
}

EBTNodeResult::Type UBTTask_WaitCirclePause::ExecuteTask(
	UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	Super::ExecuteTask(OwnerComp, NodeMemory);

	UWorld* World = OwnerComp.GetWorld();
	if (!World)
	{
		UE_LOG(LogBehaviorTree, Error, TEXT("[BTTask_WaitCirclePause] World 无效."));
		return EBTNodeResult::Failed;
	}

	AAIController* AIC = OwnerComp.GetAIOwner();
	if (!AIC)
	{
		UE_LOG(LogBehaviorTree, Error, TEXT("[BTTask_WaitCirclePause] AIController 无效."));
		return EBTNodeResult::Failed;
	}

	ABaseAIController* BaseAIC = Cast<ABaseAIController>(AIC);
	if (!BaseAIC)
	{
		UE_LOG(LogBehaviorTree, Error,
			TEXT("[BTTask_WaitCirclePause] AIC=%s 不是 ABaseAIController 派生."), *AIC->GetName());
		return EBTNodeResult::Failed;
	}

	const UAIRuntimeConfigComponent* RuntimeConfig = BaseAIC->GetRuntimeConfig();
	if (!RuntimeConfig)
	{
		UE_LOG(LogBehaviorTree, Error,
			TEXT("[BTTask_WaitCirclePause] AIC=%s RuntimeConfig 组件不可用."), *AIC->GetName());
		return EBTNodeResult::Failed;
	}

	const UAIBehaviorConfigSO* Config = RuntimeConfig->GetConfig();
	if (!Config)
	{
		UE_LOG(LogBehaviorTree, Error,
			TEXT("[BTTask_WaitCirclePause] AIC=%s ConfigSO 未应用."), *AIC->GetName());
		return EBTNodeResult::Failed;
	}

	const FAICombatParams Combat = RuntimeConfig->GetScaledCombat();

	// 【零兜底】< 0 或 NaN = ConfigSO 配错, 拒绝 fallback 到 0 (没停顿就是退化)
	//   但 = 0 也允许 (策划可以设 0 表示不要停顿 — 这是有意图, 不是错)
	if (Combat.CirclePauseSeconds < 0.f || FMath::IsNaN(Combat.CirclePauseSeconds))
	{
		UE_LOG(LogBehaviorTree, Error,
			TEXT("[BTTask_WaitCirclePause] AIC=%s CirclePauseSeconds=%.3f 非法 (< 0 或 NaN). "
			     "【v54 修复】在 DA_AIBehaviorConfig_MeleeGrunt.uasset → Combat → Circle → CirclePauseSeconds 设置为 >= 0 (DA_AIProfile_*.uasset 已删除)."),
			*AIC->GetName(), Combat.CirclePauseSeconds);
		return EBTNodeResult::Failed;
	}

	FWaitCircleMemory* Mem = reinterpret_cast<FWaitCircleMemory*>(NodeMemory);
	Mem->ElapsedSeconds = 0.f;
	Mem->TargetSeconds = Combat.CirclePauseSeconds;

	// 【性能优化】CirclePauseSeconds = 0 → 直接 Succeeded, 不走 Tick
	if (Mem->TargetSeconds <= KINDA_SMALL_NUMBER)
	{
		UE_LOG(LogBehaviorTree, Verbose,
			TEXT("[BTTask_WaitCirclePause] AIC=%s TargetSeconds=0 → 直接 Succeeded (策划设 0 跳过停顿)"),
			*AIC->GetName());
		return EBTNodeResult::Succeeded;
	}

	UE_LOG(LogBehaviorTree, Verbose,
		TEXT("[BTTask_WaitCirclePause] AIC=%s 开始等待 CirclePauseSeconds=%.3f"),
		*AIC->GetName(), Mem->TargetSeconds);

	// 异步: 返回 InProgress, 后续 TickTask 累计时间
	return EBTNodeResult::InProgress;
}

void UBTTask_WaitCirclePause::TickTask(
	UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	Super::TickTask(OwnerComp, NodeMemory, DeltaSeconds);

	FWaitCircleMemory* Mem = reinterpret_cast<FWaitCircleMemory*>(NodeMemory);
	if (!Mem)
	{
		// 内存指针失效 (BT 重启/被 Abort) → 强制收口
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		return;
	}

	Mem->ElapsedSeconds += DeltaSeconds;

	if (Mem->ElapsedSeconds >= Mem->TargetSeconds)
	{
		UE_LOG(LogBehaviorTree, Verbose,
			TEXT("[BTTask_WaitCirclePause] AIC=%s 停顿完成 (累计 %.3fs / 目标 %.3fs)"),
			*GetNameSafe(OwnerComp.GetAIOwner()), Mem->ElapsedSeconds, Mem->TargetSeconds);
		FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
	}
}

EBTNodeResult::Type UBTTask_WaitCirclePause::AbortTask(
	UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	// 大厂原则: Abort 路径清理内存 — 把 ElapsedSeconds 清零, 下次 Execute 重新计数
	if (NodeMemory)
	{
		FWaitCircleMemory* Mem = reinterpret_cast<FWaitCircleMemory*>(NodeMemory);
		Mem->ElapsedSeconds = 0.f;
		Mem->TargetSeconds = 0.f;
	}
	return Super::AbortTask(OwnerComp, NodeMemory);
}
