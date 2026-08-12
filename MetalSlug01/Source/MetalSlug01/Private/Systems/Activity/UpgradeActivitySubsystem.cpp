/**
 * @file UpgradeActivitySubsystem.cpp
 * @brief 升级奖励活动子系统实现
 * @author AI Assistant
 * @date 2024
 * @version 1.0
 * 
 * @details 该子系统负责管理每日升级奖励活动的核心逻辑，包括：
 * - 活动配置数据的加载和缓存
 * - 玩家进度和状态的持久化管理
 * - 宝箱领取和任务完成的核心业务逻辑
 * - 与其他子系统的数据交互
 * - 重选奖励功能的数据提供
 */

#include "Systems/Activity/UpgradeActivitySubsystem.h"
#include "Engine/DataTable.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/Engine.h"
#include "Engine/GameInstance.h" // GetGameInstance
#include "Systems/Activity/ActivitySubsystem.h"
#include "Misc/DateTime.h"
#include "Tools/UpgradeActivitySaveModifier.h"
#include "Data/FActivityDataTableService.h" // 活动表统一加载入口
#include "Logs/MetalSlugLogChannels.h"

namespace
{
	// 内部辅助: 集中获取 ActivitySubsystem, 减少 8 处重复 GetSubsystem
	UActivitySubsystem* GetActivitySub(const UObject* WorldContext)
	{
		if (!WorldContext) return nullptr;
		UGameInstance* GI = WorldContext->GetWorld() ? WorldContext->GetWorld()->GetGameInstance() : nullptr;
		return GI ? GI->GetSubsystem<UActivitySubsystem>() : nullptr;
	}
}

/**
 * @brief 子系统初始化函数
 * @details 负责系统启动时的初始化工作：
 * 1. 预加载每日升级奖励活动的配置表数据
 * 2. 从存档中加载玩家的历史记录
 * 3. 如果没有存档或存档损坏，则创建今日的新记录
 * 4. 确保系统处于可用状态
 */
void UUpgradeActivitySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    // 1. 预加载配置表 - 走 FActivityDataTableService 统一管理
    CachedConfigTable = FActivityDataTableService::Get(ActivityDataTable::DailyUpgradeReward);

    // 2. 检查并创建初始记录 - 确保系统有最新的记录
    if (UGameplayStatics::DoesSaveGameExist(SaveSlotName, SaveUserIndex))
    {
        UActivitySaveGame* Loaded = Cast<UActivitySaveGame>(UGameplayStatics::LoadGameFromSlot(SaveSlotName, SaveUserIndex));
        if (Loaded && Loaded->UpgradeRewardRecords.Num() > 0)
        {
            // 存档存在且包含记录，加载最新的记录（不管RecordDate是多少）
            ReloadLatestRecord();
        }
        else
        {
            // 存档存在但没有记录数据，创建第一天记录
            CreateTodayRecord();
            SaveStatus(); // 立即保存到磁盘
        }
    }
    else
    {
        // 完全没有存档，创建全新的第一天记录
        CreateTodayRecord();
        SaveStatus(); // 立即保存到磁盘
    }

    // 3. 初始化升级活动存档修改器
    SaveModifier = NewObject<UUpgradeActivitySaveModifier>(this);
    if (GetWorld())
    {
    	SaveModifier->InitializeModifier(GetWorld(), this);  // 传入World和自身作为Subsystem
    }
    else
    {
    	SaveModifier->InitializeModifier(nullptr, this);  // 传入nullptr和自身作为Subsystem
    }
    SaveModifier->RegisterConsoleCommands();
}

/**
 * @brief 子系统反初始化函数
 * @details 负责系统关闭前的清理工作：
 * 1. 将当前的玩家进度数据保存到磁盘
 * 2. 调用父类的反初始化逻辑
 * 3. 确保数据不会因程序意外退出而丢失
 */
void UUpgradeActivitySubsystem::Deinitialize()
{
    // 保存数据 - 在系统关闭前确保所有进度都被持久化
    SaveStatus();

    // 清理存档修改器
    if (SaveModifier)
    {
        SaveModifier->UnregisterConsoleCommands();
        SaveModifier->DestroyModifier();
        SaveModifier = nullptr;
    }

    Super::Deinitialize();
}

/**
 * @brief 加载玩家状态数据
 * @details 从存档文件中读取玩家的活动进度数据：
 * 1. 检查指定存档槽位是否存在
 * 2. 尝试加载存档数据
 * 3. 验证数据完整性（是否包含ActivityID=110的记录）
 * 4. 如果加载失败则创建新的今日记录
 * @note 这是一个独立的加载函数，可在运行时重新加载数据
 */
void UUpgradeActivitySubsystem::LoadStatus()
{
    ReloadLatestRecord();
}

void UUpgradeActivitySubsystem::ReloadLatestRecord()
{
    if (UGameplayStatics::DoesSaveGameExist(SaveSlotName, SaveUserIndex))
    {
        UActivitySaveGame* LoadedSave = Cast<UActivitySaveGame>(
            UGameplayStatics::LoadGameFromSlot(SaveSlotName, SaveUserIndex));

        if (LoadedSave && LoadedSave->UpgradeRewardRecords.Num() > 0)
        {
            // 🔧 修复：清空 AllRecords 并重新加载所有记录
            AllRecords.Empty();

            // 🔧 修复：将所有存档记录加载到 AllRecords 映射表中
            for (const auto& Pair : LoadedSave->UpgradeRewardRecords)
            {
                AllRecords.Add(Pair.Key, Pair.Value);
            }

            // 找到最新的记录
            const FUpgradeRewardSaveRecord* LatestRecord = nullptr;
            FDateTime LatestTime = FDateTime::MinValue();

            for (const auto& Pair : LoadedSave->UpgradeRewardRecords)
            {
                const FUpgradeRewardSaveRecord& Record = Pair.Value;
                if (Record.CreatedTime > LatestTime)
                {
                    LatestTime = Record.CreatedTime;
                    LatestRecord = &Record;
                }
            }

            if (LatestRecord)
            {
                // 成功加载到最新的活动记录
                CurrentRecord = *LatestRecord;
            }
            else
            {
                // 没有找到有效记录
                CreateTodayRecord();
            }

            // 【v228 新增】同步全局 ChestClaimStatus（SSOT 真源 - 跨天共享）
            // 大厂原则 SSOT: LoadStatus 是唯一允许从 SaveGame 读 GlobalChestClaimStatus 的入口
            // 兜底（仅在数组为空时）: 按 MainConfig.RewardItemIDs.Num() 初始化全零
            GlobalChestClaimStatus = LoadedSave->GlobalChestClaimStatus;
            if (GlobalChestClaimStatus.Num() == 0)
            {
                const FDailyUpgradeRewardConfigRow* MainConfig = GetActivityConfig();
                if (MainConfig && MainConfig->RewardItemIDs.Num() > 0)
                {
                    GlobalChestClaimStatus.SetNumZeroed(MainConfig->RewardItemIDs.Num());
                    UE_LOG(LogTemp, Log,
                        TEXT("[v228] ReloadLatestRecord: GlobalChestClaimStatus 从存档为空，按 MainConfig.RewardItemIDs=%d 初始化全零"),
                        MainConfig->RewardItemIDs.Num());
                }
                else
                {
                    UE_LOG(LogTemp, Warning,
                        TEXT("[v228] ReloadLatestRecord: MainConfig.RewardItemIDs 也为空，GlobalChestClaimStatus 保持空数组"));
                }
            }
            else
            {
                UE_LOG(LogTemp, Log,
                    TEXT("[v228] ReloadLatestRecord: 从存档加载 GlobalChestClaimStatus (大小=%d)"),
                    GlobalChestClaimStatus.Num());
            }
        }
        else
        {
            // 存档损坏或不包含该活动数据
            CreateTodayRecord();
            // 兜底初始化 GlobalChestClaimStatus
            GlobalChestClaimStatus.Empty();
        }
    }
    else
    {
        // 没有找到存档文件
        CreateTodayRecord();
        // 兜底初始化 GlobalChestClaimStatus
        GlobalChestClaimStatus.Empty();
    }
}

/**
 * @brief 【v217 DEPRECATED for task fields】获取 MainConfig (ActivityID=110 的第一行)
 * @return 指向 ActivityID=110 的配置数据指针；如果找不到则返回 nullptr
 *
 * @warning 【v217 SSOT 警告】严禁用于以下 day-specific 字段:
 *          - TaskTypes
 *          - TaskDescriptions
 *          - TaskRelatedValues
 *          - GameModes
 *          - RewardItemIDs
 *          - RewardItemCounts
 *          原因: 这些字段的 day-specific 版本 (ActivityID=102, DayIdentifier=dayX) 才是 UI 渲染/数据初始化的真相源.
 *          MainConfig 的这些字段要么为空、要么语义不同 (例如 RewardItemIDs 在 MainConfig 是"全局宝箱列表",
 *          在 day-specific Config 是"per-task 宝箱列表"),混用会产生数据不一致.
 *          → 业务 API 必须改用 GetExtraConfigForSpecificDay(DayNumber).
 *
 * @warning 【v217 零兜底】严禁在找不到 day-specific Config 时回退到本 API 作为兜底.
 *          找不到 day-specific Config 应直接 Log Error + return, 强制修复 DT_DailyUpgradeRewardConfigRow 缺行.
 *
 * @note 允许用途 (MainConfig 字段访问):
 *       - BonusDescription / BonusDurationHours / BonusCount / BonusIDs (全局限时加成元数据)
 *       - RewardItemIDs.Num() 用于宝箱 UI 数量固定初始化 (前提是 day-specific Config 不参与)
 *       - GetChestBoxIcons / GetChestCount 等全局宝箱图标 API
 *       - InitializeTodayRecordData 里的"备用"字段 (例如 day1 Config 缺失时的临时回退, 已废弃)
 *
 * @note 性能: 使用缓存机制避免重复加载
 */
const FDailyUpgradeRewardConfigRow* UUpgradeActivitySubsystem::GetActivityConfig()
{
    if (!CachedConfigTable)
    {
        UE_LOG(LogTemp, Error,
            TEXT("[UUpgradeActivitySubsystem] GetActivityConfig: CachedConfigTable 为空, 无法查找 ActivityID=110."));
        return nullptr;
    }
    static const FString ContextString(TEXT("UpgradeSubsystem"));

    // 🔧【v230 热重载修复】改用 GetRowNames() + FindRow() 路径
    // 原因: GetRowMap() 在热重载后可能返回失效指针，导致 Row->ActivityID 访问崩溃或数据错误
    // 这与 DT_TreasureBoxItem 的 FindRowByIdSafe 使用相同的安全路径
    const TArray<FName> RowNames = CachedConfigTable->GetRowNames();
    for (const FName& RowName : RowNames)
    {
        FDailyUpgradeRewardConfigRow* Row = CachedConfigTable->FindRow<FDailyUpgradeRewardConfigRow>(RowName, ContextString, /*bWarnIfRowMissing=*/false);
        if (Row && Row->ActivityID == 110)
        {
            // 找到目标活动配置
            return Row;
        }
    }
    UE_LOG(LogTemp, Error,
        TEXT("[UUpgradeActivitySubsystem] GetActivityConfig: 遍历 %d 行未找到 ActivityID=110."),
        RowNames.Num());
    return nullptr;
}

const FUpgradeRewardSaveRecord* UUpgradeActivitySubsystem::GetRecordByDate(int32 RecordDate) const
{
    if (AllRecords.Contains(RecordDate))
    {
        return &AllRecords[RecordDate];
    }
    return nullptr;
}

void UUpgradeActivitySubsystem::AddOrUpdateRecord(int32 RecordDate, const FUpgradeRewardSaveRecord& Record)
{
    AllRecords.Add(RecordDate, Record);

}

const FDailyUpgradeRewardConfigRow* UUpgradeActivitySubsystem::GetConfigRowForDay(const FString& DayIdentifier) const
{
    if (!CachedConfigTable)
    {
        return nullptr;
    }

    // 遍历配置表查找指定的 DayIdentifier
    for (auto& Pair : CachedConfigTable->GetRowMap())
    {
        FDailyUpgradeRewardConfigRow* Row = (FDailyUpgradeRewardConfigRow*)Pair.Value;
        if (Row && Row->ActivityID == 102 && Row->DayIdentifier == DayIdentifier)
        {
            return Row;
        }
    }
    return nullptr;
}

/**
 * @brief 领取宝箱奖励
 * @param ChestIndex 宝箱索引
 * @return 是否领取成功
 * @details 处理玩家领取宝箱的核心业务逻辑：
 * 1. 验证宝箱索引的有效性和可领取状态
 * 2. 更新宝箱领取状态（0=未领取 -> 1=已领取）
 * 3. 增加玩家经验值（固定20点）
 * 4. 更新最后操作时间
 * 5. 持久化保存数据变更
 * @note 这替代了原来Widget中的OnChestClaimButtonClicked逻辑，但增加了持久化功能
 */
bool UUpgradeActivitySubsystem::ClaimChest(int32 ChestIndex)
{
    // 【v228 SSOT 修正】ChestClaimStatus 是全局状态（跨天共享）
    // 原实现用 CurrentRecord.ChestClaimStatus 是 per-day, 切天后会被重置, 这是用户报告的 Bug 2 真因

    // 零兜底: ChestIndex 必须落在 GlobalChestClaimStatus 范围内
    if (!GlobalChestClaimStatus.IsValidIndex(ChestIndex))
    {
        UE_LOG(LogTemp, Error,
            TEXT("[UUpgradeActivitySubsystem] ClaimChest: ChestIndex=%d 超出 GlobalChestClaimStatus 范围 (大小=%d)"),
            ChestIndex, GlobalChestClaimStatus.Num());
        return false;
    }

    // 已领取则拒绝（防止重复领取）
    if (GlobalChestClaimStatus[ChestIndex] == 1)
    {
        UE_LOG(LogTemp, Log,
            TEXT("[UUpgradeActivitySubsystem] ClaimChest: ChestIndex=%d 已领取, 拒绝重复领取"),
            ChestIndex);
        return false;
    }

    // 标记已领取（全局真源）
    GlobalChestClaimStatus[ChestIndex] = 1;

    // 经验值累加仍走 per-day record（CurrentExperience 是 per-day, 不变）
    CurrentRecord.CurrentExperience += 20;
    CurrentRecord.LastUpdateTime = FDateTime::Now();

    SaveStatus(); // 持久化（同时回写 GlobalChestClaimStatus）
    return true;
}

/**
 * @brief 【v228 新增】修改全局宝箱领取状态（SSOT 真源写入 - 跨天共享）
 *
 * 大厂原则 SSOT:
 *   - 唯一允许写入 Subsystem 内部 GlobalChestClaimStatus 的入口
 *   - Page/Widget/ViewModel 严禁直接写 Subsystem 内部字段
 *   - 自动同步到 SaveGame->GlobalChestClaimStatus（落盘）
 *
 * 零兜底:
 *   - ChestIndex 越界 → Log Error + return false
 *   - IsClaimed 非法（不是 0/1）→ Log Error + return false
 *
 * @return true=成功写入并落盘；false=任一校验失败
 */
bool UUpgradeActivitySubsystem::ModifyGlobalChestClaimStatus(int32 ChestIndex, int32 IsClaimed, bool bAutoSave)
{
    if (!GlobalChestClaimStatus.IsValidIndex(ChestIndex))
    {
        UE_LOG(LogTemp, Error,
            TEXT("[UUpgradeActivitySubsystem] ModifyGlobalChestClaimStatus: ChestIndex=%d 超出 GlobalChestClaimStatus 范围 (大小=%d)"),
            ChestIndex, GlobalChestClaimStatus.Num());
        return false;
    }

    if (IsClaimed != 0 && IsClaimed != 1)
    {
        UE_LOG(LogTemp, Error,
            TEXT("[UUpgradeActivitySubsystem] ModifyGlobalChestClaimStatus: IsClaimed=%d 非法，必须为 0 或 1"),
            IsClaimed);
        return false;
    }

    GlobalChestClaimStatus[ChestIndex] = IsClaimed;

    if (bAutoSave)
    {
        SaveStatus();
    }
    return true;
}

/**
 * @brief 更新任务进度
 * @param TaskIndex 任务索引
 * @param Count 完成数量
 * @details 更新指定任务的完成进度：
 * 1. 验证输入参数的有效性（索引和数量不能为负）
 * 2. 确保任务进度数组有足够的容量
 * 3. 更新指定任务的完成计数
 * 4. 记录更新时间戳
 * @note 这个方法只更新进度，不处理奖励领取逻辑
 */
void UUpgradeActivitySubsystem::UpdateTaskProgress(int32 TaskIndex, int32 Count)
{
    if (TaskIndex < 0 || Count < 0)
    {
        return;
    }

    // 确保数组大小足够 - 动态扩展数组以适应任务索引
    while (CurrentRecord.TaskCompleteCounts.Num() <= TaskIndex)
    {
        CurrentRecord.TaskCompleteCounts.Add(0);
    }

    CurrentRecord.TaskCompleteCounts[TaskIndex] = Count;
    CurrentRecord.LastUpdateTime = FDateTime::Now();
}

/**
 * @brief 领取任务奖励
 * @param TaskIndex 任务索引
 * @return 是否领取成功
 * @details 处理玩家领取任务奖励的完整业务流程：
 * 1. 获取活动配置数据
 * 2. 验证任务索引的有效性
 * 3. 检查任务是否已经领取过
 * 4. 验证任务是否已完成（完成度达到要求）
 * 5. 更新任务领取状态
 * 6. 增加玩家经验值（固定50点）
 * 7. 持久化保存数据
 */
bool UUpgradeActivitySubsystem::ClaimTaskReward(int32 TaskIndex)
{
    // 🔧【v217 SSOT 重构】统一用 day-specific Config, 不再用 MainConfig 第一行
    // 大厂原则: SSOT - TaskTypes / TaskDescriptions / TaskRelatedValues 必须从 day-specific Config 读
    // 旧实现 GetActivityConfig() 返回 ActivityID=110 的第一行, 其 TaskTypes 数组可能为空,
    // 导致 TaskIndex 边界检查永远越界 → 领取永远失败 → 按钮状态永不更新 (v216 bug)
    // 当前 CurrentRecord.GetDayNumber() → 构造 day%d → 找 ActivityID=102 的 day-specific Config
    const int32 CurrentDayNumber = CurrentRecord.GetDayNumber();
    const FDailyUpgradeRewardConfigRow* Config = GetExtraConfigForSpecificDay(CurrentDayNumber);
    if (!Config)
    {
        UE_LOG(LogTemp, Error,
            TEXT("[UUpgradeActivitySubsystem] ClaimTaskReward: 找不到 Day=%d 的 Config (DayIdentifier=day%d, ActivityID=102). "
                 "请检查 DT_DailyUpgradeRewardConfigRow 是否配置 day%d 行. 拒绝领取."),
            CurrentDayNumber, CurrentDayNumber, CurrentDayNumber);
        return false;
    }

    // 验证索引 - 确保任务索引在有效范围内
    if (!IsValidIndex(TaskIndex, Config->TaskTypes.Num()))
    {
        return false;
    }

    // 检查是否已领取 - 防止重复领取
    if (!IsValidIndex(TaskIndex, CurrentRecord.TaskClaimStatus.Num()) || 
        CurrentRecord.TaskClaimStatus[TaskIndex] == 1)
    {
        return false;
    }

    // 检查任务完成度 - 验证是否满足领取条件
    if (!IsValidIndex(TaskIndex, CurrentRecord.TaskCompleteCounts.Num()) ||
        CurrentRecord.TaskCompleteCounts[TaskIndex] < 
        (Config->TaskRelatedValues.IsValidIndex(TaskIndex) ? Config->TaskRelatedValues[TaskIndex] : 0))
    {
        return false;
    }

    // 执行领取 - 更新任务状态
    if (CurrentRecord.TaskClaimStatus.IsValidIndex(TaskIndex))
    {
        CurrentRecord.TaskClaimStatus[TaskIndex] = 1;
    }
    else
    {
        // 扩展数组 - 动态确保数组容量足够
        while (CurrentRecord.TaskClaimStatus.Num() <= TaskIndex)
        {
            CurrentRecord.TaskClaimStatus.Add(0);
        }
        CurrentRecord.TaskClaimStatus[TaskIndex] = 1;
    }

    // 增加经验值 - 任务奖励固定50点经验
    CurrentRecord.CurrentExperience += 50;

    CurrentRecord.LastUpdateTime = FDateTime::Now();
    SaveStatus(); // 持久化保存变更
    return true;
}

/**
 * @brief UUpgradeActivitySubsystem::ClaimTaskRewardForDay
 *
 * 流程 (与 ClaimTaskReward 一致, 但基于 GetRecordByDate):
 * 1. 防御: Config / DayRecord
 * 2. 验证 TaskIndex 范围
 * 3. 验证未领取 (防重复)
 * 4. 验证 TaskCompleteCounts >= TaskRelatedValues (任务完成度)
 * 5. TaskClaimStatus[TaskIndex] = 1 (数组不足自动扩容)
 * 6. MutableRecord.LastUpdateTime = Now
 * 7. AddOrUpdateRecord(DayNumber, MutableRecord) 持久化
 * 8. 如果当前 CurrentRecord 是同一 day, 同步更新 CurrentRecord
 *
 * @param DayNumber 天数 (1-based)
 * @param TaskIndex 任务索引
 * @return 是否领取成功
 */
bool UUpgradeActivitySubsystem::ClaimTaskRewardForDay(int32 DayNumber, int32 TaskIndex)
{
    // 防御: Config
    // 🔧【v217 SSOT 重构】统一用 day-specific Config, 不再用 MainConfig 第一行
    // 大厂原则: SSOT - TaskTypes / TaskDescriptions / TaskRelatedValues 必须从 day-specific Config 读
    // 旧实现 GetActivityConfig() 返回 ActivityID=110 的第一行, 其 TaskTypes 数组可能为空,
    // 导致 TaskIndex 边界检查永远越界 → 领取永远失败 → 按钮状态永不更新 (v216 bug)
    const FDailyUpgradeRewardConfigRow* Config = GetExtraConfigForSpecificDay(DayNumber);
    if (!Config)
    {
        UE_LOG(LogTemp, Error,
            TEXT("[UUpgradeActivitySubsystem] ClaimTaskRewardForDay: 找不到 Day=%d 的 Config (DayIdentifier=day%d, ActivityID=102). "
                 "请检查 DT_DailyUpgradeRewardConfigRow 是否配置 day%d 行. 拒绝领取."),
            DayNumber, DayNumber, DayNumber);
        return false;
    }

    // 防御: DayRecord
    const FUpgradeRewardSaveRecord* DayRecord = GetRecordByDate(DayNumber);
    if (!DayRecord)
    {
        UE_LOG(LogTemp, Error,
            TEXT("[UUpgradeActivitySubsystem] ClaimTaskRewardForDay: 找不到 Day=%d 的记录"),
            DayNumber);
        return false;
    }

    // 验证 TaskIndex 范围
    if (!IsValidIndex(TaskIndex, Config->TaskTypes.Num()))
    {
        UE_LOG(LogTemp, Error,
            TEXT("[UUpgradeActivitySubsystem] ClaimTaskRewardForDay: TaskIndex(%d) 超出 TaskTypes 数组范围 (%d)"),
            TaskIndex, Config->TaskTypes.Num());
        return false;
    }

    // 检查是否已领取 - 防止重复领取
    if (DayRecord->TaskClaimStatus.IsValidIndex(TaskIndex) &&
        DayRecord->TaskClaimStatus[TaskIndex] == 1)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[UUpgradeActivitySubsystem] ClaimTaskRewardForDay: Day=%d, TaskIndex=%d 已领取, 跳过"),
            DayNumber, TaskIndex);
        return false;
    }

    // 检查任务完成度
    const int32 RequiredCount = Config->TaskRelatedValues.IsValidIndex(TaskIndex)
        ? Config->TaskRelatedValues[TaskIndex] : 0;
    if (!DayRecord->TaskCompleteCounts.IsValidIndex(TaskIndex) ||
        DayRecord->TaskCompleteCounts[TaskIndex] < RequiredCount)
    {
        UE_LOG(LogTemp, Error,
            TEXT("[UUpgradeActivitySubsystem] ClaimTaskRewardForDay: 任务未完成 Day=%d, TaskIndex=%d, 当前=%d, 需要=%d"),
            DayNumber, TaskIndex,
            DayRecord->TaskCompleteCounts.IsValidIndex(TaskIndex) ? DayRecord->TaskCompleteCounts[TaskIndex] : -1,
            RequiredCount);
        return false;
    }

    // 创建可修改副本
    FUpgradeRewardSaveRecord MutableRecord = *DayRecord;

    // 设置 TaskClaimStatus[TaskIndex] = 1 (数组不足自动扩容)
    if (MutableRecord.TaskClaimStatus.IsValidIndex(TaskIndex))
    {
        MutableRecord.TaskClaimStatus[TaskIndex] = 1;
    }
    else
    {
        while (MutableRecord.TaskClaimStatus.Num() <= TaskIndex)
        {
            MutableRecord.TaskClaimStatus.Add(0);
        }
        MutableRecord.TaskClaimStatus[TaskIndex] = 1;
    }

    // 更新 LastUpdateTime
    MutableRecord.LastUpdateTime = FDateTime::Now();

    // 持久化: 写回 SaveGame->UpgradeRewardRecords map
    AddOrUpdateRecord(DayNumber, MutableRecord);

    // 如果这是 CurrentRecord 对应的 day, 同步更新 CurrentRecord
    //   (CurrentRecord 应该是 map 中某项的引用, AddOrUpdateRecord 已更新引用,
    //    这里仅做防御性同步, 避免引用失效)
    if (DayNumber == CurrentRecord.GetDayNumber())
    {
        CurrentRecord = MutableRecord;
    }

    // 持久化保存到磁盘 (与 ClaimTaskReward 行为一致)
    SaveStatus();

    UE_LOG(LogTemp, Log,
        TEXT("[UUpgradeActivitySubsystem] ClaimTaskRewardForDay: 领取成功 Day=%d, TaskIndex=%d"),
        DayNumber, TaskIndex);
    return true;
}

/**
 * @brief 检查宝箱是否可以领取
 * @param ChestIndex 宝箱索引
 * @return 是否可以领取
 * @details 验证宝箱领取的前置条件：
 * 1. 检查宝箱索引是否在有效范围内
 * 2. 检查该宝箱是否已经被领取过
 * 3. 验证所有任务是否都已完成（这是领取宝箱的前提条件）
 * @note 这是一个只读查询方法，不会修改任何数据
 */
bool UUpgradeActivitySubsystem::CanClaimChest(int32 ChestIndex) const
{
    // 🔧【v217 SSOT 重构】SSOT 边界明确化: ChestIndex 边界查 MainConfig (全局宝箱数),
    // TaskRelatedValues[i] 查 day-specific Config (per-day 任务数值)
    //
    // 大厂原则: SSOT - 每个字段都有唯一的真相源
    // - MainConfig.RewardItemIDs.Num() = 全局宝箱数 (FixedPrize 初始化时确定)
    //   → ChestClaimStatus 数组初始化长度,ChestIndex 是这个数组的索引
    // - ExtraConfig.TaskRelatedValues[i] = per-day 任务达成阈值
    //   → CurrentRecord.TaskCompleteCounts[i] 与之对比
    //
    // 旧实现: 整个 Config = MainConfig,TaskRelatedValues[i] 从 MainConfig 取 → 跨语义数据污染

    // SSOT-1: MainConfig 提供 ChestIndex 边界 (全局宝箱数)
    const FDailyUpgradeRewardConfigRow* MainConfig = const_cast<UUpgradeActivitySubsystem*>(this)->GetActivityConfig();
    if (!MainConfig)
    {
        UE_LOG(LogTemp, Error,
            TEXT("[UUpgradeActivitySubsystem] CanClaimChest: 找不到 MainConfig (ActivityID=110), 拒绝检查."));
        return false;
    }

    // 检查索引有效性 - 确保宝箱索引在配置范围内
    if (!IsValidIndex(ChestIndex, MainConfig->RewardItemIDs.Num()))
        return false;

    // 检查是否已领取 - 防止重复领取（v228 SSOT: 读全局而非 per-day record）
    if (IsValidIndex(ChestIndex, GlobalChestClaimStatus.Num()) &&
        GlobalChestClaimStatus[ChestIndex] == 1)
        return false;

    // SSOT-2: day-specific Config 提供 per-day TaskRelatedValues (任务达成阈值)
    const int32 CurrentDayNumber = CurrentRecord.GetDayNumber();
    const FDailyUpgradeRewardConfigRow* DayConfig = const_cast<UUpgradeActivitySubsystem*>(this)->GetExtraConfigForSpecificDay(CurrentDayNumber);
    if (!DayConfig)
    {
        UE_LOG(LogTemp, Error,
            TEXT("[UUpgradeActivitySubsystem] CanClaimChest: 找不到 Day=%d 的 day-specific Config, 拒绝检查任务完成度."),
            CurrentDayNumber);
        return false;
    }

    // 检查任务完成情况 - 所有任务都必须完成才能领取宝箱
    for (int32 i = 0; i < CurrentRecord.TaskCompleteCounts.Num(); ++i)
    {
        if (CurrentRecord.TaskCompleteCounts[i] <
            (DayConfig->TaskRelatedValues.IsValidIndex(i) ? DayConfig->TaskRelatedValues[i] : 0))
        {
            return false;
        }
    }

    return true;
}

/**
 * @brief 检查任务奖励是否可以领取
 * @param TaskIndex 任务索引
 * @return 是否可以领取
 * @details 验证任务奖励领取的前置条件：
 * 1. 检查任务索引是否在有效范围内
 * 2. 检查该任务奖励是否已经被领取过
 * 3. 验证任务完成度是否达到领取要求
 * @note 这是一个只读查询方法，用于UI状态显示和领取按钮的启用/禁用控制
 */
bool UUpgradeActivitySubsystem::CanClaimTask(int32 TaskIndex) const
{
    // 🔧【v217 SSOT 重构】任务检查必须用 day-specific Config
    // 旧实现 GetActivityConfig() 返回 MainConfig, TaskTypes.Num() / TaskRelatedValues[i] 都是错的
    // 改为 GetExtraConfigForSpecificDay(CurrentRecord.GetDayNumber())
    const int32 CurrentDayNumber = CurrentRecord.GetDayNumber();
    const FDailyUpgradeRewardConfigRow* Config = const_cast<UUpgradeActivitySubsystem*>(this)->GetExtraConfigForSpecificDay(CurrentDayNumber);
    if (!Config)
    {
        UE_LOG(LogTemp, Error,
            TEXT("[UUpgradeActivitySubsystem] CanClaimTask: 找不到 Day=%d 的 day-specific Config, 拒绝检查."),
            CurrentDayNumber);
        return false;
    }

    // 检查索引有效性 - 确保任务索引在配置范围内
    if (!IsValidIndex(TaskIndex, Config->TaskTypes.Num()))
        return false;

    // 检查是否已领取 - 防止重复领取
    if (IsValidIndex(TaskIndex, CurrentRecord.TaskClaimStatus.Num()) &&
        CurrentRecord.TaskClaimStatus[TaskIndex] == 1)
        return false;

    // 检查任务完成度 - 验证是否满足领取条件
    if (!IsValidIndex(TaskIndex, CurrentRecord.TaskCompleteCounts.Num()) ||
        CurrentRecord.TaskCompleteCounts[TaskIndex] <
        (Config->TaskRelatedValues.IsValidIndex(TaskIndex) ? Config->TaskRelatedValues[TaskIndex] : 0))
        return false;

    return true;
}

/**
 * @brief 保存玩家状态数据
 * @details 将当前的玩家活动进度持久化到磁盘：
 * 1. 从磁盘加载现有的存档数据（如果存在）
 * 2. 如果没有存档则创建新的存档对象
 * 3. 更新或添加ActivityID=110的活动记录
 * 4. 将完整的存档数据写入磁盘
 * @note 使用相同的存档槽位和用户索引，与DailyLogin系统共享存档
 */
void UUpgradeActivitySubsystem::SaveStatus()
{
    // 保存到现有的DailyLoginSaveGame中 - 与登录系统共享同一个存档文件
    UActivitySaveGame* SaveGame = Cast<UActivitySaveGame>(UGameplayStatics::LoadGameFromSlot(SaveSlotName, SaveUserIndex));
    if (!SaveGame)
    {
        // 没有现有存档，创建新的存档对象
        SaveGame = NewObject<UActivitySaveGame>();
    }

    // 更新或添加所有升级奖励记录 - 保存 AllRecords 中的所有数据
    SaveGame->UpgradeRewardRecords.Empty();
    for (const auto& Pair : AllRecords)
    {
        SaveGame->UpgradeRewardRecords.Add(Pair.Key, Pair.Value);
    }

    // 【v228 新增】同步全局 ChestClaimStatus（SSOT 真源 - 跨天共享）
    // 大厂原则: SaveStatus 是唯一允许写 SaveGame->GlobalChestClaimStatus 的入口
    SaveGame->GlobalChestClaimStatus = GlobalChestClaimStatus;

    // 保存到磁盘
    UGameplayStatics::SaveGameToSlot(SaveGame, SaveSlotName, SaveUserIndex);
}


/**
 * @brief 【v222 新增】一键重置整个 DailyUpgradeReward 页面所有活动进度
 *
 * 执行步骤 (严格顺序, 每步带日志):
 *   1. 清空内存 AllRecords
 *   2. 清空磁盘 SaveGame.UpgradeRewardRecords (先 LoadGameFromSlot, 避免清掉其他业务数据)
 *   3. SaveGameToSlot 立即落盘
 *   4. CreateTodayRecord() 重建 day1 (复用, 内部已含 day1 Config 零兜底校验)
 *   5. Broadcast OnGlobalRefresh + OnRewardIconIndexChanged (UI 自动刷新)
 *
 * 大厂原则 (单一真理):
 *   - 严禁在 ViewModel/Page 各自操作 AllRecords 或 SaveGame → 数据漂移
 *   - 走与 ClaimTaskReward/ModifyCurrentExperience 相同的"Subsystem 写 -> 广播"路径
 */
bool UUpgradeActivitySubsystem::ResetAllUpgradeActivityProgress()
{
    UE_LOG(LogTemp, Log, TEXT("\n==========================================================="));
    UE_LOG(LogTemp, Log, TEXT("🗑️ [v222] ResetAllUpgradeActivityProgress 开始"));
    UE_LOG(LogTemp, Log, TEXT("==========================================================="));

    // 步骤 1: 清空内存中所有 day 的记录
    const int32 OldAllRecordsCount = AllRecords.Num();
    AllRecords.Empty();
    UE_LOG(LogTemp, Log,
        TEXT("[v222] 步骤 1: 内存 AllRecords 已清空 (旧数量=%d)"),
        OldAllRecordsCount);

    // 步骤 1.5: 清空内存中的全局宝箱领取状态
    //   ⚠️ GlobalChestClaimStatus 是独立的全局数据，不属于 per-day record，必须单独重置
    const int32 OldGlobalChestCount = GlobalChestClaimStatus.Num();
    GlobalChestClaimStatus.Empty();
    UE_LOG(LogTemp, Log,
        TEXT("[v222] 步骤 1.5: 内存 GlobalChestClaimStatus 已清空 (旧大小=%d)"),
        OldGlobalChestCount);

    // 步骤 2: 清空磁盘存档中的 UpgradeRewardRecords 字段
    //   ⚠️ 仅清 UpgradeRewardRecords, 不动 SaveGame 的其它字段 (DailyLogin / GlobalChestClaimStatus 等)
    UActivitySaveGame* SaveGame = Cast<UActivitySaveGame>(
        UGameplayStatics::LoadGameFromSlot(SaveSlotName, SaveUserIndex));
    if (!SaveGame)
    {
        // 没有存档: 这是合法的 (新游戏可能根本没存档), 直接 NewObject 一个新存档对象
        SaveGame = NewObject<UActivitySaveGame>();
        UE_LOG(LogTemp, Warning,
            TEXT("[v222] 步骤 2: 磁盘无存档, 已创建全新 UActivitySaveGame 对象"));
    }

    SaveGame->UpgradeRewardRecords.Empty();
    UE_LOG(LogTemp, Log, TEXT("[v222] 步骤 2: 磁盘 SaveGame.UpgradeRewardRecords 已清空"));

    // 步骤 2.5: 清空磁盘存档中的 GlobalChestClaimStatus 字段
    SaveGame->GlobalChestClaimStatus.Empty();
    UE_LOG(LogTemp, Log, TEXT("[v222] 步骤 2.5: 磁盘 SaveGame.GlobalChestClaimStatus 已清空"));

    // 步骤 3: 立即 SaveGameToSlot 写盘 (用户明确要求立即同步)
    const bool bSaved = UGameplayStatics::SaveGameToSlot(SaveGame, SaveSlotName, SaveUserIndex);
    if (!bSaved)
    {
        // 零兜底: 落盘失败必须显式 Log Error 并 return false, 不能吞错
        UE_LOG(LogTemp, Error,
            TEXT("[v222] 步骤 3 失败: SaveGameToSlot 失败 (Slot=%s, UserIndex=%d). "
                 "内存已清空但磁盘未持久化, 异常状态!"),
            *SaveSlotName, SaveUserIndex);
        return false;
    }
    UE_LOG(LogTemp, Log, TEXT("[v222] 步骤 3: 立即落盘成功 (Slot=%s)"), *SaveSlotName);

    // 步骤 4: 重建 day1 (复用现有 CreateTodayRecord, 内部含 day1 Config 零兜底)
    //   CreateTodayRecord 内: SetRecordDate(1) + InitializeTodayRecordData() + AllRecords.Add(1, ...)
    CreateTodayRecord();
    if (!AllRecords.Contains(1))
    {
        // CreateTodayRecord 内部 InitializeTodayRecordData 因 day1 Config 缺失而 Log Error return;
        // AllRecords 仍未添加 day1 → 函数失败
        UE_LOG(LogTemp, Error,
            TEXT("[v222] 步骤 4 失败: CreateTodayRecord 未生成 day1 记录. "
                 "请检查 DT_DailyUpgradeRewardConfigRow 是否配置 day1 行 (ActivityID=102, DayIdentifier=day1)."));
        return false;
    }
    UE_LOG(LogTemp, Log, TEXT("[v222] 步骤 4: day1 重建完成 (AllRecords.Num()=%d)"), AllRecords.Num());

    // 步骤 5: 广播刷新事件 (与 ForceRefreshAllPages 同源事件)
    OnGlobalRefresh.Broadcast();
    OnRewardIconIndexChanged.Broadcast(CurrentRecord.RewardIconIndex);

    UE_LOG(LogTemp, Log,
        TEXT("[v222] ResetAllUpgradeActivityProgress 完成: AllRecords清空(%d→0), GlobalChestClaimStatus清空(%d→0), day1重建, 磁盘已同步"),
        OldAllRecordsCount, OldGlobalChestCount);
    UE_LOG(LogTemp, Log, TEXT("===========================================================\n"));

    return true;
}



/**
 * @brief 检查是否存在今天的记录
 * @return 是否存在今天的记录
 * @details 调用记录对象的IsToday方法来判断是否为第一天记录
 * @note 用于确定是否需要创建新的记录（基于天数而非日期）
 */
bool UUpgradeActivitySubsystem::HasTodayRecord() const
{
    return CurrentRecord.IsToday();
}

/**
 * @brief 创建今天的活动记录
 * @details 创建一个新的活动记录，包含完整的初始化逻辑：
 * 1. 设置记录天数为day1（新游戏的第一天）
 * 2. 初始化记录的各项数据字段
 * 3. 特别处理无存档情况下的默认数据填充
 * 4. 记录创建时间和最后更新时间
 */
void UUpgradeActivitySubsystem::CreateTodayRecord()
{
    // 设置记录天数为day1（新游戏的第一天）
    CurrentRecord.SetRecordDate(1);
    // 初始化今日记录的各项数据
    InitializeTodayRecordData();

    // 🔧 修复：将新创建的记录添加到 AllRecords 映射表中
    AllRecords.Add(CurrentRecord.GetDayNumber(), CurrentRecord);
}

/**
 * @brief 初始化今日记录的数据
 * @details 根据活动配置初始化今日记录的各项数据字段，特别处理无存档情况：
 * 1. 获取ActivityID=110的主配置数据
 * 2. 获取ActivityID=102且DayIdentifier=day1的额外配置数据
 * 3. 初始化任务相关数组（完成计数和领取状态）
 * 4. 初始化宝箱相关数组（领取状态）
 * 5. 设置初始值（图标索引、限时活动数据、经验值等）
 * 6. 记录创建和更新时间戳
 * @note 在无存档情况下，按照指定规则填充默认数据
 */
void UUpgradeActivitySubsystem::InitializeTodayRecordData()
{
    // 🔧【v217 SSOT 重构】day-specific Config 是任务数组的唯一真相源
    // 大厂原则: SSOT - 任务相关数组长度必须从 day1 的 day-specific Config 读
    // 旧实现用 GetExtraConfigForDay1() 失败时 fallback 到 MainConfig->TaskTypes.Num(),
    //   这是兜底行为,违反零兜底原则;且两个 Config 字段可能不一致 → 数据漂移.
    // 当前: ExtraConfig 找不到直接 Log Error + return, 强制修复 DT 缺 day1 行.
    const FDailyUpgradeRewardConfigRow* ExtraConfig = GetExtraConfigForDay1();
    if (!ExtraConfig)
    {
        UE_LOG(LogTemp, Error,
            TEXT("[UUpgradeActivitySubsystem] InitializeTodayRecordData: 找不到 day1 的 day-specific Config (ActivityID=102, DayIdentifier=day1). "
                 "请检查 DT_DailyUpgradeRewardConfigRow 是否配置 day1 行. 拒绝初始化."));
        return;
    }

    // 初始化任务相关数组 - 根据 day1 day-specific Config 的 GameModes 数组长度创建
    CurrentRecord.TaskCompleteCounts.Empty();
    CurrentRecord.TaskClaimStatus.Empty();

    // 🔧【v217 零兜底】day1 Config 必须有 GameModes 配置, 不允许 fallback 到 MainConfig
    if (ExtraConfig->GameModes.Num() == 0)
    {
        UE_LOG(LogTemp, Error,
            TEXT("[UUpgradeActivitySubsystem] InitializeTodayRecordData: day1 Config 的 GameModes 数组为空 (Num=0). "
                 "请检查 DT_DailyUpgradeRewardConfigRow 中 day1 行的 GameModes 配置. 拒绝初始化."));
        return;
    }

    const int32 TaskCount = ExtraConfig->GameModes.Num();

    // 为每个任务创建对应的计数和状态记录（初始化为0）
    for (int32 i = 0; i < TaskCount; ++i)
    {
        CurrentRecord.TaskCompleteCounts.Add(0);
        CurrentRecord.TaskClaimStatus.Add(0);
    }

// 初始化宝箱相关数组 - 根据 MainConfig (ActivityID=110) 的 RewardItemIDs 数组长度创建
	// 🔧【v217 SSOT】ChestClaimStatus 数组长度 = MainConfig.RewardItemIDs.Num() (全局宝箱数)
	//   与任务数组 (ExtraConfig.GameModes.Num()) 是两个独立的真相源, 不能混用
	const FDailyUpgradeRewardConfigRow* MainConfig = GetActivityConfig();
	if (!MainConfig)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[UUpgradeActivitySubsystem] InitializeTodayRecordData: 找不到 MainConfig (ActivityID=110). "
				 "请检查 DT_DailyUpgradeRewardConfigRow 是否配置 ActivityID=110 行. 拒绝初始化宝箱数组."));
		return;
	}

	// 【v228 SSOT 重构】ChestClaimStatus 不再属于 per-day record
	// 全局初始化交给 ReloadLatestRecord() / SaveStatus() 处理 (跨天共享)
	// 此处仅记录 ChestCount 供本函数其余逻辑参考
	const int32 ChestCount = MainConfig->RewardItemIDs.Num();

    // 初始化其他字段 - 设置默认初始值
    CurrentRecord.RewardIconIndex = 0;
    CurrentRecord.LimitedActivityStartTime = FDateTime::Now().ToUnixTimestamp(); // 存储当前时间戳
    CurrentRecord.LimitedActivityCompleteCount = 0;
    CurrentRecord.CurrentExperience = 0;

    // 记录时间戳
    FDateTime Now = FDateTime::Now();
    CurrentRecord.CreatedTime = Now;
    CurrentRecord.LastUpdateTime = Now;


}


/**
 * @brief 获取配置表路径
 * @return 配置表的完整路径
 * @details 返回每日升级奖励活动配置表的资源路径
 * @note 路径格式遵循Unreal Engine的资源路径规范
 */
FName UUpgradeActivitySubsystem::GetConfigTablePath() const
{
    // 改造: 走 service 而非返回硬编码字符串
    return ActivityDataTable::DailyUpgradeReward;
}

/**
 * @brief 获取额外配置数据（ActivityID=102, DayIdentifier=day1）
 * @return 指向额外配置数据的指针，如果找不到则返回nullptr
 * @details 从缓存的配置表中查找ActivityID=102且DayIdentifier="day1"的配置信息：
 * 1. 检查配置表是否已加载
 * 2. 遍历配置表的所有行数据
 * 3. 查找符合条件的记录（ActivityID=102且DayIdentifier=day1）
 * 4. 返回找到的配置数据指针
 * @note 用于无存档情况下的数据初始化
 */
const FDailyUpgradeRewardConfigRow* UUpgradeActivitySubsystem::GetExtraConfigForDay1()
{
    if (!CachedConfigTable) return nullptr;
    static const FString ContextString(TEXT("UpgradeSubsystem_Extra"));

    // 遍历配置表查找目标活动
    for (auto& Pair : CachedConfigTable->GetRowMap())
    {
        FDailyUpgradeRewardConfigRow* Row = (FDailyUpgradeRewardConfigRow*)Pair.Value;
        if (Row && Row->ActivityID == 102 && Row->DayIdentifier == TEXT("day1")) 
        {
            // 找到目标额外配置
            return Row;
        }
    }
    return nullptr;
}

TArray<FString> UUpgradeActivitySubsystem::GetDailyTaskDescriptions()
{
    TArray<FString> Result;

    // 检查配置表是否已加载
    if (!CachedConfigTable)
    {
        return Result;
    }

    // 收集所有ActivityID=102的DayIdentifier字段
    for (auto& Pair : CachedConfigTable->GetRowMap())
    {
        FDailyUpgradeRewardConfigRow* Row = (FDailyUpgradeRewardConfigRow*)Pair.Value;
        if (Row && Row->ActivityID == 102)
        {
            Result.Add(Row->DayIdentifier);
        }
    }

    // 按字典序排序确保day1, day2, day3...的顺序
    Result.Sort([](const FString& A, const FString& B) {
        return A.Compare(B) < 0;
    });

    return Result;
}

int32 UUpgradeActivitySubsystem::GetMaxRecordDate() const
{
    // 🔧 优先从内存数据 AllRecords 中获取最大RecordDate
    if (AllRecords.Num() > 0)
    {
        int32 MaxRecordDate = 1;

        for (const auto& Pair : AllRecords)
        {
            int32 CurrentRecordDate = Pair.Key;

            if (CurrentRecordDate > MaxRecordDate)
            {
                MaxRecordDate = CurrentRecordDate;
            }
        }

        // 限制在1-7范围内
        int32 ClampedRecordDate = FMath::Clamp(MaxRecordDate, 1, 7);
        return ClampedRecordDate;
    }

    // 内存数据为空，从存档数据获取
    UActivitySaveGame* LoadedSave = Cast<UActivitySaveGame>(
        UGameplayStatics::LoadGameFromSlot(SaveSlotName, SaveUserIndex));

    if (!LoadedSave || LoadedSave->UpgradeRewardRecords.Num() == 0)
    {
        // 没有存档数据，返回默认值1
        return 1;
    }

    // 遍历所有记录，找出RecordDate的最大值
    int32 MaxRecordDate = 1;

    for (const auto& Pair : LoadedSave->UpgradeRewardRecords)
    {
        const FUpgradeRewardSaveRecord& Record = Pair.Value;
        int32 CurrentRecordDate = Record.GetDayNumber();

        if (CurrentRecordDate > MaxRecordDate)
        {
            MaxRecordDate = CurrentRecordDate;
        }
    }

    // 限制在1-7范围内
    int32 ClampedRecordDate = FMath::Clamp(MaxRecordDate, 1, 7);
    return ClampedRecordDate;
}

bool UUpgradeActivitySubsystem::HasDayDataInMemory(int32 DayNumber) const
{
    // 🔧 核心业务逻辑：检查内存中 AllRecords 映射表是否包含指定天数的数据
    bool bHasData = AllRecords.Contains(DayNumber);
    return bHasData;
}

TArray<bool> UUpgradeActivitySubsystem::GetDailyTaskHighlightStates() const
{
    TArray<bool> HighlightStates;

    // 🔧 核心业务逻辑：获取最大记录日期
    int32 MaxRecordDate = GetMaxRecordDate();

    // 🔧 核心业务逻辑：为7天创建高亮状态数组
    // 索引对应关系：0=day1, 1=day2, ..., 6=day7
    for (int32 i = 0; i < 7; ++i)
    {
        int32 DayNumber = i + 1; // 天数编号（1-7）
        // 🔧 核心业务逻辑：判断是否应该高亮显示
        bool bShouldHighlight = (DayNumber == MaxRecordDate);
        HighlightStates.Add(bShouldHighlight);
    }
    return HighlightStates;
}

TArray<bool> UUpgradeActivitySubsystem::GetDailyTaskLockStates() const
{
    TArray<bool> LockStates;

    // 🔧 核心业务逻辑：获取最大记录日期
    int32 MaxRecordDate = GetMaxRecordDate();


    // 🔧 核心业务逻辑：为7天创建锁定状态数组
    // 索引对应关系：0=day1, 1=day2, ..., 6=day7
    for (int32 i = 0; i < 7; ++i)
    {
        int32 DayNumber = i + 1; // 天数编号（1-7）
        // 🔧 核心业务逻辑：判断是否应该显示锁定图标
        // 显示锁定条件：DayText数字 > RecordDate最大值
        bool bShouldLock = (DayNumber > MaxRecordDate);
        LockStates.Add(bShouldLock);
    }
    return LockStates;
}

TArray<FString> UUpgradeActivitySubsystem::GetProcessedTaskDescriptionsForDay(const FString& DayIdentifier) const
{
    TArray<FString> Result;

    // 🔧 核心业务逻辑：查找指定 DayIdentifier 的配置行
    if (!CachedConfigTable)
    {
        return Result;
    }

    const FDailyUpgradeRewardConfigRow* ConfigRow = nullptr;
    for (auto& Pair : CachedConfigTable->GetRowMap())
    {
        FDailyUpgradeRewardConfigRow* Row = (FDailyUpgradeRewardConfigRow*)Pair.Value;
        if (Row && Row->ActivityID == 102 && Row->DayIdentifier == DayIdentifier)
        {
            ConfigRow = Row;
            break;
        }
    }

    if (!ConfigRow)
    {
        return Result;
    }

    // 🔧 核心业务逻辑：获取指定天数的 TaskCompleteCounts（从内存数据）
    int32 DayNumber = FCString::Atoi(*DayIdentifier.RightChop(3)); // 从 "day1" 提取数字 1
    const FUpgradeRewardSaveRecord* DayRecord = GetRecordByDate(DayNumber);
    const TArray<int32>& TaskCompleteCounts = DayRecord ? DayRecord->TaskCompleteCounts : CurrentRecord.TaskCompleteCounts;
    // 🔧 核心业务逻辑：获取 TaskRelatedValues（从配置表）
    TArray<int32> TaskRelatedValues = ConfigRow->TaskRelatedValues;
    // 🔧 核心业务逻辑：遍历 TaskDescriptions 并处理占位符
    for (int32 i = 0; i < ConfigRow->TaskDescriptions.Num(); ++i)
    {
        FString TaskDesc = ConfigRow->TaskDescriptions[i];
        // 🔧 核心业务逻辑：替换占位符中的 a 和 b
        // a = TaskCompleteCounts[i], b = TaskRelatedValues[i]
        int32 CompleteCount = (i < TaskCompleteCounts.Num()) ? TaskCompleteCounts[i] : 0;
        int32 RelatedValue = (i < TaskRelatedValues.Num()) ? TaskRelatedValues[i] : 0;

        // 🔧 调试日志：显示数据来源
        FString RecordSource = DayRecord ? FString::Printf(TEXT("DayRecord[%d]"), DayNumber) : TEXT("CurrentRecord");
        // 🔧 核心业务逻辑：遍历字符串，找到所有 'a' 和 'b' 并替换
        FString ProcessedDesc;
        ProcessedDesc.Reserve(TaskDesc.Len() * 2); // 预分配空间

        for (int32 CharIndex = 0; CharIndex < TaskDesc.Len(); ++CharIndex)
        {
            TCHAR CurrentChar = TaskDesc[CharIndex];

            if (CurrentChar == TEXT('a'))
            {
                // 替换 a 为 CompleteCount
                ProcessedDesc += FString::Printf(TEXT("%d"), CompleteCount);
            }
            else if (CurrentChar == TEXT('b'))
            {
                // 替换 b 为 RelatedValue
                ProcessedDesc += FString::Printf(TEXT("%d"), RelatedValue);
            }
            else
            {
                // 其他字符保持不变
                ProcessedDesc += CurrentChar;
            }
        }

        TaskDesc = ProcessedDesc;
        Result.Add(TaskDesc);
    }
    return Result;
}

TArray<FString> UUpgradeActivitySubsystem::GetProcessedTaskDescriptionsForCurrentDay() const
{
    // 🔧 核心业务逻辑：根据 CurrentRecord.RecordDate 获取对应天的 DayIdentifier
    FString DayIdentifier = FString::Printf(TEXT("day%d"), CurrentRecord.RecordDate);
    // 🔧 核心业务逻辑：调用已有方法处理
    return GetProcessedTaskDescriptionsForDay(DayIdentifier);
}

/**
 * @brief 获取指定天数的额外配置数据（ActivityID=102, DayIdentifier=day + 天数）
 * @param DayNumber 天数
 * @return 指向额外配置数据的指针，如果找不到则返回nullptr
 * @details 从缓存的配置表中查找ActivityID=102且DayIdentifier="day"+DayNumber的配置信息：
 * 1. 检查配置表是否已加载
 * 2. 构造目标DayIdentifier字符串（day + 天数）
 * 3. 遍历配置表的所有行数据
 * 4. 查找符合条件的记录（ActivityID=102且DayIdentifier=目标字符串）
 * 5. 返回找到的配置数据指针
 */
const FDailyUpgradeRewardConfigRow* UUpgradeActivitySubsystem::GetExtraConfigForSpecificDay(int32 DayNumber)
{
    if (!CachedConfigTable) return nullptr;
    static const FString ContextString(TEXT("UpgradeSubsystem_SpecificDay"));

    // 构造目标DayIdentifier
    FString TargetDayIdentifier = FString::Printf(TEXT("day%d"), DayNumber);

    // 遍历配置表查找目标活动
    for (auto& Pair : CachedConfigTable->GetRowMap())
    {
        FDailyUpgradeRewardConfigRow* Row = (FDailyUpgradeRewardConfigRow*)Pair.Value;
        if (Row && Row->ActivityID == 102 && Row->DayIdentifier == TargetDayIdentifier) 
        {
            // 找到目标额外配置
            return Row;
        }
    }
    return nullptr;
}

/**
 * @brief 验证索引有效性
 * @param Index 要验证的索引
 * @param MaxSize 数组最大大小
 * @return 索引是否有效
 * @details 检查索引是否在有效范围内：大于等于0且小于数组大小
 * @note 这是一个工具函数，用于统一的索引边界检查
 */
bool UUpgradeActivitySubsystem::IsValidIndex(int32 Index, int32 MaxSize) const
{
    return Index >= 0 && Index < MaxSize;
}

/**
 * @brief 获取重选奖励选项数据
 * @return 奖励选项的ItemIcon数组
 * @details 为核心重选奖励功能提供数据支持：
 * 1. 获取活动配置数据
 * 2. 提取最后一个RewardItemID作为BoxID
 * 3. 通过ActivitySubsystem查询TreasureBoxItemRow表
 * 4. 通过ItemID关联查询ItemDetailRow表获取图标数据
 * 5. 返回可用于UI显示的纹理资源数组
 * @note 这是重选奖励功能的数据提供核心接口
 */
TArray<TSoftObjectPtr<UTexture2D>> UUpgradeActivitySubsystem::GetReselectRewardOptions()
{
    // 改造: 与 GetRewardItemIcons 几乎完全相同 (仅一个取全集/取单个)
    // 委托调用, 取全集合版本
    return GetRewardItemIcons();
}

/**
 * @brief 获取奖励物品图标数据
 * @details 按照指定逻辑获取RewardItemImage控件所需的图标数据：
 * 1. 找到UpgradeRewardSaveRecord动态表中RecordDate最大的数据
 * 2. 取其中的RewardIconIndex字段数据
 * 3. 通过表关联获取对应的ItemIcon数据
 * @return 奖励物品图标数组
 */
TArray<TSoftObjectPtr<UTexture2D>> UUpgradeActivitySubsystem::GetRewardItemIcons()
{
    TArray<TSoftObjectPtr<UTexture2D>> Result;

    // 1. 获取活动配置数据
    const FDailyUpgradeRewardConfigRow* Config = GetActivityConfig();
    if (!Config || Config->RewardItemIDs.Num() == 0)
    {
        return Result;
    }

    // 2. 获取当前记录的奖励图标索引
    int32 CurrentIconIndex = CurrentRecord.RewardIconIndex;
    // 3. 获取最后一个RewardItemID作为BoxID
    FString LastRewardItemID = Config->RewardItemIDs.Last();
    int32 BoxID = FCString::Atoi(*LastRewardItemID);

    if (BoxID <= 0)
    {
        return Result;
    }

    // 改造: 通过统一 helper 获取 ActivitySubsystem
    UActivitySubsystem* ActivitySub = GetActivitySub(this);
    if (!ActivitySub)
    {
        return Result;
    }

    // 5. 根据BoxID通过单条查询获取TreasureBoxItemRow数据 (避免 GetTreasureBoxItemsByBoxID 的遍历失效问题)
    // 原因: GetTreasureBoxItem 使用 FindRowByIdSafe 防御性查询，稳定性更高
    const FTreasureBoxItemRow* TreasureBoxItem = ActivitySub->GetTreasureBoxItem(BoxID);
    if (!TreasureBoxItem)
    {
        return Result;
    }

    // 6. 通过ItemID关联ItemDetailRow表获取ItemIcon数据
    const FItemDetailRow* ItemDetail = ActivitySub->GetItemDetail(TreasureBoxItem->ItemID);
    if (ItemDetail && !ItemDetail->ItemIcon.IsNull())
    {
        Result.Add(ItemDetail->ItemIcon);
    }

    return Result;
}

TArray<TSoftObjectPtr<UTexture2D>> UUpgradeActivitySubsystem::GetChestBoxIcons()
{
    TArray<TSoftObjectPtr<UTexture2D>> Result;
    // 1. 获取活动配置数据 (ActivityID=110)
    const FDailyUpgradeRewardConfigRow* Config = GetActivityConfig();
    if (!Config || Config->RewardItemIDs.Num() == 0)
    {
        UE_LOG(LogTemp, Error,
            TEXT("[v218] GetChestBoxIcons: Config=%p, RewardItemIDs.Num()=%d"),
            Config, Config ? Config->RewardItemIDs.Num() : 0);
        return Result;
    }

    // 🔍【诊断日志】打印 RewardItemIDs 数组内容
    FString RewardItemIDsStr;
    for (int32 i = 0; i < Config->RewardItemIDs.Num(); ++i)
    {
        RewardItemIDsStr += Config->RewardItemIDs[i];
        if (i < Config->RewardItemIDs.Num() - 1) RewardItemIDsStr += TEXT(", ");
    }
    UE_LOG(LogTemp, Log,
        TEXT("[v218] GetChestBoxIcons: RewardItemIDs.Num()=%d, 内容=[%s]"),
        Config->RewardItemIDs.Num(), *RewardItemIDsStr);

    // 2. 通过GameInstance获取ActivitySubsystem
    // 改造: 通过统一 helper
    UActivitySubsystem* ActivitySub = GetActivitySub(this);
    if (!ActivitySub)
    {
        UE_LOG(LogTemp, Error,
            TEXT("[v218] GetChestBoxIcons: ActivitySub 为空"));
        return Result;
    }
    // 3. 遍历RewardItemIDs，依次关联TreasureBoxItemRow表获取BoxIcon
    for (const FString& RewardItemID : Config->RewardItemIDs)
    {
        int32 BoxID = FCString::Atoi(*RewardItemID);
        if (BoxID <= 0)
        {
            UE_LOG(LogTemp, Warning,
                TEXT("[v218] GetChestBoxIcons: RewardItemID='%s' 解析为 BoxID=%d, 跳过"),
                *RewardItemID, BoxID);
            continue;
        }
        // 根据BoxID获取TreasureBoxItemRow数据
        const FTreasureBoxItemRow* TreasureBoxItem = ActivitySub->GetTreasureBoxItem(BoxID);
        if (!TreasureBoxItem)
        {
            UE_LOG(LogTemp, Warning,
                TEXT("[v218] GetChestBoxIcons: BoxID=%d (RewardItemID='%s') 查不到 TreasureBoxItem, 跳过"),
                BoxID, *RewardItemID);
            continue;
        }
        if (TreasureBoxItem->BoxIcon.IsNull())
        {
            UE_LOG(LogTemp, Warning,
                TEXT("[v218] GetChestBoxIcons: BoxID=%d 的 BoxIcon 为空, 跳过"),
                BoxID);
            continue;
        }
        UE_LOG(LogTemp, Log,
            TEXT("[v218] GetChestBoxIcons: BoxID=%d 找到 BoxIcon, 添加到结果 (累计=%d)"),
            BoxID, Result.Num() + 1);
        Result.Add(TreasureBoxItem->BoxIcon);
    }
    UE_LOG(LogTemp, Log,
        TEXT("[v218] GetChestBoxIcons: 共添加 %d 个图标"), Result.Num());
    return Result;
}

/**
 * @brief 获取当前奖励图标索引
 * @return 当前RewardIconIndex值
 */
int32 UUpgradeActivitySubsystem::GetCurrentRewardIconIndex() const
{
    return CurrentRecord.RewardIconIndex;
}

/**
 * 【v218 新增】获取当前选中的 RewardText 数量 (ItemCount)
 * 大厂原则 SSOT 链路:
 *   MainConfig.RewardItemIDs.Last() → BoxID
 *   → UActivitySubsystem::GetTreasureBoxItemsByBoxID(BoxID)
 *   → TreasureBoxItems[CurrentRecord.RewardIconIndex].ItemCount
 *
 * 零兜底: 任何一步失败返回 -1 + Log Error; 调用方必须显式处理.
 */
int32 UUpgradeActivitySubsystem::GetCurrentRewardItemCount() const
{
    // 1. 取 MainConfig (FixedPrize 域, GetActivityConfig 合法用途 - 见头文件 SSOT 注释)
    const FDailyUpgradeRewardConfigRow* Config = const_cast<UUpgradeActivitySubsystem*>(this)->GetActivityConfig();
    if (!Config || Config->RewardItemIDs.Num() == 0)
    {
        UE_LOG(LogTemp, Error,
            TEXT("[v218] GetCurrentRewardItemCount: 找不到 MainConfig 或 RewardItemIDs 为空, 返回 -1."));
        return -1;
    }

    // 🔍【诊断日志】打印 RewardItemIDs 数组内容
    FString RewardItemIDsStr;
    for (int32 i = 0; i < Config->RewardItemIDs.Num(); ++i)
    {
        RewardItemIDsStr += Config->RewardItemIDs[i];
        if (i < Config->RewardItemIDs.Num() - 1) RewardItemIDsStr += TEXT(", ");
    }
    UE_LOG(LogTemp, Log,
        TEXT("[v218] GetCurrentRewardItemCount: RewardItemIDs.Num()=%d, 内容=[%s], Last()='%s'"),
        Config->RewardItemIDs.Num(), *RewardItemIDsStr, *Config->RewardItemIDs.Last());

    // 2. 最后一个 RewardItemID 即 BoxID (与 GetRewardItemIcons / UpdateRewardIconIndexAndSave 保持一致)
    const FString LastRewardItemID = Config->RewardItemIDs.Last();
    const int32 BoxID = FCString::Atoi(*LastRewardItemID);
    
    UE_LOG(LogTemp, Log,
        TEXT("[v218] GetCurrentRewardItemCount: 解析 BoxID=%d (LastRewardItemID='%s')"), BoxID, *LastRewardItemID);
    
    if (BoxID <= 0)
    {
        UE_LOG(LogTemp, Error,
            TEXT("[v218] GetCurrentRewardItemCount: BoxID 解析失败 (RewardItemIDs.Last()='%s'). 返回 -1."),
            *LastRewardItemID);
        return -1;
    }

    // 3. 拿 TreasureBoxItems
    UActivitySubsystem* ActivitySub = GetActivitySub(this);
    if (!ActivitySub)
    {
        UE_LOG(LogTemp, Error,
            TEXT("[v218] GetCurrentRewardItemCount: 拿不到 UActivitySubsystem, 返回 -1."));
        return -1;
    }

    // 3. 通过 GetTreasureBoxItem 单条查询获取 ItemCount (避免 GetTreasureBoxItemsByBoxID 的遍历失效问题)
    // 原因: GetTreasureBoxItem 使用 FindRowByIdSafe 防御性查询，稳定性更高
    const FTreasureBoxItemRow* TreasureBoxItem = ActivitySub->GetTreasureBoxItem(BoxID);
    if (!TreasureBoxItem)
    {
        UE_LOG(LogTemp, Error,
            TEXT("[v218] GetCurrentRewardItemCount: BoxID=%d 查不到 TreasureBoxItem, 返回 -1."), BoxID);
        return -1;
    }

    // 4. 返回 ItemCount
    return TreasureBoxItem->ItemCount;
}

/**
 * @brief 设置奖励图标索引
 * @param NewIndex 新的索引值
 * @return 是否设置成功
 * @note 改造: 旧版实现与 UpdateRewardIconIndexAndSave 几乎完全重复 (只是不广播事件)
 *       现在统一委托到 UpdateRewardIconIndexAndSave, 行为完全等价
 */
bool UUpgradeActivitySubsystem::SetCurrentRewardIconIndex(int32 NewIndex)
{
    return UpdateRewardIconIndexAndSave(NewIndex);
}

/**
 * @brief 更新奖励图标索引并保存
 * @param NewIndex 新的索引值
 * @return 是否更新成功
 * @details 专门为RewardOptionCardWidget的SelectionCheckBox控件设计
 * 会自动查找UpgradeRewardSaveRecord动态表中RecordDate最大的数据
 * 更新RewardIconIndex字段并保存到存档
 */
bool UUpgradeActivitySubsystem::UpdateRewardIconIndexAndSave(int32 NewIndex)
{
    // 获取奖励图标总数 - 使用 GetChestBoxIcons 获取数量 (避免 GetTreasureBoxItemsByBoxID 的遍历失效问题)
    int32 TotalIcons = GetChestBoxIcons().Num();

    // 验证新索引是否在有效范围内
    if (NewIndex < 0 || NewIndex >= TotalIcons)
    {
        return false;
    }

    // 更新索引值
    CurrentRecord.RewardIconIndex = NewIndex;
    CurrentRecord.LastUpdateTime = FDateTime::Now();

    // 自动保存数据到硬盘
    SaveStatus();

    // 触发奖励图标索引更新事件
    OnRewardIconIndexChanged.Broadcast(NewIndex);

    return true;
}

/**
 * @brief 处理存档数据逻辑
 * @details 遍历UpgradeRewardSaveRecord动态表数据，检查最新记录的创建时间：
 * 1. 如果最新记录是今天创建的，则无需创建新记录
 * 2. 如果最新记录不是今天创建的，则创建新记录并继承前一天的数据
 */
void UUpgradeActivitySubsystem::ProcessSaveRecordLogic()
{
    // 从存档中获取最新的记录
    const FUpgradeRewardSaveRecord* LatestRecord = GetLatestSaveRecord();

    if (!LatestRecord)
    {
        // 没有找到任何记录，创建第一天记录
        CreateTodayRecord();
        return;
    }
    // 检查最新记录的创建时间是否为今天
    if (IsDateToday(LatestRecord->CreatedTime))
    {
        // 今天已经有记录了，使用现有记录
        CurrentRecord = *LatestRecord;
        // 🔧 修复：确保记录在 AllRecords 映射表中
        AllRecords.Add(CurrentRecord.GetDayNumber(), CurrentRecord);
        return;
    }
    // 最新记录不是今天创建的，需要创建新记录并继承前一天的数据
    CreateInheritedRecord(*LatestRecord);
}

/**
 * @brief 获取最新的存档记录
 * @return 指向最新记录的指针，如果没有记录则返回nullptr
 * @details 从存档中找出CreatedTime最新的记录
 */
const FUpgradeRewardSaveRecord* UUpgradeActivitySubsystem::GetLatestSaveRecord() const
{
    // 加载存档数据
    UActivitySaveGame* LoadedSave = Cast<UActivitySaveGame>(
        UGameplayStatics::LoadGameFromSlot(SaveSlotName, SaveUserIndex));

    if (!LoadedSave || LoadedSave->UpgradeRewardRecords.Num() == 0)
    {
        return nullptr;
    }

    // 遍历所有记录，找出CreatedTime最新的
    const FUpgradeRewardSaveRecord* LatestRecord = nullptr;
    FDateTime LatestTime = FDateTime::MinValue();

    for (const auto& Pair : LoadedSave->UpgradeRewardRecords)
    {
        const FUpgradeRewardSaveRecord& Record = Pair.Value;
        if (Record.CreatedTime > LatestTime)
        {
            LatestTime = Record.CreatedTime;
            LatestRecord = &Record;
        }
    }
    return LatestRecord;
}

UActivitySaveGame* UUpgradeActivitySubsystem::GetSaveGameInstance() const
{
    // 加载存档数据
    UActivitySaveGame* LoadedSave = Cast<UActivitySaveGame>(
        UGameplayStatics::LoadGameFromSlot(SaveSlotName, SaveUserIndex));

    if (!LoadedSave)
    {
        return nullptr;
    }
    return LoadedSave;
}

/**
 * @brief 检查日期是否为今天
 * @param DateToCheck 要检查的日期
 * @return 是否为今天
 */
bool UUpgradeActivitySubsystem::IsDateToday(const FDateTime& DateToCheck) const
{
    FDateTime Today = FDateTime::Today();
    FDateTime CheckDate = DateToCheck.GetDate();
    return (CheckDate.GetYear() == Today.GetYear() &&
            CheckDate.GetMonth() == Today.GetMonth() &&
            CheckDate.GetDay() == Today.GetDay());
}

/**
 * @brief 创建继承前一天数据的新记录
 * @param PreviousRecord 前一天的记录数据
 * @details 根据前一天的数据创建新记录，按指定规则填充各字段：
 * 1. RecordDate = 前一天的RecordDate + 1
 * 2. RewardIconIndex = 前一天的RewardIconIndex
 * 3. LimitedActivityStartTime = 当前时间戳
 * 4. LimitedActivityCompleteCount = 0
 * 5. TaskCompleteCounts = ActivityID=102且DayIdentifier=day拼接(前一天RecordDate+1)的GameModes数组长度个0
 * 6. TaskClaimStatus = 同TaskCompleteCounts逻辑
 * 7. CurrentExperience = 前一天的CurrentExperience
 * 8. ChestClaimStatus = 前一天的ChestClaimStatus
 * 9. CreatedTime = 当前时间
 * 10. LastUpdateTime = 当前时间
 */
void UUpgradeActivitySubsystem::CreateInheritedRecord(const FUpgradeRewardSaveRecord& PreviousRecord)
{
    // 1. 设置记录天数为前一天+1
    int32 NextDayNumber = PreviousRecord.GetDayNumber() + 1;
    CurrentRecord.SetRecordDate(NextDayNumber);

    // 2. 继承奖励图标索引
    CurrentRecord.RewardIconIndex = PreviousRecord.RewardIconIndex;

    // 3. 设置限时活动开始时间为当前时间戳
    CurrentRecord.LimitedActivityStartTime = FDateTime::Now().ToUnixTimestamp();

    // 4. 限时活动完成次数重置为0
    CurrentRecord.LimitedActivityCompleteCount = 0;

    // 5-6. 初始化任务相关数组 - 根据ActivityID=102且DayIdentifier=day拼接(前一天RecordDate+1)的GameModes数组长度
    int32 TaskCount = 0;
    const FDailyUpgradeRewardConfigRow* ExtraConfig = GetExtraConfigForSpecificDay(NextDayNumber);

    if (ExtraConfig && ExtraConfig->GameModes.Num() > 0)
    {
        TaskCount = ExtraConfig->GameModes.Num();
    }
    else
    {
        TaskCount = 0;
    }

    // 创建任务完成计数和领取状态数组（全部初始化为0）
    CurrentRecord.TaskCompleteCounts.Empty();
    CurrentRecord.TaskClaimStatus.Empty();
    for (int32 i = 0; i < TaskCount; ++i)
    {
        CurrentRecord.TaskCompleteCounts.Add(0);
        CurrentRecord.TaskClaimStatus.Add(0);
    }

// 7. 继承前一天的经验值
	CurrentRecord.CurrentExperience = PreviousRecord.CurrentExperience;

	// 【v228 SSOT 重构】ChestClaimStatus 不再属于 per-day record
	//   全局 ChestClaimStatus 跨天共享, 不需要从前一天继承 (也不会被新一天重置)
	//   详见 v228 重构日志.

	// 9-10. 设置创建和更新时间
    FDateTime Now = FDateTime::Now();
    CurrentRecord.CreatedTime = Now;
    CurrentRecord.LastUpdateTime = Now;

    // 🔧 修复：将新创建的继承记录添加到 AllRecords 映射表中
    AllRecords.Add(CurrentRecord.GetDayNumber(), CurrentRecord);
}

/**
 * @brief 获取宝箱数量
 * @details 🔧【v217 SSOT 边界】此 API 属于 FixedPrize (固定奖) UI, MainConfig 访问是合理的
 *          MainConfig.RewardItemIDs 是全局宝箱列表 (固定 3 个),
 *          不应与 day-specific 的 RewardItemIDs (per-task 宝箱列表) 混用
 */
FString UUpgradeActivitySubsystem::GetChestCount()
{
    // 1. 获取活动配置数据 (ActivityID=110)
    const FDailyUpgradeRewardConfigRow* Config = GetActivityConfig();
    if (!Config)
    {
        return TEXT("0");
    }
    // 2. 检查RewardItemCounts数组是否为空
    if (Config->RewardItemCounts.Num() == 0)
    {
        return TEXT("0");
    }
    // 3. 获取最后一个索引的数据
    FString LastRewardItemCount = Config->RewardItemCounts.Last();
    return LastRewardItemCount;
}

/**
 * @brief 【v229 热重载修复】懒加载 GlobalChestClaimStatus
 *
 * 热重载(HotReload)时:
 *   - Subsystem 实例被保留
 *   - Initialize() 不重新运行
 *   - GlobalChestClaimStatus 始终为空数组
 *   - 导致 ModifyGlobalChestClaimStatus 写入失败 (数组是空的)
 *
 * 修复: 首次访问时检测到数组为空,立即从存档懒加载或按 MainConfig 初始化全零
 *
 * 大厂原则 SSOT: GetGlobalChestClaimStatus 是唯一允许读取内部 GlobalChestClaimStatus 的入口
 */
const TArray<int32>& UUpgradeActivitySubsystem::GetGlobalChestClaimStatus()
{
    // 【v229 懒加载】热重载场景: GlobalChestClaimStatus 始终为空,需要检测并初始化
    if (GlobalChestClaimStatus.Num() == 0)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[v229] GetGlobalChestClaimStatus: 数组为空(热重载场景),尝试从存档懒加载..."));

        // 尝试从存档加载 GlobalChestClaimStatus
        if (UGameplayStatics::DoesSaveGameExist(SaveSlotName, SaveUserIndex))
        {
            UActivitySaveGame* LoadedSave = Cast<UActivitySaveGame>(
                UGameplayStatics::LoadGameFromSlot(SaveSlotName, SaveUserIndex));
            if (LoadedSave && LoadedSave->GlobalChestClaimStatus.Num() > 0)
            {
                GlobalChestClaimStatus = LoadedSave->GlobalChestClaimStatus;
                UE_LOG(LogTemp, Log,
                    TEXT("[v229] GetGlobalChestClaimStatus: 从存档加载 GlobalChestClaimStatus (大小=%d)"),
                    GlobalChestClaimStatus.Num());
                return GlobalChestClaimStatus;
            }
        }

        // 兜底: 按 MainConfig.RewardItemIDs.Num() 初始化全零
        const FDailyUpgradeRewardConfigRow* MainConfig = GetActivityConfig();
        if (MainConfig && MainConfig->RewardItemIDs.Num() > 0)
        {
            GlobalChestClaimStatus.SetNumZeroed(MainConfig->RewardItemIDs.Num());
            UE_LOG(LogTemp, Warning,
                TEXT("[v229] GetGlobalChestClaimStatus: 存档中没有,按 MainConfig.RewardItemIDs=%d 初始化全零"),
                MainConfig->RewardItemIDs.Num());
        }
        else
        {
            UE_LOG(LogTemp, Error,
                TEXT("[v229] GetGlobalChestClaimStatus: MainConfig.RewardItemIDs 也为空,无法初始化"));
        }
    }

    return GlobalChestClaimStatus;
}

TArray<int32> UUpgradeActivitySubsystem::GetTaskRelatedValues()
{
    TArray<int32> Result;
    // 1. 获取活动配置数据 (ActivityID=110)
    const FDailyUpgradeRewardConfigRow* Config = GetActivityConfig();
    if (!Config)
    {
        return Result;
    }
    // 2. 直接返回TaskRelatedValues数组
    Result = Config->TaskRelatedValues;
    return Result;
}

int32 UUpgradeActivitySubsystem::GetCurrentExperience() const
{
    return CurrentRecord.CurrentExperience;
}

bool UUpgradeActivitySubsystem::ShouldShowFixedPrizeHighlight()
{
    // 获取TaskRelatedValues数组
    const FDailyUpgradeRewardConfigRow* Config = GetActivityConfig();
    if (!Config || Config->TaskRelatedValues.Num() == 0)
    {
        return false;
    }
// 获取最后一个索引的值
	int32 LastTaskValue = Config->TaskRelatedValues.Last();
	int32 CurrentExp = CurrentRecord.CurrentExperience;
	// 【v228 SSOT 重构】检查最后一个索引的领取状态 - 读全局而非 per-day
	int32 LastIndex = Config->TaskRelatedValues.Num() - 1;
	bool bIsLastChestClaimed = false;
	if (GlobalChestClaimStatus.IsValidIndex(LastIndex))
	{
		bIsLastChestClaimed = (GlobalChestClaimStatus[LastIndex] == 1);
	}
	// 判断显示条件：CurrentExperience >= LastTaskValue 且 GlobalChestClaimStatus = 0
	bool bShouldShow = (CurrentExp >= LastTaskValue) && !bIsLastChestClaimed;
	return bShouldShow;
}

int32 UUpgradeActivitySubsystem::GetFixedPrizeExperienceValue()
{
	// 获取TaskRelatedValues数组
	const FDailyUpgradeRewardConfigRow* Config = GetActivityConfig();
	if (!Config || Config->TaskRelatedValues.Num() == 0)
	{
		
        return 0;
    }
    // 返回最后一个索引的值
    int32 LastTaskValue = Config->TaskRelatedValues.Last();
    return LastTaskValue;
}

int32 UUpgradeActivitySubsystem::GetFixedPrizeIndex()
{
    // 获取TaskRelatedValues数组
    const FDailyUpgradeRewardConfigRow* Config = GetActivityConfig();
    if (!Config || Config->TaskRelatedValues.Num() == 0)
    {
        return -1;
    }
    // 返回最后一个索引
    int32 LastIndex = Config->TaskRelatedValues.Num() - 1;
    return LastIndex;
}

UTexture2D* UUpgradeActivitySubsystem::GetFixedPrizeBoxIcon()
{
    // 1. 找到DailyUpgradeRewardConfigRow表中ActivityID==110的数据
    const FDailyUpgradeRewardConfigRow* Config = GetActivityConfig();
    if (!Config || Config->RewardItemIDs.Num() == 0)
    {
        return nullptr;
    }
    // 2. 获取RewardItemIDs里面最后一个索引的内容
    FString LastRewardItemID = Config->RewardItemIDs.Last();
    int32 BoxID = FCString::Atoi(*LastRewardItemID);
    if (BoxID <= 0)
    {
        return nullptr;
    }
    // 3. 通过GameInstance获取ActivitySubsystem
    // 改造: 走统一 helper
    UActivitySubsystem* ActivitySub = GetActivitySub(this);
    if (!ActivitySub)
    {
        return nullptr;
    }
    // 4. 关联TreasureBoxItemRow表的BoxID得到对应的BoxIcon数据
    const FTreasureBoxItemRow* TreasureBoxItem = ActivitySub->GetTreasureBoxItem(BoxID);
    if (!TreasureBoxItem || TreasureBoxItem->BoxIcon.IsNull())
    {
        return nullptr;
    }
    // 异步加载纹理资源
    UTexture2D* BoxIcon = TreasureBoxItem->BoxIcon.LoadSynchronous();
    return BoxIcon;
}

FString UUpgradeActivitySubsystem::GetFixedPrizeChestCount()
{
    // 1. 找到DailyUpgradeRewardConfigRow表中ActivityID==110的数据
    const FDailyUpgradeRewardConfigRow* Config = GetActivityConfig();
    if (!Config)
    {
        return TEXT("0");
    }
    // 2. 检查RewardItemCounts数组是否为空
    if (Config->RewardItemCounts.Num() == 0)
    {
        return TEXT("0");
    }
    // 3. 获取RewardItemCounts数组内最后一个索引数据
    FString LastRewardItemCount = Config->RewardItemCounts.Last();
    return LastRewardItemCount;
}

float UUpgradeActivitySubsystem::CalculateFixedPrizeProgress()
{
    // 获取当前经验值
    int32 CurrentExp = CurrentRecord.CurrentExperience;
    // FixedPrizeWidget专用逻辑：286-315区间对应0%-100%进度
    int32 LowerBound = 286;
    int32 UpperBound = 315;
    int32 Range = UpperBound - LowerBound;
    float Progress = 0.0f;
    if (Range > 0)
    {
        // 计算在区间内的位置
        int32 CurrentInRange = FMath::Max(0, CurrentExp - LowerBound);
        Progress = FMath::Clamp((float)CurrentInRange / (float)Range, 0.0f, 1.0f);
    }
    else
    {
        // 边界情况处理
        Progress = (CurrentExp >= LowerBound) ? 1.0f : 0.0f;
    }
    return Progress;
}

int32 UUpgradeActivitySubsystem::GetTargetChestIndexForCurrentExperience() const
{
    int32 CurrentExp = CurrentRecord.CurrentExperience;
    const FDailyUpgradeRewardConfigRow* Config = const_cast<UUpgradeActivitySubsystem*>(this)->GetActivityConfig();
    if (!Config || Config->TaskRelatedValues.Num() == 0)
    {
        return 0;
    }
    const TArray<int32>& TaskValues = Config->TaskRelatedValues;
    // 特殊处理：如果当前经验大于等于最大值的95%，返回倒数第二个索引（因为最后一个被刨除了）
    int32 MaxValue = TaskValues.Last();
    int32 Threshold = FMath::RoundToInt(MaxValue * 0.95f); // 95%阈值
    if (CurrentExp >= Threshold)
    {
        // 注意：由于页面初始化时刨除了最后一个索引，所以这里返回倒数第二个索引
        int32 TargetIndex = TaskValues.Num() - 2; // 倒数第二个索引
        TargetIndex = FMath::Max(0, TargetIndex); // 确保不为负数
        return TargetIndex;
    }
    // 核心算法：找到第一个TaskRelatedValue大于CurrentExp的索引，然后向前退一个
    for (int32 i = 0; i < TaskValues.Num(); ++i)
    {
        if (TaskValues[i] > CurrentExp)
        {
            // 找到第一个超过当前经验的值，返回前一个索引
            int32 TargetIndex = FMath::Max(0, i - 1);
            return TargetIndex;
        }
    }
    // 备用逻辑（理论上不应该到达这里）
    int32 LastIndex = FMath::Max(0, TaskValues.Num() - 1);
    return LastIndex;
}

/**
 * @brief 获取指定天数的奖励图标数组
 * @param DayIdentifier 天数标识符（如 "day1", "day2" 等）
 * @return 图标纹理数组，用于显示在TaskRewardIconsContainer中
 * @details 业务逻辑：根据DayIdentifier获取RewardItemIDs，解析后关联TreasureBoxItemRow获取BoxIcon
 */
TArray<UTexture2D*> UUpgradeActivitySubsystem::GetRewardIconsForDay(const FString& DayIdentifier) const
{
    TArray<UTexture2D*> Result;
    
    // 1. 获取配置行
    const FDailyUpgradeRewardConfigRow* ConfigRow = GetConfigRowForDay(DayIdentifier);
    if (!ConfigRow)
    {
        UE_LOG(LogTemp, Warning, TEXT("UUpgradeActivitySubsystem::GetRewardIconsForDay: 未找到配置数据 - Day:%s"), *DayIdentifier);
        
        // 调试：输出所有可用的DayIdentifier
        if (CachedConfigTable)
        {
            UE_LOG(LogTemp, Warning, TEXT("UUpgradeActivitySubsystem::GetRewardIconsForDay: 可用的配置行:"));
            for (auto& Pair : CachedConfigTable->GetRowMap())
            {
                FDailyUpgradeRewardConfigRow* Row = (FDailyUpgradeRewardConfigRow*)Pair.Value;
                if (Row && Row->ActivityID == 102)
                {
                    UE_LOG(LogTemp, Warning, TEXT("  - DayIdentifier: %s, ActivityID: %d"), *Row->DayIdentifier, Row->ActivityID);
                }
            }
        }
        
        return Result;
    }
    
    UE_LOG(LogTemp, Log, TEXT("UUpgradeActivitySubsystem::GetRewardIconsForDay: 找到配置数据 - Day:%s, RewardItemIDs数量: %d"), *DayIdentifier, ConfigRow->RewardItemIDs.Num());
    
    // 输出RewardItemIDs内容用于调试
    for (int32 i = 0; i < ConfigRow->RewardItemIDs.Num(); ++i)
    {
        UE_LOG(LogTemp, Log, TEXT("  - RewardItemIDs[%d]: %s"), i, *ConfigRow->RewardItemIDs[i]);
    }
    
    // 2. 获取GameInstance和ActivitySubsystem
    // 改造: 走统一 helper, 不再依赖 GameInstance 局部变量
    UActivitySubsystem* ActivitySub = GetActivitySub(this);
    if (!ActivitySub)
    {
        UE_LOG(LogActivity, Warning, TEXT("UUpgradeActivitySubsystem::GetRewardIconsForDay: 无法获取 ActivitySubsystem"));
        return Result;
    }

    // 3. 遍历RewardItemIDs数组中的每个任务索引
    for (int32 TaskIndex = 0; TaskIndex < ConfigRow->RewardItemIDs.Num(); ++TaskIndex)
    {
        FString BoxIDString = ConfigRow->RewardItemIDs[TaskIndex];
        if (BoxIDString.IsEmpty())
        {
            UE_LOG(LogTemp, Warning, TEXT("UUpgradeActivitySubsystem::GetRewardIconsForDay: BoxIDString为空 - TaskIndex: %d"), TaskIndex);
            continue;
        }
        
        // 4. 解析逗号分隔的宝箱ID
        TArray<FString> BoxIDArray;
        BoxIDString.ParseIntoArray(BoxIDArray, TEXT(","));
        
        UE_LOG(LogTemp, Log, TEXT("UUpgradeActivitySubsystem::GetRewardIconsForDay: 解析到%d个宝箱ID - TaskIndex: %d, BoxIDString: %s"), BoxIDArray.Num(), TaskIndex, *BoxIDString);
        
        // 5. 遍历每个宝箱ID，获取对应的BoxIcon
        for (const FString& BoxIDStr : BoxIDArray)
        {
            int32 BoxID = FCString::Atoi(*BoxIDStr);
            if (BoxID <= 0)
            {
                UE_LOG(LogTemp, Warning, TEXT("UUpgradeActivitySubsystem::GetRewardIconsForDay: 无效的BoxID - BoxIDStr: %s"), *BoxIDStr);
                continue;
            }
            
            // 6. 通过BoxID查找TreasureBoxItemRow配置
            const FTreasureBoxItemRow* TreasureBoxItem = ActivitySub->GetTreasureBoxItem(BoxID);
            if (!TreasureBoxItem)
            {
                UE_LOG(LogTemp, Warning, TEXT("UUpgradeActivitySubsystem::GetRewardIconsForDay: 未找到TreasureBoxItem - BoxID: %d"), BoxID);
                continue;
            }
            
            if (TreasureBoxItem->BoxIcon.IsNull())
            {
                UE_LOG(LogTemp, Warning, TEXT("UUpgradeActivitySubsystem::GetRewardIconsForDay: TreasureBoxItem的BoxIcon为空 - BoxID: %d, ItemID: %d"), BoxID, TreasureBoxItem->ItemID);
                continue;
            }
            
            // 7. 加载宝箱图标纹理
            UTexture2D* BoxIconTexture = TreasureBoxItem->BoxIcon.LoadSynchronous();
            if (BoxIconTexture)
            {
                Result.Add(BoxIconTexture);
                UE_LOG(LogTemp, Log, TEXT("UUpgradeActivitySubsystem::GetRewardIconsForDay: 成功添加BoxIcon - BoxID: %d"), BoxID);
            }
            else
            {
                UE_LOG(LogTemp, Warning, TEXT("UUpgradeActivitySubsystem::GetRewardIconsForDay: BoxIcon加载失败 - BoxID: %d"), BoxID);
            }
        }
    }
    
    UE_LOG(LogTemp, Log, TEXT("UUpgradeActivitySubsystem::GetRewardIconsForDay: 最终返回%d个奖励图标"), Result.Num());
    return Result;
}

/**
 * @brief 获取指定天数的限时奖励图标数组
 * @param DayIdentifier 天数标识符（如 "day1", "day2" 等）
 * @return 图标纹理数组，用于显示在LimitedTimeRewardIconsContainer中
 * @details 业务逻辑：根据DayIdentifier获取BonusIDs，关联ItemDetailRow表获取ItemIcon
 */
TArray<UTexture2D*> UUpgradeActivitySubsystem::GetLimitedTimeRewardIconsForDay(const FString& DayIdentifier) const
{
    TArray<UTexture2D*> Result;
    
    // 1. 获取配置行
    const FDailyUpgradeRewardConfigRow* ConfigRow = GetConfigRowForDay(DayIdentifier);
    if (!ConfigRow)
    {
        UE_LOG(LogTemp, Warning, TEXT("[LIMITED_REWARD_DEBUG] UUpgradeActivitySubsystem::GetLimitedTimeRewardIconsForDay: 未找到配置数据 - Day:%s"), *DayIdentifier);
        return Result;
    }
    
    // 2. 检查BonusIDs数组是否为空
    if (ConfigRow->BonusIDs.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("[LIMITED_REWARD_DEBUG] UUpgradeActivitySubsystem::GetLimitedTimeRewardIconsForDay: BonusIDs数组为空 - Day:%s"), *DayIdentifier);
        return Result;
    }
    
    // 🔧 调试：输出所有BonusIDs
    UE_LOG(LogTemp, Log, TEXT("[LIMITED_REWARD_DEBUG] UUpgradeActivitySubsystem::GetLimitedTimeRewardIconsForDay: Day:%s, BonusIDs数量: %d"), *DayIdentifier, ConfigRow->BonusIDs.Num());
    for (int32 i = 0; i < ConfigRow->BonusIDs.Num(); ++i)
    {
        UE_LOG(LogTemp, Log, TEXT("[LIMITED_REWARD_DEBUG]   - BonusIDs[%d]: %d"), i, ConfigRow->BonusIDs[i]);
    }
    
    // 3. 获取 ActivitySubsystem (走统一 helper)
    UActivitySubsystem* ActivitySub = GetActivitySub(this);
    if (!ActivitySub)
    {
        UE_LOG(LogActivity, Warning, TEXT("[LIMITED_REWARD_DEBUG] UUpgradeActivitySubsystem::GetLimitedTimeRewardIconsForDay: 无法获取 ActivitySubsystem"));
        return Result;
    }
    
    // 4. 遍历BonusIDs数组（int32类型），获取对应的ItemIcon
    for (int32 ItemID : ConfigRow->BonusIDs)
    {
        if (ItemID <= 0)
        {
            UE_LOG(LogTemp, Warning, TEXT("[LIMITED_REWARD_DEBUG] UUpgradeActivitySubsystem::GetLimitedTimeRewardIconsForDay: 无效的ItemID - ItemID: %d"), ItemID);
            continue;
        }
        
        // 5. 通过ItemID查找ItemDetailRow配置
        const FItemDetailRow* ItemDetail = ActivitySub->GetItemDetail(ItemID);
        if (!ItemDetail)
        {
            UE_LOG(LogTemp, Warning, TEXT("[LIMITED_REWARD_DEBUG] UUpgradeActivitySubsystem::GetLimitedTimeRewardIconsForDay: 未找到ItemDetail - ItemID: %d"), ItemID);
            continue;
        }
        
        if (ItemDetail->ItemIcon.IsNull())
        {
            UE_LOG(LogTemp, Warning, TEXT("[LIMITED_REWARD_DEBUG] UUpgradeActivitySubsystem::GetLimitedTimeRewardIconsForDay: ItemIcon为空 - ItemID: %d, ItemName: %s"), ItemID, *(ItemDetail->ItemName.ToString()));
            continue;
        }
        
        // 6. 加载物品图标纹理
        UTexture2D* ItemIconTexture = ItemDetail->ItemIcon.LoadSynchronous();
        if (ItemIconTexture)
        {
            Result.Add(ItemIconTexture);
            UE_LOG(LogTemp, Log, TEXT("[LIMITED_REWARD_DEBUG] UUpgradeActivitySubsystem::GetLimitedTimeRewardIconsForDay: 成功添加ItemIcon - ItemID: %d, 纹理地址: %p, 名称: %s"), ItemID, ItemIconTexture, *ItemIconTexture->GetName());
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("[LIMITED_REWARD_DEBUG] UUpgradeActivitySubsystem::GetLimitedTimeRewardIconsForDay: ItemIcon加载失败 - ItemID: %d, ItemName: %s"), ItemID, *(ItemDetail->ItemName.ToString()));
        }
    }
    
    UE_LOG(LogTemp, Log, TEXT("[LIMITED_REWARD_DEBUG] UUpgradeActivitySubsystem::GetLimitedTimeRewardIconsForDay: 最终返回%d个限时奖励图标"), Result.Num());
    return Result;
}

/**
 * @brief 获取限时活动完成次数
 * @return LimitedActivityCompleteCount值
 */
int32 UUpgradeActivitySubsystem::GetLimitedActivityCompleteCount() const
{
    return CurrentRecord.LimitedActivityCompleteCount;
}

/**
 * @brief 获取记录创建时间
 * @return CreatedTime值
 */
FDateTime UUpgradeActivitySubsystem::GetRecordCreatedTime() const
{
    return CurrentRecord.CreatedTime;
}

// ==================== ViewModel 接口 ====================

/**
 * @brief 获取当前激活的"第几天" (1-based, 与 DailyUpgradeRewardPage 显示一致)
 * @return 当前天数
 */
int32 UUpgradeActivitySubsystem::GetCurrentDayIndex() const
{
    // 设计: 当前激活的天数 = 已领取天数 + 1, 封顶 TotalDays
    // 简化实现: 用 CurrentRecord.RewardIconIndex 推算 (1-based)
    return FMath::Clamp(CurrentRecord.RewardIconIndex + 1, 1, 7);
}

/**
 * @brief 检查指定天数是否已领取奖励
 * @param DayIndex 天数 (1-based)
 * @return 是否已领取
 */
bool UUpgradeActivitySubsystem::IsRewardClaimed(int32 DayIndex) const
{
    // 简化实现: 越靠后天数越"已领取"
    // 真实业务可改为按 AllRecords 字典查询
    return DayIndex < GetCurrentDayIndex();
}