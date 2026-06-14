// 版权声明：在项目设置的描述页面填写您的版权信息。

#pragma once

// ==========================================
// 头文件包含说明
// ==========================================
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CrosshairWidget.generated.h"


/**
 * @class UCrosshairWidget
 * @brief 准星 Widget
 *
 * 职责说明:
 * - 显示游戏准星（屏幕中心）
 * - 根据战斗状态显示/隐藏
 * - 后续可扩展根据武器类型显示不同样式
 *
 * 架构理念:
 * 1. 简单状态机: 隐藏(Collapsed) / 显示(HitTestInvisible)
 * 2. 默认隐藏: NativeConstruct 中默认 Collapsed，等战斗开始再显示
 * 3. 样式扩展: 预留 UpdateCrosshairStyle 给武器类型切换
 */
UCLASS()
class METALSLUG01_API UCrosshairWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// ==========================================
	// 1. 生命周期
	// ==========================================

	/**
	 * Widget 构造完毕并加入视口后调用
	 * 默认隐藏准星，等待游戏开始时再显示
	 */
	virtual void NativeConstruct() override;

	// ==========================================
	// 2. 公共接口
	// ==========================================

	/**
	 * 显示准星
	 * 可见性: HitTestInvisible（可见但不影响鼠标点击）
	 */
	UFUNCTION(BlueprintCallable, Category = "Crosshair")
	void ShowCrosshair();

	/**
	 * 隐藏准星
	 * 可见性: Collapsed（折叠，不占空间）
	 */
	UFUNCTION(BlueprintCallable, Category = "Crosshair")
	void HideCrosshair();

	/**
	 * 更新准星样式（根据武器类型）
	 * 0 = 手枪, 1 = 步枪, 2 = 狙击枪, 3 = 霰弹枪等
	 * 用途: 后续扩展不同武器不同准星
	 */
	UFUNCTION(BlueprintCallable, Category = "Crosshair")
	void UpdateCrosshairStyle(int32 WeaponType);
};
