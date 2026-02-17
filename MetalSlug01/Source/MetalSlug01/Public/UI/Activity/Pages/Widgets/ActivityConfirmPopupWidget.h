#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ActivityConfirmPopupWidget.generated.h"

// 前置声明
class URewardOptionCardWidget;

/**
 * @brief 活动奖励确认弹窗 - 实现标准的活动奖励领取确认界面
 * @author AI Assistant
 * @date 2024
 * @note 严格按照UE工业级编程规范开发，支持蓝图绑定和事件处理
 */
UCLASS(ClassGroup=(Custom), BlueprintType)
class METALSLUG01_API UActivityConfirmPopupWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// ==================== 蓝图绑定组件 ====================
	
	/** 关闭弹窗按钮 - 放置在弹窗右上角 */
	UPROPERTY(BlueprintReadWrite, meta = (BindWidget), Category = "UI Components")
	class UButton* CloseButton;

	/** 确认领取按钮 - 放置在弹窗底部 */
	UPROPERTY(BlueprintReadWrite, meta = (BindWidget), Category = "UI Components")
	class UButton* ConfirmButton;

	/** 水平布局容器 - 用于放置奖励选项 */
	UPROPERTY(BlueprintReadWrite, meta = (BindWidget), Category = "UI Components")
	class UHorizontalBox* RewardOptionsContainer;

	/** 奖励选项卡片类 - 用于动态创建奖励卡片 */
	UPROPERTY(EditAnywhere, Category = "Reward Cards")
	TSubclassOf<class URewardOptionCardWidget> RewardOptionCardClass;

	// ==================== 功能函数 ====================
	
	/**
	 * @brief 初始化弹窗组件
	 * @param InRewardOptions 奖励选项数据数组
	 * @param InSelectedIndex 默认选中索引
	 * @note 在蓝图Construct事件中调用此函数进行初始化
	 */
	UFUNCTION(BlueprintCallable, Category = "Activity Popup")
	void InitializePopup(const TArray<struct FDailyLoginConfigRow>& InRewardOptions, int32 InSelectedIndex = 1);

	/**
	 * @brief 设置当前选中索引
	 * @param Index 选中索引值(0-2)
	 * @note 用于更新选中状态的视觉反馈
	 */
	UFUNCTION(BlueprintCallable, Category = "Activity Popup")
	void SetSelectedIndex(int32 Index);

	/**
	 * @brief 获取当前选中索引
	 * @return 当前选中的索引值
	 */
	UFUNCTION(BlueprintPure, Category = "Activity Popup")
	int32 GetSelectedIndex() const { return SelectedIndex; }

protected:
	/** 组件构造完成回调 */
	virtual void NativeConstruct() override;

private:
	/** 当前选中索引 */
	UPROPERTY()
	int32 SelectedIndex = 1;

	/** 奖励选项数据 */
	UPROPERTY()
	TArray<struct FDailyLoginConfigRow> RewardOptions;

	/**
	 * @brief 为指定的宝箱配置创建多个奖励卡片
	 * @param Config 宝箱配置数据
	 * @note 直接将创建的卡片添加到RewardOptionsContainer中
	 */
	void CreateRewardCardsForBox(const struct FDailyLoginConfigRow& Config);

	/**
	 * @brief 创建奖励卡片UI元素
	 * @param Config 奖励配置数据
	 * @param Index 卡片索引
	 * @return 创建的UI组件
	 */
	class UWidget* CreateRewardCard(const struct FDailyLoginConfigRow& Config, int32 Index);

	/** 关闭按钮点击事件处理 */
	UFUNCTION()
	void OnCloseClicked();

	/** 确认按钮点击事件处理 */
	UFUNCTION()
	void OnConfirmClicked();

	/** 奖励卡片1点击事件 */
	UFUNCTION()
	void OnRewardCardClicked_0();

	/** 奖励卡片2点击事件 */
	UFUNCTION()
	void OnRewardCardClicked_1();

	/** 奖励卡片3点击事件 */
	UFUNCTION()
	void OnRewardCardClicked_2();

	/**
	 * @brief 内部奖励卡片点击处理函数
	 * @param Index 被点击的卡片索引
	 */
	void InternalOnRewardCardClicked(int32 Index);
	
	/** 奖励卡片选中状态改变回调 */
	UFUNCTION()
	void OnRewardCardSelected(class URewardOptionCardWidget* CardWidget, bool bIsChecked);
};