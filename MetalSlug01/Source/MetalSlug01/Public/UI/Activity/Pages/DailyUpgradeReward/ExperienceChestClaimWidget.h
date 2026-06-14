// 版权声明：在项目设置的描述页面填写您的版权信息。

/**
 * @file ExperienceChestClaimWidget.h
 * @brief 经验宝箱领取 Widget
 * @author AI Assistant
 * @date 2026
 * @version 1.0
 *
 * @details 实现经验宝箱领取功能的 UI Widget
 */

#pragma once

// ==========================================
// 头文件包含说明
// ==========================================
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ExperienceChestClaimWidget.generated.h"

class UButton;
class UTextBlock;
class UImage;
class UProgressBar;


/**
 * @class UExperienceChestClaimWidget
 * @brief 经验宝箱领取 Widget
 *
 * 职责说明:
 * - 显示经验宝箱 + 数量 + 进度条 + 钻石图标
 * - 玩家点击后请求父页面领取
 * - 区分 FixedPrizeWidget (固定奖励) 和普通 ExperienceChestWidget
 * - 状态机: Enabled(可领) / Disabled(经验不足) / Claimed(已领)
 *
 * 架构理念:
 * 1. 单一职责: 一个 Widget 管一个宝箱
 * 2. 数据驱动: 通过 ChestIndex 路由数据
 * 3. 委托通信: OnChestClaimRequested(ChestIndex) 通知父级
 * 4. 复用: FixedPrizeWidget 也用此类, 通过 ChestIndex 区分
 * 5. 防御链: 多次空指针检查 + 多种降级状态
 *
 * 关联:
 * - 上级: UDailyUpgradeRewardPage（创建/管理）
 * - 数据: UUpgradeActivitySubsystem
 */
UCLASS()
class METALSLUG01_API UExperienceChestClaimWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// ==========================================
	// 1. 生命周期
	// ==========================================

	virtual bool Initialize() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	// ==========================================
	// 2. UI 控件引用
	// ==========================================

	/** 宝箱领取按钮 */
	UPROPERTY(meta = (BindWidget))
	UButton* ChestClaimButton;

	/** 宝箱数量文本（如 "X5"） */
	UPROPERTY(meta = (BindWidget))
	UTextBlock* ChestCountText;

	/** 高亮框图片（可领取时显示） */
	UPROPERTY(meta = (BindWidget))
	UImage* HighlightFrameImage;

	/** 领取成功文本 */
	UPROPERTY(meta = (BindWidget))
	UTextBlock* SuccessText;

	/** 经验值进度条 */
	UPROPERTY(meta = (BindWidget))
	UProgressBar* ExperienceProgressBar;

	/** 菱形图标（黄/黑表示可领/未达） */
	UPROPERTY(meta = (BindWidget))
	UImage* DiamondIconImage;

	/** 经验值文本（如 "0/100"） */
	UPROPERTY(meta = (BindWidget))
	UTextBlock* ExperienceText;

	// ==========================================
	// 3. 数据管理
	// ==========================================

	/** 当前宝箱数量 */
	int32 CurrentChestCount;

	/** 当前经验值 */
	int32 CurrentExperience;

	/** 最大经验值 */
	int32 MaxExperience;

	/** 宝箱索引（用于路由 Subsystem 数据） */
	int32 ChestIndex;

	// ==========================================
	// 4. 事件处理
	// ==========================================

	/**
	 * 宝箱领取按钮点击事件
	 * 1. 校验: Subsystem + Config + Record
	 * 2. 区分 FixedPrizeWidget / 普通 Widget（用 LastIndex 判定）
	 * 3. 校验 ChestClaimStatus[TargetIndex] != 1
	 * 4. 校验 CurrentExperience >= TaskRelatedValues[TargetIndex]
	 * 5. 广播 OnChestClaimRequested(TargetIndex)
	 */
	UFUNCTION()
	void OnChestClaimButtonClicked();

	// ==========================================
	// 5. UI 更新方法
	// ==========================================

	/** 更新宝箱数量显示 */
	void UpdateChestCount();

	/**
	 * 更新 SuccessText 显示状态
	 * 规则: ChestClaimStatus[TargetIndex] == 1 -> Visible / 否则 Hidden
	 */
	void UpdateSuccessTextVisibility();

	/** 更新经验值显示 */
	void UpdateExperienceDisplay();

	/**
	 * 更新进度条
	 * 区间规则: ChestIndex=0 区间 [5,45] / 其他区间 [46+(i-1)*30, 75+(i-1)*30]
	 */
	void UpdateProgressBar();

	/** 显示领取成功效果（2 秒后自动隐藏） */
	void ShowSuccessEffect();

	/** 隐藏领取成功效果 */
	void HideSuccessEffect();

	/**
	 * 更新视觉状态
	 * @param bIsClaimed 是否已领取
	 */
	void UpdateVisualStatus(bool bIsClaimed);

	/**
	 * 设置宝箱图标（通过 Button Style）
	 * @param BoxIcon 宝箱图标纹理
	 */
	UFUNCTION(BlueprintCallable, Category = "Experience Chest")
	void SetChestBoxIcon(UTexture2D* BoxIcon);

	/**
	 * 设置宝箱索引
	 * @param Index 宝箱索引
	 */
	UFUNCTION(BlueprintCallable, Category = "Experience Chest")
	void SetChestIndex(int32 Index);

	/**
	 * @delegate FOnChestClaimRequested
	 * @brief 宝箱领取请求事件
	 * @param ChestIndex 宝箱索引
	 */
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnChestClaimRequested, int32, ChestIndex);

	/** 宝箱领取请求事件（父级订阅） */
	UPROPERTY(BlueprintAssignable, Category = "Events")
	FOnChestClaimRequested OnChestClaimRequested;

public:
	// ==========================================
	// 6. 状态设置
	// ==========================================

	/** 设置按钮启用状态（高亮框 Visible） */
	void SetButtonEnabledState();

	/** 设置按钮禁用状态（高亮框 Hidden） */
	void SetButtonDisabledState();

	/** 设置按钮已领取状态（高亮框 Hidden） */
	void SetButtonClaimedState();

	/**
	 * 根据当前数据更新按钮状态
	 * 1. 区分 FixedPrize / 普通 Widget
	 * 2. ChestClaimStatus 判定已领
	 * 3. CurrentExperience 判定可领
	 * 4. 同步 SuccessText / DiamondIcon / ExperienceText 颜色
	 */
	void UpdateButtonState();

	/** 手动刷新进度条显示 */
	UFUNCTION(BlueprintCallable, Category = "Experience Chest")
	void RefreshProgressBar();

	/** 更新菱形图标颜色（条件满足=黄, 否则=黑） */
	void UpdateDiamondIconColor();

	/** 更新经验值文本颜色（条件满足=黑, 否则=黄） */
	void UpdateExperienceTextColor();

private:
};
