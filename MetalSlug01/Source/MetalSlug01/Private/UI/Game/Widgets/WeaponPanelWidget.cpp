#include "UI/Game/Widgets/WeaponPanelWidget.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"

bool UWeaponPanelWidget::Initialize()
{
	if (!Super::Initialize())
	{
		return false;
	}

	// 初始化默认显示
	if (Text_RemainingAmmo)
	{
		Text_RemainingAmmo->SetText(NSLOCTEXT("WeaponPanel", "DefaultAmmo", "0/0"));
	}

	return true;
}

void UWeaponPanelWidget::UpdatePrimaryWeaponIcon(UTexture2D* Icon)
{
	if (Image_PrimaryWeapon && Icon)
	{
		Image_PrimaryWeapon->SetBrushFromTexture(Icon);
		Image_PrimaryWeapon->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	}
}

void UWeaponPanelWidget::UpdateSecondaryWeaponIcon(UTexture2D* Icon)
{
	if (Image_SecondaryWeapon && Icon)
	{
		Image_SecondaryWeapon->SetBrushFromTexture(Icon);
		Image_SecondaryWeapon->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	}
}

void UWeaponPanelWidget::UpdateMeleeWeaponIcon(UTexture2D* Icon)
{
	if (Image_MeleeWeapon && Icon)
	{
		Image_MeleeWeapon->SetBrushFromTexture(Icon);
		Image_MeleeWeapon->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	}
}

void UWeaponPanelWidget::UpdateRemainingAmmo(int32 Ammo)
{
	if (Text_RemainingAmmo)
	{
		Text_RemainingAmmo->SetText(FText::AsNumber(Ammo));

		// 根据弹药数量设置颜色
		FLinearColor AmmoColor = FLinearColor::White;
		if (Ammo == 0)
		{
			AmmoColor = FLinearColor::Red;
		}
		else if (Ammo <= 10)
		{
			AmmoColor = FLinearColor::Yellow;
		}
		Text_RemainingAmmo->SetColorAndOpacity(AmmoColor);
	}
}

void UWeaponPanelWidget::UpdateAmmoText(int32 CurrentMag, int32 TotalReserve)
{
	if (Text_RemainingAmmo)
	{
		Text_RemainingAmmo->SetText(FText::Format(
			NSLOCTEXT("WeaponPanel", "AmmoFormat", "{0}/{1}"),
			FText::AsNumber(CurrentMag),
			FText::AsNumber(TotalReserve)
		));

		// 根据弹药数量设置颜色
		FLinearColor AmmoColor = FLinearColor::White;
		if (CurrentMag == 0)
		{
			AmmoColor = FLinearColor::Red;
		}
		else if (CurrentMag <= 10)
		{
			AmmoColor = FLinearColor::Yellow;
		}
		Text_RemainingAmmo->SetColorAndOpacity(AmmoColor);
	}
}