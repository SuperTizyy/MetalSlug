// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/Activity/DailyLogin/Page/ActivityDailyLoginPage.h"

void UActivityDailyLoginPage::OnPageShow_Implementation()
{
	if (!LoginTrack)
	{
		LoginTrack = NewObject<UDailyLoginRewardTrack>(this);
		// 构建 Items（Day1~Day7 + Treasure）
	}

	LoginTrack->RefreshState();
}

