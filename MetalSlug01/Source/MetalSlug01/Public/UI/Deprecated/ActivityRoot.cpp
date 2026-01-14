/*// 版权声明：在项目设置的描述页面填写您的版权信息


#include "UI/Activity/Root/ActivityRoot.h"
#include "UI/Activity/Controller/ActivityController.h"
#include "UI/Activity/Root/ActivityMenuItem.h"
#include "Components/VerticalBox.h"

// BuildLeftMenu - 构建左侧菜单
// 根据配置表数据动态创建菜单项并添加到菜单容器中
void UActivityRoot::BuildLeftMenu()
{
	if (!ActivityConfigTable || !LeftMenuBox)
	{
		// 输出错误日志，显示哪个组件为空
		UE_LOG(LogTemp, Error,
			TEXT("BuildLeftMenu failed: ActivityConfigTable=%p LeftMenuBox=%p"),
			ActivityConfigTable,
			LeftMenuBox);
		return;
	}

	// 从配置表获取所有行数据
	TArray<FActivityConfig*> Rows;
	ActivityConfigTable->GetAllRows(TEXT("BuildLeftMenu"), Rows);

	UE_LOG(LogTemp, Error,
		TEXT("BuildLeftMenu: Rows.Num() = %d"),
		Rows.Num());

	if (Rows.Num() == 0)
	{
		// 如果配置表中没有数据，输出错误日志
		UE_LOG(LogTemp, Error,
			TEXT("BuildLeftMenu: EMPTY TABLE OR STRUCT MISMATCH"));
		return;
	}

	// 使用Lambda表达式按SortOrder字段对数据进行排序
	// ✅ UE 正确排序写法（注意是引用）
	Rows.Sort([](const FActivityConfig& A, const FActivityConfig& B)
	{
		return A.SortOrder < B.SortOrder;
	});

	// 遍历排序后的数据，创建菜单项
	for (FActivityConfig* Row : Rows)
	{
		if (!Row || !MenuItemClass)
		{
			continue;
		}

		// 创建菜单项实例
		UActivityMenuItem* Item =
			CreateWidget<UActivityMenuItem>(GetWorld(), MenuItemClass);

		if (!Item)
		{
			continue;
		}

		// 初始化菜单项数据
		Item->Init(Row->PageId, Row->DisplayName);

		// 绑定菜单项点击事件到控制器
		Item->OnClicked.AddDynamic(
			Controller,
			&UActivityController::OnMenuClicked
		);

		// 将菜单项添加到左侧菜单容器中
		LeftMenuBox->AddChild(Item);
	}
}




// NativeOnInitialized - 控件初始化时调用
// 执行必要的初始化逻辑，包括创建控制器和构建菜单
void UActivityRoot::NativeOnInitialized()
{
	Super::NativeOnInitialized();

	UE_LOG(LogTemp, Error, TEXT("ActivityRoot::NativeOnInitialized START"));

	if (!ActivityConfigTable)
	{
		// 检查配置表是否为空
		UE_LOG(LogTemp, Error,
			TEXT("ActivityConfigTable is NULL in ActivityRoot"));
		return;
	}
	else
	{
		UE_LOG(LogTemp, Error,
			TEXT("ActivityConfigTable OK"));
	}

	if (!RightContent || !LeftMenuBox)
	{
		// 检查UI绑定是否成功
		UE_LOG(LogTemp, Error,
			TEXT("UI BindWidget failed: RightContent=%p LeftMenuBox=%p"),
			RightContent,
			LeftMenuBox);
		return;
	}
	else
	{
		UE_LOG(LogTemp, Error,
			TEXT("BindWidget OK"));
	}

	// 创建活动控制器实例
	Controller = NewObject<UActivityController>(this);
	// 初始化控制器
	Controller->Init(ActivityConfigTable, RightContent);

	UE_LOG(LogTemp, Error, TEXT("Controller Init OK"));

	// 构建左侧菜单
	BuildLeftMenu();
}*/