#include "UI/Activity/Core/ActivityPageManager.h"
#include "Blueprint/UserWidget.h"
#include "Components/PanelWidget.h"
#include "Engine/DataTable.h"
#include "Kismet/GameplayStatics.h"
#include "UI/Activity/Data/DailyLoginConfig.h"

void UActivityPageManager::InitializeManager(UUserWidget* ParentWidget)
{
	ParentContainer = ParentWidget;
	// NavigationDataTable 将通过其他方式获取
}

void UActivityPageManager::NavigateToPage(FName ActivityId)
{
	if (!ParentContainer.IsValid() || !NavigationDataTable)
	{
		UE_LOG(LogTemp, Warning, TEXT("页面管理器未正确初始化"));
		return;
	}

	// 查找导航配置
	const FActivityInfoRow* NavConfig = FindNavConfig(ActivityId);
	if (!NavConfig || !NavConfig->TargetPageClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("找不到活动 %s 的页面配置"), *ActivityId.ToString());
		return;
	}

	// 创建或获取页面
	UUserWidget* TargetPage = CreateOrGetPage(ActivityId, NavConfig->TargetPageClass);
	if (!TargetPage)
	{
		UE_LOG(LogTemp, Error, TEXT("无法创建页面 %s"), *ActivityId.ToString());
		return;
	}

	// 清理当前页面
	ClearCurrentPage();

	// 添加新页面到父容器
	if (UPanelWidget* Panel = Cast<UPanelWidget>(ParentContainer->GetRootWidget()))
	{
		Panel->AddChild(TargetPage);
		TargetPage->SetVisibility(ESlateVisibility::Visible);
		CurrentPage = TargetPage;
		
		UE_LOG(LogTemp, Log, TEXT("成功导航到页面: %s"), *ActivityId.ToString());
	}
}

void UActivityPageManager::ClearCurrentPage()
{
	if (CurrentPage.IsValid())
	{
		CurrentPage->RemoveFromParent();
		CurrentPage.Reset();
	}
}

const FActivityInfoRow* UActivityPageManager::FindNavConfig(FName ActivityId)
{
	if (!NavigationDataTable)
		return nullptr;

	// 遍历数据表查找匹配的配置
	for (auto It = NavigationDataTable->GetRowMap().CreateConstIterator(); It; ++It)
	{
		FName RowName = It.Key();
		FActivityInfoRow* RowData = reinterpret_cast<FActivityInfoRow*>(It.Value());
		
		if (RowData && RowData->ActivityID == FCString::Atoi(*ActivityId.ToString()))
		{
			return RowData;
		}
	}
	
	return nullptr;
}

UUserWidget* UActivityPageManager::CreateOrGetPage(FName ActivityId, TSubclassOf<UUserWidget> PageClass)
{
	// 检查缓存
	if (TWeakObjectPtr<UUserWidget>* CachedPage = PageCache.Find(ActivityId))
	{
		if (CachedPage->IsValid())
		{
			return CachedPage->Get();
		}
		else
		{
			PageCache.Remove(ActivityId);
		}
	}

	// 创建新页面
	if (UWorld* World = GEngine->GetWorldFromContextObject(ParentContainer.Get(), EGetWorldErrorMode::LogAndReturnNull))
	{
		UUserWidget* NewPage = CreateWidget<UUserWidget>(World, PageClass);
		if (NewPage)
		{
			PageCache.Add(ActivityId, NewPage);
			return NewPage;
		}
	}

	return nullptr;
}