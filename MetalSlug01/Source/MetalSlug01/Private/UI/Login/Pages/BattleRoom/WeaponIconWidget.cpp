// 版权声明：在项目设置的描述页面填写您的版权信息。

// ==========================================
// 头文件包含区
// ==========================================
#include "UI/Login/Pages/BattleRoom/WeaponIconWidget.h"

#include "Components/Button.h"
#include "Components/Image.h"
#include "Engine/Texture2D.h"
#include "UI/Login/Pages/BattleRoom/RoomInsidePage.h"


// ==========================================
// 1. 生命周期
// ==========================================

/**
 * UWeaponIconWidget::Initialize
 *
 * 1. 绑定 Btn_WeaponIcon 点击事件 -> OnWeaponIconClicked
 * 2. 默认隐藏高亮框
 */
bool UWeaponIconWidget::Initialize()
{
	if (!Super::Initialize()) return false;

	// 绑定点击事件
	if (Btn_WeaponIcon)
	{
		Btn_WeaponIcon->OnClicked.AddDynamic(this, &UWeaponIconWidget::OnWeaponIconClicked);
	}

	// 初始化时，强制把高亮框隐藏
	if (Image_HighlightBox)
	{
		Image_HighlightBox->SetVisibility(ESlateVisibility::Collapsed);
	}

	return true;
}


// ==========================================
// 2. 初始化（由父页面调用）
// ==========================================

/**
 * UWeaponIconWidget::SetupWeaponItem
 *
 * 1. 缓存武器 RowName
 * 2. 认领上级大厅
 * 3. 【核心魔法】把 FWeaponInfo.WeaponIcon 贴图赋值给 Button 的背景
 *    - 构造 FSlateBrush
 *    - GetStyle() -> SetNormal/SetHovered/SetPressed
 *    - SetStyle 写回
 * 用途: 让 Button 控件既有点击响应, 又有武器图标背景
 */
void UWeaponIconWidget::SetupWeaponItem(const FName& InWeaponRowName, const FWeaponInfo& InWeaponData, URoomInsidePage* InParentPage)
{
	RepresentedWeaponRowName = InWeaponRowName;
	ParentRoomPage = InParentPage; // 认领上司

	// 记住 ID
	RepresentedWeaponRowName = InWeaponRowName;

	// ==========================================
	// 【核心魔法】: 把表里的 WeaponIcon 贴图赋值给 Button 的背景
	// ==========================================
	if (Btn_WeaponIcon && InWeaponData.WeaponIcon)
	{
		// C++ 设置 Button 的贴图比较繁琐，需要构造一套 Brush（笔刷）
		// 把 Normal（常态）和 Hovered（悬停）都设成这张图, 确保玩家鼠标移上去也能看到枪
		FSlateBrush WeaponBrush;
		WeaponBrush.SetResourceObject(InWeaponData.WeaponIcon);
		WeaponBrush.ImageSize = FVector2D(128.0f, 128.0f); // 预设图片大小（根据需要调整）
		WeaponBrush.DrawAs = ESlateBrushDrawType::Image;

		// 1. 直接获取公开的成员变量 WidgetStyle
		FButtonStyle NewButtonStyle = Btn_WeaponIcon->GetStyle();

		// 2. 中间设置贴图的逻辑不变
		NewButtonStyle.SetNormal(WeaponBrush);
		NewButtonStyle.SetHovered(WeaponBrush);
		NewButtonStyle.SetPressed(WeaponBrush); // 顺手把按下的状态也换成武器图，免得点下去变成白块

		// 3. 使用正确的 Setter 函数 SetStyle
		Btn_WeaponIcon->SetStyle(NewButtonStyle);
	}
}


// ==========================================
// 3. 高亮控制
// ==========================================

/**
 * UWeaponIconWidget::SetHighlightFrameVisibility
 *
 * 由 RoomInsidePage 统筹调用
 * true:  SelfHitTestInvisible（可见但不可点击, 点击穿透到 Button）
 * false: Collapsed（折叠）
 */
void UWeaponIconWidget::SetHighlightFrameVisibility(bool bIsVisible)
{
	// 由大厅页面（RoomInsidePage）统筹调用，告诉这个格子你是要显示高亮还是隐藏
	if (Image_HighlightBox)
	{
		if (bIsVisible)
		{
			// SelfHitTestInvisible 意味着可见但不可点击，点击会穿透到下面的按钮
			Image_HighlightBox->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		}
		else
		{
			Image_HighlightBox->SetVisibility(ESlateVisibility::Collapsed);
		}
	}
}


// ==========================================
// 4. 点击回调
// ==========================================

/**
 * UWeaponIconWidget::OnWeaponIconClicked
 *
 * 1. 校验 RowName 有效
 * 2. 向父页面（大厅）打小报告: OnWeaponItemSelectedInGrid
 * 3. 屏幕调试信息
 *
 * 未来: 父页面收到回调后会:
 * - 把右边的大预览图切换到对应武器
 * - 把上一个选中的格子 SetHighlightFrameVisibility(false)
 * - 把本格子 SetHighlightFrameVisibility(true)
 */
void UWeaponIconWidget::OnWeaponIconClicked()
{
	// 当玩家点击了这个格子
	if (RepresentedWeaponRowName.IsNone()) return;

	// 向大厅打小报告: "老大，玩家点我了！"
	if (ParentRoomPage)
	{
		ParentRoomPage->OnWeaponItemSelectedInGrid(RepresentedWeaponRowName);
	}

	// 测试打印
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 3.f, FColor::Yellow, FString::Printf(TEXT("点击了格子上的武器: %s"), *RepresentedWeaponRowName.ToString()));
	}

	// ==========================================
	// 【未来逻辑预留】: 找大厅页面报到
	// ==========================================
	// E.g., 如果你能拿到大厅页面的指针（ParentWidget）, 在这里调用 ParentWidget->OnWeaponItemSelectedInGrid(RepresentedWeaponRowName);
	// 这样大厅就会知道该把右边的大预览图切成哪把枪, 并且把上一个选中的格子的 SetHighlightFrameVisibility 设为 false, 把你设为 true。
}
