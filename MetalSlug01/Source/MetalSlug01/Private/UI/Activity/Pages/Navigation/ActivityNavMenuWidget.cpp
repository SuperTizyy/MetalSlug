// 版权声明：在项目设置的描述页面填写您的版权信息。

// ==========================================
// 头文件包含区
// ==========================================
#include "UI/Activity/Pages/Navigation/ActivityNavMenuWidget.h"
#include "Systems/Activity/ActivitySubsystem.h"
#include "Data/ActivitySaveGame.h"
#include "Components/Button.h"
#include "Components/VerticalBox.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Engine/Texture2D.h"
#include "Blueprint/UserWidget.h"
#include "Data/FActivityDataTableService.h" // 活动表统一加载入口


// ==========================================
// 1. 生命周期
// ==========================================

/**
 * UActivityNavMenuWidget::NativeConstruct
 *
 * 1. InitializeNavigation
 * 2. 绑定 Btn_BackToMenu（防呆: 先 RemoveDynamic 再 AddDynamic）
 * 3. 启动 30s 红点刷新定时器（循环）
 */
void UActivityNavMenuWidget::NativeConstruct()
{
	// 调用父类构造函数
	Super::NativeConstruct();
	// 初始化导航菜单
	InitializeNavigation();

	// 绑定返回游戏菜单按钮事件
	if (Btn_BackToMenu)
	{
		// 防呆设计: 先清理旧绑定再添加新绑定，防止热重载导致多重绑定
		Btn_BackToMenu->OnClicked.RemoveDynamic(this, &UActivityNavMenuWidget::OnBackToMenuClicked);
		Btn_BackToMenu->OnClicked.AddDynamic(this, &UActivityNavMenuWidget::OnBackToMenuClicked);
	}
	else
	{
		if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 10.0f, FColor::Red, TEXT("[ActivityNavMenuWidget 致命错误] Btn_BackToMenu 为空！请检查蓝图变量名是否完全匹配（区分大小写）！"));
	}

	// 设置定时刷新红点（每 30 秒刷新一次）
	GetWorld()->GetTimerManager().SetTimer(
		RedDotRefreshTimerHandle,      // 定时器句柄
		this,                         // 定时器所属对象
		&UActivityNavMenuWidget::RefreshRedDots,  // 定时器回调函数
		30.0f,  // 30 秒间隔
		true    // 循环执行
	);
}


/**
 * UActivityNavMenuWidget::NativeDestruct
 *
 * 1. 清理红点刷新定时器
 * 2. 清理 PageCache（RemoveFromParent）
 * 3. 清空 PageCache + CurrentPage
 */
void UActivityNavMenuWidget::NativeDestruct()
{
	// 清理红点刷新定时器
	if (GetWorld() && RedDotRefreshTimerHandle.IsValid())
	{
		GetWorld()->GetTimerManager().ClearTimer(RedDotRefreshTimerHandle);
	}

	// 清理页面缓存 - 遍历所有缓存的页面并从父级移除
	for (auto& Pair : PageCache)
	{
		if (Pair.Value.IsValid())
		{
			// 将页面从小部件树中移除
			Pair.Value->RemoveFromParent();
		}
	}
	// 清空页面缓存
	PageCache.Empty();

	// 清理当前页面引用
	CurrentPage.Reset();

	// 调用父类析构函数
	Super::NativeDestruct();
}


// ==========================================
// 2. 初始化
// ==========================================

/**
 * UActivityNavMenuWidget::InitializeNavigation
 *
 * 1. 检查 NavContainer/PageContainer/DefaultPageClass
 * 2. PopulateNavItemsFromSubsystem（数据源）
 * 3. 清空 NavContainer, 创建导航按钮
 * 4. 设置默认选中 + 切换页面
 * 5. 刷新红点
 *
 * 防御性: 任一 BindWidget 缺失即提前返回
 */
void UActivityNavMenuWidget::InitializeNavigation()
{
	// 初始化导航菜单界面

	// 检查蓝图绑定的组件是否存在
	if (!NavContainer)
	{
		// 如果导航容器未绑定，则返回
		return;
	}

	// 检查导航容器的可见性和父级

	// 检查页面容器
	if (!PageContainer)
	{
		// 如果页面容器未绑定，则返回
		return;
	}

	// 检查默认页面类
	if (DefaultPageClass)
	{
		// 默认页面类已设置
	}
	else
	{
		// 默认页面类未设置
	}

	// 如果导航项列表为空，尝试从 ActivitySubsystem 获取数据
	if (NavItems.Num() == 0)
	{
		// 从子系统填充导航项
		PopulateNavItemsFromSubsystem();
	}

	// 清理现有内容
	NavContainer->ClearChildren();

	// 创建导航项
	for (const FActivityNavItem& Item : NavItems)
	{
		// 创建导航按钮小部件
		UActivityNavButton* NavButtonWidget = CreateNavItemButton(Item);
		if (NavButtonWidget)
		{
			// 将导航按钮添加到容器
			NavContainer->AddChild(NavButtonWidget);

		}
		else
		{
			// 创建导航按钮失败
		}
	}

	// 设置默认选中项并显示对应页面
	FName DefaultActivityId = GetDefaultSelectedActivityId();
	if (!DefaultActivityId.IsNone())
	{
		// 设置默认选中的活动
		SetSelectedActivity(DefaultActivityId);

		// 自动切换到默认页面
		SwitchToActivityPage(DefaultActivityId);
	}
	else if (NavItems.Num() > 0)
	{
		// 如果没有设置默认选中项，则选择第一个
		FName FirstActivityId = NavItems[0].ActivityId;
		// 设置第一个活动为选中状态
		SetSelectedActivity(FirstActivityId);
		// 切换到第一个活动页面
		SwitchToActivityPage(FirstActivityId);
	}

	// 初始化红点显示
	RefreshRedDots();
}


// ==========================================
// 3. 导航按钮创建
// ==========================================

/**
 * UActivityNavMenuWidget::CreateNavItemButton
 *
 * 1. 加载 WBP_ActivityNavButton 蓝图类
 * 2. CreateWidget 创建实例
 * 3. 加载 DT_ActivityInfoRow 找 ActivityID 对应配置
 *    - 优先 FindRow, 失败 fallback GetAllRows 遍历
 * 4. 设置 DisplayTitle + IconTexture
 * 5. InitializeButton 初始化
 * 6. 绑定 OnButtonClicked.AddLambda -> SetSelectedActivity + SwitchToActivityPage
 * 7. 默认 SetSelected(false)
 */
UActivityNavButton* UActivityNavMenuWidget::CreateNavItemButton(const FActivityNavItem& Item)
{
	// 创建导航菜单项按钮

	UActivityNavButton* NavButtonWidget = nullptr;

	// 手动加载 ActivityNavButton 模板类
	TSubclassOf<UActivityNavButton> ButtonTemplateClass = nullptr;

	// 直接加载 WBP_ActivityNavButton 蓝图类
	FString TemplatePath = TEXT("/Game/UI/Activity/DailyLogin/WBP_ActivityNavButton.WBP_ActivityNavButton_C");
	ButtonTemplateClass = Cast<UClass>(StaticLoadObject(UClass::StaticClass(), nullptr, *TemplatePath));

	if (ButtonTemplateClass)
	{
		// 蓝图类加载成功
	}
	else
	{
		// 蓝图类加载失败，返回空指针
		return nullptr;
	}

	// 尝试使用 ActivityNavButton 模板类创建
	if (ButtonTemplateClass)
	{
		// 创建导航按钮小部件实例
		NavButtonWidget = CreateWidget<UActivityNavButton>(GetWorld(), ButtonTemplateClass);

		if (NavButtonWidget)
		{
			// 导航按钮创建成功
		}
		else
		{
			// 导航按钮创建失败，返回空指针
			return nullptr;
		}
	}

	// 如果模板创建失败，直接返回 nullptr
	if (!NavButtonWidget)
	{
		// 按钮创建失败
		return nullptr;
	}

	// 如果是有效的 ActivityNavButton 实例，则进行完整初始化
	if (NavButtonWidget && NavButtonWidget->MainButton)
	{
		// 设置 Widget 可见性
		NavButtonWidget->SetVisibility(ESlateVisibility::Visible);

		// 从 DataTable 获取完整的活动信息
		// 改造: 走 FActivityDataTableService, 避免硬编码路径
		UDataTable* ActivityInfoTable = FActivityDataTableService::Get(ActivityDataTable::ActivityInfo);

		if (!ActivityInfoTable)
		{
			// ⚠️ DataTable 缺失防御: 必须显式返回 nullptr (CreateNavItemButton 返回 UActivityNavButton*)
			return nullptr;
		}

		// 初始化显示标题和图标
		FText DisplayTitle = FText::GetEmpty();
		UTexture2D* IconTexture = nullptr;

		if (ActivityInfoTable)
		{
			// 定义查找上下文字符串
			static const FString ContextString(TEXT("NavMenuContext"));
			// 将活动 ID 转换为整数
			int32 TargetActivityId = FCString::Atoi(*Item.ActivityId.ToString());
			// 创建行名
			FName RowName(*FString::FromInt(TargetActivityId));

			// 查找活动信息
			FActivityInfoRow* ActivityInfo = ActivityInfoTable->FindRow<FActivityInfoRow>(RowName, ContextString);

			// 备用方案: 用 FindRowByIdSafe 全表遍历 (避开 GetAllRows 崩溃 v4 - 2026-08-10)
			if (!ActivityInfo)
			{
				ActivityInfo = const_cast<FActivityInfoRow*>(
					FActivityDataTableService::FindRowByIdSafe<FActivityInfoRow>(
						ActivityDataTable::ActivityInfo,
						[](const FActivityInfoRow& Row) { return Row.ActivityID; },
						TargetActivityId
					)
				);
			}

			if (ActivityInfo)
			{
				// 使用查找到的活动名称作为显示标题
				DisplayTitle = ActivityInfo->DisplayName;

				// 加载图标纹理
				if (ActivityInfo->IconTexture.Get())
				{
					// 同步加载纹理
					ActivityInfo->IconTexture.LoadSynchronous();
					// 获取纹理对象
					IconTexture = Cast<UTexture2D>(ActivityInfo->IconTexture.Get());
					if (IconTexture)
					{
						// 图标纹理加载成功
					}
				}

			}
			else
			{
				// 如果没有找到活动信息，使用默认显示名称
				DisplayTitle = GetActivityDisplayName(Item.ActivityId);
			}
		}
		else
		{
			// 如果数据表不存在，使用默认显示名称
			DisplayTitle = GetActivityDisplayName(Item.ActivityId);
		}

		// 如果仍然没有标题，使用备用方案
		if (DisplayTitle.IsEmpty())
		{
			// 使用活动 ID 构建显示标题
			DisplayTitle = FText::FromString(FString::Printf(TEXT("活动 %s"), *Item.ActivityId.ToString()));
		}

		// 初始化按钮（活动 ID、显示标题、图标纹理）
		NavButtonWidget->InitializeButton(Item.ActivityId, DisplayTitle, IconTexture);

		// 确保主按钮可见
		NavButtonWidget->MainButton->SetVisibility(ESlateVisibility::Visible);

		// 绑定点击事件

		if (!IsValid(NavButtonWidget->MainButton))
		{
			// 主按钮无效，但返回已创建的按钮
			return NavButtonWidget;
		}

		// 使用 ActivityNavButton 的普通委托绑定点击事件
		FName ButtonActivityId = Item.ActivityId;  // 保存 ActivityId 用于 lambda 捕获
		NavButtonWidget->OnButtonClicked.AddLambda([this, ButtonActivityId]() {
			// 先更新选中状态
			SetSelectedActivity(ButtonActivityId);

			// 再切换到对应活动页面
			SwitchToActivityPage(ButtonActivityId);
		});

		NavButtonWidget->SetSelected(false); // 默认未选中
	}
	else
	{
		// 按钮或主按钮无效，返回空指针
		return nullptr;
	}

	return NavButtonWidget;
}


// ==========================================
// 4. 旧式按钮点击（备用）
// ==========================================

/**
 * UActivityNavMenuWidget::OnNavItemClicked
 *
 * 旧式按钮点击处理
 * 1. SetSelectedActivity
 * 2. 广播 OnNavItemSelected
 */
void UActivityNavMenuWidget::OnNavItemClicked(UButton* Button, FName ActivityId)
{
	// 设置选中的活动
	SetSelectedActivity(ActivityId);
	// 检查导航项选择事件是否已绑定
	if (OnNavItemSelected.IsBound())
	{
		// 广播导航项选择事件
		OnNavItemSelected.Broadcast(ActivityId);
	}
}


/**
 * UActivityNavMenuWidget::OnButtonClicked
 *
 * 基础按钮点击处理函数
 * 1. 备用: 默认处理第一个导航项
 */
void UActivityNavMenuWidget::OnButtonClicked()
{
	// 这是一个备用处理函数，当没有指定活动 ID 时，默认处理第一个导航项
	if (NavItems.Num() > 0)
	{
		// 调用带参数的按钮点击处理函数，传入第一个导航项的活动 ID
		OnButtonClickedWithId(NavItems[0].ActivityId);
	}
}


/**
 * UActivityNavMenuWidget::OnButtonClickedWithId
 *
 * 带 ActivityId 的按钮点击
 * 1. 广播 OnNavItemSelected
 * 2. SetSelectedActivity
 * 3. SwitchToActivityPage
 */
void UActivityNavMenuWidget::OnButtonClickedWithId(FName ActivityId)
{

	// 使用传入的 ActivityId 触发导航事件
	if (OnNavItemSelected.IsBound())
	{
		// 广播导航项选择事件
		OnNavItemSelected.Broadcast(ActivityId);
	}
	// 设置选中的活动
	SetSelectedActivity(ActivityId);

	// 直接切换到对应活动页面
	SwitchToActivityPage(ActivityId);
}


// ==========================================
// 5. 索引点击处理（备用）
// ==========================================

/**
 * OnButtonClicked_Id0~Id3
 *
 * 多索引支持（备用, 当前主流程是 lambda 绑定）
 * 每个函数调用 OnButtonClickedWithId(NavItems[Index].ActivityId)
 * 防御性: IsValidIndex 检查
 */
void UActivityNavMenuWidget::OnButtonClicked_Id0() { if (NavItems.IsValidIndex(0)) OnButtonClickedWithId(NavItems[0].ActivityId); }
void UActivityNavMenuWidget::OnButtonClicked_Id1() { if (NavItems.IsValidIndex(1)) OnButtonClickedWithId(NavItems[1].ActivityId); }
void UActivityNavMenuWidget::OnButtonClicked_Id2() { if (NavItems.IsValidIndex(2)) OnButtonClickedWithId(NavItems[2].ActivityId); }
void UActivityNavMenuWidget::OnButtonClicked_Id3() { if (NavItems.IsValidIndex(3)) OnButtonClickedWithId(NavItems[3].ActivityId); }
void UActivityNavMenuWidget::OnButtonClicked_Default() {
	if (NavItems.Num() > 0) OnButtonClickedWithId(NavItems[0].ActivityId);
}


// ==========================================
// 6. 选中状态
// ==========================================

/**
 * UActivityNavMenuWidget::SetSelectedActivity
 *
 * 1. 缓存 CurrentSelectedActivity
 * 2. 遍历 NavItems 更新 bIsSelected
 * 3. UpdateAllButtonsVisualState
 */
void UActivityNavMenuWidget::SetSelectedActivity(FName ActivityId)
{
	// 设置当前选中的活动 ID
	CurrentSelectedActivity = ActivityId;

	// 遍历导航项数组，更新每个项的选中状态
	for (FActivityNavItem& Item : NavItems)
	{
		// 如果活动 ID 匹配，则设置为选中状态，否则为非选中状态
		Item.bIsSelected = (Item.ActivityId == ActivityId);
	}

	// 更新所有按钮的视觉状态以反映新的选中状态
	UpdateAllButtonsVisualState();
}


// ==========================================
// 7. 红点
// ==========================================

/**
 * UActivityNavMenuWidget::RefreshRedDots
 *
 * 1. 防御链: GameInstance -> UActivitySubsystem -> URedDotManager
 * 2. RedDotManager->RefreshAllRedDots
 * 3. 遍历 NavContainer 子项, 更新红点
 *    - 读 FRedDotData.bShouldShow && DotType != None
 *    - SetRedDot(bShow, DotValue)
 */
void UActivityNavMenuWidget::RefreshRedDots()
{
	// 刷新所有导航项的红点状态

	// 通过 GameInstance 获取 ActivitySubsystem
	UGameInstance* GameInstance = GetGameInstance();
	if (!GameInstance)
	{
		// 如果游戏实例不存在，则返回
		return;
	}

	UActivitySubsystem* ActivitySub = GameInstance->GetSubsystem<UActivitySubsystem>();
	if (!ActivitySub)
	{
		// 如果活动子系统不存在，则返回
		return;
	}

	// 获取红点管理器
	URedDotManager* RedDotManager = ActivitySub->GetRedDotManager();
	if (!RedDotManager)
	{
		// 如果红点管理器不存在，则返回
		return;
	}

	// 刷新所有红点数据
	RedDotManager->RefreshAllRedDots();

	// 遍历导航容器中的所有子控件，更新每个按钮的红点显示
	for (int32 i = 0; i < NavContainer->GetChildrenCount(); ++i)
	{
		// 获取子控件
		UWidget* ChildWidget = NavContainer->GetChildAt(i);
		// 尝试转换为活动导航按钮
		UActivityNavButton* NavButton = Cast<UActivityNavButton>(ChildWidget);

		// 检查按钮是否有效且具有红点图像
		if (NavButton && NavButton->RedDotImage)
		{
			// 获取按钮关联的活动 ID
			FName ButtonActivityId = NavButton->GetActivityId();
			// 将活动 ID 转换为整数
			int32 ActivityId = FCString::Atoi(*ButtonActivityId.ToString());

			// 从红点管理器获取该活动的红点数据
			FRedDotData RedDotData = RedDotManager->GetRedDotData(ActivityId);

			// 根据红点数据确定是否显示红点
			bool bShowRedDot = RedDotData.bShouldShow && RedDotData.DotType != ERedDotType::None;
			// 设置按钮的红点显示状态和数值
			NavButton->SetRedDot(bShowRedDot, RedDotData.DotValue);

		}
	}
}


/**
 * UActivityNavMenuWidget::UpdateNavItemRedDot
 *
 * 单项红点更新（备用, 当前主要靠 RefreshRedDots）
 */
void UActivityNavMenuWidget::UpdateNavItemRedDot(FName ActivityId, const FRedDotData& RedDotData)
{
	// 更新指定导航项的红点状态
	// 此函数目前未实现具体功能，待后续扩展

}


// ==========================================
// 8. 时间信息
// ==========================================

/**
 * UActivityNavMenuWidget::RefreshTimeInfos
 *
 * 刷新所有导航项的时间信息（占位, 未来扩展）
 */
void UActivityNavMenuWidget::RefreshTimeInfos()
{
	// 刷新所有导航项的时间信息显示
	// 从 ActivitySubsystem 获取最新的时间数据
	// 此函数目前未实现具体功能，待后续扩展
}


/**
 * UActivityNavMenuWidget::UpdateNavItemTimeInfo
 *
 * 更新单项时间信息（占位）
 */
void UActivityNavMenuWidget::UpdateNavItemTimeInfo(FName ActivityId, const FActivityInfoRow& TimeInfo)
{
	// 更新指定导航项的时间信息
	// 此函数目前未实现具体功能，待后续扩展
}


// ==========================================
// 9. 数据填充
// ==========================================

/**
 * UActivityNavMenuWidget::PopulateNavItemsFromSubsystem
 *
 * 1. 防御链: GameInstance -> UActivitySubsystem
 * 2. 失败 fallback LoadNavItemsFromDataTable, 再失败 CreateTestData
 * 3. 成功: GetAllNavItems -> 填充 NavItems
 *    - ActivityId 转 FName(*FString::FromInt)
 *    - LinkedActivityId = ActivityId
 */
void UActivityNavMenuWidget::PopulateNavItemsFromSubsystem()
{
	// 从 ActivitySubsystem 获取导航项数据并填充到本地数组

	// 首先尝试获取 GameInstance
	UGameInstance* GameInstance = GetGameInstance();
	if (!GameInstance)
	{
		// 如果游戏实例不存在，创建测试数据
		CreateTestData();
		return;
	}

	UActivitySubsystem* ActivitySub = GameInstance->GetSubsystem<UActivitySubsystem>();
	if (!ActivitySub)
	{
		// 如果活动子系统不存在

		// 备用方案: 直接加载 DataTable
		LoadNavItemsFromDataTable();
		return;
	}

	// 获取所有活动配置
	TArray<const FActivityInfoRow*> AllActivities = ActivitySub->GetAllNavItems();

	// 清空现有的导航项数组
	NavItems.Empty();

	// 遍历所有活动信息，创建对应的导航项
	for (const FActivityInfoRow* ActivityInfo : AllActivities)
	{
		if (!ActivityInfo) continue;

		FActivityNavItem NavItem;
		// 将活动 ID 转换为 FName 类型
		NavItem.ActivityId = FName(*FString::FromInt(ActivityInfo->ActivityID));
		// 设置链接的活动 ID
		NavItem.LinkedActivityId = NavItem.ActivityId;
		// 注意: 这里不再直接设置 IconTexture，而是在创建 Widget 时从 DataTable 获取

		// 将新创建的导航项添加到数组
		NavItems.Add(NavItem);

	}

}


/**
 * UActivityNavMenuWidget::LoadNavItemsFromDataTable
 *
 * 从 DataTable 直接加载
 * 1. StaticLoadObject DT_ActivityInfoRow
 * 2. 失败 fallback CreateTestData
 * 3. 成功: GetAllRows -> 过滤 ActivityType == CategoryHeader
 * 4. 按 ActivityID 数字大小排序
 */
void UActivityNavMenuWidget::LoadNavItemsFromDataTable()
{
	// 从数据表直接加载导航项数据

	// 先尝试直接加载 DataTable
	// 改造: 走 FActivityDataTableService, 避免硬编码路径
	UDataTable* ActivityInfoTable = FActivityDataTableService::Get(ActivityDataTable::ActivityInfo);

	if (!ActivityInfoTable)
	{

		// 如果加载失败，使用硬编码测试数据
		CreateTestData();
		return;
	}


	// 用 GetRowsSafe 防御性遍历 (避开 GetAllRows 崩溃 v4 - 2026-08-10)
	const TArray<const FActivityInfoRow*> AllRows = FActivityDataTableService::GetRowsSafe<FActivityInfoRow>(
		ActivityDataTable::ActivityInfo,
		[](const FActivityInfoRow& Row) { return Row.ActivityType == EActivityType::CategoryHeader; }
	);

	// 清空现有的导航项数组
	NavItems.Empty();

	// 筛选出导航项（CategoryHeader 类型）
	for (const FActivityInfoRow* Row : AllRows)
	{
		if (Row && Row->ActivityType == EActivityType::CategoryHeader)
		{
			FActivityNavItem NavItem;
			// 将活动 ID 转换为 FName 类型
			NavItem.ActivityId = FName(*FString::FromInt(Row->ActivityID));
			// 设置链接的活动 ID
			NavItem.LinkedActivityId = NavItem.ActivityId;
			// 注意: 图标纹理将在创建 Widget 时从 DataTable 获取

			// 将新创建的导航项添加到数组
			NavItems.Add(NavItem);

		}
	}

	// 按 ActivityID 数字大小排序
	NavItems.Sort([](const FActivityNavItem& A, const FActivityNavItem& B) {
		// 简化处理: 按 ActivityId 数字排序
		return FCString::Atoi(*A.ActivityId.ToString()) < FCString::Atoi(*B.ActivityId.ToString());
	});

	// 获取导航项数量（此变量当前未使用）
	int32 NavItemCount = NavItems.Num();
}


/**
 * UActivityNavMenuWidget::LoadAllActivityItemsFromDataTable
 *
 * 从 DataTable 加载所有活动项
 * 1. StaticLoadObject DT_ActivityInfoRow
 * 2. 失败 fallback CreateTestData
 * 3. GetAllRows -> 不过滤, 全部加入
 * 4. 按 ActivityID 数字大小排序
 */
void UActivityNavMenuWidget::LoadAllActivityItemsFromDataTable()
{
	// 从数据表加载所有活动项，而不仅仅是导航项

	// 直接加载 DataTable
	// 改造: 走 FActivityDataTableService
	UDataTable* ActivityInfoTable = FActivityDataTableService::Get(ActivityDataTable::ActivityInfo);

	if (!ActivityInfoTable)
	{
		// 如果数据表加载失败，创建测试数据
		CreateTestData();
		return;
	}


	// 用 GetRowsSafe 防御性遍历 (避开 GetAllRows 崩溃 v4 - 2026-08-10)
	const TArray<const FActivityInfoRow*> AllRows = FActivityDataTableService::GetRowsSafe<FActivityInfoRow>(
		ActivityDataTable::ActivityInfo,
		[](const FActivityInfoRow& Row) { return true; } // 不过滤, 收集所有行
	);

	// 清空现有的导航项数组
	NavItems.Empty();

	// 为每一行创建导航项（显示所有活动，而不仅仅是 CategoryHeader 类型）
	for (const FActivityInfoRow* Row : AllRows)
	{
		if (Row)
		{
			FActivityNavItem NavItem;
			// 将活动 ID 转换为 FName 类型
			NavItem.ActivityId = FName(*FString::FromInt(Row->ActivityID));
			// 设置链接的活动 ID
			NavItem.LinkedActivityId = NavItem.ActivityId;

			// 将新创建的导航项添加到数组
			NavItems.Add(NavItem);

		}
	}

	// 按 ActivityID 数字大小排序
	NavItems.Sort([](const FActivityNavItem& A, const FActivityNavItem& B) {
		// 按活动 ID 的数值进行升序排列
		return FCString::Atoi(*A.ActivityId.ToString()) < FCString::Atoi(*B.ActivityId.ToString());
	});


}


/**
 * UActivityNavMenuWidget::GetActivityDisplayName
 *
 * 1. 加载 DT_ActivityInfoRow
 * 2. 优先 FindRow(FName(ToString(ActivityID))), 失败 fallback GetAllRows 遍历
 * 3. 返回 DisplayName
 * 4. 默认: "活动 101" 格式
 */
FText UActivityNavMenuWidget::GetActivityDisplayName(FName ActivityId)
{
	// 根据活动 ID 从 DataTable 获取活动的显示名称

	// 加载活动信息数据表
	// 改造: 走 FActivityDataTableService
	UDataTable* ActivityInfoTable = FActivityDataTableService::Get(ActivityDataTable::ActivityInfo);

	if (ActivityInfoTable)
	{
		// 定义查找上下文字符串
		static const FString ContextString(TEXT("NavMenuContext"));
		// 将活动 ID 转换为整数
		int32 TargetActivityId = FCString::Atoi(*ActivityId.ToString());
		// 创建行名
		FName RowName(*FString::FromInt(TargetActivityId));

		// 尝试通过行名直接查找活动信息
		FActivityInfoRow* ActivityInfo = ActivityInfoTable->FindRow<FActivityInfoRow>(RowName, ContextString);

		// 备用方案: 用 FindRowByIdSafe 防御性遍历 (避开 GetAllRows 崩溃 v4 - 2026-08-10)
		if (!ActivityInfo)
		{
			ActivityInfo = const_cast<FActivityInfoRow*>(
				FActivityDataTableService::FindRowByIdSafe<FActivityInfoRow>(
					ActivityDataTable::ActivityInfo,
					[](const FActivityInfoRow& Row) { return Row.ActivityID; },
					TargetActivityId
				)
			);
		}

		// 如果找到了活动信息且显示名称不为空，则返回该名称
		if (ActivityInfo && !ActivityInfo->DisplayName.IsEmpty())
		{
			return ActivityInfo->DisplayName;
		}
	}

	// 如果找不到活动信息或名称为空，返回默认名称
	return FText::FromString(FString::Printf(TEXT("活动 %s"), *ActivityId.ToString()));
}


/**
 * UActivityNavMenuWidget::GetDefaultSelectedActivityId
 *
 * 1. 加载 DT_ActivityInfoRow
 * 2. GetAllRows -> 找 bIsDefaultSelected = true 的行
 * 3. 返回 ActivityID 转 FName
 * 4. 默认: NAME_None
 */
FName UActivityNavMenuWidget::GetDefaultSelectedActivityId()
{
	// 从 DataTable 中查找设置了 bIsDefaultSelected=true 的活动，返回其活动 ID
	// 改造: 走 FActivityDataTableService
	UDataTable* ActivityInfoTable = FActivityDataTableService::Get(ActivityDataTable::ActivityInfo);

	if (ActivityInfoTable)
	{
		// 用 FindRowByIdSafe 防御性遍历 (避开 GetAllRows 崩溃 v4 - 2026-08-10)
		// 仅查找 bIsDefaultSelected 为 true 的第一行
		const TArray<const FActivityInfoRow*> AllRows = FActivityDataTableService::GetRowsSafe<FActivityInfoRow>(
			ActivityDataTable::ActivityInfo,
			[](const FActivityInfoRow& Row) { return Row.bIsDefaultSelected; }
		);

		// 遍历所有行，查找设置了默认选中的活动
		for (const FActivityInfoRow* Row : AllRows)
		{
			// 检查行是否有效且设置了默认选中标志
			if (Row && Row->bIsDefaultSelected)
			{
				// 将活动 ID 转换为 FName 类型并返回
				return FName(*FString::FromInt(Row->ActivityID));
			}
		}
	}

	// 如果没有找到默认选中的活动，返回 NAME_None
	return NAME_None;
}


/**
 * UActivityNavMenuWidget::UpdateAllButtonsVisualState
 *
 * 1. 遍历 NavContainer 子项
 * 2. 找匹配 ActivityId 的 NavItem 读取 bIsSelected
 * 3. NavButton->SetSelected(bShouldBeSelected)
 */
void UActivityNavMenuWidget::UpdateAllButtonsVisualState()
{
	// 更新所有导航按钮的视觉状态，使其与内部数据状态保持一致

	// 遍历 NavContainer 中的所有子 Widget
	for (int32 i = 0; i < NavContainer->GetChildrenCount(); ++i)
	{
		// 获取子控件
		UWidget* ChildWidget = NavContainer->GetChildAt(i);
		// 尝试转换为活动导航按钮
		UActivityNavButton* NavButton = Cast<UActivityNavButton>(ChildWidget);

		// 检查按钮是否有效且具有主按钮
		if (NavButton && NavButton->MainButton)
		{
			// 根据 NavItems 中的状态更新按钮视觉
			FName ButtonActivityId = NavButton->GetActivityId();
			// 初始化选中状态为 false
			bool bShouldBeSelected = false;

			// 在 NavItems 中查找对应的选中状态
			for (const FActivityNavItem& Item : NavItems)
			{
				// 查找与当前按钮匹配的活动项
				if (Item.ActivityId == ButtonActivityId)
				{
					// 获取该项的选中状态
					bShouldBeSelected = Item.bIsSelected;
					// 找到匹配项后退出循环
					break;
				}
			}

			// 更新按钮的选中状态
			NavButton->SetSelected(bShouldBeSelected);

		}
	}
}


/**
 * UActivityNavMenuWidget::CreateTestData
 *
 * 临时方案: 硬编码一个 NavItem (101)
 * 用途: 数据源全部失效时的降级
 */
void UActivityNavMenuWidget::CreateTestData()
{
	// 创建测试数据，用于在无法获取真实数据时提供基本功能演示

	// 清空现有的导航项数组
	NavItems.Empty();

	// 创建测试导航项- 模拟实际的 DataTable 数据结构
	FActivityNavItem TestItem1;
	// 设置活动 ID
	TestItem1.ActivityId = FName(TEXT("101"));
	// 设置链接的活动 ID
	TestItem1.LinkedActivityId = TestItem1.ActivityId;
	// 设置初始选中状态为未选中
	TestItem1.bIsSelected = false;
	// 将测试项添加到导航项数组
	NavItems.Add(TestItem1);

}


// ==========================================
// 10. 页面管理
// ==========================================

/**
 * UActivityNavMenuWidget::SwitchToActivityPage
 *
 * 1. 校验 PageContainer
 * 2. 查 PageCache -> 已缓存: 复用
 * 3. 未缓存:
 *    a. 加载 DT_ActivityInfoRow
 *    b. 找 TargetConfig (ActivityID 匹配)
 *    c. 多级降级: TargetConfig->TargetPageClass > DefaultPageClass > return
 *    d. CreateActivityPage + 缓存
 * 4. ShowPageInContainer
 */
void UActivityNavMenuWidget::SwitchToActivityPage(FName ActivityId)
{
	// 根据活动 ID 切换到对应的活动页面

	// 检查 PageContainer 是否存在
	if (!PageContainer)
	{
		// 如果页面容器不存在，则返回
		return;
	}
	else
	{
		// PageContainer 存在，可以继续执行
	}

	// 从缓存中查找页面
	UUserWidget* TargetPage = nullptr;
	if (PageCache.Contains(ActivityId))
	{
		// 从缓存中获取页面
		TargetPage = PageCache[ActivityId].Get();
		if (!TargetPage)
		{
			// 如果缓存的对象已被销毁，从缓存中移除
			PageCache.Remove(ActivityId);

		}
		else
		{
			// 从缓存加载页面成功
		}
	}

	// 如果缓存中没有，则创建新页面
	if (!TargetPage)
	{
		// 直接从 DataTable 加载活动配置，获取 TargetPageClass
		// 改造: 走 FActivityDataTableService
		UDataTable* ActivityInfoTable = FActivityDataTableService::Get(ActivityDataTable::ActivityInfo);

		if (!ActivityInfoTable)
		{
			// 如果数据表加载失败

			// 回退到默认页面
			if (DefaultPageClass)
			{
				// 使用默认页面类创建页面
				TargetPage = CreateActivityPage(DefaultPageClass);
			}
			else
			{
				// 如果没有默认页面类，则返回
				return;
			}
		}
		else
		{
			// 从 DataTable 中查找对应 ActivityID 的配置
			// 用 FindRowByIdSafe 防御性查找 (避开 GetAllRows 崩溃 v4 - 2026-08-10)

			// 将活动 ID 转换为整数
			int32 TargetActivityId = FCString::Atoi(*ActivityId.ToString());

			FActivityInfoRow* TargetConfig = const_cast<FActivityInfoRow*>(
				FActivityDataTableService::FindRowByIdSafe<FActivityInfoRow>(
					ActivityDataTable::ActivityInfo,
					[](const FActivityInfoRow& Row) { return Row.ActivityID; },
					TargetActivityId
				)
			);

			if (!TargetConfig)
			{
				// 如果在 DataTable 中未找到活动配置

				// 回退到默认页面
				if (DefaultPageClass)
				{
					// 使用默认页面类创建页面
					TargetPage = CreateActivityPage(DefaultPageClass);
				}
				else
				{
					// 如果没有默认页面类，则返回
					return;
				}
			}
			else
			{
				// 使用 DataTable 中配置的目标页面类
				if (TargetConfig->TargetPageClass)
				{
					// 使用配置的页面类创建页面
					TargetPage = CreateActivityPage(TargetConfig->TargetPageClass);

				}
				else
				{
					// 如果活动 ID 未指定 TargetPageClass

					// 回退到默认页面
					if (DefaultPageClass)
					{
						// 使用默认页面类创建页面
						TargetPage = CreateActivityPage(DefaultPageClass);
					}
					else
					{
						// 如果没有默认页面类，则返回
						return;
					}
				}
			}
		}

		// 如果页面创建成功，缓存新创建的页面
		if (TargetPage)
		{
			// 将页面添加到缓存中
			PageCache.Add(ActivityId, TargetPage);
			// 页面已创建并缓存
		}
	}

	// 显示页面
	if (TargetPage)
	{
		// 在容器中显示页面
		ShowPageInContainer(TargetPage);
		// 成功切换到目标页面
	}
	else
	{
		// 目标页面为空，无法显示
	}
}


/**
 * UActivityNavMenuWidget::CreateActivityPage
 *
 * CreateWidget 简单封装
 */
UUserWidget* UActivityNavMenuWidget::CreateActivityPage(TSubclassOf<UUserWidget> PageClass)
{
	// 检查页面类是否有效
	if (!PageClass)
	{
		// 如果页面类为空，返回空指针
		return nullptr;
	}

	// 使用指定的页面类创建新的页面小部件
	UUserWidget* NewPage = CreateWidget<UUserWidget>(GetWorld(), PageClass);
	if (NewPage)
	{
		// 页面创建成功
	}
	else
	{
		// 页面创建失败
	}

	// 返回创建的页面，可能为 nullptr
	return NewPage;
}


/**
 * UActivityNavMenuWidget::ShowPageInContainer
 *
 * 1. 校验 PageContainer + PageWidget
 * 2. ClearCurrentPage（RemoveFromParent + Reset）
 * 3. AddChild(PageWidget) + SelfHitTestInvisible
 * 4. CurrentPage = PageWidget
 * 5. CanvasPanelSlot: SetAnchors 全填充 + SetOffsets 0 + SetZOrder 0
 */
void UActivityNavMenuWidget::ShowPageInContainer(UUserWidget* PageWidget)
{
	// 在容器中显示指定的页面小部件

	if (!PageContainer || !PageWidget)
	{
		// 如果页面容器或页面小部件为空，则返回
		return;
	}
	else
	{
		// PageContainer 和 PageWidget 都存在，可以继续执行

	}

	// 清理当前页面，将其从父级移除
	ClearCurrentPage();

	// 添加新页面到容器
	PageContainer->AddChild(PageWidget);
	// 设置页面可见性，允许用户交互
	PageWidget->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	// 更新当前页面引用
	CurrentPage = PageWidget;

	// 重要: 设置 Canvas Panel Slot 填充全屏
	UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(PageWidget->Slot);
	if (CanvasSlot)
	{
		// 设置锚点填充整个容器
		CanvasSlot->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f));
		// 设置偏移量为 0，确保完全填充
		CanvasSlot->SetOffsets(FMargin(0.0f, 0.0f, 0.0f, 0.0f));
		// 设置 Z 轴顺序确保显示在最上层
		CanvasSlot->SetZOrder(0);

		// Canvas 面板槽设置完成
	}
	else
	{
		// 页面小部件不是 Canvas Panel 类型，无法调整锚点
	}

	// 页面已添加到容器并设置为可见

}


/**
 * UActivityNavMenuWidget::ClearCurrentPage
 *
 * 移除当前页面 + 重置引用
 */
void UActivityNavMenuWidget::ClearCurrentPage()
{
	// 检查当前页面是否有效
	if (CurrentPage.IsValid())
	{
		// 将当前页面从父级控件中移除
		CurrentPage->RemoveFromParent();
		// 重置当前页面引用
		CurrentPage.Reset();
		// 当前页面已清除
	}
}


/**
 * UActivityNavMenuWidget::OnBackToMenuClicked
 *
 * 返回游戏主菜单（4 步骤）
 * A. 禁用 Btn_BackToMenu（防连点）
 * B. 清理 PageCache（RemoveFromParent） + Empty
 * C. ClearCurrentPage
 * D. 清理红点定时器
 * E. RemoveFromParent 销毁自身
 */
void UActivityNavMenuWidget::OnBackToMenuClicked()
{
	// 点击返回游戏菜单按钮时，销毁当前活动页面，返回到游戏主菜单

	// 步骤 A: 禁用按钮，防止玩家快速连续点击导致多次触发
	if (Btn_BackToMenu)
	{
		Btn_BackToMenu->SetIsEnabled(false);
	}

	// 步骤 B: 清理页面缓存和当前页面
	// 遍历所有缓存的页面并从父级移除
	for (auto& Pair : PageCache)
	{
		if (Pair.Value.IsValid())
		{
			Pair.Value->RemoveFromParent();
		}
	}
	PageCache.Empty();

	// 清理当前页面
	ClearCurrentPage();

	// 步骤 C: 停止红点刷新定时器
	if (GetWorld() && RedDotRefreshTimerHandle.IsValid())
	{
		GetWorld()->GetTimerManager().ClearTimer(RedDotRefreshTimerHandle);
	}

	// 步骤 D: 【核心操作】销毁自身，返回游戏主菜单
	// 通过 RemoveFromParent 将活动页面从视口移除并销毁
	// 这样玩家将看到下层的游戏主菜单页面
	this->RemoveFromParent();
}
