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

class UBorder;
class UHorizontalBox;
class UImage;
class UButton;
class UTextBlock;
class UVerticalBox;
class UScrollBox;
class UUpgradeActivitySubsystem;
class URewardOptionWidget;
class UExperienceChestClaimWidget;
class UDailyTaskWidget;
class UTaskDetailWidget;
class UDayLockHintWidget;

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
	
	/**
	 * @brief 刷新每日任务高亮状态 - 可在蓝图中调用
	 * @note 用于手动更新每日任务按钮的选中高亮显示
	 */
	UFUNCTION(BlueprintCallable, Category = "DailyUpgradeReward|UI")
	void RefreshDailyTaskHighlights();

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

	/** 加成信息背景边框 */
	UPROPERTY(meta = (BindWidget))
	UBorder* BonusInfoBorder;

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

	/** 固定奖励控件引用 */
	UPROPERTY(meta = (BindWidget))
	UExperienceChestClaimWidget* FixedPrizeWidget;
	
	/** RewardOptionWidget蓝图类引用 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DailyUpgrade|UI")
	TSubclassOf<class URewardOptionWidget> RewardOptionWidgetClass;

	/** FixedPrizeWidget蓝图类引用 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DailyUpgrade|UI")
	TSubclassOf<class UExperienceChestClaimWidget> FixedPrizeWidgetClass;

	/** ActivityConfirmPopupWidget蓝图类引用 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DailyUpgrade|UI")
	TSubclassOf<class UActivityConfirmPopupWidget> ActivityConfirmPopupWidgetClass;

	/** DailyTaskWidget蓝图类引用 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DailyUpgrade|UI")
	TSubclassOf<class UDailyTaskWidget> DailyTaskWidgetClass;
	
	/** TaskDetailWidget蓝图类引用 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DailyUpgrade|UI")
	TSubclassOf<class UTaskDetailWidget> TaskDetailWidgetClass;

	/** DayLockHintWidget蓝图类引用 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DailyUpgrade|UI")
	TSubclassOf<class UDayLockHintWidget> DayLockHintWidgetClass;

	// ==================== 数据管理 ====================
	
	/** 当前选择的天数索引 */
	int32 CurrentDayIndex;

	/** 当前选中的天数（用于临时高亮） */
	int32 CurrentSelectedDay;

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
	

	
	/**
	 * @brief 处理天数按钮点击事件
	 * @param DayIdentifier 天数标识
	 * @param DayIndex 天数索引
	 */
	void OnDayButtonClicked(const FString& DayIdentifier, int32 DayIndex);

	/**
	 * @brief 处理天数按钮点击事件（无参包装器）
	 * @note 用于绑定到 FOnButtonClickedEvent 委托
	 */
	UFUNCTION()
	void HandleDayButtonClicked();

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

	/** 更新每日任务列表 */
	void UpdateDailyTasks();

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
	 * @brief 更新限时加成信息文本
	 * @param DayIdentifier 天数标识符
	 */
	void UpdateBonusInfoText(const FString& DayIdentifier);

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
	 * @brief 处理宝箱领取请求
	 * @param ChestIndex 宝箱索引
	 */
	UFUNCTION()
	void HandleChestClaimRequest(int32 ChestIndex);
	
	/**
	 * @brief 处理奖励存储操作
	 * @param DayIndex 天数索引
	 */
	UFUNCTION()
	void HandleRewardStore(int32 DayIndex);
	
	/**
	 * @brief 更新所有宝箱Widget的状态
	 * @details 手动刷新ItemsScrollBox中所有ExperienceChestClaimWidget的视觉状态
	 * 确保领取操作后所有Widget都能正确显示最新状态
	 */
	void UpdateAllChestWidgetStates();
	
	/**
	 * @brief 更新经验宝箱控件状态
	 * @details 更新现有ExperienceChestClaimWidget的状态而不重新创建
	 */
	void UpdateExperienceChestWidgetsState();
	
	/**
	 * @brief 手动刷新所有ExperienceChestClaimWidget的进度条
	 * @details 在经验值发生变化后调用此方法更新所有进度条显示
	 */
	UFUNCTION(BlueprintCallable, Category = "Daily Upgrade Reward")
	void RefreshAllProgressBars();

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
	
	/**
	 * @brief 显示奖励选项Widget
	 * @param ChestIndex 宝箱索引
	 */
	void ShowRewardOptionWidget(int32 ChestIndex);
	
	/**
	 * @brief 初始化固定奖励控件
	 * @details 根据TaskRelatedValues最后一个索引值和ChestClaimStatus状态控制HighlightFrameImage显示
	 */
	void InitializeFixedPrizeWidget();
	
	/**
	 * @brief 更新固定奖励控件状态
	 * @details 根据当前经验和领取状态更新FixedPrizeWidget的显示状态
	 */
	void UpdateFixedPrizeWidget();
	
	/**
	 * @brief 根据当前经验值居中显示ScrollBox内容
	 * @details 读取UpgradeActivitySubsystem内存数据的CurrentExperience值，
	 *          根据TaskRelatedValues数组计算应该居中的宝箱索引，并设置ScrollBox滚动位置
	 */
	void CenterScrollBoxOnCurrentExperience();
	
	/**
	 * @brief 根据当前经验值找到目标宝箱索引
	 * @param CurrentExp 当前经验值
	 * @param TaskRelatedValues 任务相关经验值数组
	 * @return 应该居中的宝箱索引
	 */
	int32 FindTargetChestIndexForExperience(int32 CurrentExp, const TArray<int32>& TaskRelatedValues);
	
	/**
	 * @brief 计算使目标控件居中显示的滚动偏移量
	 * @param TargetIndex 目标控件索引
	 * @return 滚动偏移量
	 */
	float CalculateCenterScrollOffset(int32 TargetIndex);
	
	/**
	 * @brief 计算ScrollBox的最大滚动偏移量
	 * @return 最大滚动偏移量
	 */
	float CalculateMaxScrollOffset();
};