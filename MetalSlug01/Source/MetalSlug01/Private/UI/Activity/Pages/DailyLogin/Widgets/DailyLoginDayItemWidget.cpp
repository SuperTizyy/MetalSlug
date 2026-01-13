// 14


#include "UI/Activity/Pages/DailyLogin/Widgets/DailyLoginDayItemWidget.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "UI/Activity/Model/DailyLogin/DailyLoginDayItem.h"


void UDailyLoginDayItemWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	// 绑定按钮点击
	if (ClaimButton)
	{
		ClaimButton->OnClicked.AddDynamic(
			this,
			&UDailyLoginDayItemWidget::OnClaimClicked
		);
	}
}

/**
 * BindItem
 * ListView 调用，用于绑定数据
 */
void UDailyLoginDayItemWidget::BindItem(UDailyLoginDayItem* InItem)
{
	Item = InItem;
	RefreshView();
}

/**
 * RefreshView
 * 根据 Item 状态刷新 UI
 */
void UDailyLoginDayItemWidget::RefreshView()
{
	if (!Item)
	{
		return;
	}

	// 显示 Day X
	if (DayText)
	{
		DayText->SetText(
			FText::FromString(
				FString::Printf(TEXT("Day %d"), Item->DayIndex)
			)
		);
	}

	// 按钮状态
	if (ClaimButton)
	{
		ClaimButton->SetIsEnabled(Item->bClaimable);
	}
}

/**
 * 点击领取
 */
void UDailyLoginDayItemWidget::OnClaimClicked()
{
	if (!Item)
	{
		return;
	}

	// 仅通知 Item（真正逻辑在 Track）
	Item->RequestClaim();

	// 刷新 UI
	RefreshView();
}
