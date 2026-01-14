// 13

#pragma once

#include "CoreMinimal.h"
#include "UI/Activity/Pages/ActivityPageBase.h"
#include "DailyLoginPage.generated.h"

class UListView;
class UHorizontalBox;
class UDailyLoginTrack;
class UTreasureTrack;
class UTreasureBoxWidget;

/**
 * 每日登录活动页面
 * 职责：
 * 1. 构建每日登录列表
 * 2. 构建宝箱区域
 * 3. 负责 Track → UI 的刷新
 *
 * 规则：
 * - Page 不保存业务状态
 * - Page 不判断规则
 * - Page 只做“联动与刷新”
 */
UCLASS()
class METALSLUG01_API UDailyLoginPage : public UActivityPageBase
{
	GENERATED_BODY()

protected:
	/** 页面显示时调用 */
	virtual void OnPageShow_Implementation() override;

private:
	// ================= UI =================

	/** 每日登录 ListView */
	UPROPERTY(meta = (BindWidget))
	UListView* LoginDayList;

	/** 宝箱容器（横排） */
	UPROPERTY(meta = (BindWidget))
	UHorizontalBox* TreasureBoxContainer;

	// ================= Track =================

	UPROPERTY()
	UDailyLoginTrack* LoginTrack = nullptr;

	UPROPERTY()
	UTreasureTrack* TreasureTrack = nullptr;

	// ================= Runtime =================

	/** 当前页面持有的宝箱 Widget */
	UPROPERTY()
	TArray<UTreasureBoxWidget*> TreasureBoxWidgets;

private:
	/** 构建每日登录列表 */
	void BuildLoginList();

	/** 构建宝箱区域 */
	void BuildTreasureBoxes();

	/** 刷新所有宝箱显示 */
	void RefreshTreasureBoxes();
};


