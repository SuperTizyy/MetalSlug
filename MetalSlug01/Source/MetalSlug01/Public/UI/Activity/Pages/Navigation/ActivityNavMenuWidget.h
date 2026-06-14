#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/Activity/Data/DailyLoginSave.h"  // 包含整合后的FActivityNavItem
#include "ActivityNavButton.h"      // 包含新的导航按钮Widget
#include "UI/Activity/Pages/SelectMultiplePopup/ActivityConfirmPopupWidget.h"  // 包含新的确认弹窗Widget
#include "ActivityNavMenuWidget.generated.h"

// 注意：FActivityNavItem结构体现在已在DailyLoginSave.h中统一管理

/**
 * @brief 活动导航菜单组件
 * 实现图片中红色框出的列表功能
 */


UCLASS(BlueprintType)
class METALSLUG01_API UActivityNavMenuWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// ==================== 蓝图绑定组件 ====================
	
	/** 导航容器 - 在蓝图中绑定VerticalBox或ScrollBox */
	UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
	class UPanelWidget* NavContainer;
	
	/** 页面显示容器 - 在蓝图中绑定用于显示活动页面的面板 */
	UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
	class UPanelWidget* PageContainer;

	/** 返回游戏菜单按钮 - 在蓝图中绑定返回按钮控件 */
	UPROPERTY(meta = (BindWidget))
	class UButton* Btn_BackToMenu;
	
	/** 默认页面类 - 当无法找到特定页面时使用 */
	UPROPERTY(EditAnywhere, Category = "PageManagement")
	TSubclassOf<UUserWidget> DefaultPageClass;
	

	// ==================== 数据属性 ====================
	
	UPROPERTY(BlueprintReadOnly, Category = "Navigation")
	TArray<FActivityNavItem> NavItems;
	
	UPROPERTY(BlueprintReadOnly, Category = "Navigation")
	FName CurrentSelectedActivity;

	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnNavItemSelected, FName, SelectedActivityId);
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnNavigateToPage, FName, ActivityId, TSubclassOf<UUserWidget>, TargetPageClass);
	
	UPROPERTY(BlueprintAssignable, Category = "Events")
	FOnNavItemSelected OnNavItemSelected;
	
	UPROPERTY(BlueprintAssignable, Category = "Events")
	FOnNavigateToPage OnNavigateToPage;

	UFUNCTION(BlueprintCallable, Category = "Navigation")
	void InitializeNavigation();
	
	UFUNCTION(BlueprintCallable, Category = "Navigation")
	void SetSelectedActivity(FName ActivityId);
	
	/**
	 * @brief 从ActivitySubsystem填充导航项数据
	 * @note 自动获取所有可用的活动作为导航项
	 */
	void PopulateNavItemsFromSubsystem();
	
	/**
	 * @brief 直接从DataTable加载导航项数据（备用方案）
	 * @note 当Subsystem不可用时使用
	 */
	void LoadNavItemsFromDataTable();
	
	/**
	 * @brief 创建测试数据（临时方案）
	 * @note 用于调试和验证功能
	 */
	void CreateTestData();
	
	/**
	 * @brief 直接从DataTable加载所有活动项（新方案）
	 * @note 扫描ActivityInfoRow表有多少行就显示多少个导航项
	 */
	void LoadAllActivityItemsFromDataTable();
	
	/**
	 * @brief 获取活动显示名称
	 * @param ActivityId 活动ID
	 * @return 活动显示名称
	 */
	FText GetActivityDisplayName(FName ActivityId);
	
	/**
	 * @brief 获取默认选中的活动ID
	 * @return 默认选中的ActivityId，如果没有则返回NAME_None
	 */
	FName GetDefaultSelectedActivityId();
	
	/**
	 * @brief 更新所有按钮的视觉状态
	 * @note 根据NavItems中的选中状态同步更新按钮显示
	 */
	void UpdateAllButtonsVisualState();
	
	// ==================== 页面管理函数 ====================
	
	/**
	 * @brief 切换到指定活动页面
	 * @param ActivityId 活动ID
	 * @note 根据ActivityId加载对应的页面Widget并显示
	 */
	UFUNCTION(BlueprintCallable, Category = "PageManagement")
	void SwitchToActivityPage(FName ActivityId);
	
	/**
	 * @brief 创建活动页面实例
	 * @param PageClass 页面Widget类
	 * @return 创建的页面实例
	 * @note 负责页面的实例化和初始化
	 */
	UFUNCTION(BlueprintCallable, Category = "PageManagement")
	class UUserWidget* CreateActivityPage(TSubclassOf<UUserWidget> PageClass);
	
	/**
	 * @brief 显示页面在PageContainer中
	 * @param PageWidget 要显示的页面Widget
	 * @note 负责页面的添加和显示逻辑
	 */
	UFUNCTION(BlueprintCallable, Category = "PageManagement")
	void ShowPageInContainer(class UUserWidget* PageWidget);
	
	/**
	 * @brief 清理当前显示的页面
	 * @note 移除当前页面并重置状态
	 */
	UFUNCTION(BlueprintCallable, Category = "PageManagement")
	void ClearCurrentPage();
	
	// 红点相关函数
	UFUNCTION(BlueprintCallable, Category = "RedDot")
	void RefreshRedDots();
	
	UFUNCTION(BlueprintCallable, Category = "RedDot")
	void UpdateNavItemRedDot(FName ActivityId, const FRedDotData& RedDotData);
	
	// 时间相关函数
	UFUNCTION(BlueprintCallable, Category = "TimeInfo")
	void RefreshTimeInfos();
	
	UFUNCTION(BlueprintCallable, Category = "TimeInfo")
	void UpdateNavItemTimeInfo(FName ActivityId, const FActivityInfoRow& TimeInfo);
	


protected:
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;

private:
	
	// ==================== 页面管理成员变量 ====================
	
	/** 当前显示的页面实例 */
	UPROPERTY()
	TWeakObjectPtr<UUserWidget> CurrentPage;
	
	/** 页面缓存 - 避免重复创建相同页面 */
	UPROPERTY()
	TMap<FName, TWeakObjectPtr<UUserWidget>> PageCache;
	
	// ==================== 红点管理成员变量 ====================
	
	/** 红点刷新定时器句柄 */
	FTimerHandle RedDotRefreshTimerHandle;
	


	UFUNCTION()
	class UActivityNavButton* CreateNavItemButton(const FActivityNavItem& Item);

	UFUNCTION()
	void OnNavItemClicked(class UButton* Button, FName ActivityId);
	
	// 新增：按钮点击处理函数
	UFUNCTION()
	void OnButtonClicked();
	
	// 新增：带参数的按钮点击处理函数
	UFUNCTION()
	void OnButtonClickedWithId(FName ActivityId);
	
	// 新增：针对不同索引的按钮点击处理函数
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

	/** 点击返回游戏菜单按钮触发 - 销毁当前活动页面，返回主菜单 */
	UFUNCTION()
	void OnBackToMenuClicked();
};