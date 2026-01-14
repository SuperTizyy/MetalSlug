// 5

#include "UI/Activity/Track/DailyLogin/DailyLoginTrack.h"
#include "UI/Activity/Model/DailyLogin/DailyLoginDayItem.h"

void UDailyLoginTrack::Initialize(int32 InTotalDays)
{
	DayItems.Empty();

	for (int32 Index = 0; Index < InTotalDays; ++Index)
	{
		UDailyLoginDayItem* Item = NewObject<UDailyLoginDayItem>(this);
		Item->Init(Index);
		DayItems.Add(Item);
	}

	LoginDays = 0;
	RefreshClaimable();
}

void UDailyLoginTrack::RefreshClaimable()
{
	for (UDailyLoginDayItem* Item : DayItems)
	{
		const bool bCanClaim =
			Item->GetDayIndex() < LoginDays &&
			!Item->IsClaimed();

		Item->UpdateClaimable(bCanClaim);
	}
}

bool UDailyLoginTrack::TryClaimDay(int32 DayIndex)
{
	if (!DayItems.IsValidIndex(DayIndex))
	{
		return false;
	}

	if (!DayItems[DayIndex]->TryClaim())
	{
		return false;
	}

	// 示例：领取后 +1 登录天数
	LoginDays++;
	RefreshClaimable();
	return true;
}
