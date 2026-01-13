// 13


#include "UI/Activity/Pages/DailyLogin/Widgets/TreasureBoxWidget.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "UI/Activity/Model/Treasure/TreasureBoxItem.h"

void UTreasureBoxWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	// 绑定点击事件
	if (BoxButton)
	{
		BoxButton->OnClicked.AddDynamic(
			this,
			&UTreasureBoxWidget::OnBoxClicked
		);
	}
}

/**
 * BindBoxItem
 * 由 DailyLoginPage 调用
 */
void UTreasureBoxWidget::BindBoxItem(UTreasureBoxItem* InBoxItem)
{
	BoxItem = InBoxItem;
	RefreshView();
}

/**
 * RefreshView
 * 根据 BoxItem 的状态刷新 UI
 */
void UTreasureBoxWidget::RefreshView()
{
	if (!BoxItem)
	{
		return;
	}

	// ---------- 状态判断 ----------

	if (BoxItem->bClaimed)
	{
		// 已领取
		if (StateText)
		{
			StateText->SetText(FText::FromString(TEXT("已领取")));
		}

		if (BoxButton)
		{
			BoxButton->SetIsEnabled(false);
		}
	}
	else if (BoxItem->bUnlocked)
	{
		// 可领取
		if (StateText)
		{
			StateText->SetText(FText::FromString(TEXT("可领取")));
		}

		if (BoxButton)
		{
			BoxButton->SetIsEnabled(true);
		}
	}
	else
	{
		// 未解锁
		if (StateText)
		{
			StateText->SetText(FText::FromString(TEXT("未解锁")));
		}

		if (BoxButton)
		{
			BoxButton->SetIsEnabled(false);
		}
	}

	// ⚠ BoxIcon 外观（灰 / 高亮 / 已领）
	// 建议在蓝图里用 Bind 或 SetBrushFromTexture
}

/**
 * 点击宝箱
 */
void UTreasureBoxWidget::OnBoxClicked()
{
	if (!BoxItem)
	{
		return;
	}

	// ⚠ UI 不判断能不能领
	// 直接把请求交给 Model
	BoxItem->RequestClaim();

	// 领取后刷新显示
	RefreshView();
}
