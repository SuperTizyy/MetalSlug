/**
 * @file UpgradeActivitySaveModifier.cpp
 * @brief 升级活动存档动态修改器实现
 * @author AI Assistant
 * @date 2026
 * @version 1.0
 * 
 * @details 实现升级活动存档动态修改器的核心功能
 *          与UpgradeActivitySubsystem解耦，提供独立的数据修改能力
 */

#include "Tools/UpgradeActivitySaveModifier.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"
#include "UI/Activity/Data/DailyLoginSave.h"
#include "UI/Activity/Core/UpgradeActivitySubsystem.h"

// ==================== 结构体实现 ====================

// FUpgradeActivityModificationRecord的构造函数已在头文件中定义

// ==================== 主类实现 ====================

UUpgradeActivitySaveModifier::UUpgradeActivitySaveModifier()
	: CachedSaveGame(nullptr), TargetSubsystem(nullptr), bIsInitialized(false)
{
}

bool UUpgradeActivitySaveModifier::InitializeModifier(UObject* WorldContext, UUpgradeActivitySubsystem* Subsystem)
{
	if (!WorldContext)
	{
		UE_LOG(LogTemp, Error, TEXT("UpgradeActivitySaveModifier: WorldContext为空"));
		return false;
	}

	WorldContextObject = WorldContext;
	
	// 如果提供了Subsystem，直接使用；否则尝试自动获取
	if (Subsystem)
	{
		TargetSubsystem = Subsystem;
		UE_LOG(LogTemp, Log, TEXT("UpgradeActivitySaveModifier: 使用提供的Subsystem实例"));
	}
	else
	{
		// 尝试从GameInstance获取Subsystem
		UGameInstance* GameInstance = WorldContext->GetWorld()->GetGameInstance();
		if (GameInstance)
		{
			TargetSubsystem = GameInstance->GetSubsystem<UUpgradeActivitySubsystem>();
			if (TargetSubsystem)
			{
				UE_LOG(LogTemp, Log, TEXT("UpgradeActivitySaveModifier: 成功获取GameInstance中的Subsystem"));
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("UpgradeActivitySaveModifier: 无法从GameInstance获取Subsystem"));
			}
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("UpgradeActivitySaveModifier: 无法获取GameInstance"));
		}
	}

	bIsInitialized = true;
	UE_LOG(LogTemp, Log, TEXT("UpgradeActivitySaveModifier: 初始化成功"));
	return true;
}

void UUpgradeActivitySaveModifier::DestroyModifier()
{
	if (!bIsInitialized)
	{
		return;
	}

	// 保存所有未保存的修改
	SaveAllRecords();

	// 清理资源
	CachedSaveGame = nullptr;
	WorldContextObject.Reset();
	bIsInitialized = false;

	UE_LOG(LogTemp, Log, TEXT("UpgradeActivitySaveModifier: 已销毁"));
}

bool UUpgradeActivitySaveModifier::ModifyCurrentExperience(int32 RecordDate, int32 NewExp, bool bAutoSave)
{
	if (!bIsInitialized)
	{
		UE_LOG(LogTemp, Error, TEXT("UpgradeActivitySaveModifier: 修改器未初始化"));
		return false;
	}

	if (NewExp < 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("UpgradeActivitySaveModifier: 经验值不能为负数: %d"), NewExp);
		return false;
	}

	// 优先修改Subsystem中的热数据
	if (TargetSubsystem)
	{
		int32 OriginalExp = TargetSubsystem->GetRecord().CurrentExperience;
		
		// 直接修改Subsystem的当前记录
		TargetSubsystem->GetRecord().CurrentExperience = NewExp;
		TargetSubsystem->GetRecord().LastUpdateTime = FDateTime::Now();
		
		UE_LOG(LogTemp, Log, TEXT("UpgradeActivitySaveModifier: 直接修改Subsystem热数据 - 原经验=%d, 新经验=%d"), 
			OriginalExp, NewExp);
		
		// 如果需要保存到磁盘
		if (bAutoSave)
		{
			TargetSubsystem->SaveStatus();
		}
		
		// 必须调用Broadcast触发UI刷新
		UE_LOG(LogTemp, Log, TEXT("UpgradeActivitySaveModifier: 🔥 准备调用OnGlobalRefresh.Broadcast()"));
		// 注意：动态多播委托无法直接获取绑定数量
		
		// 强制检查所有可能的页面实例
		CheckAndNotifyAllPages();
		
		TargetSubsystem->OnGlobalRefresh.Broadcast();
		UE_LOG(LogTemp, Log, TEXT("UpgradeActivitySaveModifier: ✅ OnGlobalRefresh.Broadcast()调用完成"));
		
		return true;
	}

	// 如果没有Subsystem，则回退到原来的存档操作
	UE_LOG(LogTemp, Warning, TEXT("UpgradeActivitySaveModifier: 未找到Subsystem，使用存档操作"));
	
	UDailyLoginSaveGame* SaveGame = GetOrCreateSaveGame(RecordDate);
	if (!SaveGame)
	{
		UE_LOG(LogTemp, Error, TEXT("UpgradeActivitySaveModifier: 无法获取存档实例"));
		return false;
	}

	// 获取原始记录
	FUpgradeRewardSaveRecord& Record = SaveGame->UpgradeRewardRecords.FindOrAdd(RecordDate);
	int32 OriginalExp = Record.CurrentExperience;

	// 执行修改
	Record.CurrentExperience = NewExp;
	Record.LastUpdateTime = FDateTime::Now();

	// 自动保存
	if (bAutoSave)
	{
		return SaveRecord(RecordDate);
	}

	UE_LOG(LogTemp, Log, TEXT("UpgradeActivitySaveModifier: 修改存档数据成功 - RecordDate=%d, 原经验=%d, 新经验=%d"), 
		RecordDate, OriginalExp, NewExp);

	return true;
}

bool UUpgradeActivitySaveModifier::ModifyRewardIconIndex(int32 RecordDate, int32 NewIndex, bool bAutoSave)
{
	if (!bIsInitialized)
	{
		UE_LOG(LogTemp, Error, TEXT("UpgradeActivitySaveModifier: 修改器未初始化"));
		return false;
	}

	if (NewIndex < 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("UpgradeActivitySaveModifier: 图标索引不能为负数: %d"), NewIndex);
		return false;
	}

	UDailyLoginSaveGame* SaveGame = GetOrCreateSaveGame(RecordDate);
	if (!SaveGame)
	{
		UE_LOG(LogTemp, Error, TEXT("UpgradeActivitySaveModifier: 无法获取存档实例"));
		return false;
	}

	// 获取原始记录
	FUpgradeRewardSaveRecord& Record = SaveGame->UpgradeRewardRecords.FindOrAdd(RecordDate);
	int32 OriginalIndex = Record.RewardIconIndex;

	// 执行修改
	Record.RewardIconIndex = NewIndex;
	Record.LastUpdateTime = FDateTime::Now();

	// 自动保存
	if (bAutoSave)
	{
		return SaveRecord(RecordDate);
	}

	UE_LOG(LogTemp, Log, TEXT("UpgradeActivitySaveModifier: 修改奖励图标索引成功 - RecordDate=%d, 原索引=%d, 新索引=%d"), 
		RecordDate, OriginalIndex, NewIndex);

	// 如果有Subsystem，触发UI刷新
	if (TargetSubsystem)
	{
		UE_LOG(LogTemp, Log, TEXT("UpgradeActivitySaveModifier: 准备调用OnGlobalRefresh.Broadcast()"));
		TargetSubsystem->OnGlobalRefresh.Broadcast();
		UE_LOG(LogTemp, Log, TEXT("UpgradeActivitySaveModifier: OnGlobalRefresh.Broadcast()调用完成"));
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("UpgradeActivitySaveModifier: TargetSubsystem为空，无法触发UI刷新"));
	}

	return true;
}

bool UUpgradeActivitySaveModifier::ModifyChestClaimStatus(int32 RecordDate, int32 ChestIndex, int32 IsClaimed, bool bAutoSave)
{
	if (!bIsInitialized)
	{
		UE_LOG(LogTemp, Error, TEXT("UpgradeActivitySaveModifier: 修改器未初始化"));
		return false;
	}

	if (IsClaimed != 0 && IsClaimed != 1)
	{
		UE_LOG(LogTemp, Warning, TEXT("UpgradeActivitySaveModifier: 宝箱状态只能是0或1: %d"), IsClaimed);
		return false;
	}

	UDailyLoginSaveGame* SaveGame = GetOrCreateSaveGame(RecordDate);
	if (!SaveGame)
	{
		UE_LOG(LogTemp, Error, TEXT("UpgradeActivitySaveModifier: 无法获取存档实例"));
		return false;
	}

	// 获取记录并进行边界检查
	FUpgradeRewardSaveRecord& Record = SaveGame->UpgradeRewardRecords.FindOrAdd(RecordDate);
	if (ChestIndex < 0 || !Record.ChestClaimStatus.IsValidIndex(ChestIndex))
	{
		UE_LOG(LogTemp, Error, TEXT("UpgradeActivitySaveModifier: 宝箱索引超出范围: %d"), ChestIndex);
		return false;
	}
	int32 OriginalStatus = Record.ChestClaimStatus[ChestIndex];

	// 执行修改
	Record.ChestClaimStatus[ChestIndex] = IsClaimed;
	Record.LastUpdateTime = FDateTime::Now();

	// 自动保存
	if (bAutoSave)
	{
		return SaveRecord(RecordDate);
	}

	FString StatusText = (IsClaimed == 1) ? TEXT("已领取") : TEXT("未领取");
	UE_LOG(LogTemp, Log, TEXT("UpgradeActivitySaveModifier: 修改宝箱领取状态成功 - RecordDate=%d, 宝箱%d原状态=%s, 新状态=%s"), 
		RecordDate, ChestIndex, OriginalStatus ? TEXT("已领取") : TEXT("未领取"), *StatusText);

	// 如果有Subsystem，触发UI刷新
	if (TargetSubsystem)
	{
		UE_LOG(LogTemp, Log, TEXT("UpgradeActivitySaveModifier: 准备调用OnGlobalRefresh.Broadcast()"));
		TargetSubsystem->OnGlobalRefresh.Broadcast();
		UE_LOG(LogTemp, Log, TEXT("UpgradeActivitySaveModifier: OnGlobalRefresh.Broadcast()调用完成"));
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("UpgradeActivitySaveModifier: TargetSubsystem为空，无法触发UI刷新"));
	}

	return true;
}

bool UUpgradeActivitySaveModifier::ModifyTaskCompleteCount(int32 RecordDate, int32 TaskIndex, int32 Count, bool bAutoSave)
{
	if (!bIsInitialized)
	{
		UE_LOG(LogTemp, Error, TEXT("UpgradeActivitySaveModifier: 修改器未初始化"));
		return false;
	}

	if (Count < 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("UpgradeActivitySaveModifier: 任务完成次数不能为负数: %d"), Count);
		return false;
	}

	UDailyLoginSaveGame* SaveGame = GetOrCreateSaveGame(RecordDate);
	if (!SaveGame)
	{
		UE_LOG(LogTemp, Error, TEXT("UpgradeActivitySaveModifier: 无法获取存档实例"));
		return false;
	}

	// 获取记录并进行边界检查
	FUpgradeRewardSaveRecord& Record = SaveGame->UpgradeRewardRecords.FindOrAdd(RecordDate);
	if (TaskIndex < 0 || !Record.TaskCompleteCounts.IsValidIndex(TaskIndex))
	{
		UE_LOG(LogTemp, Error, TEXT("UpgradeActivitySaveModifier: 任务索引超出范围: %d"), TaskIndex);
		return false;
	}
	int32 OriginalCount = Record.TaskCompleteCounts[TaskIndex];

	// 执行修改
	Record.TaskCompleteCounts[TaskIndex] = Count;
	Record.LastUpdateTime = FDateTime::Now();

	// 自动保存
	if (bAutoSave)
	{
		return SaveRecord(RecordDate);
	}

	UE_LOG(LogTemp, Log, TEXT("UpgradeActivitySaveModifier: 修改任务完成次数成功 - RecordDate=%d, 任务%d原次数=%d, 新次数=%d"), 
		RecordDate, TaskIndex, OriginalCount, Count);

	// 如果有Subsystem，触发UI刷新
	if (TargetSubsystem)
	{
		TargetSubsystem->OnGlobalRefresh.Broadcast();
	}

	return true;
}

bool UUpgradeActivitySaveModifier::ModifyTaskClaimStatus(int32 RecordDate, int32 TaskIndex, int32 IsClaimed, bool bAutoSave)
{
	if (!bIsInitialized)
	{
		UE_LOG(LogTemp, Error, TEXT("UpgradeActivitySaveModifier: 修改器未初始化"));
		return false;
	}

	if (IsClaimed != 0 && IsClaimed != 1)
	{
		UE_LOG(LogTemp, Warning, TEXT("UpgradeActivitySaveModifier: 任务状态只能是0或1: %d"), IsClaimed);
		return false;
	}

	UDailyLoginSaveGame* SaveGame = GetOrCreateSaveGame(RecordDate);
	if (!SaveGame)
	{
		UE_LOG(LogTemp, Error, TEXT("UpgradeActivitySaveModifier: 无法获取存档实例"));
		return false;
	}

	// 获取记录并进行边界检查
	FUpgradeRewardSaveRecord& Record = SaveGame->UpgradeRewardRecords.FindOrAdd(RecordDate);
	if (TaskIndex < 0 || !Record.TaskClaimStatus.IsValidIndex(TaskIndex))
	{
		UE_LOG(LogTemp, Error, TEXT("UpgradeActivitySaveModifier: 任务索引超出范围: %d"), TaskIndex);
		return false;
	}
	int32 OriginalStatus = Record.TaskClaimStatus[TaskIndex];

	// 执行修改
	Record.TaskClaimStatus[TaskIndex] = IsClaimed;
	Record.LastUpdateTime = FDateTime::Now();

	// 自动保存
	if (bAutoSave)
	{
		return SaveRecord(RecordDate);
	}

	FString StatusText = (IsClaimed == 1) ? TEXT("已领取") : TEXT("未领取");
	UE_LOG(LogTemp, Log, TEXT("UpgradeActivitySaveModifier: 修改任务领取状态成功 - RecordDate=%d, 任务%d原状态=%s, 新状态=%s"), 
		RecordDate, TaskIndex, OriginalStatus ? TEXT("已领取") : TEXT("未领取"), *StatusText);

	// 如果有Subsystem，触发UI刷新
	if (TargetSubsystem)
	{
		TargetSubsystem->OnGlobalRefresh.Broadcast();
	}

	return true;
}

bool UUpgradeActivitySaveModifier::ModifyLimitedActivityCount(int32 RecordDate, int32 Count, bool bAutoSave)
{
	if (!bIsInitialized)
	{
		UE_LOG(LogTemp, Error, TEXT("UpgradeActivitySaveModifier: 修改器未初始化"));
		return false;
	}

	if (Count < 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("UpgradeActivitySaveModifier: 限时活动次数不能为负数: %d"), Count);
		return false;
	}

	UDailyLoginSaveGame* SaveGame = GetOrCreateSaveGame(RecordDate);
	if (!SaveGame)
	{
		UE_LOG(LogTemp, Error, TEXT("UpgradeActivitySaveModifier: 无法获取存档实例"));
		return false;
	}

	// 获取原始记录
	FUpgradeRewardSaveRecord& Record = SaveGame->UpgradeRewardRecords.FindOrAdd(RecordDate);
	int32 OriginalCount = Record.LimitedActivityCompleteCount;

	// 执行修改
	Record.LimitedActivityCompleteCount = Count;
	Record.LastUpdateTime = FDateTime::Now();

	// 自动保存
	if (bAutoSave)
	{
		return SaveRecord(RecordDate);
	}

	UE_LOG(LogTemp, Log, TEXT("UpgradeActivitySaveModifier: 修改限时活动完成次数成功 - RecordDate=%d, 原次数=%d, 新次数=%d"), 
		RecordDate, OriginalCount, Count);

	// 如果有Subsystem，触发UI刷新
	if (TargetSubsystem)
	{
		TargetSubsystem->OnGlobalRefresh.Broadcast();
	}

	return true;
}

bool UUpgradeActivitySaveModifier::ResetRecordData(int32 RecordDate, bool bAutoSave)
{
	if (!bIsInitialized)
	{
		UE_LOG(LogTemp, Error, TEXT("UpgradeActivitySaveModifier: 修改器未初始化"));
		return false;
	}

	UDailyLoginSaveGame* SaveGame = GetOrCreateSaveGame(RecordDate);
	if (!SaveGame)
	{
		UE_LOG(LogTemp, Error, TEXT("UpgradeActivitySaveModifier: 无法获取存档实例"));
		return false;
	}

	// 执行重置
	FUpgradeRewardSaveRecord NewRecord;
	NewRecord.SetRecordDate(RecordDate);
	
	// 初始化默认值
	NewRecord.CurrentExperience = 0;
	NewRecord.RewardIconIndex = 0;
	NewRecord.LimitedActivityCompleteCount = 0;
	
	// 初始化数组
	for (int32 i = 0; i < MAX_CHEST_COUNT; ++i)
	{
		NewRecord.ChestClaimStatus[i] = 0;
	}
	for (int32 i = 0; i < MAX_TASK_COUNT; ++i)
	{
		NewRecord.TaskCompleteCounts[i] = 0;
		NewRecord.TaskClaimStatus[i] = 0;
	}
	
	NewRecord.LastUpdateTime = FDateTime::Now();

	SaveGame->UpgradeRewardRecords.Add(RecordDate, NewRecord);

	// 自动保存
	if (bAutoSave)
	{
		return SaveRecord(RecordDate);
	}

	UE_LOG(LogTemp, Log, TEXT("UpgradeActivitySaveModifier: 重置记录数据成功 - RecordDate=%d"), RecordDate);

	return true;
}

bool UUpgradeActivitySaveModifier::CreateNewRecord(int32 RecordDate, bool bInheritPrevious, bool bAutoSave)
{
	if (!bIsInitialized)
	{
		UE_LOG(LogTemp, Error, TEXT("UpgradeActivitySaveModifier: 修改器未初始化"));
		return false;
	}

	UDailyLoginSaveGame* SaveGame = GetOrCreateSaveGame(RecordDate);
	if (!SaveGame)
	{
		UE_LOG(LogTemp, Error, TEXT("UpgradeActivitySaveModifier: 无法获取存档实例"));
		return false;
	}

	// 检查是否已存在
	if (SaveGame->UpgradeRewardRecords.Contains(RecordDate))
	{
		UE_LOG(LogTemp, Warning, TEXT("UpgradeActivitySaveModifier: 记录%d已存在"), RecordDate);
		return false;
	}

	FUpgradeRewardSaveRecord NewRecord;
	NewRecord.SetRecordDate(RecordDate);

	if (bInheritPrevious && RecordDate > 1)
	{
		// 尝试继承前一天的数据
		const FUpgradeRewardSaveRecord* PreviousRecord = SaveGame->UpgradeRewardRecords.Find(RecordDate - 1);
		if (PreviousRecord)
		{
			// 继承部分数据
			NewRecord.CurrentExperience = PreviousRecord->CurrentExperience;
			NewRecord.RewardIconIndex = PreviousRecord->RewardIconIndex;
			
			// 宝箱状态继承（但可以重新领取）
			for (int32 i = 0; i < MAX_CHEST_COUNT; ++i)
			{
				NewRecord.ChestClaimStatus[i] = 0; // 新的一天，宝箱重置
			}
			
			// 任务状态重置
			for (int32 i = 0; i < MAX_TASK_COUNT; ++i)
			{
				NewRecord.TaskCompleteCounts[i] = 0;
				NewRecord.TaskClaimStatus[i] = 0;
			}
			
			NewRecord.LimitedActivityCompleteCount = 0; // 限时活动重置
			
			UE_LOG(LogTemp, Log, TEXT("UpgradeActivitySaveModifier: 继承第%d天的部分数据创建第%d天记录"), 
				RecordDate - 1, RecordDate);
		}
		else
		{
			// 没有前一天数据，使用默认值
			NewRecord.CurrentExperience = 0;
			NewRecord.RewardIconIndex = 0;
			NewRecord.LimitedActivityCompleteCount = 0;
			
			for (int32 i = 0; i < MAX_CHEST_COUNT; ++i)
			{
				NewRecord.ChestClaimStatus[i] = 0;
			}
			for (int32 i = 0; i < MAX_TASK_COUNT; ++i)
			{
				NewRecord.TaskCompleteCounts[i] = 0;
				NewRecord.TaskClaimStatus[i] = 0;
			}
		}
	}
	else
	{
		// 不继承，使用默认值
		NewRecord.CurrentExperience = 0;
		NewRecord.RewardIconIndex = 0;
		NewRecord.LimitedActivityCompleteCount = 0;
		
		for (int32 i = 0; i < MAX_CHEST_COUNT; ++i)
		{
			NewRecord.ChestClaimStatus[i] = 0;
		}
		for (int32 i = 0; i < MAX_TASK_COUNT; ++i)
		{
			NewRecord.TaskCompleteCounts[i] = 0;
			NewRecord.TaskClaimStatus[i] = 0;
		}
	}

	NewRecord.LastUpdateTime = FDateTime::Now();
	SaveGame->UpgradeRewardRecords.Add(RecordDate, NewRecord);

	NewRecord.LastUpdateTime = FDateTime::Now();
	SaveGame->UpgradeRewardRecords.Add(RecordDate, NewRecord);

	// 自动保存
	if (bAutoSave)
	{
		return SaveRecord(RecordDate);
	}

	UE_LOG(LogTemp, Log, TEXT("UpgradeActivitySaveModifier: 创建新记录成功 - RecordDate=%d"), RecordDate);

	return true;
}

// ==================== 查询接口实现 ====================

int32 UUpgradeActivitySaveModifier::GetCurrentExperience(int32 RecordDate) const
{
	if (!bIsInitialized || !CachedSaveGame)
	{
		return 0;
	}

	const FUpgradeRewardSaveRecord* Record = CachedSaveGame->UpgradeRewardRecords.Find(RecordDate);
	if (Record)
	{
		return Record->CurrentExperience;
	}

	return 0;
}

int32 UUpgradeActivitySaveModifier::GetRewardIconIndex(int32 RecordDate) const
{
	if (!bIsInitialized || !CachedSaveGame)
	{
		return 0;
	}

	const FUpgradeRewardSaveRecord* Record = CachedSaveGame->UpgradeRewardRecords.Find(RecordDate);
	if (Record)
	{
		return Record->RewardIconIndex;
	}

	return 0;
}

int32 UUpgradeActivitySaveModifier::GetChestClaimStatus(int32 RecordDate, int32 ChestIndex) const
{
	if (!bIsInitialized || !CachedSaveGame)
	{
		return 0;
	}

	const FUpgradeRewardSaveRecord* Record = CachedSaveGame->UpgradeRewardRecords.Find(RecordDate);
	if (Record && Record->ChestClaimStatus.IsValidIndex(ChestIndex))
	{
		return Record->ChestClaimStatus[ChestIndex];
	}

	return 0;
}

int32 UUpgradeActivitySaveModifier::GetTaskCompleteCount(int32 RecordDate, int32 TaskIndex) const
{
	if (!bIsInitialized || !CachedSaveGame)
	{
		return 0;
	}

	const FUpgradeRewardSaveRecord* Record = CachedSaveGame->UpgradeRewardRecords.Find(RecordDate);
	if (Record && Record->TaskCompleteCounts.IsValidIndex(TaskIndex))
	{
		return Record->TaskCompleteCounts[TaskIndex];
	}

	return 0;
}

int32 UUpgradeActivitySaveModifier::GetTaskClaimStatus(int32 RecordDate, int32 TaskIndex) const
{
	if (!bIsInitialized || !CachedSaveGame)
	{
		return 0;
	}

	const FUpgradeRewardSaveRecord* Record = CachedSaveGame->UpgradeRewardRecords.Find(RecordDate);
	if (Record && Record->TaskClaimStatus.IsValidIndex(TaskIndex))
	{
		return Record->TaskClaimStatus[TaskIndex];
	}

	return 0;
}

int32 UUpgradeActivitySaveModifier::GetLimitedActivityCount(int32 RecordDate) const
{
	if (!bIsInitialized || !CachedSaveGame)
	{
		return 0;
	}

	const FUpgradeRewardSaveRecord* Record = CachedSaveGame->UpgradeRewardRecords.Find(RecordDate);
	if (Record)
	{
		return Record->LimitedActivityCompleteCount;
	}

	return 0;
}

// ==================== 保存接口实现 ====================

bool UUpgradeActivitySaveModifier::SaveRecord(int32 RecordDate)
{
	if (!bIsInitialized || !CachedSaveGame)
	{
		UE_LOG(LogTemp, Error, TEXT("UpgradeActivitySaveModifier: 无法保存，存档实例无效"));
		return false;
	}

	// 使用与Subsystem一致的存档槽位
	FString SaveSlotName = TEXT("UpgradeReward_SaveSlot");
	bool bSuccess = UGameplayStatics::SaveGameToSlot(CachedSaveGame, SaveSlotName, 0);

	if (bSuccess)
	{
		UE_LOG(LogTemp, Log, TEXT("UpgradeActivitySaveModifier: 成功保存RecordDate=%d的记录"), RecordDate);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("UpgradeActivitySaveModifier: 保存RecordDate=%d的记录失败"), RecordDate);
	}

	return bSuccess;
}

bool UUpgradeActivitySaveModifier::SaveAllRecords()
{
	if (!bIsInitialized || !CachedSaveGame)
	{
		return false;
	}

	bool bAllSuccess = true;

	// 保存所有记录
	for (const auto& Pair : CachedSaveGame->UpgradeRewardRecords)
	{
		int32 RecordDate = Pair.Key;
		if (!SaveRecord(RecordDate))
		{
			bAllSuccess = false;
		}
	}

	if (bAllSuccess)
	{
		UE_LOG(LogTemp, Log, TEXT("UpgradeActivitySaveModifier: 成功保存所有记录"));
	}

	return bAllSuccess;
}

bool UUpgradeActivitySaveModifier::LoadRecord(int32 RecordDate)
{
	if (!bIsInitialized)
	{
		return false;
	}

	// 使用与Subsystem一致的存档槽位
	FString SaveSlotName = TEXT("UpgradeReward_SaveSlot");
	UDailyLoginSaveGame* LoadedSaveGame = Cast<UDailyLoginSaveGame>(
		UGameplayStatics::LoadGameFromSlot(SaveSlotName, 0)
	);

	if (LoadedSaveGame)
	{
		CachedSaveGame = LoadedSaveGame;
		UE_LOG(LogTemp, Log, TEXT("UpgradeActivitySaveModifier: 成功加载RecordDate=%d的记录"), RecordDate);
		return true;
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("UpgradeActivitySaveModifier: 未找到RecordDate=%d的记录，将创建新记录"), RecordDate);
		CachedSaveGame = GetOrCreateSaveGame(RecordDate);
		return CachedSaveGame != nullptr;
	}
}

// ==================== 内部方法实现 ====================

UDailyLoginSaveGame* UUpgradeActivitySaveModifier::GetOrCreateSaveGame(int32 RecordDate)
{
	if (CachedSaveGame)
	{
		return CachedSaveGame;
	}

	// 尝试加载现有存档 - 使用与Subsystem一致的存档槽位
	FString SaveSlotName = TEXT("UpgradeReward_SaveSlot");
	UDailyLoginSaveGame* LoadedSaveGame = Cast<UDailyLoginSaveGame>(
		UGameplayStatics::LoadGameFromSlot(SaveSlotName, 0)
	);

	if (!LoadedSaveGame)
	{
		// 创建新的存档
		LoadedSaveGame = Cast<UDailyLoginSaveGame>(
			UGameplayStatics::CreateSaveGameObject(UDailyLoginSaveGame::StaticClass())
		);

		if (LoadedSaveGame)
		{
			// 初始化默认记录
			FUpgradeRewardSaveRecord NewRecord;
			NewRecord.SetRecordDate(RecordDate);
			NewRecord.CurrentExperience = 0;
			NewRecord.RewardIconIndex = 0;
			NewRecord.LimitedActivityCompleteCount = 0;
			
			// 初始化数组 - 使用动态大小
			NewRecord.ChestClaimStatus.SetNumZeroed(MAX_CHEST_COUNT);
			NewRecord.TaskCompleteCounts.SetNumZeroed(MAX_TASK_COUNT);
			NewRecord.TaskClaimStatus.SetNumZeroed(MAX_TASK_COUNT);
			
			NewRecord.LastUpdateTime = FDateTime::Now();

			LoadedSaveGame->UpgradeRewardRecords.Add(RecordDate, NewRecord);

			// 保存新创建的记录 - 使用统一的存档槽位
			UGameplayStatics::SaveGameToSlot(LoadedSaveGame, TEXT("UpgradeReward_SaveSlot"), 0);
			UE_LOG(LogTemp, Log, TEXT("UpgradeActivitySaveModifier: 创建并保存新的存档实例 for RecordDate=%d"), RecordDate);
		}
	}

	CachedSaveGame = LoadedSaveGame;
	return LoadedSaveGame;
}

// ==================== 控制台命令实现 ====================

void UUpgradeActivitySaveModifier::RegisterConsoleCommands()
{
	if (!GEngine)
	{
		return;
	}

	// 注册控制台命令处理器
	IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("Upgrade.SetExp"),
		TEXT("设置经验值: Upgrade.SetExp RecordDate ExperienceValue"),
		FConsoleCommandWithArgsDelegate::CreateLambda([WorldContext = WorldContextObject](const TArray<FString>& Args)
		{
			if (Args.Num() >= 2)
			{
				int32 RecordDate = FCString::Atoi(*Args[0]);
				int32 Exp = FCString::Atoi(*Args[1]);
				
				// 通过GameInstance获取最新的Subsystem实例
				if (WorldContext.IsValid())
				{
					UWorld* World = WorldContext->GetWorld();
					if (World)
					{
						UGameInstance* GameInstance = World->GetGameInstance();
						if (GameInstance)
						{
							UUpgradeActivitySubsystem* Subsystem = GameInstance->GetSubsystem<UUpgradeActivitySubsystem>();
							if (Subsystem)
							{
								// 直接修改Subsystem热数据
								int32 OriginalExp = Subsystem->GetRecord().CurrentExperience;
								Subsystem->GetRecord().CurrentExperience = Exp;
								Subsystem->GetRecord().LastUpdateTime = FDateTime::Now();
								
								UE_LOG(LogTemp, Log, TEXT("\n==========================================================="));
								UE_LOG(LogTemp, Log, TEXT("🔧 SUBSYSTEM_DATA_MODIFICATION_START"));
								UE_LOG(LogTemp, Log, TEXT("🆔 Subsystem地址: %p"), Subsystem);
								UE_LOG(LogTemp, Log, TEXT("📊 数据修改: 原经验=%d, 新经验=%d"), OriginalExp, Exp);
								UE_LOG(LogTemp, Log, TEXT("⏰ 修改时间: %s"), *FDateTime::Now().ToString());
								UE_LOG(LogTemp, Log, TEXT("===========================================================\n"));
								
								// 保存到磁盘
								Subsystem->SaveStatus();
								
								// 触发UI刷新
								UE_LOG(LogTemp, Log, TEXT("\n==========================================================="));
								UE_LOG(LogTemp, Log, TEXT("🔊 BROADCAST_EVENT_TRIGGER_START"));
								UE_LOG(LogTemp, Log, TEXT("🆔 广播Subsystem地址: %p"), Subsystem);
								UE_LOG(LogTemp, Log, TEXT("📢 触发事件: OnGlobalRefresh.Broadcast()"));
								Subsystem->OnGlobalRefresh.Broadcast();
								UE_LOG(LogTemp, Log, TEXT("✅ OnGlobalRefresh.Broadcast()调用完成"));
								UE_LOG(LogTemp, Log, TEXT("===========================================================\n"));
								
								UE_LOG(LogTemp, Log, TEXT("Upgrade控制台: 设置经验值 RecordDate=%d Exp=%d 成功"), RecordDate, Exp);
								return;
							}
						}
					}
				}
				
				UE_LOG(LogTemp, Error, TEXT("Upgrade控制台: 无法获取Subsystem实例"));
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("Upgrade控制台: 用法 - Upgrade.SetExp RecordDate ExperienceValue"));
			}
		}),
		ECVF_Default
	);

	IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("Upgrade.SetIcon"),
		TEXT("设置奖励图标索引: Upgrade.SetIcon RecordDate IconIndex"),
		FConsoleCommandWithArgsDelegate::CreateLambda([WorldContext = WorldContextObject](const TArray<FString>& Args)
		{
			if (Args.Num() >= 2)
			{
				int32 RecordDate = FCString::Atoi(*Args[0]);
				int32 IconIndex = FCString::Atoi(*Args[1]);
				
				// 通过GameInstance获取最新的Subsystem实例
				if (WorldContext.IsValid())
				{
					UWorld* World = WorldContext->GetWorld();
					if (World)
					{
						UGameInstance* GameInstance = World->GetGameInstance();
						if (GameInstance)
						{
							UUpgradeActivitySubsystem* Subsystem = GameInstance->GetSubsystem<UUpgradeActivitySubsystem>();
							if (Subsystem)
							{
								// 直接修改Subsystem热数据
								int32 OriginalIndex = Subsystem->GetRecord().RewardIconIndex;
								Subsystem->GetRecord().RewardIconIndex = IconIndex;
								Subsystem->GetRecord().LastUpdateTime = FDateTime::Now();
								
								UE_LOG(LogTemp, Log, TEXT("Upgrade控制台: 直接修改Subsystem热数据 - 原图标索引=%d, 新图标索引=%d"), OriginalIndex, IconIndex);
								
								// 保存到磁盘
								Subsystem->SaveStatus();
								
								// 触发UI刷新
								UE_LOG(LogTemp, Log, TEXT("Upgrade控制台: 🔥 准备调用OnRewardIconIndexChanged.Broadcast()"));
								Subsystem->OnRewardIconIndexChanged.Broadcast(IconIndex);
								UE_LOG(LogTemp, Log, TEXT("Upgrade控制台: ✅ OnRewardIconIndexChanged.Broadcast()调用完成"));
								
								UE_LOG(LogTemp, Log, TEXT("Upgrade控制台: 设置图标索引 RecordDate=%d IconIndex=%d 成功"), RecordDate, IconIndex);
								return;
							}
						}
					}
				}
				
				UE_LOG(LogTemp, Error, TEXT("Upgrade控制台: 无法获取Subsystem实例"));
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("Upgrade控制台: 用法 - Upgrade.SetIcon RecordDate IconIndex"));
			}
		}),
		ECVF_Default
	);

	IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("Upgrade.SetChest"),
		TEXT("设置宝箱领取状态: Upgrade.SetChest RecordDate ChestIndex IsClaimed"),
		FConsoleCommandWithArgsDelegate::CreateLambda([WorldContext = WorldContextObject](const TArray<FString>& Args)
		{
			if (Args.Num() >= 3)
			{
				int32 RecordDate = FCString::Atoi(*Args[0]);
				int32 ChestIndex = FCString::Atoi(*Args[1]);
				int32 IsClaimed = FCString::Atoi(*Args[2]);
				
				// 通过GameInstance获取最新的Subsystem实例
				if (WorldContext.IsValid())
				{
					UWorld* World = WorldContext->GetWorld();
					if (World)
					{
						UGameInstance* GameInstance = World->GetGameInstance();
						if (GameInstance)
						{
							UUpgradeActivitySubsystem* Subsystem = GameInstance->GetSubsystem<UUpgradeActivitySubsystem>();
							if (Subsystem && Subsystem->GetRecord().ChestClaimStatus.IsValidIndex(ChestIndex))
							{
								// 直接修改Subsystem热数据
								int32 OriginalStatus = Subsystem->GetRecord().ChestClaimStatus[ChestIndex];
								Subsystem->GetRecord().ChestClaimStatus[ChestIndex] = IsClaimed;
								Subsystem->GetRecord().LastUpdateTime = FDateTime::Now();
								
								UE_LOG(LogTemp, Log, TEXT("Upgrade控制台: 直接修改Subsystem热数据 - 宝箱%d 原状态=%d, 新状态=%d"), 
									ChestIndex, OriginalStatus, IsClaimed);
								
								// 保存到磁盘
								Subsystem->SaveStatus();
								
								// 触发UI刷新
								UE_LOG(LogTemp, Log, TEXT("Upgrade控制台: 🔥 准备调用OnGlobalRefresh.Broadcast()"));
								Subsystem->OnGlobalRefresh.Broadcast();
								UE_LOG(LogTemp, Log, TEXT("Upgrade控制台: ✅ OnGlobalRefresh.Broadcast()调用完成"));
								
								UE_LOG(LogTemp, Log, TEXT("Upgrade控制台: 设置宝箱状态 RecordDate=%d ChestIndex=%d Claimed=%d 成功"), 
									RecordDate, ChestIndex, IsClaimed);
								return;
							}
						}
					}
				}
				
				UE_LOG(LogTemp, Error, TEXT("Upgrade控制台: 无法获取Subsystem实例或无效的宝箱索引"));
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("Upgrade控制台: 用法 - Upgrade.SetChest RecordDate ChestIndex IsClaimed"));
			}
		}),
		ECVF_Default
	);

	IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("Upgrade.CreateRecord"),
		TEXT("创建新记录: Upgrade.CreateRecord RecordDate [InheritPrevious=1]"),
		FConsoleCommandWithArgsDelegate::CreateLambda([WorldContext = WorldContextObject](const TArray<FString>& Args)
		{
			if (Args.Num() >= 1)
			{
				int32 RecordDate = FCString::Atoi(*Args[0]);
				bool bInherit = Args.Num() >= 2 ? (FCString::Atoi(*Args[1]) != 0) : true;
								
				// 通过GameInstance获取最新的Subsystem实例
				if (WorldContext.IsValid())
				{
					UWorld* World = WorldContext->GetWorld();
					if (World)
					{
						UGameInstance* GameInstance = World->GetGameInstance();
						if (GameInstance)
						{
							UUpgradeActivitySubsystem* Subsystem = GameInstance->GetSubsystem<UUpgradeActivitySubsystem>();
							if (Subsystem)
							{
								// 直接操作Subsystem创建新记录
								UE_LOG(LogTemp, Log, TEXT("\n==========================================================="));
								UE_LOG(LogTemp, Log, TEXT("🔧 SUBSYSTEM_CREATE_RECORD_START"));
								UE_LOG(LogTemp, Log, TEXT("🆔 Subsystem地址: %p"), Subsystem);
								UE_LOG(LogTemp, Log, TEXT("📊 创建记录: RecordDate=%d, Inherit=%s"), RecordDate, bInherit ? TEXT("是") : TEXT("否"));
								UE_LOG(LogTemp, Log, TEXT("⏰ 操作时间: %s"), *FDateTime::Now().ToString());
								UE_LOG(LogTemp, Log, TEXT("===========================================================\n"));
												
								// 调用Subsystem的创建方法（如果存在）或者直接操作
								FUpgradeRewardSaveRecord NewRecord;
								NewRecord.SetRecordDate(RecordDate);
								NewRecord.CurrentExperience = 0;
								NewRecord.RewardIconIndex = 0;
								NewRecord.LimitedActivityCompleteCount = 0;
												
								// 初始化数组
								for (int32 i = 0; i < MAX_CHEST_COUNT; ++i)
								{
									NewRecord.ChestClaimStatus[i] = 0;
								}
								for (int32 i = 0; i < MAX_TASK_COUNT; ++i)
								{
									NewRecord.TaskCompleteCounts[i] = 0;
									NewRecord.TaskClaimStatus[i] = 0;
								}
												
								NewRecord.LastUpdateTime = FDateTime::Now();
												
								// 更新Subsystem中的记录
								Subsystem->GetRecord() = NewRecord;
												
								// 保存到磁盘
								Subsystem->SaveStatus();
												
								// 触发UI刷新
								UE_LOG(LogTemp, Log, TEXT("\n==========================================================="));
								UE_LOG(LogTemp, Log, TEXT("🔊 BROADCAST_EVENT_TRIGGER_START"));
								UE_LOG(LogTemp, Log, TEXT("🆔 广播Subsystem地址: %p"), Subsystem);
								UE_LOG(LogTemp, Log, TEXT("📢 触发事件: OnGlobalRefresh.Broadcast()"));
								Subsystem->OnGlobalRefresh.Broadcast();
								UE_LOG(LogTemp, Log, TEXT("✅ OnGlobalRefresh.Broadcast()调用完成"));
								UE_LOG(LogTemp, Log, TEXT("===========================================================\n"));
												
								UE_LOG(LogTemp, Log, TEXT("Upgrade控制台: 创建记录 RecordDate=%d Inherit=%s 成功"), RecordDate, bInherit ? TEXT("是") : TEXT("否"));
								return;
							}
						}
					}
				}
								
				UE_LOG(LogTemp, Error, TEXT("Upgrade控制台: 无法获取Subsystem实例"));
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("Upgrade控制台: 用法 - Upgrade.CreateRecord RecordDate [InheritPrevious=1]"));
			}
		}),
		ECVF_Default
	);

	IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("Upgrade.Reset"),
		TEXT("重置记录数据: Upgrade.Reset RecordDate"),
		FConsoleCommandWithArgsDelegate::CreateLambda([WorldContext = WorldContextObject](const TArray<FString>& Args)
		{
			if (Args.Num() >= 1)
			{
				int32 RecordDate = FCString::Atoi(*Args[0]);
								
				// 通过GameInstance获取最新的Subsystem实例
				if (WorldContext.IsValid())
				{
					UWorld* World = WorldContext->GetWorld();
					if (World)
					{
						UGameInstance* GameInstance = World->GetGameInstance();
						if (GameInstance)
						{
							UUpgradeActivitySubsystem* Subsystem = GameInstance->GetSubsystem<UUpgradeActivitySubsystem>();
							if (Subsystem)
							{
								// 直接重置Subsystem热数据
								UE_LOG(LogTemp, Log, TEXT("\n==========================================================="));
								UE_LOG(LogTemp, Log, TEXT("🔧 SUBSYSTEM_RESET_DATA_START"));
								UE_LOG(LogTemp, Log, TEXT("🆔 Subsystem地址: %p"), Subsystem);
								UE_LOG(LogTemp, Log, TEXT("📊 重置记录: RecordDate=%d"), RecordDate);
								UE_LOG(LogTemp, Log, TEXT("⏰ 操作时间: %s"), *FDateTime::Now().ToString());
								UE_LOG(LogTemp, Log, TEXT("===========================================================\n"));
												
								// 获取原始数据用于日志记录
								int32 OriginalExp = Subsystem->GetRecord().CurrentExperience;
								int32 OriginalIcon = Subsystem->GetRecord().RewardIconIndex;
												
								// 重置数据
								Subsystem->GetRecord().CurrentExperience = 0;
								Subsystem->GetRecord().RewardIconIndex = 0;
								Subsystem->GetRecord().LimitedActivityCompleteCount = 0;
												
								// 重置数组
								for (int32 i = 0; i < MAX_CHEST_COUNT && i < Subsystem->GetRecord().ChestClaimStatus.Num(); ++i)
								{
									Subsystem->GetRecord().ChestClaimStatus[i] = 0;
								}
								for (int32 i = 0; i < MAX_TASK_COUNT && i < Subsystem->GetRecord().TaskCompleteCounts.Num(); ++i)
								{
									Subsystem->GetRecord().TaskCompleteCounts[i] = 0;
									Subsystem->GetRecord().TaskClaimStatus[i] = 0;
								}
												
								Subsystem->GetRecord().LastUpdateTime = FDateTime::Now();
												
								UE_LOG(LogTemp, Log, TEXT("Upgrade控制台: 重置前数据 - 经验=%d, 图标=%d"), OriginalExp, OriginalIcon);
								UE_LOG(LogTemp, Log, TEXT("Upgrade控制台: 重置后数据 - 经验=0, 图标=0"));
												
								// 保存到磁盘
								Subsystem->SaveStatus();
												
								// 触发UI刷新
								UE_LOG(LogTemp, Log, TEXT("\n==========================================================="));
								UE_LOG(LogTemp, Log, TEXT("🔊 BROADCAST_EVENT_TRIGGER_START"));
								UE_LOG(LogTemp, Log, TEXT("🆔 广播Subsystem地址: %p"), Subsystem);
								UE_LOG(LogTemp, Log, TEXT("📢 触发事件: OnGlobalRefresh.Broadcast()"));
								Subsystem->OnGlobalRefresh.Broadcast();
								UE_LOG(LogTemp, Log, TEXT("✅ OnGlobalRefresh.Broadcast()调用完成"));
								UE_LOG(LogTemp, Log, TEXT("===========================================================\n"));
												
								UE_LOG(LogTemp, Log, TEXT("Upgrade控制台: 重置数据 RecordDate=%d 成功"), RecordDate);
								return;
							}
						}
					}
				}
								
				UE_LOG(LogTemp, Error, TEXT("Upgrade控制台: 无法获取Subsystem实例"));
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("Upgrade控制台: 用法 - Upgrade.Reset RecordDate"));
			}
		}),
		ECVF_Default
	);

	IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("Upgrade.ShowInfo"),
		TEXT("显示记录信息: Upgrade.ShowInfo RecordDate"),
		FConsoleCommandWithArgsDelegate::CreateLambda([WorldContext = WorldContextObject](const TArray<FString>& Args)
		{
			if (Args.Num() >= 1)
			{
				int32 RecordDate = FCString::Atoi(*Args[0]);
									
				// 通过GameInstance获取最新的Subsystem实例
				if (WorldContext.IsValid())
				{
					UWorld* World = WorldContext->GetWorld();
					if (World)
					{
						UGameInstance* GameInstance = World->GetGameInstance();
						if (GameInstance)
						{
							UUpgradeActivitySubsystem* Subsystem = GameInstance->GetSubsystem<UUpgradeActivitySubsystem>();
							if (Subsystem)
							{
								// 显示Subsystem中的实时数据
								UE_LOG(LogTemp, Log, TEXT("\n==========================================================="));
								UE_LOG(LogTemp, Log, TEXT("📋 SUBSYSTEM_DATA_DISPLAY_START"));
								UE_LOG(LogTemp, Log, TEXT("🆔 Subsystem地址: %p"), Subsystem);
								UE_LOG(LogTemp, Log, TEXT("📊 显示记录: RecordDate=%d"), RecordDate);
								UE_LOG(LogTemp, Log, TEXT("⏰ 显示时间: %s"), *FDateTime::Now().ToString());
								UE_LOG(LogTemp, Log, TEXT("===========================================================\n"));
															
								const FUpgradeRewardSaveRecord& Record = Subsystem->GetRecord();
															
								UE_LOG(LogTemp, Log, TEXT("===== 升级活动信息 (RecordDate: %d) ====="), RecordDate);
								UE_LOG(LogTemp, Log, TEXT("当前经验值: %d"), Record.CurrentExperience);
								UE_LOG(LogTemp, Log, TEXT("奖励图标索引: %d"), Record.RewardIconIndex);
								UE_LOG(LogTemp, Log, TEXT("限时活动完成次数: %d"), Record.LimitedActivityCompleteCount);
								UE_LOG(LogTemp, Log, TEXT("最后更新时间: %s"), *Record.LastUpdateTime.ToString());
															
								// 显示宝箱状态
								FString ChestStatus = TEXT("宝箱状态: ");
								for (int32 i = 0; i < Record.ChestClaimStatus.Num() && i < MAX_CHEST_COUNT; ++i)
								{
									ChestStatus += FString::Printf(TEXT("[%d]=%d "), i, Record.ChestClaimStatus[i]);
								}
								UE_LOG(LogTemp, Log, TEXT("%s"), *ChestStatus);
															
								// 显示任务状态
								FString TaskStatus = TEXT("任务状态: ");
								for (int32 i = 0; i < Record.TaskCompleteCounts.Num() && i < MAX_TASK_COUNT; ++i)
								{
									TaskStatus += FString::Printf(TEXT("[%d]完成=%d 领取=%d "), i, Record.TaskCompleteCounts[i], Record.TaskClaimStatus[i]);
								}
								UE_LOG(LogTemp, Log, TEXT("%s"), *TaskStatus);
															
								UE_LOG(LogTemp, Log, TEXT("修改器状态: 已启用热数据连接"));
								UE_LOG(LogTemp, Log, TEXT("Subsystem地址: %p"), Subsystem);
								UE_LOG(LogTemp, Log, TEXT("====================================="));
								return;
							}
						}
					}
				}
									
				UE_LOG(LogTemp, Error, TEXT("Upgrade控制台: 无法获取Subsystem实例"));
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("Upgrade控制台: 用法 - Upgrade.ShowInfo RecordDate"));
			}
		}),
		ECVF_Default
	);
	
	IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("Upgrade.ShowAllInfo"),
		TEXT("显示所有天数记录信息: Upgrade.ShowAllInfo"),
		FConsoleCommandWithArgsDelegate::CreateLambda([WorldContext = WorldContextObject](const TArray<FString>& Args)
		{
			// 通过GameInstance获取最新的Subsystem实例
			if (WorldContext.IsValid())
			{
				UWorld* World = WorldContext->GetWorld();
				if (World)
				{
					UGameInstance* GameInstance = World->GetGameInstance();
					if (GameInstance)
					{
						UUpgradeActivitySubsystem* Subsystem = GameInstance->GetSubsystem<UUpgradeActivitySubsystem>();
						if (Subsystem)
						{
							// 显示所有记录信息
							UE_LOG(LogTemp, Log, TEXT("\n==========================================================="));
							UE_LOG(LogTemp, Log, TEXT("📋 ALL_RECORDS_DATA_DISPLAY_START"));
							UE_LOG(LogTemp, Log, TEXT("🆔 Subsystem地址: %p"), Subsystem);
							UE_LOG(LogTemp, Log, TEXT("📊 显示所有记录信息"));
							UE_LOG(LogTemp, Log, TEXT("⏰ 显示时间: %s"), *FDateTime::Now().ToString());
							UE_LOG(LogTemp, Log, TEXT("===========================================================\n"));
																				
							// 获取存档游戏实例来访问所有记录
							UDailyLoginSaveGame* SaveGame = Subsystem->GetSaveGameInstance();
							if (!SaveGame)
							{
								UE_LOG(LogTemp, Error, TEXT("Upgrade控制台: 无法获取存档实例"));
								return;
							}
														
							// 获取所有记录日期
							TArray<int32> RecordDates;
							for (const auto& Pair : SaveGame->UpgradeRewardRecords)
							{
								RecordDates.Add(Pair.Key);
							}
														
							// 按日期排序
							RecordDates.Sort([](int32 A, int32 B) { return A < B; });
														
							if (RecordDates.Num() == 0)
							{
								UE_LOG(LogTemp, Log, TEXT("⚠️ 没有找到任何记录数据"));
								return;
							}
														
							UE_LOG(LogTemp, Log, TEXT("===== 所有升级活动记录信息 ====="));
							UE_LOG(LogTemp, Log, TEXT("总记录数: %d"), RecordDates.Num());
							UE_LOG(LogTemp, Log, TEXT("====================================="));
														
							// 显示每条记录的详细信息
							for (int32 RecordDate : RecordDates)
							{
								const FUpgradeRewardSaveRecord* Record = SaveGame->UpgradeRewardRecords.Find(RecordDate);
								if (Record)
								{
									UE_LOG(LogTemp, Log, TEXT("\n--- 第%d天记录 ---"), RecordDate);
									UE_LOG(LogTemp, Log, TEXT("当前经验值: %d"), Record->CurrentExperience);
									UE_LOG(LogTemp, Log, TEXT("奖励图标索引: %d"), Record->RewardIconIndex);
									UE_LOG(LogTemp, Log, TEXT("限时活动完成次数: %d"), Record->LimitedActivityCompleteCount);
									UE_LOG(LogTemp, Log, TEXT("最后更新时间: %s"), *Record->LastUpdateTime.ToString());
																
									// 显示宝箱状态
									FString ChestStatus = TEXT("宝箱状态: ");
									for (int32 i = 0; i < Record->ChestClaimStatus.Num() && i < MAX_CHEST_COUNT; ++i)
									{
										ChestStatus += FString::Printf(TEXT("[%d]=%d "), i, Record->ChestClaimStatus[i]);
									}
									UE_LOG(LogTemp, Log, TEXT("%s"), *ChestStatus);
																
									// 显示任务状态
									FString TaskStatus = TEXT("任务状态: ");
									for (int32 i = 0; i < Record->TaskCompleteCounts.Num() && i < MAX_TASK_COUNT; ++i)
									{
										TaskStatus += FString::Printf(TEXT("[%d]完成=%d 领取=%d "), i, Record->TaskCompleteCounts[i], Record->TaskClaimStatus[i]);
									}
									UE_LOG(LogTemp, Log, TEXT("%s"), *TaskStatus);
								}
							}
								
							UE_LOG(LogTemp, Log, TEXT("\n====================================="));
							UE_LOG(LogTemp, Log, TEXT("修改器状态: 已启用热数据连接"));
							UE_LOG(LogTemp, Log, TEXT("Subsystem地址: %p"), Subsystem);
							UE_LOG(LogTemp, Log, TEXT("所有记录显示完成"));
							return;
						}
					}
				}
			}
									
			UE_LOG(LogTemp, Error, TEXT("Upgrade控制台: 无法获取Subsystem实例"));
		}),
		ECVF_Default
	);
}

void UUpgradeActivitySaveModifier::UnregisterConsoleCommands()
{
	if (!GEngine)
	{
		return;
	}

	// 注销控制台命令
	IConsoleManager::Get().UnregisterConsoleObject(TEXT("Upgrade.SetExp"));
	IConsoleManager::Get().UnregisterConsoleObject(TEXT("Upgrade.SetIcon"));
	IConsoleManager::Get().UnregisterConsoleObject(TEXT("Upgrade.SetChest"));
	IConsoleManager::Get().UnregisterConsoleObject(TEXT("Upgrade.CreateRecord"));
	IConsoleManager::Get().UnregisterConsoleObject(TEXT("Upgrade.Reset"));
	IConsoleManager::Get().UnregisterConsoleObject(TEXT("Upgrade.ShowInfo"));
	IConsoleManager::Get().UnregisterConsoleObject(TEXT("Upgrade.ShowAllInfo"));
}

// ==================== 公共接口实现 ====================

bool UUpgradeActivitySaveModifier::ResetUpgradeActivityData(int32 RecordDate, bool bAutoSave)
{
	if (!bIsInitialized)
	{
		UE_LOG(LogTemp, Error, TEXT("UpgradeActivitySaveModifier: 未初始化"));
		return false;
	}

	UDailyLoginSaveGame* SaveGame = GetOrCreateSaveGame(RecordDate);
	if (!SaveGame)
	{
		return false;
	}

	// 查找或创建记录
	FUpgradeRewardSaveRecord* Record = SaveGame->UpgradeRewardRecords.Find(RecordDate);
	if (!Record)
	{
		// 创建新的记录
		FUpgradeRewardSaveRecord NewRecord;
		NewRecord.SetRecordDate(RecordDate);
		NewRecord.CurrentExperience = 0;
		NewRecord.RewardIconIndex = 0;
		NewRecord.LimitedActivityCompleteCount = 0;
		
		// 初始化数组 - 使用动态大小
		NewRecord.ChestClaimStatus.SetNumZeroed(MAX_CHEST_COUNT);
		NewRecord.TaskCompleteCounts.SetNumZeroed(MAX_TASK_COUNT);
		NewRecord.TaskClaimStatus.SetNumZeroed(MAX_TASK_COUNT);
		
		NewRecord.LastUpdateTime = FDateTime::Now();
		
		SaveGame->UpgradeRewardRecords.Add(RecordDate, NewRecord);
		Record = SaveGame->UpgradeRewardRecords.Find(RecordDate);
	}

	// 保存原始数据用于记录
	FString OriginalExp = FString::FromInt(Record->CurrentExperience);
	FString OriginalIcon = FString::FromInt(Record->RewardIconIndex);

	// 重置数据
	Record->CurrentExperience = 0;
	Record->RewardIconIndex = 0;
	Record->LimitedActivityCompleteCount = 0;
	
	// 重置数组
	for (int32 i = 0; i < Record->ChestClaimStatus.Num() && i < MAX_CHEST_COUNT; ++i)
	{
		Record->ChestClaimStatus[i] = 0;
	}
	for (int32 i = 0; i < Record->TaskCompleteCounts.Num() && i < MAX_TASK_COUNT; ++i)
	{
		Record->TaskCompleteCounts[i] = 0;
		Record->TaskClaimStatus[i] = 0;
	}
	
	Record->LastUpdateTime = FDateTime::Now();

	Record->LastUpdateTime = FDateTime::Now();

	UE_LOG(LogTemp, Log, TEXT("UpgradeActivitySaveModifier: 重置记录%d的数据"), RecordDate);

	if (bAutoSave)
	{
		return SaveAllRecords();
	}

	return true;
}

void UUpgradeActivitySaveModifier::DisplayUpgradeActivityInfo(int32 RecordDate)
{
	if (!bIsInitialized)
	{
		UE_LOG(LogTemp, Error, TEXT("UpgradeActivitySaveModifier: 未初始化"));
		return;
	}

	UDailyLoginSaveGame* SaveGame = GetOrCreateSaveGame(RecordDate);
	if (!SaveGame)
	{
		UE_LOG(LogTemp, Warning, TEXT("UpgradeActivitySaveModifier: 未找到记录%d的存档数据"), RecordDate);
		return;
	}

	// 查找记录
	FUpgradeRewardSaveRecord* Record = SaveGame->UpgradeRewardRecords.Find(RecordDate);
	if (!Record)
	{
		UE_LOG(LogTemp, Warning, TEXT("UpgradeActivitySaveModifier: 记录%d无数据"), RecordDate);
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("===== 升级活动信息 (RecordDate: %d) ====="), RecordDate);
	UE_LOG(LogTemp, Log, TEXT("当前经验值: %d"), Record->CurrentExperience);
	UE_LOG(LogTemp, Log, TEXT("奖励图标索引: %d"), Record->RewardIconIndex);
	UE_LOG(LogTemp, Log, TEXT("限时活动完成次数: %d"), Record->LimitedActivityCompleteCount);
	UE_LOG(LogTemp, Log, TEXT("最后更新时间: %s"), *Record->LastUpdateTime.ToString());
	
	// 显示宝箱状态
	FString ChestStatus = TEXT("宝箱状态: ");
	for (int32 i = 0; i < Record->ChestClaimStatus.Num() && i < MAX_CHEST_COUNT; ++i)
	{
		ChestStatus += FString::Printf(TEXT("[%d]=%d "), i, Record->ChestClaimStatus[i]);
	}
	UE_LOG(LogTemp, Log, TEXT("%s"), *ChestStatus);
	
	// 显示任务状态
	FString TaskStatus = TEXT("任务状态: ");
	for (int32 i = 0; i < Record->TaskCompleteCounts.Num() && i < MAX_TASK_COUNT; ++i)
	{
		TaskStatus += FString::Printf(TEXT("[%d]完成=%d 领取=%d "), i, Record->TaskCompleteCounts[i], Record->TaskClaimStatus[i]);
	}
	UE_LOG(LogTemp, Log, TEXT("%s"), *TaskStatus);
	
	UE_LOG(LogTemp, Log, TEXT("修改器状态: 已启用热数据连接"));
	UE_LOG(LogTemp, Log, TEXT("====================================="));
}

void UUpgradeActivitySaveModifier::CheckAndNotifyAllPages()
{
	UE_LOG(LogTemp, Log, TEXT("UpgradeActivitySaveModifier: 开始检查所有页面实例"));
	
	// 直接使用已有的TargetSubsystem
	if (!TargetSubsystem)
	{
		UE_LOG(LogTemp, Warning, TEXT("UpgradeActivitySaveModifier: TargetSubsystem为空"));
		return;
	}
	
	// 验证Subsystem地址
	UE_LOG(LogTemp, Log, TEXT("UpgradeActivitySaveModifier: TargetSubsystem地址=%p"), TargetSubsystem);
	
	// 手动触发一次全局刷新
	UE_LOG(LogTemp, Log, TEXT("UpgradeActivitySaveModifier: 手动触发全局刷新"));
	TargetSubsystem->OnGlobalRefresh.Broadcast();
	
	// 如果有奖励图标索引变化，也触发相应事件
	TargetSubsystem->OnRewardIconIndexChanged.Broadcast(TargetSubsystem->GetCurrentRewardIconIndex());
	
	UE_LOG(LogTemp, Log, TEXT("UpgradeActivitySaveModifier: 手动刷新完成"));
}

void UUpgradeActivitySaveModifier::ShowDailyUpgradePage()
{
	if (!WorldContextObject.IsValid())
	{
		UE_LOG(LogTemp, Error, TEXT("UpgradeActivitySaveModifier: 世界上下文无效"));
		return;
	}

	UWorld* World = WorldContextObject->GetWorld();
	if (!World)
	{
		UE_LOG(LogTemp, Error, TEXT("UpgradeActivitySaveModifier: 无法获取世界实例"));
		return;
	}

	// 获取DailyUpgradeRewardPage类（需要在项目中定义）
	// 这里只是一个示例，实际使用时需要替换为正确的类路径
	/*
	TSubclassOf<UDailyUpgradeRewardPage> PageClass = LoadClass<UDailyUpgradeRewardPage>(nullptr, 
		TEXT("/Game/UI/Activity/Pages/WBP_DailyUpgradeRewardPage.WBP_DailyUpgradeRewardPage"));
	
	if (!PageClass)
	{
		UE_LOG(LogTemp, Error, TEXT("UpgradeActivitySaveModifier: 无法加载DailyUpgradeRewardPage类"));
		return;
	}

	// 创建并显示页面
	UDailyUpgradeRewardPage* Page = CreateWidget<UDailyUpgradeRewardPage>(World, PageClass);
	if (Page)
	{
		Page->AddToViewport(0); // 添加到最顶层
		UE_LOG(LogTemp, Log, TEXT("✅ 成功显示DailyUpgradeRewardPage"));
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("UpgradeActivitySaveModifier: 创建页面失败"));
	}
	*/
	
	UE_LOG(LogTemp, Warning, TEXT("⚠️ 请在项目中实现ShowDailyUpgradePage功能"));
}
