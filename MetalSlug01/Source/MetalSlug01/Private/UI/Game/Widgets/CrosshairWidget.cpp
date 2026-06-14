// 版权声明：在项目设置的描述页面填写您的版权信息。

// ==========================================
// 头文件包含区
// ==========================================
#include "UI/Game/Widgets/CrosshairWidget.h"


// ==========================================
// 1. 生命周期
// ==========================================

/**
 * UCrosshairWidget::NativeConstruct
 *
 * 默认隐藏准星
 * 时机: 等战斗开始时由 GameHUDWidget->ShowCrosshair() 切换
 */
void UCrosshairWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// 默认隐藏准星，等待游戏开始时再显示
	SetVisibility(ESlateVisibility::Collapsed);
}


// ==========================================
// 2. 公共接口
// ==========================================

/**
 * UCrosshairWidget::ShowCrosshair
 *
 * 可见性: HitTestInvisible
 * 用途: 准星可见但不影响鼠标点击其他 UI
 */
void UCrosshairWidget::ShowCrosshair()
{
	SetVisibility(ESlateVisibility::HitTestInvisible);
}


/**
 * UCrosshairWidget::HideCrosshair
 *
 * 可见性: Collapsed
 * 用途: 折叠隐藏，不占用布局空间
 */
void UCrosshairWidget::HideCrosshair()
{
	SetVisibility(ESlateVisibility::Collapsed);
}


/**
 * UCrosshairWidget::UpdateCrosshairStyle
 *
 * 根据武器类型更新准星样式
 * 0 = 手枪, 1 = 步枪, 2 = 狙击枪, 3 = 霰弹枪等
 * 用途: 后续扩展不同武器不同准星（目前是占位实现）
 */
void UCrosshairWidget::UpdateCrosshairStyle(int32 WeaponType)
{
	// 根据武器类型更新准星样式（后续扩展）
	// 0 = 手枪, 1 = 步枪, 2 = 狙击枪, 3 = 霰弹枪等
}
