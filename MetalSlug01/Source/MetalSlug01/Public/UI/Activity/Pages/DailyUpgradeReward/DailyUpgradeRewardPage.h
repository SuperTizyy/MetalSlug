/**
 * @file DailyUpgradeRewardPage.h
 * @brief 每日升级奖励活动页面
 * @author AI Assistant
 * @date 2026
 * @version 1.0
 *
 * @details 实现每日升级奖励活动的核心功能页面
 */

#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "DailyUpgradeRewardPage.generated.h"

class UHorizontalBox;
class UImage;
class UButton;
class UTextBlock;
class UVerticalBox;
class UScrollBox;

/**
 * @brief 每日升级奖励活动页面
 * @details 提供每日升级奖励活动的UI展示和交互功能
 */
UCLASS()
class METALSLUG01_API UDailyUpgradeRewardPage : public UUserWidget
{
	GENERATED_BODY()

public:
	// ==================== 生命周期 ====================
	
	virtual bool Initialize() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

protected:
	// ==================== UI控件引用 ====================
	
	/** 加成图标容器 */
	UPROPERTY(meta = (BindWidget))
	UHorizontalBox* BonusIconsContainer;

	/** 奖励物品图片 */
	UPROPERTY(meta = (BindWidget))
	UImage* RewardItemImage;

	/** 重选奖励按钮 */
	UPROPERTY(meta = (BindWidget))
	UButton* ReselectRewardButton;

	/** 天数选择按钮容器 */
	UPROPERTY(meta = (BindWidget))
	UHorizontalBox* DayButtonsContainer;

	/** 加成信息时限文本 */
	UPROPERTY(meta = (BindWidget))
	UTextBlock* BonusInfoText;

	/** 任务列表容器 */
	UPROPERTY(meta = (BindWidget))
	UVerticalBox* TasksContainer;

	/** 当前经验值文本 */
	UPROPERTY(meta = (BindWidget))
	UTextBlock* CurrentExpText;

	/** 物品图标滚动容器 */
	UPROPERTY(meta = (BindWidget))
	UScrollBox* ItemsScrollBox;

	/** 宝箱数量文本 */
	UPROPERTY(meta = (BindWidget))
	UTextBlock* ChestCountText;

	// ==================== 数据管理 ====================
	
	/** 当前选择的天数索引 */
	int32 CurrentDayIndex;

	/** 当前经验值 */
	int32 CurrentExperience;

	/** 加成倍数 */
	float CurrentBonusMultiplier;

	/** 按钮到天数索引的映射 */
	TMap<UButton*, int32> ButtonToDayIndexMap;

	/**
	 * @brief 静态回调函数用于按钮点击
	 * @param Button 被点击的按钮
	 * @param DayIndex 对应的天数索引
	 */
	static void StaticButtonCallback(UButton* Button, int32 DayIndex);

	// ==================== 初始化方法 ====================
	
	/**
	 * @brief 初始化UI控件
	 */
	void InitializeUI();

	/**
	 * @brief 绑定事件处理器
	 */
	void BindEventHandlers();

	/**
	 * @brief 解绑事件处理器
	 */
	void UnbindEventHandlers();

	// ==================== UI更新方法 ====================
	
	/**
	 * @brief 更新加成图标显示
	 */
	void UpdateBonusIcons();

	/**
	 * @brief 更新奖励物品显示
	 */
	void UpdateRewardItem();

	/**
	 * @brief 更新天数按钮
	 */
	void UpdateDayButtons();

	/**
	 * @brief 更新加成信息文本
	 */
	void UpdateBonusInfoText();

	/**
	 * @brief 更新任务列表
	 */
	void UpdateTasks();

	/**
	 * @brief 更新经验值显示
	 */
	void UpdateExperienceDisplay();

	/**
	 * @brief 更新物品图标滚动列表
	 */
	void UpdateItemsScrollBox();

	// ==================== 事件处理方法 ====================
	
	/**
	 * @brief 处理任务完成状态变化
	 * @param TaskIndex 任务索引
	 * @param bCompleted 是否完成
	 */
	void OnTaskCompletionChanged(int32 TaskIndex, bool bCompleted);

	// ==================== 辅助方法 ====================
	
	/**
	 * @brief 计算当前加成倍数
	 * @param DayIndex 天数索引
	 * @return 加成倍数
	 */
	float CalculateBonusMultiplier(int32 DayIndex);

	/**
	 * @brief 获取天数对应的奖励数据
	 * @param DayIndex 天数索引
	 * @return 奖励数据
	 */
	TArray<int32> GetDayRewards(int32 DayIndex);
};