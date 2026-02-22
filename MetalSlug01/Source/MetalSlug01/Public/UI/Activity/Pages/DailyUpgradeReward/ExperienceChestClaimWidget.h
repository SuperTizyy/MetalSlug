/**
 * @file ExperienceChestClaimWidget.h
 * @brief 经验宝箱领取Widget
 * @author AI Assistant
 * @date 2026
 * @version 1.0
 *
 * @details 实现经验宝箱领取功能的UI Widget
 */

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ExperienceChestClaimWidget.generated.h"

class UButton;
class UTextBlock;
class UImage;
class UProgressBar;

/**
 * @brief 经验宝箱领取Widget
 * @details 提供经验宝箱领取的核心UI Widget功能
 */
UCLASS()
class METALSLUG01_API UExperienceChestClaimWidget : public UUserWidget
{
 GENERATED_BODY()

public:
	// ==================== 生命周期 ====================
	
	virtual bool Initialize() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

	// ==================== UI控件引用 ====================
	
	/** 宝箱领取按钮 */
	UPROPERTY(meta = (BindWidget))
	UButton* ChestClaimButton;

	/** 宝箱数量文本 */
	UPROPERTY(meta = (BindWidget))
	UTextBlock* ChestCountText;

	/** 高亮框图片 */
	UPROPERTY(meta = (BindWidget))
	UImage* HighlightFrameImage;

	/** 领取成功文本 */
	UPROPERTY(meta = (BindWidget))
	UTextBlock* SuccessText;

	/** 经验值进度条 */
	UPROPERTY(meta = (BindWidget))
	UProgressBar* ExperienceProgressBar;

	/** 菱形图标 */
	UPROPERTY(meta = (BindWidget))
	UImage* DiamondIconImage;

	/** 经验值文本 */
	UPROPERTY(meta = (BindWidget))
	UTextBlock* ExperienceText;

	// ==================== 数据管理 ====================
	
	/** 当前宝箱数量 */
	int32 CurrentChestCount;

	/** 当前经验值 */
	int32 CurrentExperience;

	/** 最大经验值 */
	int32 MaxExperience;

	// ==================== 事件处理 ====================
	
	/**
	 * @brief 宝箱领取按钮点击事件
	 */
	UFUNCTION()
	void OnChestClaimButtonClicked();

	// ==================== UI更新方法 ====================
	
	/**
	 * @brief 更新宝箱数量显示
	 */
	void UpdateChestCount();

	/**
	 * @brief 更新经验值显示
	 */
	void UpdateExperienceDisplay();

	/**
	 * @brief 更新进度条
	 */
	void UpdateProgressBar();

	/**
	 * @brief 显示领取成功效果
	 */
	void ShowSuccessEffect();

	/**
	 * @brief 隐藏领取成功效果
	 */
	void HideSuccessEffect();
};