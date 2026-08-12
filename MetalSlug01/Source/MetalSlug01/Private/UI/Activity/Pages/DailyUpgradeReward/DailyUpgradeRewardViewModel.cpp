// ==========================================
// UDailyUpgradeRewardViewModel 实现
// ==========================================
#include "UI/Activity/Pages/DailyUpgradeReward/DailyUpgradeRewardViewModel.h"
#include "Systems/Activity/UpgradeActivitySubsystem.h"
#include "Tools/UpgradeActivitySaveModifier.h"
#include "Data/ActivitySaveGame.h"
#include "Logs/MetalSlugLogChannels.h"
#include "Engine/Engine.h"
#include "Engine/World.h"

void UDailyUpgradeRewardViewModel::Bind(UUpgradeActivitySubsystem* InSubsystem)
{
	Subsystem = InSubsystem;
}

void UDailyUpgradeRewardViewModel::Unbind()
{
	// 【v213 大厂架构】显式销毁 SaveModifier (避免内存泄漏)
	if (CachedSaveModifier)
	{
		CachedSaveModifier->DestroyModifier();
		CachedSaveModifier = nullptr;
	}
	Subsystem.Reset();
}

int32 UDailyUpgradeRewardViewModel::GetCurrentDay() const
{
	if (UUpgradeActivitySubsystem* Sub = Subsystem.Get())
	{
		return Sub->GetCurrentDayIndex();
	}
	return 0;
}

int32 UDailyUpgradeRewardViewModel::GetTotalDays() const
{
	// 默认升级活动有 7 天; 后续如需动态从 DT 读取, 改为 Sub 调用
	return 7;
}

bool UDailyUpgradeRewardViewModel::IsCurrentDayClaimed() const
{
	if (UUpgradeActivitySubsystem* Sub = Subsystem.Get())
	{
		return Sub->IsRewardClaimed(GetCurrentDay());
	}
	return false;
}

int32 UDailyUpgradeRewardViewModel::GetCurrentExperience() const
{
	if (UUpgradeActivitySubsystem* Sub = Subsystem.Get())
	{
		return Sub->GetCurrentExperience();
	}
	return 0;
}

int32 UDailyUpgradeRewardViewModel::GetCurrentRewardIconIndex() const
{
	if (UUpgradeActivitySubsystem* Sub = Subsystem.Get())
	{
		return Sub->GetCurrentRewardIconIndex();
	}
	return 0;
}

bool UDailyUpgradeRewardViewModel::IsCurrentDayLocked() const
{
	// 占位实现: 当前总是未锁定, 后续按业务规则扩展
	// 真实业务可能基于 真实时间 vs 配置时间 比较
	return false;
}

bool UDailyUpgradeRewardViewModel::CanClaimToday() const
{
	// 简化规则: 未领取 且 未锁定 即可领取
	return !IsCurrentDayClaimed() && !IsCurrentDayLocked();
}

// ==========================================
// 【v213 新增】Page 提交调试数据接口实现
// ==========================================

int32 UDailyUpgradeRewardViewModel::GetMaxAvailableDay() const
{
	UUpgradeActivitySubsystem* Sub = Subsystem.Get();
	if (!Sub)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[DailyUpgradeRewardViewModel] GetMaxAvailableDay: Subsystem 未 Bind, 返回 0"));
		return 0;
	}

	const int32 MaxDay = Sub->GetMaxRecordDate();
	if (MaxDay <= 0)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[DailyUpgradeRewardViewModel] GetMaxAvailableDay: AllRecords 为空, 返回 0"));
	}
	return MaxDay;
}

int32 UDailyUpgradeRewardViewModel::GetCurrentExpByDay(int32 Day) const
{
	// 【零兜底】入参校验
	if (Day < 1 || Day > 5)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[DailyUpgradeRewardViewModel] GetCurrentExpByDay: Day=%d 超出合法范围 [1,5]"), Day);
		return 0;
	}

	UUpgradeActivitySubsystem* Sub = Subsystem.Get();
	if (!Sub)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[DailyUpgradeRewardViewModel] GetCurrentExpByDay: Subsystem 未 Bind, 返回 0"));
		return 0;
	}

	const FUpgradeRewardSaveRecord* RecordPtr = Sub->GetRecordByDate(Day);
	if (!RecordPtr)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[DailyUpgradeRewardViewModel] GetCurrentExpByDay: RecordDate=%d 的记录不存在, 返回 0"), Day);
		return 0;
	}

	return RecordPtr->CurrentExperience;
}

// ==========================================
// 【v214 新增】私有 helper: 顺序创建 MaxRecordDate+1 ~ SelectedDay 的所有中间记录
// ==========================================
//
// 大厂架构 (单一真理 + 职责集中):
//   - SelectedDay 是绝对天 (1=day1, 2=day2, ...)
//   - 若 SelectedDay > MaxRecordDate, 从 MaxRecordDate+1 到 SelectedDay 依次创建空记录
//   - 不允许 Page 直接调 SaveModifier.CreateNewRecord, 必须经此 helper
//   - bInheritPrevious=false, 创建空记录 (业务含义是"解锁", 不是"补做")
//   - 多次创建均 bAutoSave=false, 由调用方的最后一次 Modify 统一触发 ForceRefresh
//
// 零兜底:
//   - CachedSaveModifier 未 Init → Log Error + return false
//   - 任意一天 CreateNewRecord 失败 → Log Error + return false (不吞错)
//
bool UDailyUpgradeRewardViewModel::EnsureRecordChainReady(int32 SelectedDay)
{
	UUpgradeActivitySubsystem* Sub = Subsystem.Get();
	if (!Sub)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[DailyUpgradeRewardViewModel] EnsureRecordChainReady: Subsystem 未 Bind, 拒绝"));
		return false;
	}

	if (!CachedSaveModifier || !CachedSaveModifier->IsInitialized())
	{
		UE_LOG(LogTemp, Error,
			TEXT("[DailyUpgradeRewardViewModel] EnsureRecordChainReady: SaveModifier 未初始化, 拒绝"));
		return false;
	}

	const int32 MaxDay = Sub->GetMaxRecordDate();

	// 已存在 (SelectedDay <= MaxRecordDate) → 0 次创建, 直接 return true
	if (SelectedDay <= MaxDay)
	{
		UE_LOG(LogTemp, Verbose,
			TEXT("[DailyUpgradeRewardViewModel] EnsureRecordChainReady: SelectedDay=%d <= MaxRecordDate=%d, 无需创建"),
			SelectedDay, MaxDay);
		return true;
	}

	// 顺序创建 MaxDay+1 ~ SelectedDay (全部空记录, 不继承)
	for (int32 DayToCreate = MaxDay + 1; DayToCreate <= SelectedDay; ++DayToCreate)
	{
		const bool bCreated = CachedSaveModifier->CreateNewRecord(
			DayToCreate,
			/*bInheritPrevious=*/ false,
			/*bAutoSave=*/ false);
		if (!bCreated)
		{
			UE_LOG(LogTemp, Error,
				TEXT("[DailyUpgradeRewardViewModel] EnsureRecordChainReady: 创建 day=%d 记录失败, 中止后续创建"),
				DayToCreate);
			return false;
		}
		UE_LOG(LogTemp, Log,
			TEXT("[DailyUpgradeRewardViewModel] EnsureRecordChainReady: 成功创建 day=%d 空记录"),
			DayToCreate);
	}

	UE_LOG(LogTemp, Log,
		TEXT("[DailyUpgradeRewardViewModel] EnsureRecordChainReady: 完成, 创建了 %d 条记录, SelectedDay=%d 现已存在"),
		(SelectedDay - MaxDay), SelectedDay);
	return true;
}

bool UDailyUpgradeRewardViewModel::ModifyCurrentExperience(int32 SelectedDay, int32 NewExp)
{
	// 【零兜底】第 1 层: 入参合法性校验
	if (SelectedDay < 1 || SelectedDay > 5)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[DailyUpgradeRewardViewModel] ModifyCurrentExperience: SelectedDay=%d 超出合法范围 [1,5]"), SelectedDay);
		return false;
	}

	if (NewExp < 0)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[DailyUpgradeRewardViewModel] ModifyCurrentExperience: NewExp=%d 为负数, 拒绝"), NewExp);
		return false;
	}

	// 【零兜底】第 2 层: Subsystem 必检
	UUpgradeActivitySubsystem* Sub = Subsystem.Get();
	if (!Sub)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[DailyUpgradeRewardViewModel] ModifyCurrentExperience: Subsystem 未 Bind, 拒绝"));
		return false;
	}

	// 【v214 业务规则】第 3 层: 解析目标 RecordDate
	// 语义: ComboBoxString_SelectedDay = 绝对天数 (1=N day1, 2=N day2, ...)
	// 若 SelectedDay > MaxRecordDate, 从 MaxRecordDate+1 到 SelectedDay 依次创建所有中间记录 (序号顺延)
	const int32 MaxDay = Sub->GetMaxRecordDate();
	const int32 TargetRecordDate = SelectedDay;

	UE_LOG(LogTemp, Log,
		TEXT("[DailyUpgradeRewardViewModel] ModifyCurrentExperience: 语义=绝对天 (SelectedDay=%d → TargetRecordDate=%d, 当前MaxRecordDate=%d)"),
		SelectedDay, TargetRecordDate, MaxDay);

	// 【v213 大厂架构】第 4 层: SaveModifier 生命周期集中管理
	if (!CachedSaveModifier)
	{
		// 单例化: 第一次调用才 NewObject
		CachedSaveModifier = NewObject<UUpgradeActivitySaveModifier>(this);
	}

	if (!CachedSaveModifier)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[DailyUpgradeRewardViewModel] ModifyCurrentExperience: NewObject<UUpgradeActivitySaveModifier> 失败"));
		return false;
	}

	// 初始化检查: 已初始化则复用, 未初始化则初始化
	if (!CachedSaveModifier->IsInitialized())
	{
		UWorld* World = nullptr;
		if (GEngine)
		{
			// 取首个 PIE/Game World 作为 WorldContext
			for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
			{
				if (Ctx.World() && Ctx.World()->IsGameWorld())
				{
					World = Ctx.World();
					break;
				}
			}
		}

		if (!World)
		{
			UE_LOG(LogTemp, Error,
				TEXT("[DailyUpgradeRewardViewModel] ModifyCurrentExperience: 无法获取 Game World, 拒绝"));
			return false;
		}

		if (!CachedSaveModifier->InitializeModifier(World, Sub))
		{
			UE_LOG(LogTemp, Error,
				TEXT("[DailyUpgradeRewardViewModel] ModifyCurrentExperience: InitializeModifier 失败"));
			return false;
		}
	}

	// 【v214 新增】第 4.5 层: 确保 SelectedDay 记录链已就绪 (绝对天语义: 若 SelectedDay > MaxDay, 依次创建中间记录)
	if (!EnsureRecordChainReady(SelectedDay))
	{
		UE_LOG(LogTemp, Error,
			TEXT("[DailyUpgradeRewardViewModel] ModifyCurrentExperience: EnsureRecordChainReady 失败 (SelectedDay=%d)"),
			SelectedDay);
		return false;
	}

	// 第 5 层: 调用 SaveModifier 业务方法 (内部已触发 OnGlobalRefresh)
	const bool bSuccess = CachedSaveModifier->ModifyCurrentExperience(TargetRecordDate, NewExp, /*bAutoSave=*/true);
	if (!bSuccess)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[DailyUpgradeRewardViewModel] ModifyCurrentExperience: SaveModifier.ModifyCurrentExperience 失败 (RecordDate=%d, NewExp=%d)"),
			TargetRecordDate, NewExp);
		return false;
	}

	// 【vXXX.2 反向伤害已删除】原代码在此处"提交经验值后重置 ChestClaimStatus"
	//   业务解释 (已纠正): Debug 提交按钮是 "改经验 / 改任务次数", 与 "玩家是否点过领奖按钮" 是正交两件事.
	//   ViewModel 不该越权修改领进度 — 那是 ClaimChest 的职责 (Subsystem 直接落盘).
	//   详见 v228 重构日志.

	UE_LOG(LogTemp, Log,
		TEXT("[DailyUpgradeRewardViewModel] ModifyCurrentExperience: 提交成功 (SelectedDay=%d → TargetRecordDate=%d, NewExp=%d)"),
		SelectedDay, TargetRecordDate, NewExp);
	return true;
}

// ==========================================
// 【v213.1 新增】任务完成次数写入实现
// ==========================================

bool UDailyUpgradeRewardViewModel::ModifyAllTaskCounts(int32 SelectedDay, int32 Task1Count, int32 Task2Count, int32 Task3Count)
{
	// 【零兜底】第 1 层: 入参合法性校验
	// SelectedDay: 1~5 合法
	if (SelectedDay < 1 || SelectedDay > 5)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[DailyUpgradeRewardViewModel] ModifyAllTaskCounts: SelectedDay=%d 超出合法范围 [1,5]"),
			SelectedDay);
		return false;
	}

	// Task1/2/3Count: 0~9 合法 (ComboBox 约束, 但 C++ 层必须重校验)
	if (Task1Count < 0 || Task1Count > 9 ||
		Task2Count < 0 || Task2Count > 9 ||
		Task3Count < 0 || Task3Count > 9)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[DailyUpgradeRewardViewModel] ModifyAllTaskCounts: TaskCount 超出合法范围 [0,9] (Task1=%d, Task2=%d, Task3=%d)"),
			Task1Count, Task2Count, Task3Count);
		return false;
	}

	// 【零兜底】第 2 层: Subsystem 必检
	UUpgradeActivitySubsystem* Sub = Subsystem.Get();
	if (!Sub)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[DailyUpgradeRewardViewModel] ModifyAllTaskCounts: Subsystem 未 Bind, 拒绝"));
		return false;
	}

	// 【业务规则】第 3 层: 解析目标 RecordDate (与 ModifyCurrentExperience 语义一致: 绝对天)
	const int32 MaxDay = Sub->GetMaxRecordDate();
	const int32 TargetRecordDate = SelectedDay;

	UE_LOG(LogTemp, Log,
		TEXT("[DailyUpgradeRewardViewModel] ModifyAllTaskCounts: 语义=绝对天 (SelectedDay=%d → TargetRecordDate=%d, 当前MaxRecordDate=%d)"),
		SelectedDay, TargetRecordDate, MaxDay);

	// 【大厂架构】第 4 层: SaveModifier 生命周期集中管理 (复用 ModifyCurrentExperience 逻辑)
	if (!CachedSaveModifier)
	{
		CachedSaveModifier = NewObject<UUpgradeActivitySaveModifier>(this);
	}

	if (!CachedSaveModifier)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[DailyUpgradeRewardViewModel] ModifyAllTaskCounts: NewObject<UUpgradeActivitySaveModifier> 失败"));
		return false;
	}

	// 初始化检查
	if (!CachedSaveModifier->IsInitialized())
	{
		UWorld* World = nullptr;
		if (GEngine)
		{
			for (const FWorldContext& Ctx : GEngine->GetWorldContexts())
			{
				if (Ctx.World() && Ctx.World()->IsGameWorld())
				{
					World = Ctx.World();
					break;
				}
			}
		}

		if (!World)
		{
			UE_LOG(LogTemp, Error,
				TEXT("[DailyUpgradeRewardViewModel] ModifyAllTaskCounts: 无法获取 Game World, 拒绝"));
			return false;
		}

		if (!CachedSaveModifier->InitializeModifier(World, Sub))
		{
			UE_LOG(LogTemp, Error,
				TEXT("[DailyUpgradeRewardViewModel] ModifyAllTaskCounts: InitializeModifier 失败"));
			return false;
		}
	}

	// 【v214 新增】第 4.5 层: 确保 SelectedDay 记录链已就绪 (绝对天语义)
	if (!EnsureRecordChainReady(SelectedDay))
	{
		UE_LOG(LogTemp, Error,
			TEXT("[DailyUpgradeRewardViewModel] ModifyAllTaskCounts: EnsureRecordChainReady 失败 (SelectedDay=%d)"),
			SelectedDay);
		return false;
	}

	// 第 5 层: 逐个调用 SaveModifier (原子性: SaveModifier 内部 OnGlobalRefresh 触发一次)
	// TaskIndex 0=任务一, 1=任务二, 2=任务三
	if (!CachedSaveModifier->ModifyTaskCompleteCount(TargetRecordDate, 0, Task1Count, /*bAutoSave=*/false))
	{
		UE_LOG(LogTemp, Error,
			TEXT("[DailyUpgradeRewardViewModel] ModifyAllTaskCounts: Task1 (index=0) 写入失败"));
		return false;
	}

	if (!CachedSaveModifier->ModifyTaskCompleteCount(TargetRecordDate, 1, Task2Count, /*bAutoSave=*/false))
	{
		UE_LOG(LogTemp, Error,
			TEXT("[DailyUpgradeRewardViewModel] ModifyAllTaskCounts: Task2 (index=1) 写入失败"));
		return false;
	}

	if (!CachedSaveModifier->ModifyTaskCompleteCount(TargetRecordDate, 2, Task3Count, /*bAutoSave=*/true))
	{
		UE_LOG(LogTemp, Error,
			TEXT("[DailyUpgradeRewardViewModel] ModifyAllTaskCounts: Task3 (index=2) 写入失败"));
		return false;
	}

	// 【vXXX.2 反向伤害已删除】同 ModifyCurrentExperience, 不再重置 ChestClaimStatus

	UE_LOG(LogTemp, Log,
		TEXT("[DailyUpgradeRewardViewModel] ModifyAllTaskCounts: 提交成功 (SelectedDay=%d → TargetRecordDate=%d, Task1=%d, Task2=%d, Task3=%d)"),
		SelectedDay, TargetRecordDate, Task1Count, Task2Count, Task3Count);
	return true;
}


// ==========================================
// 【v222 新增】一键重置所有页面进度
// ==========================================

bool UDailyUpgradeRewardViewModel::ResetAllActivityProgress()
{
	// 零兜底: Subsystem 未 Bind 直接报错, 不允许继续
	UUpgradeActivitySubsystem* Sub = Subsystem.Get();
	if (!Sub)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[DailyUpgradeRewardViewModel] ResetAllActivityProgress: Subsystem 未 Bind. "
				 "请先调 Bind() 连接到 UpgradeActivitySubsystem. 拒绝操作."));
		return false;
	}

	UE_LOG(LogTemp, Log,
		TEXT("[DailyUpgradeRewardViewModel] ResetAllActivityProgress: 委托 Subsystem 执行"));

	// 委托 Subsystem (大厂原则: 真正的清空 + 写盘 + 重建 day1 全部在 Subsystem)
	const bool bResult = Sub->ResetAllUpgradeActivityProgress();
	if (!bResult)
	{
		// Subsystem 内部已 Log Error (落盘失败 / day1 Config 缺失), 此处仅透传
		UE_LOG(LogTemp, Error,
			TEXT("[DailyUpgradeRewardViewModel] ResetAllActivityProgress: Subsystem 报告失败 (已 Log Error 详情在上方)."));
		return false;
	}

	UE_LOG(LogTemp, Log,
		TEXT("[DailyUpgradeRewardViewModel] ResetAllActivityProgress: 委托成功 (Subsystem 已广播 OnGlobalRefresh)"));
	return true;
}
