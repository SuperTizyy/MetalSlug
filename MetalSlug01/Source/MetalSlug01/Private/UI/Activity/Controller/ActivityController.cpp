// 版权声明：在项目设置的描述页面填写您的版权信息


#include "UI/Activity/Controller/ActivityController.h"
#include "Blueprint/UserWidget.h"
#include "Components/PanelWidget.h"
#include "UI/Activity/Pages/ActivityPageBase.h"


// Init - 初始化控制器
// 设置配置表和内容承载容器
void UActivityController::Init(
	UDataTable* InConfigTable,
	UPanelWidget* InContentHost
)
{
	// 保存活动配置表
	ConfigTable = InConfigTable;

	// 保存右侧内容容器
	ContentHost = InContentHost;

	// 初始化当前页面为空
	CurrentPage = nullptr;
}

// OnMenuClicked - 处理菜单点击事件
// 接收来自菜单项的点击通知并切换到相应页面
void UActivityController::OnMenuClicked(FName PageId)
{
	// 输出日志记录点击事件
	UE_LOG(LogTemp, Error,
		TEXT("Controller::OnMenuClicked PageId=%s"),
		*PageId.ToString());
	
	// 左侧按钮点击后，统一走这里
	SwitchToPage(PageId);
}

// SwitchToPage - 切换到指定页面
// 隐藏当前页面并显示新页面
void UActivityController::SwitchToPage(FName PageId)
{
	// 如果当前有显示的页面，则隐藏它
	if (CurrentPage)
	{
		// 如果当前页面是活动页面基类的实例
		if (UActivityPageBase* Page =
			Cast<UActivityPageBase>(CurrentPage))
		{
			// 调用页面隐藏事件
			Page->OnPageHide();
		}

		// 从父容器中移除当前页面
		CurrentPage->RemoveFromParent();
		// 将当前页面设为空
		CurrentPage = nullptr;
	}


	// 如果缓存中没有这个页面，创建它
	if (!PageCache.Contains(PageId))
	{
		// 从 DataTable 查找配置
		FActivityConfig* Config =
			ConfigTable->FindRow<FActivityConfig>(
				PageId,
				TEXT("SwitchToPage")
			);

		if (!Config)
		{
			// 如果找不到配置，输出警告日志
			UE_LOG(LogTemp, Warning,
				TEXT("Activity PageId [%s] not found"),
				*PageId.ToString());
			return;
		}

		
		// 获取页面的Widget类
		UClass* WidgetClass = Config->PageWidgetClass;
		if (!WidgetClass)
		{
			// 如果Widget类无效，输出警告日志
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

		// 将页面实例加入缓存
		PageCache.Add(PageId, PageWidget);
	}

	// 显示页面
	// 获取要显示的页面实例
	CurrentPage = PageCache[PageId];
	// 将页面添加到内容容器中
	ContentHost->AddChild(CurrentPage);
	
	// 如果页面是活动页面基类的实例
	if (UActivityPageBase* Page =
	Cast<UActivityPageBase>(CurrentPage))
	{
		// 调用页面显示事件
		Page->OnPageShow();
	}
}