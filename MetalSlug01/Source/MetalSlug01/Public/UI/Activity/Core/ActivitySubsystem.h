//1

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "UI/Activity/Data/DailyLoginConfig.h"
#include "UI/Activity/Data/DailyLoginSave.h"
#include "UI/Activity/Core/RedDotManager.h"
#include "UI/Activity/Managers/ActivityTimeManager.h"
#include "Tools/DailyLoginSaveModifier.h"
#include "UObject/WeakObjectPtrTemplates.h"
#include "ActivitySubsystem.generated.h"

class URedDotManager;
class UActivityTimeManager;
class UDailyLoginPage; // 前向声明: 避免 Widget 头文件反向依赖 Subsystem

/**
 * @file ActivitySubsystem.h
 * @brief 活动管理子系统
 * @author AI Assistant
 * @date 2024
 * @version 1.0
 *
 * @details 活动系统的中央数据服务层
 *
 * 职责说明:
 * 1. 管理 URedDotManager / UActivityTimeManager 等子管理器
 * 2. 加载 DataTable (DT_ActivityInfoRow / DT_DailyLoginConfig / DT_ItemDetail / DT_TreasureBoxItem)
 * 3. 持久化玩家活动进度（FPlayerLoginRecord -> UDailyLoginSaveGame）
 * 4. 提供 Page 注册表（弱引用）供 CheatWidget 等外部模块查询
 * 5. 调度存档修改器 UDailyLoginSaveModifier 提供动态调试能力
 * 6. 暴露数据变更事件 OnActivityDataChanged 供 UI 订阅
 *
 * 架构理念:
 * 1. 单一职责: 一个 Subsystem 管所有 Activity 数据
 * 2. 业务下沉: UI 不做业务判断, Subsystem 提供完整业务查询接口
 * 3. 数据隔离: 静态配置（DataTable）+ 动态存档（SaveGame）+ 内存缓存（AllRecords）
 * 4. 防御链: 多次 IsValidPage / IsValidSubsystem / IsValidSlot
 * 5. 弱引用: RegisteredLoginPage 用 TWeakObjectPtr 避免循环依赖
 *
 * 关联:
 * - 上级: UGameInstance（生命周期同 GameInstance）
 * - 下属: URedDotManager / UActivityTimeManager / UDailyLoginSaveModifier
 * - 上层消费者: UDailyLoginPage / UDailyUpgradeRewardPage / UDailyLoginCheatWidget
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
	// ==================== 页面注册表（解耦入口） ====================

	/**
	 * @brief 注册每日登录主页面（由 Page 在 NativeConstruct 中调用）
	 * @param Page 页面实例弱指针
	 *
	 * 设计要点:
	 * 1. 使用 TWeakObjectPtr 避免循环引用, 不影响 GC
	 * 2. 同时只允许一个主页面存在, 重复注册会被覆盖并打 ERROR 日志
	 * 3. CheatWidget 等外部模块通过 GetLoginPage() 拉取, 无需遍历 World
	 */
	void RegisterLoginPage(UDailyLoginPage* Page);

	/**
	 * @brief 反注册每日登录主页面（由 Page 在 NativeDestruct 中调用）
	 * @param Page 页面实例
	 *
	 * 安全特性:
	 * 1. 入参为 nullptr 直接返回
	 * 2. 传入的不是当前注册项, 静默忽略
	 * 3. Page 已被 GC 销毁, 弱指针.IsValid() 自动返回 false
	 */
	void UnregisterLoginPage(UDailyLoginPage* Page);

	/**
	 * @brief 获取已注册的登录主页面
	 * @return 有效时返回 UDailyLoginPage*; 未注册或已销毁时返回 nullptr
	 *
	 * 调用方约定:
	 * - 返回前必须判空
	 * - 不应缓存返回值超过一帧（页面随时可能被销毁）
	 */
	UDailyLoginPage* GetLoginPage() const;

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

	// ================= 页面注册表 =================

	/**
	 * 每日登录主页面弱引用
	 * 关键: 必须用 TWeakObjectPtr 而非 UPROPERTY 强引用, 否则与 Page 的 ActivitySub 强引用形成循环
	 */
	TWeakObjectPtr<UDailyLoginPage> RegisteredLoginPage;
};


