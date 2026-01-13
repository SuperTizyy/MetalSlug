// 4


#include "UI/Activity/Model/DailyLogin/DailyLoginDayItem.h"
#include "UI/Activity/Model/DailyLogin/DailyLoginTrack.h"

void UDailyLoginDayItem::ResetState()
{
	// 重置为未领取
	bClaimed = false;

	// 重置为不可领取
	// 是否可领取由 Track 决定
	bClaimable = false;
}


void UDailyLoginDayItem::RequestClaim()
{
	if (!OwnerTrack)
	{
		return;
	}

	OwnerTrack->TryClaimDay(DayIndex);
}