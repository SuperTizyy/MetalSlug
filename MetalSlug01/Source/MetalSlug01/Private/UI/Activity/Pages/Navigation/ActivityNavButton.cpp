// 版权声明：在项目设置的描述页面填写您的版权信息。

// ==========================================
// ActivityNavButton 实现 — 活动导航按钮 Widget
// ==========================================
//
// 文件作用:
//   1. 实现 UActivityNavButton — 活动导航按钮所有 UI 逻辑
//   2. SetSelected / SetRedDot / SetActivityText 状态机
//   3. OnMainButtonClicked 广播 OnNavButtonClicked
//
// 大厂原则:
//   - 单一职责: 一个小按钮管自己的样式
//   - 双委托: 普通委托 (Lambda) + 动态委托 (BP)
//   - 防呆: 先 RemoveAll 再 AddDynamic
// ==========================================
#include "UI/Activity/Pages/Navigation/ActivityNavButton.h"
#include "Styling/SlateBrush.h"


// ==========================================
// 1. 生命周期
// ==========================================

/**
 * UActivityNavButton::NativeConstruct
 *
 * 1. 初始化未选中
 * 2. 隐藏红点
 * 3. 绑定 MainButton 点击
 */
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
		// 将按钮点击事件绑定到 OnMainButtonClicked 函数
		MainButton->OnClicked.AddDynamic(this, &UActivityNavButton::OnMainButtonClicked);
	}
}


// ==========================================
// 2. 功能函数
// ==========================================

/**
 * UActivityNavButton::InitializeButton
 *
 * 1. 缓存 ActivityId
 * 2. 设置 TitleText
 * 3. 图标设置留给蓝图处理（避免直接操作复杂的 Slate 样式）
 * 4. 确保按钮可见
 *
 * @param InActivityId 活动 ID
 * @param InTitle 按钮标题
 * @param InIconTexture 图标纹理（备用）
 */
void UActivityNavButton::InitializeButton(FName InActivityId, const FText& InTitle, UTexture2D* InIconTexture)
{
	// 设置活动 ID
	ActivityId = InActivityId;

	// 设置标题
	if (TitleText)
	{
		// 将文本控件设置为传入的标题
		TitleText->SetText(InTitle);
	}

	// 图标设置留给蓝图处理，通过 Button 的 Style 属性配置
	// 避免直接操作复杂的 Slate 样式

	// 确保按钮可见
	if (MainButton)
	{
		// 设置主按钮为可见状态
		MainButton->SetVisibility(ESlateVisibility::Visible);
	}

	// 按钮初始化完成
}


/**
 * UActivityNavButton::SetSelected
 *
 * 1. 缓存 bIsSelected
 * 2. 根据状态显示/隐藏 SelectionIndicator
 */
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


/**
 * UActivityNavButton::SetRedDot
 *
 * 根据 bShowRedDot 显示/隐藏 RedDotImage
 * RedDotValue 暂未做数字红点（预留）
 */
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


// ==========================================
// 3. 内部回调
// ==========================================

/**
 * UActivityNavButton::OnMainButtonClicked
 *
 * 双广播:
 * 1. OnButtonClicked.Broadcast()（普通委托, C++ 用）
 * 2. OnNavButtonClicked.Broadcast(ActivityId)（动态委托, 蓝图用）
 */
void UActivityNavButton::OnMainButtonClicked()
{
	// 按钮被点击时的处理函数

	// 触发普通委托（支持 Lambda）
	OnButtonClicked.Broadcast();

	// 触发动态委托（蓝图兼容）
	OnNavButtonClicked.Broadcast(ActivityId);
}
