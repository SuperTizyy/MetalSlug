// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/Activity/Pages/DailyUpgradeReward/DayLockHintWidget.h"

#include "Components/HorizontalBox.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/Engine.h"

// UE 会自动生成构造函数，无需手动实现

void UDayLockHintWidget::InitializeWidget(const FString& DayIdentifier)
{
	UE_LOG(LogTemp, Log, TEXT("🔧 UDayLockHintWidget: 初始化锁定提示Widget - Day:%s"), *DayIdentifier);

	// 设置提示文字
	if (HintText)
	{
		FString HintString = FString::Printf(TEXT("%s 尚未解锁"), *DayIdentifier.ToUpper());
		HintText->SetText(FText::FromString(HintString));
		UE_LOG(LogTemp, Log, TEXT("🔧 UDayLockHintWidget: 设置提示文字: %s"), *HintString);
	}

	// 设置任务奖励图标
	SetupTaskRewardIcons(DayIdentifier);

	// 设置限时奖励图标
	SetupLimitedTimeRewardIcons(DayIdentifier);
}

void UDayLockHintWidget::SetupTaskRewardIcons(const FString& DayIdentifier)
{
	if (!TaskRewardIconsContainer)
	{
		UE_LOG(LogTemp, Warning, TEXT("UDayLockHintWidget: TaskRewardIconsContainer 未设置"));
		return;
	}

	// 检查是否为编辑器预览模式
	if (!GetWorld() || !GetWorld()->IsGameWorld())
	{
		return; // 静默返回，不输出日志
	}

	UE_LOG(LogTemp, Log, TEXT("🔧 UDayLockHintWidget: 设置任务奖励图标容器 - Day:%s"), *DayIdentifier);

	// 清空现有内容
	TaskRewardIconsContainer->ClearChildren();

	// 获取 GameInstance
	UGameInstance* GameInstance = GetGameInstance();
	if (!GameInstance)
	{
		UE_LOG(LogTemp, Error, TEXT("UDayLockHintWidget: 无法获取 GameInstance"));
		return;
	}

	// 获取 UpgradeActivitySubsystem
	UUpgradeActivitySubsystem* Subsystem = GameInstance->GetSubsystem<UUpgradeActivitySubsystem>();
	if (!Subsystem)
	{
		UE_LOG(LogTemp, Error, TEXT("UDayLockHintWidget: 无法获取 UpgradeActivitySubsystem"));
		return;
	}

	// 获取指定天数的奖励图标
	TArray<UTexture2D*> RewardIcons = Subsystem->GetRewardIconsForDay(DayIdentifier);

	UE_LOG(LogTemp, Log, TEXT("🔧 UDayLockHintWidget: 获取到 %d 个奖励图标"), RewardIcons.Num());

	// 检查 RewardIconClass 是否设置
	if (RewardIconClass == nullptr)
	{
		UE_LOG(LogTemp, Error, TEXT("UDayLockHintWidget: RewardIconClass 未设置！请在蓝图中指定 WBP_RewardIcon 类"));
		return;
	}

	// 动态生成 WBP_RewardIcon 蓝图组件
	for (UTexture2D* IconTexture : RewardIcons)
	{
		if (!IconTexture)
		{
			continue;
		}

		// 创建奖励图标 Widget
		UUserWidget* IconWidget = CreateWidget<UUserWidget>(GetWorld(), RewardIconClass);
		if (IconWidget)
		{
			// 获取图标控件
			UImage* RewardImage = Cast<UImage>(IconWidget->GetWidgetFromName(TEXT("RewardImage")));
			
			if (RewardImage)
			{
				RewardImage->SetBrushFromTexture(IconTexture);
				RewardImage->SetDesiredSizeOverride(FVector2D(64.0f, 64.0f)); // 设置默认尺寸
			}

			// 将图标添加到容器中
			TaskRewardIconsContainer->AddChild(IconWidget);
			UE_LOG(LogTemp, Log, TEXT("✅ UDayLockHintWidget: 成功添加奖励图标到TaskRewardIconsContainer"));
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("UDayLockHintWidget: 创建 RewardIcon Widget 失败"));
		}
	}
}

void UDayLockHintWidget::SetupLimitedTimeRewardIcons(const FString& DayIdentifier)
{
	if (!LimitedTimeRewardIconsContainer)
	{
		UE_LOG(LogTemp, Warning, TEXT("UDayLockHintWidget: LimitedTimeRewardIconsContainer 未设置"));
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("🔧 UDayLockHintWidget: 设置限时奖励图标容器 - Day:%s"), *DayIdentifier);

	// 清空现有内容
	LimitedTimeRewardIconsContainer->ClearChildren();

	// 获取 GameInstance
	UGameInstance* GameInstance = GetGameInstance();
	if (!GameInstance)
	{
		UE_LOG(LogTemp, Error, TEXT("UDayLockHintWidget: 无法获取 GameInstance"));
		return;
	}

	// 获取 UpgradeActivitySubsystem
	UUpgradeActivitySubsystem* Subsystem = GameInstance->GetSubsystem<UUpgradeActivitySubsystem>();
	if (!Subsystem)
	{
		UE_LOG(LogTemp, Error, TEXT("UDayLockHintWidget: 无法获取 UpgradeActivitySubsystem"));
		return;
	}

	// 获取指定天数的限时奖励图标
	TArray<UTexture2D*> LimitedTimeRewardIcons = Subsystem->GetLimitedTimeRewardIconsForDay(DayIdentifier);

	UE_LOG(LogTemp, Log, TEXT("🔧 UDayLockHintWidget: 获取到 %d 个限时奖励图标"), LimitedTimeRewardIcons.Num());

	// 检查 RewardIconClass 是否设置
	if (RewardIconClass == nullptr)
	{
		UE_LOG(LogTemp, Error, TEXT("UDayLockHintWidget: RewardIconClass 未设置！请在蓝图中指定 WBP_RewardIcon 类"));
		return;
	}

	// 动态生成 WBP_RewardIcon 蓝图组件
	for (UTexture2D* IconTexture : LimitedTimeRewardIcons)
	{
		if (!IconTexture)
		{
			continue;
		}

		// 创建奖励图标 Widget
		UUserWidget* IconWidget = CreateWidget<UUserWidget>(GetWorld(), RewardIconClass);
		if (IconWidget)
		{
			// 获取图标控件
			UImage* RewardImage = Cast<UImage>(IconWidget->GetWidgetFromName(TEXT("RewardImage")));
			
			if (RewardImage)
			{
				RewardImage->SetBrushFromTexture(IconTexture);
				RewardImage->SetDesiredSizeOverride(FVector2D(64.0f, 64.0f)); // 设置默认尺寸
			}

			// 将图标添加到容器中
			LimitedTimeRewardIconsContainer->AddChild(IconWidget);
			UE_LOG(LogTemp, Log, TEXT("✅ UDayLockHintWidget: 成功添加限时奖励图标到LimitedTimeRewardIconsContainer"));
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("UDayLockHintWidget: 创建 RewardIcon Widget 失败"));
		}
	}
}
