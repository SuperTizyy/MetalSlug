// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "BaseAIController.h"
#include "Runtime/AIModule/Classes/AIController.h"
#include "MeleeAIController.generated.h"

UCLASS()
class METALSLUG01_API AMeleeAIController : public ABaseAIController
{
	GENERATED_BODY()

public:
	// Sets default values for this actor's properties
	AMeleeAIController();

protected:
	// 刀战独有的感官：超强视觉
	UPROPERTY()
	class UAISenseConfig_Sight* SightConfig;
};
