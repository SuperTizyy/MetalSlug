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
	
	// 初始化SuccessText状态
	UpdateSuccessTextVisibility();
	
	// 默认禁用按钮交互，保持蓝图默认外观
	if (ChestClaimButton)
	{
		ChestClaimButton->SetVisibility(ESlateVisibility::Visible);
	}

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
	// 检查领取条件
	UGameInstance* GameInstance = GetGameInstance();
	if (!GameInstance)
	{
		UE_LOG(LogTemp, Warning, TEXT("ExperienceChestClaimWidget: 无法获取GameInstance"));
		return;
	}
	
	UUpgradeActivitySubsystem* Subsystem = GameInstance->GetSubsystem<UUpgradeActivitySubsystem>();
	if (!Subsystem)
	{
		UE_LOG(LogTemp, Warning, TEXT("ExperienceChestClaimWidget: 无法获取UpgradeActivitySubsystem"));
		return;
	}
	
	const FDailyUpgradeRewardConfigRow* Config = Subsystem->GetActivityConfig();
	if (!Config)
	{
		UE_LOG(LogTemp, Warning, TEXT("ExperienceChestClaimWidget: 无法获取活动配置"));
		return;
	}
	
	const FUpgradeRewardSaveRecord& Record = Subsystem->GetRecord();
	
	// 检查是否已领取
	if (Record.ChestClaimStatus.IsValidIndex(ChestIndex) && Record.ChestClaimStatus[ChestIndex] == 1)
	{
		UE_LOG(LogTemp, Log, TEXT("ExperienceChestClaimWidget: 宝箱%d已领取，无法重复领取"), ChestIndex);
		return; // 已领取，不处理点击
	}
	
	// 检查经验值条件
	if (!Config->TaskRelatedValues.IsValidIndex(ChestIndex))
	{
		UE_LOG(LogTemp, Warning, TEXT("ExperienceChestClaimWidget: TaskRelatedValues索引%d无效"), ChestIndex);
		return;
	}
	
	int32 RequiredExp = Config->TaskRelatedValues[ChestIndex];
	if (Record.CurrentExperience < RequiredExp)
	{
		UE_LOG(LogTemp, Log, TEXT("ExperienceChestClaimWidget: 经验值不足，当前:%d, 需要:%d"), Record.CurrentExperience, RequiredExp);
		return; // 经验值不足，不处理点击
	}
	
	// 条件满足，触发领取事件
	UE_LOG(LogTemp, Log, TEXT("ExperienceChestClaimWidget: 宝箱%d条件满足，触发领取事件"), ChestIndex);
	
	if (OnChestClaimRequested.IsBound())
	{
		OnChestClaimRequested.Broadcast(ChestIndex);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("ExperienceChestClaimWidget: OnChestClaimRequested事件未绑定"));
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
		// 获取当前经验和配置数据来计算进度
		UGameInstance* GameInstance = GetGameInstance();
		if (!GameInstance)
		{
			ExperienceProgressBar->SetPercent(0.0f);
			return;
		}
		
		UUpgradeActivitySubsystem* Subsystem = GameInstance->GetSubsystem<UUpgradeActivitySubsystem>();
		if (!Subsystem)
		{
			ExperienceProgressBar->SetPercent(0.0f);
			return;
		}
		
		const FDailyUpgradeRewardConfigRow* Config = Subsystem->GetActivityConfig();
		if (!Config)
		{
			ExperienceProgressBar->SetPercent(0.0f);
			return;
		}
		
		const FUpgradeRewardSaveRecord& Record = Subsystem->GetRecord();
		int32 CurrentExp = Record.CurrentExperience;
		
		// 根据宝箱索引计算进度条范围
		if (Config->TaskRelatedValues.IsValidIndex(ChestIndex))
		{
			int32 RequiredExp = Config->TaskRelatedValues[ChestIndex];
			
			// 计算进度百分比 - 按照正确的区间规则
			float Progress = 0.0f;
			
			if (ChestIndex == 0)
			{
				// 第一个进度条：0-45 对应 0%-100%
				int32 LowerBound = 0;
				int32 UpperBound = 45;
				int32 Range = UpperBound - LowerBound;
				
				if (Range > 0)
				{
					int32 CurrentInRange = FMath::Max(0, CurrentExp - LowerBound);
					Progress = FMath::Clamp((float)CurrentInRange / (float)Range, 0.0f, 1.0f);
					UE_LOG(LogTemp, Log, TEXT("ExperienceChestClaimWidget[%d]: 第一个进度条 范围[0-45] 当前经验:%d, 范围内进度:%d/%d, 进度:%.2f%%"), 
						ChestIndex, CurrentExp, CurrentInRange, Range, Progress * 100);
				}
				else
				{
					Progress = (CurrentExp >= LowerBound) ? 1.0f : 0.0f;
				}
			}
			else
			{
				// 其他进度条：每个区间跨度30
				// 第2个：46-75，第3个：76-105，第4个：106-135...
				int32 LowerBound = 46 + (ChestIndex - 1) * 30;
				int32 UpperBound = LowerBound + 29; // 30个数字，所以是+29
				int32 Range = UpperBound - LowerBound;
				
				if (Range > 0)
				{
					int32 CurrentInRange = FMath::Max(0, CurrentExp - LowerBound);
					Progress = FMath::Clamp((float)CurrentInRange / (float)Range, 0.0f, 1.0f);
					UE_LOG(LogTemp, Log, TEXT("ExperienceChestClaimWidget[%d]: 进度条范围[%d-%d] 当前经验:%d, 范围内进度:%d/%d, 进度:%.2f%%"), 
						ChestIndex, LowerBound, UpperBound, CurrentExp, CurrentInRange, Range, Progress * 100);
				}
				else
				{
					Progress = (CurrentExp >= LowerBound) ? 1.0f : 0.0f;
				}
			}
			
			ExperienceProgressBar->SetPercent(Progress);
		}
		else
		{
			ExperienceProgressBar->SetPercent(0.0f);
		}
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
	if (!ChestClaimButton)
	{
		return;
	}
	
	// 获取当前经验和配置数据来判断完整条件
	UGameInstance* GameInstance = GetGameInstance();
	if (!GameInstance)
	{
		// 默认禁用按钮
		SetButtonDisabledState();
		UpdateSuccessTextVisibility(); // 更新SuccessText状态
		return;
	}
	
	UUpgradeActivitySubsystem* Subsystem = GameInstance->GetSubsystem<UUpgradeActivitySubsystem>();
	if (!Subsystem)
	{
		// 默认禁用按钮
		SetButtonDisabledState();
		UpdateSuccessTextVisibility(); // 更新SuccessText状态
		return;
	}
	
	const FDailyUpgradeRewardConfigRow* Config = Subsystem->GetActivityConfig();
	if (!Config)
	{
		// 默认禁用按钮
		SetButtonDisabledState();
		UpdateSuccessTextVisibility(); // 更新SuccessText状态
		return;
	}
	
	const FUpgradeRewardSaveRecord& Record = Subsystem->GetRecord();
	int32 CurrentExp = Record.CurrentExperience;
	
	// 判断按钮状态
	if (bIsClaimed)
	{
		// 已领取状态
		SetButtonClaimedState();
		UE_LOG(LogTemp, Log, TEXT("ExperienceChestClaimWidget: 按钮已领取 - 索引:%d"), ChestIndex);
	}
	else
	{
		// 未领取状态，检查经验值条件
		bool bHasEnoughExp = false;
		if (Config->TaskRelatedValues.IsValidIndex(ChestIndex))
		{
			int32 RequiredExp = Config->TaskRelatedValues[ChestIndex];
			bHasEnoughExp = (CurrentExp >= RequiredExp);
		}
		
		if (bHasEnoughExp)
		{
			// 满足领取条件：启用按钮
			SetButtonEnabledState();
			UE_LOG(LogTemp, Log, TEXT("ExperienceChestClaimWidget: 按钮启用 - 索引:%d, 当前经验:%d"), ChestIndex, CurrentExp);
		}
		else
		{
			// 不满足领取条件：禁用按钮但保持正常颜色
			SetButtonDisabledState();
			UE_LOG(LogTemp, Log, TEXT("ExperienceChestClaimWidget: 按钮条件不足 - 索引:%d, 当前经验:%d"), ChestIndex, CurrentExp);
		}
	}
	
	// 更新SuccessText显示状态
	UpdateSuccessTextVisibility();
}

void UExperienceChestClaimWidget::SetButtonEnabledState()
{
	if (!ChestClaimButton)
		return;
	
	// 启用按钮交互 - 保持Visible状态以接收点击
	ChestClaimButton->SetVisibility(ESlateVisibility::Visible);
	
	// 显示高亮框（如果有）
	if (HighlightFrameImage)
	{
		HighlightFrameImage->SetVisibility(ESlateVisibility::Visible);
	}
}

void UExperienceChestClaimWidget::SetButtonDisabledState()
{
	if (!ChestClaimButton)
		return;
	
	// 保持Visible状态以接收点击，但在点击处理中会检查条件
	ChestClaimButton->SetVisibility(ESlateVisibility::Visible);
	
	// 隐藏高亮框
	if (HighlightFrameImage)
	{
		HighlightFrameImage->SetVisibility(ESlateVisibility::Hidden);
	}
}

void UExperienceChestClaimWidget::SetButtonClaimedState()
{
	if (!ChestClaimButton)
		return;
	
	// 已领取状态 - 保持Visible状态以接收点击，但在点击处理中会检查条件
	ChestClaimButton->SetVisibility(ESlateVisibility::Visible);
	
	// 隐藏高亮框
	if (HighlightFrameImage)
	{
		HighlightFrameImage->SetVisibility(ESlateVisibility::Hidden);
	}
}

void UExperienceChestClaimWidget::RefreshProgressBar()
{
	UpdateProgressBar();
}

void UExperienceChestClaimWidget::UpdateButtonState()
{
	// 获取当前状态并更新按钮
	UGameInstance* GameInstance = GetGameInstance();
	if (!GameInstance)
	{
		SetButtonDisabledState();
		UpdateSuccessTextVisibility(); // 更新SuccessText状态
		return;
	}

	UUpgradeActivitySubsystem* Subsystem = GameInstance->GetSubsystem<UUpgradeActivitySubsystem>();
	if (!Subsystem)
	{
		SetButtonDisabledState();
		UpdateSuccessTextVisibility(); // 更新SuccessText状态
		return;
	}

	const FDailyUpgradeRewardConfigRow* Config = Subsystem->GetActivityConfig();
	if (!Config)
	{
		SetButtonDisabledState();
		UpdateSuccessTextVisibility(); // 更新SuccessText状态
		return;
	}

	const FUpgradeRewardSaveRecord& Record = Subsystem->GetRecord();

	// 判断状态
	bool bIsClaimed = Record.ChestClaimStatus.IsValidIndex(ChestIndex) && Record.ChestClaimStatus[ChestIndex] == 1;
	
	if (bIsClaimed)
	{
		SetButtonClaimedState();
	}
	else
	{
		// 检查经验值条件
		bool bHasEnoughExp = false;
		if (Config->TaskRelatedValues.IsValidIndex(ChestIndex))
		{
			int32 RequiredExp = Config->TaskRelatedValues[ChestIndex];
			bHasEnoughExp = (Record.CurrentExperience >= RequiredExp);
		}
		
		if (bHasEnoughExp)
		{
			SetButtonEnabledState();
		}
		else
		{
			SetButtonDisabledState();
		}
	}
	
	// 更新SuccessText显示状态
	UpdateSuccessTextVisibility();
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

void UExperienceChestClaimWidget::SetChestIndex(int32 Index)
{
	ChestIndex = Index;
	UE_LOG(LogTemp, Log, TEXT("ExperienceChestClaimWidget: 设置宝箱索引为 %d"), Index);
	
	// 设置索引后立即更新SuccessText状态
	UpdateSuccessTextVisibility();
}

void UExperienceChestClaimWidget::UpdateSuccessTextVisibility()
{
	// 获取Subsystem数据来判断SuccessText显示状态
	UGameInstance* GameInstance = GetGameInstance();
	if (!GameInstance)
	{
		// 无法获取数据时隐藏SuccessText
		if (SuccessText)
		{
			SuccessText->SetVisibility(ESlateVisibility::Hidden);
		}
		return;
	}
	
	UUpgradeActivitySubsystem* Subsystem = GameInstance->GetSubsystem<UUpgradeActivitySubsystem>();
	if (!Subsystem)
	{
		// 无法获取Subsystem时隐藏SuccessText
		if (SuccessText)
		{
			SuccessText->SetVisibility(ESlateVisibility::Hidden);
		}
		return;
	}
	
	const FUpgradeRewardSaveRecord& Record = Subsystem->GetRecord();
	
	// 根据ChestClaimStatus数组数据控制SuccessText显示
	bool bIsClaimed = Record.ChestClaimStatus.IsValidIndex(ChestIndex) && Record.ChestClaimStatus[ChestIndex] == 1;
	
	if (SuccessText)
	{
		if (bIsClaimed)
		{
			// 已领取状态：显示SuccessText
			SuccessText->SetVisibility(ESlateVisibility::Visible);
			UE_LOG(LogTemp, Log, TEXT("ExperienceChestClaimWidget: 宝箱%d已领取，显示SuccessText"), ChestIndex);
		}
		else
		{
			// 未领取状态：隐藏SuccessText
			SuccessText->SetVisibility(ESlateVisibility::Hidden);
			UE_LOG(LogTemp, Log, TEXT("ExperienceChestClaimWidget: 宝箱%d未领取，隐藏SuccessText"), ChestIndex);
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("ExperienceChestClaimWidget: SuccessText控件未绑定"));
	}
}