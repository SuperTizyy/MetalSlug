/*
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UI/Activity/Data/ActivityConfig.h"
#include "ActivityController.generated.h"

class UPanelWidget;
class UUserWidget;

/**
 * 活动系统核心控制器
 * 负责管理活动系统的业务逻辑，不依赖具体的UI组件
 * 处理页面切换、页面缓存等核心功能
 #1#
UCLASS()
class METALSLUG01_API UActivityController : public UObject
{
	GENERATED_BODY()

public:

	/** 初始化控制器
	 *  @param InConfigTable - 活动配置表，包含活动项目的信息
	 *  @param InContentHost - 内容承载容器，用于显示活动页面
	 #1#
	void Init(
		UDataTable* InConfigTable,
		UPanelWidget* InContentHost
	);

	/** 处理左侧菜单点击事件
	 *  @param PageId - 要跳转的页面ID
	 #1#
	UFUNCTION()
	void OnMenuClicked(FName PageId);

private:

	/** 切换到指定页面
	 *  @param PageId - 要切换到的页面ID
	 #1#
	void SwitchToPage(FName PageId);

private:

	/** 活动配置表 - 存储活动项目的基本信息 #1#
	UPROPERTY()
	UDataTable* ConfigTable;

	/** 右侧内容容器 - 用于承载和显示活动页面 #1#
	UPROPERTY()
	UPanelWidget* ContentHost;

	/** 页面缓存 - 缓存已创建的页面实例，避免重复创建 #1#
	UPROPERTY()
	TMap<FName, UUserWidget*> PageCache;

	/** 当前显示页面 - 记录当前正在显示的页面实例 #1#
	UPROPERTY()
	UUserWidget* CurrentPage;
};
*/
