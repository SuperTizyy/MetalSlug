// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "DailyTaskWidget.generated.h"

/**
 * @brief 每天任务Widget - 显示单个任务项
 * @author 
 * @date 
 * @note 包含选中高亮框、天数按钮、天数显示、锁定图标等基础控件
 */
UCLASS()
class METALSLUG01_API UDailyTaskWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/** 选中高亮框 */
	UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
	class UImage* SelectionHighlightImage;

	/** 天数按钮 */
	UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
	class UButton* DayButton;

	/** 天数显示文本 */
	UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
	class UTextBlock* DayText;

	/** 锁定图标 */
	UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
	class UImage* LockIconImage;
};