// Copyright (c) 2026.

#include "Systems/AI/Tasks/BTTask_ReleaseTarget.h"

#include "AIController.h"
#include "BehaviorTree/BlackboardComponent.h"

#include "Systems/RoomGameMode.h"
#include "Characters/BaseCharacter.h"
#include "Systems/AI/AIBehaviorTypes.h"

EBTNodeResult::Type UBTTask_ReleaseTarget::ExecuteTask(
	UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	AActor* Pawn = OwnerComp.GetAIOwner() ? OwnerComp.GetAIOwner()->GetPawn() : nullptr;
	ABaseCharacter* AIChar = Cast<ABaseCharacter>(Pawn);

	if (!AIChar)
	{
		return EBTNodeResult::Failed;
	}

	UWorld* World = Pawn->GetWorld();
	if (!World)
	{
		return EBTNodeResult::Failed;
	}

	// 从猎人账本移除本 AI 的记录
	if (ARoomGameMode* GM = World->GetAuthGameMode<ARoomGameMode>())
	{
		GM->ReleaseTarget(AIChar);
	}

	// 清空 BB 里的目标（BT 后续自然停止追击）
	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	if (BB)
	{
		BB->SetValueAsObject(FName(AIBlackboardKeyNames::TargetActor), nullptr);
		BB->SetValueAsBool(FName(AIBlackboardKeyNames::bIsInCombat), false);
		BB->SetValueAsBool(FName(AIBlackboardKeyNames::bHasAttackToken), false);
	}

	return EBTNodeResult::Succeeded;
}

FString UBTTask_ReleaseTarget::GetStaticDescription() const
{
	return FString(TEXT("从 RoomGameMode 猎人账本移除本 AI，"
		"并清空 BB 中的 TargetActor/bIsInCombat/bHasAttackToken。"));
}

UBTTask_ReleaseTarget::UBTTask_ReleaseTarget()
{
	NodeName = TEXT("Release Target (释放目标)");
}
