// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "TaskDetailWidget.generated.h"

/**
 * @brief 任务明细 Widget - 显示单个任务的详细信息
 * @author 
 * @date 
 * @note 包含任务需求说明、奖励展示、领取状态等基础控件
 */
UCLASS()
class METALSLUG01_API UTaskDetailWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 任务需求说明文本 */
	UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
	class UTextBlock* TaskRequirementText;

	/** 奖励展示容器 */
	UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
	class UHorizontalBox* RewardsContainer;

	/** 领取成功图标 */
	UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
	class UImage* ClaimSuccessImage;

	/** 领取按钮 */
	UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
	class UButton* ClaimButton;

	/** 领取提示文本 */
	UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
	class UTextBlock* ClaimHintText;
	
	/**
	 * @brief 设置领取按钮状态和绑定点击事件
	 * @param DayIdentifier 天数标识（如"day1", "day2"）
	 * @param TaskIndex 任务索引（在 TaskDescriptions 数组中的索引）
	 * @param CompleteCount 当前完成次数（TaskCompleteCounts[i]）
	 * @param RequiredCount 需要完成的次数（TaskRelatedValues[i]）
	 */
	void SetupClaimButton(const FString& DayIdentifier, int32 TaskIndex, int32 CompleteCount, int32 RequiredCount);
	
	/**
	 * @brief 设置奖励展示容器内容
	 * @param DayIdentifier 天数标识
	 * @param TaskIndex 任务索引
	 */
	void SetupRewardsContainer(const FString& DayIdentifier, int32 TaskIndex);
	
	/**
	 * @brief 处理领取按钮点击事件
	 * @param DayIdentifier 天数标识
	 * @param TaskIndex 任务索引
	 */
	UFUNCTION()
	void HandleClaimButtonClicked(const FString& DayIdentifier, int32 TaskIndex);
	
	/**
	 * @brief 处理领取按钮点击事件的无参包装器（用于委托绑定）
	 */
	UFUNCTION()
	void HandleClaimButtonClickWrapper();
	

	/**
	 * @brief 处理奖励存储到背包事件
	 * @param TaskIndex 任务索引
	 */
	UFUNCTION()
	void HandleRewardStore(int32 TaskIndex);

public:
	/** WBP_RewardIcon 蓝图类引用 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TaskDetail|UI")
	TSubclassOf<class UUserWidget> RewardIconClass;

	/** RewardOptionWidget 蓝图类引用 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TaskDetail|UI")
	TSubclassOf<class URewardOptionWidget> RewardOptionWidgetClass;

private:
	/** 当前绑定的天数标识（用于无参委托） */
	FString CurrentDayIdentifier;
	
	/** 当前绑定的任务索引（用于无参委托） */
	int32 CurrentTaskIndex;

	/** 当前任务索引（用于奖励事件处理） */
	int32 CurrentTaskIndexForReward;
};