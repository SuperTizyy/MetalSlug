#include "UI/Activity/Core/ActivitySubsystem.h"
#include "UI/Activity/Core/RedDotManager.h"
#include "UI/Activity/Managers/ActivityTimeManager.h"
#include "Tools/DailyLoginSaveModifier.h"
#include "Kismet/GameplayStatics.h"
// 必须在 .cpp 引入完整定义: 前向声明无法用于 IsValid(Page) 的类型转换和 Page->GetName() 调用
// 仅在 .h 中前向声明是为了避免循环头文件依赖, .cpp 是真正使用类型的地方
#include "UI/Activity/Pages/DailyLogin/DailyLoginPage.h"

void UActivitySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// ================= 初始化管理器 =================
	RedDotManager = NewObject<URedDotManager>(this);
	ActivityTimeManager = NewObject<UActivityTimeManager>(this);

	// ================= 初始化动态存档修改器 =================
	SaveModifier = NewObject<UDailyLoginSaveModifier>(this);
	SaveModifier->InitializeModifier(this);
	// 注册控制台命令
	SaveModifier->RegisterConsoleCommands();
	UE_LOG(LogTemp, Warning, TEXT("ActivitySubsystem: 动态存档修改器初始化完成"));
}

void UActivitySubsystem::Deinitialize()
{
	// Subsystem 销毁时，管理器会随 GC 自动回收
	RedDotManager = nullptr;
	ActivityTimeManager = nullptr;

	// 清理动态存档修改器
	if (SaveModifier)
	{
		SaveModifier->DestroyModifier();
		SaveModifier = nullptr;
	}

	CachedSaveGame = nullptr;

	// 清空弱引用: Subsystem 即将销毁, 不再持有任何 Page 引用
	RegisteredLoginPage.Reset();

	Super::Deinitialize();
}

// ==================== 页面注册表实现 ====================

void UActivitySubsystem::RegisterLoginPage(UDailyLoginPage* Page)
{
	// 防御: 入参校验, UE 5.6 中应同时检查 PendingKill / Garbage
	if (!IsValid(Page))
	{
		UE_LOG(LogTemp, Error, TEXT("ActivitySubsystem::RegisterLoginPage: 入参 Page 无效, 已拒绝"));
		return;
	}

	// 已注册检查: 同时只允许一个主页面, 防止多个 Page 实例互踩
	if (RegisteredLoginPage.IsValid() && RegisteredLoginPage.Get() != Page)
	{
		UE_LOG(LogTemp, Error,
			TEXT("ActivitySubsystem::RegisterLoginPage: 已存在其他主页面实例 (%s), 新实例 (%s) 将覆盖"),
			*RegisteredLoginPage->GetName(), *Page->GetName());
	}

	// 弱引用赋值: 不增加引用计数, Page 销毁时 Get() 自动返回 nullptr
	RegisteredLoginPage = Page;
	UE_LOG(LogTemp, Log, TEXT("ActivitySubsystem: 主页面已注册 -> %s"), *Page->GetName());
}

void UActivitySubsystem::UnregisterLoginPage(UDailyLoginPage* Page)
{
	// 防御: 入参为 nullptr 直接返回, 避免误清空
	if (!Page)
	{
		return;
	}

	// 仅在指针匹配时清空, 防止误清空后续注册的新实例
	if (RegisteredLoginPage.Get() == Page)
	{
		RegisteredLoginPage.Reset();
		UE_LOG(LogTemp, Log, TEXT("ActivitySubsystem: 主页面已反注册 -> %s"), *Page->GetName());
	}
}

UDailyLoginPage* UActivitySubsystem::GetLoginPage() const
{
	// Get() 内部会检查 UObject 是否已被 GC, 无效时返回 nullptr
	return RegisteredLoginPage.Get();
}

URedDotManager* UActivitySubsystem::GetRedDotManager() const
{
	return RedDotManager;
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
		NewRecord.Progress = 1;  // 进度为1，表示第一天可领取
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
	// 在Progress范围内的未领取天数
	if (DayIndex > PlayerRecord.Progress)
	{
		UE_LOG(LogTemp, Warning, TEXT("ActivitySubsystem: 第%d天超出可领取范围，当前进度只到第%d天"), DayIndex, PlayerRecord.Progress);
		return false;
	}
	
	if (PlayerRecord.IsDayClaimed(DayIndex))
	{
		UE_LOG(LogTemp, Warning, TEXT("ActivitySubsystem: 第%d天已经领取过了"), DayIndex);
		return false;
	}
	
	// 更新玩家记录
	PlayerRecord.SetDayClaimed(DayIndex, true);
	if (!PlayerRecord.ClaimedDays.Contains(DayIndex))
	{
		PlayerRecord.ClaimedDays.Add(DayIndex);
	}
	PlayerRecord.CurrentClaimCount++;
	
	// 智能Progress更新逻辑：
	// 1. 如果是按顺序领取（DayIndex == Progress + 1），则更新Progress
	// 2. 如果是跳跃式领取（通过作弊跳转），保持原有Progress不变
	if (DayIndex == PlayerRecord.Progress + 1)
	{
		PlayerRecord.Progress = DayIndex;
		UE_LOG(LogTemp, Warning, TEXT("ActivitySubsystem: 顺序领取第%d天，更新Progress为%d"), DayIndex, PlayerRecord.Progress);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("ActivitySubsystem: 跳跃式领取第%d天，保持Progress=%d不变"), DayIndex, PlayerRecord.Progress);
	}
	
	PlayerRecord.LastClaimTimestamp = FDateTime::Now().ToUnixTimestamp();
	PlayerRecord.LastUpdateTime = FDateTime::Now();
	
	// 保存到磁盘
	SavePlayerRecord(ActivityID);
	
	// 广播数据变更事件
	OnActivityDataChanged.Broadcast();
	
	UE_LOG(LogTemp, Warning, TEXT("ActivitySubsystem: 成功领取第%d天奖励，当前已领取天数:%d"), DayIndex, PlayerRecord.CurrentClaimCount);
	return true;
}

void UActivitySubsystem::Cheat_JumpToDay(int32 ActivityID, int32 NewDay)
{
	// 获取玩家记录
	FPlayerLoginRecord& PlayerRecord = GetOrInitPlayerRecord(ActivityID);
	
	// 跳转逻辑：将Progress设置为目标天数，表示前面的天数都已经可领取
	// 例如：JumpToDay(3) 表示第1、2、3天都可领取
	int32 OldProgress = PlayerRecord.Progress;
	PlayerRecord.Progress = NewDay;  // Progress表示已领取到第几天
	PlayerRecord.LastUpdateTime = FDateTime::Now();
	
	// 不自动标记为已领取，让用户可以选择性领取
	// 只更新进度，不改变已领取状态
	
	// 保存到磁盘
	SavePlayerRecord(ActivityID);
	
	// 广播数据变更事件
	OnActivityDataChanged.Broadcast();
	
	UE_LOG(LogTemp, Warning, TEXT("ActivitySubsystem: 作弊跳转 - ActivityID=%d 从Progress=%d跳转到Progress=%d (第1天到第%d天可领取)"), 
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
	FString ConfigPath = TEXT("/Game/UI/Activity/Data/DT_ItemDetailRow");
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
		
		UE_LOG(LogTemp, Warning, TEXT("❌ 未找到ItemID: %d 的记录"), ItemID);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("❌ 无法加载ItemDetail DataTable: %s"), *ConfigPath);
	}
	
	return nullptr;
}

const FTreasureBoxItemRow* UActivitySubsystem::GetTreasureBoxItem(int32 BoxID) const
{
	// 从TreasureBoxItemRow表加载指定BoxID的宝箱物品配置
	FString ConfigPath = TEXT("/Game/UI/Activity/Data/DT_TreasureBoxItemRow");
	UE_LOG(LogTemp, Warning, TEXT("🔍 GetTreasureBoxItem: 尝试加载路径 %s"), *ConfigPath);
	
	UDataTable* ConfigTable = LoadObject<UDataTable>(nullptr, *ConfigPath);
	
	if (ConfigTable)
	{
		UE_LOG(LogTemp, Warning, TEXT("✅ 成功加载DT_TreasureBoxItemRow DataTable"));
		static const FString ContextString(TEXT("ActivitySubsystem"));
		TArray<FTreasureBoxItemRow*> AllRows;
		ConfigTable->GetAllRows<FTreasureBoxItemRow>(ContextString, AllRows);
		
		UE_LOG(LogTemp, Warning, TEXT("📊 TreasureBox DataTable包含 %d 条记录"), AllRows.Num());
		
		for (FTreasureBoxItemRow* Row : AllRows)
		{
			if (Row)
			{
				UE_LOG(LogTemp, Warning, TEXT("📋 宝箱记录: BoxID=%d, ItemID=%d"), Row->BoxID, Row->ItemID);
				UE_LOG(LogTemp, Warning, TEXT("   BoxIcon IsValid: %s"), Row->BoxIcon.IsValid() ? TEXT("是") : TEXT("否"));
				UE_LOG(LogTemp, Warning, TEXT("   BoxIcon IsNull: %s"), Row->BoxIcon.IsNull() ? TEXT("是") : TEXT("否"));
				UE_LOG(LogTemp, Warning, TEXT("   BoxIcon IsPending: %s"), Row->BoxIcon.IsPending() ? TEXT("是") : TEXT("否"));
				
				if (Row->BoxID == BoxID)
				{
					UE_LOG(LogTemp, Warning, TEXT("✅ 找到匹配的BoxID: %d"), BoxID);
					UE_LOG(LogTemp, Warning, TEXT("   匹配记录的BoxIcon状态:"));
					UE_LOG(LogTemp, Warning, TEXT("   IsValid: %s"), Row->BoxIcon.IsValid() ? TEXT("是") : TEXT("否"));
					UE_LOG(LogTemp, Warning, TEXT("   IsNull: %s"), Row->BoxIcon.IsNull() ? TEXT("是") : TEXT("否"));
					UE_LOG(LogTemp, Warning, TEXT("   IsPending: %s"), Row->BoxIcon.IsPending() ? TEXT("是") : TEXT("否"));
					return Row;
				}
			}
		}
		
		UE_LOG(LogTemp, Warning, TEXT("❌ 未找到BoxID: %d 的记录"), BoxID);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("❌ 无法加载TreasureBoxItem DataTable: %s"), *ConfigPath);
	}
	
	return nullptr;
}

TArray<const FTreasureBoxItemRow*> UActivitySubsystem::GetTreasureBoxItemsByBoxID(int32 BoxID) const
{
	// 从TreasureBoxItemRow表加载指定BoxID的所有宝箱物品配置
	FString ConfigPath = TEXT("/Game/UI/Activity/Data/DT_TreasureBoxItemRow");
	UE_LOG(LogTemp, Warning, TEXT("🔍 GetTreasureBoxItemsByBoxID: 尝试加载路径 %s"), *ConfigPath);
	
	UDataTable* ConfigTable = LoadObject<UDataTable>(nullptr, *ConfigPath);
	
	TArray<const FTreasureBoxItemRow*> Result;
	
	if (ConfigTable)
	{
		UE_LOG(LogTemp, Warning, TEXT("✅ 成功加载DT_TreasureBoxItemRow DataTable"));
		static const FString ContextString(TEXT("ActivitySubsystem"));
		TMap<FName, uint8*> RowMap = ConfigTable->GetRowMap();
		
		UE_LOG(LogTemp, Warning, TEXT("📊 TreasureBox DataTable包含 %d 条记录"), RowMap.Num());
		
		for (const auto& Pair : RowMap)
		{
			const FTreasureBoxItemRow* Row = reinterpret_cast<const FTreasureBoxItemRow*>(Pair.Value);
			if (Row)
			{
				UE_LOG(LogTemp, Warning, TEXT("📋 宝箱记录: BoxID=%d, ItemID=%d"), Row->BoxID, Row->ItemID);
				
				if (Row->BoxID == BoxID)
				{
					UE_LOG(LogTemp, Warning, TEXT("✅ 找到匹配的BoxID: %d, ItemID: %d"), BoxID, Row->ItemID);
					Result.Add(Row);
				}
			}
		}
		
		UE_LOG(LogTemp, Warning, TEXT("📦 找到 %d 个BoxID=%d的宝箱物品记录"), Result.Num(), BoxID);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("❌ 无法加载TreasureBoxItem DataTable: %s"), *ConfigPath);
	}
	
	return Result;
}

// ==================== 动态存档修改器接口实现 ====================

// ==================== 动态存档修改器接口实现 ====================

UDailyLoginSaveModifier* UActivitySubsystem::GetSaveModifier() const
{
	return SaveModifier;
}

bool UActivitySubsystem::InitializeSaveModifier(UObject* WorldContext)
{
	if (!SaveModifier)
	{
		SaveModifier = NewObject<UDailyLoginSaveModifier>(this);
	}

	bool bSuccess = SaveModifier->InitializeModifier(WorldContext);
	if (bSuccess)
	{
		UE_LOG(LogTemp, Warning, TEXT("ActivitySubsystem: 动态存档修改器初始化成功"));
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("ActivitySubsystem: 动态存档修改器初始化失败"));
	}

	return bSuccess;
}

bool UActivitySubsystem::ModifyPlayerProgress(int32 ActivityID, int32 NewProgress, bool bAutoSave)
{
	if (!SaveModifier)
	{
		UE_LOG(LogTemp, Error, TEXT("ActivitySubsystem: 存档修改器未初始化"));
		return false;
	}

	bool bSuccess = SaveModifier->ModifyPlayerProgress(ActivityID, NewProgress, bAutoSave);
	if (bSuccess)
	{
		// 广播数据变更事件
		OnActivityDataChanged.Broadcast();
		UE_LOG(LogTemp, Warning, TEXT("ActivitySubsystem: 成功修改玩家进度 - ActivityID=%d, NewProgress=%d"), ActivityID, NewProgress);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("ActivitySubsystem: 修改玩家进度失败 - ActivityID=%d"), ActivityID);
	}

	return bSuccess;
}

bool UActivitySubsystem::ModifyDayClaimedStatus(int32 ActivityID, int32 DayIndex, bool bClaimed, bool bAutoSave)
{
	if (!SaveModifier)
	{
		UE_LOG(LogTemp, Error, TEXT("ActivitySubsystem: 存档修改器未初始化"));
		return false;
	}

	bool bSuccess = SaveModifier->ModifyDayClaimedStatus(ActivityID, DayIndex, bClaimed, bAutoSave);
	if (bSuccess)
	{
		// 广播数据变更事件
		OnActivityDataChanged.Broadcast();
		UE_LOG(LogTemp, Warning, TEXT("ActivitySubsystem: 成功修改天数领取状态 - ActivityID=%d, Day=%d, Claimed=%s"), 
			ActivityID, DayIndex, bClaimed ? TEXT("是") : TEXT("否"));
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("ActivitySubsystem: 修改天数领取状态失败 - ActivityID=%d"), ActivityID);
	}

	return bSuccess;
}

bool UActivitySubsystem::ModifyClaimedDays(int32 ActivityID, const TArray<int32>& ClaimedDays, bool bAutoSave)
{
	if (!SaveModifier)
	{
		UE_LOG(LogTemp, Error, TEXT("ActivitySubsystem: 存档修改器未初始化"));
		return false;
	}

	bool bSuccess = SaveModifier->ModifyClaimedDays(ActivityID, ClaimedDays, bAutoSave);
	if (bSuccess)
	{
		// 广播数据变更事件
		OnActivityDataChanged.Broadcast();
		UE_LOG(LogTemp, Warning, TEXT("ActivitySubsystem: 成功批量修改已领取天数 - ActivityID=%d, DaysCount=%d"), 
			ActivityID, ClaimedDays.Num());
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("ActivitySubsystem: 批量修改已领取天数失败 - ActivityID=%d"), ActivityID);
	}

	return bSuccess;
}

bool UActivitySubsystem::ResetPlayerRecord(int32 ActivityID, bool bAutoSave)
{
	if (!SaveModifier)
	{
		UE_LOG(LogTemp, Error, TEXT("ActivitySubsystem: 存档修改器未初始化"));
		return false;
	}

	bool bSuccess = SaveModifier->ResetPlayerRecord(ActivityID, bAutoSave);
	if (bSuccess)
	{
		// 广播数据变更事件
		OnActivityDataChanged.Broadcast();
		UE_LOG(LogTemp, Warning, TEXT("ActivitySubsystem: 成功重置玩家记录 - ActivityID=%d"), ActivityID);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("ActivitySubsystem: 重置玩家记录失败 - ActivityID=%d"), ActivityID);
	}

	return bSuccess;
}
