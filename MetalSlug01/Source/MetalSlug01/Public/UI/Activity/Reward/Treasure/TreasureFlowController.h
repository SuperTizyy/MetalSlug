// Fill out your copyright notice in the Description page of Project Settings.

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "TreasureFlowController.generated.h"

// 前置声明，避免循环依赖
class UTreasureRewardItem;

UCLASS()
class METALSLUG01_API UTreasureFlowController : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY()
	UTreasureRewardItem* TreasureItem;

public:
	void OpenTreasure();
	void SelectReward(int32 Index);
	void ConfirmReward();
	void CancelAndClose();
};