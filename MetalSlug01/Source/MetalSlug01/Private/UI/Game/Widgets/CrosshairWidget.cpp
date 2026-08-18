// 版权声明：在项目设置的描述页面填写您的版权信息。

// ==========================================
// CrosshairWidget 实现 — 准星 Widget
// ==========================================
//
// 文件作用:
//   1. NativeConstruct 默认隐藏准星
//   2. ShowCrosshair/HideCrosshair 控制显隐
//   3. GetCenterScreenPosition 提供准星屏幕坐标 (武器射线真理源)
//
// 关键架构 (v60.11):
//   - 玩家射线必须从 Muzzle 朝准星射出
//   - 准星屏幕坐标 = Widget 几何中心 (绝对屏幕坐标)
//   - UE 5.6 标准做法: GetCachedGeometry().LocalToAbsolute(LocalCenter)
//
// 零兜底:
//   - Widget 未渲染 → 返回 ZeroVector, 调用方必须显式校验
// ==========================================

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


// ==========================================
// 4. v60.11 准星屏幕坐标
// ==========================================

/**
 * UCrosshairWidget::GetCenterScreenPosition
 *
 * 大厂原则 — 武器射线检测的真理源:
 *   - 玩家射线 = "从 Muzzle 朝准星射出"
 *   - 准星屏幕坐标 = Widget 几何中心 (绝对屏幕坐标, 不是相对坐标)
 *   - UE 5.6 标准做法: GetCachedGeometry().LocalToAbsolute(LocalCenter)
 *
 * 失败模式 (零兜底):
 *   - Widget 未渲染 (Visibility=Collapsed 或 Size=0) → 返回 ZeroVector
 *   - 调用方 (URangedLineStrategy) 必须显式校验 IsZero 后拒绝
 */
FVector2D UCrosshairWidget::GetCenterScreenPosition() const
{
	// (1) Widget 必须可见 — Collapsed / Hidden 时 IsConstructed=true 但 CachedGeometry 可能为空
	if (GetVisibility() == ESlateVisibility::Collapsed
		|| GetVisibility() == ESlateVisibility::Hidden)
	{
		return FVector2D::ZeroVector;
	}

	// (2) CachedGeometry 必须有效 — Widget 必须至少 Tick 过一次
	const FGeometry& Geo = GetCachedGeometry();
	const FVector2D LocalSize = Geo.GetLocalSize();
	if (LocalSize.X <= 0.0f || LocalSize.Y <= 0.0f)
	{
		return FVector2D::ZeroVector;
	}

	// (3) 几何中心 → 绝对屏幕坐标 (左上角原点, 像素)
	const FVector2D LocalCenter = LocalSize * 0.5f;
	const FVector2D AbsoluteCenter = Geo.LocalToAbsolute(LocalCenter);

	return AbsoluteCenter;
}
