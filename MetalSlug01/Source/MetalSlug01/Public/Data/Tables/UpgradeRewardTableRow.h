// ==========================================
// 每日升级奖励配置表行
// 关联: DT_UpgradeReward
// ==========================================
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Data/Enums/ActivityEnums.h"
#include "UpgradeRewardTableRow.generated.h"

/**
 * @struct FDailyUpgradeRewardConfigRow
 * @brief 每日升级奖励活动配置数据表行
 * 用途: 定义每日升级奖励活动的详细配置信息
 * 注意: 包含活动基本信息、任务配置、奖励配置和限时加成等完整信息
 */
USTRUCT(BlueprintType)
struct FDailyUpgradeRewardConfigRow : public FTableRowBase
{
	GENERATED_BODY()

public:
	// ==================== 基础活动信息 ====================
	
	/** 活动唯一标识符 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DailyUpgrade|Basic")
	int32 ActivityID;

	/** 天数标识（day1-day7） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DailyUpgrade|Basic")
	FString DayIdentifier;

	// ==================== 任务配置 ====================
	
	/** 任务描述数组 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DailyUpgrade|Task")
	TArray<FString> TaskDescriptions;

	/** 游戏模式数组（枚举值） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DailyUpgrade|Task")
	TArray<EGameModeType> GameModes;

	/** 任务类型数组（枚举值） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DailyUpgrade|Task")
	TArray<ETaskType> TaskTypes;

	/** 任务相关数值数组 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DailyUpgrade|Task")
	TArray<int32> TaskRelatedValues;

	// ==================== 奖励配置 ====================
	
	/** 奖励物品 ID 数组 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DailyUpgrade|Reward")
	TArray<FString> RewardItemIDs;

	/** 奖励物品数量数组 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DailyUpgrade|Reward")
	TArray<FString> RewardItemCounts;

	// ==================== 限时加成配置 ====================
	
	/** 限时加成描述 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DailyUpgrade|Bonus")
	FString BonusDescription;

	/** 限时加成时限（小时） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DailyUpgrade|Bonus")
	int32 BonusDurationHours;

	/** 限时加成次数 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DailyUpgrade|Bonus")
	int32 BonusCount;

	/** 限时加成 ID 数组 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DailyUpgrade|Bonus")
	TArray<int32> BonusIDs;

	/** 默认构造函数 */
	FDailyUpgradeRewardConfigRow() 
		: ActivityID(0)
		, DayIdentifier(TEXT("day1"))
		, BonusDescription(TEXT(""))
		, BonusDurationHours(0)
		, BonusCount(0)
	{};
};
