// 13

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "TreasureBoxWidget.generated.h"

class UButton;
class UTextBlock;
class UImage;
class UTreasureBoxItem;

/**
 * 宝箱 UI Widget
 * 职责：
 * 1. 显示宝箱状态（未解锁 / 可领取 / 已领取）
 * 2. 响应点击并通知 Model
 *
 * ⚠ 不做任何逻辑判断
 */
UCLASS()
class METALSLUG01_API UTreasureBoxWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	/**
	 * 绑定宝箱数据
	 * 由 Page 调用
	 */
	void BindBoxItem(UTreasureBoxItem* InBoxItem);

protected:
	virtual void NativeOnInitialized() override;

private:
	/** 当前绑定的宝箱数据 */
	UPROPERTY()
	UTreasureBoxItem* BoxItem = nullptr;

	// ================= UI 组件 =================

	/** 宝箱按钮（整体可点击区域） */
	UPROPERTY(meta = (BindWidget))
	UButton* BoxButton;

	/** 宝箱图标（根据状态切换外观） */
	UPROPERTY(meta = (BindWidget))
	UImage* BoxIcon;

	/** 状态文本（锁定 / 可领取 / 已领取） */
	UPROPERTY(meta = (BindWidget))
	UTextBlock* StateText;

private:
	/** 点击宝箱 */
	UFUNCTION()
	void OnBoxClicked();

	/** 根据 Model 刷新显示 */
	void RefreshView();
};

