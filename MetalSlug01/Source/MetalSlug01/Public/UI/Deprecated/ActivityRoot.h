/*
#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ActivityRoot.generated.h"

class UVerticalBox;
class UActivityController;
class UActivityMenuItem;
class UDataTable;

/**
 * 活动系统根控件 - 活动界面的主容器
 * 包含左侧菜单和右侧内容区域，管理整个活动系统的UI结构
 #1#
UCLASS()
class METALSLUG01_API UActivityRoot : public UUserWidget
{
	GENERATED_BODY()

protected:
	// 重写初始化函数，在控件初始化时执行必要的设置
	virtual void NativeOnInitialized() override;

protected:
	// 左侧菜单容器 - 用于放置活动菜单项
	UPROPERTY(meta = (BindWidget))
	UVerticalBox* LeftMenuBox;

	// 右侧内容区域 - 用于显示选中菜单项对应的详细内容
	UPROPERTY(meta = (BindWidget))
	UPanelWidget* RightContent;

	// 活动配置表 - 包含活动项目的配置信息
	UPROPERTY(EditDefaultsOnly, Category = "Activity")
	UDataTable* ActivityConfigTable;

	// 菜单项蓝图类 - 用于动态创建菜单项实例
	UPROPERTY(EditDefaultsOnly, Category = "Activity")
	TSubclassOf<UActivityMenuItem> MenuItemClass;

private:
	// 活动控制器 - 管理活动系统的业务逻辑
	UPROPERTY()
	UActivityController* Controller;

	// 构建左侧菜单 - 根据配置表动态创建菜单项
	void BuildLeftMenu();
};
*/
