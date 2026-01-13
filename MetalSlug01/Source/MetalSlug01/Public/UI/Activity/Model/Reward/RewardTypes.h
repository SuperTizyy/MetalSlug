//11
#pragma once

#include "CoreMinimal.h"
#include "RewardTypes.generated.h"

/**
 * 通用奖励状态
 * 所有奖励系统统一使用
 */
UENUM(BlueprintType)
enum class ERewardState : uint8
{
	Incomplete UMETA(DisplayName = "未到期"),
	Claimable  UMETA(DisplayName = "可领取"),
	Claimed    UMETA(DisplayName = "已领取")
};
