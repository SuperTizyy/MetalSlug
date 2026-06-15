// ==========================================
// 活动存档数据
// 职责: 仅包含 UActivitySaveGame 及其持久化数据结构的纯数据定义
// 不包含任何运行时状态/UI 缓存结构, 单一职责
// ==========================================
#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "ActivitySaveGame.generated.h"

/**
 * @struct FPlayerLoginRecord
 * @brief 玩家每日登录进度记录
 * @details 存储玩家在各种活动中的进度状态信息, 会被序列化保存到磁盘
 */
USTRUCT(BlueprintType)
struct FPlayerLoginRecord
{
	GENERATED_BODY()

public:
	/** 活动唯一标识符 */
	UPROPERTY(SaveGame)
	int32 ActivityID;

	/** 玩家 ID */
	UPROPERTY(SaveGame)
	FString PlayerID;

	/** 玩家当前进度（已领取到第几天） */
	UPROPERTY(SaveGame)
	int32 Progress;

	/** 当前已领取次数 */
	UPROPERTY(SaveGame)
	int32 CurrentClaimCount;

	/** 已领取历史掩码 */
	UPROPERTY(SaveGame)
	int32 ClaimedHistoryMask;

	/** 已领取的天数数组 */
	UPROPERTY(SaveGame)
	TArray<int32> ClaimedDays;

	/** 最后领取时间戳 */
	UPROPERTY(SaveGame)
	int64 LastClaimTimestamp;

	/** 最后更新时间戳 */
	UPROPERTY(SaveGame)
	FDateTime LastUpdateTime;

	FPlayerLoginRecord()
		: ActivityID(0), PlayerID(TEXT("DefaultPlayer")), Progress(0)
		, CurrentClaimCount(0), ClaimedHistoryMask(0), LastClaimTimestamp(0)
	{
		LastUpdateTime = FDateTime::Now();
	}

	/** 检查指定天数是否已被领取（DayIndex 从 1 开始） */
	bool IsDayClaimed(int32 DayIndex) const
	{
		if (DayIndex <= 0 || DayIndex > 32) return false;
		return (ClaimedHistoryMask & (1 << (DayIndex - 1))) != 0;
	}

	/** 设置指定天数的领取状态（DayIndex 从 1 开始） */
	void SetDayClaimed(int32 DayIndex, bool bClaimed)
	{
		if (DayIndex <= 0 || DayIndex > 32) return;
		if (bClaimed)
			ClaimedHistoryMask |= (1 << (DayIndex - 1));
		else
			ClaimedHistoryMask &= ~(1 << (DayIndex - 1));
	}
};

/**
 * @struct FUpgradeRewardSaveRecord
 * @brief 升级奖励活动存档记录
 * @details 存储玩家在升级奖励活动中的进度和状态信息
 */
USTRUCT(BlueprintType)
struct FUpgradeRewardSaveRecord
{
	GENERATED_BODY()

public:
	/** 记录天数（1 代表 day1, 2 代表 day2, 以此类推） */
	UPROPERTY(SaveGame)
	int32 RecordDate;

	/** 奖励图标显示下标（对应 RewardItemIDs 数组的索引） */
	UPROPERTY(SaveGame)
	int32 RewardIconIndex;

	/** 限时活动开始时间（Unix 时间戳, 秒） */
	UPROPERTY(SaveGame)
	int64 LimitedActivityStartTime;

	/** 限时活动完成次数 */
	UPROPERTY(SaveGame)
	int32 LimitedActivityCompleteCount;

	/** 任务完成次数数组（与 TaskTypes 数组一一对应） */
	UPROPERTY(SaveGame)
	TArray<int32> TaskCompleteCounts;

	/** 任务领取情况数组（与 TaskTypes 数组一一对应, 0=未领取, 1=已领取） */
	UPROPERTY(SaveGame)
	TArray<int32> TaskClaimStatus;

	/** 当前累计经验值 */
	UPROPERTY(SaveGame)
	int32 CurrentExperience;

	/** 宝箱领取情况数组（与 RewardItemIDs 数组一一对应, 0=未领取, 1=已领取） */
	UPROPERTY(SaveGame)
	TArray<int32> ChestClaimStatus;

	/** 记录创建时间 */
	UPROPERTY(SaveGame)
	FDateTime CreatedTime;

	/** 最后更新时间 */
	UPROPERTY(SaveGame)
	FDateTime LastUpdateTime;

	FUpgradeRewardSaveRecord()
		: RecordDate(1), RewardIconIndex(0), LimitedActivityStartTime(0)
		, LimitedActivityCompleteCount(0), CurrentExperience(0)
	{
		CreatedTime = FDateTime::Now();
		LastUpdateTime = FDateTime::Now();
	}

	/** 设置记录天数（DayNumber 从 1 开始） */
	void SetRecordDate(int32 DayNumber)
	{
		if (DayNumber >= 1)
		{
			RecordDate = DayNumber;
			LastUpdateTime = FDateTime::Now();
		}
	}

	/** 获取记录天数 */
	int32 GetDayNumber() const { return RecordDate; }

	/** 获取天数字符串（如 "day1", "day2"） */
	FString GetDayString() const { return FString::Printf(TEXT("day%d"), RecordDate); }

	/** 检查是否为第一天记录 */
	bool IsToday() const { return RecordDate == 1; }
};

/**
 * @class UActivitySaveGame
 * @brief 活动存档类
 * @details 管理所有活动的玩家进度数据的序列化和反序列化
 * @note 继承自 USaveGame, 支持 Unreal Engine 的存档系统
 */
UCLASS()
class METALSLUG01_API UActivitySaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	/** 玩家所有活动的进度记录映射表 */
	UPROPERTY(SaveGame)
	TMap<int32, FPlayerLoginRecord> ActivityRecords;

	/** 升级奖励活动存档记录映射表（按日期存储） */
	UPROPERTY(SaveGame)
	TMap<int32, FUpgradeRewardSaveRecord> UpgradeRewardRecords;
};
