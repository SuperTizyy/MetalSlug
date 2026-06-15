// ==========================================
// UDailyUpgradeRewardViewModel 实现
// ==========================================
#include "UI/Activity/Pages/DailyUpgradeReward/DailyUpgradeRewardViewModel.h"
#include "Systems/Activity/UpgradeActivitySubsystem.h"
#include "Logs/MetalSlugLogChannels.h"

void UDailyUpgradeRewardViewModel::Bind(UUpgradeActivitySubsystem* InSubsystem)
{
	Subsystem = InSubsystem;
}

void UDailyUpgradeRewardViewModel::Unbind()
{
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
