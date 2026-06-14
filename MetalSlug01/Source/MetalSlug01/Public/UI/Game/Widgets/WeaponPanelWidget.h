// 版权声明：在项目设置的描述页面填写您的版权信息。

#pragma once

// ==========================================
// 头文件包含说明
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
 * - 显示剩余弹药（"弹夹/总弹药"格式）
 * - 弹药数过低时颜色变化（白/黄/红）
 *
 * 架构理念:
 * 1. 主/副/近战: 各自独立的 Image 控件（图标 + 底座两层）
 * 2. 弹药: 单一 Text_RemainingAmmo
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
	 * 更新剩余弹药（单一数字）
	 * @deprecated 建议使用 UpdateAmmoText
	 */
	UFUNCTION(BlueprintCallable, Category = "WeaponPanel")
	void UpdateRemainingAmmo(int32 Ammo);

	/**
	 * 更新弹药文本（支持弹夹/总弹药格式，如 "30/120"）
	 * 颜色: 0 红 / <=10 黄 / 否则 白
	 */
	UFUNCTION(BlueprintCallable, Category = "WeaponPanel")
	void UpdateAmmoText(int32 CurrentMag, int32 TotalReserve);

protected:
	// ==========================================
	// 2. 生命周期
	// ==========================================

	/**
	 * 初始化: Text_RemainingAmmo 默认 "0/0"
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

	/** 弹药显示文本 "X/Y" */
	UPROPERTY(meta = (BindWidget))
	UTextBlock* Text_RemainingAmmo;
};
