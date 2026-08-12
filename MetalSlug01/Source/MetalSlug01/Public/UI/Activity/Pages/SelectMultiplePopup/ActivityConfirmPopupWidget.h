// 版权声明：在项目设置的描述页面填写您的版权信息。

#pragma once

// ==========================================
// 头文件包含说明
// ==========================================
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ActivityConfirmPopupWidget.generated.h"

// 前置声明
class URewardOptionCardWidget;

/**
 * @delegate FOnRewardConfirmed
 * @brief 玩家确认奖励选择时广播
 * @param DayIndex 天数索引
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnRewardConfirmed, int32, DayIndex);


/**
 * @class UActivityConfirmPopupWidget
 * @brief 活动奖励确认弹窗
 *
 * 职责说明:
 * - 标准的活动奖励领取确认界面
 * - 包含关闭按钮 + 确认按钮 + 水平布局容器
 * - 动态创建 3 张奖励选项卡片
 *
 * 架构理念:
 * 1. 双层委托: 卡片选中通过 OnRewardCardSelected 广播
 * 2. PendingSelectedIndex: 跟踪用户当前选择但尚未确认的索引
 * 3. 复用: 各活动可共用此弹窗
 * 4. 防御性: 防御卡片创建/绑定失败
 *
 * 关联:
 * - 上级: UDailyLoginPage, UActivityNavMenuWidget 等
 * - 卡片: URewardOptionCardWidget
 */
UCLASS(ClassGroup=(Custom), BlueprintType)
class METALSLUG01_API UActivityConfirmPopupWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// ==========================================
	// 1. UI 组件
	// ==========================================

	/** 关闭弹窗按钮（放置在弹窗右上角） */
	UPROPERTY(BlueprintReadWrite, meta = (BindWidget), Category = "UI Components")
	class UButton* CloseButton;

	/** 确认领取按钮（放置在弹窗底部） */
	UPROPERTY(BlueprintReadWrite, meta = (BindWidget), Category = "UI Components")
	class UButton* ConfirmButton;

	/** 水平布局容器（用于放置奖励选项） */
	UPROPERTY(BlueprintReadWrite, meta = (BindWidget), Category = "UI Components")
	class UHorizontalBox* RewardOptionsContainer;

	/** 奖励选项卡片类（用于动态创建奖励卡片） */
	UPROPERTY(EditAnywhere, Category = "Reward Cards")
	TSubclassOf<class URewardOptionCardWidget> RewardOptionCardClass;

	// ==========================================
	// 2. 公共接口
	// ==========================================

	/**
	 * 初始化弹窗组件
	 * @param InRewardOptions 奖励选项数据数组
	 * @param InSelectedIndex 默认选中索引
	 * @param InDayIndex 当前处理的天数（用于回调）
	 * @note 在蓝图 Construct 事件中调用此函数进行初始化
	 */
	UFUNCTION(BlueprintCallable, Category = "Activity Popup")
	void InitializePopup(const TArray<struct FDailyLoginConfigRow>& InRewardOptions, int32 InSelectedIndex = 1, int32 InDayIndex = 0);

	/**
	 * 设置当前选中索引
	 * @param Index 选中索引值(0-2)
	 * @note 用于更新选中状态的视觉反馈
	 */
	UFUNCTION(BlueprintCallable, Category = "Activity Popup")
	void SetSelectedIndex(int32 Index);

	/**
	 * 获取当前选中索引
	 */
	UFUNCTION(BlueprintPure, Category = "Activity Popup")
	int32 GetSelectedIndex() const { return SelectedIndex; }

	/**
	 * 领取完成回调（供外部订阅）
	 * 用途: DailyLoginPage 订阅此事件来处理奖励领取和刷新
	 */
	UPROPERTY(BlueprintAssignable, Category = "Activity Popup|Events")
	FOnRewardConfirmed OnRewardConfirmed;

protected:
	// ==========================================
	// 3. 生命周期
	// ==========================================

	/**
	 * 组件构造完成回调
	 * 1. 绑定 CloseButton / ConfirmButton
	 * 2. 默认状态初始化
	 */
	virtual void NativeConstruct() override;

private:
	// ==========================================
	// 4. 私有成员
	// ==========================================

	/** 当前选中索引 */
	UPROPERTY()
	int32 SelectedIndex = 1;

	/**
	 * 临时选中状态跟踪
	 * 用途: 记录用户当前选择但尚未确认的索引
	 */
	UPROPERTY()
	int32 PendingSelectedIndex = -1;

	/** 奖励选项数据 */
	UPROPERTY()
	TArray<struct FDailyLoginConfigRow> RewardOptions;

	/** 当前处理的天数（用于回调时传递） */
	UPROPERTY()
	int32 CurrentDayIndex = 0;

	// ==========================================
	// 5. 私有辅助
	// ==========================================

	/**
	 * 为指定的宝箱配置创建多个奖励卡片
	 * @param Config 宝箱配置数据
	 * @note 直接将创建的卡片添加到 RewardOptionsContainer 中
	 */
	void CreateRewardCardsForBox(const struct FDailyLoginConfigRow& Config);

	/**
	 * 创建奖励卡片 UI 元素
	 * @param Config 奖励配置数据
	 * @param Index 卡片索引
	 * @return 创建的 UI 组件
	 */
	class UWidget* CreateRewardCard(const struct FDailyLoginConfigRow& Config, int32 Index);

	// ==========================================
	// 6. 内部回调
	// ==========================================

	/** 关闭按钮点击事件处理 */
	UFUNCTION()
	void OnCloseClicked();

	/** 确认按钮点击事件处理 */
	UFUNCTION()
	void OnConfirmClicked();

	/** 奖励卡片 1 点击事件 */
	UFUNCTION()
	void OnRewardCardClicked_0();

	/** 奖励卡片 2 点击事件 */
	UFUNCTION()
	void OnRewardCardClicked_1();

	/** 奖励卡片 3 点击事件 */
	UFUNCTION()
	void OnRewardCardClicked_2();

	/**
	 * 内部奖励卡片点击处理函数
	 * @param Index 被点击的卡片索引
	 */
	void InternalOnRewardCardClicked(int32 Index);

	/**
	 * 奖励卡片选中状态改变回调
	 * @param CardWidget 卡片 Widget
	 * @param bIsChecked 是否选中
	 */
	UFUNCTION()
	void OnRewardCardSelected(class URewardOptionCardWidget* CardWidget, bool bIsChecked);
};
