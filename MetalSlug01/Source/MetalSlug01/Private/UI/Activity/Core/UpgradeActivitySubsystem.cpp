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

#include "UI/Activity/Core/UpgradeActivitySubsystem.h"
#include "Engine/DataTable.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/Engine.h"
#include "UI/Activity/Core/ActivitySubsystem.h"
#include "Misc/DateTime.h"
#include "Tools/UpgradeActivitySaveModifier.h"

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

    // 1. 预加载配置表 - 提前加载活动配置数据到内存中，提高运行时性能
    CachedConfigTable = LoadObject<UDataTable>(nullptr, TEXT("/Game/UI/Activity/Data/DT_DailyUpgradeRewardConfigRow.DT_DailyUpgradeRewardConfigRow"));

    // 2. 检查并创建初始记录 - 确保系统有最新的记录
    if (UGameplayStatics::DoesSaveGameExist(SaveSlotName, SaveUserIndex))
    {
        UDailyLoginSaveGame* Loaded = Cast<UDailyLoginSaveGame>(UGameplayStatics::LoadGameFromSlot(SaveSlotName, SaveUserIndex));
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
        UDailyLoginSaveGame* LoadedSave = Cast<UDailyLoginSaveGame>(
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
        }
        else
        {
            // 存档损坏或不包含该活动数据
            CreateTodayRecord();
        }
    }
    else
    {
        // 没有找到存档文件
        CreateTodayRecord();
    }
}

/**
 * @brief 获取活动配置数据
 * @return 指向ActivityID=110的配置数据指针，如果找不到则返回nullptr
 * @details 从缓存的配置表中查找指定活动ID的配置信息：
 * 1. 首先检查配置表是否已加载
 * 2. 遍历配置表的所有行数据
 * 3. 查找ActivityID等于110的记录
 * 4. 返回找到的配置数据指针
 * @note 使用缓存机制避免重复加载，提高性能
 */
const FDailyUpgradeRewardConfigRow* UUpgradeActivitySubsystem::GetActivityConfig()
{
    if (!CachedConfigTable) return nullptr;
    static const FString ContextString(TEXT("UpgradeSubsystem"));

    // 封装重复的 RowMap 遍历逻辑 - 遍历配置表查找目标活动
    for (auto& Pair : CachedConfigTable->GetRowMap())
    {
        FDailyUpgradeRewardConfigRow* Row = (FDailyUpgradeRewardConfigRow*)Pair.Value;
        if (Row && Row->ActivityID == 110) 
        {
            // 找到目标活动配置
            return Row;
        }
    }
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
    // 验证宝箱是否可以领取
    if (CurrentRecord.ChestClaimStatus.IsValidIndex(ChestIndex) && CurrentRecord.ChestClaimStatus[ChestIndex] == 0)
    {
        CurrentRecord.ChestClaimStatus[ChestIndex] = 1; // 标记已领
        CurrentRecord.CurrentExperience += 20; // 经验值累加
        CurrentRecord.LastUpdateTime = FDateTime::Now();
        SaveStatus(); // 持久化保存
        return true;
    }
    return false;
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
    const FDailyUpgradeRewardConfigRow* Config = GetActivityConfig();
    if (!Config)
    {
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
    const FDailyUpgradeRewardConfigRow* Config = const_cast<UUpgradeActivitySubsystem*>(this)->GetActivityConfig();
    if (!Config)
        return false;

    // 检查索引有效性 - 确保宝箱索引在配置范围内
    if (!IsValidIndex(ChestIndex, Config->RewardItemIDs.Num()))
        return false;

    // 检查是否已领取 - 防止重复领取
    if (IsValidIndex(ChestIndex, CurrentRecord.ChestClaimStatus.Num()) && 
        CurrentRecord.ChestClaimStatus[ChestIndex] == 1)
        return false;

    // 检查任务完成情况 - 所有任务都必须完成才能领取宝箱
    for (int32 i = 0; i < CurrentRecord.TaskCompleteCounts.Num(); ++i)
    {
        if (CurrentRecord.TaskCompleteCounts[i] < 
            (Config->TaskRelatedValues.IsValidIndex(i) ? Config->TaskRelatedValues[i] : 0))
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
    const FDailyUpgradeRewardConfigRow* Config = const_cast<UUpgradeActivitySubsystem*>(this)->GetActivityConfig();
    if (!Config)
        return false;

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
    UDailyLoginSaveGame* SaveGame = Cast<UDailyLoginSaveGame>(UGameplayStatics::LoadGameFromSlot(SaveSlotName, SaveUserIndex));
    if (!SaveGame)
    {
        // 没有现有存档，创建新的存档对象
        SaveGame = NewObject<UDailyLoginSaveGame>();
    }

    // 更新或添加所有升级奖励记录 - 保存 AllRecords 中的所有数据
    SaveGame->UpgradeRewardRecords.Empty();
    for (const auto& Pair : AllRecords)
    {
        SaveGame->UpgradeRewardRecords.Add(Pair.Key, Pair.Value);
    }
    // 保存到磁盘
    UGameplayStatics::SaveGameToSlot(SaveGame, SaveSlotName, SaveUserIndex);
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
    // 获取主配置数据 (ActivityID=110)
    const FDailyUpgradeRewardConfigRow* MainConfig = GetActivityConfig();
    if (!MainConfig)
    {
        return;
    }

    // 获取额外配置数据 (ActivityID=102, DayIdentifier=day1)
    const FDailyUpgradeRewardConfigRow* ExtraConfig = GetExtraConfigForDay1();
    if (!ExtraConfig)
    {
    }

    // 初始化任务相关数组 - 根据ActivityID=102的GameModes数组长度创建
    CurrentRecord.TaskCompleteCounts.Empty();
    CurrentRecord.TaskClaimStatus.Empty();

    int32 TaskCount = 0;
    if (ExtraConfig && ExtraConfig->GameModes.Num() > 0)
    {
        // 使用ActivityID=102的GameModes数组长度
        TaskCount = ExtraConfig->GameModes.Num();
    }
    else if (MainConfig->TaskTypes.Num() > 0)
    {
        // 回退到主配置的TaskTypes长度
        TaskCount = MainConfig->TaskTypes.Num();
    }

    // 为每个任务创建对应的计数和状态记录（初始化为0）
    for (int32 i = 0; i < TaskCount; ++i)
    {
        CurrentRecord.TaskCompleteCounts.Add(0);
        CurrentRecord.TaskClaimStatus.Add(0);
    }

    // 初始化宝箱相关数组 - 根据ActivityID=110的RewardItemIDs数组长度创建
    CurrentRecord.ChestClaimStatus.Empty();
    int32 ChestCount = MainConfig->RewardItemIDs.Num();
    for (int32 i = 0; i < ChestCount; ++i)
    {
        CurrentRecord.ChestClaimStatus.Add(0);
    }

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
    return TEXT("/Game/UI/Activity/Data/DT_DailyUpgradeRewardConfigRow.DT_DailyUpgradeRewardConfigRow");
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
    UDailyLoginSaveGame* LoadedSave = Cast<UDailyLoginSaveGame>(
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
    TArray<TSoftObjectPtr<UTexture2D>> Result;

    const FDailyUpgradeRewardConfigRow* Config = GetActivityConfig();
    if (!Config || Config->RewardItemIDs.Num() == 0)
    {
        return Result;
    }

    // 获取最后一个RewardItemID - 根据需求规格使用最后一个奖励项
    FString LastRewardItemID = Config->RewardItemIDs.Last();
    int32 BoxID = FCString::Atoi(*LastRewardItemID);

    if (BoxID <= 0)
    {
        return Result;
    }

    // 通过GameInstance获取ActivitySubsystem - 实现子系统间的数据交互
    UGameInstance* GameInstance = GetGameInstance();
    if (!GameInstance)
    {
        return Result;
    }

    UActivitySubsystem* ActivitySub = GameInstance->GetSubsystem<UActivitySubsystem>();
    if (!ActivitySub)
    {
        return Result;
    }

    // 根据BoxID获取TreasureBoxItemRow数据 - 查询宝箱包含的物品
    TArray<const FTreasureBoxItemRow*> TreasureBoxItems = ActivitySub->GetTreasureBoxItemsByBoxID(BoxID);
    if (TreasureBoxItems.Num() == 0)
    {
        return Result;
    }

    // 通过ItemID关联ItemDetailRow表获取ItemIcon数据 - 获取物品的显示图标
    for (const FTreasureBoxItemRow* TreasureBoxItem : TreasureBoxItems)
    {
        if (TreasureBoxItem)
        {
            const FItemDetailRow* ItemDetail = ActivitySub->GetItemDetail(TreasureBoxItem->ItemID);
            if (ItemDetail && !ItemDetail->ItemIcon.IsNull())
            {
                Result.Add(ItemDetail->ItemIcon);

            }
        }
    }

    return Result;
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

    // 4. 通过GameInstance获取ActivitySubsystem
    UGameInstance* GameInstance = GetGameInstance();
    if (!GameInstance)
    {
        return Result;
    }

    UActivitySubsystem* ActivitySub = GameInstance->GetSubsystem<UActivitySubsystem>();
    if (!ActivitySub)
    {
        return Result;
    }

    // 5. 根据BoxID获取TreasureBoxItemRow数据
    TArray<const FTreasureBoxItemRow*> TreasureBoxItems = ActivitySub->GetTreasureBoxItemsByBoxID(BoxID);
    if (TreasureBoxItems.Num() == 0)
    {
        return Result;
    }

    // 6. 通过ItemID关联ItemDetailRow表获取ItemIcon数据
    for (const FTreasureBoxItemRow* TreasureBoxItem : TreasureBoxItems)
    {
        if (TreasureBoxItem)
        {
            const FItemDetailRow* ItemDetail = ActivitySub->GetItemDetail(TreasureBoxItem->ItemID);
            if (ItemDetail && !ItemDetail->ItemIcon.IsNull())
            {
                Result.Add(ItemDetail->ItemIcon);

            }
        }
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
        return Result;
    }
    // 2. 通过GameInstance获取ActivitySubsystem
    UGameInstance* GameInstance = GetGameInstance();
    if (!GameInstance)
    {
        return Result;
    }
    UActivitySubsystem* ActivitySub = GameInstance->GetSubsystem<UActivitySubsystem>();
    if (!ActivitySub)
    {
        return Result;
    }
    // 3. 遍历RewardItemIDs，依次关联TreasureBoxItemRow表获取BoxIcon
    for (const FString& RewardItemID : Config->RewardItemIDs)
    {
        int32 BoxID = FCString::Atoi(*RewardItemID);
        if (BoxID <= 0)
        {
            continue;
        }
        // 根据BoxID获取TreasureBoxItemRow数据
        const FTreasureBoxItemRow* TreasureBoxItem = ActivitySub->GetTreasureBoxItem(BoxID);
        if (TreasureBoxItem && !TreasureBoxItem->BoxIcon.IsNull())
        {
            Result.Add(TreasureBoxItem->BoxIcon);
        }
    }
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
 * @brief 设置奖励图标索引
 * @param NewIndex 新的索引值
 * @return 是否设置成功
 */
bool UUpgradeActivitySubsystem::SetCurrentRewardIconIndex(int32 NewIndex)
{
    // 验证索引有效性
    const FDailyUpgradeRewardConfigRow* Config = GetActivityConfig();
    if (!Config)
    {
        return false;
    }

    // 获取奖励图标总数
    int32 TotalIcons = 0;

    // 通过同样的数据关联逻辑计算图标总数
    if (Config->RewardItemIDs.Num() > 0)
    {
        FString LastRewardItemID = Config->RewardItemIDs.Last();
        int32 BoxID = FCString::Atoi(*LastRewardItemID);

        if (BoxID > 0)
        {
            UGameInstance* GameInstance = GetGameInstance();
            if (GameInstance)
            {
                UActivitySubsystem* ActivitySub = GameInstance->GetSubsystem<UActivitySubsystem>();
                if (ActivitySub)
                {
                    TArray<const FTreasureBoxItemRow*> TreasureBoxItems = ActivitySub->GetTreasureBoxItemsByBoxID(BoxID);
                    TotalIcons = TreasureBoxItems.Num();
                }
            }
        }
    }

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

    return true;
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
    // 验证索引有效性
    const FDailyUpgradeRewardConfigRow* Config = GetActivityConfig();
    if (!Config)
    {
        return false;
    }

    // 获取奖励图标总数
    int32 TotalIcons = 0;

    // 通过同样的数据关联逻辑计算图标总数
    if (Config->RewardItemIDs.Num() > 0)
    {
        FString LastRewardItemID = Config->RewardItemIDs.Last();
        int32 BoxID = FCString::Atoi(*LastRewardItemID);

        if (BoxID > 0)
        {
            UGameInstance* GameInstance = GetGameInstance();
            if (GameInstance)
            {
                UActivitySubsystem* ActivitySub = GameInstance->GetSubsystem<UActivitySubsystem>();
                if (ActivitySub)
                {
                    TArray<const FTreasureBoxItemRow*> TreasureBoxItems = ActivitySub->GetTreasureBoxItemsByBoxID(BoxID);
                    TotalIcons = TreasureBoxItems.Num();
                }
            }
        }
    }

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
    UDailyLoginSaveGame* LoadedSave = Cast<UDailyLoginSaveGame>(
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

UDailyLoginSaveGame* UUpgradeActivitySubsystem::GetSaveGameInstance() const
{
    // 加载存档数据
    UDailyLoginSaveGame* LoadedSave = Cast<UDailyLoginSaveGame>(
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

    // 8. 继承宝箱领取状态
    CurrentRecord.ChestClaimStatus = PreviousRecord.ChestClaimStatus;

    // 9-10. 设置创建和更新时间
    FDateTime Now = FDateTime::Now();
    CurrentRecord.CreatedTime = Now;
    CurrentRecord.LastUpdateTime = Now;

    // 🔧 修复：将新创建的继承记录添加到 AllRecords 映射表中
    AllRecords.Add(CurrentRecord.GetDayNumber(), CurrentRecord);
}

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
    // 检查最后一个索引的领取状态
    int32 LastIndex = Config->TaskRelatedValues.Num() - 1;
    bool bIsLastChestClaimed = false;
    if (CurrentRecord.ChestClaimStatus.IsValidIndex(LastIndex))
    {
        bIsLastChestClaimed = (CurrentRecord.ChestClaimStatus[LastIndex] == 1);
    }
    // 判断显示条件：CurrentExperience >= LastTaskValue 且 ChestClaimStatus = 0
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
    UGameInstance* GameInstance = GetGameInstance();
    if (!GameInstance)
    {
        return nullptr;
    }
    UActivitySubsystem* ActivitySub = GameInstance->GetSubsystem<UActivitySubsystem>();
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
    UGameInstance* GameInstance = GetGameInstance();
    if (!GameInstance)
    {
        UE_LOG(LogTemp, Error, TEXT("UUpgradeActivitySubsystem::GetRewardIconsForDay: 无法获取 GameInstance"));
        return Result;
    }
    
    UActivitySubsystem* ActivitySub = GameInstance->GetSubsystem<UActivitySubsystem>();
    if (!ActivitySub)
    {
        UE_LOG(LogTemp, Warning, TEXT("UUpgradeActivitySubsystem::GetRewardIconsForDay: 无法获取 ActivitySubsystem"));
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
        UE_LOG(LogTemp, Warning, TEXT("UUpgradeActivitySubsystem::GetLimitedTimeRewardIconsForDay: 未找到配置数据 - Day:%s"), *DayIdentifier);
        return Result;
    }
    
    // 2. 检查BonusIDs数组是否为空
    if (ConfigRow->BonusIDs.Num() == 0)
    {
        UE_LOG(LogTemp, Warning, TEXT("UUpgradeActivitySubsystem::GetLimitedTimeRewardIconsForDay: BonusIDs数组为空 - Day:%s"), *DayIdentifier);
        return Result;
    }
    
    // 3. 获取GameInstance和ActivitySubsystem
    UGameInstance* GameInstance = GetGameInstance();
    if (!GameInstance)
    {
        UE_LOG(LogTemp, Error, TEXT("UUpgradeActivitySubsystem::GetLimitedTimeRewardIconsForDay: 无法获取 GameInstance"));
        return Result;
    }
    
    UActivitySubsystem* ActivitySub = GameInstance->GetSubsystem<UActivitySubsystem>();
    if (!ActivitySub)
    {
        UE_LOG(LogTemp, Warning, TEXT("UUpgradeActivitySubsystem::GetLimitedTimeRewardIconsForDay: 无法获取 ActivitySubsystem"));
        return Result;
    }
    
    // 4. 遍历BonusIDs数组（int32类型），获取对应的ItemIcon
    for (int32 ItemID : ConfigRow->BonusIDs)
    {
        if (ItemID <= 0)
        {
            UE_LOG(LogTemp, Warning, TEXT("UUpgradeActivitySubsystem::GetLimitedTimeRewardIconsForDay: 无效的ItemID - ItemID: %d"), ItemID);
            continue;
        }
        
        // 5. 通过ItemID查找ItemDetailRow配置
        const FItemDetailRow* ItemDetail = ActivitySub->GetItemDetail(ItemID);
        if (!ItemDetail || ItemDetail->ItemIcon.IsNull())
        {
            UE_LOG(LogTemp, Warning, TEXT("UUpgradeActivitySubsystem::GetLimitedTimeRewardIconsForDay: 未找到ItemDetail或ItemIcon为空 - ItemID: %d"), ItemID);
            continue;
        }
        
        // 6. 加载物品图标纹理
        UTexture2D* ItemIconTexture = ItemDetail->ItemIcon.LoadSynchronous();
        if (ItemIconTexture)
        {
            Result.Add(ItemIconTexture);
            UE_LOG(LogTemp, Log, TEXT("UUpgradeActivitySubsystem::GetLimitedTimeRewardIconsForDay: 成功添加ItemIcon - ItemID: %d"), ItemID);
        }
        else
        {
            UE_LOG(LogTemp, Warning, TEXT("UUpgradeActivitySubsystem::GetLimitedTimeRewardIconsForDay: ItemIcon加载失败 - ItemID: %d"), ItemID);
        }
    }
    
    UE_LOG(LogTemp, Log, TEXT("UUpgradeActivitySubsystem::GetLimitedTimeRewardIconsForDay: 最终返回%d个限时奖励图标"), Result.Num());
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