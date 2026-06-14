// 版权声明：在项目设置的描述页面填写您的版权信息。

#pragma once

// ==========================================
// 头文件包含说明
// ==========================================
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "ActivityNavButton.generated.h"


/**
 * @class UActivityNavButton
 * @brief 活动导航按钮 Widget
 *
 * 职责说明:
 * - 专为 ActivityNavMenu 的 NavContainer 设计
 * - 包含: 图标 + 标题 + 选中状态 + 红点
 * - 点击后向父页面广播活动 ID
 *
 * 架构理念:
 * 1. 双委托: 普通委托（Lambda 友好）+ 动态委托（蓝图友好）
 * 2. 单一职责: 一个小按钮管自己的样式
 * 3. 数据驱动: ActivityId 让父页面知道点击的是哪个活动
 * 4. 状态机: SetSelected / SetRedDot 都是单一职责方法
 */
UCLASS()
class METALSLUG01_API UActivityNavButton : public UUserWidget
{
	GENERATED_BODY()

public:
	// ==========================================
	// 1. 事件委托
	// ==========================================

	/**
	 * 动态多播委托（用于蓝图兼容）
	 * 参数: ActivityId（让父页面知道点的是哪个活动）
	 */
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnNavButtonClicked, FName, ActivityId);

	/**
	 * 普通委托（支持 Lambda 绑定）
	 * 用途: C++ 层直接订阅, 性能更好
	 */
	FSimpleMulticastDelegate OnButtonClicked;

	/**
	 * 蓝图可绑定的事件分发器
	 */
	UPROPERTY(BlueprintAssignable, Category = "Events")
	FOnNavButtonClicked OnNavButtonClicked;

	// ==========================================
	// 2. 蓝图绑定组件
	// ==========================================

	/** 主按钮（同时包含图标和文字） */
	UPROPERTY(meta = (BindWidget))
	class UButton* MainButton;

	/** 按钮标题（放在按钮内部） */
	UPROPERTY(meta = (BindWidget))
	class UTextBlock* TitleText;

	/** 选中状态指示器（左侧高亮条/边框） */
	UPROPERTY(meta = (BindWidget))
	class UImage* SelectionIndicator;

	/** 红点提示（右上角小圆点） */
	UPROPERTY(meta = (BindWidget))
	class UImage* RedDotImage;

	// ==========================================
	// 3. 功能函数
	// ==========================================

	/**
	 * 初始化按钮
	 * @param InActivityId 活动 ID
	 * @param InTitle 按钮标题
	 * @param InIconTexture 图标纹理（直接设置到按钮上, 但本函数不直接操作 Slate）
	 */
	UFUNCTION(BlueprintCallable, Category = "ActivityNavButton")
	void InitializeButton(FName InActivityId, const FText& InTitle, UTexture2D* InIconTexture);

	/**
	 * 设置选中状态
	 * @param bIsSelected true=显示指示器, false=隐藏
	 */
	UFUNCTION(BlueprintCallable, Category = "ActivityNavButton")
	void SetSelected(bool bIsSelected);

	/**
	 * 设置红点状态
	 * @param bShowRedDot true=显示, false=隐藏
	 * @param RedDotValue 红点数值（备用, 目前未做数字红点）
	 */
	UFUNCTION(BlueprintCallable, Category = "ActivityNavButton")
	void SetRedDot(bool bShowRedDot, int32 RedDotValue = 0);

	/**
	 * 获取活动 ID
	 */
	UFUNCTION(BlueprintPure, Category = "ActivityNavButton")
	FName GetActivityId() const { return ActivityId; }

protected:
	// ==========================================
	// 4. 生命周期
	// ==========================================

	/**
	 * Native 构造
	 * 1. 默认未选中
	 * 2. 隐藏红点
	 * 3. 绑定按钮点击
	 */
	virtual void NativeConstruct() override;

	/**
	 * 主按钮点击处理函数
	 * 1. 广播 OnButtonClicked（普通委托）
	 * 2. 广播 OnNavButtonClicked（动态委托, 带 ActivityId）
	 */
	UFUNCTION()
	void OnMainButtonClicked();

private:
	// ==========================================
	// 5. 私有成员
	// ==========================================

	/** 当前活动 ID */
	UPROPERTY()
	FName ActivityId;

	/** 是否选中状态 */
	UPROPERTY()
	bool bIsSelected = false;
};
