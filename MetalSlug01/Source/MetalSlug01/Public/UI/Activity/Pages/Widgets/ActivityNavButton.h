#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "ActivityNavButton.generated.h"

/**
 * @brief 活动导航按钮Widget
 * 专为ActivityNavMenu的NavContainer设计的按钮控件
 * 包含图标、标题和选中状态显示
 */
UCLASS()
class METALSLUG01_API UActivityNavButton : public UUserWidget
{
	GENERATED_BODY()

public:
	// ==================== 事件委托 ====================
	
	/** 动态多播委托（用于蓝图兼容） */
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnNavButtonClicked, FName, ActivityId);
	
	/** 普通委托（支持Lambda绑定） */
	FSimpleMulticastDelegate OnButtonClicked;
	
	UPROPERTY(BlueprintAssignable, Category = "Events")
	FOnNavButtonClicked OnNavButtonClicked;
	// ==================== 蓝图绑定组件 ====================
	
	/** 主按钮（同时包含图标和文字） */
	UPROPERTY(meta = (BindWidget))
	class UButton* MainButton;
	
	/** 按钮标题（放在按钮内部） */
	UPROPERTY(meta = (BindWidget))
	class UTextBlock* TitleText;
	
	/** 选中状态指示器 */
	UPROPERTY(meta = (BindWidget))
	class UImage* SelectionIndicator;
	
	/** 红点提示 */
	UPROPERTY(meta = (BindWidget))
	class UImage* RedDotImage;

	// ==================== 功能函数 ====================
	
	/**
	 * @brief 初始化按钮
	 * @param InActivityId 活动ID
	 * @param InTitle 按钮标题
	 * @param InIconTexture 图标纹理（直接设置到按钮上）
	 */
	UFUNCTION(BlueprintCallable, Category = "ActivityNavButton")
	void InitializeButton(FName InActivityId, const FText& InTitle, UTexture2D* InIconTexture);

	/**
	 * @brief 设置选中状态
	 * @param bIsSelected 是否选中
	 */
	UFUNCTION(BlueprintCallable, Category = "ActivityNavButton")
	void SetSelected(bool bIsSelected);

	/**
	 * @brief 设置红点状态
	 * @param bShowRedDot 是否显示红点
	 * @param RedDotValue 红点数值
	 */
	UFUNCTION(BlueprintCallable, Category = "ActivityNavButton")
	void SetRedDot(bool bShowRedDot, int32 RedDotValue = 0);

	/**
	 * @brief 获取活动ID
	 */
	UFUNCTION(BlueprintPure, Category = "ActivityNavButton")
	FName GetActivityId() const { return ActivityId; }

protected:
	virtual void NativeConstruct() override;
	
	/** 主按钮点击处理函数 */
	UFUNCTION()
	void OnMainButtonClicked();

private:
	/** 当前活动ID */
	UPROPERTY()
	FName ActivityId;

	/** 是否选中状态 */
	UPROPERTY()
	bool bIsSelected = false;
};