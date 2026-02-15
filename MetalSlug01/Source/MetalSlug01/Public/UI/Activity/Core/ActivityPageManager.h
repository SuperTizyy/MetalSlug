#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "ActivityPageManager.generated.h"

class UUserWidget;
class UDataTable;

/**
 * @brief 活动页面管理器
 * 负责根据导航选择动态加载和显示对应的活动页面
 */
UCLASS(BlueprintType)
class METALSLUG01_API UActivityPageManager : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * @brief 初始化页面管理器
	 * @param ParentWidget 父容器Widget
	 * @param NavDataTable 导航配置数据表
	 */
	UFUNCTION(BlueprintCallable, Category = "Page Manager")
	void InitializeManager(UUserWidget* ParentWidget);

	/**
	 * @brief 导航到指定活动页面
	 * @param ActivityId 活动标识符
	 */
	UFUNCTION(BlueprintCallable, Category = "Page Manager")
	void NavigateToPage(FName ActivityId);

	/**
	 * @brief 获取当前显示的页面
	 */
	UFUNCTION(BlueprintPure, Category = "Page Manager")
	UUserWidget* GetCurrentPage() const { return CurrentPage.Get(); }

	/**
	 * @brief 清理当前页面
	 */
	UFUNCTION(BlueprintCallable, Category = "Page Manager")
	void ClearCurrentPage();

protected:
	/** 父容器Widget */
	UPROPERTY()
	TWeakObjectPtr<UUserWidget> ParentContainer;

	/** 导航配置数据表 */
	UPROPERTY()
	TObjectPtr<UDataTable> NavigationDataTable;

	/** 当前显示的页面 */
	UPROPERTY()
	TWeakObjectPtr<UUserWidget> CurrentPage;

	/** 页面缓存（避免重复创建） */
	UPROPERTY()
	TMap<FName, TWeakObjectPtr<UUserWidget>> PageCache;

private:
	/**
	 * @brief 从数据表中查找导航配置
	 */
	const struct FActivityInfoRow* FindNavConfig(FName ActivityId);

	/**
	 * @brief 创建或获取页面实例
	 */
	UUserWidget* CreateOrGetPage(FName ActivityId, TSubclassOf<UUserWidget> PageClass);
};