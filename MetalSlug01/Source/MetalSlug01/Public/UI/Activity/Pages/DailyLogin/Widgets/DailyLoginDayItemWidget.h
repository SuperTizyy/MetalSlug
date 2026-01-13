// 14

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "DailyLoginDayItemWidget.generated.h"

class UTextBlock;
class UButton;
class UDailyLoginDayItem;

/**
 * 单日登录奖励 Item Widget
 * 职责：
 * 1. 展示 DayIndex / 奖励 / 状态
 * 2. 将点击行为上抛（不处理逻辑）
 */
UCLASS()
class METALSLUG01_API UDailyLoginDayItemWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// ListView 绑定入口
	void BindItem(UDailyLoginDayItem* InItem);

protected:
	virtual void NativeOnInitialized() override;

private:
	// 绑定的数据
	UPROPERTY()
	UDailyLoginDayItem* Item = nullptr;

	// ==== UI ====

	UPROPERTY(meta = (BindWidget))
	UTextBlock* DayText;

	UPROPERTY(meta = (BindWidget))
	UButton* ClaimButton;

private:
	// 点击领取
	UFUNCTION()
	void OnClaimClicked();

	// 根据数据刷新 UI
	void RefreshView();
};

