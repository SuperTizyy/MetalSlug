//12
#pragma once

#include "CoreMinimal.h"
#include "RewardData.generated.h"

/**
 * 通用奖励数据结构
 */
USTRUCT(BlueprintType)
struct FRewardData
{
	GENERATED_BODY()

	UPROPERTY(BlueprintReadOnly)
	int32 RewardId = 0;

	UPROPERTY(BlueprintReadOnly)
	FText RewardName;

	UPROPERTY(BlueprintReadOnly)
	int32 Amount = 0;
};
