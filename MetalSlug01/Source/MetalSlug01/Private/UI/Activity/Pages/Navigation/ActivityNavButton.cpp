#include "UI/Activity/Pages/Navigation/ActivityNavButton.h"
#include "Styling/SlateBrush.h"

void UActivityNavButton::NativeConstruct()
{
	Super::NativeConstruct();
	
	// 设置初始状态
	SetSelected(false);
	
	// 隐藏红点
	if (RedDotImage)
	{
		RedDotImage->SetVisibility(ESlateVisibility::Collapsed);
	}
	
	// 绑定按钮点击事件
	if (MainButton)
	{
		MainButton->OnClicked.AddDynamic(this, &UActivityNavButton::OnMainButtonClicked);
	}
}

void UActivityNavButton::InitializeButton(FName InActivityId, const FText& InTitle, UTexture2D* InIconTexture)
{
	ActivityId = InActivityId;
	
	// 设置标题
	if (TitleText)
	{
		TitleText->SetText(InTitle);
	}
	
	// 图标设置留给蓝图处理，通过Button的Style属性配置
	// 避免直接操作复杂的Slate样式
	
	// 确保按钮可见
	if (MainButton)
	{
		MainButton->SetVisibility(ESlateVisibility::Visible);
	}
	
	UE_LOG(LogTemp, Log, TEXT("ActivityNavButton: 初始化完成，活动ID: %s，图标设置需在蓝图中完成"), *ActivityId.ToString());
}

void UActivityNavButton::SetSelected(bool bInIsSelected)
{
	bIsSelected = bInIsSelected;
	
	// 更新选中状态指示器
	if (SelectionIndicator)
	{
		SelectionIndicator->SetVisibility(bIsSelected ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	
	UE_LOG(LogTemp, Log, TEXT("ActivityNavButton: 设置选中状态 [%s] 为 %s"), 
		   *ActivityId.ToString(), bIsSelected ? TEXT("选中") : TEXT("未选中"));
}

void UActivityNavButton::SetRedDot(bool bShowRedDot, int32 RedDotValue)
{
	if (RedDotImage)
	{
		RedDotImage->SetVisibility(bShowRedDot ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	
	UE_LOG(LogTemp, Log, TEXT("ActivityNavButton: 设置红点状态 [%s] 显示:%s 数值:%d"), 
		   *ActivityId.ToString(), bShowRedDot ? TEXT("是") : TEXT("否"), RedDotValue);
}

void UActivityNavButton::OnMainButtonClicked()
{
	UE_LOG(LogTemp, Warning, TEXT("🎯 ActivityNavButton被点击，活动ID: %s"), *ActivityId.ToString());
	
	// 触发普通委托（支持Lambda）
	OnButtonClicked.Broadcast();
	
	// 触发动态委托（蓝图兼容）
	OnNavButtonClicked.Broadcast(ActivityId);
}