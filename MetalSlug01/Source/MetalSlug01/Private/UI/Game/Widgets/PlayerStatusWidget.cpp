#include "UI/Game/Widgets/PlayerStatusWidget.h"
#include "Components/ProgressBar.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "Components/HorizontalBox.h"

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

void UPlayerStatusWidget::UpdateHealth(float Current, float Max)
{
	if (!PB_HealthBar)
	{
		return;
	}

	float Percent = (Max > 0.0f) ? (Current / Max) : 0.0f;
	Percent = FMath::Clamp(Percent, 0.0f, 1.0f);
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

void UPlayerStatusWidget::UpdateACValue(int32 Value)
{
	if (Text_ACValue)
	{
		Text_ACValue->SetText(FText::AsNumber(Value));
	}

	// AC 值变化时同步刷新防护服图标颜色
	RefreshACIconColor(Value);
}

void UPlayerStatusWidget::RefreshACIconColor(int32 CurrentAC)
{
	if (!Image_ACIcon)
	{
		return;
	}

	FLinearColor IconColor;

	// AC 值越高，防护服越亮（蓝白色）；AC 值越低，防护服越暗（红黑色）
	// 分档：0-25 低 / 26-50 中 / 51-75 良好 / 76+ 最佳
	if (CurrentAC >= 76)
	{
		// 最佳状态：明亮的蓝白色（防护服完好）
		IconColor = FLinearColor(0.6f, 0.85f, 1.0f, 1.0f);
	}
	else if (CurrentAC >= 51)
	{
		// 良好状态：黄色（防护服轻微受损）
		IconColor = FLinearColor(1.0f, 0.9f, 0.2f, 1.0f);
	}
	else if (CurrentAC >= 26)
	{
		// 中等状态：橙色（防护服明显受损）
		IconColor = FLinearColor(1.0f, 0.55f, 0.1f, 1.0f);
	}
	else
	{
		// 危急状态：深红色（防护服濒临崩溃）
		IconColor = FLinearColor(0.9f, 0.1f, 0.1f, 1.0f);
	}

	Image_ACIcon->SetColorAndOpacity(IconColor);
}

void UPlayerStatusWidget::UpdateACEValue(int32 Value)
{
	if (Text_ACEValue)
	{
		Text_ACEValue->SetText(FText::AsNumber(Value));
		Text_ACEValue->SetColorAndOpacity(FLinearColor::White);
	}
}

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
		TextColor = FLinearColor(0.5f, 0.5f, 0.5f, 1.0f); // 灰色（无ACE时）
		break;
	}

	Text_ACEValue->SetColorAndOpacity(TextColor);
}

void UPlayerStatusWidget::UpdateCharacterIcon(UTexture2D* Icon)
{
	if (Image_CharacterIcon && Icon)
	{
		Image_CharacterIcon->SetBrushFromTexture(Icon);
	}
}

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