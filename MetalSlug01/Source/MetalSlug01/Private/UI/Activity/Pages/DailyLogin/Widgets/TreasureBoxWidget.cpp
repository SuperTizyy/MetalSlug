// 13


#include "UI/Activity/Pages/DailyLogin/Widgets/TreasureBoxWidget.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"

#include "UI/Activity/Model/Treasure/TreasureBoxItem.h"

void UTreasureBoxWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	if (BoxButton)
	{
		BoxButton->OnClicked.AddDynamic(this, &UTreasureBoxWidget::OnBoxClicked);
	}
}

void UTreasureBoxWidget::BindBoxItem(UTreasureBoxItem* InBoxItem)
{
	BoxItem = InBoxItem;
	RefreshView();
}

void UTreasureBoxWidget::Refresh()
{
	RefreshView();
}

void UTreasureBoxWidget::OnBoxClicked()
{
	if (!BoxItem)
	{
		return;
	}

	// 仅通知 Model，不判断规则
	BoxItem->RequestReceive();

	// 点击后立刻刷新一次
	RefreshView();
}

void UTreasureBoxWidget::RefreshView()
{
	if (!BoxItem || !BoxIcon || !StateText)
	{
		return;
	}

	if (BoxItem->IsReceived())
	{
		StateText->SetText(FText::FromString(TEXT("已领取")));
		BoxButton->SetIsEnabled(false);
	}
	else if (BoxItem->IsUnlocked())
	{
		StateText->SetText(FText::FromString(TEXT("可领取")));
		BoxButton->SetIsEnabled(true);
	}
	else
	{
		StateText->SetText(FText::FromString(TEXT("未解锁")));
		BoxButton->SetIsEnabled(false);
	}

}

