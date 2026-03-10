// RewardOptionCardWidget.h
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Components/CheckBox.h"
#include "RewardOptionCardWidget.generated.h"

/**
 * @brief 奖励选项卡片Widget - 用于ActivityConfirmPopupWidget中动态生成奖励选项
 * @author AI Assistant
 * @date 2024
 * @note 配合ActivityConfirmPopupWidget使用，提供标准化的奖励选项展示界面
 */
UCLASS(ClassGroup=(Custom), BlueprintType)
class METALSLUG01_API URewardOptionCardWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// ==================== 蓝图绑定组件 ====================
	
	/** 奖励图标显示 */
	UPROPERTY(BlueprintReadWrite, meta = (BindWidget), Category = "UI Components")
	class UImage* RewardImage;

	/** 奖励文本信息 */
	UPROPERTY(BlueprintReadWrite, meta = (BindWidget), Category = "UI Components")
	class UTextBlock* RewardText;

	/** 选择复选框 */
	UPROPERTY(BlueprintReadWrite, meta = (BindWidget), Category = "UI Components")
	class UCheckBox* SelectionCheckBox;

	// ==================== 数据属性 ====================
	
	/** 奖励ID */
	UPROPERTY(BlueprintReadOnly, Category = "Reward Data")
	int32 RewardID;

	/** 奖励数量 */
	UPROPERTY(BlueprintReadOnly, Category = "Reward Data")
	int32 RewardCount;

	/** 是否已选中 */
	UPROPERTY(BlueprintReadOnly, Category = "Reward Data")
	bool bIsSelected;

	/** 卡片在选项中的索引位置 */
	UPROPERTY()
	int32 CardIndex;

	// ==================== 事件委托 ====================
	
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnRewardSelected, URewardOptionCardWidget*, CardWidget, bool, bIsChecked);
	
	/** 当奖励选项被选中时触发 */
	UPROPERTY(BlueprintAssignable, Category = "Reward Events")
	FOnRewardSelected OnRewardSelected;

	// ==================== 功能函数 ====================
	
	/**
	 * @brief 初始化奖励卡片
	 * @param InRewardID 奖励ID
	 * @param InRewardCount 奖励数量
	 * @param InRewardName 奖励名称
	 * @param InRewardIcon 奖励图标
	 */
	UFUNCTION(BlueprintCallable, Category = "Reward Card")
	void InitializeCard(int32 InRewardID, int32 InRewardCount, const FText& InRewardName, UTexture2D* InRewardIcon);

	/**
	 * @brief 通过数据表初始化奖励卡片
	 * @param InItemID 物品ID（用于查询ItemDetailRow表的ItemIcon）
	 * @param InBoxID 宝箱ID（用于查询TreasureBoxItemRow表的ItemCount）
	 */
	UFUNCTION(BlueprintCallable, Category = "Reward Card")
	void InitializeCardWithDataTables(int32 InItemID, int32 InBoxID);
	
	/**
	 * @brief 通过数据表初始化奖励卡片并设置选中状态
	 * @param InItemID 物品ID（用于查询ItemDetailRow表的ItemIcon）
	 * @param InBoxID 宝箱ID（用于查询TreasureBoxItemRow表的ItemCount）
	 * @param InCardIndex 卡片在选项中的索引位置
	 * @param bShouldBeSelected 是否应该被选中
	 * @details 专为RewardOptionCardWidget的SelectionCheckBox控件设计，
	 * 根据UpgradeActivitySubsystem中的RewardIconIndex设置选中状态
	 */
	UFUNCTION(BlueprintCallable, Category = "Reward Card")
	void InitializeCardWithDataTablesAndSelection(int32 InItemID, int32 InBoxID, int32 InCardIndex, bool bShouldBeSelected);
	
	/**
	 * @brief 直接使用已获取的数据初始化奖励卡片
	 * @param InItemDetail 物品详情记录
	 * @param InTreasureBoxItem 宝箱物品记录
	 * @param InCardIndex 卡片在选项中的索引位置
	 */
	void InitializeCardWithDirectData(const struct FItemDetailRow* InItemDetail, const struct FTreasureBoxItemRow* InTreasureBoxItem, int32 InCardIndex);

	/**
	 * @brief 获取卡片索引
	 * @return 卡片在选项中的索引位置
	 */
	UFUNCTION(BlueprintPure, Category = "Reward Card")
	int32 GetCardIndex() const { return CardIndex; }
	
	/**
	 * @brief 设置选中状态
	 * @param bInSelected 是否选中
	 */
	UFUNCTION(BlueprintCallable, Category = "Reward Card")
	void SetSelected(bool bInSelected);

	/**
	 * @brief 获取当前选中状态
	 * @return 是否被选中
	 */
	UFUNCTION(BlueprintPure, Category = "Reward Card")
	bool IsSelected() const { return bIsSelected; }

protected:
	virtual void NativeConstruct() override;

private:
	/** 订阅奖励图标索引更新事件 */
	void SubscribeToRewardIconEvents();
	
	/** 奖励图标索引更新事件回调 */
	UFUNCTION()
	void OnRewardIconIndexChanged(int32 NewIndex);
	
	/** 复选框状态改变回调 */
	UFUNCTION()
	void OnCheckBoxStateChanged(bool bIsChecked);
};