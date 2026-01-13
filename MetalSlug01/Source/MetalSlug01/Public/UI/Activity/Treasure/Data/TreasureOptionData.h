//6
#pragma once

#include "CoreMinimal.h"
#include "TreasureOptionData.generated.h"

/**
 * 宝箱选项数据
 * 职责：
 * 1. 描述一个可选奖励
 * 2. 纯数据，不含状态
 */
USTRUCT(BlueprintType)
struct FTreasureOptionData
{
	GENERATED_BODY()

public:
	// 奖励 ID（服务器 / 配置表用）
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int32 RewardId = 0;

	// 奖励名称（UI 显示）
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FText RewardName;

	// 奖励数量
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int32 Amount = 0;
};
