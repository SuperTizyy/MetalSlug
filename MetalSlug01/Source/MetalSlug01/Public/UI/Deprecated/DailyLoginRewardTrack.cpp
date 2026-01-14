/*
// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/Activity/DailyLogin/Track/DailyLoginRewardTrack.h"
#include "UI/Activity/DailyLogin/Item/DailyLoginRewardItem.h"


void UDailyLoginRewardTrack::RefreshState()
{
	for (auto* Item : Items)
	{
		auto* LoginItem = Cast<UDailyLoginRewardItem>(Item);
		if (!LoginItem) continue;

		if (LoginItem->DayIndex < CurrentLoginDay)
		{
			Item->State = ERewardState::Claimed;
		}
		else if (LoginItem->DayIndex == CurrentLoginDay)
		{
			Item->State = ERewardState::Claimable;
		}
		else
		{
			Item->State = ERewardState::Locked;
		}
	}
}
*/
