// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UI/Activity/DailyLogin/Track/DailyLoginRewardTrack.h"
#include "UI/Activity/Pages/ActivityPageBase.h"
#include "ActivityDailyLoginPage.generated.h"

UCLASS()
class METALSLUG01_API UActivityDailyLoginPage : public UActivityPageBase
{
	GENERATED_BODY()

public:
	UPROPERTY()
	UDailyLoginRewardTrack* LoginTrack;

	virtual void OnPageShow_Implementation() override;
};
