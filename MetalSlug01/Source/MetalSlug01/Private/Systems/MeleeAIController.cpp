// Fill out your copyright notice in the Description page of Project Settings.


#include "Systems/MeleeAIController.h"

#include "BehaviorTree/BlackboardComponent.h"
#include "Perception/AIPerceptionComponent.h"
#include "Perception/AIPerceptionTypes.h"
#include "Perception/AISenseConfig_Sight.h"
#include "Characters/BaseCharacter.h"


// Sets default values
AMeleeAIController::AMeleeAIController()
{
	// 2. 自己创建属于刀战独有的“视力配置” (这个不冲突，因为爷爷没有)
	SightConfig = CreateDefaultSubobject<UAISenseConfig_Sight>(TEXT("Sight Config"));
	SightConfig->SightRadius = 1500.0f;
	SightConfig->LoseSightRadius = 1800.0f;
	SightConfig->PeripheralVisionAngleDegrees = 90.0f;
	SightConfig->DetectionByAffiliation.bDetectEnemies = true;
	SightConfig->DetectionByAffiliation.bDetectNeutrals = true;
	SightConfig->DetectionByAffiliation.bDetectFriendlies = true;

	// 3. 【重点】：直接拿爷爷创建好的 AIPerception 来用！不要去 Create 它！
	if (AIPerception)
	{
		AIPerception->ConfigureSense(*SightConfig);
		AIPerception->SetDominantSense(SightConfig->GetSenseImplementation());
	}
}
