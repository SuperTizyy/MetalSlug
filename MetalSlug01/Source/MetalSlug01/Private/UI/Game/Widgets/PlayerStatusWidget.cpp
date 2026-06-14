// 版权声明：在项目设置的描述页面填写您的版权信息。

// ==========================================
// 头文件包含区
// ==========================================
#include "UI/Game/Widgets/PlayerStatusWidget.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "Components/HorizontalBox.h"
#include "Characters/BaseCharacter.h"
#include "Kismet/GameplayStatics.h"


// ==========================================
// 1. 生命周期
// ==========================================

/**
 * UPlayerStatusWidget::NativeConstruct
 *
 * 主动从角色拉取初始数据
 * 解决: UI 创建晚于角色数据初始化导致的初始值为 0 的问题
 */
void UPlayerStatusWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// 主动从角色拉取初始数据，解决 UI 创建晚于角色数据初始化导致的初始值为 0 的问题
	PullInitialDataFromCharacter();
}


/**
 * UPlayerStatusWidget::PullInitialDataFromCharacter
 *
 * 1. PC = GetOwningPlayer() 获取当前被操控的玩家控制器
 * 2. Cast<ABaseCharacter>(PC->GetPawn()) 获取当前角色
 * 3. 拉取 AC/ACE/HP/Energy 各项初值
 * 注意: GetCurrentHealth/GetMaxHealth 等方法必须存在，否则编译失败
 */
void UPlayerStatusWidget::PullInitialDataFromCharacter()
{
	// 通过 PlayerController 获取当前被操控的角色
	if (APlayerController* PC = GetOwningPlayer())
	{
		if (ABaseCharacter* Character = Cast<ABaseCharacter>(PC->GetPawn()))
		{
			// 拉取初始 AC 值（此时角色可能在服务端已经初始化过 AC）
			UpdateACValue(Character->GetAC());

			// 拉取初始 ACE 值
			UpdateACEValue(Character->GetACE());

			// 拉取初始血量
			UpdateHealth(Character->GetCurrentHealth(), Character->GetMaxHealth());
			UpdateHealthText(FMath::CeilToInt(Character->GetCurrentHealth()), FMath::CeilToInt(Character->GetMaxHealth()));

			// 拉取初始能量
			UpdateEnergy(Character->GetCurrentEnergy(), Character->GetMaxEnergy());
			UpdateEnergyText(FMath::CeilToInt(Character->GetCurrentEnergy()), FMath::CeilToInt(Character->GetMaxEnergy()));
		}
	}
}


/**
 * UPlayerStatusWidget::Initialize
 *
 * 1. 血条默认 100%
 * 2. 能量条默认 100%
 * 防御性: BindWidget 绑定失败时也能编译通过
 */
bool UPlayerStatusWidget::Initialize()
{
	if (!Super::Initialize())
	{
		return false;
	}

	// 初始化默认值
	if (PB_HealthBar)
	{
		PB_HealthBar->SetPercent(1.0f);
	}

	if (PB_EnergyBar)
	{
		PB_EnergyBar->SetPercent(1.0f);
	}

	return true;
}


// ==========================================
// 2. 血量 / 能量
// ==========================================

/**
 * UPlayerStatusWidget::UpdateHealth
 *
 * 1. 计算百分比: Current / Max (防御性: Max > 0)
 * 2. Clamp 0~1
 * 3. 设置进度条
 * 4. 根据百分比设置颜色: > 60% 绿, > 30% 黄, 否则 红
 */
void UPlayerStatusWidget::UpdateHealth(float Current, float Max)
{
	if (!PB_HealthBar)
	{
		UE_LOG(LogTemp, Error, TEXT("[Health] PlayerStatus UpdateHealth: PB_HealthBar 为空!"));
		return;
	}

	float Percent = (Max > 0.0f) ? (Current / Max) : 0.0f;
	Percent = FMath::Clamp(Percent, 0.0f, 1.0f);
	UE_LOG(LogTemp, Warning, TEXT("[Health] PlayerStatus UpdateHealth: %.1f/%.1f = %.2f%%"), Current, Max, Percent * 100);
	PB_HealthBar->SetPercent(Percent);

	// 根据血量百分比设置颜色
	if (Percent > 0.6f)
	{
		PB_HealthBar->SetFillColorAndOpacity(FLinearColor::Green);
	}
	else if (Percent > 0.3f)
	{
		PB_HealthBar->SetFillColorAndOpacity(FLinearColor::Yellow);
	}
	else
	{
		PB_HealthBar->SetFillColorAndOpacity(FLinearColor::Red);
	}
}


/**
 * UPlayerStatusWidget::UpdateEnergy
 *
 * 1. 计算百分比
 * 2. 设置能量条
 * 注意: 能量条颜色保持单一
 */
void UPlayerStatusWidget::UpdateEnergy(float Current, float Max)
{
	if (!PB_EnergyBar)
	{
		return;
	}

	float Percent = (Max > 0.0f) ? (Current / Max) : 0.0f;
	Percent = FMath::Clamp(Percent, 0.0f, 1.0f);
	PB_EnergyBar->SetPercent(Percent);
}


/**
 * UPlayerStatusWidget::UpdateHealthText
 *
 * 文本格式: "X/Y"（使用 FText::Format 支持本地化）
 */
void UPlayerStatusWidget::UpdateHealthText(int32 Current, int32 Max)
{
	if (Text_HealthValue)
	{
		Text_HealthValue->SetText(FText::Format(
			NSLOCTEXT("PlayerStatus", "HealthFormat", "{0}/{1}"),
			FText::AsNumber(Current),
			FText::AsNumber(Max)
		));
	}
}


/**
 * UPlayerStatusWidget::UpdateEnergyText
 *
 * 文本格式: "X/Y"
 */
void UPlayerStatusWidget::UpdateEnergyText(int32 Current, int32 Max)
{
	if (Text_EnergyValue)
	{
		Text_EnergyValue->SetText(FText::Format(
			NSLOCTEXT("PlayerStatus", "EnergyFormat", "{0}/{1}"),
			FText::AsNumber(Current),
			FText::AsNumber(Max)
		));
	}
}


// ==========================================
// 3. AC / ACE
// ==========================================

/**
 * UPlayerStatusWidget::UpdateACValue
 *
 * 1. 设置 Text_ACValue 数字
 * 2. 调用 RefreshACIconColor 同步刷新防护服图标
 */
void UPlayerStatusWidget::UpdateACValue(int32 Value)
{
	if (Text_ACValue)
	{
		Text_ACValue->SetText(FText::AsNumber(Value));
	}

	// AC 值变化时同步刷新防护服图标颜色
	RefreshACIconColor(Value);
}


/**
 * UPlayerStatusWidget::RefreshACIconColor
 *
 * AC 值越高，防护服越亮（蓝白色）；AC 值越低，防护服越暗（红黑色）
 * 分档: 0-25 低 / 26-50 中 / 51-75 良好 / 76+ 最佳
 */
void UPlayerStatusWidget::RefreshACIconColor(int32 CurrentAC)
{
	if (!Image_ACIcon)
	{
		return;
	}

	FLinearColor IconColor;

	// AC 值越高，防护服越亮（蓝白色）；AC 值越低，防护服越暗（红黑色）
	// 分档: 0-25 低 / 26-50 中 / 51-75 良好 / 76+ 最佳
	if (CurrentAC >= 76)
	{
		// 最佳状态: 明亮的蓝白色（防护服完好）
		IconColor = FLinearColor(0.6f, 0.85f, 1.0f, 1.0f);
	}
	else if (CurrentAC >= 51)
	{
		// 良好状态: 黄色（防护服轻微受损）
		IconColor = FLinearColor(1.0f, 0.9f, 0.2f, 1.0f);
	}
	else if (CurrentAC >= 26)
	{
		// 中等状态: 橙色（防护服明显受损）
		IconColor = FLinearColor(1.0f, 0.55f, 0.1f, 1.0f);
	}
	else
	{
		// 危急状态: 深红色（防护服濒临崩溃）
		IconColor = FLinearColor(0.9f, 0.1f, 0.1f, 1.0f);
	}

	Image_ACIcon->SetColorAndOpacity(IconColor);
}


/**
 * UPlayerStatusWidget::UpdateACEValue
 *
 * 简单设置 ACE 文本 + 默认白色
 */
void UPlayerStatusWidget::UpdateACEValue(int32 Value)
{
	if (Text_ACEValue)
	{
		Text_ACEValue->SetText(FText::AsNumber(Value));
		Text_ACEValue->SetColorAndOpacity(FLinearColor::White);
	}
}


/**
 * UPlayerStatusWidget::SetACEValueWithRank
 *
 * 根据 EACERankType 设置颜色:
 * - Gold: 金色 (1, 0.85, 0.2)
 * - White: 白色
 * - None: 灰色 (0.5, 0.5, 0.5)
 */
void UPlayerStatusWidget::SetACEValueWithRank(int32 Value, EACERankType RankType)
{
	if (!Text_ACEValue)
	{
		return;
	}

	Text_ACEValue->SetText(FText::AsNumber(Value));

	FLinearColor TextColor;
	switch (RankType)
	{
	case EACERankType::Gold:
		TextColor = FLinearColor(1.0f, 0.85f, 0.2f, 1.0f); // 金色
		break;
	case EACERankType::White:
		TextColor = FLinearColor(1.0f, 1.0f, 1.0f, 1.0f); // 白色
		break;
	default:
		TextColor = FLinearColor(0.5f, 0.5f, 0.5f, 1.0f); // 灰色（无 ACE 时）
		break;
	}

	Text_ACEValue->SetColorAndOpacity(TextColor);
}


// ==========================================
// 4. 角色 / 技能
// ==========================================

/**
 * UPlayerStatusWidget::UpdateCharacterIcon
 *
 * 设置 Image_CharacterIcon 的画刷
 */
void UPlayerStatusWidget::UpdateCharacterIcon(UTexture2D* Icon)
{
	if (Image_CharacterIcon && Icon)
	{
		Image_CharacterIcon->SetBrushFromTexture(Icon);
	}
}


/**
 * UPlayerStatusWidget::UpdateSkillIcon
 *
 * 1. 校验 SkillIndex 范围
 * 2. HB_SkillBar->GetChildAt(SkillIndex) 获取对应槽位
 * 3. Cast 为 UImage
 * 4. 设置画刷并显示
 */
void UPlayerStatusWidget::UpdateSkillIcon(int32 SkillIndex, UTexture2D* Icon)
{
	if (!HB_SkillBar || SkillIndex < 0)
	{
		return;
	}

	// 获取技能栏中的图标控件
	if (UWidget* SkillWidget = HB_SkillBar->GetChildAt(SkillIndex))
	{
		if (UImage* SkillIcon = Cast<UImage>(SkillWidget))
		{
			if (Icon)
			{
				SkillIcon->SetBrushFromTexture(Icon);
				SkillIcon->SetVisibility(ESlateVisibility::Visible);
			}
		}
	}
}


/**
 * UPlayerStatusWidget::SetSkillCooldown
 *
 * 设置冷却遮罩透明度
 * 冷却百分比越高, 遮罩越透明 (冷却完毕)
 * @param SkillIndex 技能槽索引
 * @param CooldownPercent 冷却百分比 (0~1)
 */
void UPlayerStatusWidget::SetSkillCooldown(int32 SkillIndex, float CooldownPercent)
{
	if (!HB_SkillBar || SkillIndex < 0 || CooldownPercent < 0.0f)
	{
		return;
	}

	// 获取对应的冷却遮罩控件并设置透明度
	if (SkillIndex < SkillCooldownOverlays.Num())
	{
		if (UImage* Overlay = SkillCooldownOverlays[SkillIndex])
		{
			// 冷却百分比越高，遮罩越透明
			float Opacity = 1.0f - FMath::Clamp(CooldownPercent, 0.0f, 1.0f);
			Overlay->SetRenderOpacity(Opacity);
		}
	}
}
