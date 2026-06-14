// 版权声明：在项目设置的描述页面填写您的版权信息。

#pragma once

// ==========================================
// 头文件包含说明
// ==========================================
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/Activity/Data/DailyLoginSave.h"  // 包含整合后的 FActivityNavItem
#include "ActivityNavButton.h"      // 包含新的导航按钮 Widget
#include "UI/Activity/Pages/SelectMultiplePopup/ActivityConfirmPopupWidget.h"  // 包含新的确认弹窗 Widget
#include "ActivityNavMenuWidget.generated.h"

// 注意: FActivityNavItem 结构体现在已在 DailyLoginSave.h 中统一管理


/**
 * @class UActivityNavMenuWidget
 * @brief 活动导航菜单组件
 *
 * 职责说明:
 * - 实现活动中心左侧导航栏
 * - 包含: 导航容器 + 页面容器 + 返回按钮
 * - 动态创建 UActivityNavButton 子项
 * - 页面缓存 + 切换
 * - 30 秒定时刷新红点
 *
 * 架构理念:
 * 1. 页面缓存: TMap<FName, TWeakObjectPtr<UUserWidget>> 避免重复创建
 * 2. 数据驱动: DT_ActivityInfoRow 是单一数据源
 * 3. 双委托: OnNavItemSelected / OnNavigateToPage
 * 4. 状态机: SetSelectedActivity 维护选中状态
 * 5. 防御性: 多级降级方案（DataTable 找不到 / Subsystem 缺失 / 测试数据）
 * 6. 生命周期: 30s 定时器, NativeDestruct 清理
 *
 * 关联:
 * - 子项: UActivityNavButton
 * - 数据: UActivitySubsystem + DT_ActivityInfoRow
 * - 页面: 通过 TargetPageClass 动态创建
 */
UCLASS(BlueprintType)
class METALSLUG01_API UActivityNavMenuWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// ==========================================
	// 1. 蓝图绑定组件
	// ==========================================

	/**
	 * 导航容器
	 * 在蓝图中绑定 VerticalBox 或 ScrollBox
	 */
	UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
	class UPanelWidget* NavContainer;

	/**
	 * 页面显示容器
	 * 在蓝图中绑定用于显示活动页面的面板
	 */
	UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
	class UPanelWidget* PageContainer;

	/** 返回游戏菜单按钮（在蓝图中绑定返回按钮控件） */
	UPROPERTY(meta = (BindWidget))
	class UButton* Btn_BackToMenu;

	/** 默认页面类 - 当无法找到特定页面时使用 */
	UPROPERTY(EditAnywhere, Category = "PageManagement")
	TSubclassOf<UUserWidget> DefaultPageClass;

	// ==========================================
	// 2. 数据属性
	// ==========================================

	/** 导航项数据 */
	UPROPERTY(BlueprintReadOnly, Category = "Navigation")
	TArray<FActivityNavItem> NavItems;

	/** 当前选中的活动 ID */
	UPROPERTY(BlueprintReadOnly, Category = "Navigation")
	FName CurrentSelectedActivity;

	// ==========================================
	// 3. 事件委托
	// ==========================================

	/**
	 * @delegate FOnNavItemSelected
	 * @brief 导航项被选中
	 */
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnNavItemSelected, FName, SelectedActivityId);

	/**
	 * @delegate FOnNavigateToPage
	 * @brief 导航到指定页面
	 * @param ActivityId 活动 ID
	 * @param TargetPageClass 目标页面类
	 */
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnNavigateToPage, FName, ActivityId, TSubclassOf<UUserWidget>, TargetPageClass);

	/** 导航项被选中（外层订阅） */
	UPROPERTY(BlueprintAssignable, Category = "Events")
	FOnNavItemSelected OnNavItemSelected;

	/** 导航到指定页面 */
	UPROPERTY(BlueprintAssignable, Category = "Events")
	FOnNavigateToPage OnNavigateToPage;

	// ==========================================
	// 4. 初始化 / 选中
	// ==========================================

	/**
	 * 初始化导航菜单
	 * 1. 检查 NavContainer/PageContainer/DefaultPageClass
	 * 2. PopulateNavItemsFromSubsystem（数据源）
	 * 3. 清空 NavContainer, 创建导航按钮
	 * 4. 设置默认选中 + 切换页面
	 * 5. 刷新红点
	 */
	UFUNCTION(BlueprintCallable, Category = "Navigation")
	void InitializeNavigation();

	/**
	 * 设置选中的活动
	 * 1. 更新 CurrentSelectedActivity
	 * 2. 更新 NavItems 中的 bIsSelected
	 * 3. UpdateAllButtonsVisualState
	 */
	UFUNCTION(BlueprintCallable, Category = "Navigation")
	void SetSelectedActivity(FName ActivityId);

	// ==========================================
	// 5. 数据填充（多种数据源）
	// ==========================================

	/**
	 * 从 ActivitySubsystem 填充导航项数据
	 * @note 自动获取所有可用的活动作为导航项
	 */
	void PopulateNavItemsFromSubsystem();

	/**
	 * 直接从 DataTable 加载导航项数据（备用方案）
	 * @note 当 Subsystem 不可用时使用
	 */
	void LoadNavItemsFromDataTable();

	/**
	 * 直接从 DataTable 加载所有活动项（新方案）
	 * @note 扫描 ActivityInfoRow 表有多少行就显示多少个导航项
	 */
	void LoadAllActivityItemsFromDataTable();

	/**
	 * 创建测试数据（临时方案）
	 * @note 用于调试和验证功能
	 */
	void CreateTestData();

	/**
	 * 获取活动显示名称
	 * @param ActivityId 活动 ID
	 * @return 活动显示名称
	 */
	FText GetActivityDisplayName(FName ActivityId);

	/**
	 * 获取默认选中的活动 ID
	 * @return 默认选中的 ActivityId, 如果没有则返回 NAME_None
	 */
	FName GetDefaultSelectedActivityId();

	/**
	 * 更新所有按钮的视觉状态
	 * @note 根据 NavItems 中的选中状态同步更新按钮显示
	 */
	void UpdateAllButtonsVisualState();

	// ==========================================
	// 6. 页面管理
	// ==========================================

	/**
	 * 切换到指定活动页面
	 * @param ActivityId 活动 ID
	 * @note 根据 ActivityId 加载对应的页面 Widget 并显示
	 */
	UFUNCTION(BlueprintCallable, Category = "PageManagement")
	void SwitchToActivityPage(FName ActivityId);

	/**
	 * 创建活动页面实例
	 * @param PageClass 页面 Widget 类
	 * @return 创建的页面实例
	 * @note 负责页面的实例化和初始化
	 */
	UFUNCTION(BlueprintCallable, Category = "PageManagement")
	class UUserWidget* CreateActivityPage(TSubclassOf<UUserWidget> PageClass);

	/**
	 * 显示页面在 PageContainer 中
	 * @param PageWidget 要显示的页面 Widget
	 * @note 负责页面的添加和显示逻辑
	 */
	UFUNCTION(BlueprintCallable, Category = "PageManagement")
	void ShowPageInContainer(class UUserWidget* PageWidget);

	/**
	 * 清理当前显示的页面
	 * @note 移除当前页面并重置状态
	 */
	UFUNCTION(BlueprintCallable, Category = "PageManagement")
	void ClearCurrentPage();

	// ==========================================
	// 7. 红点
	// ==========================================

	/**
	 * 刷新所有红点状态
	 * 1. 调 URedDotManager->RefreshAllRedDots
	 * 2. 遍历 NavContainer 子项更新红点
	 */
	UFUNCTION(BlueprintCallable, Category = "RedDot")
	void RefreshRedDots();

	/**
	 * 更新指定导航项的红点
	 * @param ActivityId 活动 ID
	 * @param RedDotData 红点数据
	 */
	UFUNCTION(BlueprintCallable, Category = "RedDot")
	void UpdateNavItemRedDot(FName ActivityId, const FRedDotData& RedDotData);

	// ==========================================
	// 8. 时间信息
	// ==========================================

	/** 刷新所有导航项的时间信息 */
	UFUNCTION(BlueprintCallable, Category = "TimeInfo")
	void RefreshTimeInfos();

	/**
	 * 更新指定导航项的时间信息
	 */
	UFUNCTION(BlueprintCallable, Category = "TimeInfo")
	void UpdateNavItemTimeInfo(FName ActivityId, const FActivityInfoRow& TimeInfo);

protected:
	// ==========================================
	// 9. 生命周期
	// ==========================================

	/**
	 * Native 构造
	 * 1. InitializeNavigation
	 * 2. 绑定 Btn_BackToMenu
	 * 3. 启动 30s 红点刷新定时器
	 */
	virtual void NativeConstruct() override;

	/**
	 * Native 析构
	 * 1. 清理红点定时器
	 * 2. 清理 PageCache（RemoveFromParent）
	 * 3. 清理 CurrentPage
	 */
	virtual void NativeDestruct() override;

private:
	// ==========================================
	// 10. 私有成员
	// ==========================================

	/** 当前显示的页面实例（弱引用） */
	UPROPERTY()
	TWeakObjectPtr<UUserWidget> CurrentPage;

	/**
	 * 页面缓存 - 避免重复创建相同页面
	 * Key: ActivityId, Value: Page WeakPtr
	 */
	UPROPERTY()
	TMap<FName, TWeakObjectPtr<UUserWidget>> PageCache;

	/** 红点刷新定时器句柄（30s 周期） */
	FTimerHandle RedDotRefreshTimerHandle;

	// ==========================================
	// 11. 内部回调
	// ==========================================

	/**
	 * 创建单个导航按钮 Widget
	 * @param Item 导航项数据
	 * @return 创建的 UActivityNavButton
	 */
	UFUNCTION()
	class UActivityNavButton* CreateNavItemButton(const FActivityNavItem& Item);

	/**
	 * 旧式按钮点击处理（备用, 当前用 lambda）
	 */
	UFUNCTION()
	void OnNavItemClicked(class UButton* Button, FName ActivityId);

	/** 基础按钮点击处理函数 */
	UFUNCTION()
	void OnButtonClicked();

	/** 带 ActivityId 的按钮点击处理函数 */
	UFUNCTION()
	void OnButtonClickedWithId(FName ActivityId);

	// ==========================================
	// 索引点击处理（多索引支持, 备用）
	// ==========================================
	UFUNCTION()
	void OnButtonClicked_Id0();
	UFUNCTION()
	void OnButtonClicked_Id1();
	UFUNCTION()
	void OnButtonClicked_Id2();
	UFUNCTION()
	void OnButtonClicked_Id3();
	UFUNCTION()
	void OnButtonClicked_Default();

	/**
	 * 点击返回游戏菜单按钮触发
	 * 1. 禁用按钮（防连点）
	 * 2. 清理 PageCache + CurrentPage
	 * 3. 清理红点定时器
	 * 4. RemoveFromParent 销毁自身
	 */
	UFUNCTION()
	void OnBackToMenuClicked();
};
