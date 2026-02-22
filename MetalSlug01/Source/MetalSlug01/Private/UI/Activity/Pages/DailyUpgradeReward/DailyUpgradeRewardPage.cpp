/**
 * @file DailyUpgradeRewardPage.cpp
 * @brief 每日升级奖励活动页面实现
 * @author AI Assistant
 * @date 2026
 * @version 1.0
 *
 * @details 实现每日升级奖励活动页面的核心功能
 */

#include "UI/Activity/Pages/DailyUpgradeReward/DailyUpgradeRewardPage.h"
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
	CurrentExperience = 0;
	CurrentBonusMultiplier = 1.0f;

	return true;
}

void UDailyUpgradeRewardPage::NativeConstruct()
{
	Super::NativeConstruct();
}

void UDailyUpgradeRewardPage::NativeDestruct()
{
	Super::NativeDestruct();
}



