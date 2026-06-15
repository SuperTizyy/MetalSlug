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

// ==========================================
// 头文件包含说明
// ==========================================
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "DailyUpgradeRewardPage.generated.h"

// 前向声明: 仅引用指针, 避免循环包含
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
 * @class UDailyUpgradeRewardPage
 * @brief 每日升级奖励活动页面
 *
 * 职责说明:
 * - 7 天活动进度的中央控制页
 * - 集成 DayButtons (每日按钮) + Tasks (任务列表) + Chest (经验宝箱) + FixedPrize
 * - 与 UUpgradeActivitySubsystem 深度联动, 实时反映数据变化
 *
 * 架构理念:
 * 1. 单一职责: 一个页面管一种活动
 * 2. 事件驱动: 订阅 Subsystem 事件自动更新
 * 3. 数据驱动: 配置表 (DT_DailyUpgradeRewardConfig) + 存档 (FUpgradeRewardSaveRecord)
 * 4. 模块化: TaskDetail / ExperienceChest / DayLockHint / DailyTask 拆分
 * 5. 防御链: 多次空指针 + 多次 IsValidIndex
 *
 * 关联:
 * - 上级: UActivityNavMenuWidget
 * - 数据: UUpgradeActivitySubsystem
 * - 子件: UTaskDetailWidget / UExperienceChestClaimWidget / UDailyTaskWidget / UDayLockHintWidget
 */
UCLASS()
class METALSLUG01_API UDailyUpgradeRewardPage : public UUserWidget
{
	GENERATED_BODY()

public:
	// ==========================================
	// 0. ViewModel (改造: 数据访问从 Page 抽离)
	// ==========================================

	/**
	 * 访问 Page 持有的 ViewModel (蓝图可用)
	 * @return ViewModel 实例; 未 Bind 时返回 nullptr
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "DailyUpgrade")
	class UDailyUpgradeRewardViewModel* GetViewModel() const { return ViewModel; }

public:
	// ==========================================
	// 1. 生命周期
	// ==========================================

	/**
	 * 初始化
	 * 1. InitializeUI: 初始化所有 UI 控件
	 * 2. BindEventHandlers: 绑定事件
	 * 3. SubscribeToSubsystemEvents
	 * 4. UpdateExperienceDisplay / UpdateBonusInfoText / UpdateRewardItem
	 */
	virtual bool Initialize() override;

	/**
	 * 构造
	 * 1. InitializeUI
	 * 2. BindEventHandlers
	 * 3. SubscribeToSubsystemEvents
	 * 4. CenterScrollBoxOnCurrentExperience 居中滚动条
	 */
	virtual void NativeConstruct() override;

	/**
	 * 析构
	 * 1. UnbindEventHandlers
	 * 2. UnsubscribeFromSubsystemEvents
	 */
	virtual void NativeDestruct() override;

	// ==========================================
	// 2. 蓝图可调用函数
	// ==========================================

	/**
	 * 手动刷新 UI（用于调试测试）
	 * 在蓝图中调用此函数来测试 UI 刷新功能
	 */
	UFUNCTION(BlueprintCallable, Category = "DailyUpgradeReward|Debug")
	void ManualRefreshUI();

	/**
	 * 刷新每日任务高亮状态（可在蓝图中调用）
	 * 用于手动更新每日任务按钮的选中高亮显示
	 */
	UFUNCTION(BlueprintCallable, Category = "DailyUpgradeReward|UI")
	void RefreshDailyTaskHighlights();

protected:
	// ==========================================
	// 3.5 ViewModel 实例 (改造: 数据访问从 Page 抽离)
	// ==========================================

	/** ViewModel, 持有后由 Page 通过 GetViewModel() 访问 */
	UPROPERTY(BlueprintReadOnly, Category = "DailyUpgradeReward")
	TObjectPtr<UDailyUpgradeRewardViewModel> ViewModel = nullptr;

protected:
	// ==========================================
	// 3. UI 控件引用
	// ==========================================

	/** 加成图标容器（HBox, 用于显示活动加成） */
	UPROPERTY(meta = (BindWidget))
	UHorizontalBox* BonusIconsContainer;

	/** 奖励物品图片（可切换的奖励预览图） */
	UPROPERTY(meta = (BindWidget))
	UImage* RewardItemImage;

	/** 重选奖励按钮（触发 SwitchToNextRewardIcon / SwitchToPreviousRewardIcon） */
	UPROPERTY(meta = (BindWidget))
	UButton* ReselectRewardButton;

	/** 天数选择按钮容器（HBox, 第 1-7 天） */
	UPROPERTY(meta = (BindWidget))
	UHorizontalBox* DayButtonsContainer;

	/** 加成信息时限文本 */
	UPROPERTY(meta = (BindWidget))
	UTextBlock* BonusInfoText;

	/** 加成信息背景边框 */
	UPROPERTY(meta = (BindWidget))
	UBorder* BonusInfoBorder;

	/** 任务列表容器（VBox, 包含所有 UTaskDetailWidget） */
	UPROPERTY(meta = (BindWidget))
	UVerticalBox* TasksContainer;

	/** 当前经验值文本 */
	UPROPERTY(meta = (BindWidget))
	UTextBlock* CurrentExpText;

	/** 物品图标滚动容器（滚动显示所有 ExperienceChestClaimWidget） */
	UPROPERTY(meta = (BindWidget))
	UScrollBox* ItemsScrollBox;

	/** 宝箱数量文本 */
	UPROPERTY(meta = (BindWidget))
	UTextBlock* ChestCountText;

	/** 固定奖励控件引用（通常显示最后一个 FixedPrize） */
	UPROPERTY(meta = (BindWidget))
	UExperienceChestClaimWidget* FixedPrizeWidget;

	// ==========================================
	// 4. Widget 蓝图类引用
	// ==========================================

	/** ExperienceChestClaimWidget 蓝图类引用 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DailyUpgrade|UI")
	TSubclassOf<class UExperienceChestClaimWidget> ExperienceChestWidgetClass;

	/** RewardOptionWidget 蓝图类引用 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DailyUpgrade|UI")
	TSubclassOf<class URewardOptionWidget> RewardOptionWidgetClass;

	/** FixedPrizeWidget 蓝图类引用 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DailyUpgrade|UI")
	TSubclassOf<class UExperienceChestClaimWidget> FixedPrizeWidgetClass;

	/** ActivityConfirmPopupWidget 蓝图类引用 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DailyUpgrade|UI")
	TSubclassOf<class UActivityConfirmPopupWidget> ActivityConfirmPopupWidgetClass;

	/** DailyTaskWidget 蓝图类引用 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DailyUpgrade|UI")
	TSubclassOf<class UDailyTaskWidget> DailyTaskWidgetClass;

	/** TaskDetailWidget 蓝图类引用 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DailyUpgrade|UI")
	TSubclassOf<class UTaskDetailWidget> TaskDetailWidgetClass;

	/** DayLockHintWidget 蓝图类引用 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DailyUpgrade|UI")
	TSubclassOf<class UDayLockHintWidget> DayLockHintWidgetClass;

	/** WBP_RewardIcon 蓝图类引用 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DailyUpgrade|UI")
	TSubclassOf<class UUserWidget> RewardIconWidgetClass;

	// ==========================================
	// 5. 数据管理
	// ==========================================

	/** 当前选择的天数索引 */
	int32 CurrentDayIndex;

	/** 当前选中的天数（用于临时高亮） */
	int32 CurrentSelectedDay;

	/** 当前经验值 */
	int32 CurrentExperience;

	/** 加成倍数 */
	float CurrentBonusMultiplier;

	/** 按钮到天数索引的映射（用于 OnClicked 路由） */
	TMap<UButton*, int32> ButtonToDayIndexMap;

	/** 缓存的物品图标数据（全局变量, 用于循环切换） */
	TArray<TSoftObjectPtr<UTexture2D>> CachedItemIcons;

	// ==========================================
	// 6. 事件处理
	// ==========================================

	/**
	 * 静态回调函数用于按钮点击
	 * @param Button 被点击的按钮
	 * @param DayIndex 对应的天数索引
	 */
	static void StaticButtonCallback(UButton* Button, int32 DayIndex);

	/**
	 * 处理天数按钮点击事件
	 * @param DayIdentifier 天数标识
	 * @param DayIndex 天数索引
	 */
	void OnDayButtonClicked(const FString& DayIdentifier, int32 DayIndex);

	/**
	 * 处理天数按钮点击事件（无参包装器）
	 * 用于绑定到 FOnButtonClickedEvent 委托
	 */
	UFUNCTION()
	void HandleDayButtonClicked();

	// ==========================================
	// 7. 初始化方法
	// ==========================================

	/**
	 * 初始化 UI 控件
	 * 1. UpdateExperienceDisplay
	 * 2. UpdateBonusInfoText
	 * 3. UpdateRewardItem
	 * 4. UpdateBonusIconsContainer
	 * 5. UpdateDayButtons
	 * 6. UpdateDailyTasks
	 */
	void InitializeUI();

	/**
	 * 绑定事件处理器
	 * 绑定 ReselectRewardButton / DayButtons / FixedPrizeWidget
	 */
	void BindEventHandlers();

	/**
	 * 解绑事件处理器
	 * 解绑所有事件（包括 Subsystem 事件）
	 */
	void UnbindEventHandlers();

	// ==========================================
	// 8. UI 更新方法
	// ==========================================

	/**
	 * 获取页面唯一身份标识
	 * @return 页面身份字符串（包含地址和创建时间等信息）
	 */
	FString GetPageIdentity() const;

	/**
	 * 更新加成图标显示（BonusIconsContainer）
	 */
	void UpdateBonusIcons();

	/**
	 * 更新限时加成图标容器
	 * 根据活动时效性动态加载 WBP_RewardIcon 组件
	 */
	void UpdateBonusIconsContainer();

	/**
	 * 更新奖励物品显示（基于 CachedItemIcons + CurrentDayIndex）
	 */
	void UpdateRewardItem();

	/**
	 * 更新天数按钮（第 1-7 天的可点击按钮）
	 */
	void UpdateDayButtons();

	/**
	 * 更新加成信息文本
	 */
	void UpdateBonusInfoText();

	/**
	 * 更新任务列表（TasksContainer）
	 */
	void UpdateTasks();

	/**
	 * 更新经验值显示（CurrentExpText）
	 */
	void UpdateExperienceDisplay();

	/**
	 * 更新物品图标滚动列表（ItemsScrollBox 内的 ExperienceChestClaimWidget）
	 */
	void UpdateItemsScrollBox();

	/** 更新每日任务列表（DayButtonsContainer 内的 UDailyTaskWidget） */
	void UpdateDailyTasks();

	// ==========================================
	// 9. 事件处理方法
	// ==========================================

	/**
	 * 处理任务完成状态变化
	 * @param TaskIndex 任务索引
	 * @param bCompleted 是否完成
	 */
	void OnTaskCompletionChanged(int32 TaskIndex, bool bCompleted);

	// ==========================================
	// 10. 辅助方法
	// ==========================================

	/**
	 * 计算当前加成倍数
	 * @param DayIndex 天数索引
	 * @return 加成倍数
	 */
	float CalculateBonusMultiplier(int32 DayIndex);

	/**
	 * 获取天数对应的奖励数据
	 * @param DayIndex 天数索引
	 * @return 奖励数据
	 */
	TArray<int32> GetDayRewards(int32 DayIndex);

private:
	// ==========================================
	// 11. 图标管理方法
	// ==========================================

	/**
	 * 初始化奖励物品图标数据
	 * 从配置表读取数据并缓存 ItemIcon 到全局变量
	 */
	void InitializeRewardItemIcons();

	/**
	 * 更新奖励物品图片显示
	 * 使用缓存的图标数据显示 RewardItemImage 控件
	 */
	void UpdateRewardItemImage();

	/**
	 * 更新限时加成信息文本
	 * @param DayIdentifier 天数标识符
	 */
	void UpdateBonusInfoText(const FString& DayIdentifier);

	/**
	 * 切换到下一个奖励图标
	 * 循环切换 CachedItemIcons 数组中的图标索引
	 */
	UFUNCTION(BlueprintCallable, Category = "DailyUpgradeReward")
	void SwitchToNextRewardIcon();

	/**
	 * 切换到上一个奖励图标
	 * 循环切换 CachedItemIcons 数组中的图标索引
	 */
	UFUNCTION(BlueprintCallable, Category = "DailyUpgradeReward")
	void SwitchToPreviousRewardIcon();

	/**
	 * 更新宝箱数量文本显示
	 * 从 DailyUpgradeRewardConfigRow 表获取 ActivityID==110 数据的 RewardItemCounts 最后一个索引值
	 */
	void UpdateChestCountText();

	/**
	 * 初始化 ItemsScrollBox 中的 ExperienceChestClaimWidget
	 * 根据 RewardItemIDs 创建多个 ExperienceChestClaimWidget 并设置对应的 BoxIcon
	 */
	void InitializeExperienceChestWidgets();

	/**
	 * 刷新 UI 显示
	 * 重新初始化所有 UI 组件以反映最新状态
	 */
	UFUNCTION()
	void RefreshUI();

	/**
	 * 处理重选奖励按钮点击事件
	 */
	UFUNCTION()
	void OnReselectRewardClicked();

	/**
	 * 处理宝箱领取请求
	 * @param ChestIndex 宝箱索引
	 */
	UFUNCTION()
	void HandleChestClaimRequest(int32 ChestIndex);

	/**
	 * 处理奖励存储操作
	 * @param DayIndex 天数索引
	 */
	UFUNCTION()
	void HandleRewardStore(int32 DayIndex);

	/**
	 * 更新所有宝箱 Widget 的状态
	 * 手动刷新 ItemsScrollBox 中所有 ExperienceChestClaimWidget 的视觉状态
	 * 确保领取操作后所有 Widget 都能正确显示最新状态
	 */
	void UpdateAllChestWidgetStates();

	/**
	 * 更新经验宝箱控件状态
	 * 更新现有 ExperienceChestClaimWidget 的状态而不重新创建
	 */
	void UpdateExperienceChestWidgetsState();

	/**
	 * 手动刷新所有 ExperienceChestClaimWidget 的进度条
	 * 在经验值发生变化后调用此方法更新所有进度条显示
	 */
	UFUNCTION(BlueprintCallable, Category = "Daily Upgrade Reward")
	void RefreshAllProgressBars();

	/**
	 * 订阅 Subsystem 事件
	 */
	void SubscribeToSubsystemEvents();

	/**
	 * 取消订阅 Subsystem 事件
	 */
	void UnsubscribeFromSubsystemEvents();

	/**
	 * 处理奖励图标索引更新事件
	 * @param NewIndex 新的图标索引
	 */
	UFUNCTION()
	void OnRewardIconIndexChanged(int32 NewIndex);

	/**
	 * 显示奖励选项 Widget
	 * @param ChestIndex 宝箱索引
	 */
	void ShowRewardOptionWidget(int32 ChestIndex);

	/**
	 * 初始化固定奖励控件
	 * 根据 TaskRelatedValues 最后一个索引值和 ChestClaimStatus 状态控制 HighlightFrameImage 显示
	 */
	void InitializeFixedPrizeWidget();

	/**
	 * 更新固定奖励控件状态
	 * 根据当前经验和领取状态更新 FixedPrizeWidget 的显示状态
	 */
	void UpdateFixedPrizeWidget();

	/**
	 * 根据当前经验值居中显示 ScrollBox 内容
	 * 读取 UpgradeActivitySubsystem 内存数据的 CurrentExperience 值，
	 * 根据 TaskRelatedValues 数组计算应该居中的宝箱索引，并设置 ScrollBox 滚动位置
	 */
	void CenterScrollBoxOnCurrentExperience();

	/**
	 * 根据当前经验值找到目标宝箱索引
	 * @param CurrentExp 当前经验值
	 * @param TaskRelatedValues 任务相关经验值数组
	 * @return 应该居中的宝箱索引
	 */
	int32 FindTargetChestIndexForExperience(int32 CurrentExp, const TArray<int32>& TaskRelatedValues);

	/**
	 * 计算使目标控件居中显示的滚动偏移量
	 * @param TargetIndex 目标控件索引
	 * @return 滚动偏移量
	 */
	float CalculateCenterScrollOffset(int32 TargetIndex);

	/**
	 * 计算 ScrollBox 的最大滚动偏移量
	 * @return 最大滚动偏移量
	 */
	float CalculateMaxScrollOffset();
};
