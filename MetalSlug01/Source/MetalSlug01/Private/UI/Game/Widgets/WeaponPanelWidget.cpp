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
 */
bool UWeaponPanelWidget::Initialize()
{
	if (!Super::Initialize())
	{
		return false;
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
// 4. 弹药显示
// ==========================================

/**
 * UWeaponPanelWidget::UpdateWeaponAmmoText
 *
 * 【v85 大厂架构新增】武器弹药数量文本显示
 *
 * 格式规则:
 *   - 近战武器 (bIsMelee=true): "1/1" (固定值, 不显示备用弹药)
 *   - 枪械 (bIsMelee=false): "弹匣弹药/备用弹药总数" 如 "30/120"
 *
 * 颜色逻辑:
 *   - CurrentMag = 0: 红色 (弹药耗尽)
 *   - CurrentMag <= 10: 黄色 (弹药过低)
 *   - 否则: 白色 (正常)
 *
 * 大厂原则 - 单一真理源:
 *   - 本方法只负责 UI 显示逻辑
 *   - 数据由调用方 (GameHUDWidget) 从 WeaponFireComponent 读取后传入
 *   - 不自行查表, 不参与数据计算
 */
void UWeaponPanelWidget::UpdateWeaponAmmoText(int32 CurrentMag, int32 MagazineSize, int32 ReserveAmmo, bool bIsMelee)
{
	if (!Text_WeaponAmmo)
	{
		// Text_WeaponAmmo 是 BindWidgetOptional, 可能未在 BP 中绑定
		// 此时静默 return (这是 BP 配置问题, 不是 C++ 代码问题)
		return;
	}

	// 根据武器类型格式化弹药文本
	FText AmmoText;
	if (bIsMelee)
	{
		// 近战武器: 显示固定值 "1/1"
		AmmoText = NSLOCTEXT("WeaponPanel", "MeleeAmmoFormat", "1/1");
	}
	else
	{
		// 枪械: 显示 "弹匣弹药/备用弹药总数" (如 30/120)
		AmmoText = FText::Format(
			NSLOCTEXT("WeaponPanel", "RangedAmmoFormat", "{0}/{2}"),
			FText::AsNumber(CurrentMag),
			FText::AsNumber(MagazineSize),
			FText::AsNumber(ReserveAmmo)
		);
	}

	Text_WeaponAmmo->SetText(AmmoText);

	// 根据弹药数量设置颜色
	FLinearColor AmmoColor = FLinearColor::White;
	if (CurrentMag == 0)
	{
		// 弹药耗尽: 红色
		AmmoColor = FLinearColor::Red;
	}
	else if (CurrentMag <= 10)
	{
		// 弹药过低: 黄色
		AmmoColor = FLinearColor::Yellow;
	}
	// 否则保持白色 (正常)

	Text_WeaponAmmo->SetColorAndOpacity(AmmoColor);

	UE_LOG(LogTemp, Log,
		TEXT("[WeaponPanelWidget] UpdateWeaponAmmoText: CurrentMag=%d, MagazineSize=%d, ReserveAmmo=%d, bIsMelee=%d, Color=%s"),
		CurrentMag, MagazineSize, ReserveAmmo, bIsMelee ? 1 : 0,
		CurrentMag == 0 ? TEXT("Red") : (CurrentMag <= 10 ? TEXT("Yellow") : TEXT("White")));
}
