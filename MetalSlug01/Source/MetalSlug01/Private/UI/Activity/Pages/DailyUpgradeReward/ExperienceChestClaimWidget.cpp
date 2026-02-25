/**
 * @file ExperienceChestClaimWidget.cpp
 * @brief 经验宝箱领取Widget实现
 * @author AI Assistant
 * @date 2026
 * @version 1.0
 *
 * @details 实现经验宝箱领取Widget的核心功能
 */

#include "UI/Activity/Pages/DailyUpgradeReward/ExperienceChestClaimWidget.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "Components/ProgressBar.h"
#include "Kismet/GameplayStatics.h"
#include "UI/Activity/Core/UpgradeActivitySubsystem.h"

bool UExperienceChestClaimWidget::Initialize()
{
	if (!Super::Initialize())
	{
		return false;
	}

	// 初始化默认值
	CurrentChestCount = 5;
	CurrentExperience = 0;
	MaxExperience = 100;

	// 绑定按钮点击事件
	if (ChestClaimButton)
	{
		ChestClaimButton->OnClicked.AddDynamic(this, &UExperienceChestClaimWidget::OnChestClaimButtonClicked);
	}

	// 初始化UI显示
	UpdateChestCount();
	UpdateExperienceDisplay();
	UpdateProgressBar();
	HideSuccessEffect();

	return true;
}

void UExperienceChestClaimWidget::NativeConstruct()
{
	Super::NativeConstruct();
}

void UExperienceChestClaimWidget::NativeDestruct()
{
	// 解绑按钮事件
	if (ChestClaimButton)
	{
		ChestClaimButton->OnClicked.RemoveDynamic(this, &UExperienceChestClaimWidget::OnChestClaimButtonClicked);
	}

	Super::NativeDestruct();
}

void UExperienceChestClaimWidget::OnChestClaimButtonClicked()
{
	if (CurrentChestCount > 0)
	{
		// 减少宝箱数量
		CurrentChestCount--;
		
		// 增加经验值
		CurrentExperience += 20;
		if (CurrentExperience > MaxExperience)
		{
			CurrentExperience = MaxExperience;
		}

		// 更新UI显示
		UpdateChestCount();
		UpdateExperienceDisplay();
		UpdateProgressBar();

		// 显示领取成功效果
		ShowSuccessEffect();

		UE_LOG(LogTemp, Log, TEXT("ExperienceChestClaimPage: 领取成功，剩余宝箱数: %d，当前经验值: %d"), 
			CurrentChestCount, CurrentExperience);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("ExperienceChestClaimPage: 宝箱数量不足"));
	}
}

void UExperienceChestClaimWidget::UpdateChestCount()
{
	if (ChestCountText)
	{
		FString CountText = FString::Printf(TEXT("X%d"), CurrentChestCount);
		ChestCountText->SetText(FText::FromString(CountText));
	}
}

void UExperienceChestClaimWidget::UpdateExperienceDisplay()
{
	if (ExperienceText)
	{
		FString ExpText = FString::Printf(TEXT("%d/%d"), CurrentExperience, MaxExperience);
		ExperienceText->SetText(FText::FromString(ExpText));
	}
}

void UExperienceChestClaimWidget::UpdateProgressBar()
{
	if (ExperienceProgressBar)
	{
		float Progress = (MaxExperience > 0) ? (float)CurrentExperience / (float)MaxExperience : 0.0f;
		ExperienceProgressBar->SetPercent(Progress);
	}
}

void UExperienceChestClaimWidget::ShowSuccessEffect()
{
	if (SuccessText)
	{
		SuccessText->SetVisibility(ESlateVisibility::Visible);
	}

	// 可以在这里添加动画效果或其他视觉反馈
	FTimerHandle SuccessTimerHandle;
	GetWorld()->GetTimerManager().SetTimer(SuccessTimerHandle, this, &UExperienceChestClaimWidget::HideSuccessEffect, 2.0f, false);
}

void UExperienceChestClaimWidget::HideSuccessEffect()
{
	if (SuccessText)
	{
		SuccessText->SetVisibility(ESlateVisibility::Hidden);
	}
}

void UExperienceChestClaimWidget::UpdateVisualStatus(bool bIsClaimed)
{
	if (ChestClaimButton)
	{
		if (bIsClaimed)
		{
			// 已领取状态：禁用按钮，改变视觉样式
			ChestClaimButton->SetIsEnabled(false);
			ChestClaimButton->SetColorAndOpacity(FLinearColor(0.5f, 0.5f, 0.5f, 0.7f)); // 灰色半透明
			
			// 可以添加其他视觉效果，比如添加已领取标签等
			if (ChestCountText)
			{
				ChestCountText->SetColorAndOpacity(FLinearColor(0.7f, 0.7f, 0.7f, 1.0f)); // 文本变灰
			}
		}
		else
		{
			// 未领取状态：启用按钮，恢复正常样式
			ChestClaimButton->SetIsEnabled(true);
			ChestClaimButton->SetColorAndOpacity(FLinearColor::White);
			
			if (ChestCountText)
			{
				ChestCountText->SetColorAndOpacity(FLinearColor::White);
			}
		}
	}
	
	UE_LOG(LogTemp, Log, TEXT("ExperienceChestClaimWidget: 视觉状态已更新，已领取: %s"), bIsClaimed ? TEXT("是") : TEXT("否"));
}

void UExperienceChestClaimWidget::SetChestBoxIcon(UTexture2D* BoxIcon)
{
	if (!BoxIcon)
	{
		UE_LOG(LogTemp, Warning, TEXT("ExperienceChestClaimWidget: 传入的BoxIcon为空"));
		return;
	}
	
	// 通过Button Style设置图标
	if (ChestClaimButton)
	{
		// 获取当前按钮样式
		FButtonStyle ButtonStyle = ChestClaimButton->GetStyle();
		
		// 设置正常状态的背景图片
		FSlateBrush NormalBrush;
		NormalBrush.SetResourceObject(BoxIcon);
		NormalBrush.ImageSize = FVector2D(64, 64); // 设置图标大小
		NormalBrush.DrawAs = ESlateBrushDrawType::Image;
		ButtonStyle.Normal = NormalBrush;
		
		// 设置按下状态的背景图片
		FSlateBrush PressedBrush;
		PressedBrush.SetResourceObject(BoxIcon);
		PressedBrush.ImageSize = FVector2D(64, 64);
		PressedBrush.DrawAs = ESlateBrushDrawType::Image;
		PressedBrush.TintColor = FSlateColor(FLinearColor(0.8f, 0.8f, 0.8f, 1.0f)); // 按下时稍微变暗
		ButtonStyle.Pressed = PressedBrush;
		
		// 设置悬停状态的背景图片
		FSlateBrush HoveredBrush;
		HoveredBrush.SetResourceObject(BoxIcon);
		HoveredBrush.ImageSize = FVector2D(64, 64);
		HoveredBrush.DrawAs = ESlateBrushDrawType::Image;
		HoveredBrush.TintColor = FSlateColor(FLinearColor(1.2f, 1.2f, 1.2f, 1.0f)); // 悬停时稍微变亮
		ButtonStyle.Hovered = HoveredBrush;
		
		// 应用新的按钮样式
		ChestClaimButton->SetStyle(ButtonStyle);
		
		UE_LOG(LogTemp, Log, TEXT("ExperienceChestClaimWidget: 成功通过Button Style设置宝箱图标"));
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("ExperienceChestClaimWidget: ChestClaimButton控件未绑定"));
	}
}

