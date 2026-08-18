// 版权声明：在项目设置的描述页面填写您的版权信息。

#pragma once

// ==========================================
// WeaponPanelWidget 头文件 — 武器面板组件
// ==========================================
//
// 文件作用:
//   1. 声明 UWeaponPanelWidget — 显示玩家武器 + 弹药数量的面板
//   2. 提供主武器/副武器/近战武器 3 个槽位
//   3. 显示弹药数量 + 颜色变化 (耗尽/低/正常)
//
// 设计理念 (大厂原则 - 数据驱动):
//   1. 主/副/近战: 各自独立的 Image 控件 (图标 + 底座两层)
//   2. 弹药: Text_WeaponAmmo 显示弹药数量 (近战=1/1, 枪械=弹匣+备用)
//   3. 近战图标特殊处理: M_WeaponIconRotate 动态材质旋转 90 度
//   4. 防御性: 每个 Update* 都做空指针检查
//   5. 单一真理源: 数据由调用方提供, Widget 只渲染
//
// 大厂对应:
//   - Lyra: UWeaponInventoryEntry
//   - 商业 FPS: 通用武器面板
// ==========================================
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "WeaponPanelWidget.generated.h"

// 前向声明
class UImage;
class UTextBlock;


/**
 * @class UWeaponPanelWidget
 * @brief 武器面板组件
 *
 * 职责说明:
 * - 显示主武器、副武器、近战武器的图标
 * - 显示武器弹药数量
 *
 * 架构理念:
 * 1. 主/副/近战: 各自独立的 Image 控件（图标 + 底座两层）
 * 2. 弹药: Text_WeaponAmmo 显示弹药数量 (近战=1/1, 枪械=当前弹药/弹匣+备用弹药)
 * 3. 近战图标特殊处理: 使用动态材质 M_WeaponIconRotate 旋转 90 度
 * 4. 防御性: 每个 Update* 都做空指针检查
 */
UCLASS()
class METALSLUG01_API UWeaponPanelWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// ==========================================
	// 1. 公共接口
	// ==========================================

	/**
	 * 更新主武器图标
	 */
	UFUNCTION(BlueprintCallable, Category = "WeaponPanel")
	void UpdatePrimaryWeaponIcon(UTexture2D* Icon);

	/**
	 * 更新副武器图标
	 */
	UFUNCTION(BlueprintCallable, Category = "WeaponPanel")
	void UpdateSecondaryWeaponIcon(UTexture2D* Icon);

	/**
	 * 更新近战武器图标
	 * 特殊: 使用 M_WeaponIconRotate 动态材质旋转 90 度
	 */
	UFUNCTION(BlueprintCallable, Category = "WeaponPanel")
	void UpdateMeleeWeaponIcon(UTexture2D* Icon);

	/**
	 * 【v84 大厂架构新增】更新武器弹药数量文本
	 *
	 * 格式规则:
	 *   - 近战武器 (bIsMelee=true): "1/1" (固定值)
	 *   - 枪械 (bIsMelee=false): "弹匣弹药/弹匣容量 + 备用弹药" 如 "30/30 +120"
	 *
	 * 颜色逻辑:
	 *   - CurrentMag = 0: 红色 (弹药耗尽)
	 *   - CurrentMag <= 10: 黄色 (弹药过低)
	 *   - 否则: 白色 (正常)
	 *
	 * @param CurrentMag   当前弹匣弹药
	 * @param MagazineSize 弹匣容量
	 * @param ReserveAmmo  备用弹药总数
	 * @param bIsMelee    是否为近战武器 (近战武器只显示 1/1)
	 */
	UFUNCTION(BlueprintCallable, Category = "WeaponPanel")
	void UpdateWeaponAmmoText(int32 CurrentMag, int32 MagazineSize, int32 ReserveAmmo, bool bIsMelee);

protected:
	// ==========================================
	// 2. 生命周期
	// ==========================================

	/**
	 * 初始化
	 */
	virtual bool Initialize() override;

private:
	// ==========================================
	// 3. UI 组件
	// ==========================================

	/** 主武器图标 */
	UPROPERTY(meta = (BindWidget))
	UImage* Image_PrimaryWeapon;

	/** 主武器底座（背景框） */
	UPROPERTY(meta = (BindWidget))
	UImage* Image_PrimaryWeaponBase;

	/** 副武器图标 */
	UPROPERTY(meta = (BindWidget))
	UImage* Image_SecondaryWeapon;

	/** 副武器底座 */
	UPROPERTY(meta = (BindWidget))
	UImage* Image_SecondaryWeaponBase;

	/** 近战武器图标 */
	UPROPERTY(meta = (BindWidget))
	UImage* Image_MeleeWeapon;

	/** 近战武器底座 */
	UPROPERTY(meta = (BindWidget))
	UImage* Image_MeleeWeaponBase;

	/** 武器弹药数量文本 【v84 大厂架构新增】 */
	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* Text_WeaponAmmo;
};
