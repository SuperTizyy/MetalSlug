#include "UI/Activity/Pages/Widgets/ActivityNavItemWidget.h"

void UActivityNavItemWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// 设置初始状态
	SetSelected(false);
}

void UActivityNavItemWidget::InitializeItem(FName InActivityId, const FText& InTitle, const FText& InDescription, UTexture2D* InIconTexture)
{
	ActivityId = InActivityId;

	// 设置显示内容
	if (TitleText)
	{
		TitleText->SetText(InTitle);
	}

	if (DescriptionText)
	{
		DescriptionText->SetText(InDescription);
	}

	if (IconImage && InIconTexture)
	{
		IconImage->SetBrushFromTexture(InIconTexture);
	}

	// 隐藏红点（默认状态）
	if (RedDotImage)
	{
		RedDotImage->SetVisibility(ESlateVisibility::Collapsed);
	}

	// 清空时间信息
	if (TimeInfoText)
	{
		TimeInfoText->SetText(FText::GetEmpty());
	}
	
	// 确保主按钮可见
	if (MainButton)
	{
		MainButton->SetVisibility(ESlateVisibility::Visible);
		UE_LOG(LogTemp, Log, TEXT("ActivityNavItemWidget: MainButton已设置为可见"));
	}

	UE_LOG(LogTemp, Log, TEXT("ActivityNavItemWidget: 初始化完成，活动ID: %s, MainButton可见性: %d"), 
		*ActivityId.ToString(), MainButton ? (int32)MainButton->GetVisibility() : -1);
}

void UActivityNavItemWidget::SetSelected(bool bInIsSelected)
{
	bIsSelected = bInIsSelected;

	// 更新视觉状态（可以通过蓝图实现具体的样式变化）
	UE_LOG(LogTemp, Log, TEXT("ActivityNavItemWidget: 设置选中状态 [%s] 为 %s"), 
		   *ActivityId.ToString(), bIsSelected ? TEXT("选中") : TEXT("未选中"));
}

void UActivityNavItemWidget::SetRedDot(bool bShowRedDot, int32 RedDotValue)
{
	if (RedDotImage)
	{
		RedDotImage->SetVisibility(bShowRedDot ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		
		// 如果有红点数值显示，可以在TimeInfoText中显示
		if (TimeInfoText && RedDotValue > 0)
		{
			TimeInfoText->SetText(FText::FromString(FString::Printf(TEXT("%d"), RedDotValue)));
		}
	}

	UE_LOG(LogTemp, Log, TEXT("ActivityNavItemWidget: 设置红点状态 [%s] 显示:%s 数值:%d"), 
		   *ActivityId.ToString(), bShowRedDot ? TEXT("是") : TEXT("否"), RedDotValue);
}

void UActivityNavItemWidget::SetTimeInfo(const FText& TimeInfo)
{
	if (TimeInfoText)
	{
		TimeInfoText->SetText(TimeInfo);
	}

	UE_LOG(LogTemp, Log, TEXT("ActivityNavItemWidget: 设置时间信息 [%s] 内容:%s"), 
		   *ActivityId.ToString(), *TimeInfo.ToString());
}