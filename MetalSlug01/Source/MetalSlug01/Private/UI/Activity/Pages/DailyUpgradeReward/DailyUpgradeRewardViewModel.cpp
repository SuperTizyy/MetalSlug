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

	// 【v213 业务规则】第 3 层: 解析目标 RecordDate
	// 规则: 优先用 SelectedDay; 若不存在该天记录, fallback 到 MaxRecordDate
	int32 TargetRecordDate = SelectedDay;
	const FUpgradeRewardSaveRecord* RecordPtr = Sub->GetRecordByDate(SelectedDay);
	if (!RecordPtr)
	{
		const int32 MaxDay = Sub->GetMaxRecordDate();
		if (MaxDay <= 0)
		{
			UE_LOG(LogTemp, Error,
				TEXT("[DailyUpgradeRewardViewModel] ModifyCurrentExperience: RecordDate=%d 不存在 且 AllRecords 为空, 拒绝 (不允许凭空创建)"),
				SelectedDay);
			return false;
		}
		UE_LOG(LogTemp, Warning,
			TEXT("[DailyUpgradeRewardViewModel] ModifyCurrentExperience: RecordDate=%d 不存在, fallback 到 MaxRecordDate=%d"),
			SelectedDay, MaxDay);
		TargetRecordDate = MaxDay;
	}

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

	// 第 5 层: 调用 SaveModifier 业务方法 (内部已触发 OnGlobalRefresh)
	const bool bSuccess = CachedSaveModifier->ModifyCurrentExperience(TargetRecordDate, NewExp, /*bAutoSave=*/true);
	if (!bSuccess)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[DailyUpgradeRewardViewModel] ModifyCurrentExperience: SaveModifier.ModifyCurrentExperience 失败 (RecordDate=%d, NewExp=%d)"),
			TargetRecordDate, NewExp);
		return false;
	}

	UE_LOG(LogTemp, Log,
		TEXT("[DailyUpgradeRewardViewModel] ModifyCurrentExperience: 提交成功 (SelectedDay=%d → TargetRecordDate=%d, NewExp=%d)"),
		SelectedDay, TargetRecordDate, NewExp);
	return true;
}
