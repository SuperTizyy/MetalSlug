#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ActivityPageBase.generated.h"

/**
 * 所有活动页面的基类
 * 定义页面显示和隐藏的生命周期事件，供所有活动页面继承
 */
UCLASS(Abstract)
class METALSLUG01_API UActivityPageBase : public UUserWidget
{
	GENERATED_BODY()

public:

	/** 页面显示时调用
	 *  当页面被切换到前台显示时触发
	 */
	UFUNCTION(BlueprintNativeEvent)
	void OnPageShow();
	virtual void OnPageShow_Implementation();

	/** 页面隐藏时调用
	 *  当页面被切换到后台隐藏时触发
	 */
	UFUNCTION(BlueprintNativeEvent)
	void OnPageHide();
	virtual void OnPageHide_Implementation();
};
