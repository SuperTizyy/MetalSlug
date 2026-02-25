/**
 * @file UpgradeActivitySubsystem.h
 * @brief 升级奖励活动子系统
 * @author AI Assistant
 * @date 2024
 * @version 1.0
 * 
 * @details 管理升级奖励活动的核心业务逻辑，包括数据存取、宝箱领取等功能
 */

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "UI/Activity/Data/DailyLoginConfig.h"
#include "UI/Activity/Data/DailyLoginSave.h"
#include "Tools/UpgradeActivitySaveModifier.h"
#include "UpgradeActivitySubsystem.generated.h"

/**
 * @brief 奖励图标索引更新委托
 * @param NewIndex 新的奖励图标索引
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnRewardIconIndexChanged, int32, NewIndex);

/**
 * @brief 全局刷新委托
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnGlobalRefresh);

/**
 * @brief 升级奖励活动子系统
 * @details 负责管理升级奖励活动的所有业务逻辑和数据操作
 */
UCLASS()
class METALSLUG01_API UUpgradeActivitySubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    // ==================== 委托事件 ====================
    
    /** 奖励图标索引更新事件 */
    UPROPERTY(BlueprintAssignable, Category = "Upgrade Events")
    FOnRewardIconIndexChanged OnRewardIconIndexChanged;
    
    /** 全局刷新事件 */
    UPROPERTY(BlueprintAssignable, Category = "Upgrade Events")
    FOnGlobalRefresh OnGlobalRefresh;
    
    // ==================== 生命周期 ====================
    
    /**
     * @brief 子系统初始化
     * @param Collection 子系统集合
     */
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;

    /**
     * @brief 子系统去初始化
     */
    virtual void Deinitialize() override;

    // ==================== 数据访问接口 ====================
    
    /**
     * @brief 获取当前存档记录
     * @return 当前记录的引用
     */
    FUpgradeRewardSaveRecord& GetRecord() { return CurrentRecord; }

    /**
     * @brief 获取配置表行（封装了ID 110的查找逻辑）
     * @return 配置表行指针，找不到则返回nullptr
     */
    const FDailyUpgradeRewardConfigRow* GetActivityConfig();

    // ==================== 核心业务接口 ====================
    
    /**
     * @brief 点击领取宝箱
     * @param ChestIndex 宝箱索引
     * @return 是否领取成功
     */
    bool ClaimChest(int32 ChestIndex);

    /**
     * @brief 更新任务完成次数
     * @param TaskIndex 任务索引
     * @param Count 完成次数
     */
    void UpdateTaskProgress(int32 TaskIndex, int32 Count);

    /**
     * @brief 领取任务奖励
     * @param TaskIndex 任务索引
     * @return 是否领取成功
     */
    bool ClaimTaskReward(int32 TaskIndex);

    /**
     * @brief 检查宝箱是否可领取
     * @param ChestIndex 宝箱索引
     * @return 是否可领取
     */
    bool CanClaimChest(int32 ChestIndex) const;

    /**
     * @brief 检查任务是否可领取
     * @param TaskIndex 任务索引
     * @return 是否可领取
     */
    bool CanClaimTask(int32 TaskIndex) const;

    /**
     * @brief 获取重选奖励选项数据
     * @return 奖励选项的ItemIcon数组
     */
    TArray<TSoftObjectPtr<UTexture2D>> GetReselectRewardOptions();

    /**
     * @brief 获取奖励物品图标数据
     * @details 按照指定逻辑获取RewardItemImage控件所需的图标数据：
     * 1. 找到UpgradeRewardSaveRecord动态表中RecordDate最大的数据
     * 2. 取其中的RewardIconIndex字段数据
     * 3. 通过表关联获取对应的ItemIcon数据
     * @return 奖励物品图标数组
     */
    TArray<TSoftObjectPtr<UTexture2D>> GetRewardItemIcons();
    	
    /**
     * @brief 获取宝箱图标数据
     * @details 按照指定逻辑获取ItemsScrollBox中ExperienceChestClaimWidget的ChestClaimButton图标数据：
     * 1. 找到DailyUpgradeRewardConfigRow表中ActivityID==110的数据
     * 2. 获取其RewardItemIDs里面的全部内容
     * 3. 依次关联TreasureBoxItemRow表的BoxID得到对应的BoxIcon数据
     * @return 宝箱图标数组
     */
    TArray<TSoftObjectPtr<UTexture2D>> GetChestBoxIcons();
    	
    /**
     * @brief 获取宝箱数量
     * @details 获取DailyUpgradeRewardConfigRow表中ActivityID==110数据的RewardItemCounts最后一个索引值
     * @return 宝箱数量字符串，失败时返回"0"
     */
    FString GetChestCount();
    	
    /**
     * @brief 获取任务相关数值数组
     * @details 获取DailyUpgradeRewardConfigRow表中ActivityID==110数据的TaskRelatedValues数组
     * @return TaskRelatedValues数组，失败时返回空数组
     */
    TArray<int32> GetTaskRelatedValues();
    	
    /**
     * @brief 获取当前经验值
     * @details 获取UpgradeRewardSaveRecord动态表中RecordDate最大的数据的CurrentExperience字段
     * @return 当前经验值
     */
    int32 GetCurrentExperience() const;

    /**
     * @brief 获取当前奖励图标索引
     * @return 当前RewardIconIndex值
     */
    int32 GetCurrentRewardIconIndex() const;

    /**
     * @brief 设置奖励图标索引
     * @param NewIndex 新的索引值
     * @return 是否设置成功
     */
    bool SetCurrentRewardIconIndex(int32 NewIndex);

    /**
     * @brief 更新奖励图标索引并保存
     * @param NewIndex 新的索引值
     * @return 是否更新成功
     * @details 专门为RewardOptionCardWidget的SelectionCheckBox控件设计
     * 会自动查找UpgradeRewardSaveRecord动态表中RecordDate最大的数据
     * 更新RewardIconIndex字段并保存到存档
     */
    bool UpdateRewardIconIndexAndSave(int32 NewIndex);

    // ==================== 数据持久化 ====================
    
    /**
     * @brief 保存数据到硬盘
     */
    void SaveStatus();

    /**
     * @brief 从硬盘加载数据
     */
    void LoadStatus();

    /**
     * @brief 检查今日记录是否存在
     * @return 是否存在今日记录
     */
    bool HasTodayRecord() const;

    /**
     * @brief 创建今日记录
     */
    void CreateTodayRecord();

    /**
     * @brief 处理存档数据逻辑
     * @details 遍历UpgradeRewardSaveRecord动态表数据，检查最新记录的创建时间：
     * 1. 如果最新记录是今天创建的，则无需创建新记录
     * 2. 如果最新记录不是今天创建的，则创建新记录并继承前一天的数据
     */
    void ProcessSaveRecordLogic();

    /**
     * @brief 获取最新的存档记录
     * @return 指向最新记录的指针，如果没有记录则返回nullptr
     * @details 从存档中找出CreatedTime最新的记录
     */
    const FUpgradeRewardSaveRecord* GetLatestSaveRecord() const;


private:
    // ==================== 私有成员 ====================
    
    /** 当前存档记录 */
    FUpgradeRewardSaveRecord CurrentRecord;
    
    /** 升级活动存档修改器 */
    UPROPERTY()
    UUpgradeActivitySaveModifier* SaveModifier;
    
    /** 缓存的配置表 */
    UPROPERTY()
    UDataTable* CachedConfigTable;

    /** 存档槽名称 */
    const FString SaveSlotName = TEXT("UpgradeReward_SaveSlot");

    /** 存档用户索引 */
    const int32 SaveUserIndex = 0;

    // ==================== 私有方法 ====================
    
    /**
     * @brief 初始化今日记录数据
     */
    void InitializeTodayRecordData();

    /**
     * @brief 获取配置表路径
     * @return 配置表路径
     */
    FName GetConfigTablePath() const;

    /**
     * @brief 获取额外配置数据（ActivityID=102, DayIdentifier=day1）
     * @return 额外配置数据指针
     */
    const FDailyUpgradeRewardConfigRow* GetExtraConfigForDay1();

    /**
     * @brief 获取指定天数的额外配置数据（ActivityID=102, DayIdentifier=day + 天数）
     * @param DayNumber 天数
     * @return 额外配置数据指针
     */
    const FDailyUpgradeRewardConfigRow* GetExtraConfigForSpecificDay(int32 DayNumber);

    /**
     * @brief 验证索引有效性
     * @param Index 索引
     * @param MaxSize 最大大小
     * @return 是否有效
     */
    bool IsValidIndex(int32 Index, int32 MaxSize) const;

    /**
     * @brief 检查日期是否为今天
     * @param DateToCheck 要检查的日期
     * @return 是否为今天
     */
    bool IsDateToday(const FDateTime& DateToCheck) const;

    /**
     * @brief 创建继承前一天数据的新记录
     * @param PreviousRecord 前一天的记录数据
     * @details 根据前一天的数据创建新记录，按指定规则填充各字段
     */
    void CreateInheritedRecord(const FUpgradeRewardSaveRecord& PreviousRecord);
};