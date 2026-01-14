// 14

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Blueprint/IUserObjectListEntry.h"
#include "DailyLoginDayItemWidget.generated.h"

class UTextBlock;
class UButton;
class UImage;
class UDailyLoginDayItem;
class UDailyLoginTrack;

/**
 * 每日登录 Item Widget
 * 职责：
 * - 展示 DayIndex / 可领取 / 已领取
 * - 点击按钮 → 通知 Track
 *
 * ⚠ 不保存状态
 * ⚠ 不写规则
 */
UCLASS()
class METALSLUG01_API UDailyLoginDayItemWidget
	: public UUserWidget
	, public IUserObjectListEntry
{
	GENERATED_BODY()

protected:
	/** ListView 设置 Item 时调用（核心入口） */
	virtual void NativeOnListItemObjectSet(UObject* ListItemObject) override;

private:
	/** 当前绑定的数据 */
	UPROPERTY()
	UDailyLoginDayItem* ItemData = nullptr;

	/** Track 引用（只调用接口） */
	UPROPERTY()
	UDailyLoginTrack* LoginTrack = nullptr;

	// ================= UI =================

	UPROPERTY(meta = (BindWidget))
	UTextBlock* DayText;

	UPROPERTY(meta = (BindWidget))
	UButton* ClaimButton;

	UPROPERTY(meta = (BindWidget))
	UImage* ClaimedIcon;

private:
	/** 点击领取 */
	UFUNCTION()
	void OnClaimClicked();

	/** 刷新 UI */
	void RefreshView();
};
