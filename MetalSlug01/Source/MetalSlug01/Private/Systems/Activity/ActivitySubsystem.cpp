#include "Systems/Activity/ActivitySubsystem.h"
#include "Systems/Activity/RedDotManager.h"
#include "Systems/Activity/ActivityDataTableService.h" // v231: UActivityDataTableService 定义
#include "Tools/DailyLoginSaveModifier.h"
#include "Kismet/GameplayStatics.h"
#include "Data/FActivityDataTableService.h" // 活动表统一加载入口 (替代硬编码路径)
// 必须在 .cpp 引入完整定义: 前向声明无法用于 IsValid(Page) 的类型转换和 Page->GetName() 调用
// 仅在 .h 中前向声明是为了避免循环头文件依赖, .cpp 是真正使用类型的地方
#include "UI/Activity/Pages/DailyLogin/DailyLoginPage.h"

void UActivitySubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
	Super::Initialize(Collection);

	// ================= 初始化管理器 =================
	RedDotManager = NewObject<URedDotManager>(this);

	// ================= 初始化 DataTable 服务 (v231: 强引用 GC 安全) =================
	// 必须在 RedDotManager 之后, 因为后续会有 DataTable 访问
	DataTableService = UActivityDataTableService::Create(this);

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

	// 释放 DataTable 服务 (UPROPERTY 引用清空, 内部 TStrongObjectPtr 随之析构, RemoveFromRoot)
	DataTableService = nullptr;

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

TArray<const FActivityInfoRow*> UActivitySubsystem::GetAllNavItems() const
{
	// v231 改造: 通过 UActivityDataTableService 走实例方法 (强引用 GC 安全)
	// 旧版 static 调用已删除, 零兜底 — Service 必然存活
	return DataTableService->GetService().GetRowsSafe<FActivityInfoRow>(
		ActivityDataTable::ActivityInfo,
		[](const FActivityInfoRow& Row) { return true; } // 不过滤,收集所有行
	);
}

const FActivityInfoRow* UActivitySubsystem::GetActivityInfo(int32 ActivityID) const
{
	// v231 改造: 通过 Service 实例方法
	return DataTableService->GetService().FindRowByIdSafe<FActivityInfoRow>(
		ActivityDataTable::ActivityInfo,
		[](const FActivityInfoRow& Row) { return Row.ActivityID; },
		ActivityID
	);
}

/**
 * @brief 构造活动存档槽位名(进程级隔离)
 * @param ActivityID 活动 ID
 * @return 槽位名字符串, 格式 "DailyLogin_<ActivityID>_<PID>"
 *
 * 包含进程 ID (PID) 防止多开客户端 / PIE 多端覆盖同一存档
 * 单一构造点: 所有 SaveGame 路径必须走此函数, 不允许硬编码
 */
FString UActivitySubsystem::BuildSaveSlotName(int32 ActivityID) const
{
	return FString::Printf(TEXT("DailyLogin_%d_%d"), ActivityID, FPlatformProcess::GetCurrentProcessId());
}

FPlayerLoginRecord& UActivitySubsystem::GetOrInitPlayerRecord(int32 ActivityID)
{
	// 从SaveGame系统获取或创建玩家记录
	FString SaveSlotName = BuildSaveSlotName(ActivityID);
	
	// 尝试加载现有存档
	UActivitySaveGame* SaveGameInstance = Cast<UActivitySaveGame>(UGameplayStatics::LoadGameFromSlot(SaveSlotName, 0));
	
	if (!SaveGameInstance)
	{
		// 如果没有存档，创建新的
		SaveGameInstance = Cast<UActivitySaveGame>(UGameplayStatics::CreateSaveGameObject(UActivitySaveGame::StaticClass()));
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
		FString NewSaveSlotName = BuildSaveSlotName(ActivityID);
		UGameplayStatics::SaveGameToSlot(SaveGameInstance, NewSaveSlotName, 0);
		UE_LOG(LogTemp, Warning, TEXT("ActivitySubsystem: 主动保存新创建的记录到磁盘"));
	}
	
	// 保存引用以便后续使用
	CachedSaveGame = SaveGameInstance;
	
	return SaveGameInstance->ActivityRecords[ActivityID];
}

/**
 * @brief 将 CachedSaveGame 中的玩家记录持久化到磁盘
 * @param ActivityID 活动 ID(用于构建槽位名)
 *
 * 依赖 CachedSaveGame 已由 GetOrInitPlayerRecord 加载/初始化
 * CachedSaveGame 为空时 Log Error 并跳过(零兜底 — 调用方必须先 Init)
 */
void UActivitySubsystem::SavePlayerRecord(int32 ActivityID)
{
	if (CachedSaveGame)
	{
		FString SaveSlotName = BuildSaveSlotName(ActivityID);
		UGameplayStatics::SaveGameToSlot(CachedSaveGame, SaveSlotName, 0);
		UE_LOG(LogTemp, Warning, TEXT("ActivitySubsystem: 成功保存ActivityID=%d的玩家记录"), ActivityID);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("ActivitySubsystem: 无法保存玩家记录，CachedSaveGame为空"));
	}
}

const TArray<const FDailyLoginConfigRow*> UActivitySubsystem::GetDailyLoginConfigs(int32 ActivityID) const
{
	// v231: 改返回 const T*, 从源头消除 reinterpret_cast 兜底
	// 强引用 GC 安全 + Service 单一真理源
	return DataTableService->GetService().GetRowsSafe<FDailyLoginConfigRow>(
		ActivityDataTable::DailyLoginConfig,
		[ActivityID](const FDailyLoginConfigRow& Row) { return Row.ActivityID == ActivityID; }
	);
}

const TArray<const FDailyLoginConfigRow*> UActivitySubsystem::GetRewardsByDay(int32 ActivityID, int32 Day) const
{
	// v231: 改返回 const T*, 从源头消除 reinterpret_cast 兜底
	return DataTableService->GetService().GetRowsSafe<FDailyLoginConfigRow>(
		ActivityDataTable::DailyLoginConfig,
		[ActivityID, Day](const FDailyLoginConfigRow& Row)
		{
			return Row.ActivityID == ActivityID && Row.DayIndex == Day;
		}
	);
}

/**
 * @brief 尝试领取指定天的奖励
 * @param ActivityID 活动 ID
 * @param DayIndex 目标天数(1-based)
 * @return 领取是否成功(超出进度 / 已领取 / 存档失败 → false)
 *
 * 三层校验: 进度范围校验 → 重复领取校验 → 顺序/跳跃分支
 * 顺序领取(DayIndex == Progress+1)→ Progress 推进
 * 跳跃领取(DayIndex > Progress+1)→ Progress 不变(防作弊穿透)
 * 成功时广播 OnActivityDataChanged 通知 UI 刷新
 */
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
	// v231 改造: 通过 Service 实例方法 (强引用 GC 安全)
	return DataTableService->GetService().FindRowByIdSafe<FItemDetailRow>(
		ActivityDataTable::ItemDetail,
		[](const FItemDetailRow& Row) { return Row.ItemID; },
		ItemID
	);
}

const FTreasureBoxItemRow* UActivitySubsystem::GetTreasureBoxItem(int32 BoxID) const
{
	// v231 改造: 通过 Service 实例方法
	return DataTableService->GetService().FindRowByIdSafe<FTreasureBoxItemRow>(
		ActivityDataTable::TreasureBoxItem,
		[](const FTreasureBoxItemRow& Row) { return Row.BoxID; },
		BoxID
	);
}

TArray<const FTreasureBoxItemRow*> UActivitySubsystem::GetTreasureBoxItemsByBoxID(int32 BoxID) const
{
	// v231 重构: 直接调 Service.GetRowsSafe, 复用 SSOT 真理源
	// 旧版手写 GetRowNames + FindRow + 三层 IsValid 防御全部删除 — 强引用保证 GC 安全
	return DataTableService->GetService().GetRowsSafe<FTreasureBoxItemRow>(
		ActivityDataTable::TreasureBoxItem,
		[BoxID](const FTreasureBoxItemRow& Row) { return Row.BoxID == BoxID; }
	);
}

// ==================== 动态存档修改器接口实现 ====================

UDailyLoginSaveModifier* UActivitySubsystem::GetSaveModifier() const
{
	return SaveModifier;
}

/**
 * @brief 初始化动态存档修改器(可重复调用, 幂等)
 * @param WorldContext 世界上下文(用于 SaveModifier 内部访问 World)
 * @return 初始化是否成功
 *
 * SaveModifier 为空时懒加载创建; 重复调用安全
 * 失败时 Log Error 但不中断上层流程(降级到无修改器模式)
 */
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
