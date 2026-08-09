// 版权声明：在项目设置的描述页面填写您的版权信息。

/**
 * @file ExperienceChestClaimWidget.cpp
 * @brief 经验宝箱领取 Widget 实现
 * @author AI Assistant
 * @date 2026
 * @version 1.0
 *
 * @details 实现经验宝箱领取 Widget 的核心功能
 */

// ==========================================
// 头文件包含区
// ==========================================
#include "UI/Activity/Pages/DailyUpgradeReward/ExperienceChestClaimWidget.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "Components/ProgressBar.h"
#include "Components/SizeBox.h"         // USizeBox 控件类型
#include "Components/SizeBoxSlot.h"     // ⚠️ 2026-08-10 Padding 在 Slot 上, 不在 USizeBox 自身; USizeBoxSlot::SetPadding(FMargin) 才是 API
#include "Kismet/GameplayStatics.h"
#include "Systems/Activity/UpgradeActivitySubsystem.h"


// ==========================================
// 1. 生命周期
// ==========================================

/**
 * UExperienceChestClaimWidget::Initialize
 *
 * 1. 默认值: ChestCount=5 / Exp=0 / Max=100
 * 2. 绑定 ChestClaimButton
 * 3. 调用所有 Update* 初始化 UI
 * 4. 默认 Visible
 */
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

	// 初始化 UI 显示
	UpdateChestCount();
	UpdateExperienceDisplay();
	UpdateProgressBar();
	HideSuccessEffect();

	// 初始化 SuccessText 状态
	UpdateSuccessTextVisibility();

	// 初始化 DiamondIcon 颜色
	UpdateDiamondIconColor();

	// 初始化 ExperienceText 颜色
	UpdateExperienceTextColor();

	// 默认禁用按钮交互，保持蓝图默认外观
	if (ChestClaimButton)
	{
		ChestClaimButton->SetVisibility(ESlateVisibility::Visible);
	}

	return true;
}


/**
 * UExperienceChestClaimWidget::NativeConstruct
 */
void UExperienceChestClaimWidget::NativeConstruct()
{
	Super::NativeConstruct();
}


/**
 * UExperienceChestClaimWidget::NativeDestruct
 *
 * 解绑按钮事件
 */
void UExperienceChestClaimWidget::NativeDestruct()
{
	// 解绑按钮事件
	if (ChestClaimButton)
	{
		ChestClaimButton->OnClicked.RemoveDynamic(this, &UExperienceChestClaimWidget::OnChestClaimButtonClicked);
	}

	Super::NativeDestruct();
}


// ==========================================
// 2. 按钮点击
// ==========================================

/**
 * UExperienceChestClaimWidget::OnChestClaimButtonClicked
 *
 * 1. 防御链: GameInstance / UUpgradeActivitySubsystem / Config / Record
 * 2. 区分 FixedPrize / 普通 Widget（ChestIndex >= LastIndex）
 * 3. 校验 ChestClaimStatus[TargetIndex] != 1
 * 4. 校验 CurrentExperience >= TaskRelatedValues[TargetIndex]
 * 5. 广播 OnChestClaimRequested(TargetIndex)
 */
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

	// 区分 FixedPrizeWidget 和普通 ExperienceChestWidget 的逻辑
	int32 TargetIndex = ChestIndex; // 默认使用自身索引

	// 如果是 FixedPrizeWidget（通常 ChestIndex 较大），使用最后一个索引
	int32 LastIndex = Config->TaskRelatedValues.Num() - 1;
	if (ChestIndex >= LastIndex && LastIndex >= 0)
	{
		TargetIndex = LastIndex; // FixedPrizeWidget 使用最后一个索引
		UE_LOG(LogTemp, Log, TEXT("ExperienceChestClaimWidget: 检测为FixedPrizeWidget，使用索引%d"), TargetIndex);
	}
	else
	{
		TargetIndex = ChestIndex; // 普通 ExperienceChestWidget 使用自身索引
		UE_LOG(LogTemp, Log, TEXT("ExperienceChestClaimWidget: 检测为普通Widget，使用索引%d"), TargetIndex);
	}

	// 检查是否已领取（读取目标索引的 ChestClaimStatus）
	if (Record.ChestClaimStatus.IsValidIndex(TargetIndex) && Record.ChestClaimStatus[TargetIndex] == 1)
	{
		UE_LOG(LogTemp, Log, TEXT("ExperienceChestClaimWidget: 宝箱%d已领取，无法重复领取"), TargetIndex);
		return; // 已领取，不处理点击
	}

	// 检查经验值条件（读取 TaskRelatedValues 数组目标索引的数据）
	if (!Config->TaskRelatedValues.IsValidIndex(TargetIndex))
	{
		UE_LOG(LogTemp, Warning, TEXT("ExperienceChestClaimWidget: TaskRelatedValues索引%d无效"), TargetIndex);
		return;
	}

	int32 RequiredExp = Config->TaskRelatedValues[TargetIndex];
	if (Record.CurrentExperience < RequiredExp)
	{
		UE_LOG(LogTemp, Log, TEXT("ExperienceChestClaimWidget: 经验值不足，当前:%d, 需要:%d"), Record.CurrentExperience, RequiredExp);
		return; // 经验值不足，不处理点击
	}

	// 条件满足，触发领取事件
	UE_LOG(LogTemp, Log, TEXT("ExperienceChestClaimWidget: 宝箱%d条件满足，触发领取事件"), TargetIndex);

	if (OnChestClaimRequested.IsBound())
	{
		// 传递目标索引作为 ChestIndex
		OnChestClaimRequested.Broadcast(TargetIndex);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("ExperienceChestClaimWidget: OnChestClaimRequested事件未绑定"));
	}
}


// ==========================================
// 3. UI 更新
// ==========================================

/**
 * UExperienceChestClaimWidget::UpdateChestCount
 *
 * 格式: "X5"
 */
void UExperienceChestClaimWidget::UpdateChestCount()
{
	if (ChestCountText)
	{
		FString CountText = FString::Printf(TEXT("X%d"), CurrentChestCount);
		ChestCountText->SetText(FText::FromString(CountText));
	}
}


/**
 * UExperienceChestClaimWidget::UpdateExperienceDisplay
 *
 * 格式: "0/100"
 */
void UExperienceChestClaimWidget::UpdateExperienceDisplay()
{
	if (ExperienceText)
	{
		FString ExpText = FString::Printf(TEXT("%d/%d"), CurrentExperience, MaxExperience);
		ExperienceText->SetText(FText::FromString(ExpText));
	}
}


/**
 * UExperienceChestClaimWidget::UpdateProgressBar
 *
 * 区间规则:
 * - ChestIndex == 0: 区间 [5, 45], 范围 40
 * - ChestIndex >  0: 区间 [46 + (i-1)*30, 75 + (i-1)*30], 范围 30
 *
 * 防御链: 任一环节缺失都 SetPercent(0)
 */
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
				// 第一个进度条: 5-45 对应 0%-100%
				int32 LowerBound = 5;
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
				// 其他进度条: 每个区间跨度 30
				// 第 2 个: 46-75，第 3 个: 76-105，第 4 个: 106-135...
				int32 LowerBound = 46 + (ChestIndex - 1) * 30;
				int32 UpperBound = LowerBound + 29; // 30 个数字，所以是 +29
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


/**
 * UExperienceChestClaimWidget::ShowSuccessEffect
 *
 * 1. SuccessText -> Visible
 * 2. SetTimer 2 秒后自动 Hide
 */
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


/**
 * UExperienceChestClaimWidget::HideSuccessEffect
 */
void UExperienceChestClaimWidget::HideSuccessEffect()
{
	if (SuccessText)
	{
		SuccessText->SetVisibility(ESlateVisibility::Hidden);
	}
}


// ==========================================
// 4. 视觉状态
// ==========================================

/**
 * UExperienceChestClaimWidget::UpdateVisualStatus
 *
 * 1. 防御链
 * 2. bIsClaimed = true -> SetButtonClaimedState
 * 3. bIsClaimed = false + 经验足够 -> SetButtonEnabledState
 * 4. bIsClaimed = false + 经验不够 -> SetButtonDisabledState
 * 5. 同步 SuccessText / DiamondIcon / ExperienceText
 */
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
		UpdateSuccessTextVisibility(); // 更新 SuccessText 状态
		return;
	}

	UUpgradeActivitySubsystem* Subsystem = GameInstance->GetSubsystem<UUpgradeActivitySubsystem>();
	if (!Subsystem)
	{
		// 默认禁用按钮
		SetButtonDisabledState();
		UpdateSuccessTextVisibility(); // 更新 SuccessText 状态
		return;
	}

	const FDailyUpgradeRewardConfigRow* Config = Subsystem->GetActivityConfig();
	if (!Config)
	{
		// 默认禁用按钮
		SetButtonDisabledState();
		UpdateSuccessTextVisibility(); // 更新 SuccessText 状态
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
			// 满足领取条件: 启用按钮
			SetButtonEnabledState();
			UE_LOG(LogTemp, Log, TEXT("ExperienceChestClaimWidget: 按钮启用 - 索引:%d, 当前经验:%d"), ChestIndex, CurrentExp);
		}
		else
		{
			// 不满足领取条件: 禁用按钮但保持正常颜色
			SetButtonDisabledState();
			UE_LOG(LogTemp, Log, TEXT("ExperienceChestClaimWidget: 按钮条件不足 - 索引:%d, 当前经验:%d"), ChestIndex, CurrentExp);
		}
	}

	// 更新 SuccessText 显示状态
	UpdateSuccessTextVisibility();

	// 更新 DiamondIcon 颜色
	UpdateDiamondIconColor();

	// 更新 ExperienceText 颜色
	UpdateExperienceTextColor();
}


// ==========================================
// 5. 按钮状态
// ==========================================

/**
 * UExperienceChestClaimWidget::SetButtonEnabledState
 *
 * Visible + HighlightFrameImage Visible
 */
void UExperienceChestClaimWidget::SetButtonEnabledState()
{
	if (!ChestClaimButton)
		return;

	// 启用按钮交互 - 保持 Visible 状态以接收点击
	ChestClaimButton->SetVisibility(ESlateVisibility::Visible);

	// 显示高亮框（如果有）
	if (HighlightFrameImage)
	{
		HighlightFrameImage->SetVisibility(ESlateVisibility::Visible);
	}
}


/**
 * UExperienceChestClaimWidget::SetButtonDisabledState
 *
 * Visible（仍接收点击, 在点击中校验）+ HighlightFrameImage Hidden
 */
void UExperienceChestClaimWidget::SetButtonDisabledState()
{
	if (!ChestClaimButton)
		return;

	// 保持 Visible 状态以接收点击，但在点击处理中会检查条件
	ChestClaimButton->SetVisibility(ESlateVisibility::Visible);

	// 隐藏高亮框
	if (HighlightFrameImage)
	{
		HighlightFrameImage->SetVisibility(ESlateVisibility::Hidden);
	}
}


/**
 * UExperienceChestClaimWidget::SetButtonClaimedState
 *
 * Visible + HighlightFrameImage Hidden
 */
void UExperienceChestClaimWidget::SetButtonClaimedState()
{
	if (!ChestClaimButton)
		return;

	// 已领取状态 - 保持 Visible 状态以接收点击，但在点击处理中会检查条件
	ChestClaimButton->SetVisibility(ESlateVisibility::Visible);

	// 隐藏高亮框
	if (HighlightFrameImage)
	{
		HighlightFrameImage->SetVisibility(ESlateVisibility::Hidden);
	}
}


/**
 * UExperienceChestClaimWidget::RefreshProgressBar
 */
void UExperienceChestClaimWidget::RefreshProgressBar()
{
	UpdateProgressBar();
}


// ==========================================
// 6. 综合状态更新
// ==========================================

/**
 * UExperienceChestClaimWidget::UpdateButtonState
 *
 * 1. 防御链
 * 2. 区分 FixedPrize / 普通 Widget
 * 3. 根据 ChestClaimStatus 判定已领
 * 4. 根据 CurrentExperience 判定可领
 * 5. 同步所有视觉状态
 */
void UExperienceChestClaimWidget::UpdateButtonState()
{
	// 获取当前状态并更新按钮
	UGameInstance* GameInstance = GetGameInstance();
	if (!GameInstance)
	{
		SetButtonDisabledState();
		UpdateSuccessTextVisibility(); // 更新 SuccessText 状态
		return;
	}

	UUpgradeActivitySubsystem* Subsystem = GameInstance->GetSubsystem<UUpgradeActivitySubsystem>();
	if (!Subsystem)
	{
		SetButtonDisabledState();
		UpdateSuccessTextVisibility(); // 更新 SuccessText 状态
		return;
	}

	const FDailyUpgradeRewardConfigRow* Config = Subsystem->GetActivityConfig();
	if (!Config)
	{
		SetButtonDisabledState();
		UpdateSuccessTextVisibility(); // 更新 SuccessText 状态
		return;
	}

	const FUpgradeRewardSaveRecord& Record = Subsystem->GetRecord();
	int32 CurrentExp = Record.CurrentExperience;

	// 区分 FixedPrizeWidget 和普通 ExperienceChestWidget 的逻辑
	int32 TargetIndex = ChestIndex; // 默认使用自身索引

	// 如果是 FixedPrizeWidget（通常 ChestIndex 较大或者有特殊标识），使用最后一个索引
	if (Config->TaskRelatedValues.IsValidIndex(ChestIndex))
	{
		// 检查是否为 FixedPrizeWidget 逻辑
		int32 LastIndex = Config->TaskRelatedValues.Num() - 1;
		// 如果当前索引接近或等于最后一个索引，则认为是 FixedPrizeWidget
		if (ChestIndex >= LastIndex && LastIndex >= 0)
		{
			TargetIndex = LastIndex; // FixedPrizeWidget 使用最后一个索引
			UE_LOG(LogTemp, Log, TEXT("ExperienceChestClaimWidget: 检测为FixedPrizeWidget，使用最后一个索引%d"), TargetIndex);
		}
		else
		{
			TargetIndex = ChestIndex; // 普通 ExperienceChestWidget 使用自身索引
			UE_LOG(LogTemp, Log, TEXT("ExperienceChestClaimWidget: 检测为普通ExperienceChestWidget，使用索引%d"), TargetIndex);
		}
	}

	// 判断按钮状态
	bool bIsClaimed = Record.ChestClaimStatus.IsValidIndex(TargetIndex) && Record.ChestClaimStatus[TargetIndex] == 1;

	if (bIsClaimed)
	{
		// 已领取状态
		SetButtonClaimedState();
		UE_LOG(LogTemp, Log, TEXT("ExperienceChestClaimWidget: 按钮已领取 - 索引:%d"), TargetIndex);
	}
	else
	{
		// 未领取状态，检查经验值条件
		bool bHasEnoughExp = false;
		if (Config->TaskRelatedValues.IsValidIndex(TargetIndex))
		{
			int32 RequiredExp = Config->TaskRelatedValues[TargetIndex];
			bHasEnoughExp = (CurrentExp >= RequiredExp);
		}

		if (bHasEnoughExp)
		{
			// 满足领取条件: 启用按钮
			SetButtonEnabledState();
			UE_LOG(LogTemp, Log, TEXT("ExperienceChestClaimWidget: 按钮启用 - 索引:%d, 当前经验:%d"), TargetIndex, CurrentExp);
		}
		else
		{
			// 不满足领取条件: 禁用按钮但保持正常颜色
			SetButtonDisabledState();
			UE_LOG(LogTemp, Log, TEXT("ExperienceChestClaimWidget: 按钮条件不足 - 索引:%d, 当前经验:%d"), TargetIndex, CurrentExp);
		}
	}

	// 更新 SuccessText 显示状态
	UpdateSuccessTextVisibility();

	// 更新 DiamondIcon 颜色
	UpdateDiamondIconColor();

	// 更新 ExperienceText 颜色
	UpdateExperienceTextColor();
}


// ==========================================
// 7. 公开接口 - SetChestBoxIcon
// ==========================================

/**
 * UExperienceChestClaimWidget::SetChestBoxIcon
 *
 * 通过 Button Style 设置图标
 * 1. Normal: BoxIcon (64x64)
 * 2. Pressed: BoxIcon (64x64) 暗 (0.8, 0.8, 0.8)
 * 3. Hovered: BoxIcon (64x64) 亮 (1.2, 1.2, 1.2)
 */
void UExperienceChestClaimWidget::SetChestBoxIcon(UTexture2D* BoxIcon)
{
	if (!BoxIcon)
	{
		UE_LOG(LogTemp, Warning, TEXT("ExperienceChestClaimWidget: 传入的BoxIcon为空"));
		return;
	}

	// 通过 Button Style 设置图标
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


// ==========================================
// 8. 公开接口 - SetChestIndex
// ==========================================

/**
 * UExperienceChestClaimWidget::SetChestIndex
 *
 * 1. 设置 ChestIndex
 * 2. 立即 UpdateSuccessTextVisibility
 */
void UExperienceChestClaimWidget::SetChestIndex(int32 Index)
{
	ChestIndex = Index;
	UE_LOG(LogTemp, Log, TEXT("ExperienceChestClaimWidget: 设置宝箱索引为 %d"), Index);

	// 设置索引后立即更新 SuccessText 状态
	UpdateSuccessTextVisibility();
}

// ==========================================
// 9.5 容器布局 API (2026-08-10)
// ==========================================

/**
 * @brief 设置 WBP_FixedPrizeWidget 内 SizeBox 的 Padding (Top + Bottom)
 *
 * 职责: ItemsScrollBox 中动态生成的子项专用, 控制其上下内边距
 *       页面单独 FixedPrizeWidget 不调用本方法 (布局由蓝图控制)
 *
 * @param PaddingTop    SizeBox 顶部内边距 (像素, 必须 >= 0)
 * @param PaddingBottom SizeBox 底部内边距 (像素, 必须 >= 0)
 *
 * 零兜底原则 (大厂架构):
 *  - SizeBoxPrizeSlot 为 null → Log Error + return (不静默吞错)
 *  - 参数 < 0 → Log Error + return (不允许负值, 避免布局计算异常)
 */
void UExperienceChestClaimWidget::SetPrizeSlotPadding(float PaddingTop, float PaddingBottom, float PaddingRight)
{
	// 1. 参数校验: 必须 >= 0 (负值会导致 Slate layout 计算异常)
	if (PaddingTop < 0.0f || PaddingBottom < 0.0f || PaddingRight < 0.0f)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[ExperienceChestClaimWidget] SetPrizeSlotPadding: 参数无效 (Top=%.2f, Bottom=%.2f, Right=%.2f), 必须 >= 0"),
			PaddingTop, PaddingBottom, PaddingRight);
		return;
	}

	// 2. 防御链: SizeBoxPrizeSlot 必须绑定
	if (!SizeBoxPrizeSlot)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[ExperienceChestClaimWidget] SetPrizeSlotPadding: SizeBoxPrizeSlot 控件未绑定 (WBP 蓝图里缺少 'SizeBoxPrizeSlot' 控件, 或命名不一致)"));
		return;
	}

	// 3. 应用 Padding: 设 Top + Bottom + Right, Left 保持 0 (不破坏现有水平布局)
	//    ⚠️ Padding 不在 USizeBox 自身, 而在它的唯一子项 Slot (USizeBoxSlot) 上
	//    UE 5.6 UMG 编辑器里 "SizeBox 控件面板上的 Top/Bottom/Right" 实际就是 Slot 的 FMargin
	//    API 路径: UContentWidget::GetContentSlot() → Cast<USizeBoxSlot> → SetPadding(FMargin)
	//    FMargin 4 参数构造顺序: (Left, Top, Right, Bottom)
	USizeBoxSlot* SizeBoxSlot = Cast<USizeBoxSlot>(SizeBoxPrizeSlot->GetContentSlot());
	if (!SizeBoxSlot)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[ExperienceChestClaimWidget] SetPrizeSlotPadding: SizeBox 上没有 Content Slot (WBP 蓝图里 SizeBoxPrizeSlot 可能没拖入子控件)"));
		return;
	}

	const FMargin NewPadding(0.0f, PaddingTop, PaddingRight, PaddingBottom);
	SizeBoxSlot->SetPadding(NewPadding);

	UE_LOG(LogTemp, Log,
		TEXT("[ExperienceChestClaimWidget] SetPrizeSlotPadding: 已设置 Padding Top=%.2f, Bottom=%.2f, Right=%.2f"),
		PaddingTop, PaddingBottom, PaddingRight);
}


// ==========================================
// 9. 颜色更新
// ==========================================

/**
 * UExperienceChestClaimWidget::UpdateSuccessTextVisibility
 *
 * 规则: ChestClaimStatus[TargetIndex] == 1 -> Visible / Hidden
 */
void UExperienceChestClaimWidget::UpdateSuccessTextVisibility()
{
	// 获取 Subsystem 数据来判断 SuccessText 显示状态
	UGameInstance* GameInstance = GetGameInstance();
	if (!GameInstance)
	{
		// 无法获取数据时隐藏 SuccessText
		if (SuccessText)
		{
			SuccessText->SetVisibility(ESlateVisibility::Hidden);
		}
		return;
	}

	UUpgradeActivitySubsystem* Subsystem = GameInstance->GetSubsystem<UUpgradeActivitySubsystem>();
	if (!Subsystem)
	{
		// 无法获取 Subsystem 时隐藏 SuccessText
		if (SuccessText)
		{
			SuccessText->SetVisibility(ESlateVisibility::Hidden);
		}
		return;
	}

	const FDailyUpgradeRewardConfigRow* Config = Subsystem->GetActivityConfig();
	if (!Config)
	{
		// 无法获取配置时隐藏 SuccessText
		if (SuccessText)
		{
			SuccessText->SetVisibility(ESlateVisibility::Hidden);
		}
		return;
	}

	const FUpgradeRewardSaveRecord& Record = Subsystem->GetRecord();

	// 区分 FixedPrizeWidget 和普通 ExperienceChestWidget 的逻辑
	int32 TargetIndex = ChestIndex; // 默认使用自身索引

	// 如果是 FixedPrizeWidget（通常 ChestIndex 较大），使用最后一个索引
	int32 LastIndex = Config->TaskRelatedValues.Num() - 1;
	if (ChestIndex >= LastIndex && LastIndex >= 0)
	{
		TargetIndex = LastIndex; // FixedPrizeWidget 使用最后一个索引
		UE_LOG(LogTemp, Log, TEXT("ExperienceChestClaimWidget: SuccessText检测为FixedPrizeWidget，使用索引%d"), TargetIndex);
	}
	else
	{
		TargetIndex = ChestIndex; // 普通 ExperienceChestWidget 使用自身索引
		UE_LOG(LogTemp, Log, TEXT("ExperienceChestClaimWidget: SuccessText检测为普通Widget，使用索引%d"), TargetIndex);
	}

	// 根据目标索引的 ChestClaimStatus 数据控制 SuccessText 显示
	if (Record.ChestClaimStatus.IsValidIndex(TargetIndex))
	{
		bool bIsClaimed = (Record.ChestClaimStatus[TargetIndex] == 1);

		if (SuccessText)
		{
			if (bIsClaimed)
			{
				// 已领取状态: 显示 SuccessText
				SuccessText->SetVisibility(ESlateVisibility::Visible);
				UE_LOG(LogTemp, Log, TEXT("ExperienceChestClaimWidget: 索引%d已领取，显示SuccessText"), TargetIndex);
			}
			else
			{
				// 未领取状态: 隐藏 SuccessText
				SuccessText->SetVisibility(ESlateVisibility::Hidden);
				UE_LOG(LogTemp, Log, TEXT("ExperienceChestClaimWidget: 索引%d未领取，隐藏SuccessText"), TargetIndex);
			}
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("ExperienceChestClaimWidget: ChestClaimStatus索引%d无效"), TargetIndex);
		if (SuccessText)
		{
			SuccessText->SetVisibility(ESlateVisibility::Hidden);
		}
	}
}


/**
 * UExperienceChestClaimWidget::UpdateDiamondIconColor
 *
 * 规则: CurrentExperience >= RequiredExp -> Yellow / 否则 Black
 */
void UExperienceChestClaimWidget::UpdateDiamondIconColor()
{
	// 获取 Subsystem 数据来判断 DiamondIconImage 颜色
	UGameInstance* GameInstance = GetGameInstance();
	if (!GameInstance)
	{
		// 无法获取数据时设置默认黑色
		if (DiamondIconImage)
		{
			DiamondIconImage->SetBrushTintColor(FSlateColor(FLinearColor::Black));
		}
		return;
	}

	UUpgradeActivitySubsystem* Subsystem = GameInstance->GetSubsystem<UUpgradeActivitySubsystem>();
	if (!Subsystem)
	{
		// 无法获取 Subsystem 时设置默认黑色
		if (DiamondIconImage)
		{
			DiamondIconImage->SetBrushTintColor(FSlateColor(FLinearColor::Black));
		}
		return;
	}

	const FDailyUpgradeRewardConfigRow* Config = Subsystem->GetActivityConfig();
	if (!Config)
	{
		// 无法获取配置时设置默认黑色
		if (DiamondIconImage)
		{
			DiamondIconImage->SetBrushTintColor(FSlateColor(FLinearColor::Black));
		}
		return;
	}

	const FUpgradeRewardSaveRecord& Record = Subsystem->GetRecord();
	int32 CurrentExp = Record.CurrentExperience;

	// 区分 FixedPrizeWidget 和普通 ExperienceChestWidget 的逻辑
	int32 TargetIndex = ChestIndex; // 默认使用自身索引

	// 如果是 FixedPrizeWidget（通常 ChestIndex 较大），使用最后一个索引
	int32 LastIndex = Config->TaskRelatedValues.Num() - 1;
	if (ChestIndex >= LastIndex && LastIndex >= 0)
	{
		TargetIndex = LastIndex; // FixedPrizeWidget 使用最后一个索引
		UE_LOG(LogTemp, Log, TEXT("ExperienceChestClaimWidget: DiamondIcon检测为FixedPrizeWidget，使用索引%d"), TargetIndex);
	}
	else
	{
		TargetIndex = ChestIndex; // 普通 ExperienceChestWidget 使用自身索引
		UE_LOG(LogTemp, Log, TEXT("ExperienceChestClaimWidget: DiamondIcon检测为普通Widget，使用索引%d"), TargetIndex);
	}

	// 判断是否满足条件: CurrentExperience >= TaskRelatedValues[TargetIndex]
	bool bConditionMet = false;
	if (Config->TaskRelatedValues.IsValidIndex(TargetIndex))
	{
		int32 RequiredExp = Config->TaskRelatedValues[TargetIndex];
		bConditionMet = (CurrentExp >= RequiredExp);

		UE_LOG(LogTemp, Log, TEXT("ExperienceChestClaimWidget[索引%d]: 当前经验:%d, 需要经验:%d, 条件%s"),
			TargetIndex, CurrentExp, RequiredExp, bConditionMet ? TEXT("满足") : TEXT("不满足"));
	}

	// 设置 DiamondIconImage 颜色
	if (DiamondIconImage)
	{
		if (bConditionMet)
		{
			// 条件满足: 显示黄色
			DiamondIconImage->SetBrushTintColor(FSlateColor(FLinearColor::Yellow));
			UE_LOG(LogTemp, Log, TEXT("ExperienceChestClaimWidget[索引%d]: DiamondIcon设置为黄色"), TargetIndex);
		}
		else
		{
			// 条件不满足: 显示黑色
			DiamondIconImage->SetBrushTintColor(FSlateColor(FLinearColor::Black));
			UE_LOG(LogTemp, Log, TEXT("ExperienceChestClaimWidget[索引%d]: DiamondIcon设置为黑色"), TargetIndex);
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("ExperienceChestClaimWidget: DiamondIconImage控件未绑定"));
	}
}


/**
 * UExperienceChestClaimWidget::UpdateExperienceTextColor
 *
 * 规则: 满足 -> Black / 不满足 -> Yellow
 */
void UExperienceChestClaimWidget::UpdateExperienceTextColor()
{
	// 获取 Subsystem 数据来判断 ExperienceText 颜色
	UGameInstance* GameInstance = GetGameInstance();
	if (!GameInstance)
	{
		// 无法获取数据时设置默认黄色
		if (ExperienceText)
		{
			ExperienceText->SetColorAndOpacity(FLinearColor::Yellow);
		}
		return;
	}

	UUpgradeActivitySubsystem* Subsystem = GameInstance->GetSubsystem<UUpgradeActivitySubsystem>();
	if (!Subsystem)
	{
		// 无法获取 Subsystem 时设置默认黄色
		if (ExperienceText)
		{
			ExperienceText->SetColorAndOpacity(FLinearColor::Yellow);
		}
		return;
	}

	const FDailyUpgradeRewardConfigRow* Config = Subsystem->GetActivityConfig();
	if (!Config)
	{
		// 无法获取配置时设置默认黄色
		if (ExperienceText)
		{
			ExperienceText->SetColorAndOpacity(FLinearColor::Yellow);
		}
		return;
	}

	const FUpgradeRewardSaveRecord& Record = Subsystem->GetRecord();
	int32 CurrentExp = Record.CurrentExperience;

	// 区分 FixedPrizeWidget 和普通 ExperienceChestWidget 的逻辑
	int32 TargetIndex = ChestIndex; // 默认使用自身索引

	// 如果是 FixedPrizeWidget（通常 ChestIndex 较大），使用最后一个索引
	int32 LastIndex = Config->TaskRelatedValues.Num() - 1;
	if (ChestIndex >= LastIndex && LastIndex >= 0)
	{
		TargetIndex = LastIndex; // FixedPrizeWidget 使用最后一个索引
		UE_LOG(LogTemp, Log, TEXT("ExperienceChestClaimWidget: ExperienceText检测为FixedPrizeWidget，使用索引%d"), TargetIndex);
	}
	else
	{
		TargetIndex = ChestIndex; // 普通 ExperienceChestWidget 使用自身索引
		UE_LOG(LogTemp, Log, TEXT("ExperienceChestClaimWidget: ExperienceText检测为普通Widget，使用索引%d"), TargetIndex);
	}

	// 检查 CurrentExperience 是否大于等于 TaskRelatedValues 中目标索引的值
	bool bConditionMet = false;

	UE_LOG(LogTemp, Log, TEXT("ExperienceChestClaimWidget[索引%d]: 当前经验:%d, TaskRelatedValues数量:%d"),
		TargetIndex, CurrentExp, Config->TaskRelatedValues.Num());

	// 只检查目标索引的 TaskRelatedValues 值
	if (Config->TaskRelatedValues.IsValidIndex(TargetIndex))
	{
		int32 RequiredExp = Config->TaskRelatedValues[TargetIndex];
		bConditionMet = (CurrentExp >= RequiredExp);

		UE_LOG(LogTemp, Log, TEXT("ExperienceChestClaimWidget[索引%d]: 经验值%d %s 需要值%d，条件%s"),
			TargetIndex, CurrentExp,
			bConditionMet ? TEXT(">=") : TEXT("<"),
			RequiredExp,
			bConditionMet ? TEXT("满足") : TEXT("不满足"));
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("ExperienceChestClaimWidget[索引%d]: TaskRelatedValues索引无效"), TargetIndex);
	}

	// 设置 ExperienceText 颜色
	if (ExperienceText)
	{
		if (bConditionMet)
		{
			// 条件满足: 显示黑色
			ExperienceText->SetColorAndOpacity(FLinearColor::Black);
			UE_LOG(LogTemp, Log, TEXT("ExperienceChestClaimWidget[索引%d]: ExperienceText设置为黑色"), TargetIndex);
		}
		else
		{
			// 条件不满足: 显示黄色
			ExperienceText->SetColorAndOpacity(FLinearColor::Yellow);
			UE_LOG(LogTemp, Log, TEXT("ExperienceChestClaimWidget[索引%d]: ExperienceText设置为黄色"), TargetIndex);
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("ExperienceChestClaimWidget: ExperienceText控件未绑定"));
	}
}
