// 1


#include "UI/Activity/Core/ActivitySubsystem.h"

#include "UI/Activity/Track/DailyLogin/DailyLoginTrack.h"
#include "UI/Activity/Track/Treasure/TreasureTrack.h"
#include "Kismet/GameplayStatics.h"
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
	// 从SaveGame系统获取或创建玩家记录
	FString SaveSlotName = FString::Printf(TEXT("DailyLogin_%d"), ActivityID);
	
	// 尝试加载现有存档
	UDailyLoginSaveGame* SaveGameInstance = Cast<UDailyLoginSaveGame>(UGameplayStatics::LoadGameFromSlot(SaveSlotName, 0));
	
	if (!SaveGameInstance)
	{
		// 如果没有存档，创建新的
		SaveGameInstance = Cast<UDailyLoginSaveGame>(UGameplayStatics::CreateSaveGameObject(UDailyLoginSaveGame::StaticClass()));
		UE_LOG(LogTemp, Warning, TEXT("ActivitySubsystem: 创建新的存档实例 for ActivityID=%d"), ActivityID);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("ActivitySubsystem: 成功加载存档 for ActivityID=%d"), ActivityID);
	}
	
	// 检查是否已有该活动的记录
	if (!SaveGameInstance->ActivityRecords.Contains(ActivityID))
	{
		// 创建新的玩家记录，默认第一天可领取
		FPlayerLoginRecord NewRecord;
		NewRecord.ActivityID = ActivityID;
		NewRecord.PlayerID = TEXT("Player1"); // 临时使用固定玩家ID
		NewRecord.Progress = 0;  // 进度为0，表示第一天可领取
		NewRecord.CurrentClaimCount = 0;
		NewRecord.ClaimedHistoryMask = 0;
		NewRecord.LastClaimTimestamp = 0;
		NewRecord.LastUpdateTime = FDateTime::Now();
		
		SaveGameInstance->ActivityRecords.Add(ActivityID, NewRecord);
		UE_LOG(LogTemp, Warning, TEXT("ActivitySubsystem: 为ActivityID=%d创建新的玩家记录，默认第一天可领取"), ActivityID);
		UE_LOG(LogTemp, Warning, TEXT("ActivitySubsystem: 新记录初始状态 - Progress=%d, CurrentClaimCount=%d"), NewRecord.Progress, NewRecord.CurrentClaimCount);
		
		// 立即保存新创建的记录，确保.sav文件被创建
		FString NewSaveSlotName = FString::Printf(TEXT("DailyLogin_%d"), ActivityID);
		UGameplayStatics::SaveGameToSlot(SaveGameInstance, NewSaveSlotName, 0);
		UE_LOG(LogTemp, Warning, TEXT("ActivitySubsystem: 主动保存新创建的记录到磁盘"));
	}
	
	// 保存引用以便后续使用
	CachedSaveGame = SaveGameInstance;
	
	return SaveGameInstance->ActivityRecords[ActivityID];
}

void UActivitySubsystem::SavePlayerRecord(int32 ActivityID)
{
	if (CachedSaveGame)
	{
		FString SaveSlotName = FString::Printf(TEXT("DailyLogin_%d"), ActivityID);
		UGameplayStatics::SaveGameToSlot(CachedSaveGame, SaveSlotName, 0);
		UE_LOG(LogTemp, Warning, TEXT("ActivitySubsystem: 成功保存ActivityID=%d的玩家记录"), ActivityID);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("ActivitySubsystem: 无法保存玩家记录，CachedSaveGame为空"));
	}
}

TArray<FDailyLoginConfigRow*> UActivitySubsystem::GetDailyLoginConfigs(int32 ActivityID) const
{
	// 从DailyLoginConfig表加载指定ActivityID的所有配置
	FString ConfigPath = TEXT("/Game/UI/Activity/Data/DT_DailyLoginConfigRow");
	UDataTable* ConfigTable = LoadObject<UDataTable>(nullptr, *ConfigPath);
	
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
	UDataTable* ConfigTable = LoadObject<UDataTable>(nullptr, *ConfigPath);
	
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
	// 获取玩家记录
	FPlayerLoginRecord& PlayerRecord = GetOrInitPlayerRecord(ActivityID);
	
	// 检查是否可以领取
	// Progress=0表示第一天可领取，Progress=1表示第二天可领取，以此类推
	if (DayIndex <= PlayerRecord.Progress)
	{
		UE_LOG(LogTemp, Warning, TEXT("ActivitySubsystem: 第%d天已经领取过了"), DayIndex);
		return false;
	}
	
	if (DayIndex > PlayerRecord.Progress + 1)
	{
		UE_LOG(LogTemp, Warning, TEXT("ActivitySubsystem: 只能领取连续的天数，当前进度%d，请求领取第%d天"), PlayerRecord.Progress, DayIndex);
		return false;
	}
	
	// 更新玩家记录
	PlayerRecord.Progress = DayIndex;
	PlayerRecord.CurrentClaimCount++;
	PlayerRecord.SetDayClaimed(DayIndex, true);
	PlayerRecord.LastClaimTimestamp = FDateTime::Now().ToUnixTimestamp();
	PlayerRecord.LastUpdateTime = FDateTime::Now();
	
	// 保存到磁盘
	SavePlayerRecord(ActivityID);
	
	// 广播数据变更事件
	OnActivityDataChanged.Broadcast();
	
	UE_LOG(LogTemp, Warning, TEXT("ActivitySubsystem: 成功领取第%d天奖励，当前进度:%d (表示第%d天可领取)"), DayIndex, PlayerRecord.Progress, PlayerRecord.Progress + 1);
	UE_LOG(LogTemp, Warning, TEXT("ActivitySubsystem: 第一天奖励现在可领取，Progress=%d"), PlayerRecord.Progress);
	return true;
}

void UActivitySubsystem::Cheat_JumpToDay(int32 ActivityID, int32 NewDay)
{
	// 获取玩家记录
	FPlayerLoginRecord& PlayerRecord = GetOrInitPlayerRecord(ActivityID);
	
	// 更新进度
	int32 OldProgress = PlayerRecord.Progress;
	// 用户输入的NewDay是想要跳转到的可领取天数
	// Progress应该比可领取天数少1（Progress=0表示第一天可领取）
	PlayerRecord.Progress = NewDay - 1;
	PlayerRecord.LastUpdateTime = FDateTime::Now();
	
	// 更新领取历史（标记前面已领取的天数）
	for (int32 Day = 1; Day < NewDay && Day <= 32; Day++)
	{
		PlayerRecord.SetDayClaimed(Day, true);
	}
	PlayerRecord.CurrentClaimCount = NewDay - 1;
	
	// 保存到磁盘
	SavePlayerRecord(ActivityID);
	
	// 广播数据变更事件
	OnActivityDataChanged.Broadcast();
	
	UE_LOG(LogTemp, Warning, TEXT("ActivitySubsystem: 作弊跳转 - ActivityID=%d 从Progress=%d跳转到Progress=%d (第%d天可领取)"), 
		ActivityID, OldProgress, PlayerRecord.Progress, NewDay);
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

const FItemDetailRow* UActivitySubsystem::GetItemDetail(int32 ItemID) const
{
	// 从DT_ItemDetailRow表加载指定ItemID的物品详情
	FString ConfigPath = TEXT("/Script/Engine.DataTable'/Game/UI/Activity/Data/DT_ItemDetailRow.DT_ItemDetailRow'");
	UE_LOG(LogTemp, Warning, TEXT("🔍 GetItemDetail: 尝试加载路径 %s"), *ConfigPath);
	
	UDataTable* ConfigTable = LoadObject<UDataTable>(nullptr, *ConfigPath);
	
	if (ConfigTable)
	{
		UE_LOG(LogTemp, Warning, TEXT("✅ 成功加载DT_ItemDetailRow DataTable"));
		static const FString ContextString(TEXT("ActivitySubsystem"));
		TArray<FItemDetailRow*> AllRows;
		ConfigTable->GetAllRows<FItemDetailRow>(ContextString, AllRows);
		
		UE_LOG(LogTemp, Warning, TEXT("📊 DataTable包含 %d 条记录"), AllRows.Num());
		
		for (FItemDetailRow* Row : AllRows)
		{
			if (Row)
			{
				UE_LOG(LogTemp, Warning, TEXT("📋 记录: ItemID=%d, Name=%s"), Row->ItemID, *Row->ItemName.ToString());
				UE_LOG(LogTemp, Warning, TEXT("   ItemIcon IsValid: %s"), Row->ItemIcon.IsValid() ? TEXT("是") : TEXT("否"));
				UE_LOG(LogTemp, Warning, TEXT("   ItemIcon IsNull: %s"), Row->ItemIcon.IsNull() ? TEXT("是") : TEXT("否"));
				UE_LOG(LogTemp, Warning, TEXT("   ItemIcon IsPending: %s"), Row->ItemIcon.IsPending() ? TEXT("是") : TEXT("否"));
				
				if (Row->ItemID == ItemID)
				{
					UE_LOG(LogTemp, Warning, TEXT("✅ 找到匹配的ItemID: %d"), ItemID);
					UE_LOG(LogTemp, Warning, TEXT("   匹配记录的ItemIcon状态:"));
					UE_LOG(LogTemp, Warning, TEXT("   IsValid: %s"), Row->ItemIcon.IsValid() ? TEXT("是") : TEXT("否"));
					UE_LOG(LogTemp, Warning, TEXT("   IsNull: %s"), Row->ItemIcon.IsNull() ? TEXT("是") : TEXT("否"));
					UE_LOG(LogTemp, Warning, TEXT("   IsPending: %s"), Row->ItemIcon.IsPending() ? TEXT("是") : TEXT("否"));
					return Row;
				}
			}
		}
		UE_LOG(LogTemp, Error, TEXT("❌ 未找到ItemID: %d 的记录"), ItemID);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("❌ 无法加载DT_ItemDetailRow DataTable，路径: %s"), *ConfigPath);
	}
	
	return nullptr;
}
