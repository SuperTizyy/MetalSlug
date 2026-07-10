// Copyright (c) 2026.

#include "Systems/AI/Services/BTService_RefreshTarget.h"

#include "BehaviorTree/BehaviorTreeComponent.h"
#include "AIController.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Character.h"
#include "GameFramework/Actor.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"

#include "Systems/BaseAIController.h"
#include "Systems/Targeting/RoomTargetingSubsystem.h"
#include "Characters/BaseCharacter.h"
#include "Engine/World.h"

UBTService_RefreshTarget::UBTService_RefreshTarget()
{
	NodeName = TEXT("Refresh Target (反扎堆)");

	Interval = 0.3f;
	RandomDeviation = 0.f;
	bTickIntervals = true;

	TargetKey.AddObjectFilter(this,
		GET_MEMBER_NAME_CHECKED(UBTService_RefreshTarget, TargetKey),
		AActor::StaticClass());
}

FString UBTService_RefreshTarget::GetStaticDescription() const
{
	return TEXT("【反扎堆账本】每 0.3s 调 URoomTargetingSubsystem::RequestTargetForAI 申请锁定目标。\n"
		"Subsystem 内部: 优先 unlocked 池 (未被己方 AI 锁定) → 反扎堆。\n"
		"账本单一真理源: AIHuntingMap。失败 → Log Error + 清 BB。");
}

void UBTService_RefreshTarget::TickNode(
	UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	Super::TickNode(OwnerComp, NodeMemory, DeltaSeconds);

	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	if (!BB)
	{
		return;
	}

	AAIController* AIC = OwnerComp.GetAIOwner();
	if (!AIC)
	{
		return;
	}

	APawn* AIPawn = AIC->GetPawn();
	if (!AIPawn)
	{
		return;
	}

	ABaseAIController* BaseAIC = Cast<ABaseAIController>(AIC);
	if (!BaseAIC)
	{
		return;
	}

	ABaseCharacter* MyPawn = Cast<ABaseCharacter>(AIPawn);
	if (!MyPawn || MyPawn->IsDead())
	{
		return;
	}

	UWorld* World = BaseAIC->GetWorld();
	if (!World)
	{
		// 【零兜底】不允许 fallback, 必须 Log Error 让上游修复
		UE_LOG(LogBehaviorTree, Error,
			TEXT("[BTService_RefreshTarget] %s: World 无效. 【零兜底】拒绝静默跳过."),
			*AIC->GetName());
		return;
	}

	URoomTargetingSubsystem* TargetSys = URoomTargetingSubsystem::Get(World);
	if (!TargetSys)
	{
		// 【零兜底】不允许走 ScanForNearestEnemy — 绕过账本 = 反扎堆失效
		UE_LOG(LogBehaviorTree, Error,
			TEXT("[BTService_RefreshTarget] %s: URoomTargetingSubsystem 不可用. "
			     "【零兜底】不允许 fallback, 修复: 检查 ShouldCreateSubsystem 是否被 NM_Client 错误跳过."),
			*AIC->GetName());
		return;
	}

	// 关键逻辑: 每 0.3s 调账本 — 账本自动反扎堆决策 (优先 unlocked 池)
	//
	// 为什么"每次 Tick 都申请" 而不是"失效才申请":
	//   - 反扎堆需求: 账本动态变化 (新 AI 加入 / AI 死亡释放)
	//   - 如果只在 TargetActor 失效时申请, AI 永远不会主动让出已锁定目标
	//   - 每次申请: 账本稳定锁定时不会换目标 (因为自己锁的目标进 UnlockedEnemies 池)
	//   - 账本抖动防护: 账本内部 RequestTargetForAI 优先 Unlocked 池, 不会因为距离近重选
	const TArray<ABaseCharacter*> Candidates = TargetSys->GetAllAliveEnemiesFor(MyPawn);
	if (Candidates.Num() == 0)
	{
		// 无候选 = 清 BB → BT 全部分支拒判 → AI 静止
		if (BB->GetValueAsObject(TargetKey.SelectedKeyName))
		{
			BB->ClearValue(TargetKey.SelectedKeyName);
			UE_LOG(LogBehaviorTree, Log,
				TEXT("[BTService_RefreshTarget] %s: 无候选敌人, 清空 BB (反扎堆账本无法运转). "
				     "根因: 1) 阵营配错 (Pawn.FactionTag 为空) 2) 真无敌人"),
				*AIC->GetName());
		}
		return;
	}

	ABaseCharacter* NewTarget = TargetSys->RequestTargetForAI(MyPawn, Candidates);
	if (!NewTarget)
	{
		// Subsystem 拒绝分配 — 候选都被距离过滤 / 评分全 0
		if (BB->GetValueAsObject(TargetKey.SelectedKeyName))
		{
			BB->ClearValue(TargetKey.SelectedKeyName);
			UE_LOG(LogBehaviorTree, Log,
				TEXT("[BTService_RefreshTarget] %s: Subsystem 拒绝分配目标 (Candidates=%d), 清空 BB."),
				*AIC->GetName(), Candidates.Num());
		}
		return;
	}

	// 写 BB.TargetActor — 账本真理源输出端
	UObject* CurrentTargetObj = BB->GetValueAsObject(TargetKey.SelectedKeyName);
	AActor* CurrentTarget = Cast<AActor>(CurrentTargetObj);
	if (CurrentTarget != NewTarget)
	{
		BB->SetValueAsObject(TargetKey.SelectedKeyName, NewTarget);
		UE_LOG(LogBehaviorTree, Log,
			TEXT("[BTService_RefreshTarget] %s: 账本分配新目标=%s (Candidates=%d, 反扎堆账本驱动)"),
			*AIC->GetName(), *NewTarget->GetName(), Candidates.Num());
	}
}
