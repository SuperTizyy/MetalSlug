// 14

#include "UI/Activity/Pages/DailyLogin/Widgets/DailyLoginDayItemWidget.h"

#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Components/Image.h"

#include "UI/Activity/Model/DailyLogin/DailyLoginDayItem.h"
#include "UI/Activity/Track/DailyLogin/DailyLoginTrack.h"
#include "UI/Activity/Core/ActivitySubsystem.h"
#include "Kismet/GameplayStatics.h"

void UDailyLoginDayItemWidget::NativeOnListItemObjectSet(UObject* ListItemObject)
{
	ItemData = Cast<UDailyLoginDayItem>(ListItemObject);
	if (!ItemData)
	{
		return;
	}

	// 获取 Track（当前阶段直接从 Subsystem 拿）
	if (!LoginTrack)
	{
		if (UGameInstance* GI = GetGameInstance())
		{
			if (UActivitySubsystem* Subsystem = GI->GetSubsystem<UActivitySubsystem>())
			{
				LoginTrack = Subsystem->GetDailyLoginTrack();
			}
		}
	}

	// 绑定按钮
	if (ClaimButton)
	{
		ClaimButton->OnClicked.Clear();
		ClaimButton->OnClicked.AddDynamic(this, &UDailyLoginDayItemWidget::OnClaimClicked);
	}

	RefreshView();
}

void UDailyLoginDayItemWidget::RefreshView()
{
	if (!ItemData)
	{
		return;
	}

	// Day 文本
	if (DayText)
	{
		DayText->SetText(
			FText::Format(
				FText::FromString(TEXT("Day {0}")),
				FText::AsNumber(ItemData->GetDayIndex() + 1)
			)
		);
	}

	// 是否可点击
	const bool bCanClick =
		ItemData->CanClaim() &&
		!ItemData->IsClaimed();

	if (ClaimButton)
	{
		ClaimButton->SetIsEnabled(bCanClick);
	}

	// 已领取标识
	if (ClaimedIcon)
	{
		ClaimedIcon->SetVisibility(
			ItemData->IsClaimed()
				? ESlateVisibility::Visible
				: ESlateVisibility::Collapsed
		);
	}
}

void UDailyLoginDayItemWidget::OnClaimClicked()
{
	if (!ItemData || !LoginTrack)
	{
		return;
	}

	if (LoginTrack->TryClaimDay(ItemData->GetDayIndex()))
	{
		RefreshView();
	}
}
