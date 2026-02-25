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
class UUpgradeActivitySubsystem;

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
	
	// ==================== 蓝图可调用函数 ====================
	
	/**
	 * @brief 手动刷新UI - 用于调试测试
	 * @note 可以在蓝图中调用此函数来测试UI刷新功能
	 */
	UFUNCTION(BlueprintCallable, Category = "DailyUpgradeReward|Debug")
	void ManualRefreshUI();

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

	/** ExperienceChestClaimWidget蓝图类引用 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DailyUpgrade|UI")
	TSubclassOf<class UExperienceChestClaimWidget> ExperienceChestWidgetClass;

	/** ActivityConfirmPopupWidget蓝图类引用 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DailyUpgrade|UI")
	TSubclassOf<class UActivityConfirmPopupWidget> ActivityConfirmPopupWidgetClass;

	// ==================== 数据管理 ====================
	
	/** 当前选择的天数索引 */
	int32 CurrentDayIndex;

	/** 当前经验值 */
	int32 CurrentExperience;

	/** 加成倍数 */
	float CurrentBonusMultiplier;

	/** 按钮到天数索引的映射 */
	TMap<UButton*, int32> ButtonToDayIndexMap;

	/** 缓存的物品图标数据（全局变量） */
	TArray<TSoftObjectPtr<UTexture2D>> CachedItemIcons;

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
	 * @brief 获取页面唯一身份标识
	 * @return 页面身份字符串（包含地址和创建时间等信息）
	 */
	FString GetPageIdentity() const;
	
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

private:
	// ==================== 图标管理方法 ====================
	
	/**
	 * @brief 初始化奖励物品图标数据
	 * @details 从配置表读取数据并缓存ItemIcon到全局变量
	 */
	void InitializeRewardItemIcons();

	/**
	 * @brief 更新奖励物品图片显示
	 * @details 使用缓存的图标数据显示RewardItemImage控件
	 */
	void UpdateRewardItemImage();

	/**
	 * @brief 切换到下一个奖励图标
	 * @details 循环切换CachedItemIcons数组中的图标索引
	 */
	UFUNCTION(BlueprintCallable, Category = "DailyUpgradeReward")
	void SwitchToNextRewardIcon();

	/**
	 * @brief 切换到上一个奖励图标
	 * @details 循环切换CachedItemIcons数组中的图标索引
	 */
	UFUNCTION(BlueprintCallable, Category = "DailyUpgradeReward")
	void SwitchToPreviousRewardIcon();

	/**
	 * @brief 更新宝箱数量文本显示
	 * @details 从DailyUpgradeRewardConfigRow表获取ActivityID==110数据的RewardItemCounts最后一个索引值
	 */
	void UpdateChestCountText();

	/**
	 * @brief 初始化ItemsScrollBox中的ExperienceChestClaimWidget
	 * @details 根据RewardItemIDs创建多个ExperienceChestClaimWidget并设置对应的BoxIcon
	 */
	void InitializeExperienceChestWidgets();

	/**
	 * @brief 刷新UI显示
	 * @details 重新初始化所有UI组件以反映最新状态
	 */
	UFUNCTION()
	void RefreshUI();

	/**
	 * @brief 处理重选奖励按钮点击事件
	 */
	UFUNCTION()
	void OnReselectRewardClicked();
	
	/**
	 * @brief 订阅Subsystem事件
	 */
	void SubscribeToSubsystemEvents();
	
	/**
	 * @brief 取消订阅Subsystem事件
	 */
	void UnsubscribeFromSubsystemEvents();
	
	/**
	 * @brief 处理奖励图标索引更新事件
	 * @param NewIndex 新的图标索引
	 */
	UFUNCTION()
	void OnRewardIconIndexChanged(int32 NewIndex);
};