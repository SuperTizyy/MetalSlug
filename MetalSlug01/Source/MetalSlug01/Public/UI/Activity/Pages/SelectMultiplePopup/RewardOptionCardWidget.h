// 版权声明：在项目设置的描述页面填写您的版权信息。

// RewardOptionCardWidget.h
#pragma once

// ==========================================
// RewardOptionCardWidget 头文件 — 奖励选项卡片 Widget
// ==========================================
//
// 文件作用:
//   1. 声明 URewardOptionCardWidget — 单张奖励选项卡片
//   2. 显示奖励图标 + 文本 + 复选框
//   3. 选中时通过 OnRewardSelected 广播
//
// 架构理念:
//   - 单一职责: 一张卡片只管自己
//   - 数据驱动: CardIndex 用于反查
//   - 双向同步: 外部状态变化能更新 UI
//   - 复用: 标准化的奖励选项展示
// ==========================================
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Components/CheckBox.h"
#include "RewardOptionCardWidget.generated.h"


/**
 * @class URewardOptionCardWidget
 * @brief 奖励选项卡片 Widget
 *
 * 职责说明:
 * - 用于 ActivityConfirmPopupWidget 中动态生成奖励选项
 * - 显示奖励图标 + 文本 + 复选框
 * - 选中时通过 OnRewardSelected 广播
 *
 * 架构理念:
 * 1. 单一职责: 一张卡片只管自己
 * 2. 数据驱动: CardIndex 用于反查
 * 3. 双向同步: 外部状态变化（OnRewardIconIndexChanged）能更新 UI
 * 4. 复用: 标准化的奖励选项展示
 *
 * 关联:
 * - 上级: UActivityConfirmPopupWidget（创建/管理）
 * - 数据: FItemDetailRow / FTreasureBoxItemRow
 */
UCLASS(ClassGroup=(Custom), BlueprintType)
class METALSLUG01_API URewardOptionCardWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// ==========================================
	// 1. UI 组件
	// ==========================================

	/** 奖励图标显示 */
	UPROPERTY(BlueprintReadWrite, meta = (BindWidget), Category = "UI Components")
	class UImage* RewardImage;

	/** 奖励文本信息（数量 + 名称） */
	UPROPERTY(BlueprintReadWrite, meta = (BindWidget), Category = "UI Components")
	class UTextBlock* RewardText;

	/** 选择复选框 */
	UPROPERTY(BlueprintReadWrite, meta = (BindWidget), Category = "UI Components")
	class UCheckBox* SelectionCheckBox;

	// ==========================================
	// 2. 数据属性
	// ==========================================

	/** 奖励 ID */
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

	// ==========================================
	// 3. 事件委托
	// ==========================================

	/**
	 * @delegate FOnRewardSelected
	 * @brief 奖励选中事件
	 * @param CardWidget 卡片 Widget（让父级拿到具体实例）
	 * @param bIsChecked 是否选中
	 */
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnRewardSelected, URewardOptionCardWidget*, CardWidget, bool, bIsChecked);

	/** 当奖励选项被选中时触发 */
	UPROPERTY(BlueprintAssignable, Category = "Reward Events")
	FOnRewardSelected OnRewardSelected;

	// ==========================================
	// 4. 功能函数
	// ==========================================

	/**
	 * 初始化奖励卡片
	 * @param InRewardID 奖励 ID
	 * @param InRewardCount 奖励数量
	 * @param InRewardName 奖励名称
	 * @param InRewardIcon 奖励图标
	 */
	UFUNCTION(BlueprintCallable, Category = "Reward Card")
	void InitializeCard(int32 InRewardID, int32 InRewardCount, const FText& InRewardName, UTexture2D* InRewardIcon);

	/**
	 * 通过数据表初始化奖励卡片
	 * @param InItemID 物品 ID（用于查询 ItemDetailRow 表的 ItemIcon）
	 * @param InBoxID 宝箱 ID（用于查询 TreasureBoxItemRow 表的 ItemCount）
	 */
	UFUNCTION(BlueprintCallable, Category = "Reward Card")
	void InitializeCardWithDataTables(int32 InItemID, int32 InBoxID);

	/**
	 * 通过数据表初始化奖励卡片并设置选中状态
	 * @param InItemID 物品 ID
	 * @param InBoxID 宝箱 ID
	 * @param InCardIndex 卡片在选项中的索引位置
	 * @param bShouldBeSelected 是否应该被选中
	 * @details 专为 RewardOptionCardWidget 的 SelectionCheckBox 控件设计,
	 * 根据 UpgradeActivitySubsystem 中的 RewardIconIndex 设置选中状态
	 */
	UFUNCTION(BlueprintCallable, Category = "Reward Card")
	void InitializeCardWithDataTablesAndSelection(int32 InItemID, int32 InBoxID, int32 InCardIndex, bool bShouldBeSelected);

	/**
	 * 直接使用已获取的数据初始化奖励卡片
	 * @param InItemDetail 物品详情记录
	 * @param InTreasureBoxItem 宝箱物品记录
	 * @param InCardIndex 卡片在选项中的索引位置
	 */
	void InitializeCardWithDirectData(const struct FItemDetailRow* InItemDetail, const struct FTreasureBoxItemRow* InTreasureBoxItem, int32 InCardIndex);

	/** 获取卡片索引 */
	UFUNCTION(BlueprintPure, Category = "Reward Card")
	int32 GetCardIndex() const { return CardIndex; }

	/**
	 * 设置选中状态
	 * @param bInSelected 是否选中
	 */
	UFUNCTION(BlueprintCallable, Category = "Reward Card")
	void SetSelected(bool bInSelected);

	/** 获取当前选中状态 */
	UFUNCTION(BlueprintPure, Category = "Reward Card")
	bool IsSelected() const { return bIsSelected; }

protected:
	// ==========================================
	// 5. 生命周期
	// ==========================================

	/**
	 * 组件构造完成回调
	 * 1. 绑定 SelectionCheckBox -> OnCheckBoxStateChanged
	 * 2. SubscribeToRewardIconEvents
	 */
	virtual void NativeConstruct() override;

private:
	// ==========================================
	// 6. 私有成员
	// ==========================================

	/**
	 * 订阅奖励图标索引更新事件
	 * 用途: 外部状态变化同步到 UI
	 */
	void SubscribeToRewardIconEvents();

	/**
	 * 奖励图标索引更新事件回调
	 * @param NewIndex 新索引
	 */
	UFUNCTION()
	void OnRewardIconIndexChanged(int32 NewIndex);

	/**
	 * 复选框状态改变回调
	 * @param bIsChecked 是否选中
	 */
	UFUNCTION()
	void OnCheckBoxStateChanged(bool bIsChecked);
};
