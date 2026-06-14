/**
 * @file UpgradeActivitySubsystem.h
 * @brief 升级奖励活动子系统
 * @author AI Assistant
 * @date 2024
 * @version 1.0
 *
 * @details 管理升级奖励活动的所有业务逻辑和数据操作
 *
 * 职责说明:
 * 1. 活动配置缓存 (DT_DailyUpgradeRewardConfigRow, 启动时预加载)
 * 2. 玩家进度存档 (FUpgradeRewardSaveRecord, 多日表)
 * 3. 业务规则计算: HasDayDataInMemory / GetDailyTaskHighlightStates / ShouldShowFixedPrizeHighlight
 * 4. 跨表数据关联: Config -> TreasureBoxItem -> ItemDetail (多级查找)
 * 5. 委托事件: OnRewardIconIndexChanged / OnGlobalRefresh
 *
 * 架构理念:
 * 1. 业务下沉: 所有高亮/锁定/状态判断都在 Subsystem, UI 仅消费结果
 * 2. AllRecords 内存表 + CurrentRecord 单一当前记录
 * 3. 通过 TArray<int32> TaskRelatedValues 实现"第N天完成N局"型规则
 * 4. 宝箱+任务双轨: ChestClaimStatus + TaskClaimStatus 独立管理
 *
 * 关联:
 * - 上级: UGameInstance
 * - 下属: UUpgradeActivitySaveModifier（修改器）
 * - 上层消费者: UDailyUpgradeRewardPage / UActivityNavMenuWidget / 各子 Widget
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
     * @brief 获取所有存档记录
     * @return 所有记录的映射表（RecordDate -> Record）
     */
    const TMap<int32, FUpgradeRewardSaveRecord>& GetAllRecords() const { return AllRecords; }
    
    /**
     * @brief 获取指定天数的记录
     * @param RecordDate 天数（如 1, 2, 3...）
     * @return 指定记录的引用，如果不存在则返回 nullptr
     */
    const FUpgradeRewardSaveRecord* GetRecordByDate(int32 RecordDate) const;
    
    /**
     * @brief 添加或更新记录到表格
     * @param RecordDate 天数
     * @param Record 要添加的记录
     */
    void AddOrUpdateRecord(int32 RecordDate, const FUpgradeRewardSaveRecord& Record);
    
    /**
     * @brief 获取当前存档记录
     * @return 当前记录的引用
     */
    FUpgradeRewardSaveRecord& GetRecord() { return CurrentRecord; }
    
    /**
     * @brief 获取当前存档记录的 TaskCompleteCounts 数组
     * @return TaskCompleteCounts 数组的引用
     */
    const TArray<int32>& GetCurrentTaskCompleteCounts() const { return CurrentRecord.TaskCompleteCounts; }
    
    /**
     * @brief 获取当前存档记录的 TaskClaimStatus 数组
     * @return TaskClaimStatus 数组的引用
     */
    const TArray<int32>& GetCurrentTaskClaimStatus() const { return CurrentRecord.TaskClaimStatus; }
    
    /**
     * @brief 根据 DayIdentifier 获取配置行
     * @param DayIdentifier 天数标识（如"day1", "day2"）
     * @return 配置行指针，找不到则返回 nullptr
     */
    const FDailyUpgradeRewardConfigRow* GetConfigRowForDay(const FString& DayIdentifier) const;

    /**
     * @brief 获取配置表行（封装了ID 110的查找逻辑）
     * @return 配置表行指针，找不到则返回nullptr
     */
    const FDailyUpgradeRewardConfigRow* GetActivityConfig();
    
    /**
     * @brief 获取额外配置数据（ActivityID=102, DayIdentifier=day1）
     * @return 额外配置数据指针
     */
    const FDailyUpgradeRewardConfigRow* GetExtraConfigForDay1();
    
    /**
     * @brief 获取每日任务DayIdentifier数组
     * @return DayIdentifier数组（如"day1", "day2", "day3"...）
     * @details 从ActivityID=102的所有配置行中收集DayIdentifier字段
     */
    TArray<FString> GetDailyTaskDescriptions();
    
    /**
     * @brief 获取最大记录日期
     * @return 最大记录日期（1-7之间的数字）
     * @details 遍历UpgradeRewardSaveRecord动态表数据，找到RecordDate字段的最大值
     * 如果超出7则返回7，最小值为1
     */
    int32 GetMaxRecordDate() const;
    
    /**
     * @brief 检查内存中是否存在指定天数的数据
     * @param DayNumber 天数编号（例如：1, 2, 3...）
     * @return 如果内存中存在该天数的数据则返回true，否则返回false
     * @details 检查AllRecords映射表中是否存在指定天数的记录
     */
    UFUNCTION(BlueprintCallable, Category = "UpgradeActivity|Data")
    bool HasDayDataInMemory(int32 DayNumber) const;
    
    /**
     * @brief 获取指定天数的奖励图标数组
     * @param DayIdentifier 天数标识符（如 "day1", "day2" 等）
     * @return 图标纹理数组，用于显示在TaskRewardIconsContainer中
     * @details 业务逻辑下沉：根据DayIdentifier获取RewardItemIDs，解析后关联TreasureBoxItemRow获取BoxIcon
     */
    UFUNCTION(BlueprintCallable, Category = "UpgradeActivity|Data")
    TArray<UTexture2D*> GetRewardIconsForDay(const FString& DayIdentifier) const;
    
    /**
     * @brief 获取指定天数的限时奖励图标数组
     * @param DayIdentifier 天数标识符（如 "day1", "day2" 等）
     * @return 图标纹理数组，用于显示在LimitedTimeRewardIconsContainer中
     * @details 业务逻辑：根据DayIdentifier获取BonusIDs，关联ItemDetailRow表获取ItemIcon
     */
    UFUNCTION(BlueprintCallable, Category = "UpgradeActivity|Data")
    TArray<UTexture2D*> GetLimitedTimeRewardIconsForDay(const FString& DayIdentifier) const;
    
    /**
     * @brief 获取限时活动完成次数
     * @return LimitedActivityCompleteCount值
     */
    UFUNCTION(BlueprintCallable, Category = "UpgradeActivity|Data")
    int32 GetLimitedActivityCompleteCount() const;
    
    /**
     * @brief 获取记录创建时间
     * @return CreatedTime值
     */
    UFUNCTION(BlueprintCallable, Category = "UpgradeActivity|Data")
    FDateTime GetRecordCreatedTime() const;
    
    /**
     * @brief 获取每日任务高亮状态数组（核心业务逻辑）
     * @return 布尔数组，每个元素对应一个任务按钮是否应该高亮显示
     * @details 业务逻辑下沉：根据最大RecordDate值确定哪些天数按钮应该显示高亮
     * 数组索引对应任务按钮索引（0=day1, 1=day2, ... 6=day7）
     * @note 此方法封装了完整的业务判断逻辑，UI层只需使用结果
     */
    TArray<bool> GetDailyTaskHighlightStates() const;
    
    /**
     * @brief 获取每日任务锁定状态数组（核心业务逻辑）
     * @return 布尔数组，每个元素对应一个任务按钮是否应该显示锁定图标
     * @details 业务逻辑下沉：根据最大 RecordDate 值确定哪些天数按钮应该显示锁定
     * 数组索引对应任务按钮索引（0=day1, 1=day2, ... 6=day7）
     * 显示锁定条件：DayText 数字 > RecordDate 最大值
     * @note 此方法封装了完整的业务判断逻辑，UI 层只需使用结果
     */
    TArray<bool> GetDailyTaskLockStates() const;
        
    /**
     * @brief 获取指定天数的处理后任务描述数组（核心业务逻辑）
     * @param DayIdentifier 天数标识（如"day1", "day2"）
     * @return 处理后的任务描述数组（已替换占位符）
     * @details 业务逻辑下沉：从 ActivityID=102 且 DayIdentifier=指定的配置行中获取 TaskDescriptions
     * 并对每个描述中的占位符进行替换：
     * - 将"游玩匹配模式（a/b）局"中的 b 替换为 TaskRelatedValues 对应值
     * - 将"游玩匹配模式（a/b）局"中的 a 替换为 TaskCompleteCounts 对应值
     * @note 此方法封装了完整的文本处理逻辑，UI 层只需使用结果
     */
    TArray<FString> GetProcessedTaskDescriptionsForDay(const FString& DayIdentifier) const;
    
    /**
     * @brief 获取当前天的处理后任务描述数组（核心业务逻辑）
     * @return 处理后的任务描述数组（已替换占位符）
     * @details 业务逻辑下沉：根据 CurrentRecord.RecordDate 获取对应天的配置并处理 TaskDescriptions
     * 并对每个描述中的占位符进行替换：
     * - 将"游玩匹配模式（a/b）局"中的 b 替换为 TaskRelatedValues 对应值
     * - 将"游玩匹配模式（a/b）局"中的 a 替换为 TaskCompleteCounts 对应值
     * @note 此方法封装了完整的文本处理逻辑，UI 层只需使用结果
     */
    TArray<FString> GetProcessedTaskDescriptionsForCurrentDay() const;

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
    
    /**
     * @brief 检查固定奖励控件是否应该显示高亮
     * @details 根据TaskRelatedValues最后一个索引值和ChestClaimStatus状态判断
     * 显示条件：CurrentExperience >= TaskRelatedValues最后一个值 且 ChestClaimStatus = 0
     * @return 是否应该显示高亮
     */
    bool ShouldShowFixedPrizeHighlight();
    
    /**
     * @brief 获取固定奖励控件的经验值显示
     * @details 返回TaskRelatedValues数组中最后一个索引的值
     * @return 最后一个TaskRelatedValues值，失败时返回0
     */
    int32 GetFixedPrizeExperienceValue();
    
    /**
     * @brief 获取固定奖励控件的索引
     * @details 返回TaskRelatedValues数组的最后一个索引
     * @return 最后一个索引值，失败时返回-1
     */
    int32 GetFixedPrizeIndex();
    
    /**
     * @brief 获取固定奖励控件的宝箱图标
     * @details 按照指定逻辑获取FixedPrizeWidget的ChestClaimButton图标数据：
     * 1. 找到DailyUpgradeRewardConfigRow表中ActivityID==110的数据
     * 2. 获取RewardItemIDs里面最后一个索引的内容
     * 3. 关联TreasureBoxItemRow表的BoxID得到对应的BoxIcon数据
     * @return 宝箱图标纹理，失败时返回nullptr
     */
    UTexture2D* GetFixedPrizeBoxIcon();
    
    /**
     * @brief 获取固定奖励控件的宝箱数量
     * @details 找到DailyUpgradeRewardConfigRow表中ActivityID==110的数据，
     * 获取RewardItemCounts数组内最后一个索引数据
     * @return 宝箱数量字符串，失败时返回"0"
     */
    FString GetFixedPrizeChestCount();
    
    /**
     * @brief 计算固定奖励控件进度条百分比
     * @details 根据CurrentExperience值映射到286-315区间计算进度百分比
     * 数值区间286-315对应0%-100%进度显示
     * @return 进度百分比(0.0f-1.0f)，失败时返回0.0f
     */
    float CalculateFixedPrizeProgress();
    
    /**
     * @brief 根据当前经验值获取应该居中的宝箱索引
     * @details 这是业务逻辑，与具体UI无关
     * @return 应该居中的宝箱索引
     */
    int32 GetTargetChestIndexForCurrentExperience() const;

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
     * @brief 重新加载最新的记录
     * @details 强制从存档中重新加载CreatedTime最新的记录
     */
    void ReloadLatestRecord();

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
    
    /**
     * @brief 获取存档游戏实例
     * @return 存档游戏实例指针，如果加载失败则返回nullptr
     * @details 从磁盘加载存档游戏实例，用于访问所有记录数据
     */
    UDailyLoginSaveGame* GetSaveGameInstance() const;


private:
    // ==================== 私有成员 ====================
    
    /** 所有存档记录 - 表格形式存储每一天的数据 */
    TMap<int32, FUpgradeRewardSaveRecord> AllRecords;
    
    /** 当前存档记录 - 页面加载时调取的那一行数据 */
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