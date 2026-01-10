#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UI/Activity/Data/ActivityConfig.h"
#include "ActivityController.generated.h"

class UPanelWidget;
class UUserWidget;

/**
 * 活动系统核心控制器
 * 不依赖任何具体 UI
 */
UCLASS()
class METALSLUG01_API UActivityController : public UObject
{
	GENERATED_BODY()

public:

	/** 初始化 Controller */
	void Init(
		UDataTable* InConfigTable,
		UPanelWidget* InContentHost
	);

	/** 点击左侧菜单 */
	UFUNCTION()
	void OnMenuClicked(FName PageId);

private:

	/** 切换页面 */
	void SwitchToPage(FName PageId);

private:

	/** 活动配置表 */
	UPROPERTY()
	UDataTable* ConfigTable;

	/** 右侧内容容器 */
	UPROPERTY()
	UPanelWidget* ContentHost;

	/** 页面缓存 */
	UPROPERTY()
	TMap<FName, UUserWidget*> PageCache;

	/** 当前显示页面 */
	UPROPERTY()
	UUserWidget* CurrentPage;
};
