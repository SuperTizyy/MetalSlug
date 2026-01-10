// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/Activity/Controller/ActivityController.h"
#include "Blueprint/UserWidget.h"
#include "Components/PanelWidget.h"
#include "UI/Activity/Pages/ActivityPageBase.h"


void UActivityController::Init(
	UDataTable* InConfigTable,
	UPanelWidget* InContentHost
)
{
	// 保存配置表
	ConfigTable = InConfigTable;

	// 保存右侧内容容器
	ContentHost = InContentHost;

	// 初始化为空
	CurrentPage = nullptr;
}

void UActivityController::OnMenuClicked(FName PageId)
{
	UE_LOG(LogTemp, Error,
		TEXT("Controller::OnMenuClicked PageId=%s"),
		*PageId.ToString());
	
	// 左侧按钮点击后，统一走这里
	SwitchToPage(PageId);
}

void UActivityController::SwitchToPage(FName PageId)
{
	if (CurrentPage)
	{
		if (UActivityPageBase* Page =
			Cast<UActivityPageBase>(CurrentPage))
		{
			Page->OnPageHide();
		}

		CurrentPage->RemoveFromParent();
		CurrentPage = nullptr;
	}


	// 如果缓存中没有这个页面，创建它
	if (!PageCache.Contains(PageId))
	{
		// 从 DataTable 查配置
		FActivityConfig* Config =
			ConfigTable->FindRow<FActivityConfig>(
				PageId,
				TEXT("SwitchToPage")
			);

		if (!Config)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("Activity PageId [%s] not found"),
				*PageId.ToString());
			return;
		}

		
		UClass* WidgetClass = Config->PageWidgetClass;
		if (!WidgetClass)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("WidgetClass invalid [%s]"),
				*PageId.ToString());
			return;
		}

		// 创建页面实例
		UUserWidget* PageWidget =
			CreateWidget<UUserWidget>(
				ContentHost->GetWorld(),
				WidgetClass
			);

		// 加入缓存
		PageCache.Add(PageId, PageWidget);
	}

	// 显示页面
	CurrentPage = PageCache[PageId];
	ContentHost->AddChild(CurrentPage);
	
	if (UActivityPageBase* Page =
	Cast<UActivityPageBase>(CurrentPage))
	{
		Page->OnPageShow();
	}
}
