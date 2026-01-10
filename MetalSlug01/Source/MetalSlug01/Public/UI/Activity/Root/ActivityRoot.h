#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ActivityRoot.generated.h"

class UVerticalBox;
class UActivityController;
class UActivityMenuItem;
class UDataTable;

UCLASS()
class METALSLUG01_API UActivityRoot : public UUserWidget
{
	GENERATED_BODY()

protected:
	virtual void NativeOnInitialized() override;

protected:
	// 左侧菜单容器
	UPROPERTY(meta = (BindWidget))
	UVerticalBox* LeftMenuBox;

	// 右侧内容区域
	UPROPERTY(meta = (BindWidget))
	UPanelWidget* RightContent;

	// 活动配置表
	UPROPERTY(EditDefaultsOnly, Category = "Activity")
	UDataTable* ActivityConfigTable;

	// 【缺的就是这个】菜单项蓝图类
	UPROPERTY(EditDefaultsOnly, Category = "Activity")
	TSubclassOf<UActivityMenuItem> MenuItemClass;

private:
	UPROPERTY()
	UActivityController* Controller;

	void BuildLeftMenu();
};
