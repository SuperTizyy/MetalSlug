/**
 * @file DailyLoginSaveModifier.cpp
 * @brief 每日登录存档动态修改器实现
 * @author AI Assistant
 * @date 2026
 * @version 1.0
 * 
 * @details 实现每日登录存档动态修改器的核心功能
 */

#include "Tools/DailyLoginSaveModifier.h"
#include "Engine/World.h"
#include "HAL/IConsoleManager.h"
#include "Kismet/GameplayStatics.h"

// ==================== 结构体实现 ====================

// FDailyLoginModificationRecord的构造函数已在头文件中定义

// ==================== 主类实现 ====================

UDailyLoginSaveModifier::UDailyLoginSaveModifier()
	: Super()
{
	// 父类已初始化 CachedSaveGame=nullptr, IsInitialized()=false
}

bool UDailyLoginSaveModifier::InitializeModifier(UObject* WorldContext)
{
	// 【2026-06-15 修复】: 委托给基类 (基类会实际赋值 WorldContext 并置 IsInitialized()=true)
	// 修复前: 这里只调了基类, 但基类没有赋值, 导致 IsInitialized() 永远 = false
	return Super::InitializeBase(WorldContext);
}

void UDailyLoginSaveModifier::DestroyModifier()
{
	// 【2026-06-15 修复】: 用基类 IsInitialized() (字段已提升)
	// 修复前: 永远触发, 无法释放
	if (!IsInitialized())
	{
		return;
	}

	// 保存所有未保存的修改
	SaveAllRecords();

	// 委托给基类清理 (基类负责重置 IsInitialized()=false)
	Super::DestroyBase();

	UE_LOG(LogAccount, Log, TEXT("[DailyLoginSaveModifier] 已销毁"));
}

bool UDailyLoginSaveModifier::ModifyPlayerProgress(int32 ActivityID, int32 NewProgress, bool bAutoSave)
{
	if (!IsInitialized())
	{
		UE_LOG(LogTemp, Error, TEXT("DailyLoginSaveModifier: 修改器未初始化"));
		return false;
	}

	UActivitySaveGame* SaveGame = GetOrCreateSaveGame(ActivityID);
	if (!SaveGame)
	{
		UE_LOG(LogTemp, Error, TEXT("DailyLoginSaveModifier: 无法获取存档实例"));
		return false;
	}

	// 获取原始记录
	FPlayerLoginRecord& Record = SaveGame->ActivityRecords.FindOrAdd(ActivityID);
	int32 OriginalProgress = Record.Progress;

	// 记录修改
	AddModificationRecord(ActivityID, TEXT("Progress"), 
		FString::FromInt(OriginalProgress), FString::FromInt(NewProgress));

	// 执行修改
	Record.Progress = NewProgress;
	Record.LastUpdateTime = FDateTime::Now();

	// 自动保存
	if (bAutoSave)
	{
		return SaveActivityRecord(ActivityID);
	}

	UE_LOG(LogTemp, Log, TEXT("DailyLoginSaveModifier: 修改玩家进度成功 - ActivityID=%d, 原进度=%d, 新进度=%d"), 
		ActivityID, OriginalProgress, NewProgress);

	return true;
}

bool UDailyLoginSaveModifier::ModifyDayClaimedStatus(int32 ActivityID, int32 DayIndex, bool bClaimed, bool bAutoSave)
{
	if (!IsInitialized())
	{
		UE_LOG(LogTemp, Error, TEXT("DailyLoginSaveModifier: 修改器未初始化"));
		return false;
	}

	if (DayIndex <= 0 || DayIndex > 32)
	{
		UE_LOG(LogTemp, Error, TEXT("DailyLoginSaveModifier: 天数索引无效: %d"), DayIndex);
		return false;
	}

	UActivitySaveGame* SaveGame = GetOrCreateSaveGame(ActivityID);
	if (!SaveGame)
	{
		UE_LOG(LogTemp, Error, TEXT("DailyLoginSaveModifier: 无法获取存档实例"));
		return false;
	}

	// 获取原始记录
	FPlayerLoginRecord& Record = SaveGame->ActivityRecords.FindOrAdd(ActivityID);
	bool bWasClaimed = Record.IsDayClaimed(DayIndex);

	// 记录修改
	AddModificationRecord(ActivityID, FString::Printf(TEXT("Day%d_Claimed"), DayIndex),
		bWasClaimed ? TEXT("true") : TEXT("false"),
		bClaimed ? TEXT("true") : TEXT("false"));

	// 执行修改
	Record.SetDayClaimed(DayIndex, bClaimed);
	
	// 更新ClaimedDays数组
	if (bClaimed)
	{
		if (!Record.ClaimedDays.Contains(DayIndex))
		{
			Record.ClaimedDays.Add(DayIndex);
		}
	}
	else
	{
		Record.ClaimedDays.Remove(DayIndex);
	}

	// 更新领取次数
	if (bClaimed && !bWasClaimed)
	{
		Record.CurrentClaimCount++;
	}
	else if (!bClaimed && bWasClaimed)
	{
		Record.CurrentClaimCount = FMath::Max(0, Record.CurrentClaimCount - 1);
	}

	Record.LastUpdateTime = FDateTime::Now();

	// 自动保存
	if (bAutoSave)
	{
		return SaveActivityRecord(ActivityID);
	}

	UE_LOG(LogTemp, Log, TEXT("DailyLoginSaveModifier: 修改天数领取状态成功 - ActivityID=%d, Day=%d, 原状态=%s, 新状态=%s"), 
		ActivityID, DayIndex, bWasClaimed ? TEXT("已领取") : TEXT("未领取"), bClaimed ? TEXT("已领取") : TEXT("未领取"));

	return true;
}

bool UDailyLoginSaveModifier::ModifyClaimedDays(int32 ActivityID, const TArray<int32>& ClaimedDays, bool bAutoSave)
{
	if (!IsInitialized())
	{
		UE_LOG(LogTemp, Error, TEXT("DailyLoginSaveModifier: 修改器未初始化"));
		return false;
	}

	UActivitySaveGame* SaveGame = GetOrCreateSaveGame(ActivityID);
	if (!SaveGame)
	{
		UE_LOG(LogTemp, Error, TEXT("DailyLoginSaveModifier: 无法获取存档实例"));
		return false;
	}

	// 获取原始记录
	FPlayerLoginRecord& Record = SaveGame->ActivityRecords.FindOrAdd(ActivityID);
	TArray<int32> OriginalClaimedDays = Record.ClaimedDays;

	// 记录修改
	FString OriginalDaysStr, NewDaysStr;
	for (int32 Day : OriginalClaimedDays)
	{
		OriginalDaysStr += FString::Printf(TEXT("%d,"), Day);
	}
	for (int32 Day : ClaimedDays)
	{
		NewDaysStr += FString::Printf(TEXT("%d,"), Day);
	}

	AddModificationRecord(ActivityID, TEXT("ClaimedDays"), OriginalDaysStr, NewDaysStr);

	// 执行修改
	Record.ClaimedDays = ClaimedDays;
	Record.ClaimedHistoryMask = 0;
	Record.CurrentClaimCount = ClaimedDays.Num();

	// 更新掩码
	for (int32 Day : ClaimedDays)
	{
		if (Day > 0 && Day <= 32)
		{
			Record.SetDayClaimed(Day, true);
		}
	}

	Record.LastUpdateTime = FDateTime::Now();

	// 自动保存
	if (bAutoSave)
	{
		return SaveActivityRecord(ActivityID);
	}

	UE_LOG(LogTemp, Log, TEXT("DailyLoginSaveModifier: 批量修改已领取天数成功 - ActivityID=%d, 原天数=%d个, 新天数=%d个"), 
		ActivityID, OriginalClaimedDays.Num(), ClaimedDays.Num());

	return true;
}

bool UDailyLoginSaveModifier::ModifyCurrentClaimCount(int32 ActivityID, int32 NewCount, bool bAutoSave)
{
	if (!IsInitialized())
	{
		UE_LOG(LogTemp, Error, TEXT("DailyLoginSaveModifier: 修改器未初始化"));
		return false;
	}

	UActivitySaveGame* SaveGame = GetOrCreateSaveGame(ActivityID);
	if (!SaveGame)
	{
		UE_LOG(LogTemp, Error, TEXT("DailyLoginSaveModifier: 无法获取存档实例"));
		return false;
	}

	// 获取原始记录
	FPlayerLoginRecord& Record = SaveGame->ActivityRecords.FindOrAdd(ActivityID);
	int32 OriginalCount = Record.CurrentClaimCount;

	// 记录修改
	AddModificationRecord(ActivityID, TEXT("CurrentClaimCount"), 
		FString::FromInt(OriginalCount), FString::FromInt(NewCount));

	// 执行修改
	Record.CurrentClaimCount = NewCount;
	Record.LastUpdateTime = FDateTime::Now();

	// 自动保存
	if (bAutoSave)
	{
		return SaveActivityRecord(ActivityID);
	}

	UE_LOG(LogTemp, Log, TEXT("DailyLoginSaveModifier: 修改当前领取次数成功 - ActivityID=%d, 原次数=%d, 新次数=%d"), 
		ActivityID, OriginalCount, NewCount);

	return true;
}

bool UDailyLoginSaveModifier::ResetPlayerRecord(int32 ActivityID, bool bAutoSave)
{
	if (!IsInitialized())
	{
		UE_LOG(LogTemp, Error, TEXT("DailyLoginSaveModifier: 修改器未初始化"));
		return false;
	}

	UActivitySaveGame* SaveGame = GetOrCreateSaveGame(ActivityID);
	if (!SaveGame)
	{
		UE_LOG(LogTemp, Error, TEXT("DailyLoginSaveModifier: 无法获取存档实例"));
		return false;
	}

	// 记录修改
	AddModificationRecord(ActivityID, TEXT("Reset"), TEXT("PlayerRecord"), TEXT("Reset"));

	// 执行重置
	FPlayerLoginRecord NewRecord;
	NewRecord.ActivityID = ActivityID;
	NewRecord.PlayerID = TEXT("Player1");
	NewRecord.Progress = 1;  // 重置为第一天可领取
	NewRecord.CurrentClaimCount = 0;
	NewRecord.ClaimedHistoryMask = 0;
	NewRecord.LastClaimTimestamp = 0;
	NewRecord.LastUpdateTime = FDateTime::Now();

	SaveGame->ActivityRecords.Add(ActivityID, NewRecord);

	// 自动保存
	if (bAutoSave)
	{
		return SaveActivityRecord(ActivityID);
	}

	UE_LOG(LogTemp, Log, TEXT("DailyLoginSaveModifier: 重置玩家记录成功 - ActivityID=%d"), ActivityID);

	return true;
}

// ==================== 查询接口实现 ====================

int32 UDailyLoginSaveModifier::GetPlayerProgress(int32 ActivityID) const
{
	if (!IsInitialized() || !CachedSaveGame)
	{
		return 0;
	}

	const FPlayerLoginRecord* Record = CachedSaveGame->ActivityRecords.Find(ActivityID);
	if (Record)
	{
		return Record->Progress;
	}

	return 0;
}

TArray<int32> UDailyLoginSaveModifier::GetClaimedDays(int32 ActivityID) const
{
	TArray<int32> Result;

	if (!IsInitialized() || !CachedSaveGame)
	{
		return Result;
	}

	const FPlayerLoginRecord* Record = CachedSaveGame->ActivityRecords.Find(ActivityID);
	if (Record)
	{
		Result = Record->ClaimedDays;
	}

	return Result;
}

bool UDailyLoginSaveModifier::IsDayClaimed(int32 ActivityID, int32 DayIndex) const
{
	if (!IsInitialized() || !CachedSaveGame)
	{
		return false;
	}

	const FPlayerLoginRecord* Record = CachedSaveGame->ActivityRecords.Find(ActivityID);
	if (Record)
	{
		return Record->IsDayClaimed(DayIndex);
	}

	return false;
}

int32 UDailyLoginSaveModifier::GetCurrentClaimCount(int32 ActivityID) const
{
	if (!IsInitialized() || !CachedSaveGame)
	{
		return 0;
	}

	const FPlayerLoginRecord* Record = CachedSaveGame->ActivityRecords.Find(ActivityID);
	if (Record)
	{
		return Record->CurrentClaimCount;
	}

	return 0;
}

// ==================== 历史记录接口实现 ====================

TArray<FDailyLoginModificationRecord> UDailyLoginSaveModifier::GetModificationHistory(int32 MaxRecords) const
{
	TArray<FDailyLoginModificationRecord> Result;

	int32 StartIndex = FMath::Max(0, ModificationHistory.Num() - MaxRecords);
	for (int32 i = StartIndex; i < ModificationHistory.Num(); ++i)
	{
		Result.Add(ModificationHistory[i]);
	}

	return Result;
}

void UDailyLoginSaveModifier::ClearModificationHistory()
{
	ModificationHistory.Empty();
	UE_LOG(LogTemp, Log, TEXT("DailyLoginSaveModifier: 已清除修改历史记录"));
}

// ==================== 保存接口实现 ====================

bool UDailyLoginSaveModifier::SaveActivityRecord(int32 ActivityID)
{
	if (!IsInitialized() || !CachedSaveGame)
	{
		UE_LOG(LogTemp, Error, TEXT("DailyLoginSaveModifier: 无法保存，存档实例无效"));
		return false;
	}

	FString SaveSlotName = FString::Printf(TEXT("DailyLogin_%d"), ActivityID);
	bool bSuccess = UGameplayStatics::SaveGameToSlot(CachedSaveGame, SaveSlotName, 0);

	if (bSuccess)
	{
		// 标记相关修改记录为已保存
		for (FDailyLoginModificationRecord& Record : ModificationHistory)
		{
			if (Record.ActivityID == ActivityID)
			{
				Record.bIsSaved = true;
			}
		}

		UE_LOG(LogTemp, Log, TEXT("DailyLoginSaveModifier: 成功保存ActivityID=%d的记录"), ActivityID);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("DailyLoginSaveModifier: 保存ActivityID=%d的记录失败"), ActivityID);
	}

	return bSuccess;
}

bool UDailyLoginSaveModifier::SaveAllRecords()
{
	if (!IsInitialized() || !CachedSaveGame)
	{
		return false;
	}

	bool bAllSuccess = true;

	// 保存所有活动记录
	for (const auto& Pair : CachedSaveGame->ActivityRecords)
	{
		int32 ActivityID = Pair.Key;
		if (!SaveActivityRecord(ActivityID))
		{
			bAllSuccess = false;
		}
	}

	if (bAllSuccess)
	{
		UE_LOG(LogTemp, Log, TEXT("DailyLoginSaveModifier: 成功保存所有记录"));
	}

	return bAllSuccess;
}

bool UDailyLoginSaveModifier::LoadActivityRecord(int32 ActivityID)
{
	if (!IsInitialized())
	{
		return false;
	}

	FString SaveSlotName = FString::Printf(TEXT("DailyLogin_%d"), ActivityID);
	UActivitySaveGame* LoadedSaveGame = Cast<UActivitySaveGame>(
		UGameplayStatics::LoadGameFromSlot(SaveSlotName, 0)
	);

	if (LoadedSaveGame)
	{
		CachedSaveGame = LoadedSaveGame;
		UE_LOG(LogTemp, Log, TEXT("DailyLoginSaveModifier: 成功加载ActivityID=%d的记录"), ActivityID);
		return true;
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("DailyLoginSaveModifier: 未找到ActivityID=%d的记录，将创建新记录"), ActivityID);
		CachedSaveGame = GetOrCreateSaveGame(ActivityID);
		return CachedSaveGame != nullptr;
	}
}

// ==================== 内部方法实现 ====================

UActivitySaveGame* UDailyLoginSaveModifier::GetOrCreateSaveGame(int32 ActivityID)
{
	if (CachedSaveGame)
	{
		return CachedSaveGame;
	}

	// 尝试加载现有存档
	FString SaveSlotName = FString::Printf(TEXT("DailyLogin_%d"), ActivityID);
	UActivitySaveGame* LoadedSaveGame = Cast<UActivitySaveGame>(
		UGameplayStatics::LoadGameFromSlot(SaveSlotName, 0)
	);

	if (!LoadedSaveGame)
	{
		// 创建新的存档
		LoadedSaveGame = Cast<UActivitySaveGame>(
			UGameplayStatics::CreateSaveGameObject(UActivitySaveGame::StaticClass())
		);

		if (LoadedSaveGame)
		{
			// 初始化默认记录
			FPlayerLoginRecord NewRecord;
			NewRecord.ActivityID = ActivityID;
			NewRecord.PlayerID = TEXT("Player1");
			NewRecord.Progress = 1;  // 默认第一天可领取
			NewRecord.CurrentClaimCount = 0;
			NewRecord.ClaimedHistoryMask = 0;
			NewRecord.LastClaimTimestamp = 0;
			NewRecord.LastUpdateTime = FDateTime::Now();

			LoadedSaveGame->ActivityRecords.Add(ActivityID, NewRecord);

			// 保存新创建的记录
			UGameplayStatics::SaveGameToSlot(LoadedSaveGame, SaveSlotName, 0);
			UE_LOG(LogTemp, Log, TEXT("DailyLoginSaveModifier: 创建并保存新的存档实例 for ActivityID=%d"), ActivityID);
		}
	}

	CachedSaveGame = LoadedSaveGame;
	return LoadedSaveGame;
}

void UDailyLoginSaveModifier::AddModificationRecord(int32 ActivityID, const FString& FieldName, const FString& OriginalValue, const FString& ModifiedValue)
{
	FDailyLoginModificationRecord Record;
	Record.ActivityID = ActivityID;
	Record.FieldName = FieldName;
	Record.OriginalValue = OriginalValue;
	Record.ModifiedValue = ModifiedValue;
	Record.ModificationTime = FDateTime::Now();
	Record.bIsSaved = false;

	ModificationHistory.Add(Record);
	CleanupOldHistory();

	UE_LOG(LogTemp, Verbose, TEXT("DailyLoginSaveModifier: 添加修改记录 - ActivityID=%d, Field=%s, %s -> %s"), 
		ActivityID, *FieldName, *OriginalValue, *ModifiedValue);
}

void UDailyLoginSaveModifier::CleanupOldHistory()
{
	const int32 MaxHistoryRecords = 100;
	if (ModificationHistory.Num() > MaxHistoryRecords)
	{
		ModificationHistory.RemoveAt(0, ModificationHistory.Num() - MaxHistoryRecords);
	}
}

// ==================== 控制台命令实现 ====================

void UDailyLoginSaveModifier::RegisterConsoleCommands()
{
	if (!GEngine)
	{
		return;
	}

	// 注册控制台命令处理器
	IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("DailyLogin.SetProgress"),
		TEXT("设置每日登录进度: DailyLogin.SetProgress ActivityID ProgressValue"),
		FConsoleCommandWithArgsDelegate::CreateLambda([this](const TArray<FString>& Args)
		{
			if (Args.Num() >= 2)
			{
				int32 ActivityID = FCString::Atoi(*Args[0]);
				int32 Progress = FCString::Atoi(*Args[1]);
				
				bool bSuccess = ModifyPlayerProgress(ActivityID, Progress, true);
				UE_LOG(LogTemp, Log, TEXT("DailyLogin控制台: 设置进度 ActivityID=%d Progress=%d %s"), 
					ActivityID, Progress, bSuccess ? TEXT("成功") : TEXT("失败"));
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("DailyLogin控制台: 用法 - DailyLogin.SetProgress ActivityID ProgressValue"));
			}
		}),
		ECVF_Default
	);

	IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("DailyLogin.SetDayClaimed"),
		TEXT("设置某天领取状态: DailyLogin.SetDayClaimed ActivityID DayIndex IsClaimed"),
		FConsoleCommandWithArgsDelegate::CreateLambda([this](const TArray<FString>& Args)
		{
			if (Args.Num() >= 3)
			{
				int32 ActivityID = FCString::Atoi(*Args[0]);
				int32 DayIndex = FCString::Atoi(*Args[1]);
				bool bClaimed = FCString::Atoi(*Args[2]) != 0;
				
				bool bSuccess = ModifyDayClaimedStatus(ActivityID, DayIndex, bClaimed, true);
				UE_LOG(LogTemp, Log, TEXT("DailyLogin控制台: 设置天数领取状态 ActivityID=%d DayIndex=%d Claimed=%s %s"), 
					ActivityID, DayIndex, bClaimed ? TEXT("是") : TEXT("否"), bSuccess ? TEXT("成功") : TEXT("失败"));
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("DailyLogin控制台: 用法 - DailyLogin.SetDayClaimed ActivityID DayIndex IsClaimed"));
			}
		}),
		ECVF_Default
	);

	IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("DailyLogin.Reset"),
		TEXT("重置每日登录数据: DailyLogin.Reset ActivityID"),
		FConsoleCommandWithArgsDelegate::CreateLambda([this](const TArray<FString>& Args)
		{
			if (Args.Num() >= 1)
			{
				int32 ActivityID = FCString::Atoi(*Args[0]);
				
				bool bSuccess = ResetDailyLoginData(ActivityID);
				UE_LOG(LogTemp, Log, TEXT("DailyLogin控制台: 重置数据 ActivityID=%d %s"), 
					ActivityID, bSuccess ? TEXT("成功") : TEXT("失败"));
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("DailyLogin控制台: 用法 - DailyLogin.Reset ActivityID"));
			}
		}),
		ECVF_Default
	);

	IConsoleManager::Get().RegisterConsoleCommand(
		TEXT("DailyLogin.ShowInfo"),
		TEXT("显示每日登录信息: DailyLogin.ShowInfo ActivityID"),
		FConsoleCommandWithArgsDelegate::CreateLambda([this](const TArray<FString>& Args)
		{
			if (Args.Num() >= 1)
			{
				int32 ActivityID = FCString::Atoi(*Args[0]);
				DisplayDailyLoginInfo(ActivityID);
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("DailyLogin控制台: 用法 - DailyLogin.ShowInfo ActivityID"));
			}
		}),
		ECVF_Default
	);
}

void UDailyLoginSaveModifier::UnregisterConsoleCommands()
{
	if (!GEngine)
	{
		return;
	}

	// 注销控制台命令
	IConsoleManager::Get().UnregisterConsoleObject(TEXT("DailyLogin.SetProgress"));
	IConsoleManager::Get().UnregisterConsoleObject(TEXT("DailyLogin.SetDayClaimed"));
	IConsoleManager::Get().UnregisterConsoleObject(TEXT("DailyLogin.Reset"));
	IConsoleManager::Get().UnregisterConsoleObject(TEXT("DailyLogin.ShowInfo"));
}

// ==================== 公共接口实现 ====================

bool UDailyLoginSaveModifier::ResetDailyLoginData(int32 ActivityID, bool bAutoSave)
{
	if (!IsInitialized())
	{
		UE_LOG(LogTemp, Error, TEXT("DailyLoginSaveModifier: 未初始化"));
		return false;
	}

	UActivitySaveGame* SaveGame = GetOrCreateSaveGame(ActivityID);
	if (!SaveGame)
	{
		return false;
	}

	// 查找或创建活动记录
	FPlayerLoginRecord* Record = SaveGame->ActivityRecords.Find(ActivityID);
	if (!Record)
	{
		// 创建新的记录
		FPlayerLoginRecord NewRecord;
		NewRecord.ActivityID = ActivityID;
		NewRecord.PlayerID = TEXT("DefaultPlayer");
		NewRecord.Progress = 0;
		NewRecord.CurrentClaimCount = 0;
		NewRecord.ClaimedHistoryMask = 0;
		NewRecord.ClaimedDays.Empty();
		NewRecord.LastClaimTimestamp = 0;
		NewRecord.LastUpdateTime = FDateTime::Now();
		
		SaveGame->ActivityRecords.Add(ActivityID, NewRecord);
		Record = SaveGame->ActivityRecords.Find(ActivityID);
	}

	// 保存原始数据用于记录
	FString OriginalProgress = FString::FromInt(Record->Progress);
	FString OriginalClaimedDays = "[";
	for (int32 i = 0; i < Record->ClaimedDays.Num(); ++i)
	{
		if (i > 0) OriginalClaimedDays += ", ";
		OriginalClaimedDays += FString::FromInt(Record->ClaimedDays[i]);
	}
	OriginalClaimedDays += "]";

	// 重置数据
	Record->Progress = 0;
	Record->CurrentClaimCount = 0;
	Record->ClaimedHistoryMask = 0;
	Record->ClaimedDays.Empty();
	Record->LastClaimTimestamp = 0;
	Record->LastUpdateTime = FDateTime::Now();

	// 添加修改记录
	AddModificationRecord(ActivityID, TEXT("Progress"), OriginalProgress, TEXT("0"));
	AddModificationRecord(ActivityID, TEXT("CurrentClaimCount"), TEXT("0"), TEXT("0"));
	AddModificationRecord(ActivityID, TEXT("ClaimedHistoryMask"), TEXT("0"), TEXT("0"));
	AddModificationRecord(ActivityID, TEXT("ClaimedDays"), OriginalClaimedDays, TEXT("[]"));
	AddModificationRecord(ActivityID, TEXT("LastClaimTimestamp"), TEXT("0"), TEXT("0"));

	UE_LOG(LogTemp, Log, TEXT("DailyLoginSaveModifier: 重置活动%d的数据"), ActivityID);

	if (bAutoSave)
	{
		return SaveAllRecords();
	}

	return true;
}

void UDailyLoginSaveModifier::DisplayDailyLoginInfo(int32 ActivityID)
{
	if (!IsInitialized())
	{
		UE_LOG(LogTemp, Error, TEXT("DailyLoginSaveModifier: 未初始化"));
		return;
	}

	UActivitySaveGame* SaveGame = GetOrCreateSaveGame(ActivityID);
	if (!SaveGame)
	{
		UE_LOG(LogTemp, Warning, TEXT("DailyLoginSaveModifier: 未找到活动%d的存档数据"), ActivityID);
		return;
	}

	// 查找活动记录
	FPlayerLoginRecord* Record = SaveGame->ActivityRecords.Find(ActivityID);
	if (!Record)
	{
		UE_LOG(LogTemp, Warning, TEXT("DailyLoginSaveModifier: 活动%d无记录数据"), ActivityID);
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("===== 每日登录信息 (ActivityID: %d) ====="), ActivityID);
	UE_LOG(LogTemp, Log, TEXT("玩家ID: %s"), *Record->PlayerID);
	UE_LOG(LogTemp, Log, TEXT("当前进度: %d"), Record->Progress);
	UE_LOG(LogTemp, Log, TEXT("当前领取次数: %d"), Record->CurrentClaimCount);
	UE_LOG(LogTemp, Log, TEXT("领取历史掩码: %d"), Record->ClaimedHistoryMask);
	UE_LOG(LogTemp, Log, TEXT("最后领取时间戳: %lld"), Record->LastClaimTimestamp);
	UE_LOG(LogTemp, Log, TEXT("最后更新时间: %s"), *Record->LastUpdateTime.ToString());
	
	FString ClaimedDaysStr = "[";
	for (int32 i = 0; i < Record->ClaimedDays.Num(); ++i)
	{
		if (i > 0) ClaimedDaysStr += ", ";
		ClaimedDaysStr += FString::FromInt(Record->ClaimedDays[i]);
	}
	ClaimedDaysStr += "]";
	UE_LOG(LogTemp, Log, TEXT("已领取天数: %s"), *ClaimedDaysStr);
	
	UE_LOG(LogTemp, Log, TEXT("修改历史记录数量: %d"), ModificationHistory.Num());
	UE_LOG(LogTemp, Log, TEXT("====================================="));
}