/*#pragma once

#include "CoreMinimal.h"
#include "RewardProgressData.generated.h"

USTRUCT(BlueprintType)
struct FRewardProgressData
{
	GENERATED_BODY()

public:
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int32 Id = 0;

	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FText Title;

	// 当前值（登录天数 / 行为次数）
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int32 CurrentValue = 0;

	// 目标值
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int32 TargetValue = 0;

	// 是否已发奖
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	bool bRewardClaimed = false;
};*/