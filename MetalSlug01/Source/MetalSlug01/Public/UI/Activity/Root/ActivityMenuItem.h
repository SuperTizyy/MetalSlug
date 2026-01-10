#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ActivityMenuItem.generated.h"

class UButton;
class UTextBlock;

// 定义活动菜单项点击事件委托
// 当菜单项被点击时触发，传递页面ID参数
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(
	FOnActivityMenuClicked,
	FName,
	PageId
);

/**
 * 活动菜单项控件 - 表示活动系统中的单个菜单项
 * 处理菜单项的显示和点击事件
 */
UCLASS()
class METALSLUG01_API UActivityMenuItem : public UUserWidget
{
	GENERATED_BODY()

public:
	// 初始化菜单项
	// 设置菜单项的页面ID和显示名称
	void Init(FName InPageId, const FText& InDisplayName);

public:
	// 菜单项点击事件
	// 当菜单项被点击时触发
	UPROPERTY(BlueprintAssignable)
	FOnActivityMenuClicked OnClicked;

protected:
	// 重写初始化函数，在控件初始化时绑定事件
	virtual void NativeOnInitialized() override;

protected:
	// 点击按钮 - 用于响应用户的点击操作
	UPROPERTY(meta = (BindWidget))
	UButton* ClickButton;

	// 标题文本 - 显示菜单项的名称
	UPROPERTY(meta = (BindWidget))
	UTextBlock* TitleText;

private:
	// 页面ID - 对应要跳转的页面标识
	FName PageId;

	// 处理点击事件
	// 当按钮被点击时执行的回调函数
	UFUNCTION()
	void HandleClicked();
};
