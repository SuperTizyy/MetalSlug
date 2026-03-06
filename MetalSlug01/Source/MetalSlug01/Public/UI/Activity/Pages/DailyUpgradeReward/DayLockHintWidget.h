// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/Activity/Core/UpgradeActivitySubsystem.h"
#include "DayLockHintWidget.generated.h"

/**
 * @brief 天数锁提示 Widget - 显示限时奖励图标和任务奖励图标
 * @details 用于展示当前天数锁定状态下的奖励预览信息
 */
UCLASS()
class METALSLUG01_API UDayLockHintWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 限时奖励图标容器 */
	UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
	class UHorizontalBox* LimitedTimeRewardIconsContainer;

	/** 任务奖励图标容器 */
	UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
	class UHorizontalBox* TaskRewardIconsContainer;

	/** 文字提示 */
	UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
	class UTextBlock* HintText;

	/**
	 * @brief 初始化锁定提示Widget
	 * @param DayIdentifier 天数标识符（如"day1", "day2"等）
	 * @details 根据天数标识符动态生成奖励图标
	 */
	UFUNCTION(BlueprintCallable, Category = "DayLockHint")
	void InitializeWidget(const FString& DayIdentifier);

private:
	/**
	 * @brief 设置任务奖励图标
	 * @param DayIdentifier 天数标识符
	 */
	void SetupTaskRewardIcons(const FString& DayIdentifier);

	/**
	 * @brief 设置限时奖励图标
	 * @param DayIdentifier 天数标识符
	 */
	void SetupLimitedTimeRewardIcons(const FString& DayIdentifier);

public:
	/** WBP_RewardIcon 蓝图类引用 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DayLockHint|UI")
	TSubclassOf<class UUserWidget> RewardIconClass;
};
