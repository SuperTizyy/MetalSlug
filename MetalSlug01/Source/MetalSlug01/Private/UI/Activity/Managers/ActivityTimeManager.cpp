// 版权声明：在项目设置的描述页面填写您的版权信息。

/*
这个系统原本设计用于:
- 活动自动上下架 - 根据时间自动控制活动显示/隐藏
- 倒计时显示 - 在 UI 中显示活动开始/结束倒计时
- 状态提示 - 预告即将开始或即将结束的活动
- 红点系统 - 结合时间状态显示活动红点
- 运营维护 - 手动控制活动状态进行紧急维护
-------并未使用!!!!
*/

// ==========================================
// 头文件包含区
// ==========================================
#include "UI/Activity/Managers/ActivityTimeManager.h"
#include "UI/Activity/Core/ActivitySubsystem.h"
#include "Kismet/GameplayStatics.h"


// ==========================================
// 1. 初始化
// ==========================================

/**
 * UActivityTimeManager::InitializeManager
 *
 * 1. 缓存 ActivitySubsystem 弱引用
 * 2. 记录 LastRefreshTime
 * 3. 立即 RefreshAllActivityTimes
 */
void UActivityTimeManager::InitializeManager(UActivitySubsystem* InSubsystem)
{
	ActivitySubsystem = InSubsystem;
	LastRefreshTime = FPlatformTime::Seconds();
	RefreshAllActivityTimes();
}


// ==========================================
// 2. 公共接口
// ==========================================

/**
 * UActivityTimeManager::RefreshAllActivityTimes
 *
 * 1. 防御: ActivitySubsystem 无效时 Log 警告并返回
 * 2. 清空 TimeInfoCache
 * 3. 遍历 NavItems -> CalculateTimeInfoForActivity -> 缓存
 * 4. 更新 LastRefreshTime + 日志
 */
void UActivityTimeManager::RefreshAllActivityTimes()
{
	if (!ActivitySubsystem.IsValid())
	{
		UE_LOG(LogTemp, Warning, TEXT("ActivityTimeManager: ActivitySubsystem 未初始化"));
		return;
	}

	TimeInfoCache.Empty();

	// 获取所有导航项配置
	TArray<const FActivityInfoRow*> NavItems = ActivitySubsystem->GetAllNavItems();

	for (const FActivityInfoRow* Config : NavItems)
	{
		if (Config)
		{
			FActivityRuntimeState TimeInfo = CalculateTimeInfoForActivity(Config);
			TimeInfoCache.Add(Config->ActivityID, TimeInfo);
		}
	}

	LastRefreshTime = FPlatformTime::Seconds();
	UE_LOG(LogTemp, Log, TEXT("ActivityTimeManager: 刷新了 %d 个活动的时间状态"), NavItems.Num());
}


/**
 * UActivityTimeManager::GetActivityTimeInfo
 *
 * 1. 查 TimeInfoCache
 * 2. 找不到时返回默认 FActivityRuntimeState
 */
FActivityRuntimeState UActivityTimeManager::GetActivityTimeInfo(int32 ActivityId) const
{
	if (const FActivityRuntimeState* Info = TimeInfoCache.Find(ActivityId))
	{
		return *Info;
	}

	// 返回默认的时间信息
	FActivityRuntimeState DefaultInfo;
	return DefaultInfo;
}


/**
 * UActivityTimeManager::IsActivityAvailable
 *
 * 规则: CurrentStatus = Active 或 Upcoming, 且 bInPreNoticePeriod = true
 */
bool UActivityTimeManager::IsActivityAvailable(int32 ActivityId) const
{
	FActivityRuntimeState TimeInfo = GetActivityTimeInfo(ActivityId);
	return (TimeInfo.CurrentStatus == EActivityStatus::Active ||
			TimeInfo.CurrentStatus == EActivityStatus::Upcoming) &&
		   TimeInfo.bInPreNoticePeriod;
}


/**
 * UActivityTimeManager::GetAvailableActivities
 *
 * 遍历 TimeInfoCache, 收集所有 IsActivityAvailable = true 的 ID
 */
TArray<int32> UActivityTimeManager::GetAvailableActivities() const
{
	TArray<int32> AvailableActivities;

	for (const auto& Pair : TimeInfoCache)
	{
		if (IsActivityAvailable(Pair.Key))
		{
			AvailableActivities.Add(Pair.Key);
		}
	}

	return AvailableActivities;
}


/**
 * UActivityTimeManager::SetActivityStatusManually
 *
 * 1. 查 TimeInfoCache
 * 2. 找到时强制设置 CurrentStatus
 * 3. 日志记录
 */
void UActivityTimeManager::SetActivityStatusManually(int32 ActivityId, EActivityStatus Status)
{
	if (FActivityRuntimeState* Info = TimeInfoCache.Find(ActivityId))
	{
		Info->CurrentStatus = Status;
		UE_LOG(LogTemp, Log, TEXT("ActivityTimeManager: 手动设置活动 %d 状态为 %s"),
			   ActivityId, *UEnum::GetValueAsString(Status));
	}
}


/**
 * UActivityTimeManager::GetServerTime
 *
 * 当前实现: FDateTime::Now()（本地时间）
 * 未来: 接入真正的服务器时间 API 防止玩家改本地时间作弊
 */
FDateTime UActivityTimeManager::GetServerTime() const
{
	// 这里可以连接到真正的服务器时间 API
	// 目前使用本地时间作为示例
	return FDateTime::Now();
}


// ==========================================
// 3. 内部计算
// ==========================================

/**
 * UActivityTimeManager::CalculateTimeInfoForActivity
 *
 * 1. Config 空时返回默认
 * 2. 获取 CurrentTime + Status
 * 3. 计算 SecondsUntilStart / SecondsUntilEnd（防御 GetTicks>0）
 * 4. 判定 bInPreNotice / bInEndWarning
 * 5. Recurring: 计算 CycleIndex
 * 6. 装配 FActivityRuntimeState
 */
FActivityRuntimeState UActivityTimeManager::CalculateTimeInfoForActivity(const FActivityInfoRow* Config)
{
	if (!Config)
	{
		return FActivityRuntimeState();
	}

	FDateTime CurrentTime = GetServerTime();
	EActivityStatus Status = CalculateStatusByTimeControl(Config, CurrentTime);

	// 计算时间差
	float SecondsUntilStart = 0.0f;
	float SecondsUntilEnd = 0.0f;

	if (Config->StartTime.GetTicks() > 0)
	{
		FTimespan TimeDiff = Config->StartTime - CurrentTime;
		SecondsUntilStart = TimeDiff.GetTotalSeconds();
	}

	if (Config->EndTime.GetTicks() > 0)
	{
		FTimespan TimeDiff = Config->EndTime - CurrentTime;
		SecondsUntilEnd = TimeDiff.GetTotalSeconds();
	}

	// 计算提醒期状态
	bool bInPreNotice = false;
	bool bInEndWarning = false;

	if (SecondsUntilStart > 0 && SecondsUntilStart <= Config->PreNoticeTime.GetTotalSeconds())
	{
		bInPreNotice = true;
	}

	if (SecondsUntilEnd > 0 && SecondsUntilEnd <= Config->EndWarningTime.GetTotalSeconds())
	{
		bInEndWarning = true;
	}

	// 计算循环周期索引
	int32 CycleIndex = 0;
	if (Config->TimeControlType == ETimeControlType::Recurring && Config->CycleDuration.GetTicks() > 0)
	{
		FTimespan ElapsedTime = CurrentTime - Config->StartTime;
		CycleIndex = FMath::FloorToInt(ElapsedTime.GetTotalSeconds() / Config->CycleDuration.GetTotalSeconds());
	}

	FActivityRuntimeState TimeInfo;

	// 更新运行时字段
	TimeInfo.CurrentStatus = Status;
	TimeInfo.TimeUntilStart = SecondsUntilStart;
	TimeInfo.TimeUntilEnd = SecondsUntilEnd;
	TimeInfo.bInPreNoticePeriod = bInPreNotice;
	TimeInfo.bInEndWarningPeriod = bInEndWarning;
	TimeInfo.CurrentCycleIndex = CycleIndex;

	return TimeInfo;
}


/**
 * UActivityTimeManager::CalculateStatusByTimeControl
 *
 * 策略分发:
 * - FixedPeriod -> CalculateFixedPeriodStatus
 * - Recurring   -> CalculateRecurringStatus
 * - Manual      -> CalculateManualStatus
 * - Permanent   -> EActivityStatus::Active
 */
EActivityStatus UActivityTimeManager::CalculateStatusByTimeControl(const FActivityInfoRow* Config, const FDateTime& CurrentTime)
{
	switch (Config->TimeControlType)
	{
	case ETimeControlType::FixedPeriod:
		return CalculateFixedPeriodStatus(Config, CurrentTime);

	case ETimeControlType::Recurring:
		return CalculateRecurringStatus(Config, CurrentTime);

	case ETimeControlType::Manual:
		return CalculateManualStatus(Config);

	case ETimeControlType::Permanent:
	default:
		return EActivityStatus::Active;
	}
}


/**
 * UActivityTimeManager::CalculateFixedPeriodStatus
 *
 * 规则:
 * 1. 无时间限制 -> Active
 * 2. Now < Start -> Upcoming
 * 3. Now > End -> Ended
 * 4. End-Now < EndWarningTime -> EndingSoon
 * 5. 否则 Active
 */
EActivityStatus UActivityTimeManager::CalculateFixedPeriodStatus(const FActivityInfoRow* Config, const FDateTime& CurrentTime)
{
	if (Config->StartTime.GetTicks() == 0 && Config->EndTime.GetTicks() == 0)
	{
		return EActivityStatus::Active; // 无时间限制
	}

	if (CurrentTime < Config->StartTime)
	{
		return EActivityStatus::Upcoming;
	}

	if (CurrentTime > Config->EndTime)
	{
		return EActivityStatus::Ended;
	}

	// 检查是否即将结束
	FTimespan TimeLeft = Config->EndTime - CurrentTime;
	if (TimeLeft < Config->EndWarningTime)
	{
		return EActivityStatus::EndingSoon;
	}

	return EActivityStatus::Active;
}


/**
 * UActivityTimeManager::CalculateRecurringStatus
 *
 * 1. 无起始时间或周期 -> Active
 * 2. 计算 TotalCycles + CurrentCycle + CycleProgress
 * 3. CycleProgress < 0.8 -> Active
 * 4. 否则 -> EndingSoon
 */
EActivityStatus UActivityTimeManager::CalculateRecurringStatus(const FActivityInfoRow* Config, const FDateTime& CurrentTime)
{
	if (Config->StartTime.GetTicks() == 0 || Config->CycleDuration.GetTicks() == 0)
	{
		return EActivityStatus::Active;
	}

	// 计算当前处于哪个周期
	FTimespan ElapsedTime = CurrentTime - Config->StartTime;
	double TotalCycles = ElapsedTime.GetTotalSeconds() / Config->CycleDuration.GetTotalSeconds();
	int32 CurrentCycle = FMath::FloorToInt(TotalCycles);
	double CycleProgress = TotalCycles - CurrentCycle;

	// 在每个周期的前 80% 时间内为 Active，后 20% 为 EndingSoon
	if (CycleProgress < 0.8)
	{
		return EActivityStatus::Active;
	}
	else
	{
		return EActivityStatus::EndingSoon;
	}
}


/**
 * UActivityTimeManager::CalculateManualStatus
 *
 * 简单: bManualEnabled ? Active : Maintenance
 */
EActivityStatus UActivityTimeManager::CalculateManualStatus(const FActivityInfoRow* Config)
{
	return Config->bManualEnabled ? EActivityStatus::Active : EActivityStatus::Maintenance;
}
