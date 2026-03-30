#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "WeaponPanelWidget.generated.h"

class UImage;
class UTextBlock;

/**
 * 武器面板组件
 * 负责显示主武器、副武器、近战武器的图标和剩余弹药
 */
UCLASS()
class METALSLUG01_API UWeaponPanelWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// 更新主武器图标
	UFUNCTION(BlueprintCallable, Category = "WeaponPanel")
	void UpdatePrimaryWeaponIcon(UTexture2D* Icon);

	// 更新副武器图标
	UFUNCTION(BlueprintCallable, Category = "WeaponPanel")
	void UpdateSecondaryWeaponIcon(UTexture2D* Icon);

	// 更新近战武器图标
	UFUNCTION(BlueprintCallable, Category = "WeaponPanel")
	void UpdateMeleeWeaponIcon(UTexture2D* Icon);

	// 更新剩余弹药
	UFUNCTION(BlueprintCallable, Category = "WeaponPanel")
	void UpdateRemainingAmmo(int32 Ammo);

	// 更新弹药文本（支持弹夹/总弹药格式，如 "30/120"）
	UFUNCTION(BlueprintCallable, Category = "WeaponPanel")
	void UpdateAmmoText(int32 CurrentMag, int32 TotalReserve);

protected:
	virtual bool Initialize() override;

private:
	// ==========================================
	// 主武器相关
	// ==========================================
	UPROPERTY(meta = (BindWidget))
	UImage* Image_PrimaryWeapon;

	UPROPERTY(meta = (BindWidget))
	UImage* Image_PrimaryWeaponBase;

	// ==========================================
	// 副武器相关
	// ==========================================
	UPROPERTY(meta = (BindWidget))
	UImage* Image_SecondaryWeapon;

	UPROPERTY(meta = (BindWidget))
	UImage* Image_SecondaryWeaponBase;

	// ==========================================
	// 近战武器相关
	// ==========================================
	UPROPERTY(meta = (BindWidget))
	UImage* Image_MeleeWeapon;

	UPROPERTY(meta = (BindWidget))
	UImage* Image_MeleeWeaponBase;

	// ==========================================
	// 弹药显示
	// ==========================================
	UPROPERTY(meta = (BindWidget))
	UTextBlock* Text_RemainingAmmo;
};