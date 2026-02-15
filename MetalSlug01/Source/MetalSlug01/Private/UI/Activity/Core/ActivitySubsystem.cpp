// 1


#include "UI/Activity/Core/ActivitySubsystem.h"

#include "UI/Activity/Track/DailyLogin/DailyLoginTrack.h"
#include "UI/Activity/Track/Treasure/TreasureTrack.h"
// 管理器通过Track间接访问，无需直接包含

void UActivitySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// ================= 创建 DailyLogin Track =================
	DailyLoginTrack = NewObject<UDailyLoginTrack>(this);
	if (DailyLoginTrack)
	{
		DailyLoginTrack->Initialize(7);
	}

	// ================= 创建 Treasure Track =================
	TreasureTrack = NewObject<UTreasureTrack>(this);
	if (TreasureTrack)
	{
		TreasureTrack->Init();
	}

	// ================= 初始化管理器 =================
	// 管理器通过Track间接访问，无需在此处创建实例
}

void UActivitySubsystem::Deinitialize()
{
	// Subsystem 销毁时，Track 和管理器会随 GC 自动回收
	DailyLoginTrack = nullptr;
	TreasureTrack = nullptr;
	RedDotManager = nullptr;
	ActivityPageManager = nullptr;
	ActivityTimeManager = nullptr;

	Super::Deinitialize();
}

UDailyLoginTrack* UActivitySubsystem::GetDailyLoginTrack() const
{
	return DailyLoginTrack;
}

UTreasureTrack* UActivitySubsystem::GetTreasureTrack() const
{
	return TreasureTrack;
}

URedDotManager* UActivitySubsystem::GetRedDotManager() const
{
	return RedDotManager;
}

UActivityPageManager* UActivitySubsystem::GetActivityPageManager() const
{
	return ActivityPageManager;
}

UActivityTimeManager* UActivitySubsystem::GetActivityTimeManager() const
{
	return ActivityTimeManager;
}

TArray<const FActivityInfoRow*> UActivitySubsystem::GetAllNavItems() const
{
	// 直接从DataTable加载所有活动信息
	FString InfoPath = TEXT("/Game/UI/Activity/Data/DT_ActivityInfoRow");
	UDataTable* ActivityInfoTable = Cast<UDataTable>(StaticLoadObject(UDataTable::StaticClass(), nullptr, *InfoPath));
	
	TArray<const FActivityInfoRow*> Result;
	
	if (ActivityInfoTable)
	{
		static const FString ContextString(TEXT("ActivitySubsystem"));
		TArray<FActivityInfoRow*> AllRows;
		ActivityInfoTable->GetAllRows<FActivityInfoRow>(ContextString, AllRows);
		
		for (FActivityInfoRow* Row : AllRows)
		{
			if (Row)
			{
				Result.Add(Row);
			}
		}
	}
	
	return Result;
}

const FActivityInfoRow* UActivitySubsystem::GetActivityInfo(int32 ActivityID) const
{
	// 从DataTable查找指定ActivityID的信息
	FString InfoPath = TEXT("/Game/UI/Activity/Data/DT_ActivityInfoRow");
	UDataTable* ActivityInfoTable = Cast<UDataTable>(StaticLoadObject(UDataTable::StaticClass(), nullptr, *InfoPath));
	
	if (ActivityInfoTable)
	{
		static const FString ContextString(TEXT("ActivitySubsystem"));
		TArray<FActivityInfoRow*> AllRows;
		ActivityInfoTable->GetAllRows<FActivityInfoRow>(ContextString, AllRows);
		
		for (FActivityInfoRow* Row : AllRows)
		{
			if (Row && Row->ActivityID == ActivityID)
			{
				return Row;
			}
		}
	}
	
	return nullptr;
}

FPlayerLoginRecord& UActivitySubsystem::GetOrInitPlayerRecord(int32 ActivityID)
{
	// 这里应该从SaveGame系统获取或创建玩家记录
	// 临时返回静态记录以避免编译错误
	static FPlayerLoginRecord DummyRecord;
	DummyRecord.ActivityID = ActivityID;
	return DummyRecord;
}

TArray<FDailyLoginConfigRow*> UActivitySubsystem::GetDailyLoginConfigs(int32 ActivityID) const
{
	// 从DailyLoginConfig表加载指定ActivityID的所有配置
	FString ConfigPath = TEXT("/Game/UI/Activity/Data/DT_DailyLoginConfigRow");
	UDataTable* ConfigTable = Cast<UDataTable>(StaticLoadObject(UDataTable::StaticClass(), nullptr, *ConfigPath));
	
	TArray<FDailyLoginConfigRow*> Result;
	
	if (ConfigTable)
	{
		static const FString ContextString(TEXT("ActivitySubsystem"));
		TArray<FDailyLoginConfigRow*> AllRows;
		ConfigTable->GetAllRows<FDailyLoginConfigRow>(ContextString, AllRows);
		
		UE_LOG(LogTemp, Warning, TEXT("ActivitySubsystem: GetDailyLoginConfigs called with ActivityID=%d"), ActivityID);
		UE_LOG(LogTemp, Warning, TEXT("ActivitySubsystem: Found %d total rows in DT_DailyLoginConfig"), AllRows.Num());
		
		for (FDailyLoginConfigRow* Row : AllRows)
		{
			if (Row)
			{
				UE_LOG(LogTemp, Log, TEXT("Config Row: ActivityID=%d, DayIndex=%d, RewardItemID=%d"), 
					Row->ActivityID, Row->DayIndex, Row->RewardItemID);
				
				if (Row->ActivityID == ActivityID)
				{
					Result.Add(Row);
				}
			}
		}
		
		UE_LOG(LogTemp, Warning, TEXT("ActivitySubsystem: Returning %d config rows for ActivityID=%d"), Result.Num(), ActivityID);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("ActivitySubsystem: Failed to load DT_DailyLoginConfig for GetDailyLoginConfigs"));
	}
	
	return Result;
}

TArray<FDailyLoginConfigRow*> UActivitySubsystem::GetRewardsByDay(int32 ActivityID, int32 Day) const
{
	// 从DailyLoginConfig表加载指定ActivityID和DayIndex的奖励配置
	FString ConfigPath = TEXT("/Game/UI/Activity/Data/DT_DailyLoginConfigRow");
	UDataTable* ConfigTable = Cast<UDataTable>(StaticLoadObject(UDataTable::StaticClass(), nullptr, *ConfigPath));
	
	TArray<FDailyLoginConfigRow*> Result;
	
	if (ConfigTable)
	{
		static const FString ContextString(TEXT("ActivitySubsystem"));
		TArray<FDailyLoginConfigRow*> AllRows;
		ConfigTable->GetAllRows<FDailyLoginConfigRow>(ContextString, AllRows);
		
		// 调试日志：输出所有行的信息
		UE_LOG(LogTemp, Warning, TEXT("ActivitySubsystem: GetRewardsByDay called with ActivityID=%d, Day=%d"), ActivityID, Day);
		UE_LOG(LogTemp, Warning, TEXT("ActivitySubsystem: Found %d rows in DT_DailyLoginConfig"), AllRows.Num());
		
		for (FDailyLoginConfigRow* Row : AllRows)
		{
			if (Row)
			{
				UE_LOG(LogTemp, Log, TEXT("Row: ActivityID=%d, DayIndex=%d, RewardItemID=%d"), 
					Row->ActivityID, Row->DayIndex, Row->RewardItemID);
				
				if (Row->ActivityID == ActivityID && Row->DayIndex == Day)
				{
					UE_LOG(LogTemp, Warning, TEXT("ActivitySubsystem: Found matching row for ActivityID=%d, Day=%d"), ActivityID, Day);
					Result.Add(Row);
				}
			}
		}
		
		UE_LOG(LogTemp, Warning, TEXT("ActivitySubsystem: Returning %d reward rows"), Result.Num());
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("ActivitySubsystem: Failed to load DT_DailyLoginConfig from path: %s"), *ConfigPath);
	}
	
	return Result;
}

bool UActivitySubsystem::TryClaimReward(int32 ActivityID, int32 DayIndex)
{
	// 这里应该实现实际的奖励领取逻辑
	// 临时返回true以避免编译错误
	return true;
}

void UActivitySubsystem::Cheat_JumpToDay(int32 ActivityID, int32 NewDay)
{
	// 这里应该实现作弊跳转逻辑
	// 临时空实现
}

bool UActivitySubsystem::TryClaimMultipleRewards(int32 ActivityID, const TArray<int32>& DayIndices)
{
	// 批量领取奖励逻辑
	bool bAllSuccess = true;
	for (int32 DayIndex : DayIndices)
	{
		if (!TryClaimReward(ActivityID, DayIndex))
		{
			bAllSuccess = false;
		}
	}
	return bAllSuccess;
}
