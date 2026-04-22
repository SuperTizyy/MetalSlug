#include "UI/Game/Widgets/CrosshairWidget.h"

void UCrosshairWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// 默认隐藏准星，等待游戏开始时再显示
	SetVisibility(ESlateVisibility::Collapsed);
}

void UCrosshairWidget::ShowCrosshair()
{
	SetVisibility(ESlateVisibility::HitTestInvisible);
}

void UCrosshairWidget::HideCrosshair()
{
	SetVisibility(ESlateVisibility::Collapsed);
}

void UCrosshairWidget::UpdateCrosshairStyle(int32 WeaponType)
{
	// 根据武器类型更新准星样式（后续扩展）
	// 0 = 手枪, 1 = 步枪, 2 = 狙击枪, 3 = 霰弹枪等
}
