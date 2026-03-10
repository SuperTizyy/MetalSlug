#include "UI/Activity/Pages/Navigation/ActivityNavButton.h"
#include "Styling/SlateBrush.h"

void UActivityNavButton::NativeConstruct()
{
	// 调用父类构造函数
	Super::NativeConstruct();
	
	// 设置初始状态为未选中
	SetSelected(false);
	
	// 隐藏红点
	if (RedDotImage)
	{
		// 将红点图片设置为隐藏状态
		RedDotImage->SetVisibility(ESlateVisibility::Collapsed);
	}
	
	// 绑定按钮点击事件
	if (MainButton)
	{
		// 将按钮点击事件绑定到OnMainButtonClicked函数
		MainButton->OnClicked.AddDynamic(this, &UActivityNavButton::OnMainButtonClicked);
	}
}

void UActivityNavButton::InitializeButton(FName InActivityId, const FText& InTitle, UTexture2D* InIconTexture)
{
	// 设置活动ID
	ActivityId = InActivityId;
	
	// 设置标题
	if (TitleText)
	{
		// 将文本控件设置为传入的标题
		TitleText->SetText(InTitle);
	}
	
	// 图标设置留给蓝图处理，通过Button的Style属性配置
	// 避免直接操作复杂的Slate样式
	
	// 确保按钮可见
	if (MainButton)
	{
		// 设置主按钮为可见状态
		MainButton->SetVisibility(ESlateVisibility::Visible);
	}
	
	// 按钮初始化完成
}

void UActivityNavButton::SetSelected(bool bInIsSelected)
{
	// 设置按钮的选中状态
	bIsSelected = bInIsSelected;

	// 更新选中状态指示器
	if (SelectionIndicator)
	{
		// 根据选中状态设置指示器的可见性
		SelectionIndicator->SetVisibility(bIsSelected ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}

	// 选中状态设置完成
}

void UActivityNavButton::SetRedDot(bool bShowRedDot, int32 RedDotValue)
{
	// 如果红点图片存在
	if (RedDotImage)
	{
		// 根据是否显示红点的标志设置红点图片的可见性
		RedDotImage->SetVisibility(bShowRedDot ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}
	
	// 红点状态设置完成
}

void UActivityNavButton::OnMainButtonClicked()
{
	// 按钮被点击时的处理函数
	
	// 触发普通委托（支持Lambda）
	OnButtonClicked.Broadcast();
	
	// 触发动态委托（蓝图兼容）
	OnNavButtonClicked.Broadcast(ActivityId);
}