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

UBTService_RefreshTarget::UBTService_RefreshTarget()
{
	NodeName = TEXT("Refresh Target");

	Interval = 0.3f;
	RandomDeviation = 0.f;
	bTickIntervals = true;

	TargetKey.AddObjectFilter(this,
		GET_MEMBER_NAME_CHECKED(UBTService_RefreshTarget, TargetKey),
		AActor::StaticClass());
}

FString UBTService_RefreshTarget::GetStaticDescription() const
{
	return FString::Printf(TEXT("每 %.2fs 检查目标有效性。\n"
		"失效时扫描 %.0f cm 半径重新选目标。"),
		Interval, ScanRadius);
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

	UObject* TargetObj = BB->GetValueAsObject(TargetKey.SelectedKeyName);
	AActor* TargetActor = Cast<AActor>(TargetObj);

	bool bNeedRefresh = false;
	if (!TargetActor)
	{
		bNeedRefresh = true;
	}
	else
	{
		const float Distance = BaseAIC->ComputeActorCenterDistance(AIPawn, TargetActor);
		if (Distance > ScanRadius)
		{
			bNeedRefresh = true;
		}
	}

	if (bNeedRefresh)
	{
		ACharacter* AIPawnChar = Cast<ACharacter>(AIPawn);
		AActor* NewTarget = BaseAIC->ScanForNearestEnemy(AIPawnChar, ScanRadius);

		if (NewTarget)
		{
			BB->SetValueAsObject(TargetKey.SelectedKeyName, NewTarget);
		}
		else
		{
			BB->ClearValue(TargetKey.SelectedKeyName);
		}
	}
}
