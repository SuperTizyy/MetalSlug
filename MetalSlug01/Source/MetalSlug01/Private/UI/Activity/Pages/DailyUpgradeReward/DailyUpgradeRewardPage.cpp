/**
 * @file DailyUpgradeRewardPage.cpp
 * @brief 每日升级奖励活动页面实现
 * @author AI Assistant
 * @date 2026
 * @version 1.0
 *
 * @details 实现每日升级奖励活动页面的核心功能
 *
 * ====================================================================
 * 文件实现说明（按代码顺序）
 * ====================================================================
 *
 * §1. 头文件包含区
 *   - UMG 控件 (Border, HBox, Image, Button, TextBlock, ProgressBar, VBox, ScrollBox)
 *   - Styling/SlateTypes（按钮样式）
 *   - ActivitySubsystem / UpgradeActivitySubsystem（数据源）
 *   - 子 Widget: ExperienceChestClaimWidget / TaskDetailWidget / DayLockHintWidget
 *
 * §2. 生命周期 (Initialize / NativeConstruct / NativeDestruct)
 *   - Initialize: 绑定 ReselectRewardButton
 *   - NativeConstruct: 0.2s 延迟居中 ScrollBox
 *   - NativeDestruct: 解绑 ReselectRewardButton（保留 Subsystem 订阅, 让缓存页可继续接收事件）
 *
 * §3. 奖励物品图标 (InitializeRewardItemIcons / UpdateRewardItemImage / SwitchToNext/PreviousRewardIcon)
 *   - CachedItemIcons: 全局缓存, 用于循环切换
 *   - 依赖 Subsystem: GetRewardItemIcons / GetCurrentRewardIconIndex
 *   - 编辑器预览模式下静默返回
 *
 * §4. 宝箱数量 (UpdateChestCountText)
 *   - 格式: "X{count}" - 拼接 X 前缀
 *
 * §5. 经验宝箱控件列表 (InitializeExperienceChestWidgets)
 *   - 动态创建 ExperienceChestClaimWidget（刨除最后一个索引）
 *   - 状态机: 已领取 / 满足经验 / 经验不足
 *   - 设置 HighlightFrameImage 可见性
 *   - **防御链**: ItemsScrollBox / GameInstance / Subsystem / Config / ChestBoxIcons
 *
 * §6. 经验值显示 (UpdateExperienceDisplay)
 *   - 简单同步: Subsystem->GetCurrentExperience -> CurrentExpText
 *
 * §7. 每日任务列表 (UpdateDailyTasks)
 *   - 动态创建 UDailyTaskWidget 到 DayButtonsContainer
 *   - 业务逻辑下沉: Subsystem 计算 HighlightStates / LockStates
 *   - 默认选中 MaxRecordDate 那一天
 *   - 清理 TasksContainer 时保留 BonusInfoBorder / BonusInfoText
 *
 * §8. 天数按钮点击 (OnDayButtonClicked)
 *   - 流程: 提取 DayNumber -> RefreshDailyTaskHighlights -> 清理 TasksContainer
 *   - HasDayDataInMemory 检查: 否则显示 DayLockHintWidget
 *   - 动态创建 TaskDetailWidget + SetupClaimButton + SetupRewardsContainer
 *
 * §9. 整体刷新 (RefreshUI)
 *   - 7 步骤: 图标 -> 宝箱状态 -> 进度条 -> 居中 -> 宝箱数量 -> 经验值 -> 奖励图 -> 任务 -> 固定奖励
 *   - 自我身份核对: GetPageIdentity 输出
 *
 * §10. 按钮事件包装器 (HandleDayButtonClicked)
 *   - 通过 IsHovered() 反查被点击的按钮（无参委托技巧）
 *   - 防御: ButtonToDayIndexMap 为空 / 按钮失效
 *
 * §11. 重选奖励 (OnReselectRewardClicked)
 *   - 创建 ActivityConfirmPopupWidget
 *   - 弹窗内由用户选择索引 -> UpdateRewardIconIndexAndSave
 *
 * §12. Subsystem 事件订阅 (SubscribeToSubsystemEvents / UnsubscribeFromSubsystemEvents / OnRewardIconIndexChanged)
 *   - 订阅: OnRewardIconIndexChanged / OnGlobalRefresh
 *   - 防御: 先 RemoveDynamic 防止 NativeConstruct 多次触发导致重复绑定
 *
 * §13. 手动刷新 (ManualRefreshUI / RefreshDailyTaskHighlights)
 *   - ManualRefreshUI: 直接调用 RefreshUI
 *   - RefreshDailyTaskHighlights: 临时高亮覆盖自动高亮
 *
 * §14. 页面身份 (GetPageIdentity)
 *   - 格式: "Page[0xADDRESS]@MMdd-HHmmss"
 *
 * §15. 事件处理 (HandleChestClaimRequest / ShowRewardOptionWidget / HandleRewardStore)
 *   - HandleChestClaimRequest -> ShowRewardOptionWidget -> CreateWidget<URewardOptionWidget>
 *   - HandleRewardStore: 同步 AllRecords + CurrentRecord + OnGlobalRefresh.Broadcast
 *   - TObjectIterator 查找并关闭 RewardOptionWidget 弹窗
 *
 * §16. 固定奖励 (InitializeFixedPrizeWidget / UpdateFixedPrizeWidget)
 *   - 数据下沉: Subsystem->ShouldShowFixedPrizeHighlight / GetFixedPrizeExperienceValue
 *   - 复用 InitializeFixedPrizeWidget 完成所有更新
 *
 * §17. 滚动居中 (CenterScrollBoxOnCurrentExperience / FindTargetChestIndexForExperience / CalculateCenterScrollOffset / CalculateMaxScrollOffset)
 *   - 目标: ItemsScrollBox 居中显示当前经验对应的宝箱
 *   - 备用方案: 估算 WidgetWidth=130 / Spacing=10
 *   - 几何信息无效时通过全局 bool 标志降级
 *
 * §18. 加成信息文本 (UpdateBonusInfoText)
 *   - 格式: "{Description} {Complete}/{Total} 剩余时长: {H}H {M}M"
 *   - 过期时: 隐藏 BonusInfoText + BonusInfoBorder
 *
 * @section Overview 系统概述
 * 该文件实现了每日升级奖励活动页面的完整功能，采用 Subsystem 模式实现
 * 数据访问层与 UI 层的分离，确保代码的可维护性和扩展性。
 *
 * @section Architecture 系统架构
 * - UI 层：DailyUpgradeRewardPage 负责界面显示和用户交互
 * - 数据层：UpgradeActivitySubsystem 提供数据访问接口
 * - 配置层：通过 DataTable 配置活动规则和奖励信息
 *
 * @section KeyFeatures 核心功能
 * 1. 奖励物品图标动态加载和显示
 * 2. 经验宝箱状态的实时更新
 * 3. 重选奖励功能的弹窗交互
 * 4. 编辑器预览模式的支持
 * 5. 完善的事件绑定和资源管理
 *
 * @section DataFlow 数据流向
 * 配置表 (DT_DailyUpgradeRewardConfigRow) → Subsystem → UI 组件 → 界面显示
 *
 * @section BestPractices 最佳实践
 * - 使用 Subsystem 模式解耦数据访问
 * - 实现完善的错误处理和日志记录
 * - 支持编辑器预览模式下的优雅降级
 * - 遵循 UE C++ 编码规范和内存管理原则
 */

// ==========================================
// §1. 头文件包含区
// ==========================================
#include "UI/Activity/Pages/DailyUpgradeReward/DailyUpgradeRewardPage.h"
#include "UI/Activity/Pages/DailyUpgradeReward/DailyUpgradeRewardViewModel.h" // 改造: ViewModel
#include "Components/Border.h"
#include "Components/HorizontalBox.h"
#include "Components/Image.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/ProgressBar.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/ScrollBox.h"
#include "Components/EditableTextBox.h" // 【v213 新增】调试用
#include "Engine/Engine.h"              // 【v213 新增】GEngine 屏幕提示
#include "Styling/SlateTypes.h"
#include "Components/ComboBoxString.h"  // 【v31.5.1】必须在 SlateTypes.h 之后, 否则 EUMGSequencePlayMode 未定义
#include "Styling/SlateTypes.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/EngineTypes.h"
#include "Systems/Activity/ActivitySubsystem.h"
#include "Systems/Activity/UpgradeActivitySubsystem.h"
#include "Data/Tables/DailyLoginTableRow.h"
#include "UI/Activity/Pages/DailyUpgradeReward/ExperienceChestClaimWidget.h"
#include "UI/Activity/Pages/SelectMultiplePopup/ActivityConfirmPopupWidget.h"
#include "UI/Activity/Pages/DailyUpgradeReward/DailyTaskWidget.h"
#include "UI/Activity/Pages/DailyUpgradeReward/TaskDetailWidget.h"
#include "UI/Activity/Pages/DailyUpgradeReward/DayLockHintWidget.h"
#include "UI/Activity/Pages/ClaimBox/RewardOptionWidget.h"
#include "UI/Activity/Pages/DailyUpgradeReward/DailyUpgradeRewardViewModel.h" // 【v213 新增】确保 ViewModel 实现已链接

/**
 * @brief 初始化每日升级奖励页面
 * @details 在Widget初始化阶段设置默认值并绑定UI事件
 * 主要功能：
 * 1. 调用父类初始化
 * 2. 设置默认的游戏状态值
 * 3. 绑定重选奖励按钮的点击事件
 * @return 初始化是否成功
 * @note 此方法在构造函数之后、NativeConstruct之前调用
 */
bool UDailyUpgradeRewardPage::Initialize()
{
	if (!Super::Initialize())
	{
		return false;
	}

	// 改造: 创建 ViewModel (不 Bind, Bind 在 NativeConstruct 中)
	// 【vXXX.1 大厂修复】Bind 必须在每次页面激活时执行, 否则页面缓存激活时 ViewModel->Subsystem 为 null
	if (!ViewModel)
	{
		ViewModel = NewObject<UDailyUpgradeRewardViewModel>(this);
	}

	// 初始化默认值
	CurrentDayIndex = 1;
	CurrentExperience = 0;
	CurrentBonusMultiplier = 1.0f;


	// ==========================================
	// 【v213 新增】调试数据提交控件绑定 + ComboBox 初始化
	// 大厂原则:
	//   - 仅绑定事件 + 填充 ComboBox 选项, 不在此处做业务逻辑
	//   - 业务逻辑全在回调函数中委托 ViewModel
	// ==========================================

	if (ComboBoxString_SelectedDay)
	{
		// 清空已有项 (防御性: 重复 Initialize 不重复添加)
		ComboBoxString_SelectedDay->ClearOptions();

		// 用户需求: 选项 1~5
		ComboBoxString_SelectedDay->AddOption(TEXT("1"));
		ComboBoxString_SelectedDay->AddOption(TEXT("2"));
		ComboBoxString_SelectedDay->AddOption(TEXT("3"));
		ComboBoxString_SelectedDay->AddOption(TEXT("4"));
		ComboBoxString_SelectedDay->AddOption(TEXT("5"));

		// 默认选中: 1 (用户最常改当前天)
		ComboBoxString_SelectedDay->SetSelectedOption(TEXT("1"));

		// 绑定选项变化回调
		ComboBoxString_SelectedDay->OnSelectionChanged.AddDynamic(
			this, &UDailyUpgradeRewardPage::OnDebugDayComboBoxSelectionChanged);
	}
	else
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[DailyUpgradeRewardPage] Initialize: ComboBoxString_SelectedDay 未绑定 (蓝图缺控件)"));
	}

	if (EditableTextInput_NewExp)
	{
		// 仅设置 HintText, 不预设数值 (避免误改存档)
		EditableTextInput_NewExp->SetHintText(FText::FromString(TEXT("新经验值 (>=0)")));
	}
	else
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[DailyUpgradeRewardPage] Initialize: EditableTextInput_NewExp 未绑定 (蓝图缺控件)"));
	}

	// 【vXXX.1 修复】调试按钮 OnClicked 绑定已移至 BindDebugButtons(), 在 NativeConstruct 调用
	// 原因: Initialize() 仅首次创建时调用一次, 页面从缓存激活时不会重新调用 → 按钮 OnClicked 失效
	// 修复: 模仿 BindReselectRewardButton 模式, 在 NativeConstruct 中再次绑定 (包含先解绑)

	// ==========================================
	// 【v222 新增】一键重置按钮绑定
	// 【vXXX.1 修复】OnClicked 绑定已移至 BindDebugButtons(), 此处保留变量检查日志
	// ==========================================

	// ==========================================
	// 【v213.1 新增】任务完成次数 ComboBox 绑定 (选项 0-9)
	// 大厂原则:
	//   - 仅绑定事件 + 填充 ComboBox 选项, 不做业务逻辑
	//   - SelectedDay 切换时不刷新 (用户选择 B: 保持上次选择的值)
	//   - 回调仅防崩溃, 不触发任何存档操作
	// ==========================================
	BindTaskCountComboBox(ComboBoxString_Task1Count);
	BindTaskCountComboBox(ComboBoxString_Task2Count);
	BindTaskCountComboBox(ComboBoxString_Task3Count);

	// 【v213.1 新增】绑定任务次数 ComboBox 的 OnSelectionChanged 事件
	BindTaskCountComboBoxCallbacks();

	return true;
}

/**
 * @brief 原生构造函数 - UI完全构建完成后执行
 * @details 在Widget的所有子控件都创建完毕后调用
 * 主要功能：
 * 1. 调用父类原生构造
 * 2. 初始化奖励物品图标缓存
 * 3. 更新宝箱数量显示文本
 * 4. 初始化经验宝箱控件列表
 * @note 此阶段所有UI控件均已创建完成，可以安全访问
 */
void UDailyUpgradeRewardPage::NativeConstruct()
{
	// 初始化当前选中的天数索引为-1（表示未选择）
	CurrentDayIndex = -1;
	CurrentSelectedDay = 0; // 0表示没有临时高亮

	// 【v228 Bug 1 修】初始化 ComboBox 待提交快照
	//   - 旧实现 (-1) 会导致: BP 默认 ComboBox=1, 但用户没切换直接点 Apply, 此时 PendingSelectedDay=-1
	//     → 校验 [1,5] 失败 → Log Error "PendingSelectedDay=-1" (用户报告)
	//   - 修正: 默认值 = 1 (与 ComboBox BP 初始选项同步), 与 .h 字段默认值一致
	PendingSelectedDay = 1;

	// 清空按钮映射表以避免重复添加
	ButtonToDayIndexMap.Empty();

	// 【vXXX.1 大厂修复】ViewModel.Bind 必须在 NativeConstruct 中执行, 防止页面缓存激活时 Subsystem 丢失
	// 大厂原则: ViewModel.Bind 依赖运行时 GameInstance, 必须在每次页面激活时重新绑定
	if (ViewModel)
	{
		UGameInstance* GI = GetGameInstance();
		if (GI)
		{
			if (UUpgradeActivitySubsystem* UpgradeSub = GI->GetSubsystem<UUpgradeActivitySubsystem>())
			{
				ViewModel->Bind(UpgradeSub);
			}
			else
			{
				UE_LOG(LogTemp, Error,
					TEXT("[DailyUpgradeRewardPage] NativeConstruct: 无法获取 UUpgradeActivitySubsystem, ViewModel.Bind 失败"));
			}
		}
	}

	Super::NativeConstruct();
	// 【vXXX 大厂修复】按钮绑定移到 NativeConstruct，确保页面从缓存激活时重新绑定
	BindReselectRewardButton();

	// 【vXXX.1 修复】调试按钮绑定 (Button_ApplyDebugValues / Button_ResetAllActivity / ComboBoxString_SelectedDay)
	// 原因: 模仿 BindReselectRewardButton 模式, 防止页面从缓存激活时按钮失效
	BindDebugButtons();

	// 订阅UpgradeActivitySubsystem的奖励图标索引更新事件
	SubscribeToSubsystemEvents();

	// 在NativeConstruct阶段执行初始化逻辑，确保UI完全准备好
	InitializeRewardItemIcons();
	UpdateChestCountText();
	InitializeExperienceChestWidgets();
	InitializeFixedPrizeWidget();  // 初始化固定奖励控件
	UpdateExperienceDisplay();
	UpdateDailyTasks();  // 初始化每日任务列表

	// 🔧 【v215 修复】首次进入页面: 显式触发默认 day 渲染 (大厂原则: 职责分离)
	// 🐛 原 Bug: UpdateDailyTasks 内部强制把 CurrentDayIndex 改回 MaxRecordDate (反模式)
	// ✅ 修复后: UpdateDailyTasks 不再改 CurrentDayIndex; 这里显式触发首次默认 day
	//   - 取 MaxRecordDate 作为默认 day (符合用户原期望: 打开页面看最新 day 任务)
	//   - 注意: 这段只在首次 NativeConstruct 跑, 不影响后续 RefreshUI 的"保留选中 day" 行为
	{
		UGameInstance* GI = GetGameInstance();
		if (GI)
		{
			if (UUpgradeActivitySubsystem* UpgradeSub = GI->GetSubsystem<UUpgradeActivitySubsystem>())
			{
				const int32 MaxDay = UpgradeSub->GetMaxRecordDate();
				if (MaxDay >= 1)
				{
					CurrentDayIndex = MaxDay - 1; // 0-based 索引
					const FString DefaultDayIdentifier = FString::Printf(TEXT("day%d"), MaxDay);
					OnDayButtonClicked(DefaultDayIdentifier, CurrentDayIndex);
					UE_LOG(LogTemp, Log,
						TEXT("[DailyUpgradeRewardPage] NativeConstruct: 首次进入, 默认显示 day=%d 的任务详情"),
						MaxDay);
				}
			}
		}
	}
	
	// 延迟执行居中显示，等待UI完全渲染完成
	FTimerHandle CenterTimerHandle;
	GetWorld()->GetTimerManager().SetTimer(CenterTimerHandle, this, &UDailyUpgradeRewardPage::CenterScrollBoxOnCurrentExperience, 0.2f, false);
	
	// 检查RewardIconWidgetClass是否已设置
	if (!RewardIconWidgetClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("DailyUpgradeRewardPage: RewardIconWidgetClass未设置，请在蓝图中指定WBP_RewardIcon类"));
	}
	
	
	
}

/**
 * @brief 原生析构函数 - Widget销毁前执行清理工作
 * @details 在Widget即将被销毁时调用，用于释放资源和解绑事件
 * 主要功能：
 * 1. 解绑重选奖励按钮的点击事件
 * 2. 防止悬空指针和内存泄漏
 * 3. 调用父类原生析构
 * @note 必须在Super::NativeDestruct()之前解绑事件
 */
void UDailyUpgradeRewardPage::NativeDestruct()
{
	// 【vXXX.1 大厂架构修复】取消订阅Subsystem事件
	// 根本原因: NativeDestruct 故意不解绑会导致多个缓存页面同时响应子系统广播
	// 架构问题: "让缓存的页面也能持续接收广播" 是错误的设计决策
	// 大厂原则: 订阅/取消订阅必须成对出现，不允许残留绑定
	// 修复: 页面销毁时必须解绑，页面激活时重新订阅
	UnsubscribeFromSubsystemEvents();

	// 停止 Bonus 倒计时定时器, 防止页面销毁后回调触发野指针
	StopBonusCountdownTimer();

	// 解绑重选奖励按钮事件
	if (ReselectRewardButton)
	{
		ReselectRewardButton->OnClicked.RemoveDynamic(this, &UDailyUpgradeRewardPage::OnReselectRewardClicked);

	}

	// ==========================================
	// 【v213 新增】解绑调试数据提交控件事件
	// 大厂原则: 与 Initialize 对称解绑, 防止页面销毁后回调触发野指针
	// ==========================================
	if (ComboBoxString_SelectedDay)
	{
		ComboBoxString_SelectedDay->OnSelectionChanged.RemoveDynamic(
			this, &UDailyUpgradeRewardPage::OnDebugDayComboBoxSelectionChanged);
	}

	// ==========================================
	// 【v213.1 新增】解绑任务完成次数 ComboBox
	// ==========================================
	if (ComboBoxString_Task1Count)
	{
		ComboBoxString_Task1Count->OnSelectionChanged.RemoveDynamic(
			this, &UDailyUpgradeRewardPage::OnTask1CountComboBoxChanged);
	}
	if (ComboBoxString_Task2Count)
	{
		ComboBoxString_Task2Count->OnSelectionChanged.RemoveDynamic(
			this, &UDailyUpgradeRewardPage::OnTask2CountComboBoxChanged);
	}
	if (ComboBoxString_Task3Count)
	{
		ComboBoxString_Task3Count->OnSelectionChanged.RemoveDynamic(
			this, &UDailyUpgradeRewardPage::OnTask3CountComboBoxChanged);
	}

	if (Button_ApplyDebugValues)
	{
		Button_ApplyDebugValues->OnClicked.RemoveDynamic(
			this, &UDailyUpgradeRewardPage::OnApplyDebugValuesClicked);
	}

	// 【v222 新增】一键重置按钮解绑
	if (Button_ResetAllActivity)
	{
		Button_ResetAllActivity->OnClicked.RemoveDynamic(
			this, &UDailyUpgradeRewardPage::OnResetAllActivityClicked);
	}

	// 【Ensure 修复】解绑 FixedPrizeWidget 事件 + 重置幂等标志
	if (FixedPrizeWidget && bIsFixedPrizeWidgetEventBound)
	{
		FixedPrizeWidget->OnChestClaimRequested.RemoveDynamic(this, &UDailyUpgradeRewardPage::HandleChestClaimRequest);
		bIsFixedPrizeWidgetEventBound = false;
	}

	// 改造: 解除 ViewModel 绑定
	if (ViewModel)
	{
		ViewModel->Unbind();
	}

	Super::NativeDestruct();
}

/**
 * @brief 初始化奖励物品图标缓存
 * @details 从配置表中读取数据，通过多表关联获取奖励物品的图标资源
 * 数据流向：
 * 1. 读取DT_DailyUpgradeRewardConfigRow表(ActivityID==110)
 * 2. 获取RewardItemIDs数组的最后一个元素作为BoxID
 * 3. 通过ActivitySubsystem查询TreasureBoxItemRow表
 * 4. 通过ItemID关联ItemDetailRow表获取ItemIcon
 * 5. 将所有ItemIcon缓存到CachedItemIcons数组
 * @note 支持编辑器预览模式下的优雅降级处理
 */
void UDailyUpgradeRewardPage::InitializeRewardItemIcons()
{
	// 检查是否为编辑器预览模式
	if (!GetWorld() || !GetWorld()->IsGameWorld())
	{
		return; // 静默返回，不输出日志
	}

	// 全局变量存储ItemIcon数据
	CachedItemIcons.Empty();

	// 1. 通过GameInstance获取UpgradeActivitySubsystem
	UGameInstance* GameInstance = GetGameInstance();
	if (!GameInstance)
	{
		// 在编辑器预览模式下静默处理
		UpdateRewardItemImage();
		return;
	}

	UUpgradeActivitySubsystem* UpgradeSub = GameInstance->GetSubsystem<UUpgradeActivitySubsystem>();
	if (!UpgradeSub)
	{
		// 在编辑器预览模式下静默处理
		UpdateRewardItemImage();
		return;
	}

	// 2. 调用Subsystem方法获取奖励物品图标数据
	CachedItemIcons = UpgradeSub->GetRewardItemIcons();
	
	// 3. 更新RewardItemImage显示
	UpdateRewardItemImage();
}

/**
 * @brief 更新奖励物品图像显示
 * @details 将缓存的ItemIcon数据显示到RewardItemImage控件上
 * 主要功能：
 * 1. 检查RewardItemImage控件是否存在
 * 2. 如果有缓存的图标，则显示当前索引的图标
 * 3. 如果没有缓存图标，则隐藏控件
 * @note 使用SetBrushFromSoftTexture支持异步资源加载
 */
void UDailyUpgradeRewardPage::UpdateRewardItemImage()
{
	if (!RewardItemImage)
	{
		return;
	}

	if (CachedItemIcons.Num() > 0)
	{
		// 通过GameInstance获取UpgradeActivitySubsystem来获取当前索引
		UGameInstance* GameInstance = GetGameInstance();
		if (GameInstance)
		{
			UUpgradeActivitySubsystem* UpgradeSub = GameInstance->GetSubsystem<UUpgradeActivitySubsystem>();
			if (UpgradeSub)
			{
				int32 CurrentIndex = UpgradeSub->GetCurrentRewardIconIndex();
				
				// 确保索引在有效范围内
				if (CurrentIndex >= 0 && CurrentIndex < CachedItemIcons.Num())
				{
					RewardItemImage->SetBrushFromSoftTexture(CachedItemIcons[CurrentIndex]);	
				}
				else
				{
					// 索引超出范围，显示第一个图标
					RewardItemImage->SetBrushFromSoftTexture(CachedItemIcons[0]);
				}
			}
			else
			{
				// 无法获取Subsystem，显示第一个图标
				RewardItemImage->SetBrushFromSoftTexture(CachedItemIcons[0]);
			}
		}
		else
		{
			// 无法获取GameInstance，显示第一个图标
			RewardItemImage->SetBrushFromSoftTexture(CachedItemIcons[0]);
		}
	}
	else
	{
		// 在编辑器预览模式下静默处理，不输出日志
		RewardItemImage->SetVisibility(ESlateVisibility::Hidden);
	}
}

/**
 * @brief 切换到下一个奖励图标
 * @details 循环切换CachedItemIcons数组中的图标索引，并更新Subsystem中的RewardIconIndex
 */
void UDailyUpgradeRewardPage::SwitchToNextRewardIcon()
{
	if (CachedItemIcons.Num() <= 1)
	{
		return;
	}

	// 通过GameInstance获取UpgradeActivitySubsystem
	UGameInstance* GameInstance = GetGameInstance();
	if (!GameInstance)
	{
		return;
	}

	UUpgradeActivitySubsystem* UpgradeSub = GameInstance->GetSubsystem<UUpgradeActivitySubsystem>();
	if (!UpgradeSub)
	{
		return;
	}

	// 获取当前索引并计算下一个索引
	int32 CurrentIndex = UpgradeSub->GetCurrentRewardIconIndex();
	int32 NextIndex = (CurrentIndex + 1) % CachedItemIcons.Num();

	// 设置新的索引
	if (UpgradeSub->SetCurrentRewardIconIndex(NextIndex))
	{
		UpdateRewardItemImage();
	}
}

/**
 * @brief 切换到上一个奖励图标
 * @details 循环切换CachedItemIcons数组中的图标索引，并更新Subsystem中的RewardIconIndex
 */
void UDailyUpgradeRewardPage::SwitchToPreviousRewardIcon()
{
	if (CachedItemIcons.Num() <= 1)
	{
		return;
	}

	// 通过GameInstance获取UpgradeActivitySubsystem
	UGameInstance* GameInstance = GetGameInstance();
	if (!GameInstance)
	{
		return;
	}

	UUpgradeActivitySubsystem* UpgradeSub = GameInstance->GetSubsystem<UUpgradeActivitySubsystem>();
	if (!UpgradeSub)
	{
		return;
	}

	// 获取当前索引并计算上一个索引
	int32 CurrentIndex = UpgradeSub->GetCurrentRewardIconIndex();
	int32 PreviousIndex = (CurrentIndex - 1 + CachedItemIcons.Num()) % CachedItemIcons.Num();

	// 设置新的索引
	if (UpgradeSub->SetCurrentRewardIconIndex(PreviousIndex))
	{
		UpdateRewardItemImage();
	}
}

/**
 * @brief 更新宝箱数量显示文本 (页面级 ChestCountText)
 * @details 🔧【v218 大厂原则 SSOT】联动 popup 选中:
 *          - 数据源: UUpgradeActivitySubsystem::GetCurrentRewardItemCount()
 *          - 链路: MainConfig.RewardItemIDs.Last() → BoxID → TreasureBoxItems[CurrentRewardIconIndex].ItemCount
 *          - 与 WBP_RewardOptionCardWidget::RewardText 显示的 ItemCount 完全一致
 *          - 事件: 订阅 OnRewardIconIndexChanged, popup 选中卡片后自动同步
 *          - 零兜底: API 返回 -1 (查不到任何一步) → 显示空字符串 + Log Error
 * @note 在编辑器预览模式下会静默返回
 */
void UDailyUpgradeRewardPage::UpdateChestCountText()
{
	if (!ChestCountText)
	{
		return;
	}

	// 检查是否为编辑器预览模式
	if (!GetWorld() || !GetWorld()->IsGameWorld())
	{
		return; // 静默返回，不输出日志
	}

	UGameInstance* GameInstance = GetGameInstance();
	if (!GameInstance)
	{
		ChestCountText->SetText(FText::FromString(TEXT("")));
		UE_LOG(LogTemp, Error, TEXT("[v218] UpdateChestCountText: 无法获取GameInstance, 清空 ChestCountText"));
		return;
	}

	UUpgradeActivitySubsystem* Sub = GameInstance->GetSubsystem<UUpgradeActivitySubsystem>();
	if (!Sub)
	{
		ChestCountText->SetText(FText::FromString(TEXT("")));
		UE_LOG(LogTemp, Error, TEXT("[v218] UpdateChestCountText: 无法获取UpgradeActivitySubsystem, 清空 ChestCountText"));
		return;
	}

	// 🔧【v218 SSOT】读 GetCurrentRewardItemCount (与 popup RewardText 同步)
	const int32 ItemCount = Sub->GetCurrentRewardItemCount();
	if (ItemCount < 0)
	{
		// API 内部已 Log Error 给出具体失败点, 此处按"无可用值"显式清空 (零兜底: 不显示假数据)
		ChestCountText->SetText(FText::FromString(TEXT("")));
		return;
	}

	ChestCountText->SetText(FText::FromString(FString::Printf(TEXT("X%d"), ItemCount)));
}

/**
 * @brief 初始化经验宝箱控件列表
 * @details 动态创建经验宝箱控件并根据存档状态设置显示效果
 * 主要功能：
 * 1. 通过UpgradeActivitySubsystem获取活动配置和玩家记录
 * 2. 遍历RewardItemIDs数组创建对应的ExperienceChestClaimWidget
 * 3. 根据存档状态(Record.ChestClaimStatus)设置控件的视觉状态
 * 4. 设置每个宝箱的奖励数量显示
 * @note 使用Subsystem模式实现数据访问层与UI层的分离
 */
void UDailyUpgradeRewardPage::InitializeExperienceChestWidgets()
{
	if (!ItemsScrollBox)
	{
		return;
	}

	// 检查是否为编辑器预览模式
	if (!GetWorld() || !GetWorld()->IsGameWorld())
	{
		return; // 静默返回，不输出日志
	}

	// 通过Subsystem获取配置和记录
	UGameInstance* GameInstance = GetGameInstance();
	if (!GameInstance)
	{
		return;
	}

	UUpgradeActivitySubsystem* Sub = GameInstance->GetSubsystem<UUpgradeActivitySubsystem>();
	if (!Sub)
	{
		return;
	}

	const auto* Config = Sub->GetActivityConfig();
	if (!Config)
	{
		return;
	}

	// 调用UpgradeActivitySubsystem获取宝箱图标数据
	TArray<TSoftObjectPtr<UTexture2D>> ChestBoxIcons = Sub->GetChestBoxIcons();
	
	// 调用UpgradeActivitySubsystem获取TaskRelatedValues数据
	TArray<int32> TaskRelatedValues = Sub->GetTaskRelatedValues();
	
	// 调用UpgradeActivitySubsystem获取当前经验值
	int32 CurrentExpFromSubsystem = Sub->GetCurrentExperience();
	
	// 计算需要显示的Widget数量（刨除最后一个索引）
	int32 DisplayWidgetCount = FMath::Min(Config->RewardItemIDs.Num() - 1, ChestBoxIcons.Num());
	auto& Record = Sub->GetRecord();

	// 【v228 SSOT 重构】ChestClaimStatus 是全局状态, 跨天共享, 必须从 Subsystem 读取全局真源
	// 严禁直接读 Record.ChestClaimStatus (该字段已废弃, 切天后会被重置)
	const TArray<int32>& GlobalChestStatus4 = Sub->GetGlobalChestClaimStatus();

	// 检查 ExperienceChestWidgetClass 是否设置
	if (ExperienceChestWidgetClass == nullptr)
	{
		return;
	}

	// 清空现有的子控件
	ItemsScrollBox->ClearChildren();

	// 遍历RewardItemIDs创建Widget（刨除最后一个索引）
	for (int32 i = 0; i < DisplayWidgetCount; ++i)
	{
		UExperienceChestClaimWidget* ChestWidget = CreateWidget<UExperienceChestClaimWidget>(GetWorld(), ExperienceChestWidgetClass.Get());
		if (!ChestWidget)
		{
			continue;
		}
		
		// 设置宝箱索引
		ChestWidget->SetChestIndex(i);
		
		// 立即更新进度条（使用正确的索引）
		ChestWidget->UpdateProgressBar();
		
		// 关键：绑定事件
		ChestWidget->OnChestClaimRequested.AddDynamic(this, &UDailyUpgradeRewardPage::HandleChestClaimRequest);
		
		// 关键：根据完整条件来决定显示状态
		// 【v228 SSOT】 读 GlobalChestClaimStatus 而非 Record.ChestClaimStatus
		bool bIsClaimed = GlobalChestStatus4.IsValidIndex(i) && GlobalChestStatus4[i] == 1;
		bool bHasEnoughExp = false;
		
		// 检查经验值条件
		if (!bIsClaimed && TaskRelatedValues.IsValidIndex(i))
		{
			int32 RequiredExp = TaskRelatedValues[i];
			bHasEnoughExp = (CurrentExpFromSubsystem >= RequiredExp);
		}
		
		// 根据完整条件设置按钮状态
		if (bIsClaimed)
		{
			// 已领取状态
			ChestWidget->SetButtonClaimedState();
		}
		else if (bHasEnoughExp)
		{
			// 满足条件，启用按钮
			ChestWidget->SetButtonEnabledState();
		}
		else
		{
			// 未满足条件，禁用但保持原外观
			ChestWidget->SetButtonDisabledState();
		}
		
		// 设置奖励数量
		FString RewardCount = TEXT("0");
		if (i < Config->RewardItemCounts.Num())
		{
			RewardCount = Config->RewardItemCounts[i];
		}
		
		if (ChestWidget->ChestCountText)
		{
			FString DisplayText = FString::Printf(TEXT("X%s"), *RewardCount);
			ChestWidget->ChestCountText->SetText(FText::FromString(DisplayText));
		}
		
		// 设置宝箱图标到ChestClaimButton（刨除最后一个索引）
		if (i < ChestBoxIcons.Num() && ChestWidget->ChestClaimButton)
		{
			UTexture2D* BoxIcon = ChestBoxIcons[i].LoadSynchronous();
			if (BoxIcon)
			{
				ChestWidget->SetChestBoxIcon(BoxIcon);
			}
		}
		
		// 设置ExperienceText显示TaskRelatedValues对应索引的数据
		if (i < TaskRelatedValues.Num() && ChestWidget->ExperienceText)
		{
			FString ExperienceValue = FString::FromInt(TaskRelatedValues[i]);
			ChestWidget->ExperienceText->SetText(FText::FromString(ExperienceValue));
			
			// 根据条件控制HighlightFrameImage显示：
			// GlobalChestClaimStatus=0 且 CurrentExperience >= TaskRelatedValues[i] 时显示高亮框
			// 【v228 SSOT】 读 GlobalChestClaimStatus 而非 Record.ChestClaimStatus
			bool bShouldShowHighlight = false;
			if (GlobalChestStatus4.IsValidIndex(i) && GlobalChestStatus4[i] == 0)
			{
				if (CurrentExpFromSubsystem >= TaskRelatedValues[i])
				{
					bShouldShowHighlight = true;
				}
			}
			
			if (ChestWidget->HighlightFrameImage)
			{
				ChestWidget->HighlightFrameImage->SetVisibility(bShouldShowHighlight ? ESlateVisibility::Visible : ESlateVisibility::Hidden);
			}
		}
		else if (ChestWidget->ExperienceText)
		{
			// 如果索引超出范围，显示默认值0
			ChestWidget->ExperienceText->SetText(FText::FromString(TEXT("0")));
			
			// 索引超出范围时隐藏高亮框
			if (ChestWidget->HighlightFrameImage)
			{
				ChestWidget->HighlightFrameImage->SetVisibility(ESlateVisibility::Hidden);
			}
		}
		
		ItemsScrollBox->AddChild(ChestWidget);

		// ⚠️ 2026-08-10: 设置 ScrollBox 子项 SizeBox 的 Padding (Top=30, Bottom=30, Right=10)
		// 页面单独 FixedPrizeWidget (右侧大宝箱) 不调用此方法, 布局由 WBP_DailyUpgradeRewardPage 控制
		// 大厂架构: ScrollBox 子项 vs 页面 FixedPrizeWidget 走不同路径, 互不干扰
		ChestWidget->SetPrizeSlotPadding(30.0f, 30.0f, 10.0f);

		// 更新DiamondIcon颜色
		ChestWidget->UpdateDiamondIconColor();

		// 更新ExperienceText颜色
		ChestWidget->UpdateExperienceTextColor();
	}
}

/**
 * @brief 更新经验值显示
 * @details 从UpgradeActivitySubsystem获取当前经验值并在界面上显示
 * 主要功能：
 * 1. 检查CurrentExpText控件是否存在
 * 2. 通过Subsystem获取玩家的当前经验值
 * 3. 将数值格式化后显示在文本控件上
 * @note 支持编辑器预览模式下的默认值显示
 */
void UDailyUpgradeRewardPage::UpdateExperienceDisplay()
{
	if (!CurrentExpText)
	{
		UE_LOG(LogTemp, Warning, TEXT("[UI_REFRESH_DEBUG] CurrentExpText is NULL!"));
		return;
	}

	// 检查是否为编辑器预览模式
	if (!GetWorld() || !GetWorld()->IsGameWorld())
	{
		return; // 静默返回，不输出日志
	}

	// 通过Subsystem获取当前经验值
	UGameInstance* GameInstance = GetGameInstance();
	if (!GameInstance)
	{
		CurrentExpText->SetText(FText::FromString(TEXT("0")));
		return;
	}

	UUpgradeActivitySubsystem* Sub = GameInstance->GetSubsystem<UUpgradeActivitySubsystem>();
	if (!Sub)
	{
		CurrentExpText->SetText(FText::FromString(TEXT("0")));
		return;
	}

	// 调用UpgradeActivitySubsystem类中的方法，查询UpgradeRewardSaveRecord动态表，
	// 获取最大RecordDate的CurrentExperience的值
	int32 CurrentExp = Sub->GetCurrentExperience();
	FString ExpText = FString::Printf(TEXT("%d"), CurrentExp);
	CurrentExpText->SetText(FText::FromString(ExpText));
	UE_LOG(LogTemp, Log, TEXT("[UI_REFRESH_DEBUG] UpdateCurrentExperienceText: 设置经验值为 %d"), CurrentExp);
	
}

/**
 * @brief UpdateDailyTasks - 重新构建每日任务按钮列表
 * @details 在 NativeConstruct 和 RefreshUI 步骤 6 中调用
 *
 * 流程 (10 步):
 *  1. 防御: 必须在游戏世界 + DayButtonsContainer + DailyTaskWidgetClass 都存在
 *  2. DayButtonsContainer->ClearChildren() (DayButtonsContainer 自身可以全清)
 *  3. TasksContainer 特殊清理 - 保留 BonusInfoBorder 和 BonusInfoText 两个特殊控件
 *     (因为它们是在蓝图中预设的, 不应该被动态清理)
 *  4. 清空 ButtonToDayIndexMap (防止旧映射悬挂)
 *  5. 拿 GameInstance / Subsystem
 *  6. 拿三组数据:
 *     - DayIdentifiers (从 GetDailyTaskDescriptions 拿 "day1"~"day7" 字符串数组)
 *     - HighlightStates (从 GetDailyTaskHighlightStates 拿 bool 数组)
 *     - LockStates (从 GetDailyTaskLockStates 拿 bool 数组)
 *  7. 循环创建 UDailyTaskWidget, AddChild 到 DayButtonsContainer
 *     - 设置 DayText / SelectionHighlightImage / LockIconImage
 *  8. 第二次循环绑定点击事件
 *     - 填充 ButtonToDayIndexMap
 *     - OnClicked.AddDynamic 绑到 HandleDayButtonClicked (无参包装器)
 *  9. 查 MaxRecordDate, 拼 "day{N}" 格式默认 DayIdentifier
 * 10. 调用 OnDayButtonClicked(DefaultDayIdentifier, DefaultDayIndex) 默认显示
 *
 * 业务逻辑下沉:
 *   HighlightStates / LockStates 完全由 Subsystem 计算, Page 只负责渲染
 *   这样新增"第 N 天"业务时, 只需改 Subsystem
 *
 * @note 调试日志很多 ([BONUS_DEBUG]), 调试完后可清理
 */
void UDailyUpgradeRewardPage::UpdateDailyTasks()
{
	// 确保在游戏世界中运行
	if (!GetWorld() || !GetWorld()->IsGameWorld())
	{
		return;
	}

	// 检查必要的组件是否存在
	if (!DayButtonsContainer || !DailyTaskWidgetClass)
	{
		return;
	}
	


	// 清空现有内容（保留BonusInfoBorder和BonusInfoText）
	DayButtonsContainer->ClearChildren();
	{
		UE_LOG(LogTemp, Log, TEXT("[BONUS_DEBUG] TasksContainer子控件数量: %d"), TasksContainer->GetChildrenCount());
		UE_LOG(LogTemp, Log, TEXT("[BONUS_DEBUG] BonusInfoBorder地址: %p"), BonusInfoBorder);
		UE_LOG(LogTemp, Log, TEXT("[BONUS_DEBUG] BonusInfoText地址: %p"), BonusInfoText);
		
		TArray<UWidget*> ChildrenToRemove;
		for (int32 i = 0; i < TasksContainer->GetChildrenCount(); ++i)
		{
			UWidget* Child = TasksContainer->GetChildAt(i);
			UE_LOG(LogTemp, Log, TEXT("[BONUS_DEBUG] 子控件[%d]地址: %p, 名称: %s"), i, Child, Child ? *Child->GetName() : TEXT("null"));
			
			// 只移除不是BonusInfoBorder且不是BonusInfoText的控件
			if (Child && Child != BonusInfoBorder && Child != BonusInfoText)
			{
				ChildrenToRemove.Add(Child);
				UE_LOG(LogTemp, Log, TEXT("[BONUS_DEBUG] 标记为删除: %s"), *Child->GetName());
			}
			else
			{
				UE_LOG(LogTemp, Log, TEXT("[BONUS_DEBUG] 保留控件: %s"), Child ? *Child->GetName() : TEXT("null"));
			}
		}
		
		UE_LOG(LogTemp, Log, TEXT("[BONUS_DEBUG] 需要删除的控件数量: %d"), ChildrenToRemove.Num());
		
		// 移除TaskDetailWidget和其他动态控件
		for (UWidget* WidgetToRemove : ChildrenToRemove)
		{
			TasksContainer->RemoveChild(WidgetToRemove);
		}
		
		UE_LOG(LogTemp, Log, TEXT("[BONUS_DEBUG] 清理后TasksContainer子控件数量: %d"), TasksContainer->GetChildrenCount());
	}
	
	// 清空按钮映射表以避免重复添加
	ButtonToDayIndexMap.Empty();

	// 获取Subsystem
	UGameInstance* GameInstance = GetGameInstance();
	if (!GameInstance)
	{
		return;
	}

	UUpgradeActivitySubsystem* Subsystem = GameInstance->GetSubsystem<UUpgradeActivitySubsystem>();
	if (!Subsystem)
	{
		return;
	}

	// 📥 调用 Subsystem 获取所有 DayIdentifier（day1-day7）
	TArray<FString> DayIdentifiers = Subsystem->GetDailyTaskDescriptions();
	if (DayIdentifiers.Num() == 0)
	{
		return;
	}
	
	
	// 📥 调用 Subsystem 获取每日任务高亮状态数组（业务逻辑完全在 Subsystem 中）
	TArray<bool> HighlightStates = Subsystem->GetDailyTaskHighlightStates();
	
	// 📥 调用Subsystem获取每日任务锁定状态数组（业务逻辑完全在Subsystem中）
	TArray<bool> LockStates = Subsystem->GetDailyTaskLockStates();

	// 🖼️ UI 层：根据 DayIdentifier 数组动态生成 DailyTaskWidget（天数按钮）
	for (int32 i = 0; i < DayIdentifiers.Num(); ++i)
	{
		UDailyTaskWidget* TaskWidget = CreateWidget<UDailyTaskWidget>(GetWorld(), DailyTaskWidgetClass);
		if (TaskWidget)
		{
			DayButtonsContainer->AddChild(TaskWidget);
			
			// 设置天数显示文本
			if (TaskWidget->DayText)
			{
				TaskWidget->DayText->SetText(FText::FromString(DayIdentifiers[i]));
			}
			
			// 设置高亮显示逻辑（使用Subsystem提供的业务逻辑结果）
			if (TaskWidget->SelectionHighlightImage)
			{
				// 直接使用Subsystem计算好的高亮状态
				bool bShouldHighlight = false;
				if (i < HighlightStates.Num())
				{
					bShouldHighlight = HighlightStates[i];
				}
				
				TaskWidget->SelectionHighlightImage->SetVisibility(
					bShouldHighlight ? ESlateVisibility::Visible : ESlateVisibility::Hidden);
				
			}
			
			// 设置锁定图标显示逻辑（使用Subsystem提供的业务逻辑结果）
			if (TaskWidget->LockIconImage)
			{
				// 直接使用Subsystem计算好的锁定状态
				bool bShouldLock = false;
				if (i < LockStates.Num())
				{
					bShouldLock = LockStates[i];
				}
				
				TaskWidget->LockIconImage->SetVisibility(
					bShouldLock ? ESlateVisibility::Visible : ESlateVisibility::Hidden);
				
			}
			
		}
	}
		
	// 🔧 核心业务逻辑：为每个 DayButton 绑定点击事件
	for (int32 i = 0; i < DayIdentifiers.Num(); ++i)
	{
		UDailyTaskWidget* TaskWidget = Cast<UDailyTaskWidget>(DayButtonsContainer->GetChildAt(i));
		if (TaskWidget && TaskWidget->DayButton)
		{
			FString CurrentDayIdentifier = DayIdentifiers[i];
			int32 CurrentIndex = i;
			
			// 存储映射关系用于后续查找
			ButtonToDayIndexMap.Add(TaskWidget->DayButton, CurrentIndex);
			
			// 绑定点击事件（使用 UFUNCTION 包装器）
			TaskWidget->DayButton->OnClicked.AddDynamic(this, &UDailyUpgradeRewardPage::HandleDayButtonClicked);

		}
	}

	// 🔧 【v215 修复】移除反模式: 不再强制重置 CurrentDayIndex = MaxRecordDate
	// 🐛 原 Bug: 用户停留在 day=3 任务页面 → 点 ClaimButton → RewardOptionWidget 弹窗 StoreBtn
	//   → 广播 OnGlobalRefresh → RefreshUI step 6 走 UpdateDailyTasks
	//   → 原 line 1014-1022 强制把 CurrentDayIndex 改回 MaxRecordDate
	//   → 用户看到"跳转到最新 day 的任务页面"
	// ✅ 正确职责分离:
	//   - UpdateDailyTasks: 只重建 DayButtonsContainer UI (清空 + 重建), 不改任何状态
	//   - TasksContainer 重建由 RefreshUI step 6.1 负责 (用 CurrentDayIndex 渲染)
	//   - 默认初始选中由 Initialize() 或首次 NativeConstruct 路径处理 (单次, 不在每次刷新里)
	//   - 重建 DayButtons 后, 用户手动点击 DayButton 来切换; 首次进入的默认选中
	//     应该在 NativeConstruct / Initialize 中显式调用一次 OnDayButtonClicked, 而不是在刷新里反复重置
}

/**
 * @brief 处理天数按钮点击的核心方法 - 显示某一天的任务详情
 * @param DayIdentifier 天数标识符 (如 "day1", "day3")
 * @param DayIndex 天数索引 (0-based)
 *
 * 流程 (12 步):
 *  1. 记录 CurrentDayIndex = DayIndex, CurrentSelectedDay = 数字部分
 *  2. 调用 RefreshDailyTaskHighlights() 刷新选中态
 *  3. DayButtonsContainer->InvalidateLayoutAndVolatility() 强制重绘
 *  4. 更新 BonusInfoText 和 BonusIconsContainer
 *  5. 防御: GameWorld / TasksContainer / TaskDetailWidgetClass
 *  6. 清理 TasksContainer 动态子节点 (保留 BonusInfoBorder/Text)
 *  7. 校验 GameInstance / Subsystem
 *  8. **核心业务**: 调用 Subsystem->HasDayDataInMemory(DayNumber) 判断是否有数据
 *     - 没数据: 创建 UDayLockHintWidget 锁屏提示, 初始化后 AddChild
 *     - 有数据: 继续
 *  9. 拿 ProcessedDescriptions (Subsystem 已处理过的任务文案)
 * 10. 循环创建 UTaskDetailWidget
 *     - 设置 TaskRequirementText
 *     - 从 ConfigRow / DayRecord 拿 CompleteCount vs RequiredCount
 *     - SetupClaimButton(DayIdentifier, i, CompleteCount, RequiredCount) - 子 Widget 状态机
 *     - SetupRewardsContainer(DayIdentifier, i) - 子 Widget 奖励展示
 *     - 隐藏 ClaimSuccessImage
 * 11. 末尾再次 RefreshDailyTaskHighlights + InvalidateLayout
 * 12. 日志分隔线结束
 *
 * @note 锁屏 vs 任务详情的分支: 这是"玩家尚未到达那一天"的核心入口
 *       HasDayDataInMemory 的判断依据: 存档中是否有 DayNumber 对应的 FUpgradeRewardSaveRecord
 */
void UDailyUpgradeRewardPage::OnDayButtonClicked(const FString& DayIdentifier, int32 DayIndex)
{
	
	// 存储当前选中的天数索引
	CurrentDayIndex = DayIndex;
		
	// 从DayIdentifier提取天数（如从"day1"提取1）
	int32 SelectedDayNumber = FCString::Atoi(*DayIdentifier.RightChop(3));
		
	// 存储当前选中的天数（用于临时高亮）
	CurrentSelectedDay = SelectedDayNumber;
	UE_LOG(LogTemp, Log, TEXT("[HIGHLIGHT_DEBUG] 设置CurrentSelectedDay为: %d"), CurrentSelectedDay);
		
// 刷新每日任务高亮状态（确保点击DayButton后SelectionHighlightImage跟随）
	RefreshDailyTaskHighlights();

	// 强制刷新UI
	if (DayButtonsContainer)
	{
		DayButtonsContainer->InvalidateLayoutAndVolatility();
	}

	// 更新限时加成信息文本 (一次性, 不要重复)
	UpdateBonusInfoText(DayIdentifier);

	// 更新限时加成图标容器
	UpdateBonusIconsContainer();

	// 确保在游戏世界中运行
	if (!GetWorld() || !GetWorld()->IsGameWorld())
	{
		return;
	}

	// 🔧【v219 重复调用清理】原代码在 1109 和 1121 重复调用 UpdateBonusInfoText/UpdateBonusIconsContainer
	// 大厂原则 DRY: 同一函数内同一调用不要出现 2 次. 已合并到上面一次.

	// 检查必要的组件是否存在
	if (!TasksContainer || !TaskDetailWidgetClass)
	{
		return;
	}

	// 清空现有内容（只清理动态创建的TaskDetailWidget，保留BonusInfoBorder和BonusInfoText）
	{
		TArray<UWidget*> ChildrenToRemove;
		for (int32 i = 0; i < TasksContainer->GetChildrenCount(); ++i)
		{
			UWidget* Child = TasksContainer->GetChildAt(i);
			// 只移除不是BonusInfoBorder且不是BonusInfoText的控件
			if (Child && Child != BonusInfoBorder && Child != BonusInfoText)
			{
				ChildrenToRemove.Add(Child);
			}
		}
		
		// 移除TaskDetailWidget和其他动态控件
		for (UWidget* WidgetToRemove : ChildrenToRemove)
		{
			TasksContainer->RemoveChild(WidgetToRemove);
		}
	}

	// 获取 Subsystem
	UGameInstance* GameInstance = GetGameInstance();
	if (!GameInstance)
	{
		return;
	}

	UUpgradeActivitySubsystem* Subsystem = GameInstance->GetSubsystem<UUpgradeActivitySubsystem>();
	if (!Subsystem)
	{
		return;
	}

	// 🔧 核心业务逻辑：使用 Subsystem 检查内存中是否有该天数的数据
	int32 CheckDayNumber = FCString::Atoi(*DayIdentifier.RightChop(3)); // 从 "day1" 提取数字 1
	bool bHasDayData = Subsystem->HasDayDataInMemory(CheckDayNumber);
	
	if (!bHasDayData)
	{
		
		if (DayLockHintWidgetClass && TasksContainer)
		{
			UDayLockHintWidget* LockHintWidget = CreateWidget<UDayLockHintWidget>(GetWorld(), DayLockHintWidgetClass);
			if (LockHintWidget)
			{
				TasksContainer->AddChild(LockHintWidget);
				
				// 初始化锁定提示Widget（包含奖励图标生成）
				LockHintWidget->InitializeWidget(DayIdentifier);
			}
		}	
		return;
	}
	
	// 📥 调用 Subsystem 的业务逻辑方法获取指定天的处理后任务描述
	TArray<FString> ProcessedDescriptions = Subsystem->GetProcessedTaskDescriptionsForDay(DayIdentifier);
	if (ProcessedDescriptions.Num() == 0)
	{
		return;
	}

	// 🖼️ UI 层：根据 TaskDescriptions 数组动态生成 TaskDetailWidget
	for (int32 i = 0; i < ProcessedDescriptions.Num(); ++i)
	{
		UTaskDetailWidget* TaskDetailWidget = CreateWidget<UTaskDetailWidget>(GetWorld(), TaskDetailWidgetClass);
		if (TaskDetailWidget)
		{
			TasksContainer->AddChild(TaskDetailWidget);
			
			// 设置任务需求说明文本（使用 Subsystem 处理后的结果）
			if (TaskDetailWidget->TaskRequirementText)
			{
				TaskDetailWidget->TaskRequirementText->SetText(FText::FromString(ProcessedDescriptions[i]));
				UE_LOG(LogTemp, Log, TEXT("🖼️ DailyUpgradeRewardPage: 设置第%d个 TaskDetailWidget 的 TaskRequirementText 为：%s"), i + 1, *ProcessedDescriptions[i]);
			}
			
			// 🔧 核心业务逻辑：设置领取按钮状态
			// 获取当前天的配置数据用于比较
			const FDailyUpgradeRewardConfigRow* ConfigRow = Subsystem->GetConfigRowForDay(DayIdentifier);
			
			// 从指定天数的记录中获取任务完成数量
			int32 DayNumber = FCString::Atoi(*DayIdentifier.RightChop(3)); // 从 "day1" 提取数字 1
			const FUpgradeRewardSaveRecord* DayRecord = Subsystem->GetRecordByDate(DayNumber);
			
			if (ConfigRow && i < ConfigRow->TaskRelatedValues.Num() && DayRecord && i < DayRecord->TaskCompleteCounts.Num())
			{
				int32 CompleteCount = DayRecord->TaskCompleteCounts[i];
				int32 RequiredCount = ConfigRow->TaskRelatedValues[i];
				
				// 🔧 调试日志：确认从指定天数获取的任务完成数量
				UE_LOG(LogTemp, Log, TEXT("🔍 DailyUpgradeRewardPage: 从天数 %s 获取任务%d的完成数量: %d"), *DayIdentifier, i + 1, CompleteCount);
				
				// 调用 TaskDetailWidget 的方法设置按钮状态
				TaskDetailWidget->SetupClaimButton(DayIdentifier, i, CompleteCount, RequiredCount);
				
				// 🔧 核心业务逻辑：设置奖励展示容器
				TaskDetailWidget->SetupRewardsContainer(DayIdentifier, i);
				
				UE_LOG(LogTemp, Log, TEXT("🔧 DailyUpgradeRewardPage: 设置第%d个 TaskDetailWidget 的 ClaimButton - Complete:%d, Required:%d"), 
					i + 1, CompleteCount, RequiredCount);
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("🔧 DailyUpgradeRewardPage: 无法获取第%d个任务的配置数据或索引越界"), i + 1);
			}
			
			// 隐藏领取成功图标（默认状态）
			if (TaskDetailWidget->ClaimSuccessImage)
			{
				TaskDetailWidget->ClaimSuccessImage->SetVisibility(ESlateVisibility::Hidden);
			}
			
			UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: 成功创建第%d个 TaskDetailWidget"), i + 1);
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("DailyUpgradeRewardPage: 创建第%d个 TaskDetailWidget 失败"), i + 1);
		}
	}

	UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: TasksContainer 初始化完成，共创建%d个 TaskDetailWidget"), ProcessedDescriptions.Num());
	
	// 刷新每日任务高亮状态（确保点击DayButton后SelectionHighlightImage跟随）
	RefreshDailyTaskHighlights();
	
	// 强制刷新UI
	if (DayButtonsContainer)
	{
		DayButtonsContainer->InvalidateLayoutAndVolatility();
	}
	
	UE_LOG(LogTemp, Log, TEXT("===========================================================\n"));
}



/**
 * @brief 刷新整个页面 UI
 * @details 重新初始化所有UI组件，确保显示与数据同步
 * 主要功能：
 * 1. 重新初始化经验宝箱控件列表
 * 2. 更新宝箱数量显示
 * 3. 更新经验值显示
 * @note 通常在数据发生变化后调用此方法
 */
void UDailyUpgradeRewardPage::RefreshUI()
{
	// 🆔 自我身份核对 - 显示页面唯一标识
	FString PageIdentity = GetPageIdentity();
	UE_LOG(LogTemp, Log, TEXT("\n==========================================================="));
	UE_LOG(LogTemp, Log, TEXT("[UI_REFRESH_DEBUG] 🔄 DAILY_UPGRADE_REWARD_PAGE_REFRESH_START"));
	UE_LOG(LogTemp, Log, TEXT("[UI_REFRESH_DEBUG] 🆔 页面身份: %s"), *PageIdentity);
	UE_LOG(LogTemp, Log, TEXT("[UI_REFRESH_DEBUG] 📍 刷新时页面地址: %p"), this);
	UE_LOG(LogTemp, Log, TEXT("[UI_REFRESH_DEBUG] ⏰ 刷新时间: %s"), *FDateTime::Now().ToString());
	UE_LOG(LogTemp, Log, TEXT("[UI_REFRESH_DEBUG] 📅 当前选中天数索引: %d"), CurrentDayIndex);
	UE_LOG(LogTemp, Log, TEXT("===========================================================\n"));
	
	// 执行全量刷新：重新初始化所有核心UI组件
	
	// 1. 重新初始化奖励物品图标缓存（最重要的数据源）
	UE_LOG(LogTemp, Log, TEXT("[步骤1/5] 🎯 初始化奖励物品图标缓存..."));
	InitializeRewardItemIcons();
	UE_LOG(LogTemp, Log, TEXT("✅ 奖励物品图标缓存已刷新 (缓存数量: %d)"), CachedItemIcons.Num());
	
	// 2. 更新现有经验宝箱控件状态（不重新创建）
	UE_LOG(LogTemp, Log, TEXT("\n[步骤2/7] 📦 更新经验宝箱控件状态..."));
	UpdateExperienceChestWidgetsState();
	int32 ChestCount = ItemsScrollBox ? ItemsScrollBox->GetChildrenCount() : 0;
	UE_LOG(LogTemp, Log, TEXT("✅ 经验宝箱控件状态已更新 (控件数量: %d)"), ChestCount);
	
	// 2.1 额外刷新所有进度条显示
	UE_LOG(LogTemp, Log, TEXT("\n[步骤2.1/7] 📊 刷新经验进度条显示..."));
	RefreshAllProgressBars();
	UE_LOG(LogTemp, Log, TEXT("✅ 经验进度条已刷新"));
	
	// 2.2 根据当前经验值居中显示相关内容
	UE_LOG(LogTemp, Log, TEXT("\n[步骤2.2/7] 🎯 根据经验值居中显示内容..."));
	// 【v228 Bug 3 修】领取宝箱后不应滚动, 仅页面刷新时滚动
	//   HandleChestClaimRequest 在领取后 Set bSuppressNextCenterScroll=true, 此处跳过一次
	if (bSuppressNextCenterScroll)
	{
		bSuppressNextCenterScroll = false;
		UE_LOG(LogTemp, Log, TEXT("⏭️ 跳过本次居中 (领取后)"));
	}
	else
	{
		CenterScrollBoxOnCurrentExperience();
		UE_LOG(LogTemp, Log, TEXT("✅ ScrollBox已根据经验值居中定位"));
	}
	
	// 3. 更新宝箱数量显示
	UE_LOG(LogTemp, Log, TEXT("\n[步骤3/7] 📊 更新宝箱数量显示..."));
	UpdateChestCountText();
	UE_LOG(LogTemp, Log, TEXT("✅ 宝箱数量文本已刷新"));
	
	// 4. 更新经验值显示
	UE_LOG(LogTemp, Log, TEXT("\n[步骤4/7] ⭐ 更新经验值显示..."));
	UpdateExperienceDisplay();
	UE_LOG(LogTemp, Log, TEXT("✅ 经验值显示已刷新"));
	
	// 5. 更新奖励物品图像显示（基于最新的缓存数据）
	UE_LOG(LogTemp, Log, TEXT("\n[步骤5/7] 🖼️ 更新奖励物品图像显示..."));
	UpdateRewardItemImage();
	UE_LOG(LogTemp, Log, TEXT("✅ 奖励物品图像已刷新"));
	
	// 6. 更新每日任务列表和高亮状态
	UE_LOG(LogTemp, Log, TEXT("\n[步骤6/7] 📅 更新每日任务列表和高亮状态..."));
	UpdateDailyTasks();
	UE_LOG(LogTemp, Log, TEXT("✅ 每日任务列表和高亮状态已刷新"));
	
	// 6.1 更新限时加成图标容器
	UE_LOG(LogTemp, Log, TEXT("\n[步骤6.1/7] 🎁 更新限时加成图标容器..."));
	UpdateBonusIconsContainer();
	UE_LOG(LogTemp, Log, TEXT("✅ 限时加成图标容器已刷新"));
	
	// 6.1 如果当前有选中的天数，显示该天数的任务详情
	if (CurrentDayIndex != -1)
	{
		FString SelectedDayIdentifier = FString::Printf(TEXT("day%d"), CurrentDayIndex + 1);
		UE_LOG(LogTemp, Log, TEXT("🔄 刷新UI时显示天数 %s 的任务详情，索引: %d"), *SelectedDayIdentifier, CurrentDayIndex);
		OnDayButtonClicked(SelectedDayIdentifier, CurrentDayIndex);
	}
	
	// 7. 更新固定奖励控件状态
	UE_LOG(LogTemp, Log, TEXT("\n[步骤7/7] 🎁 更新固定奖励控件状态..."));
	UpdateFixedPrizeWidget();
	UE_LOG(LogTemp, Log, TEXT("✅ 固定奖励控件状态已刷新"));
	
	// 🎉 刷新完成总结
	UE_LOG(LogTemp, Log, TEXT("\n==========================================================="));
	UE_LOG(LogTemp, Log, TEXT("🎉 DAILY_UPGRADE_REWARD_PAGE_REFRESH_COMPLETE"));
	UE_LOG(LogTemp, Log, TEXT("🆔 页面身份: %s"), *PageIdentity);
	UE_LOG(LogTemp, Log, TEXT("📊 最终状态: 宝箱控件数=%d, 图标缓存数=%d"), ChestCount, CachedItemIcons.Num());
	UE_LOG(LogTemp, Log, TEXT("===========================================================\n"));
}

/**
 * @brief 处理天数按钮点击事件（无参包装器）
 */
void UDailyUpgradeRewardPage::HandleDayButtonClicked()
{
	UE_LOG(LogTemp, Log, TEXT("[HIGHLIGHT_DEBUG] HandleDayButtonClicked 被调用"));
	
	// 🔧 安全检查：确保 ButtonToDayIndexMap 不为空
	if (ButtonToDayIndexMap.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("⚠️ HandleDayButtonClicked: ButtonToDayIndexMap 为空"));
		return;
	}
	
	UE_LOG(LogTemp, Log, TEXT("[HIGHLIGHT_DEBUG] ButtonToDayIndexMap 数量: %d"), ButtonToDayIndexMap.Num());
	
	// 遍历 ButtonToDayIndexMap 找到被点击的按钮
	for (auto& Pair : ButtonToDayIndexMap)
	{
		// 🔧 安全检查：确保按钮有效且未被销毁
		if (!Pair.Key || !Pair.Key->IsValidLowLevel())
		{
			continue;
		}
		
		UE_LOG(LogTemp, Log, TEXT("[HIGHLIGHT_DEBUG] 检查按钮是否悬停: %s"), Pair.Key->IsHovered() ? TEXT("是") : TEXT("否"));
		
		if (Pair.Key->IsHovered())
		{
			int32 DayIndex = Pair.Value;
			FString DayIdentifier = FString::Printf(TEXT("day%d"), DayIndex + 1);
			
			UE_LOG(LogTemp, Log, TEXT("🖱️ HandleDayButtonClicked: 检测到按钮 %s (索引:%d) 被点击"), *DayIdentifier, DayIndex);
			
			// 调用实际的处理方法
			OnDayButtonClicked(DayIdentifier, DayIndex);
			return;
		}
	}
	
	UE_LOG(LogTemp, Warning, TEXT("[HIGHLIGHT_DEBUG] 未找到悬停的按钮！"));
}


/**
 * @brief 处理"重选奖励"按钮的点击事件 - 弹出奖励选项确认弹窗
 * @details 当玩家点击 ReselectRewardButton 时触发
 *
 * 业务流程:
 * 1. 防御链: GameInstance -> UpgradeActivitySubsystem -> 配置数据 -> 弹窗类
 * 2. 调用 Subsystem 的 GetReselectRewardOptions 获取所有可选的奖励物品 TSoftObjectPtr<UTexture2D>
 * 3. 校验 ActivityConfirmPopupWidgetClass 是否已绑定 (蓝图端 WBP_ActivityConfirmPopupWidget)
 * 4. 通过 CreateWidget 创建 UActivityConfirmPopupWidget 实例
 * 5. 构造一份 FDailyLoginConfigRow 数据 (ActivityID / RewardItemID 来自 Config)
 * 6. 调用 InitializePopup(PopupOptions, 0) 初始化弹窗 (0 表示默认选中第一项)
 * 7. AddToViewport(1000) - 用高 ZOrder 确保弹窗始终在最上层
 *
 * 数据格式转换说明:
 *   UActivityConfirmPopupWidget 期望 FDailyLoginConfigRow 格式
 *   但本类业务配置为 FDailyUpgradeRewardConfigRow
 *   所以这里需要做一次"桥接": 用后者的 ActivityID + 最后一个 RewardItemID 拼一个临时的 FDailyLoginConfigRow
 *
 * 关联:
 * - 上游: ReselectRewardButton->OnClicked (Initialize 中绑定)
 * - 下游: UActivityConfirmPopupWidget->InitializePopup
 * - 事件: 用户在弹窗内选择 → UpdateRewardIconIndexAndSave → OnRewardIconIndexChanged 广播
 */
void UDailyUpgradeRewardPage::OnReselectRewardClicked()
{
	UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: 重选奖励按钮被点击"));
	
	// 通过Subsystem获取重选奖励选项数据
	UGameInstance* GameInstance = GetGameInstance();
	if (!GameInstance)
	{
		UE_LOG(LogTemp, Error, TEXT("DailyUpgradeRewardPage: 无法获取GameInstance"));
		return;
	}
	
	UUpgradeActivitySubsystem* UpgradeSub = GameInstance->GetSubsystem<UUpgradeActivitySubsystem>();
	if (!UpgradeSub)
	{
		UE_LOG(LogTemp, Error, TEXT("DailyUpgradeRewardPage: 无法获取UpgradeActivitySubsystem"));
		return;
	}
	
	TArray<TSoftObjectPtr<UTexture2D>> RewardOptions = UpgradeSub->GetReselectRewardOptions();
	if (RewardOptions.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("DailyUpgradeRewardPage: 没有可重选的奖励选项"));
		return;
	}
	
	UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: 准备弹出确认页面，奖励选项数量: %d"), RewardOptions.Num());
	
	// 检查ActivityConfirmPopupWidgetClass是否已设置
	if (!ActivityConfirmPopupWidgetClass)
	{
		UE_LOG(LogTemp, Error, TEXT("DailyUpgradeRewardPage: ActivityConfirmPopupWidgetClass未设置！请在蓝图中指定WBP_ActivityConfirmPopupWidget类"));
		return;
	}
	
	// 创建ActivityConfirmPopupWidget实例
	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogTemp, Error, TEXT("DailyUpgradeRewardPage: 无法获取世界上下文"));
		return;
	}
	
	UActivityConfirmPopupWidget* ConfirmPopup = CreateWidget<UActivityConfirmPopupWidget>(World, ActivityConfirmPopupWidgetClass);
	if (!ConfirmPopup)
	{
		UE_LOG(LogTemp, Error, TEXT("DailyUpgradeRewardPage: 创建ActivityConfirmPopupWidget失败"));
		return;
	}
	
	UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: 成功创建ActivityConfirmPopupWidget实例"));
	
	// 获取ActivityID==110的配置数据用于初始化弹窗
	const FDailyUpgradeRewardConfigRow* Config = UpgradeSub->GetActivityConfig();
	if (!Config)
	{
		UE_LOG(LogTemp, Error, TEXT("DailyUpgradeRewardPage: 无法获取活动配置数据"));
		return;
	}
	
	// 转换为DailyLoginConfigRow格式（因为ActivityConfirmPopupWidget期望这种格式）
	TArray<FDailyLoginConfigRow> PopupOptions;
	FDailyLoginConfigRow TempRow;
	TempRow.ActivityID = Config->ActivityID;
	
	// 使用最后一个RewardItemID作为RewardItemID
	if (Config->RewardItemIDs.Num() > 0)
	{
		FString LastRewardItemID = Config->RewardItemIDs.Last();
		TempRow.RewardItemID = FCString::Atoi(*LastRewardItemID);
		UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: 使用RewardItemID: %d"), TempRow.RewardItemID);
	}
	
	PopupOptions.Add(TempRow);
	
	// 初始化弹窗
	ConfirmPopup->InitializePopup(PopupOptions, 0);
	
	// 添加到视口显示
	ConfirmPopup->AddToViewport(1000); // 使用高Z-order确保显示在最上层
	
	UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: ActivityConfirmPopupWidget已添加到视口显示"));
	
	// 记录奖励选项信息
	for (int32 i = 0; i < RewardOptions.Num(); ++i)
	{
		UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: 奖励选项 %d: %s"), i + 1, 
			RewardOptions[i].IsNull() ? TEXT("空") : *RewardOptions[i].ToString());
	}
}

/**
 * @brief 订阅 UpgradeActivitySubsystem 提供的两个核心事件
 * @details 在 NativeConstruct 中调用, 完成本页面与 Subsystem 的事件桥接
 *
 * 订阅的事件:
 * 1. OnRewardIconIndexChanged(int32 NewIndex)
 *    - 触发时机: 玩家在 ActivityConfirmPopupWidget 选中新的奖励图标, Subsystem 完成存档写入后
 *    - 本类处理: OnRewardIconIndexChanged → 重新拉取图标缓存 + 刷新 RewardItemImage
 *
 * 2. OnGlobalRefresh()
 *    - 触发时机: 任意对 Subsystem 数据有影响的操作 (HandleRewardStore 领取宝箱 / SaveModifier 控制台命令 / 跨天刷新)
 *    - 本类处理: RefreshUI → 走全量 7 步刷新流程
 *
 * 防御性设计:
 * - 先 RemoveDynamic 再 AddDynamic, 防止 NativeConstruct 多次触发 (蓝图重载 / 重新打开页面) 导致重复绑定
 * - 强校验 GameInstance / Subsystem 是否有效, 失败时打 Error 日志直接返回
 * - 末尾日志记录 Subsystem 与 Page 的指针地址, 便于排查多实例共存问题
 *
 * 注意:
 *   对称的反订阅函数 UnsubscribeFromSubsystemEvents() 在 NativeDestruct 中**不调用**
 *   故意保留订阅, 让被缓存的页面也能继续响应全局刷新广播
 */
void UDailyUpgradeRewardPage::SubscribeToSubsystemEvents()
{
	// 通过GameInstance获取UpgradeActivitySubsystem
	UGameInstance* GameInstance = GetGameInstance();
	if (!GameInstance)
	{
		UE_LOG(LogTemp, Error, TEXT("DailyUpgradeRewardPage: 无法获取GameInstance"));
		return;
	}
	
	UUpgradeActivitySubsystem* UpgradeSub = GameInstance->GetSubsystem<UUpgradeActivitySubsystem>();
	if (!UpgradeSub)
	{
		UE_LOG(LogTemp, Error, TEXT("DailyUpgradeRewardPage: 无法获取UpgradeActivitySubsystem"));
		return;
	}
	
	// 记录Subsystem地址用于调试
	UE_LOG(LogTemp, Log, TEXT("🔗 Subsystem地址绑定: %p -> Page地址: %p"), UpgradeSub, this);
	
	// 先解绑已存在的事件绑定，防止重复绑定（因为NativeConstruct会多次触发）
	UpgradeSub->OnRewardIconIndexChanged.RemoveDynamic(this, &UDailyUpgradeRewardPage::OnRewardIconIndexChanged);
	UpgradeSub->OnGlobalRefresh.RemoveDynamic(this, &UDailyUpgradeRewardPage::RefreshUI);
	
	// 订阅奖励图标索引更新事件
	UpgradeSub->OnRewardIconIndexChanged.AddDynamic(this, &UDailyUpgradeRewardPage::OnRewardIconIndexChanged);
	
	// 订阅全局刷新事件
	UpgradeSub->OnGlobalRefresh.AddDynamic(this, &UDailyUpgradeRewardPage::RefreshUI);
	
	UE_LOG(LogTemp, Log, TEXT("✅ 事件绑定完成:"));
	UE_LOG(LogTemp, Log, TEXT("   OnRewardIconIndexChanged: %s"), UpgradeSub->OnRewardIconIndexChanged.IsBound() ? TEXT("✅ 已绑定") : TEXT("❌ 未绑定"));
	UE_LOG(LogTemp, Log, TEXT("   OnGlobalRefresh: %s"), UpgradeSub->OnGlobalRefresh.IsBound() ? TEXT("✅ 已绑定") : TEXT("❌ 未绑定"));
	UE_LOG(LogTemp, Log, TEXT("🔗 绑定关系: Subsystem[%p] <-> Page[%p]"), UpgradeSub, this);
	
}

/**
 * @brief 取消订阅 Subsystem 事件
 * @details 与 SubscribeToSubsystemEvents 配对使用
 *
 * 当前调用方:
 * - **未被 NativeDestruct 调用** (有意保留, 让缓存页继续接收广播)
 * - 仅在外部强制调用时使用 (例如 CheatWidget 主动关闭页面)
 *
 * 取消的委托:
 * - OnRewardIconIndexChanged
 * - OnGlobalRefresh
 *
 * @note 与订阅时一致, 必须使用 RemoveDynamic 显式解绑, 不能依赖 UObject 析构
 */
void UDailyUpgradeRewardPage::UnsubscribeFromSubsystemEvents()
{
	// 通过GameInstance获取UpgradeActivitySubsystem
	UGameInstance* GameInstance = GetGameInstance();
	if (!GameInstance)
	{
		return;
	}
	
	UUpgradeActivitySubsystem* UpgradeSub = GameInstance->GetSubsystem<UUpgradeActivitySubsystem>();
	if (!UpgradeSub)
	{
		return;
	}
	
	// 取消订阅奖励图标索引更新事件
	UpgradeSub->OnRewardIconIndexChanged.RemoveDynamic(this, &UDailyUpgradeRewardPage::OnRewardIconIndexChanged);
	
	// 取消订阅全局刷新事件
	UpgradeSub->OnGlobalRefresh.RemoveDynamic(this, &UDailyUpgradeRewardPage::RefreshUI);
	
	UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: 已取消订阅Subsystem事件"));
}

/**
 * @brief OnRewardIconIndexChanged 事件回调 - 玩家重选奖励图标后被广播
 * @param NewIndex Subsystem 中新写入的奖励图标索引
 *
 * 处理流程:
 * 1. 日志记录新索引
 * 2. 调用 InitializeRewardItemIcons() 重新拉取 CachedItemIcons (可能图标列表未变, 但确保缓存与 Subsystem 一致)
 * 3. 调用 UpdateRewardItemImage() 让 RewardItemImage 显示新图标
 *
 * @note 这里**不**调用 RefreshUI 全部刷新, 因为只有"奖励图标"一项变更, 没必要重走 7 步
 */
void UDailyUpgradeRewardPage::OnRewardIconIndexChanged(int32 NewIndex)
{
	UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: 接收到奖励图标索引更新事件，新索引: %d"), NewIndex);

	// 重新初始化奖励图标数据
	InitializeRewardItemIcons();

	// 更新奖励物品图像显示
	UpdateRewardItemImage();

	// 🔧【v218 SSOT 联动】popup 选中 WBP_RewardOptionCardWidget → 同步 ChestCountText
	UpdateChestCountText();

	UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: 奖励图标索引更新完成"));
}

/**
 * @brief 手动触发全量 UI 刷新
 * @details 给蓝图 / CheatWidget 调用的"一键刷新"入口
 *
 * 内部实现: 直接调用 RefreshUI()
 * 日志: 输出当前 Page 地址, 便于多实例调试
 */

// 【vXXX 大厂修复】将按钮绑定逻辑抽取为独立方法
// 原因: Initialize() 只调用一次，页面从缓存激活时不会重新调用
// 解决: 在 NativeConstruct() 中调用此方法，确保每次激活都重新绑定
void UDailyUpgradeRewardPage::BindReselectRewardButton()
{
	if (ReselectRewardButton)
	{
		// 先解绑，防止重复绑定
		ReselectRewardButton->OnClicked.RemoveDynamic(this, &UDailyUpgradeRewardPage::OnReselectRewardClicked);
		// 重新绑定
		ReselectRewardButton->OnClicked.AddDynamic(this, &UDailyUpgradeRewardPage::OnReselectRewardClicked);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("DailyUpgradeRewardPage: ReselectRewardButton 未绑定"));
	}
}

/**
 * @brief 绑定调试按钮事件 (在 NativeConstruct 调用, 防止页面缓存激活时事件失效)
 * 【vXXX.1 大厂修复】模仿 BindReselectRewardButton 模式
 * - Button_ApplyDebugValues: 提交体验值/任务次数
 * - Button_ResetAllActivity: 一键重置活动
 * - ComboBoxString_SelectedDay: 调试天数选择
 * 大厂原则: 先解绑再绑定, 防止重复绑定导致事件不触发
 */
void UDailyUpgradeRewardPage::BindDebugButtons()
{
	if (Button_ApplyDebugValues)
	{
		// 先解绑, 防止重复绑定
		Button_ApplyDebugValues->OnClicked.RemoveDynamic(this, &UDailyUpgradeRewardPage::OnApplyDebugValuesClicked);
		Button_ApplyDebugValues->OnClicked.AddDynamic(this, &UDailyUpgradeRewardPage::OnApplyDebugValuesClicked);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("DailyUpgradeRewardPage: Button_ApplyDebugValues 未绑定"));
	}

	if (Button_ResetAllActivity)
	{
		Button_ResetAllActivity->OnClicked.RemoveDynamic(this, &UDailyUpgradeRewardPage::OnResetAllActivityClicked);
		Button_ResetAllActivity->OnClicked.AddDynamic(this, &UDailyUpgradeRewardPage::OnResetAllActivityClicked);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("DailyUpgradeRewardPage: Button_ResetAllActivity 未绑定"));
	}

	if (ComboBoxString_SelectedDay)
	{
		// 先解绑, 防止重复绑定
		ComboBoxString_SelectedDay->OnSelectionChanged.RemoveDynamic(this, &UDailyUpgradeRewardPage::OnDebugDayComboBoxSelectionChanged);
		ComboBoxString_SelectedDay->OnSelectionChanged.AddDynamic(this, &UDailyUpgradeRewardPage::OnDebugDayComboBoxSelectionChanged);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("DailyUpgradeRewardPage: ComboBoxString_SelectedDay 未绑定"));
	}
}


void UDailyUpgradeRewardPage::ManualRefreshUI()
{
	UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: 手动刷新UI被调用 - 页面地址=%p"), this);
	RefreshUI();
}

/**
 * @brief 刷新每日任务按钮的高亮状态
 * @details 在两种情况下被调用:
 *  1. OnDayButtonClicked 之后 (用户点击新的一天)
 *  2. UpdateDailyTasks 之后 (重新生成所有 DailyTaskWidget)
 *
 * 高亮逻辑 (双轨叠加):
 *  - 自动高亮: 由 Subsystem 提供的 HighlightStates (基于存档中 MaxRecordDate 计算)
 *  - 临时高亮: 用户当前点击的 CurrentSelectedDay (优先级更高, 覆盖自动高亮)
 *
 * 关键点:
 *  - 用户点击的天数索引 = CurrentSelectedDay - 1
 *  - 当 CurrentSelectedDay > 0 时, 临时高亮完全替代自动高亮 (只点亮被点击的那一天)
 *  - 当 CurrentSelectedDay == 0 时 (默认值, 未点击), 使用 Subsystem 的自动高亮
 *
 * 防御: 校验 DayButtonsContainer / GameInstance / Subsystem 全部有效
 */
void UDailyUpgradeRewardPage::RefreshDailyTaskHighlights()
{
	UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: 刷新每日任务高亮状态"));
	UE_LOG(LogTemp, Log, TEXT("[HIGHLIGHT_DEBUG] RefreshDailyTaskHighlights 被调用, CurrentSelectedDay: %d"), CurrentSelectedDay);
	
	// 检查必要组件
	if (!DayButtonsContainer)
	{
		UE_LOG(LogTemp, Warning, TEXT("DailyUpgradeRewardPage: DayButtonsContainer未设置"));
		return;
	}
	
	// 获取Subsystem
	UGameInstance* GameInstance = GetGameInstance();
	if (!GameInstance)
	{
		UE_LOG(LogTemp, Error, TEXT("DailyUpgradeRewardPage: 无法获取GameInstance"));
		return;
	}
	
	UUpgradeActivitySubsystem* Subsystem = GameInstance->GetSubsystem<UUpgradeActivitySubsystem>();
	if (!Subsystem)
	{
		UE_LOG(LogTemp, Error, TEXT("DailyUpgradeRewardPage: 无法获取UpgradeActivitySubsystem"));
		return;
	}
	
	// 📥 调用Subsystem获取高亮状态数组（业务逻辑完全在Subsystem中）
	TArray<bool> HighlightStates = Subsystem->GetDailyTaskHighlightStates();
	UE_LOG(LogTemp, Log, TEXT("📥 DailyUpgradeRewardPage: 从Subsystem获取到%d个高亮状态用于刷新（业务逻辑已下沉）"), HighlightStates.Num());
	
	// 遍历所有DailyTaskWidget并更新高亮状态
	for (int32 i = 0; i < DayButtonsContainer->GetChildrenCount(); ++i)
	{
		UWidget* ChildWidget = DayButtonsContainer->GetChildAt(i);
		UDailyTaskWidget* TaskWidget = Cast<UDailyTaskWidget>(ChildWidget);
		
		if (TaskWidget && TaskWidget->SelectionHighlightImage)
		{
			// 结合自动高亮和临时高亮逻辑
			bool bShouldHighlight = false;
			
			// 自动高亮：基于RecordDate最大值
			if (i < HighlightStates.Num())
			{
				bShouldHighlight = HighlightStates[i];
			}
			
			// 临时高亮：用户点击的天数（优先级更高）
			if (CurrentSelectedDay > 0 && CurrentSelectedDay <= 7)
			{
				UE_LOG(LogTemp, Log, TEXT("[HIGHLIGHT_DEBUG] 当前选中天数: %d, 当前索引: %d"), CurrentSelectedDay, i);
				// 用户点击的天数对应索引是 CurrentSelectedDay - 1
				int32 SelectedIndex = CurrentSelectedDay - 1;
				if (i == SelectedIndex)
				{
					bShouldHighlight = true; // 临时高亮覆盖自动高亮
					UE_LOG(LogTemp, Log, TEXT("[HIGHLIGHT_DEBUG] 高亮索引 %d"), i);
				}
				else
				{
					bShouldHighlight = false; // 其他天数不显示高亮
					UE_LOG(LogTemp, Log, TEXT("[HIGHLIGHT_DEBUG] 隐藏索引 %d"), i);
				}
			}
			
			// 设置高亮显示状态
			TaskWidget->SelectionHighlightImage->SetVisibility(
				bShouldHighlight ? ESlateVisibility::Visible : ESlateVisibility::Hidden);
			
			UE_LOG(LogTemp, Log, TEXT("🖼️ DailyUpgradeRewardPage: 刷新第%d个任务Widget高亮状态: %s (使用Subsystem业务逻辑结果)"), 
				i + 1, bShouldHighlight ? TEXT("显示✅") : TEXT("隐藏❌"));
		}
	}
	
	UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: 每日任务高亮状态刷新完成"));
}



FString UDailyUpgradeRewardPage::GetPageIdentity() const
{
	// 生成页面唯一身份标识
	FString AddressStr = FString::Printf(TEXT("0x%p"), this);
	FString TimestampStr = FDateTime::Now().ToString(TEXT("MMdd-HHmmss"));
	
	// 返回格式化的身份字符串
	return FString::Printf(TEXT("Page[%s]@%s"), *AddressStr, *TimestampStr);
}

// ==================== 事件处理函数 ====================

/**
 * @brief 处理宝箱领取请求 - 由 ExperienceChestClaimWidget 通过 OnChestClaimRequested 委托广播
 * @param ChestIndex 玩家点击的宝箱索引
 *
 * 流程:
 * 1. 验证 Subsystem 可用
 * 2. 调用 ShowRewardOptionWidget(ChestIndex) 弹出奖励选择弹窗
 * 3. **不在这里直接修改 Subsystem 数据** - 数据更新由 RewardOptionWidget 的 StoreBtn 点击后通过 HandleRewardStore 完成
 *
 * 这种"两阶段提交"的设计:
 *  - 玩家点宝箱 → 看到奖励选项 → 玩家在弹窗中再次确认 → 才写入存档
 *  - 防止误点击造成奖励直接发放
 */
void UDailyUpgradeRewardPage::HandleChestClaimRequest(int32 ChestIndex)
{
	UE_LOG(LogTemp, Log, TEXT("=========================================="));
	UE_LOG(LogTemp, Log, TEXT("🎉 DailyUpgradeRewardPage: 接收到宝箱领取请求！"));
	UE_LOG(LogTemp, Log, TEXT("📦 宝箱索引: %d"), ChestIndex);
	UE_LOG(LogTemp, Log, TEXT("📍 页面地址: %p"), this);
	UE_LOG(LogTemp, Log, TEXT("=========================================="));
	
	// 获取Subsystem
	UGameInstance* GameInstance = GetGameInstance();
	if (!GameInstance)
	{
		UE_LOG(LogTemp, Error, TEXT("DailyUpgradeRewardPage: 无法获取GameInstance"));
		return;
	}
	
	UUpgradeActivitySubsystem* Subsystem = GameInstance->GetSubsystem<UUpgradeActivitySubsystem>();
	if (!Subsystem)
	{
		UE_LOG(LogTemp, Error, TEXT("DailyUpgradeRewardPage: 无法获取UpgradeActivitySubsystem"));
		return;
	}
	
	// 弹出RewardOptionWidget页面
	ShowRewardOptionWidget(ChestIndex);
	
	// 注意：状态更新将在RewardOptionWidget的StoreBtn点击时执行
}

/**
 * @brief 弹出 URewardOptionWidget 奖励选择/确认弹窗
 * @param ChestIndex 玩家尝试领取的宝箱索引
 *
 * 流程:
 * 1. 防御链: RewardOptionWidgetClass / World / Subsystem / Config 缺一不可
 * 2. CreateWidget<URewardOptionWidget> 创建弹窗实例
 * 3. 构造 FDailyLoginConfigRow TempRow 桥接数据 (ActivityID + ChestIndex 对应的 RewardItemID + DayIndex)
 * 4. RewardOptionWidget->InitSelection(RewardOptions) 初始化弹窗
 * 5. AddToViewport(1000) 高 ZOrder 显示
 * 6. 绑定 OnStoreToBag 事件 (先 Clear 防止热重载残留绑定) 到 HandleRewardStore
 *
 * @note 这里的 "RewardOptions" 数组只有一项, 因为单个宝箱只对应一个奖励物品
 */
void UDailyUpgradeRewardPage::ShowRewardOptionWidget(int32 ChestIndex)
{
	UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: 准备显示RewardOptionWidget，宝箱索引: %d"), ChestIndex);
	
	// 检查RewardOptionWidgetClass是否已设置
	if (!RewardOptionWidgetClass)
	{
		UE_LOG(LogTemp, Error, TEXT("DailyUpgradeRewardPage: RewardOptionWidgetClass未设置！请在蓝图中指定WBP_RewardOptionWidget类"));
		return;
	}
	
	// 创建RewardOptionWidget实例
	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogTemp, Error, TEXT("DailyUpgradeRewardPage: 无法获取世界上下文"));
		return;
	}
	
	URewardOptionWidget* RewardOptionWidget = CreateWidget<URewardOptionWidget>(World, RewardOptionWidgetClass.Get());
	if (!RewardOptionWidget)
	{
		UE_LOG(LogTemp, Error, TEXT("DailyUpgradeRewardPage: 创建RewardOptionWidget失败"));
		return;
	}
	
	UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: 成功创建RewardOptionWidget实例"));
	
	// 获取活动配置数据
	UGameInstance* GameInstance = GetGameInstance();
	if (!GameInstance)
	{
		UE_LOG(LogTemp, Error, TEXT("DailyUpgradeRewardPage: 无法获取GameInstance"));
		return;
	}
	
	UUpgradeActivitySubsystem* Subsystem = GameInstance->GetSubsystem<UUpgradeActivitySubsystem>();
	if (!Subsystem)
	{
		UE_LOG(LogTemp, Error, TEXT("DailyUpgradeRewardPage: 无法获取UpgradeActivitySubsystem"));
		return;
	}
	
	const FDailyUpgradeRewardConfigRow* Config = Subsystem->GetActivityConfig();
	if (!Config)
	{
		UE_LOG(LogTemp, Error, TEXT("DailyUpgradeRewardPage: 无法获取活动配置数据"));
		return;
	}
	
	// 准备奖励选项数据
	TArray<FDailyLoginConfigRow> RewardOptions;
	FDailyLoginConfigRow TempRow;
	TempRow.ActivityID = Config->ActivityID;
	
	// 使用对应的RewardItemID
	if (Config->RewardItemIDs.IsValidIndex(ChestIndex))
	{
		FString RewardItemID = Config->RewardItemIDs[ChestIndex];
		TempRow.RewardItemID = FCString::Atoi(*RewardItemID);
		UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: 使用RewardItemID: %d"), TempRow.RewardItemID);
	}
	
	// 设置天数索引（用于后续状态更新）
	TempRow.DayIndex = ChestIndex;
	
	RewardOptions.Add(TempRow);
	
	// 初始化弹窗
	RewardOptionWidget->InitSelection(RewardOptions);
	
	// 添加到视口显示
	RewardOptionWidget->AddToViewport(1000); // 使用高Z-order确保显示在最上层
	
	// 绑定StoreBtn事件
	RewardOptionWidget->OnStoreToBag.Clear();
	RewardOptionWidget->OnStoreToBag.AddDynamic(this, &UDailyUpgradeRewardPage::HandleRewardStore);
	
	UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: RewardOptionWidget已添加到视口并绑定事件"));
}

/**
 * @brief 刷新所有经验宝箱 Widget 的进度条
 * @details 在 RefreshUI 步骤 2.1 中调用, 与 UpdateExperienceChestWidgetsState 配合
 *
 * 区别:
 *  - RefreshAllProgressBars: 调用每个 ChestWidget 的 RefreshProgressBar (重算进度)
 *  - UpdateExperienceChestWidgetsState: 调用 UpdateButtonState (重算按钮三态)
 *
 * 流程:
 * 1. 校验 ItemsScrollBox 存在
 * 2. 遍历所有子控件, Cast 到 UExperienceChestClaimWidget
 * 3. 对每个有效 ChestWidget 调用:
 *     - RefreshProgressBar() - 重算百分比并更新 ExperienceProgressBar
 *     - UpdateDiamondIconColor() - 根据经验是否满足设置钻石颜色
 *     - UpdateExperienceTextColor() - 根据经验是否满足设置文本颜色
 * 4. 统计并日志输出成功刷新的数量
 *
 * @note 这里**不创建新 Widget**, 假定 ItemsScrollBox 的子节点已由 InitializeExperienceChestWidgets 预创建
 */
void UDailyUpgradeRewardPage::RefreshAllProgressBars()
{
	UE_LOG(LogTemp, Log, TEXT("RefreshAllProgressBars 开始执行"));
	
	if (!ItemsScrollBox)
	{
		UE_LOG(LogTemp, Warning, TEXT("ItemsScrollBox 为空，无法刷新进度条"));
		return;
	}
	
	UE_LOG(LogTemp, Log, TEXT("ItemsScrollBox 子控件数量: %d"), ItemsScrollBox->GetChildrenCount());
	
	// 遍历所有子控件并刷新进度条
	int32 UpdatedCount = 0;
	for (int32 i = 0; i < ItemsScrollBox->GetChildrenCount(); ++i)
	{
		UWidget* ChildWidget = ItemsScrollBox->GetChildAt(i);
		UExperienceChestClaimWidget* ChestWidget = Cast<UExperienceChestClaimWidget>(ChildWidget);
		
		if (ChestWidget)
		{
			UE_LOG(LogTemp, Log, TEXT("刷新第 %d 个宝箱控件的进度条"), i);
			ChestWidget->RefreshProgressBar();
			
			// 更新DiamondIcon颜色
			ChestWidget->UpdateDiamondIconColor();
			
			// 更新ExperienceText颜色
			ChestWidget->UpdateExperienceTextColor();
			UpdatedCount++;
		}
		else
		{
			UE_LOG(LogTemp, Log, TEXT("子控件 %d 不是 ExperienceChestClaimWidget 类型"), i);
		}
	}
	
	UE_LOG(LogTemp, Log, TEXT("RefreshAllProgressBars 执行完成，共刷新 %d 个进度条"), UpdatedCount);
}

/**
 * @brief 更新所有经验宝箱 Widget 的按钮状态 (不重算进度条)
 * @details 与 RefreshAllProgressBars 的区别: 这里只重算按钮三态, 不重算进度条
 *
 * 适用场景: 业务规则变了, 但经验值没变 (例如玩家刚刚领取某个宝箱, ChestClaimStatus 变了, 但 CurrentExperience 没变)
 *
 * 流程:
 * 1. 遍历 ItemsScrollBox 子节点
 * 2. Cast 到 UExperienceChestClaimWidget 后调用:
 *     - UpdateButtonState() - 重新评估 已领取 / 满足经验 / 经验不足 三种状态
 *     - UpdateDiamondIconColor() - 同步更新钻石图标颜色
 *     - UpdateExperienceTextColor() - 同步更新经验文本颜色
 *
 * @note 增量更新, 比 RefreshAllProgressBars 更轻量
 */
void UDailyUpgradeRewardPage::UpdateExperienceChestWidgetsState()
{
	UE_LOG(LogTemp, Log, TEXT("UpdateExperienceChestWidgetsState 开始执行"));
	
	if (!ItemsScrollBox)
	{
		UE_LOG(LogTemp, Log, TEXT("ItemsScrollBox 为空"));
		return;
	}
		
	// 遍历所有子控件并更新状态
	for (int32 i = 0; i < ItemsScrollBox->GetChildrenCount(); ++i)
	{
		UWidget* ChildWidget = ItemsScrollBox->GetChildAt(i);
		UExperienceChestClaimWidget* ChestWidget = Cast<UExperienceChestClaimWidget>(ChildWidget);
		
		if (ChestWidget)
		{
			ChestWidget->UpdateButtonState();
			
			// 更新DiamondIcon颜色
			ChestWidget->UpdateDiamondIconColor();
			
			// 更新ExperienceText颜色
			ChestWidget->UpdateExperienceTextColor();
		}
	}
	
	UE_LOG(LogTemp, Log, TEXT("UpdateExperienceChestWidgetsState 执行完成"));
}

/**
 * @brief 玩家在 RewardOptionWidget 弹窗中点击 StoreBtn 后的回调
 * @param DayIndex 宝箱索引 (DayIndex 实际就是 ChestIndex, 命名沿用弹窗事件签名)
 *
 * 数据更新流程:
 * 1. 从 Subsystem 取出当前 CurrentRecord (副本)
 * 2. 校验 ChestClaimStatus[ChestIndex] 索引有效
 * 3. 设置 ChestClaimStatus[ChestIndex] = 1 (标记为已领取)
 * 4. 更新 LastUpdateTime = FDateTime::Now()
 * 5. **双轨同步**:
 *     - Subsystem->AddOrUpdateRecord(CurrentDay, ModifiedRecord) → 写入 AllRecords 字典
 *     - Subsystem->GetRecord() = ModifiedRecord → 同步写入 CurrentRecord
 * 6. Subsystem->OnGlobalRefresh.Broadcast() → 通知所有订阅者刷新 (含本页面)
 * 7. UpdateFixedPrizeWidget() → 本页面立即响应, 不用等广播
 *
 * 弹窗关闭:
 *   使用 TObjectIterator<URewardOptionWidget> 全局遍历已打开的弹窗
 *   找到第一个 IsInViewport 的实例, 调用 RemoveFromParent 销毁
 *
 * @note 存档写入: HandleRewardStore **不直接调用 SaveGameToSlot**, 走 Subsystem 的 OnGlobalRefresh 事件链
 */
void UDailyUpgradeRewardPage::HandleRewardStore(int32 DayIndex)
{
	
	// 获取Subsystem
	UGameInstance* GameInstance = GetGameInstance();
	if (!GameInstance)
	{
		UE_LOG(LogTemp, Error, TEXT("DailyUpgradeRewardPage: 无法获取GameInstance"));
		return;
	}
	
	UUpgradeActivitySubsystem* Subsystem = GameInstance->GetSubsystem<UUpgradeActivitySubsystem>();
	if (!Subsystem)
	{
		UE_LOG(LogTemp, Error, TEXT("DailyUpgradeRewardPage: 无法获取UpgradeActivitySubsystem"));
		return;
	}
	
	// DayIndex实际上就是ChestIndex
	int32 ChestIndex = DayIndex;

	// 【v228 SSOT 重构】ChestClaimStatus 是全局状态, 跨天共享
	// 严禁直接改 Subsystem 内部字段 (反向伤害)
	// 唯一允许写入的入口: Subsystem->ModifyGlobalChestClaimStatus
	if (!Subsystem->ModifyGlobalChestClaimStatus(ChestIndex, 1, /*bAutoSave=*/false))
	{
		UE_LOG(LogTemp, Error,
			TEXT("DailyUpgradeRewardPage: HandleChestClaimRequest ModifyGlobalChestClaimStatus 失败, ChestIndex=%d"),
			ChestIndex);
		return;
	}
	UE_LOG(LogTemp, Log,
		TEXT("DailyUpgradeRewardPage: 成功更新全局宝箱%d状态为已领取"),
		ChestIndex);

	// 【v228 Bug 3 修】领取后抑制下次 RefreshUI 内的居中滚动 (用户要求: 仅页面刷新时才滚)
	bSuppressNextCenterScroll = true;

	// 全局刷新活动页面
	Subsystem->OnGlobalRefresh.Broadcast();
	UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: 已广播全局刷新事件"));

	// 特别更新FixedPrizeWidget状态
	UpdateFixedPrizeWidget();
	UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: FixedPrizeWidget状态已更新"));

	// 关闭RewardOptionWidget弹窗
	if (RewardOptionWidgetClass)
	{
		// 查找并移除现有的RewardOptionWidget
		for (TObjectIterator<URewardOptionWidget> It; It; ++It)
		{
			URewardOptionWidget* ExistingWidget = *It;
			if (ExistingWidget && ExistingWidget->IsInViewport())
			{
				ExistingWidget->RemoveFromParent();
				UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: 已关闭RewardOptionWidget弹窗"));
				break;
			}
		}
	}
}

/**
 * @brief 初始化"固定奖励" Widget (页面右侧的大宝箱)
 * @details 集中配置 FixedPrizeWidget 的所有显示属性, 可作为"完全更新"使用
 *
 * 设置的内容:
 *  1. HighlightFrameImage 可见性 - 由 Subsystem->ShouldShowFixedPrizeHighlight() 决定
 *  2. ExperienceText 文本 - 来自 TaskRelatedValues 最后一个索引
 *  3. SetChestIndex(FixedIndex) - 设置逻辑索引 (用于区分普通宝箱)
 *  4. UpdateButtonState() - 三态评估 (已领取/可领/不可领)
 *  5. UpdateDiamondIconColor() + UpdateExperienceTextColor() - 颜色同步
 *  6. 绑定 OnChestClaimRequested 到 HandleChestClaimRequest (重复绑定会有警告, 但 RemoveDynamic 由 EnsureSafeBinding 守卫)
 *  7. ChestCountText - "X{count}" 格式
 *  8. ExperienceProgressBar.SetPercent - 通过 Subsystem->CalculateFixedPrizeProgress 计算
 *  9. SetChestBoxIcon - 通过 Subsystem->GetFixedPrizeBoxIcon 加载
 *
 * @note 实质上是 "Initialize + Refresh" 二合一, 后续 UpdateFixedPrizeWidget 复用同一函数
 */
void UDailyUpgradeRewardPage::InitializeFixedPrizeWidget()
{
	UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: 开始初始化FixedPrizeWidget"));
	
	// 检查FixedPrizeWidget控件是否存在
	if (!FixedPrizeWidget)
	{
		UE_LOG(LogTemp, Warning, TEXT("DailyUpgradeRewardPage: FixedPrizeWidget控件未绑定"));
		return;
	}
	
	// 检查是否为编辑器预览模式
	if (!GetWorld() || !GetWorld()->IsGameWorld())
	{
		return; // 静默返回，不输出日志
	}
	
	// 通过Subsystem获取必要数据
	UGameInstance* GameInstance = GetGameInstance();
	if (!GameInstance)
	{
		UE_LOG(LogTemp, Warning, TEXT("DailyUpgradeRewardPage: 无法获取GameInstance"));
		return;
	}
	
	UUpgradeActivitySubsystem* Sub = GameInstance->GetSubsystem<UUpgradeActivitySubsystem>();
	if (!Sub)
	{
		UE_LOG(LogTemp, Error, TEXT("DailyUpgradeRewardPage: 无法获取UpgradeActivitySubsystem"));
		return;
	}
	
	// 使用Subsystem提供的接口获取数据
	bool bShouldShowHighlight = Sub->ShouldShowFixedPrizeHighlight();
	int32 ExperienceValue = Sub->GetFixedPrizeExperienceValue();
	int32 FixedIndex = Sub->GetFixedPrizeIndex();
	
	UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: FixedPrizeWidget数据获取结果:"));
	UE_LOG(LogTemp, Log, TEXT("  - Highlight显示状态: %s"), bShouldShowHighlight ? TEXT("显示") : TEXT("隐藏"));
	UE_LOG(LogTemp, Log, TEXT("  - ExperienceValue(来自TaskRelatedValues最后一个索引): %d"), ExperienceValue);
	UE_LOG(LogTemp, Log, TEXT("  - FixedIndex: %d"), FixedIndex);
	UE_LOG(LogTemp, Log, TEXT("  - 当前经验值: %d"), Sub->GetCurrentExperience());
	
	// 设置HighlightFrameImage的显示状态
	if (FixedPrizeWidget->HighlightFrameImage)
	{
		FixedPrizeWidget->HighlightFrameImage->SetVisibility(
			bShouldShowHighlight ? ESlateVisibility::Visible : ESlateVisibility::Hidden);
		UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: FixedPrizeWidget HighlightFrameImage已%s"), 
			bShouldShowHighlight ? TEXT("显示") : TEXT("隐藏"));
	}
	
	// 设置ExperienceText显示值
	if (FixedPrizeWidget->ExperienceText)
	{
		FString ExperienceText = FString::FromInt(ExperienceValue);
		FixedPrizeWidget->ExperienceText->SetText(FText::FromString(ExperienceText));
		UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: FixedPrizeWidget ExperienceText已设置为: %s (来自TaskRelatedValues最后一个索引)"), *ExperienceText);
	}
	
	// 设置宝箱索引
	if (FixedIndex >= 0)
	{
		FixedPrizeWidget->SetChestIndex(FixedIndex);
		UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: FixedPrizeWidget索引已设置为: %d"), FixedIndex);
	}
	
	// 更新按钮状态
	FixedPrizeWidget->UpdateButtonState();
	
	// 更新钻石图标颜色
	FixedPrizeWidget->UpdateDiamondIconColor();
	
	// 更新经验文本颜色 - 这里会根据CurrentExperience和TaskRelatedValues值判断颜色
	FixedPrizeWidget->UpdateExperienceTextColor();
	
	// 🔧【Ensure 修复】幂等保护: 避免 NativeConstruct + RefreshUI 双路径触发重复绑定
	if (!bIsFixedPrizeWidgetEventBound)
	{
		FixedPrizeWidget->OnChestClaimRequested.AddDynamic(this, &UDailyUpgradeRewardPage::HandleChestClaimRequest);
		bIsFixedPrizeWidgetEventBound = true;
		UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: FixedPrizeWidget事件首次绑定完成"));
	}
	
	// 设置FixedPrizeWidget的宝箱数量文本
	FString ChestCount = Sub->GetFixedPrizeChestCount();
	if (FixedPrizeWidget->ChestCountText)
	{
		FString DisplayText = FString::Printf(TEXT("X%s"), *ChestCount);
		FixedPrizeWidget->ChestCountText->SetText(FText::FromString(DisplayText));
		UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: FixedPrizeWidget宝箱数量已设置为: %s"), *DisplayText);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("DailyUpgradeRewardPage: FixedPrizeWidget的ChestCountText控件未绑定"));
	}
	
	// 更新FixedPrizeWidget专用进度条（调用Subsystem方法）
	float Progress = Sub->CalculateFixedPrizeProgress();
	if (FixedPrizeWidget->ExperienceProgressBar)
	{
		FixedPrizeWidget->ExperienceProgressBar->SetPercent(Progress);
		UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: FixedPrizeWidget进度条已更新为 %.2f%%"), Progress * 100);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("DailyUpgradeRewardPage: FixedPrizeWidget的ExperienceProgressBar控件未绑定"));
	}
	
	// 设置FixedPrizeWidget的宝箱图标
	UTexture2D* BoxIcon = Sub->GetFixedPrizeBoxIcon();
	if (BoxIcon)
	{
		// 使用ExperienceChestClaimWidget已有的SetChestBoxIcon方法
		FixedPrizeWidget->SetChestBoxIcon(BoxIcon);
		UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: FixedPrizeWidget宝箱图标已设置"));
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("DailyUpgradeRewardPage: 无法获取FixedPrizeWidget宝箱图标"));
	}
	
	UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: FixedPrizeWidget初始化完成"));
}

/**
 * @brief 增量更新 FixedPrizeWidget 状态
 * @details 实际是 InitializeFixedPrizeWidget 的别名
 *
 * 原因: InitializeFixedPrizeWidget 设计时已经把"初始化"和"更新"逻辑合并,
 *       所以刷新时直接复用即可
 *
 * 调用方:
 *  - RefreshUI 步骤 7
 *  - HandleRewardStore 完成后立即调用
 */
void UDailyUpgradeRewardPage::UpdateFixedPrizeWidget()
{
	UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: 开始更新FixedPrizeWidget状态"));
	
	// 直接调用初始化函数，因为它已经包含了完整的更新逻辑
	InitializeFixedPrizeWidget();
	
	UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: FixedPrizeWidget状态更新完成"));
}

/**
 * @brief 匿名命名空间 - 存放仅本翻译单元可见的全局静态变量
 * @details GHasInvalidGeometryDetected 用于跨函数通信, 标记是否检测到 ScrollBox 子控件几何信息无效
 *
 * 用途:
 *  - 在 CalculateMaxScrollOffset() 中检测 WidgetSize.X 或 LocalToAbsolute 位置 <= 0 时, 设为 true
 *  - 在 CenterScrollBoxOnCurrentExperience() 中读到该标志为 true 时, 改用估算公式 (130px 控件 + 10px 间距)
 *
 * 为什么需要它:
 *   UE 的 ScrollBox 在 ForceLayoutPrepass() 之后, 部分子 Widget 仍未被布局引擎计算实际大小
 *   (尤其是 InitializeExperienceChestWidgets 刚创建还没经过一帧的 Widget)
 *   此时 GetCachedGeometry().GetLocalSize().X 会是 0
 *   单个函数内无法判断"计算结果为 0 是正常, 还是异常",
 *   所以用全局标志位 + 备用方案规避
 */
namespace
{
	bool GHasInvalidGeometryDetected = false;
}

/**
 * @brief 滚动 ItemsScrollBox, 让"当前经验值对应"的宝箱居中显示
 * @details 在 NativeConstruct (延迟 0.2s 触发) 和 RefreshUI 步骤 2.2 中调用
 *
 * 算法流程:
 *  1. ItemsScrollBox->ForceLayoutPrepass() - 强制布局预计算
 *  2. 防御链: ItemsScrollBox / 子控件 > 0 / GameInstance / Subsystem
 *  3. 调用 Subsystem->GetTargetChestIndexForCurrentExperience() 拿到目标索引
 *  4. 目标索引有效性校验
 *  5. 区分两种分支:
 *     a) 目标是最后一个控件 → 用 CalculateMaxScrollOffset 算最大值, 必要时启用估算公式
 *     b) 目标是中间控件 → 用 CalculateCenterScrollOffset 算居中偏移
 *  6. SetScrollOffset(ScrollOffset) 应用
 *
 * 备选方案触发条件:
 *   - ScrollOffset <= 0 (说明 TotalContentWidth <= ViewportWidth, 内容放不下)
 *   - GHasInvalidGeometryDetected == true (说明子控件几何信息无效)
 *   此时用 130px 控件宽 + 10px 间距 + 50px 额外偏移的估算值
 */
void UDailyUpgradeRewardPage::CenterScrollBoxOnCurrentExperience()
{
	UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: 开始根据当前经验值居中ScrollBox内容"));
	
	// 强制刷新布局，确保几何信息准确
	if (ItemsScrollBox)
	{
		ItemsScrollBox->ForceLayoutPrepass();
		UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: 已强制刷新ScrollBox布局"));
	}
	
	// 检查必要组件
	if (!ItemsScrollBox)
	{
		UE_LOG(LogTemp, Warning, TEXT("DailyUpgradeRewardPage: ItemsScrollBox控件未绑定"));
		return;
	}
	
	if (ItemsScrollBox->GetChildrenCount() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("DailyUpgradeRewardPage: ItemsScrollBox中没有子控件"));
		return;
	}
	
	// 通过Subsystem获取业务逻辑结果
	UGameInstance* GameInstance = GetGameInstance();
	if (!GameInstance)
	{
		UE_LOG(LogTemp, Warning, TEXT("DailyUpgradeRewardPage: 无法获取GameInstance"));
		return;
	}
	
	UUpgradeActivitySubsystem* Sub = GameInstance->GetSubsystem<UUpgradeActivitySubsystem>();
	if (!Sub)
	{
		UE_LOG(LogTemp, Error, TEXT("DailyUpgradeRewardPage: 无法获取UpgradeActivitySubsystem"));
		return;
	}
	
	// 直接调用Subsystem的业务逻辑函数
	int32 TargetIndex = Sub->GetTargetChestIndexForCurrentExperience();
	UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: 从Subsystem获取的目标宝箱索引: %d"), TargetIndex);
	UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: ItemsScrollBox子控件总数: %d"), ItemsScrollBox->GetChildrenCount());
	UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: 目标索引有效性检查: %s"), 
		(TargetIndex >= 0 && TargetIndex < ItemsScrollBox->GetChildrenCount()) ? TEXT("有效") : TEXT("无效"));
	
	if (TargetIndex >= 0 && TargetIndex < ItemsScrollBox->GetChildrenCount())
	{
		float ScrollOffset = 0.0f;
		
		// 检查是否为目标是最后一个控件（需要靠右显示）
		if (TargetIndex == ItemsScrollBox->GetChildrenCount() - 1)
		{
			// 最后一个控件：使用最大滚动偏移量，使其靠右显示
			ScrollOffset = CalculateMaxScrollOffset();
			UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: 目标为最后一个控件，设置最大滚动偏移量: %.2f"), ScrollOffset);
					
			// 检查是否需要使用备选方案（几何信息无效或计算结果为0）
			if (ScrollOffset <= 0.0f || GHasInvalidGeometryDetected)
			{
				// 备选方案：基于控件数量和估算宽度计算
				float EstimatedWidgetWidth = 130.0f; // 使用实际测量的控件宽度
				float EstimatedSpacing = 10.0f; // 估算间距
				int32 WidgetCount = ItemsScrollBox->GetChildrenCount();
						
				float TotalEstimatedWidth = WidgetCount * EstimatedWidgetWidth + (WidgetCount - 1) * EstimatedSpacing;
				float ViewportWidth = ItemsScrollBox->GetCachedGeometry().GetLocalSize().X;
				float EstimatedMaxOffset = FMath::Max(0.0f, TotalEstimatedWidth - ViewportWidth);
						
				// 使用更大的估算值确保靠右效果
				EstimatedMaxOffset += 50.0f; // 额外偏移确保完全靠右
						
				ScrollOffset = EstimatedMaxOffset;
				UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: 使用备选方案估算值: %.2f (控件数:%d, 估算总宽:%.2f, 可视宽:%.2f, 额外偏移:50.00)"), 
					EstimatedMaxOffset, WidgetCount, TotalEstimatedWidth, ViewportWidth);
						
				GHasInvalidGeometryDetected = false; // 重置状态
			}
		}
		else
		{
			// 非最后一个控件：使用居中逻辑
			ScrollOffset = CalculateCenterScrollOffset(TargetIndex);
			UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: 目标为中间控件，设置居中滚动偏移量: %.2f"), ScrollOffset);
		}
		
		// 设置滚动位置
		ItemsScrollBox->SetScrollOffset(ScrollOffset);
		
		UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: 已设置ScrollBox滚动偏移量为 %.2f，目标控件索引: %d"), 
			ScrollOffset, TargetIndex);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("DailyUpgradeRewardPage: 目标索引 %d 超出范围 [0, %d]"), 
			TargetIndex, ItemsScrollBox->GetChildrenCount() - 1);
	}
}

/**
 * @brief 根据当前经验值查找目标宝箱索引 - 业务逻辑下沉到 Subsystem 的本地桥接
 * @param CurrentExp 当前经验值 (虽然函数已不再直接用, 但保留签名以兼容可能的外部调用)
 * @param TaskRelatedValues 经验阈值数组
 * @return 目标宝箱索引
 *
 * 业务流程:
 *  1. 优先调用 Subsystem->GetTargetChestIndexForCurrentExperience() (业务逻辑下沉)
 *  2. Subsystem 不可用时, 启用降级方案:
 *     - 遍历 TaskRelatedValues, 找到第一个 > CurrentExp 的索引 i
 *     - 返回 max(0, i - 1) (即"刚好满足"的那个宝箱)
 *     - 如果所有阈值都 <= CurrentExp, 返回最后一个索引
 *
 * @note 现在几乎不会被调用, 因为 CenterScrollBoxOnCurrentExperience 直接走 Subsystem
 *       保留它是为了未来可能的扩展 (例如外部逻辑传入自定义经验值)
 */
int32 UDailyUpgradeRewardPage::FindTargetChestIndexForExperience(int32 CurrentExp, const TArray<int32>& TaskRelatedValues)
{
	// 调用Subsystem的业务逻辑函数
	UGameInstance* GameInstance = GetGameInstance();
	if (GameInstance)
	{
		UUpgradeActivitySubsystem* Sub = GameInstance->GetSubsystem<UUpgradeActivitySubsystem>();
		if (Sub)
		{
			return Sub->GetTargetChestIndexForCurrentExperience();
		}
	}
	
	// 降级方案：使用原来的本地逻辑
	for (int32 i = 0; i < TaskRelatedValues.Num(); ++i)
	{
		if (TaskRelatedValues[i] > CurrentExp)
		{
			int32 TargetIndex = FMath::Max(0, i - 1);
			UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: 降级方案 - 找到经验值分界点，索引 %d 的值 %d > 当前经验 %d，目标索引: %d"), 
				i, TaskRelatedValues[i], CurrentExp, TargetIndex);
			return TargetIndex;
		}
	}
	
	int32 LastIndex = FMath::Max(0, TaskRelatedValues.Num() - 1);
	UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: 降级方案 - 当前经验 %d 超过了所有TaskRelatedValues，返回最后一个索引: %d"), 
		CurrentExp, LastIndex);
	return LastIndex;
}

/**
 * @brief 计算"让目标索引的控件居中"所需的 ScrollBox 滚动偏移量
 * @param TargetIndex 目标宝箱在 ItemsScrollBox 中的子节点索引
 * @return 滚动偏移量 (像素), 已 Clamp 到 [0, MaxOffset]
 *
 * 算法:
 *  1. ViewportWidth = ScrollBox 的可视区域宽度
 *  2. 遍历累加前 N 个子节点的 WidgetWidth + 假设 10px 间距, 累加到 TargetPosition
 *  3. TargetPosition = 目标控件左边缘的累计 X 坐标
 *  4. CenterOffset = TargetPosition + TargetWidgetWidth/2 - ViewportWidth/2
 *     (让目标控件中心对齐 ScrollBox 中心)
 *  5. 用 CalculateMaxScrollOffset() 拿最大值, Clamp(CenterOffset, 0, MaxOffset)
 *
 * 关键假设: 子控件间距恒为 10px (因为 ItemsScrollBox 默认没有 spacing 设置)
 *          如果未来调整了 spacing, 需要同步修改这个常量
 */
float UDailyUpgradeRewardPage::CalculateCenterScrollOffset(int32 TargetIndex)
{
	// 计算使目标控件居中显示所需的滚动偏移量
	
	if (ItemsScrollBox->GetChildrenCount() == 0)
		return 0.0f;
	
	// 获取ScrollBox的可视区域宽度
	float ViewportWidth = ItemsScrollBox->GetCachedGeometry().GetLocalSize().X;
	UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: ScrollBox可视区域宽度: %.2f"), ViewportWidth);
	
	// 计算目标控件的累计位置
	float TargetPosition = 0.0f;
	float TargetWidgetWidth = 0.0f;
	
	for (int32 i = 0; i < ItemsScrollBox->GetChildrenCount(); ++i)
	{
		UWidget* ChildWidget = ItemsScrollBox->GetChildAt(i);
		if (ChildWidget)
		{
			float WidgetWidth = ChildWidget->GetCachedGeometry().GetLocalSize().X;
			
			// 添加间距（假设有默认间距）
			if (i > 0)
			{
				TargetPosition += 10.0f; // 假设10像素间距
			}
			
			if (i == TargetIndex)
			{
				TargetWidgetWidth = WidgetWidth;
				break; // 找到目标控件，停止累加
			}
			
			TargetPosition += WidgetWidth;
		}
	}
	
	UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: 目标控件累积位置: %.2f, 宽度: %.2f"), TargetPosition, TargetWidgetWidth);
	
	// 计算居中偏移量
	float CenterOffset = TargetPosition + (TargetWidgetWidth / 2.0f) - (ViewportWidth / 2.0f);
	
	// 确保偏移量在有效范围内
	float MaxOffset = CalculateMaxScrollOffset();
	CenterOffset = FMath::Clamp(CenterOffset, 0.0f, MaxOffset);
	
	UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: 计算得出的居中偏移量: %.2f (范围: 0.0 - %.2f)"), CenterOffset, MaxOffset);
	
	return CenterOffset;
}

/**
 * @brief 计算 ScrollBox 的最大滚动偏移量 (总内容宽度 - 可视宽度)
 * @return MaxOffset (像素), 当内容不够滚动时返回 0
 *
 * 算法:
 *  1. 累加所有子 Widget 宽度 + 假设的 10px 间距
 *  2. 减去 ViewportWidth
 *  3. 用 FMath::Max 防止负数
 *
 * 副作用:
 *  - 遍历子控件时检查每个 WidgetSize.X 和 LocalToAbsolute X 是否 > 0
 *  - 如果发现任何一个 <= 0, 标记 GHasInvalidGeometryDetected = true
 *  - 调用方 (CenterScrollBoxOnCurrentExperience) 看到标志后, 走估算公式
 *
 * @note 这是一个有"外部副作用"的纯计算函数, 设计上耦合了全局静态变量
 *       在单线程的 Slate 渲染线程下安全, 但要小心多线程/异步场景
 */
float UDailyUpgradeRewardPage::CalculateMaxScrollOffset()
{
	// 计算ScrollBox的最大滚动偏移量
	
	if (ItemsScrollBox->GetChildrenCount() == 0)
		return 0.0f;
	
	float TotalContentWidth = 0.0f;
	
	// 计算所有子控件的总宽度
	for (int32 i = 0; i < ItemsScrollBox->GetChildrenCount(); ++i)
	{
		UWidget* ChildWidget = ItemsScrollBox->GetChildAt(i);
		if (ChildWidget)
		{
			TotalContentWidth += ChildWidget->GetCachedGeometry().GetLocalSize().X;
			
			// 添加间距
			if (i < ItemsScrollBox->GetChildrenCount() - 1)
			{
				TotalContentWidth += 10.0f; // 假设10像素间距
			}
		}
	}
	
	// 获取可视区域宽度
	float ViewportWidth = ItemsScrollBox->GetCachedGeometry().GetLocalSize().X;
	
	// 最大偏移量 = 总内容宽度 - 可视区域宽度
	float MaxOffset = FMath::Max(0.0f, TotalContentWidth - ViewportWidth);
	
	UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: 总内容宽度: %.2f, 可视区域宽度: %.2f, 最大偏移量: %.2f"), 
		TotalContentWidth, ViewportWidth, MaxOffset);
	
	// 详细调试信息
	UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: 详细几何信息:"));
	UE_LOG(LogTemp, Log, TEXT("  - ScrollBox实际宽度: %.2f"), ItemsScrollBox->GetCachedGeometry().GetLocalSize().X);
	
	// 检查所有子控件的信息
	bool bHasInvalidGeometry = false;
	for (int32 i = 0; i < ItemsScrollBox->GetChildrenCount(); ++i)
	{
		UWidget* ChildWidget = ItemsScrollBox->GetChildAt(i);
		if (ChildWidget)
		{
			FVector2D WidgetSize = ChildWidget->GetCachedGeometry().GetLocalSize();
			FVector2D WidgetPosition = ChildWidget->GetCachedGeometry().LocalToAbsolute(FVector2D(0, 0));
			UE_LOG(LogTemp, Log, TEXT("  - 控件[%d] 宽度: %.2f, 位置X: %.2f"), i, WidgetSize.X, WidgetPosition.X);
			
			// 检查是否有无效的几何信息
			if (WidgetSize.X <= 0.0f || WidgetPosition.X <= 0.0f)
			{
				bHasInvalidGeometry = true;
				UE_LOG(LogTemp, Log, TEXT("  ⚠️  控件[%d] 几何信息无效，将使用备选方案"), i);
			}
		}
	}
	
	// 如果发现无效几何信息，提前标记需要使用备选方案
	if (bHasInvalidGeometry)
	{
		UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: 检测到无效几何信息，准备使用备选计算方案"));
		
		// 设置全局标志，让滚动函数知道需要使用备选方案
		GHasInvalidGeometryDetected = true;
	}

	return MaxOffset;
}

/**
 * @brief 更新限时加成图标容器 (BonusIconsContainer) 的内容
 * @details 根据 TargetDay 的 CreatedTime + BonusDurationHours 判断是否过期, 然后加载对应 BonusIDs 的图标
 *
 * 业务流程 (8 步):
 *  1. 校验 BonusIconsContainer 存在且在游戏世界
 *  2. 校验两个 Subsystem (Upgrade + Activity) 都有
 *  3. 查 MaxRecordDate, 但上限 5 (用于"day5 内完成累计任务"型业务)
 *  4. 查 TargetRecord (FUpgradeRewardSaveRecord) - 拿 CreatedTime
 *  5. 查 ConfigRow (FDailyUpgradeRewardConfigRow) - 拿 BonusDurationHours
 *  6. 计算 ExpiryTime = CreatedTime + BonusDurationHours
 *  7. 过期检查: CurrentTime >= ExpiryTime 则清空容器并返回
 *  8. 未过期: 遍历 ConfigRow->BonusIDs, 每个 ItemID 通过 ActivitySubsystem->GetItemDetail 查到 ItemIcon
 *     - CreateWidget<WBP_RewardIcon>
 *     - GetWidgetFromName("RewardImage") 设置 ItemIcon (128x128)
 *     - GetWidgetFromName("CountText") 设为 Collapsed (不显示数量)
 *     - 添加到容器, 清空 HorizontalBoxSlot 的 padding
 *
 * @note 防御链非常长, 任意一步失败都不会崩, 只会清空容器或跳过单个图标
 */
void UDailyUpgradeRewardPage::UpdateBonusIconsContainer()
{
	// [BONUS_DEBUG] 开始执行UpdateBonusIconsContainer
	UE_LOG(LogTemp, Log, TEXT("[BONUS_DEBUG] 开始执行UpdateBonusIconsContainer"));
	
	// 检查必要组件
	if (!BonusIconsContainer)
	{
		UE_LOG(LogTemp, Warning, TEXT("[BONUS_DEBUG] BonusIconsContainer为空"));
		return;
	}

	// 检查是否为编辑器预览模式
	if (!GetWorld() || !GetWorld()->IsGameWorld())
	{
		UE_LOG(LogTemp, Warning, TEXT("[BONUS_DEBUG] 不在游戏世界中，跳过执行"));
		return; // 静默返回，不输出日志
	}

	// [BONUS_DEBUG] 获取Subsystem
	UGameInstance* GameInstance = GetGameInstance();
	if (!GameInstance)
	{
		UE_LOG(LogTemp, Warning, TEXT("[BONUS_DEBUG] GameInstance为空"));
		return;
	}

	UUpgradeActivitySubsystem* UpgradeActivitySubsystem = GameInstance->GetSubsystem<UUpgradeActivitySubsystem>();
	if (!UpgradeActivitySubsystem)
	{
		UE_LOG(LogTemp, Warning, TEXT("[BONUS_DEBUG] UpgradeActivitySubsystem为空"));
		return;
	}

	UActivitySubsystem* ActivitySubsystem = GameInstance->GetSubsystem<UActivitySubsystem>();
	if (!ActivitySubsystem)
	{
		UE_LOG(LogTemp, Warning, TEXT("[BONUS_DEBUG] ActivitySubsystem为空"));
		return;
	}

	// 1. 查询UpgradeActivitySubsystem类内存数据的RecordDate的最大值（如超过5就取5的行数据做判断）
	int32 MaxRecordDate = UpgradeActivitySubsystem->GetMaxRecordDate();
	int32 TargetDayNumber = FMath::Min(MaxRecordDate, 5); // 超过5就取5
	FString TargetDayIdentifier = FString::Printf(TEXT("day%d"), TargetDayNumber);
	UE_LOG(LogTemp, Log, TEXT("[BONUS_DEBUG] MaxRecordDate=%d, TargetDayNumber=%d, TargetDayIdentifier=%s"), MaxRecordDate, TargetDayNumber, *TargetDayIdentifier);

	// 2. 查询此行数据的CreatedTime值
	const FUpgradeRewardSaveRecord* TargetRecord = UpgradeActivitySubsystem->GetRecordByDate(TargetDayNumber);
	if (!TargetRecord)
	{
		UE_LOG(LogTemp, Warning, TEXT("[BONUS_DEBUG] 未找到TargetRecord，TargetDayNumber=%d"), TargetDayNumber);
		// 如果没有找到记录，清空容器
		BonusIconsContainer->ClearChildren();
		return;
	}
	UE_LOG(LogTemp, Log, TEXT("[BONUS_DEBUG] 找到TargetRecord，CreatedTime=%s"), *TargetRecord->CreatedTime.ToString());

	FDateTime CreatedTime = TargetRecord->CreatedTime;

	// 3. 获取DailyUpgradeRewardConfigRow表对应天数的BonusDurationHours的值
	const FDailyUpgradeRewardConfigRow* ConfigRow = UpgradeActivitySubsystem->GetConfigRowForDay(TargetDayIdentifier);
	if (!ConfigRow)
	{
		UE_LOG(LogTemp, Warning, TEXT("[BONUS_DEBUG] 未找到ConfigRow，TargetDayIdentifier=%s"), *TargetDayIdentifier);
		// 如果没有找到配置，清空容器
		BonusIconsContainer->ClearChildren();
		return;
	}
	UE_LOG(LogTemp, Log, TEXT("[BONUS_DEBUG] 找到ConfigRow，BonusDurationHours=%d"), ConfigRow->BonusDurationHours);

	int32 BonusDurationHours = ConfigRow->BonusDurationHours;

	// 4. 检查CreatedTime是否小于当前时间 - BonusDurationHours
	FDateTime CurrentTime = FDateTime::Now();
	FDateTime ExpiryTime = CreatedTime + FTimespan::FromHours(BonusDurationHours);

	UE_LOG(LogTemp, Log, TEXT("[BONUS_DEBUG] CurrentTime=%s, ExpiryTime=%s, 过期检查结果=%s"), 
		*CurrentTime.ToString(), *ExpiryTime.ToString(), (CurrentTime >= ExpiryTime) ? TEXT("已过期") : TEXT("未过期"));
	
	if (CurrentTime >= ExpiryTime)
	{
		// 已过期，不加载RewardIcon蓝图组件
		BonusIconsContainer->ClearChildren();
		UE_LOG(LogTemp, Log, TEXT("[BONUS_ICONS] 活动已过期，不加载奖励图标 - Day: %s, CreatedTime: %s, ExpiryTime: %s, CurrentTime: %s"), 
			*TargetDayIdentifier, *CreatedTime.ToString(), *ExpiryTime.ToString(), *CurrentTime.ToString());
		return;
	}

	// 5. 未过期，查询DailyUpgradeRewardConfigRow表对应的天数行数据的BonusIDs数组
	const TArray<int32>& BonusIDs = ConfigRow->BonusIDs;
	UE_LOG(LogTemp, Log, TEXT("[BONUS_DEBUG] BonusIDs数量=%d"), BonusIDs.Num());
	if (BonusIDs.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[BONUS_DEBUG] BonusIDs为空，清空容器"));
		// 没有BonusIDs，清空容器
		BonusIconsContainer->ClearChildren();
		return;
	}

	// 6. 清空现有子控件
	BonusIconsContainer->ClearChildren();

	// 7. 使用已配置的WBP_RewardIcon蓝图类
	UE_LOG(LogTemp, Log, TEXT("[BONUS_DEBUG] RewardIconWidgetClass是否有效=%s"), RewardIconWidgetClass ? TEXT("是") : TEXT("否"));
	if (!RewardIconWidgetClass)
	{
		UE_LOG(LogTemp, Error, TEXT("[BONUS_ICONS] RewardIconWidgetClass未设置，请在蓝图中指定WBP_RewardIcon类"));
		return;
	}

	// 8. 遍历BonusIDs数组，创建RewardIcon组件
	UE_LOG(LogTemp, Log, TEXT("[BONUS_DEBUG] 开始遍历BonusIDs，共%d个"), BonusIDs.Num());
	for (int32 i = 0; i < BonusIDs.Num(); ++i)
	{
		int32 ItemID = BonusIDs[i];
		UE_LOG(LogTemp, Log, TEXT("[BONUS_DEBUG] 处理ItemID=%d (%d/%d)"), ItemID, i + 1, BonusIDs.Num());

		// 关联ItemDetailRow表的ItemID字段，得到对应的ItemIcon
		// 使用ActivitySubsystem的GetItemDetail方法，它会遍历所有行查找ItemID匹配的记录
		const FItemDetailRow* ItemDetailRow = ActivitySubsystem->GetItemDetail(ItemID);
		if (!ItemDetailRow)
		{
			UE_LOG(LogTemp, Warning, TEXT("[BONUS_DEBUG] 无法找到ItemID %d 的ItemDetailRow"), ItemID);
			continue;
		}
		UE_LOG(LogTemp, Log, TEXT("[BONUS_DEBUG] 找到ItemDetailRow，ItemIcon是否有效=%s"), ItemDetailRow->ItemIcon.IsNull() ? TEXT("否") : TEXT("是"));

		// 创建WBP_RewardIcon实例
		UUserWidget* RewardIconWidget = CreateWidget<UUserWidget>(GetWorld(), RewardIconWidgetClass);
		if (!RewardIconWidget)
		{
			UE_LOG(LogTemp, Warning, TEXT("[BONUS_DEBUG] 无法创建WBP_RewardIcon实例"));
			continue;
		}
		UE_LOG(LogTemp, Log, TEXT("[BONUS_DEBUG] 成功创建WBP_RewardIcon实例"));

		// 同步加载图标, 与 SetupRewardsContainer 逻辑一致 (避免异步占位符淡色问题)
		UTexture2D* ItemTexture = ItemDetailRow->ItemIcon.LoadSynchronous();

		// 设置图标
		// 蓝图自身控制尺寸, C++ 不干预
		UImage* RewardImage = Cast<UImage>(RewardIconWidget->GetWidgetFromName(TEXT("RewardImage")));
		if (RewardImage && ItemTexture)
		{
			RewardImage->SetBrushFromTexture(ItemTexture);
		}

		// 隐藏 CountText 控件
		UTextBlock* CountText = Cast<UTextBlock>(RewardIconWidget->GetWidgetFromName(TEXT("CountText")));
		if (CountText)
		{
			CountText->SetVisibility(ESlateVisibility::Collapsed);
		}

		// 添加到 BonusIconsContainer, 蓝图自身控制 Slot 布局
		BonusIconsContainer->AddChild(RewardIconWidget);

		UE_LOG(LogTemp, Log, TEXT("[BONUS_ICONS] 成功创建奖励图标 %d: ItemID=%d"), i + 1, ItemID);
	}
	
	UE_LOG(LogTemp, Log, TEXT("[BONUS_DEBUG] UpdateBonusIconsContainer执行完成，容器子项数量=%d"), BonusIconsContainer->GetChildrenCount());
	UE_LOG(LogTemp, Log, TEXT("[BONUS_ICONS] 限时加成图标容器更新完成，共创建 %d 个图标"), BonusIDs.Num());
}

/**
 * @brief 更新限时加成信息文本 (BonusInfoText) - 显示 "{描述} {完成数}/{总数} 剩余时长: {H}H {M}M {S}S"
 * @param DayIdentifier 天数标识符 (如 "day1", "day3")
 *
 * 业务流程:
 *  1. 防御链: BonusInfoText 控件有效 / GameInstance / Subsystem
 *  2. 查 ConfigRow, 如果没有或 BonusDescription 为空, 隐藏控件
 *  3. 查 HasDayDataInMemory, 没数据则隐藏
 *  4. 查 DayRecord 拿 CreatedTime
 *  5. 计算 EndTime = CreatedTime + BonusDurationHours
 *  6. 已过期则隐藏
 *  7. 计算 RemainingTime = EndTime - CurrentTime
 *  8. 拼装显示文本: "{Description} {Complete}/{Total} 剩余时长: {H}H {M}M"
 *  9. 强制设置字体大小 (如果未设置, 用 16.0f 默认值)
 * 10. BonusInfoText->SetText + SetVisibility(Visible)
 * 11. 设置 BonusInfoBorder 的 Padding (10,5,10,5) 和可见性
 * 12. 强制刷新布局 (InvalidateLayoutAndVolatility)
 *
 * @note 包含大量调试日志 ([BONUS_DEBUG]), 调试完成后可清理
 *       实际业务逻辑下沉到了 Subsystem::GetLimitedActivityCompleteCount 等方法
 */
void UDailyUpgradeRewardPage::UpdateBonusInfoText(const FString& DayIdentifier)
{
	if (!BonusInfoText)
	{
		UE_LOG(LogTemp, Warning, TEXT("[BONUS_DEBUG] UDailyUpgradeRewardPage::UpdateBonusInfoText: BonusInfoText控件未绑定"));
		return;
	}

	// 调试：检查控件是否有效
	if (!IsValid(BonusInfoText))
	{
		UE_LOG(LogTemp, Warning, TEXT("[BONUS_DEBUG] UDailyUpgradeRewardPage::UpdateBonusInfoText: BonusInfoText控件无效"));
		return;
	}

	// 调试：检查Border控件
	if (BonusInfoBorder && !IsValid(BonusInfoBorder))
	{
		UE_LOG(LogTemp, Warning, TEXT("[BONUS_DEBUG] UDailyUpgradeRewardPage::UpdateBonusInfoText: BonusInfoBorder控件无效"));
		BonusInfoBorder = nullptr; // 设为null，使用兼容模式
	}

	// 调试：检查控件的父容器
	UWidget* Parent = BonusInfoText->GetParent();
	if (Parent)
	{
		UE_LOG(LogTemp, Log, TEXT("[BONUS_DEBUG] BonusInfoText的父容器: %s"), *Parent->GetName());
		if (!IsValid(Parent))
		{
			UE_LOG(LogTemp, Warning, TEXT("[BONUS_DEBUG] UDailyUpgradeRewardPage::UpdateBonusInfoText: BonusInfoText的父容器无效"));
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[BONUS_DEBUG] BonusInfoText没有父容器"));
		
		// 如果没有父容器，说明需要在蓝图中设置
		UE_LOG(LogTemp, Warning, TEXT("[BONUS_DEBUG] 请在蓝图中将BonusInfoText设置为BonusInfoBorder的内容"));
	}

	// 获取 GameInstance 和 Subsystem
	UGameInstance* GameInstance = GetGameInstance();
	if (!GameInstance)
	{
		UE_LOG(LogTemp, Warning, TEXT("[BONUS_DEBUG] UDailyUpgradeRewardPage::UpdateBonusInfoText: 无法获取GameInstance"));
		BonusInfoText->SetVisibility(ESlateVisibility::Collapsed);
		if (BonusInfoBorder)
		{
			BonusInfoBorder->SetVisibility(ESlateVisibility::Collapsed);
		}
		StopBonusCountdownTimer(); // 🔧【v219】隐藏时停倒计时
		return;
	}

	UUpgradeActivitySubsystem* Subsystem = GameInstance->GetSubsystem<UUpgradeActivitySubsystem>();
	if (!Subsystem)
	{
		UE_LOG(LogTemp, Warning, TEXT("[BONUS_DEBUG] UDailyUpgradeRewardPage::UpdateBonusInfoText: 无法获取UpgradeActivitySubsystem"));
		BonusInfoText->SetVisibility(ESlateVisibility::Collapsed);
		if (BonusInfoBorder)
		{
			BonusInfoBorder->SetVisibility(ESlateVisibility::Collapsed);
		}
		StopBonusCountdownTimer(); // 🔧【v219】隐藏时停倒计时
		return;
	}

	// 获取配置行
	const FDailyUpgradeRewardConfigRow* ConfigRow = Subsystem->GetConfigRowForDay(DayIdentifier);
	if (!ConfigRow)
	{
		UE_LOG(LogTemp, Warning, TEXT("[BONUS_DEBUG] UDailyUpgradeRewardPage::UpdateBonusInfoText: 未找到配置行 - Day:%s"), *DayIdentifier);
		BonusInfoText->SetVisibility(ESlateVisibility::Collapsed);
		if (BonusInfoBorder)
		{
			BonusInfoBorder->SetVisibility(ESlateVisibility::Collapsed);
		}
		StopBonusCountdownTimer(); // 🔧【v219】隐藏时停倒计时
		return;
	}

	if (ConfigRow->BonusDescription.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("[BONUS_DEBUG] UDailyUpgradeRewardPage::UpdateBonusInfoText: BonusDescription为空 - Day:%s"), *DayIdentifier);
		BonusInfoText->SetVisibility(ESlateVisibility::Collapsed);
		if (BonusInfoBorder)
		{
			BonusInfoBorder->SetVisibility(ESlateVisibility::Collapsed);
		}
		StopBonusCountdownTimer(); // 🔧【v219】隐藏时停倒计时
		return;
	}

	// 检查内存中是否有该天数的数据
	int32 DayNumber = FCString::Atoi(*DayIdentifier.RightChop(3)); // 从 "day1" 提取数字 1
	bool bHasDayData = Subsystem->HasDayDataInMemory(DayNumber);
	
	// 如果内存中没有该天数的数据，隐藏控件
	if (!bHasDayData)
	{
		UE_LOG(LogTemp, Warning, TEXT("[BONUS_DEBUG] UDailyUpgradeRewardPage::UpdateBonusInfoText: 内存中无天数数据 - Day:%s"), *DayIdentifier);
		BonusInfoText->SetVisibility(ESlateVisibility::Collapsed);
		if (BonusInfoBorder)
		{
			BonusInfoBorder->SetVisibility(ESlateVisibility::Collapsed);
		}
		StopBonusCountdownTimer(); // 🔧【v219】隐藏时停倒计时
		return;
	}
	
	// 获取相关数据
	int32 LimitedActivityCompleteCount = Subsystem->GetLimitedActivityCompleteCount();
	int32 BonusCount = ConfigRow->BonusCount;
	// 从指定天数的记录中获取 CreatedTime
	const FUpgradeRewardSaveRecord* DayRecord = Subsystem->GetRecordByDate(DayNumber);
	FDateTime CreatedTime = DayRecord ? DayRecord->CreatedTime : Subsystem->GetRecordCreatedTime();
	int32 BonusDurationHours = ConfigRow->BonusDurationHours;

	UE_LOG(LogTemp, Log, TEXT("[BONUS_DEBUG] UDailyUpgradeRewardPage::UpdateBonusInfoText: Day:%s, BonusDescription:%s, CompleteCount:%d, BonusCount:%d, CreatedTime:%s, DurationHours:%d"), 
		*DayIdentifier, *ConfigRow->BonusDescription, LimitedActivityCompleteCount, BonusCount, *CreatedTime.ToString(), BonusDurationHours);

	// 计算结束时间
	FDateTime EndTime = CreatedTime + FTimespan::FromHours(BonusDurationHours);
	FDateTime CurrentTime = FDateTime::Now();

	UE_LOG(LogTemp, Log, TEXT("[BONUS_DEBUG] UDailyUpgradeRewardPage::UpdateBonusInfoText: CurrentTime:%s, EndTime:%s"), *CurrentTime.ToString(), *EndTime.ToString());

	// 检查是否已过期
	if (CurrentTime >= EndTime)
	{
		UE_LOG(LogTemp, Warning, TEXT("[BONUS_DEBUG] UDailyUpgradeRewardPage::UpdateBonusInfoText: 活动已过期 - Day:%s"), *DayIdentifier);
		// 已过期，隐藏控件
		BonusInfoText->SetVisibility(ESlateVisibility::Collapsed);
		if (BonusInfoBorder)
		{
			BonusInfoBorder->SetVisibility(ESlateVisibility::Collapsed);
		}
		StopBonusCountdownTimer(); // 🔧【v219】过期时停倒计时
		return;
	}

	// 🔧【v218】增加秒显示 - 大厂原则: 时间精度要全, 不能丢精度
	FTimespan RemainingTime = EndTime - CurrentTime;
	const int32 RemainingHours = RemainingTime.GetHours();
	const int32 RemainingMinutes = RemainingTime.GetMinutes() % 60;
	const int32 RemainingSeconds = RemainingTime.GetSeconds() % 60;

	// 格式化显示文本 (H + M + S)
	const FString DisplayText = FString::Printf(TEXT("%s %d/%d 剩余时长：%dH %dM %dS"),
		*ConfigRow->BonusDescription,
		LimitedActivityCompleteCount,
		BonusCount,
		RemainingHours,
		RemainingMinutes,
		RemainingSeconds);

	UE_LOG(LogTemp, Log, TEXT("[BONUS_DEBUG] UDailyUpgradeRewardPage::UpdateBonusInfoText: 显示文本 - %s"), *DisplayText);

	// 设置文本并显示
	BonusInfoText->SetText(FText::FromString(DisplayText));

	// 【大厂架构】字体大小由蓝图控制, C++ 不干预
	// 原因: 这是布局属性, 蓝图改样式不会触发 C++ 重置, 避免 C++ 与蓝图反复打架

	BonusInfoText->SetVisibility(ESlateVisibility::Visible);
	
	// 设置 Border 可见性（如果 Border 存在）
	// 【大厂架构】Padding / BrushColor / 尺寸规则 全部由蓝图控制
	// 原因: 这些是布局属性, 蓝图改样式不会触发 C++ 重置, 避免 C++ 与蓝图反复打架
	if (BonusInfoBorder)
	{
		BonusInfoBorder->SetVisibility(ESlateVisibility::Visible);
	}
	
	// 调试：检查文本是否为空
	if (DisplayText.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("[BONUS_DEBUG] DisplayText为空！"));
	}
	
	// 调试：检查控件的实际可见性
	ESlateVisibility CurrentVisibility = BonusInfoText->GetVisibility();
	UE_LOG(LogTemp, Log, TEXT("[BONUS_DEBUG] BonusInfoText控件设置后的可见性: %d"), (int32)CurrentVisibility);
	
	// 调试：检查控件的大小
	FVector2D WidgetSize = BonusInfoText->GetDesiredSize();
	UE_LOG(LogTemp, Log, TEXT("[BONUS_DEBUG] BonusInfoText控件大小: %.2f x %.2f"), WidgetSize.X, WidgetSize.Y);
	
	// 调试：强制设置文本颜色为红色以便确认是否显示
	// 注意：这需要在蓝图中设置TextBlock的Style来支持颜色修改
	UE_LOG(LogTemp, Log, TEXT("[BONUS_DEBUG] BonusInfoText控件已设置文本并设为可见"));
	
	// 强制刷新布局
	if (BonusInfoBorder)
	{
		BonusInfoBorder->InvalidateLayoutAndVolatility();
	}
	if (BonusInfoText)
	{
		BonusInfoText->InvalidateLayoutAndVolatility();
	}

	// 🔧【v219】启动/重启 Bonus 倒计时 1Hz 定时器
	// - 进入"显示"状态时启动定时器, 让秒数每秒刷新
	// - 调用前已经确保 BonusInfoText->GetVisibility() == Visible (上面的 2980 行的 SetVisibility 已设)
	StartBonusCountdownTimer();
}

// ==========================================
// 【v219 新增】Bonus 倒计时 1Hz Tick 实现
// 大厂原则: 事件 + Tick 双轨, 事件轨 (UpdateBonusInfoText) 负责重算 EndTime / 处理过期;
//          Tick 轨仅在未过期时用最新 CurrentTime 刷新数字. 互不重复.
// ==========================================

/**
 * 启动 1Hz 定时器 (如果已经在跑则先清, 避免叠加)
 * - 只有 BonusInfoText 当前可见且有显式数据时才启动
 * - 零兜底: World 无效 → Log Error + return (不允许静默)
 */
void UDailyUpgradeRewardPage::StartBonusCountdownTimer()
{
	if (!BonusInfoText || BonusInfoText->GetVisibility() != ESlateVisibility::Visible)
	{
		return; // 没有可见的 Bonus, 不需要倒计时
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[v219] StartBonusCountdownTimer: World 无效, 无法启动定时器"));
		return;
	}

	FTimerManager& TimerManager = World->GetTimerManager();

	// 防叠加: 先清后启
	if (TimerManager.IsTimerActive(BonusCountdownTimerHandle))
	{
		TimerManager.ClearTimer(BonusCountdownTimerHandle);
	}

	TimerManager.SetTimer(
		BonusCountdownTimerHandle,
		this,
		&UDailyUpgradeRewardPage::OnBonusCountdownTick,
		/*InRate=*/1.0f,    // 1Hz (秒级精度)
		/*bLoop=*/true);

	UE_LOG(LogTemp, Log,
		TEXT("[v219] StartBonusCountdownTimer: Bonus 倒计时 1Hz 定时器已启动"));
}

void UDailyUpgradeRewardPage::StopBonusCountdownTimer()
{
	if (UWorld* World = GetWorld())
	{
		if (World->GetTimerManager().IsTimerActive(BonusCountdownTimerHandle))
		{
			World->GetTimerManager().ClearTimer(BonusCountdownTimerHandle);
			UE_LOG(LogTemp, Log, TEXT("[v219] StopBonusCountdownTimer: 定时器已停止"));
		}
	}
	// World 无效时不报错 (页面销毁时常见, 直接吞掉)
}

/**
 * 1Hz Tick - 仅刷新秒级倒计时数字 (不重算 EndTime / 不检查过期)
 * 过期检查交给下一次事件轨 (UpdateBonusInfoText) 或手动触发
 * @note 大厂原则: Tick 里不做任何重算 + 不查 DataTable, 只读取已在内存中的 CurrentDayIndex 等少量数据
 */
void UDailyUpgradeRewardPage::OnBonusCountdownTick()
{
	if (!BonusInfoText || !IsValid(BonusInfoText))
	{
		StopBonusCountdownTimer();
		return;
	}

	// 防御: 已经被外部设为隐藏, 直接停止定时器, 避免后台空转
	if (BonusInfoText->GetVisibility() != ESlateVisibility::Visible)
	{
		StopBonusCountdownTimer();
		return;
	}

	UGameInstance* GameInstance = GetGameInstance();
	if (!GameInstance)
	{
		StopBonusCountdownTimer();
		UE_LOG(LogTemp, Error,
			TEXT("[v219] OnBonusCountdownTick: 无法获取GameInstance, 停止定时器"));
		return;
	}

	UUpgradeActivitySubsystem* Subsystem = GameInstance->GetSubsystem<UUpgradeActivitySubsystem>();
	if (!Subsystem)
	{
		StopBonusCountdownTimer();
		UE_LOG(LogTemp, Error,
			TEXT("[v219] OnBonusCountdownTick: 无法获取UpgradeActivitySubsystem, 停止定时器"));
		return;
	}

	// 🔧【v219 优化】Tick 不再走完整 UpdateBonusInfoText (避免重复查 ConfigRow / HasDayDataInMemory / 设置字体等)
	//    只重算"秒级倒计时数字"部分, 用最少的 API 调用刷新文本
	const int32 CurrentDayIndex1Based = CurrentDayIndex + 1; // 0-based → 1-based
	const FString DayIdentifier = FString::Printf(TEXT("day%d"), CurrentDayIndex1Based);

	const FDailyUpgradeRewardConfigRow* ConfigRow = Subsystem->GetConfigRowForDay(DayIdentifier);
	if (!ConfigRow || ConfigRow->BonusDescription.IsEmpty())
	{
		// 配置没了 → 停止定时器 (避免空转报错)
		StopBonusCountdownTimer();
		return;
	}

	const FUpgradeRewardSaveRecord* DayRecord = Subsystem->GetRecordByDate(CurrentDayIndex1Based);
	if (!DayRecord)
	{
		// 内存中没数据 → 隐藏 (走原事件轨逻辑), 停定时器
		BonusInfoText->SetVisibility(ESlateVisibility::Collapsed);
		if (BonusInfoBorder)
		{
			BonusInfoBorder->SetVisibility(ESlateVisibility::Collapsed);
		}
		StopBonusCountdownTimer();
		return;
	}

	const FDateTime EndTime = DayRecord->CreatedTime + FTimespan::FromHours(ConfigRow->BonusDurationHours);
	const FDateTime CurrentTime = FDateTime::Now();

	// 已过期 → 走原事件轨的过期逻辑 (隐藏 + 停定时器)
	if (CurrentTime >= EndTime)
	{
		BonusInfoText->SetVisibility(ESlateVisibility::Collapsed);
		if (BonusInfoBorder)
		{
			BonusInfoBorder->SetVisibility(ESlateVisibility::Collapsed);
		}
		StopBonusCountdownTimer();
		return;
	}

	// 还在有效期内 → 仅刷新倒计时数字, 不重算 EndTime, 不重查 ConfigRow
	const FTimespan RemainingTime = EndTime - CurrentTime;
	const int32 RemainingHours = RemainingTime.GetHours();
	const int32 RemainingMinutes = RemainingTime.GetMinutes() % 60;
	const int32 RemainingSeconds = RemainingTime.GetSeconds() % 60;

	const int32 LimitedActivityCompleteCount = Subsystem->GetLimitedActivityCompleteCount();
	const int32 BonusCount = ConfigRow->BonusCount;

	const FString DisplayText = FString::Printf(TEXT("%s %d/%d 剩余时长：%dH %dM %dS"),
		*ConfigRow->BonusDescription,
		LimitedActivityCompleteCount,
		BonusCount,
		RemainingHours,
		RemainingMinutes,
		RemainingSeconds);

	BonusInfoText->SetText(FText::FromString(DisplayText));
}

// ==========================================
// 【v213.1 新增】任务完成次数 ComboBox 回调实现
// ==========================================

/**
 * 【v213.1 大厂架构】任务完成次数 ComboBox 绑定辅助函数
 * 大厂原则: DRY, 3 个 ComboBox 共用同一逻辑
 * @param ComboBox 目标 ComboBox (可能为 null)
 */
void UDailyUpgradeRewardPage::BindTaskCountComboBox(UComboBoxString* ComboBox)
{
	if (ComboBox)
	{
		// 清空已有项 (防御性: 重复 Initialize 不重复添加)
		ComboBox->ClearOptions();

		// 用户需求: 选项 0-9
		for (int32 i = 0; i <= 9; ++i)
		{
			ComboBox->AddOption(FString::FromInt(i));
		}

		// 默认选中 0
		ComboBox->SetSelectedOption(TEXT("0"));
	}
	else
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[DailyUpgradeRewardPage] Initialize: 任务次数 ComboBox 未绑定 (蓝图缺控件)"));
	}
}

// ==========================================
// 【v213.1 新增】任务完成次数 ComboBox 回调绑定 (在 Initialize 中统一处理)
// ==========================================

/**
 * 【v213.1 大厂架构】绑定任务完成次数 ComboBox 的事件回调
 * 绑定到对应的 OnTaskXCountComboBoxChanged
 */
void UDailyUpgradeRewardPage::BindTaskCountComboBoxCallbacks()
{
	if (ComboBoxString_Task1Count)
	{
		ComboBoxString_Task1Count->OnSelectionChanged.AddDynamic(
			this, &UDailyUpgradeRewardPage::OnTask1CountComboBoxChanged);
	}
	if (ComboBoxString_Task2Count)
	{
		ComboBoxString_Task2Count->OnSelectionChanged.AddDynamic(
			this, &UDailyUpgradeRewardPage::OnTask2CountComboBoxChanged);
	}
	if (ComboBoxString_Task3Count)
	{
		ComboBoxString_Task3Count->OnSelectionChanged.AddDynamic(
			this, &UDailyUpgradeRewardPage::OnTask3CountComboBoxChanged);
	}
}

void UDailyUpgradeRewardPage::OnTask1CountComboBoxChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
	UE_LOG(LogTemp, Verbose,
		TEXT("[DailyUpgradeRewardPage] OnTask1CountComboBoxChanged: SelectedItem='%s', SelectionType=%d (无业务操作)"),
		*SelectedItem, static_cast<int32>(SelectionType));
}

void UDailyUpgradeRewardPage::OnTask2CountComboBoxChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
	UE_LOG(LogTemp, Verbose,
		TEXT("[DailyUpgradeRewardPage] OnTask2CountComboBoxChanged: SelectedItem='%s', SelectionType=%d (无业务操作)"),
		*SelectedItem, static_cast<int32>(SelectionType));
}

void UDailyUpgradeRewardPage::OnTask3CountComboBoxChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
	UE_LOG(LogTemp, Verbose,
		TEXT("[DailyUpgradeRewardPage] OnTask3CountComboBoxChanged: SelectedItem='%s', SelectionType=%d (无业务操作)"),
		*SelectedItem, static_cast<int32>(SelectionType));
}

// ==========================================
// 【v213 新增】调试数据提交控件回调实现
// ==========================================

/**
 * 【v217 大厂架构】ComboBox 选项变化回调
 *
 * 大厂原则:
 *   - Page 仅记录 SelectedDay 字符串, 不在此处解析数字
 *   - ComboBox 已经在 Initialize 中默认选中 "1", 此回调只在用户主动切换时触发
 *   - 解析 + 校验全部委托给 ViewModel.ModifyCurrentExperience
 *
 * 【v217 修复】根因分析 (用户反馈 "每次更新 ComboBoxString_SelectedDay 点击 Button_ApplyDebugValues, 领取状态没重置"):
 *   - Page 的 CurrentDayIndex 在首次 NativeConstruct 时被设成 MaxDay-1 (来自 Subsystem)
 *   - 之前该回调为空 → 用户切换 ComboBox 时, CurrentDayIndex 不变
 *   - 用户以为 ComboBox 切到 day=3 → 实际 Page 内部 CurrentDayIndex 还是 day=2
 *   - 即使 viewmodel 已经把 CurrentRecord 改成了 day=3, Page 任务列表/高亮还在 day=2 上 → 视觉上 "老样子"
 *
 * 【v217 修复】新增逻辑:
 *   1. 解析 SelectedItem 为 SelectedDayNumber
 *   2. 同步更新 CurrentDayIndex + CurrentSelectedDay
 *   3. 调用 OnDayButtonClicked 触发任务详情重建 (复用 day 切换流程)
 *   4. 这样任务列表/TasksContainer/任务高亮全部跟随 ComboBox 切换, UI 与数据一致
 */
void UDailyUpgradeRewardPage::OnDebugDayComboBoxSelectionChanged(FString SelectedItem, ESelectInfo::Type SelectionType)
{
	// 调试日志 (Verbose 而非 Display: 避免刷屏)
	UE_LOG(LogTemp, Verbose,
		TEXT("[DailyUpgradeRewardPage] OnDebugDayComboBoxSelectionChanged: SelectedItem='%s', SelectionType=%d"),
		*SelectedItem, static_cast<int32>(SelectionType));

	// 【零兜底】第 1 层: 入参合法性校验
	if (SelectedItem.IsEmpty() || !SelectedItem.IsNumeric())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[DailyUpgradeRewardPage] OnDebugDayComboBoxSelectionChanged: SelectedItem='%s' 非空数字, 拒绝更新"),
			*SelectedItem);
		return;
	}

	const int32 SelectedDayNumber = FCString::Atoi(*SelectedItem);
	if (SelectedDayNumber < 1 || SelectedDayNumber > 5)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[DailyUpgradeRewardPage] OnDebugDayComboBoxSelectionChanged: SelectedDayNumber=%d 超出合法范围 [1,5], 拒绝更新"),
			SelectedDayNumber);
		return;
	}

	// 【v228 重构 - 大厂架构】ComboBox 仅作为 "待提交控件"
	// 1. ComboBox 选中后, 仅缓存到 PendingSelectedDay (SSOT 单源, 此刻 UI 不变)
	// 2. 不动 CurrentDayIndex / CurrentSelectedDay (DayButtonsContainer 高亮不变)
	// 3. 不动 ItemsScrollBox (ItemsScrollBox 领取进度不变, 避免 Apply 过程中出现 bug)
	// 4. 只有 Button_ApplyDebugValues 确认后, 才把 PendingSelectedDay 推到正式位
	PendingSelectedDay = SelectedDayNumber;

	UE_LOG(LogTemp, Log,
		TEXT("[DailyUpgradeRewardPage] 【v228 待提交快照】 ComboBox 切换: PendingSelectedDay=%d (CurrentDayIndex 仍=%d, UI 未刷新, 等待 Apply 确认)"),
		PendingSelectedDay, CurrentDayIndex);
}

/**
 * 【v213 大厂架构】提交按钮点击回调
 *
 * 流程 (严格 4 步, 无任何兜底):
 *   1. 校验 3 个 UPROPERTY 控件都存在 (零兜底: 任一为 nullptr → Log Error + return)
 *   2. 从 EditableText 读取 NewExp, 用 FCString::Atoi 解析 (解析失败 → Log Error + return)
 *   3. 从 ComboBox 读取 SelectedItem, 用 FCString::Atoi 解析 SelectedDay
 *   4. 委托 ViewModel.ModifyCurrentExperience(SelectedDay, NewExp)
 *      → 业务校验 / SaveModifier 初始化 / Subsystem 写入 都在 ViewModel 内
 *   5. 根据返回值显示反馈 (成功绿/失败红)
 */
void UDailyUpgradeRewardPage::OnApplyDebugValuesClicked()
{
	UE_LOG(LogTemp, Log,
		TEXT("[DailyUpgradeRewardPage] OnApplyDebugValuesClicked: 提交按钮被点击"));

	// 【零兜底】第 1 层: 控件存在性校验
	if (!ViewModel)
	{
		const FString Msg = TEXT("[DailyUpgradeRewardPage] ViewModel 未创建, 拒绝提交");
		UE_LOG(LogTemp, Error, TEXT("%s"), *Msg);
		ShowDebugApplyFeedback(false, Msg);
		return;
	}

	if (!EditableTextInput_NewExp)
	{
		const FString Msg = TEXT("[DailyUpgradeRewardPage] EditableTextInput_NewExp 未绑定, 拒绝提交");
		UE_LOG(LogTemp, Error, TEXT("%s"), *Msg);
		ShowDebugApplyFeedback(false, Msg);
		return;
	}

	if (!ComboBoxString_SelectedDay)
	{
		const FString Msg = TEXT("[DailyUpgradeRewardPage] ComboBoxString_SelectedDay 未绑定, 拒绝提交");
		UE_LOG(LogTemp, Error, TEXT("%s"), *Msg);
		ShowDebugApplyFeedback(false, Msg);
		return;
	}

	// 【v228 SSOT 修 - ComboBox 真源同步】每次 Apply 入口都把 ComboBox 当前选中解析回 PendingSelectedDay
	//   - 大厂原则 (SSOT): ComboBox 的选项范围 1~5 是 BP 配置, 永远不会越界
	//   - 旧反模式: Apply 成功后清空 PendingSelectedDay=-1, 用户连续点 Apply 时因 -1 校验失败而报错
	//     即使 ComboBox 选中合法, 因为 PendingSelectedDay 是缓存字段而不是真源
	//   - 修正: ComboBox 真源 → 每次 Apply 入口主动同步一次到 PendingSelectedDay
	//     (PendingSelectedDay 仍是 ComboBox 的"待提交快照", 但快照必须随 ComboBox 实时更新)
	{
		const FString ComboSelectedStr = ComboBoxString_SelectedDay->GetSelectedOption().TrimStartAndEnd();
		if (ComboSelectedStr.IsEmpty())
		{
			const FString Msg = TEXT("[DailyUpgradeRewardPage] ComboBoxString_SelectedDay 当前未选中任何项 (空字符串), 拒绝提交");
			UE_LOG(LogTemp, Error, TEXT("%s"), *Msg);
			ShowDebugApplyFeedback(false, Msg);
			return;
		}
		if (!ComboSelectedStr.IsNumeric())
		{
			const FString Msg = FString::Printf(TEXT("[DailyUpgradeRewardPage] ComboBox 选中 '%s' 不是合法数字, 拒绝提交"), *ComboSelectedStr);
			UE_LOG(LogTemp, Error, TEXT("%s"), *Msg);
			ShowDebugApplyFeedback(false, Msg);
			return;
		}
		const int32 ComboSelectedDay = FCString::Atoi(*ComboSelectedStr);
		if (ComboSelectedDay < 1 || ComboSelectedDay > 5)
		{
			const FString Msg = FString::Printf(
				TEXT("[DailyUpgradeRewardPage] ComboBox 选中 %d 不在合法范围 [1,5], 请检查 BP ComboBoxString_SelectedDay 配置"), ComboSelectedDay);
			UE_LOG(LogTemp, Error, TEXT("%s"), *Msg);
			ShowDebugApplyFeedback(false, Msg);
			return;
		}
		PendingSelectedDay = ComboSelectedDay;
		UE_LOG(LogTemp, Log,
			TEXT("[DailyUpgradeRewardPage] 【v228 ComboBox 真源同步】 ComboBox 当前选中=%d → PendingSelectedDay=%d (校验通过)"),
			ComboSelectedDay, PendingSelectedDay);
	}

	// 【零兜底】第 2 层: 解析 NewExp
	const FString NewExpStr = EditableTextInput_NewExp->GetText().ToString().TrimStartAndEnd();
	if (NewExpStr.IsEmpty())
	{
		const FString Msg = TEXT("经验值输入为空, 请填写数字 (>=0)");
		UE_LOG(LogTemp, Warning, TEXT("[DailyUpgradeRewardPage] %s"), *Msg);
		ShowDebugApplyFeedback(false, Msg);
		return;
	}

	if (!NewExpStr.IsNumeric())
	{
		const FString Msg = FString::Printf(TEXT("经验值 '%s' 不是合法数字"), *NewExpStr);
		UE_LOG(LogTemp, Error, TEXT("[DailyUpgradeRewardPage] %s"), *Msg);
		ShowDebugApplyFeedback(false, Msg);
		return;
	}

	const int32 NewExp = FCString::Atoi(*NewExpStr);

	// 【v228 重构 - 大厂架构】第 3 层: 读取 PendingSelectedDay (v228 新增字段)
	// 1. PendingSelectedDay 是 ComboBox 选中后的 "待提交快照", 它**不等于** Page 当前的 CurrentDayIndex
	// 2. ComboBox 切换时 Page UI 不变 (DayButtonsContainer 高亮 / ItemsScrollBox 全部不动)
	// 3. 只有 Apply 时, PendingSelectedDay 才推送到 CurrentDayIndex/CurrentSelectedDay
	// 4. 【零兜底】PendingSelectedDay 必须是合法值; 否则 Log Error + return
	const int32 SelectedDay = PendingSelectedDay;
	if (SelectedDay < 1 || SelectedDay > 5)
	{
		const FString Msg = FString::Printf(
			TEXT("PendingSelectedDay=%d 不在合法范围 [1,5], 必须先在 ComboBox 选中 1~5 中的某一天"), SelectedDay);
		UE_LOG(LogTemp, Error, TEXT("[DailyUpgradeRewardPage] %s"), *Msg);
		ShowDebugApplyFeedback(false, Msg);
		return;
	}

	// ==========================================
	// 【v213.1 新增】解析 3 个任务完成次数
	// 大厂原则:
	//   - 零兜底: 控件 null → Log Error + return
	//   - ComboBox 约束 0-9, C++ 仍重校验 (防止 BP 绕过)
	// ==========================================
	int32 TaskCounts[3] = {0, 0, 0};
	UComboBoxString* TaskCombos[3] = {
		ComboBoxString_Task1Count,
		ComboBoxString_Task2Count,
		ComboBoxString_Task3Count
	};
	const TCHAR* TaskNames[3] = {TEXT("Task1Count"), TEXT("Task2Count"), TEXT("Task3Count")};

	for (int32 i = 0; i < 3; ++i)
	{
		if (!TaskCombos[i])
		{
			const FString Msg = FString::Printf(
				TEXT("[DailyUpgradeRewardPage] ComboBoxString_%s 未绑定, 拒绝提交"), TaskNames[i]);
			UE_LOG(LogTemp, Error, TEXT("%s"), *Msg);
			ShowDebugApplyFeedback(false, Msg);
			return;
		}

		const FString CountStr = TaskCombos[i]->GetSelectedOption();
		if (CountStr.IsEmpty())
		{
			const FString Msg = FString::Printf(
				TEXT("任务%d完成次数未选择, 请从下拉框选0~9"), i + 1);
			UE_LOG(LogTemp, Warning, TEXT("[DailyUpgradeRewardPage] %s"), *Msg);
			ShowDebugApplyFeedback(false, Msg);
			return;
		}

		if (!CountStr.IsNumeric())
		{
			const FString Msg = FString::Printf(
				TEXT("任务%d完成次数 '%s' 不是合法数字"), i + 1, *CountStr);
			UE_LOG(LogTemp, Error, TEXT("[DailyUpgradeRewardPage] %s"), *Msg);
			ShowDebugApplyFeedback(false, Msg);
			return;
		}

		const int32 CountVal = FCString::Atoi(*CountStr);
		if (CountVal < 0 || CountVal > 9)
		{
			const FString Msg = FString::Printf(
				TEXT("任务%d完成次数 %d 超出合法范围 [0,9]"), i + 1, CountVal);
			UE_LOG(LogTemp, Error, TEXT("[DailyUpgradeRewardPage] %s"), *Msg);
			ShowDebugApplyFeedback(false, Msg);
			return;
		}

		TaskCounts[i] = CountVal;
	}

	// 【v228 重构 - 大厂架构】第 4 层: 先把 PendingSelectedDay 推到正式位, 再调 ViewModel
	// 大厂原则 (UI 与数据同步):
	//   1. Apply 是 "提交并切换" 的合并点, 而不是 "只提交不切换"
	//   2. Apply 时先把 PendingSelectedDay 推到 CurrentDayIndex/CurrentSelectedDay
	//   3. 调用 OnDayButtonClicked 重建: DayButtonsContainer 高亮 + ItemsScrollBox 重建 (不丢 Claim)
	//   4. ItemsScrollBox 重建后, 领取进度仍由 Subsystem.ChestClaimStatus 决定 (v228 不再重置)
	//   5. 最后 ViewModel.ModifyCurrentExperience/ModifyAllTaskCounts 修改数据 + SaveModifier 落盘
	const int32 NewDayIndex = SelectedDay - 1; // 0-based
	const bool bDayChanged = (NewDayIndex != CurrentDayIndex);

	if (bDayChanged)
	{
		CurrentDayIndex = NewDayIndex;
		CurrentSelectedDay = SelectedDay;
		const FString DayIdentifier = FString::Printf(TEXT("day%d"), SelectedDay);
		UE_LOG(LogTemp, Log,
			TEXT("[DailyUpgradeRewardPage] 【v228 正式切换】 Apply 触发: PendingSelectedDay=%d → CurrentDayIndex=%d (0-based), CurrentSelectedDay=%d, DayIdentifier='%s'"),
			PendingSelectedDay, CurrentDayIndex, CurrentSelectedDay, *DayIdentifier);

		// 调用 OnDayButtonClicked 触发 DayButtonsContainer 高亮 + TasksContainer + 任务详情 widget 重建
		OnDayButtonClicked(DayIdentifier, NewDayIndex);
	}
	else
	{
		UE_LOG(LogTemp, Log,
			TEXT("[DailyUpgradeRewardPage] 【v228 同日 Apply】 SelectedDay=%d 与 CurrentDayIndex=%d 相同, 跳过重建"),
			SelectedDay, CurrentDayIndex);
	}

	// 委托 ViewModel → Subsystem 写入经验值 + 3 个任务次数
	const bool bExpSuccess = ViewModel->ModifyCurrentExperience(SelectedDay, NewExp);
	const bool bTaskSuccess = ViewModel->ModifyAllTaskCounts(SelectedDay, TaskCounts[0], TaskCounts[1], TaskCounts[2]);
	const bool bSuccess = bExpSuccess && bTaskSuccess;

	// 【v228 反模式移除】不再清空 PendingSelectedDay = -1
	//   - 旧反模式: 清空后, 用户连续按 Apply 时 PendingSelectedDay=-1 触发校验失败, 而 ComboBox 实际选中合法
	//   - 修正: PendingSelectedDay 在 Apply 入口已由 ComboBox 真源同步, Apply 完成后保持合法值
	//   - 下次 Apply 入口会再次重新同步 (用户可换 ComboBox 选项, 也可不换, 都正确)

	// 第 5 层: 显示反馈 (绿/红)
	if (bSuccess)
	{
		const FString Msg = FString::Printf(
			TEXT("[成功] SelectedDay=%d, NewExp=%d, Task1=%d, Task2=%d, Task3=%d 已写入 (UI 自动刷新)"),
			SelectedDay, NewExp, TaskCounts[0], TaskCounts[1], TaskCounts[2]);
		ShowDebugApplyFeedback(true, Msg);
	}
	else
	{
		// ViewModel 内部已 Log Error, 这里仅展示简短反馈
		ShowDebugApplyFeedback(false,
			FString::Printf(TEXT("[失败] 请查看 Output Log")));
	}
}


/**
 * 【v222 新增】一键重置按钮点击回调
 *
 * 大厂原则 (分层):
 *   - Page 不直接调 Subsystem (那是 ViewModel 的事)
 *   - Page 仅通过 ViewModel.ResetAllActivityProgress() 委托
 *   - 复位成功后回写 3 个任务 ComboBox 默认 "0" (避免 UI 与重建后的 day1 数据不一致)
 *
 * 大厂原则 (零兜底):
 *   - ViewModel == null → Log Error + 屏幕红字 + return
 *   - ViewModel 内部失败 → 屏幕红字 + return (具体原因 ViewModel 已 Log)
 *
 * 大厂原则 (职责分工):
 *   - 没有确认弹窗 (用户明确要求)
 *   - 成功反馈用 ShowDebugApplyFeedback (绿色) 而不是新增控件
 *   - Subsystem 内部已 Broadcast OnGlobalRefresh, UI 自动刷新 (无需手动 Refresh)
 */
void UDailyUpgradeRewardPage::OnResetAllActivityClicked()
{
	UE_LOG(LogTemp, Log,
		TEXT("[DailyUpgradeRewardPage] OnResetAllActivityClicked: 一键重置按钮被点击"));

	// 【零兜底】第 1 层: ViewModel 存在性
	if (!ViewModel)
	{
		const FString Msg = TEXT("[DailyUpgradeRewardPage] ViewModel 未创建, 拒绝重置");
		UE_LOG(LogTemp, Error, TEXT("%s"), *Msg);
		ShowDebugApplyFeedback(false, Msg);
		return;
	}

	// 委托 ViewModel → Subsystem 执行真正的重置 (清内存 + 落盘 + 重建 day1 + Broadcast)
	const bool bSuccess = ViewModel->ResetAllActivityProgress();
	if (bSuccess)
	{
		// 大厂原则 (职责分工): UI 复位 ComboBox 默认值, 避免与 day1 重建后的数据漂移
		//   注: Subsystem 已 Broadcast OnGlobalRefresh → 触发 Page RefreshUI → ComboBox_SelectedDay 重新填充
		//   但 3 个任务 ComboBox 不在 Refresh 范围 (用户原设计: 不随日切换刷新, 保留上次)
		//   重置是"全部清空" → 必须显式回写为 "0", 否则 UI 显示的是重置前最后的"7"之类
		if (ComboBoxString_Task1Count)
		{
			ComboBoxString_Task1Count->SetSelectedOption(TEXT("0"));
		}
		if (ComboBoxString_Task2Count)
		{
			ComboBoxString_Task2Count->SetSelectedOption(TEXT("0"));
		}
		if (ComboBoxString_Task3Count)
		{
			ComboBoxString_Task3Count->SetSelectedOption(TEXT("0"));
		}

		ShowDebugApplyFeedback(true,
			FString::Printf(TEXT("[成功] 活动进度已重置 (清空所有 day + 落盘 + 重建 day1)")));
	}
	else
	{
		// ViewModel/Subsystem 内部已 Log Error (落盘失败 / day1 Config 缺失)
		ShowDebugApplyFeedback(false,
			FString::Printf(TEXT("[失败] 重置失败, 请查看 Output Log")));
	}
}

/**
 * 【v213 大厂架构】屏幕反馈 (不新增控件)
 * 大厂原则:
 *   - 用户没要新提示控件, 用 GEngine->AddOnScreenDebugMessage
 *   - 仅在 PIE/Game 有效 (Editor 预览模式 GEngine 仍可用, 但 World=null)
 *   - 调试用途, 不写入 SaveGame
 */
void UDailyUpgradeRewardPage::ShowDebugApplyFeedback(bool bSuccess, const FString& Message)
{
	if (!GEngine)
	{
		return;
	}

	// 绿色 = 成功; 红色 = 失败; 持续 5 秒
	const FColor MsgColor = bSuccess ? FColor::Green : FColor::Red;
	const uint64 UniqueId = static_cast<uint64>(reinterpret_cast<UPTRINT>(this)) + GetUniqueID();

	GEngine->AddOnScreenDebugMessage(
		UniqueId,                          // Key (覆盖之前的同 Key 消息, 防止刷屏)
		5.0f,                              // 显示时长 (5 秒)
		MsgColor,                          // 颜色
		Message                            // 文本
	);

	UE_LOG(LogTemp, Log, TEXT("[DailyUpgradeRewardPage] %s"), *Message);
}
