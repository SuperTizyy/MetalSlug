//1

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "UI/Activity/Data/DailyLoginConfig.h"
#include "UI/Activity/Data/DailyLoginSave.h"
#include "UI/Activity/Core/RedDotManager.h"
#include "UI/Activity/Managers/ActivityTimeManager.h"
#include "Tools/DailyLoginSaveModifier.h"
#include "ActivitySubsystem.generated.h"

class URedDotManager;
class UActivityTimeManager;

/**
 * Activity 子系统
 * 职责：
 * 1. 统一管理所有 Activity Track
 * 2. 负责 Track 的创建、生命周期
 * 3. 为 Page / UI 提供 Track 访问入口
 *
 * 规则：
 * - Subsystem 不做业务判断
 * - Subsystem 不直接操作 UI
 * - Track 的逻辑独立存在
 */
UCLASS()
class METALSLUG01_API UActivitySubsystem : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	// ================= 生命周期 =================

	virtual void Initialize(FSubsystemCollectionBase& Collection) override;
	virtual void Deinitialize() override;

public:
	// ================= 管理器访问接口 =================

	/** 获取红点管理器 */
	URedDotManager* GetRedDotManager() const;

	/** 获取活动时间管理器 */
	UActivityTimeManager* GetActivityTimeManager() const;

	/** 获取所有导航项 */
	TArray<const FActivityInfoRow*> GetAllNavItems() const;

	/** 获取活动信息 */
	const FActivityInfoRow* GetActivityInfo(int32 ActivityID) const;

	/** 获取玩家记录 */
	FPlayerLoginRecord& GetOrInitPlayerRecord(int32 ActivityID);
		
	/** 保存玩家记录 */
	void SavePlayerRecord(int32 ActivityID);
		
	/** 获取每日登录配置 */
	TArray<FDailyLoginConfigRow*> GetDailyLoginConfigs(int32 ActivityID) const;

	/** 根据天数获取奖励 */
	TArray<FDailyLoginConfigRow*> GetRewardsByDay(int32 ActivityID, int32 Day) const;

	/** 尝试领取奖励 */
	bool TryClaimReward(int32 ActivityID, int32 DayIndex);

	/** 作弊跳转到指定天 */
	void Cheat_JumpToDay(int32 ActivityID, int32 NewDay);

	/** 批量尝试领取奖励 */
	bool TryClaimMultipleRewards(int32 ActivityID, const TArray<int32>& DayIndices);

	/** 根据物品ID获取物品详情 */
	const FItemDetailRow* GetItemDetail(int32 ItemID) const;

	/** 根据宝箱ID获取宝箱物品配置 */
	const FTreasureBoxItemRow* GetTreasureBoxItem(int32 BoxID) const;

	/** 根据宝箱ID获取所有匹配的宝箱物品记录 */
	TArray<const FTreasureBoxItemRow*> GetTreasureBoxItemsByBoxID(int32 BoxID) const;

public:
	// ==================== 动态存档修改器接口 ====================

	/**
	 * @brief 获取每日登录存档修改器实例
	 * @return 修改器实例
	 */
	UFUNCTION(BlueprintCallable, Category = "Save Modifier")
	UDailyLoginSaveModifier* GetSaveModifier() const;

	/**
	 * @brief 初始化存档修改器
	 * @param WorldContext 世界上下文
	 * @return 是否初始化成功
	 */
	UFUNCTION(BlueprintCallable, Category = "Save Modifier")
	bool InitializeSaveModifier(UObject* WorldContext);

	/**
	 * @brief 修改玩家进度
	 * @param ActivityID 活动ID
	 * @param NewProgress 新进度
	 * @param bAutoSave 是否自动保存
	 * @return 是否修改成功
	 */
	UFUNCTION(BlueprintCallable, Category = "Save Modifier")
	bool ModifyPlayerProgress(int32 ActivityID, int32 NewProgress, bool bAutoSave = true);

	/**
	 * @brief 修改天数领取状态
	 * @param ActivityID 活动ID
	 * @param DayIndex 天数索引
	 * @param bClaimed 是否已领取
	 * @param bAutoSave 是否自动保存
	 * @return 是否修改成功
	 */
	UFUNCTION(BlueprintCallable, Category = "Save Modifier")
	bool ModifyDayClaimedStatus(int32 ActivityID, int32 DayIndex, bool bClaimed, bool bAutoSave = true);

	/**
	 * @brief 批量修改已领取天数
	 * @param ActivityID 活动ID
	 * @param ClaimedDays 已领取天数数组
	 * @param bAutoSave 是否自动保存
	 * @return 是否修改成功
	 */
	UFUNCTION(BlueprintCallable, Category = "Save Modifier")
	bool ModifyClaimedDays(int32 ActivityID, const TArray<int32>& ClaimedDays, bool bAutoSave = true);

	/**
	 * @brief 重置玩家记录
	 * @param ActivityID 活动ID
	 * @param bAutoSave 是否自动保存
	 * @return 是否重置成功
	 */
	UFUNCTION(BlueprintCallable, Category = "Save Modifier")
	bool ResetPlayerRecord(int32 ActivityID, bool bAutoSave = true);

public:
	// ================= 事件 =================


	// ================= 事件 =================
	
	/** 活动数据变更事件 */
	DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnActivityDataChanged);
	UPROPERTY(BlueprintAssignable, Category = "Activity")
	FOnActivityDataChanged OnActivityDataChanged;

private:
	// ================= Track 持有 =================

	// ================= 管理器持有 =================
		
	UPROPERTY()
	URedDotManager* RedDotManager = nullptr;
		
	UPROPERTY()
	UActivityTimeManager* ActivityTimeManager = nullptr;
		
	// ================= SaveGame 缓存 =================
		
	UPROPERTY()
	UDailyLoginSaveGame* CachedSaveGame = nullptr;

	// ================= 动态存档修改器 =================
	
	/** 每日登录存档修改器实例 */
	UPROPERTY()
	UDailyLoginSaveModifier* SaveModifier = nullptr;
};


