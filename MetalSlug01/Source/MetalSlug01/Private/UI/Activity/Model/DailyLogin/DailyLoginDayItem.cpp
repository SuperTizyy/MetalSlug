// 4

#include "UI/Activity/Model/DailyLogin/DailyLoginDayItem.h"

void UDailyLoginDayItem::Init(int32 InDayIndex)
{
	DayIndex   = InDayIndex;
	bClaimed  = false;
	bCanClaim = false;
}

void UDailyLoginDayItem::UpdateClaimable(bool bInCanClaim)
{
	bCanClaim = bInCanClaim;
}

bool UDailyLoginDayItem::TryClaim()
{
	if (!bCanClaim || bClaimed)
	{
		return false;
	}

	bClaimed  = true;
	bCanClaim = false;
	return true;
}
