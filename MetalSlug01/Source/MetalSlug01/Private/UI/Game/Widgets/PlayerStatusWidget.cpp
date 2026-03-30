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
}

void UPlayerStatusWidget::UpdateACEValue(int32 Value)
{
	if (Text_ACEValue)
	{
		Text_ACEValue->SetText(FText::AsNumber(Value));
	}
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