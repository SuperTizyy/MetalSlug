/**
 * @file DailyUpgradeRewardPage.cpp
 * @brief 每日升级奖励活动页面实现
 * @author AI Assistant
 * @date 2026
 * @version 1.0
 *
 * @details 实现每日升级奖励活动页面的核心功能
 */

#include "UI/Activity/Pages/DailyUpgradeRewardPage.h"
#include "Components/HorizontalBox.h"
#include "Components/Image.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/ScrollBox.h"
#include "Kismet/GameplayStatics.h"

bool UDailyUpgradeRewardPage::Initialize()
{
	if (!Super::Initialize())
	{
		return false;
	}

	// 初始化默认值
	CurrentDayIndex = 1;
	CurrentExperience = 1500;
	CurrentBonusMultiplier = 1.0f;

	return true;
}

void UDailyUpgradeRewardPage::NativeConstruct()
{
	Super::NativeConstruct();

	// 初始化UI
	InitializeUI();

	// 绑定事件处理器
	BindEventHandlers();

	// 初始更新UI
	UpdateBonusIcons();
	UpdateRewardItem();
	UpdateDayButtons();
	UpdateBonusInfoText();
	UpdateTasks();
	UpdateExperienceDisplay();
	UpdateItemsScrollBox();
}

void UDailyUpgradeRewardPage::NativeDestruct()
{
	// 解绑事件处理器
	UnbindEventHandlers();

	Super::NativeDestruct();
}

// ==================== 初始化方法 ====================

void UDailyUpgradeRewardPage::InitializeUI()
{
	// 验证必要控件是否存在
	if (!BonusIconsContainer)
	{
		UE_LOG(LogTemp, Warning, TEXT("DailyUpgradeRewardPage: BonusIconsContainer未绑定"));
	}

	if (!RewardItemImage)
	{
		UE_LOG(LogTemp, Warning, TEXT("DailyUpgradeRewardPage: RewardItemImage未绑定"));
	}

	if (!ReselectRewardButton)
	{
		UE_LOG(LogTemp, Warning, TEXT("DailyUpgradeRewardPage: ReselectRewardButton未绑定"));
	}

	if (!DayButtonsContainer)
	{
		UE_LOG(LogTemp, Warning, TEXT("DailyUpgradeRewardPage: DayButtonsContainer未绑定"));
	}

	if (!BonusInfoText)
	{
		UE_LOG(LogTemp, Warning, TEXT("DailyUpgradeRewardPage: BonusInfoText未绑定"));
	}

	if (!TasksContainer)
	{
		UE_LOG(LogTemp, Warning, TEXT("DailyUpgradeRewardPage: TasksContainer未绑定"));
	}

	if (!CurrentExpText)
	{
		UE_LOG(LogTemp, Warning, TEXT("DailyUpgradeRewardPage: CurrentExpText未绑定"));
	}

	if (!ItemsScrollBox)
	{
		UE_LOG(LogTemp, Warning, TEXT("DailyUpgradeRewardPage: ItemsScrollBox未绑定"));
	}
}

void UDailyUpgradeRewardPage::BindEventHandlers()
{
	if (ReselectRewardButton)
	{
		ReselectRewardButton->OnClicked.AddDynamic(this, &UDailyUpgradeRewardPage::OnReselectRewardClicked);
	}
}

void UDailyUpgradeRewardPage::UnbindEventHandlers()
{
	if (ReselectRewardButton)
	{
		ReselectRewardButton->OnClicked.RemoveDynamic(this, &UDailyUpgradeRewardPage::OnReselectRewardClicked);
	}
}

// ==================== UI更新方法 ====================

void UDailyUpgradeRewardPage::UpdateBonusIcons()
{
	if (!BonusIconsContainer)
	{
		return;
	}

	// 清空现有内容
	BonusIconsContainer->ClearChildren();

	// 根据当前经验和天数添加加成图标
	// 这里可以根据具体需求添加不同的加成图标
	// 示例：经验值达到一定数值时显示对应图标
	
	if (CurrentExperience >= 1000)
	{
		// 添加基础加成图标
		// UImage* BonusIcon1 = CreateBonusIcon("基础加成");
		// BonusIconsContainer->AddChild(BonusIcon1);
	}

	if (CurrentExperience >= 2000)
	{
		// 添加进阶加成图标
		// UImage* BonusIcon2 = CreateBonusIcon("进阶加成");
		// BonusIconsContainer->AddChild(BonusIcon2);
	}

	UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: 更新加成图标完成"));
}

void UDailyUpgradeRewardPage::UpdateRewardItem()
{
	if (!RewardItemImage)
	{
		return;
	}

	// 根据当前选择的天数更新奖励物品图片
	TArray<int32> Rewards = GetDayRewards(CurrentDayIndex);
	if (Rewards.Num() > 0)
	{
		// 设置奖励物品图片（需要根据实际物品ID加载对应图片）
		// RewardItemImage->SetBrushFromTexture(LoadRewardItemTexture(Rewards[0]));
	}

	UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: 更新奖励物品显示 - 天数%d"), CurrentDayIndex);
}

void UDailyUpgradeRewardPage::UpdateDayButtons()
{
	if (!DayButtonsContainer)
	{
		return;
	}

	// 清空现有按钮
	DayButtonsContainer->ClearChildren();

	// 创建1-7天的按钮
	for (int32 DayIndex = 1; DayIndex <= 7; ++DayIndex)
	{
		UButton* DayButton = CreateDayButton(DayIndex);
		if (DayButton)
		{
			DayButtonsContainer->AddChild(DayButton);
		}
	}

	UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: 更新天数按钮完成"));
}

void UDailyUpgradeRewardPage::UpdateBonusInfoText()
{
	if (!BonusInfoText)
	{
		return;
	}

	// 计算当前加成倍数
	CurrentBonusMultiplier = CalculateBonusMultiplier(CurrentDayIndex);

	// 更新加成信息文本
	FString BonusText = FString::Printf(TEXT("当前加成: %.1fx (经验:%d)"), 
		CurrentBonusMultiplier, CurrentExperience);
	BonusInfoText->SetText(FText::FromString(BonusText));

	UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: 更新加成信息 - %.1fx"), CurrentBonusMultiplier);
}

void UDailyUpgradeRewardPage::UpdateTasks()
{
	if (!TasksContainer)
	{
		return;
	}

	// 清空现有任务
	TasksContainer->ClearChildren();

	// 创建示例任务（实际项目中应从数据表加载）
	for (int32 TaskIndex = 0; TaskIndex < 3; ++TaskIndex)
	{
		UWidget* TaskWidget = CreateTaskWidget(TaskIndex);
		if (TaskWidget)
		{
			TasksContainer->AddChild(TaskWidget);
		}
	}

	UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: 更新任务列表完成"));
}

void UDailyUpgradeRewardPage::UpdateExperienceDisplay()
{
	if (!CurrentExpText)
	{
		return;
	}

	// 更新经验值显示
	FString ExpText = FString::Printf(TEXT("当前经验值: %d"), CurrentExperience);
	CurrentExpText->SetText(FText::FromString(ExpText));

	UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: 更新经验值显示 - %d"), CurrentExperience);
}

void UDailyUpgradeRewardPage::UpdateItemsScrollBox()
{
	if (!ItemsScrollBox)
	{
		return;
	}

	// 清空现有物品
	ItemsScrollBox->ClearChildren();

	// 获取当前天数的奖励物品
	TArray<int32> Rewards = GetDayRewards(CurrentDayIndex);
	
	// 创建物品图标
	for (int32 ItemID : Rewards)
	{
		UImage* ItemIcon = CreateItemIcon(ItemID);
		if (ItemIcon)
		{
			ItemsScrollBox->AddChild(ItemIcon);
		}
	}

	UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: 更新物品图标滚动列表完成 - %d个物品"), Rewards.Num());
}

// ==================== 事件处理方法 ====================

void UDailyUpgradeRewardPage::OnReselectRewardClicked()
{
	UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: 点击重选奖励按钮"));

	// 实现重选奖励逻辑
	// 可以弹出选择界面或者重新随机奖励
	
	// 示例：增加经验值作为重选消耗
	if (CurrentExperience >= 100)
	{
		CurrentExperience -= 100;
		UpdateExperienceDisplay();
		UpdateBonusIcons();
		
		// 重新生成奖励
		UpdateRewardItem();
		UpdateItemsScrollBox();
		
		UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: 重选奖励成功"));
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("DailyUpgradeRewardPage: 经验值不足，无法重选奖励"));
	}
}

void UDailyUpgradeRewardPage::OnDayButtonClicked(int32 DayIndex)
{
	UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: 点击天数按钮 - 第%d天"), DayIndex);

	// 更新当前选择的天数
	CurrentDayIndex = DayIndex;

	// 更新相关UI
	UpdateRewardItem();
	UpdateBonusInfoText();
	UpdateItemsScrollBox();

	UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: 天数切换完成"));
}

void UDailyUpgradeRewardPage::OnTaskCompletionChanged(int32 TaskIndex, bool bCompleted)
{
	UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: 任务完成状态变化 - 任务%d, 完成:%s"), 
		TaskIndex, bCompleted ? TEXT("是") : TEXT("否"));

	// 根据任务完成情况更新经验值
	if (bCompleted)
	{
		int32 ExpReward = 50 * (TaskIndex + 1); // 示例：任务1奖励50经验，任务2奖励100经验...
		CurrentExperience += ExpReward;
		
		UpdateExperienceDisplay();
		UpdateBonusIcons();
		UpdateBonusInfoText();
		
		UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: 获得%d点经验值"), ExpReward);
	}
}

// ==================== 辅助方法 ====================

UButton* UDailyUpgradeRewardPage::CreateDayButton(int32 DayIndex)
{
	// 创建按钮
	UButton* DayButton = NewObject<UButton>(this, UButton::StaticClass());
	if (!DayButton)
	{
		return nullptr;
	}

	// 设置按钮文本
	FString ButtonText = FString::Printf(TEXT("Day %d"), DayIndex);
	// DayButton->SetText(FText::FromString(ButtonText)); // 需要获取按钮的文本块

	// 绑定点击事件
	DayButton->OnClicked.AddDynamic(this, [this, DayIndex]()
	{
		OnDayButtonClicked(DayIndex);
	});

	UE_LOG(LogTemp, Verbose, TEXT("DailyUpgradeRewardPage: 创建天数按钮 - Day %d"), DayIndex);
	return DayButton;
}

UWidget* UDailyUpgradeRewardPage::CreateTaskWidget(int32 TaskIndex)
{
	// 创建任务控件（这里简单返回一个文本块作为示例）
	UTextBlock* TaskText = NewObject<UTextBlock>(this, UTextBlock::StaticClass());
	if (!TaskText)
	{
		return nullptr;
	}

	// 设置任务描述
	FString TaskDescription = FString::Printf(TEXT("任务%d: 完成指定目标 +%d经验"), 
		TaskIndex + 1, 50 * (TaskIndex + 1));
	TaskText->SetText(FText::FromString(TaskDescription));

	UE_LOG(LogTemp, Verbose, TEXT("DailyUpgradeRewardPage: 创建任务控件 - 任务%d"), TaskIndex);
	return TaskText;
}

UImage* UDailyUpgradeRewardPage::CreateItemIcon(int32 ItemID)
{
	// 创建物品图标
	UImage* ItemIcon = NewObject<UImage>(this, UImage::StaticClass());
	if (!ItemIcon)
	{
		return nullptr;
	}

	// 设置物品图标（需要根据ItemID加载对应图片）
	// ItemIcon->SetBrushFromTexture(LoadItemIconTexture(ItemID));

	UE_LOG(LogTemp, Verbose, TEXT("DailyUpgradeRewardPage: 创建物品图标 - 物品ID %d"), ItemID);
	return ItemIcon;
}

float UDailyUpgradeRewardPage::CalculateBonusMultiplier(int32 DayIndex)
{
	// 计算加成倍数逻辑
	float BaseMultiplier = 1.0f;
	float DayBonus = DayIndex * 0.1f; // 每天增加0.1倍
	float ExpBonus = FMath::Min(CurrentExperience / 1000.0f, 2.0f); // 经验值加成，最多2倍
	
	return BaseMultiplier + DayBonus + ExpBonus;
}

TArray<int32> UDailyUpgradeRewardPage::GetDayRewards(int32 DayIndex)
{
	// 根据天数获取奖励数据（示例数据）
	TArray<int32> Rewards;
	
	switch (DayIndex)
	{
	case 1:
		Rewards = {1001, 1002}; // 示例物品ID
		break;
	case 2:
		Rewards = {1003, 1004, 1005};
		break;
	case 3:
		Rewards = {1006, 1007};
		break;
	default:
		Rewards = {1001}; // 默认奖励
		break;
	}

	return Rewards;
}