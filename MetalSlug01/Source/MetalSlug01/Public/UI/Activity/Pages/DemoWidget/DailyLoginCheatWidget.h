#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "DailyLoginCheatWidget.generated.h"

/**
 * 测试用的调试小工具：手动设置当前登录天数
 */
UCLASS()
class METALSLUG01_API UDailyLoginCheatWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// 绑定输入框，BlueprintReadWrite 方便蓝图操作
	UPROPERTY(meta = (BindWidget), BlueprintReadWrite, Category = "Cheat")
	class UEditableText* DayInput;

	// 绑定确认按钮
	UPROPERTY(meta = (BindWidget), BlueprintReadWrite, Category = "Cheat")
	class UButton* ApplyCheatBtn;

	// 虚函数：组件初始化
	virtual void NativeConstruct() override;

protected:
	// 点击按钮的回调逻辑
	UFUNCTION()
	void OnApplyClicked();

	// 辅助函数：查找场景中的主界面并触发刷新
	void NotifyMainPageRefresh(int32 NewDay);
};