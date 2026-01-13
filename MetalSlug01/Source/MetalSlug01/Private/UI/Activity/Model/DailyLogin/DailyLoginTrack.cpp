// 5


#include "UI/Activity/Model/DailyLogin/DailyLoginTrack.h"
#include "UI/Activity/Model/DailyLogin/DailyLoginDayItem.h"

void UDailyLoginTrack::InitializeTrack()
{
    // 设置活动唯一 ID（用于 ActivitySubsystem / UI 索引）
    ActivityId = TEXT("DailyLogin");

    // 是否激活由服务器控制
    // Demo 阶段默认开启
    bIsActive = true;

    // 清空旧数据（防止重建）
    DayItems.Empty();

    /**
     * ⚠️ 注意：
     * 这里“只创建结构”，不判断可领取状态
     * 状态一律交给 RefreshTrack
     */

    const int32 TotalDays = 8; // ⚠️ 后续来自配置表

    for (int32 Day = 1; Day <= TotalDays; ++Day)
    {
        // 创建单日登录 Item（纯数据对象）
        UDailyLoginDayItem* DayItem = NewObject<UDailyLoginDayItem>(this);

        // 第几天
        DayItem->DayIndex = Day;

        // 默认状态：未到期
        DayItem->RewardState = EDailyRewardState::Incomplete;

        // 示例奖励（占位）
        FRewardData Reward;
        Reward.RewardId = Day;
        Reward.RewardName = FText::FromString(
            FString::Printf(TEXT("Day %d Reward"), Day)
        );
        Reward.Amount = 100 * Day;

        DayItem->Rewards.Add(Reward);

        DayItems.Add(DayItem);
    }

    // 初始化后统一刷新一次状态
    RefreshTrack();
}


void UDailyLoginTrack::RefreshTrack()
{
    /**
     * 核心规则：
     * - 已领取 → Claimed
     * - 当前登录天 → Claimable
     * - 未来天数 → Incomplete
     */

    for (UDailyLoginDayItem* Item : DayItems)
    {
        if (!Item)
        {
            continue;
        }

        if (Item->RewardState == EDailyRewardState::Claimed)
        {
            // 已领取永远保持
            continue;
        }

        if (Item->DayIndex == CurrentLoginDay)
        {
            Item->RewardState = EDailyRewardState::Claimable;
        }
        else if (Item->DayIndex < CurrentLoginDay)
        {
            // 漏领（未来可加补领规则）
            Item->RewardState = EDailyRewardState::Incomplete;
        }
        else
        {
            Item->RewardState = EDailyRewardState::Incomplete;
        }
    }
}


bool UDailyLoginTrack::TryClaimDay(int32 DayIndex)
{
    UDailyLoginDayItem* Item = GetDayItem(DayIndex);
    if (!Item)
    {
        return false;
    }

    // 只有 Claimable 才能领
    if (Item->RewardState != EDailyRewardState::Claimable)
    {
        return false;
    }

    // 标记为已领取
    Item->RewardState = EDailyRewardState::Claimed;

    // TODO：在这里发奖励 / 通知 TreasureTrack
    // GrantRewards(Item->Rewards);

    // 刷新整体状态
    RefreshTrack();

    return true;
}


UDailyLoginDayItem* UDailyLoginTrack::GetDayItem(int32 DayIndex) const
{
    for (UDailyLoginDayItem* Item : DayItems)
    {
        if (Item && Item->DayIndex == DayIndex)
        {
            return Item;
        }
    }
    return nullptr;
    
}

void UDailyLoginTrack::SetCurrentLoginDay(int32 NewDay)
{
    if (CurrentLoginDay == NewDay)
    {
        return;
    }

    CurrentLoginDay = NewDay;

    // 刷新自身状态
    RefreshTrack();

    // 通知外部
    OnLoginDayChanged.Broadcast(CurrentLoginDay);
}

