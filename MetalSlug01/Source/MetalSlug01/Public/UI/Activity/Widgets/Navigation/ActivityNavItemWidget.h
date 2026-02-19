#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "Styling/SlateColor.h"
#include "ActivityNavItemWidget.generated.h"

/**
 * @brief 活动导航项Widget
 * 用于ActivityNavMenu中的单个导航项显示
 */
UCLASS()
class METALSLUG01_API UActivityNavItemWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// ==================== 蓝图绑定组件 ====================
	
	/** 主按钮容器 */
	UPROPERTY(meta = (BindWidget))
	class UButton* MainButton;
	
	/** 图标显示 */
	UPROPERTY(meta = (BindWidget))
	class UImage* IconImage;
	
	/** 标题文本 */
	UPROPERTY(meta = (BindWidget))
	class UTextBlock* TitleText;
	
	/** 描述文本 */
	UPROPERTY(meta = (BindWidget))
	class UTextBlock* DescriptionText;
	
	/** 红点提示 */
	UPROPERTY(meta = (BindWidget))
	class UImage* RedDotImage;
	
	/** 时间信息文本 */
	UPROPERTY(meta = (BindWidget))
	class UTextBlock* TimeInfoText;

	// ==================== 功能函数 ====================
	
	/**
	 * @brief 初始化导航项
	 * @param InActivityId 活动ID
	 * @param InTitle 标题
	 * @param InDescription 描述
	 * @param InIconTexture 图标纹理
	 */
	UFUNCTION(BlueprintCallable, Category = "ActivityNavItem")
	void InitializeItem(FName InActivityId, const FText& InTitle, const FText& InDescription, UTexture2D* InIconTexture);

	/**
	 * @brief 设置选中状态
	 * @param bIsSelected 是否选中
	 */
	UFUNCTION(BlueprintCallable, Category = "ActivityNavItem")
	void SetSelected(bool bIsSelected);

	/**
	 * @brief 设置红点状态
	 * @param bShowRedDot 是否显示红点
	 * @param RedDotValue 红点数值
	 */
	UFUNCTION(BlueprintCallable, Category = "ActivityNavItem")
	void SetRedDot(bool bShowRedDot, int32 RedDotValue = 0);

	/**
	 * @brief 设置时间信息
	 * @param TimeInfo 时间信息文本
	 */
	UFUNCTION(BlueprintCallable, Category = "ActivityNavItem")
	void SetTimeInfo(const FText& TimeInfo);

	/**
	 * @brief 获取活动ID
	 */
	UFUNCTION(BlueprintPure, Category = "ActivityNavItem")
	FName GetActivityId() const { return ActivityId; }

protected:
	virtual void NativeConstruct() override;

private:
	/** 当前活动ID */
	UPROPERTY()
	FName ActivityId;

	/** 是否选中状态 */
	UPROPERTY()
	bool bIsSelected = false;
};