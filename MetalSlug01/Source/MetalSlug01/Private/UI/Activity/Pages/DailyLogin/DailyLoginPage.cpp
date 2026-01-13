// 13

#include "UI/Activity/Pages/DailyLogin/DailyLoginPage.h"
#include "Components/ListView.h"
#include "UI/Activity/Model/DailyLogin/DailyLoginDayItem.h"


void UDailyLoginPage::OnPageShow_Implementation()
{
	Super::OnPageShow_Implementation();

	// 从 ActivitySubsystem 获取 Track（示意）
	LoginTrack = GetActivitySubsystem()->GetDailyLoginTrack();

	BuildList();
}

void UDailyLoginPage::BuildList()
{
	if (!LoginDayList || !LoginTrack)
	{
		return;
	}

	LoginDayList->ClearListItems();

	for (UDailyLoginDayItem* Item : LoginTrack->DayItems)
	{
		LoginDayList->AddItem(Item);
	}
}
