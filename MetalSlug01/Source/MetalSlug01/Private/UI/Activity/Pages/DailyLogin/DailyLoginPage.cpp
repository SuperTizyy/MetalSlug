// 13

#include "UI/Activity/Pages/DailyLogin/DailyLoginPage.h"
#include "Components/ListView.h"
#include "Components/HorizontalBox.h"

#include "UI/Activity/Core/ActivitySubsystem.h"
#include "UI/Activity/Track/DailyLogin/DailyLoginTrack.h"
#include "UI/Activity/Model/DailyLogin/DailyLoginDayItem.h"
#include "UI/Activity/Track/Treasure/TreasureTrack.h"
#include "UI/Activity/Model/Treasure/TreasureBoxItem.h"

#include "UI/Activity/Pages/DailyLogin/Widgets/TreasureBoxWidget.h"

void UDailyLoginPage::OnPageShow_Implementation()
{
	Super::OnPageShow_Implementation();

	// 从 ActivitySubsystem 获取 Track
	UActivitySubsystem* Subsystem = GetGameInstance()->GetSubsystem<UActivitySubsystem>();
	if (!Subsystem)
	{
		return;
	}

	LoginTrack   = Subsystem->GetDailyLoginTrack();
	TreasureTrack = Subsystem->GetTreasureTrack();

	if (!LoginTrack || !TreasureTrack)
	{
		return;
	}

	BuildLoginList();
	BuildTreasureBoxes();
}

void UDailyLoginPage::BuildLoginList()
{
	if (!LoginDayList || !LoginTrack)
	{
		return;
	}

	LoginDayList->ClearListItems();

	for (UDailyLoginDayItem* Item : LoginTrack->GetDayItems())
	{
		if (Item)
		{
			LoginDayList->AddItem(Item);
		}
	}
}

void UDailyLoginPage::BuildTreasureBoxes()
{
	if (!TreasureBoxContainer || !TreasureTrack)
	{
		return;
	}

	TreasureBoxContainer->ClearChildren();
	TreasureBoxWidgets.Empty();

	for (UTreasureBoxItem* BoxItem : TreasureTrack->TreasureBoxes)
	{
		if (!BoxItem)
		{
			continue;
		}

		UTreasureBoxWidget* BoxWidget =
			CreateWidget<UTreasureBoxWidget>(
				GetWorld(),
				UTreasureBoxWidget::StaticClass()
			);

		if (!BoxWidget)
		{
			continue;
		}

		BoxWidget->BindBoxItem(BoxItem);

		TreasureBoxContainer->AddChild(BoxWidget);
		TreasureBoxWidgets.Add(BoxWidget);
	}
}

void UDailyLoginPage::RefreshTreasureBoxes()
{
	for (UTreasureBoxWidget* Widget : TreasureBoxWidgets)
	{
		if (Widget)
		{
			Widget->Refresh();
		}
	}
}


