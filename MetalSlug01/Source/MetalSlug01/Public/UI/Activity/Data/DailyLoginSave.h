/**
 * @file DailyLoginSave.h
 * @brief 活动系统动态数据和存档定义文件
 * @author AI Assistant
 * @date 2024
 * @version 1.0
 *
 * @details 本文件包含活动系统的所有动态运行时数据结构和存档类定义
 *          所有需要持久化存储的玩家进度数据都定义在此文件中
 *          遵循"所有动态表结构体放入此文件"的项目规范
 *
 * ====================================================================
 * 文件内容索引
 * ====================================================================
 *
 * §1. 运行时数据结构
 *   - FActivityRuntimeState:    活动的运行时状态（不在存档）
 *   - FPlayerLoginRecord:       玩家每日登录进度记录
 *   - FActivityNavItem:         UI 导航项显示数据结构
 *   - FUpgradeRewardSaveRecord: 升级奖励活动存档记录
 *
 * §2. 存档管理类
 *   - UDailyLoginSaveGame: 继承 USaveGame, 容器
 *     - ActivityRecords: TMap<ActivityID, FPlayerLoginRecord>
 *     - UpgradeRewardRecords: TMap<Date, FUpgradeRewardSaveRecord>
 *     - NavigationItemsCache: 运行时缓存（不存档）
 *
 * 设计理念:
 * 1. 静态表（配置）放 DailyLoginConfig.h, 动态表（存档）放 DailyLoginSave.h
 * 2. 所有 USTRUCT 都标记 BlueprintType 便于蓝图访问
 * 3. 所有持久化字段标记 SaveGame, 运行时缓存标记 Transient
 * 4. FPlayerLoginRecord / FUpgradeRewardSaveRecord 提供 bit-mask 优化状态查询
 */

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/SaveGame.h"
#include "DailyLoginConfig.h"  // 包含静态配置结构和枚举
#include "DailyLoginSave.generated.h"

// ==================== 运行时数据结构 ====================

/**
 * @brief 活动运行时状态结构
 * @details 包含活动的动态运行时信息，不保存到DataTable
 * @note 这些状态在程序运行时动态计算和更新，属于动态表范畴
 */


USTRUCT(BlueprintType)
struct FActivityRuntimeState
{
	GENERATED_BODY()

public:
	// ==================== 时间运行时状态 ====================
	
	/** 活动当前状态 */
	UPROPERTY(BlueprintReadOnly, Category = "Runtime|Time")
	EActivityStatus CurrentStatus = EActivityStatus::Active;

	/** 距离开始的剩余时间（秒） */
	UPROPERTY(BlueprintReadOnly, Category = "Runtime|Time")
	float TimeUntilStart = 0.0f;

	/** 距离结束的剩余时间（秒） */
	UPROPERTY(BlueprintReadOnly, Category = "Runtime|Time")
	float TimeUntilEnd = 0.0f;

	/** 是否处于开始前提醒期 */
	UPROPERTY(BlueprintReadOnly, Category = "Runtime|Time")
	bool bInPreNoticePeriod = false;

	/** 是否处于结束前提醒期 */
	UPROPERTY(BlueprintReadOnly, Category = "Runtime|Time")
	bool bInEndWarningPeriod = false;

	/** 当前循环周期索引 */
	UPROPERTY(BlueprintReadOnly, Category = "Runtime|Time")
	int32 CurrentCycleIndex = 0;

	// ==================== UI显示运行时状态 ====================
	
	/** 在UI中是否被选中 */
	UPROPERTY(BlueprintReadOnly, Category = "Runtime|UI")
	bool bIsSelectedInUI = false;

	/** UI显示的红点数据 */
	UPROPERTY(BlueprintReadOnly, Category = "Runtime|UI")
	FRedDotData UIDotData;

	/** 是否在导航中显示 */
	UPROPERTY(BlueprintReadOnly, Category = "Runtime|UI")
	bool bShowInNavigation = true;

	/**
	 * @brief 构造函数
	 * @details 使用默认初始化值
	 */
	FActivityRuntimeState() = default;
};

/**
 * @brief 玩家每日登录进度记录
 * @details 存储玩家在各种活动中的进度状态信息
 * @note 这是玩家个人数据的核心结构，会被序列化保存到磁盘
 */
USTRUCT(BlueprintType)
struct FPlayerLoginRecord
{
	GENERATED_BODY()

public:
	/** 活动唯一标识符 */
	UPROPERTY(SaveGame)
	int32 ActivityID;

	/** 玩家ID */
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

	/**
	 * @brief 构造函数
	 * @details 初始化默认值并设置当前时间为最后更新时间
	 */
	FPlayerLoginRecord()
		: ActivityID(0), PlayerID(TEXT("DefaultPlayer")), Progress(0), CurrentClaimCount(0), ClaimedHistoryMask(0), LastClaimTimestamp(0)
	{
		LastUpdateTime = FDateTime::Now();
	}
	
	/**
	 * @brief 检查指定天数是否已被领取
	 * @param DayIndex 天数索引（从1开始）
	 * @return 是否已领取
	 */
	bool IsDayClaimed(int32 DayIndex) const
	{
		if (DayIndex <= 0 || DayIndex > 32) return false;
		return (ClaimedHistoryMask & (1 << (DayIndex - 1))) != 0;
	}
	
	/**
	 * @brief 设置指定天数的领取状态
	 * @param DayIndex 天数索引（从1开始）
	 * @param bClaimed 是否已领取
	 */
	void SetDayClaimed(int32 DayIndex, bool bClaimed)
	{
		if (DayIndex <= 0 || DayIndex > 32) return;
		if (bClaimed)
		{
			ClaimedHistoryMask |= (1 << (DayIndex - 1));
		}
		else
		{
			ClaimedHistoryMask &= ~(1 << (DayIndex - 1));
		}
	}
};

/**
 * @brief UI导航项显示数据结构（优化版）
 * @details 用于存储导航菜单项的UI显示状态信息
 * @note 通过引用FActivityInfoRow避免DisplayName等字段的冗余存储
 */
USTRUCT(BlueprintType)
struct FActivityNavItem
{
	GENERATED_BODY()

public:
	// ==================== 基础导航信息 ====================
	
	/** 关联的活动标识符 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Navigation")
	FName ActivityId;

	/** 是否当前选中状态 */
	UPROPERTY(BlueprintReadOnly, Category = "Navigation")
	bool bIsSelected = false;

	/** 导航项图标资源 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Navigation")
	TSoftObjectPtr<UTexture2D> IconTexture;

	// ==================== 运行时状态引用 ====================
	
	/** 关联的活动ID（通过ID关联避免数据复制冗余） */
	UPROPERTY(BlueprintReadOnly, Category = "RuntimeReference")
	FName LinkedActivityId;

	/** 缓存的活动信息指针（运行时使用） */
	UPROPERTY(Transient)
	TWeakObjectPtr<UObject> CachedActivityInfo;

	/**
	 * @brief 获取显示名称（通过活动ID获取对应信息）
	 * @return 导航项显示文本
	 * @note 需要在运行时通过ActivitySubsystem获取对应的活动信息
	 */
	FORCEINLINE FText GetDisplayName() const 
	{ 
		// 实际实现需要通过LinkedActivityId从DataTable中查找对应活动信息
		// 这里返回空文本，具体实现在运行时由管理者处理
		return FText::GetEmpty(); 
	}

	/**
	 * @brief 获取导航ID（通过活动ID获取）
	 * @return 导航项标识符
	 * @note 需要在运行时通过ActivitySubsystem获取对应的活动信息
	 */
	FORCEINLINE FName GetNavId() const 
	{ 
		// 实际实现需要通过LinkedActivityId从DataTable中查找对应活动信息
		// 这里返回空名称，具体实现在运行时由管理者处理
		return NAME_None; 
	}

	/**
	 * @brief 默认构造函数
	 */
	FActivityNavItem() = default;
};

/**
 * @brief 升级奖励活动存档记录
 * @details 存储玩家在升级奖励活动中的进度和状态信息
 * @note 这是升级奖励活动的专属存档结构，用于持久化存储玩家数据
 */
USTRUCT(BlueprintType)
struct FUpgradeRewardSaveRecord
{
	GENERATED_BODY()

public:
	// ==================== 基础信息 ====================
	
	/** 记录天数（1代表day1，2代表day2，以此类推） */
	UPROPERTY(SaveGame)
	int32 RecordDate;

	/** 奖励图标显示下标（对应RewardItemIDs数组的索引） */
	UPROPERTY(SaveGame)
	int32 RewardIconIndex;

	// ==================== 限时活动数据 ====================
	
	/** 限时活动开始时间（Unix时间戳，秒） */
	UPROPERTY(SaveGame)
	int64 LimitedActivityStartTime;

	/** 限时活动完成次数 */
	UPROPERTY(SaveGame)
	int32 LimitedActivityCompleteCount;

	// ==================== 任务数据 ====================
	
	/** 任务完成次数数组（与TaskTypes数组一一对应） */
	UPROPERTY(SaveGame)
	TArray<int32> TaskCompleteCounts;

	/** 任务领取情况数组（与TaskTypes数组一一对应，0=未领取，1=已领取） */
	UPROPERTY(SaveGame)
	TArray<int32> TaskClaimStatus;

	// ==================== 经验值数据 ====================
	
	/** 当前累计经验值 */
	UPROPERTY(SaveGame)
	int32 CurrentExperience;

	// ==================== 宝箱数据 ====================
	
	/** 宝箱领取情况数组（与RewardItemIDs数组一一对应，0=未领取，1=已领取） */
	UPROPERTY(SaveGame)
	TArray<int32> ChestClaimStatus;

	// ==================== 时间戳 ====================
	
	/** 记录创建时间 */
	UPROPERTY(SaveGame)
	FDateTime CreatedTime;

	/** 最后更新时间 */
	UPROPERTY(SaveGame)
	FDateTime LastUpdateTime;

	/**
	 * @brief 构造函数
	 * @details 初始化默认值并设置当前时间为创建和更新时间
	 */
	FUpgradeRewardSaveRecord()
		: RecordDate(1), RewardIconIndex(0), LimitedActivityStartTime(0)
		, LimitedActivityCompleteCount(0), CurrentExperience(0)
	{
		CreatedTime = FDateTime::Now();
		LastUpdateTime = FDateTime::Now();
	}
	
	/**
	 * @brief 设置记录天数
	 * @param DayNumber 天数编号（从1开始，1代表day1）
	 */
	void SetRecordDate(int32 DayNumber)
	{
		if (DayNumber >= 1)
		{
			RecordDate = DayNumber;
			LastUpdateTime = FDateTime::Now();
		}
	}
	
	/**
	 * @brief 获取记录天数
	 * @return 天数编号（1代表day1）
	 */
	int32 GetDayNumber() const
	{
		return RecordDate;
	}
	
	/**
	 * @brief 获取天数字符串
	 * @return 格式化的天数字符串（如"day1", "day2"）
	 */
	FString GetDayString() const
	{
		return FString::Printf(TEXT("day%d"), RecordDate);
	}
	
	/**
	 * @brief 检查是否为第一天记录
	 * @return 是否为第一天
	 * @details 直接比较RecordDate值是否为1
	 */
	bool IsToday() const
	{
		return RecordDate == 1;
	}
};

// ==================== 存档管理类 ====================

/**
 * @brief 每日登录存档类
 * @details 管理所有活动的玩家进度数据的序列化和反序列化
 * @note 继承自USaveGame，支持Unreal Engine的存档系统
 */
UCLASS()
class METALSLUG01_API UDailyLoginSaveGame : public USaveGame
{
	GENERATED_BODY()

public:
	// ==================== 持久化数据 ====================
	
	/** 玩家所有活动的进度记录映射表 */
	UPROPERTY(SaveGame)
	TMap<int32, FPlayerLoginRecord> ActivityRecords;

	/** 升级奖励活动存档记录映射表（按日期存储） */
	UPROPERTY(SaveGame)
	TMap<int32, FUpgradeRewardSaveRecord> UpgradeRewardRecords;

	// ==================== 运行时数据 ====================
	
	/** UI导航项缓存（运行时数据，不保存到磁盘） */
	UPROPERTY(Transient)
	TArray<FActivityNavItem> NavigationItemsCache;
};