// 版权声明：在项目设置的描述页面填写您的版权信息。

// ==========================================
// 头文件包含区
// ==========================================
#include "UI/Game/Widgets/WeaponPanelWidget.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Engine/Texture.h"


// ==========================================
// 1. 初始化
// ==========================================

/**
 * UWeaponPanelWidget::Initialize
 *
 * 默认弹药文本显示 "0/0"
 */
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


// ==========================================
// 2. 主武器 / 副武器图标
// ==========================================

/**
 * UWeaponPanelWidget::UpdatePrimaryWeaponIcon
 *
 * 1. 设置 Image_PrimaryWeapon 画刷
 * 2. 设置画刷颜色为白色
 * 3. 设置 SelfHitTestInvisible（不影响点击）
 */
void UWeaponPanelWidget::UpdatePrimaryWeaponIcon(UTexture2D* Icon)
{
	if (Image_PrimaryWeapon && Icon)
	{
		Image_PrimaryWeapon->SetBrushFromTexture(Icon);
		Image_PrimaryWeapon->SetBrushTintColor(FSlateColor(FLinearColor::White));
		Image_PrimaryWeapon->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	}
}


/**
 * UWeaponPanelWidget::UpdateSecondaryWeaponIcon
 */
void UWeaponPanelWidget::UpdateSecondaryWeaponIcon(UTexture2D* Icon)
{
	if (Image_SecondaryWeapon && Icon)
	{
		Image_SecondaryWeapon->SetBrushFromTexture(Icon);
		Image_SecondaryWeapon->SetBrushTintColor(FSlateColor(FLinearColor::White));
		Image_SecondaryWeapon->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	}
}


// ==========================================
// 3. 近战武器图标（特殊: 动态材质旋转 90 度）
// ==========================================

/**
 * UWeaponPanelWidget::UpdateMeleeWeaponIcon
 *
 * 近战武器特殊处理:
 * 1. 加载 M_WeaponIconRotate 旋转材质
 * 2. 创建 UMaterialInstanceDynamic
 * 3. 设置 WeaponTex = Icon, Angle = 90
 * 4. 通过 Brush.SetResourceObject 挂载动态材质
 * 5. 降级: 找不到材质时退化为普通 SetBrushFromTexture
 *
 * 性能: 使用 static TWeakObjectPtr 缓存 BaseMat，避免重复 LoadSynchronous
 */
void UWeaponPanelWidget::UpdateMeleeWeaponIcon(UTexture2D* Icon)
{
	if (!Image_MeleeWeapon)
	{
		UE_LOG(LogTemp, Error, TEXT("[WeaponPanelWidget] UpdateMeleeWeaponIcon: Image_MeleeWeapon 未绑定! 请检查 WBP_WeaponPanel 蓝图中是否有名为 Image_MeleeWeapon 的 Image 控件"));
		return;
	}

	if (!Icon)
	{
		UE_LOG(LogTemp, Error, TEXT("[WeaponPanelWidget] UpdateMeleeWeaponIcon: 传入的 Icon 为空"));
		return;
	}

	// 加载旋转材质资源 (Content/Materials/UI/M_WeaponIconRotate)
	// 需要先在编辑器中创建该材质: TextureSampleParameter2D -> Rotator -> Output
	// 其中 TextureSampleParameter2D 和 Rotator 的 Angle 均暴露为 Parameter
	static TWeakObjectPtr<UMaterial> CachedBaseMat = nullptr;
	UMaterial* BaseMat = CachedBaseMat.Get();

	if (!BaseMat)
	{
		TSoftObjectPtr<UMaterial> MatSoft(FSoftObjectPath(TEXT("Material'/Game/Materials/UI/M_WeaponIconRotate.M_WeaponIconRotate'")));
		BaseMat = MatSoft.LoadSynchronous();
		CachedBaseMat = BaseMat;
	}

	if (!BaseMat)
	{
		// 降级: 找不到材质时直接设置纹理
		Image_MeleeWeapon->SetBrushFromTexture(Icon);
		Image_MeleeWeapon->SetBrushTintColor(FSlateColor(FLinearColor::White));
		Image_MeleeWeapon->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		UE_LOG(LogTemp, Warning, TEXT("[WeaponPanelWidget] 未找到 M_WeaponIconRotate 材质，降级为普通纹理设置"));
		return;
	}

	// 创建动态材质实例，传入纹理并设置旋转角度 90 度
	UMaterialInstanceDynamic* DynMat = UMaterialInstanceDynamic::Create(BaseMat, this);
	if (DynMat)
	{
		DynMat->SetTextureParameterValue(FName(TEXT("WeaponTex")), Icon);
		DynMat->SetScalarParameterValue(FName(TEXT("Angle")), 90.0f);
	}

	// 通过 Brush 挂载动态材质，控件尺寸保持不变
	FSlateBrush Brush = Image_MeleeWeapon->GetBrush();
	Brush.SetResourceObject(DynMat);
	Image_MeleeWeapon->SetBrush(Brush);
	Image_MeleeWeapon->SetBrushTintColor(FSlateColor(FLinearColor::White));
	Image_MeleeWeapon->SetVisibility(ESlateVisibility::SelfHitTestInvisible);

	UE_LOG(LogTemp, Log, TEXT("[WeaponPanelWidget] UpdateMeleeWeaponIcon: 使用动态材质旋转90度，图标=%s"), *Icon->GetName());
}


// ==========================================
// 4. 弹药
// ==========================================

/**
 * UWeaponPanelWidget::UpdateRemainingAmmo
 *
 * 旧接口: 仅显示单一数字
 * 颜色: 0 红 / <=10 黄 / 否则 白
 */
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


/**
 * UWeaponPanelWidget::UpdateAmmoText
 *
 * 新接口: 弹夹/总弹药格式 "X/Y"
 * 颜色: CurrentMag = 0 红 / <=10 黄 / 否则 白
 */
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
