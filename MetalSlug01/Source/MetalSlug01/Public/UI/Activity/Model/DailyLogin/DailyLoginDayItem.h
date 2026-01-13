// 4

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UI/Activity/Data/RewardData.h"
#include "DailyLoginDayItem.generated.h"

/**
 * 单日登录奖励 Item
 * 职责：
 * 1. 保存某一天的奖励内容
 * 2. 保存该天的领取状态
 * 3. 不包含任何 UI 或流程逻辑
 */
UCLASS(BlueprintType)
class METALSLUG01_API UDailyLoginDayItem : public UObject
{
	GENERATED_BODY()

public:
	// 第几天（Day1、Day2、Day8 等）
	// 用于和服务器、配置表对齐
	UPROPERTY(BlueprintReadOnly)
	int32 DayIndex = 0;

	// 当天的奖励内容（可多个）
	UPROPERTY(BlueprintReadOnly)
	TArray<FRewardData> Rewards;

	// 是否已领取
	// true：已领取
	// false：未领取
	UPROPERTY(BlueprintReadOnly)
	bool bClaimed = false;

	// 是否可领取（由 Track 统一刷新）
	UPROPERTY(BlueprintReadOnly)
	bool bClaimable = false;

public:
	// 重置该天的状态（用于重建 / 测试）
	void ResetState();
	
	// 请求领取（转发给 Track）
	void RequestClaim();
};

