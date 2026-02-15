#include "UI/Activity/Pages/Widgets/ActivityNavMenuWidget.h"
#include "UI/Activity/Core/ActivitySubsystem.h"
#include "UI/Activity/Data/DailyLoginConfig.h"
#include "UI/Activity/Data/DailyLoginSave.h"
#include "Components/Button.h"
#include "Components/VerticalBox.h"
#include "Components/TextBlock.h"
#include "Components/Image.h"
#include "Components/CanvasPanel.h"
#include "Components/CanvasPanelSlot.h"
#include "Engine/Texture2D.h"
#include "Blueprint/UserWidget.h"

void UActivityNavMenuWidget::NativeConstruct()
{
	Super::NativeConstruct();
	InitializeNavigation();
	
	// 设置定时刷新红点（每30秒刷新一次）
	GetWorld()->GetTimerManager().SetTimer(
		RedDotRefreshTimerHandle,
		this,
		&UActivityNavMenuWidget::RefreshRedDots,
		30.0f,  // 30秒间隔
		true    // 循环执行
	);
	
	UE_LOG(LogTemp, Warning, TEXT("✅ ActivityNavMenuWidget 初始化完成，已启动红点定时刷新"));
}

void UActivityNavMenuWidget::NativeDestruct()
{
	// 清理红点刷新定时器
	if (GetWorld() && RedDotRefreshTimerHandle.IsValid())
	{
		GetWorld()->GetTimerManager().ClearTimer(RedDotRefreshTimerHandle);
		UE_LOG(LogTemp, Warning, TEXT("✅ 已清理红点刷新定时器"));
	}
	
	// 清理页面缓存
	for (auto& Pair : PageCache)
	{
		if (Pair.Value.IsValid())
		{
			Pair.Value->RemoveFromParent();
		}
	}
	PageCache.Empty();
	
	// 清理当前页面引用
	CurrentPage.Reset();
	
	Super::NativeDestruct();
}

void UActivityNavMenuWidget::InitializeNavigation()
{
	// UE_LOG(LogTemp, Warning, TEXT("=== ActivityNavMenuWidget 初始化开�?==="));
	
	// 检查蓝图绑定的组件是否存在
	UE_LOG(LogTemp, Warning, TEXT("检查NavContainer..."));
	if (!NavContainer)
	{
		UE_LOG(LogTemp, Error, TEXT("❌ ActivityNavMenuWidget: NavContainer未绑定，请在蓝图中设置VerticalBox容器"));
		return;
	}
	UE_LOG(LogTemp, Warning, TEXT("✅ NavContainer有效"));
	
	// 检查NavContainer的可见性和父级
	// UE_LOG(LogTemp, Warning, TEXT("NavContainer存在，可见性: %d, 父级: %s"), 
	// 	(int32)NavContainer->GetVisibility(), 
	// 	NavContainer->GetParent() ? NavContainer->GetParent()->GetName() : TEXT("None"));
	
	// 检查PageContainer
	UE_LOG(LogTemp, Warning, TEXT("检查PageContainer..."));
	if (PageContainer)
	{
		UE_LOG(LogTemp, Warning, TEXT("✅ PageContainer存在，可见性: %d"), (int32)PageContainer->GetVisibility());
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("❌ PageContainer未绑定，请在蓝图中设置页面容器"));
	}
	
	// 检查默认页面类
	if (DefaultPageClass)
	{
		// UE_LOG(LogTemp, Warning, TEXT("DefaultPageClass已设�? %s"), *DefaultPageClass->GetName());
	}
	else
	{
		// UE_LOG(LogTemp, Warning, TEXT("DefaultPageClass未设�?));
	}
	
	// 如果NavItems为空，尝试从ActivitySubsystem获取数据
	UE_LOG(LogTemp, Warning, TEXT("检查NavItems数据..."));
	if (NavItems.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("⚠️ NavItems为空，尝试从ActivitySubsystem获取数据"));
		PopulateNavItemsFromSubsystem();
		UE_LOG(LogTemp, Warning, TEXT("从Subsystem获取后NavItems数量: %d"), NavItems.Num());
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("✅ NavItems已有数据: %d 项"), NavItems.Num());
	}
	
	// 清理现有内容
	UE_LOG(LogTemp, Warning, TEXT("清理NavContainer现有内容..."));
	NavContainer->ClearChildren();
	UE_LOG(LogTemp, Warning, TEXT("✅ NavContainer内容已清空"));
	
	UE_LOG(LogTemp, Log, TEXT("ActivityNavMenuWidget: 准备创建 %d 个导航项"), NavItems.Num());
	
	// 创建导航�?
	for (const FActivityNavItem& Item : NavItems)
	{
		UActivityNavButton* NavButtonWidget = CreateNavItemButton(Item);
		if (NavButtonWidget)
		{
			NavContainer->AddChild(NavButtonWidget);
			// UE_LOG(LogTemp, Warning, TEXT("ActivityNavMenuWidget: 成功添加导航项[%s], Widget可见性: %d"), 
			// 	*Item.ActivityId.ToString(), (int32)NavItemWidget->GetVisibility());
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("ActivityNavMenuWidget: 创建导航项失败[%s]"), *Item.ActivityId.ToString());
		}
	}
	
	// 设置默认选中项并显示对应页面
	FName DefaultActivityId = GetDefaultSelectedActivityId();
	if (!DefaultActivityId.IsNone())
	{
		SetSelectedActivity(DefaultActivityId);
		UE_LOG(LogTemp, Warning, TEXT("🎯 设置默认活动 [%s] 并切换页面"), *DefaultActivityId.ToString());
		
		// 自动切换到默认页面
		SwitchToActivityPage(DefaultActivityId);
	}
	else if (NavItems.Num() > 0)
	{
		// 如果没有设置默认选中项，则选择第一个
		FName FirstActivityId = NavItems[0].ActivityId;
		SetSelectedActivity(FirstActivityId);
		UE_LOG(LogTemp, Warning, TEXT("⚠️ 未找到默认选中项，选择第一个活动 [%s]"), *FirstActivityId.ToString());
		SwitchToActivityPage(FirstActivityId);
	}
	
	// 初始化红点显示
	RefreshRedDots();
}

UActivityNavButton* UActivityNavMenuWidget::CreateNavItemButton(const FActivityNavItem& Item)
{
	UE_LOG(LogTemp, Warning, TEXT("=== 开始 CreateNavItemButton (ActivityNavButton版本) ==="));
	UE_LOG(LogTemp, Warning, TEXT("输入参数 - ActivityId: %s, DisplayName: %s"), 
		*Item.ActivityId.ToString(), *Item.GetDisplayName().ToString());
	
	UActivityNavButton* NavButtonWidget = nullptr;
	
	// 手动加载ActivityNavButton模板类
	TSubclassOf<UActivityNavButton> ButtonTemplateClass = nullptr;
	
	// 直接加载WBP_ActivityNavButton蓝图类
	FString TemplatePath = TEXT("/Game/UI/Activity/DailyLogin/WBP_ActivityNavButton.WBP_ActivityNavButton_C");
	ButtonTemplateClass = Cast<UClass>(StaticLoadObject(UClass::StaticClass(), nullptr, *TemplatePath));
	
	if (ButtonTemplateClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("✅ 成功加载ActivityNavButton模板类: %s"), *ButtonTemplateClass->GetName());
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("❌ 加载ActivityNavButton模板类失败: %s"), *TemplatePath);
	}
	
	// 尝试使用ActivityNavButton模板类创建
	if (ButtonTemplateClass)
	{
		NavButtonWidget = CreateWidget<UActivityNavButton>(GetWorld(), ButtonTemplateClass);
		
		if (NavButtonWidget)
		{
			UE_LOG(LogTemp, Warning, TEXT("✅ ActivityNavButton创建成功"));
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("❌ ActivityNavButton创建失败"));
		}
	}
	
	// 如果模板创建失败，直接返回nullptr
	if (!NavButtonWidget)
	{
		UE_LOG(LogTemp, Error, TEXT("❌ ActivityNavButton创建完全失败"));
		return nullptr;
	}
	
	// 如果是有效的ActivityNavButton实例，则进行完整初始化
	if (NavButtonWidget && NavButtonWidget->MainButton)
	{
		// 设置Widget可见性
		NavButtonWidget->SetVisibility(ESlateVisibility::Visible);
		
		// 从DataTable获取完整的活动信息
		FString InfoPath = TEXT("/Game/UI/Activity/Data/DT_ActivityInfoRow");
		UDataTable* ActivityInfoTable = Cast<UDataTable>(StaticLoadObject(UDataTable::StaticClass(), nullptr, *InfoPath));
		
		FText DisplayTitle = FText::GetEmpty();
		UTexture2D* IconTexture = nullptr;
		
		UE_LOG(LogTemp, Warning, TEXT("检查DataTable加载结果..."));
		if (ActivityInfoTable)
		{
			UE_LOG(LogTemp, Warning, TEXT("✅ DataTable加载成功"));
		
			static const FString ContextString(TEXT("NavMenuContext"));
			int32 TargetActivityId = FCString::Atoi(*Item.ActivityId.ToString());
			FName RowName(*FString::FromInt(TargetActivityId));
			
			// 查找活动信息
			FActivityInfoRow* ActivityInfo = ActivityInfoTable->FindRow<FActivityInfoRow>(RowName, ContextString);
			
			// 备用方案：全表遍历
			if (!ActivityInfo)
			{
				TArray<FActivityInfoRow*> AllRows;
				ActivityInfoTable->GetAllRows<FActivityInfoRow>(ContextString, AllRows);
				for (FActivityInfoRow* Row : AllRows)
				{
					if (Row && Row->ActivityID == TargetActivityId)
					{
						ActivityInfo = Row;
						break;
					}
				}
			}
			
			if (ActivityInfo)
			{
				UE_LOG(LogTemp, Warning, TEXT("✅ 找到活动信息，开始设置显示内容"));
				DisplayTitle = ActivityInfo->DisplayName;
				
				// 加载图标纹理
				if (ActivityInfo->IconTexture.Get())
				{
					ActivityInfo->IconTexture.LoadSynchronous();
					IconTexture = Cast<UTexture2D>(ActivityInfo->IconTexture.Get());
					if (IconTexture)
					{
						UE_LOG(LogTemp, Warning, TEXT("✅ 图标纹理加载成功"));
					}
				}
				
				UE_LOG(LogTemp, Warning, TEXT("✅ 从DataTable获取活动信息完成"));
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("❌ DataTable中未找到ActivityID=%d的配置"), TargetActivityId);
				DisplayTitle = GetActivityDisplayName(Item.ActivityId);
			}
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("❌ 无法加载ActivityInfo DataTable"));
			DisplayTitle = GetActivityDisplayName(Item.ActivityId);
		}
		
		// 如果仍然没有标题，使用备用方案
		if (DisplayTitle.IsEmpty())
		{
			DisplayTitle = FText::FromString(FString::Printf(TEXT("活动 %s"), *Item.ActivityId.ToString()));
		}
		
		UE_LOG(LogTemp, Warning, TEXT("开始初始化ActivityNavButton..."));
		NavButtonWidget->InitializeButton(Item.ActivityId, DisplayTitle, IconTexture);
		UE_LOG(LogTemp, Warning, TEXT("✅ ActivityNavButton初始化完成，标题: %s"), *DisplayTitle.ToString());
		
		// 确保MainButton可见
		NavButtonWidget->MainButton->SetVisibility(ESlateVisibility::Visible);
		UE_LOG(LogTemp, Log, TEXT("✅ ActivityNavButton: MainButton已设置为可见"));
		
		// 绑定点击事件
		UE_LOG(LogTemp, Warning, TEXT("绑定点击事件..."));
		
		if (!IsValid(NavButtonWidget->MainButton))
		{
			UE_LOG(LogTemp, Error, TEXT("❌ NavButtonWidget->MainButton is null! 无法绑定点击事件"));
			return NavButtonWidget;
		}
		
		// 使用ActivityNavButton的普通委托绑定点击事件
		FName ButtonActivityId = Item.ActivityId;  // 保存ActivityId用于lambda捕获
		NavButtonWidget->OnButtonClicked.AddLambda([this, ButtonActivityId]() {
			UE_LOG(LogTemp, Warning, TEXT("🎯 按钮点击 - 活动ID: %s"), *ButtonActivityId.ToString());
			
			// 先更新选中状态
			SetSelectedActivity(ButtonActivityId);
			
			// 再切换到对应活动页面
			SwitchToActivityPage(ButtonActivityId);
		});
		UE_LOG(LogTemp, Warning, TEXT("✅ 绑定到ActivityNavButton的普通委托"));
		
		UE_LOG(LogTemp, Warning, TEXT("✅ 点击事件绑定完成"));
		UE_LOG(LogTemp, Warning, TEXT("按钮事件绑定成功 - ActivityId: %s"), *Item.ActivityId.ToString());
		
		NavButtonWidget->SetSelected(false); // 默认未选中
		UE_LOG(LogTemp, Warning, TEXT("✅ ActivityNavButton: 设置选中状态[%s] 为未选中"), *Item.ActivityId.ToString());
		
		UE_LOG(LogTemp, Warning, TEXT("🎉 ActivityNavButton完整初始化流程完成"));
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("❌ ActivityNavButton创建完全失败"));
	}
	
	UE_LOG(LogTemp, Warning, TEXT("=== CreateNavItemButton (ActivityNavButton版本) 执行结束 ==="));
	return NavButtonWidget;
}

void UActivityNavMenuWidget::OnNavItemClicked(UButton* Button, FName ActivityId)
{
	SetSelectedActivity(ActivityId);
	if (OnNavItemSelected.IsBound())
	{
		OnNavItemSelected.Broadcast(ActivityId);
	}
}

// 新增：基础按钮点击处理函数
void UActivityNavMenuWidget::OnButtonClicked()
{
	UE_LOG(LogTemp, Warning, TEXT("ActivityNavMenuWidget: 基础按钮点击处理函数被调用"));
	// 这是一个备用处理函数，实际逻辑在带参数的版本中
	if (NavItems.Num() > 0)
	{
		OnButtonClickedWithId(NavItems[0].ActivityId);
	}
}

// 新增：带参数的按钮点击处理函数
void UActivityNavMenuWidget::OnButtonClickedWithId(FName ActivityId)
{
	UE_LOG(LogTemp, Log, TEXT("ActivityNavMenuWidget: 按钮点击 - 活动ID: %s"), *ActivityId.ToString());
	
	// 使用传入的ActivityId触发导航事件
	if (OnNavItemSelected.IsBound())
	{
		OnNavItemSelected.Broadcast(ActivityId);
	}
	SetSelectedActivity(ActivityId);
	
	// 直接切换到对应活动页面
	SwitchToActivityPage(ActivityId);
}

// 新增：针对不同索引的按钮点击处理函数
void UActivityNavMenuWidget::OnButtonClicked_Id0() { if (NavItems.IsValidIndex(0)) OnButtonClickedWithId(NavItems[0].ActivityId); }
void UActivityNavMenuWidget::OnButtonClicked_Id1() { if (NavItems.IsValidIndex(1)) OnButtonClickedWithId(NavItems[1].ActivityId); }
void UActivityNavMenuWidget::OnButtonClicked_Id2() { if (NavItems.IsValidIndex(2)) OnButtonClickedWithId(NavItems[2].ActivityId); }
void UActivityNavMenuWidget::OnButtonClicked_Id3() { if (NavItems.IsValidIndex(3)) OnButtonClickedWithId(NavItems[3].ActivityId); }
void UActivityNavMenuWidget::OnButtonClicked_Default() { 
	UE_LOG(LogTemp, Warning, TEXT("ActivityNavMenuWidget: 点击了默认按钮处理函数"));
	if (NavItems.Num() > 0) OnButtonClickedWithId(NavItems[0].ActivityId); 
}

void UActivityNavMenuWidget::SetSelectedActivity(FName ActivityId)
{
	UE_LOG(LogTemp, Warning, TEXT("🎯 设置选中活动: %s"), *ActivityId.ToString());
	
	CurrentSelectedActivity = ActivityId;
	
	// 更新NavItems数组中的选中状态
	for (FActivityNavItem& Item : NavItems)
	{
		Item.bIsSelected = (Item.ActivityId == ActivityId);
	}
	
	// 更新所有按钮的视觉状态
	UpdateAllButtonsVisualState();
}

void UActivityNavMenuWidget::RefreshRedDots()
{
	UE_LOG(LogTemp, Warning, TEXT("🔄 刷新所有导航项红点状态"));
	
	// 通过GameInstance获取ActivitySubsystem
	UGameInstance* GameInstance = GetGameInstance();
	if (!GameInstance)
	{
		UE_LOG(LogTemp, Error, TEXT("❌ 无法获取GameInstance"));
		return;
	}
	
	UActivitySubsystem* ActivitySub = GameInstance->GetSubsystem<UActivitySubsystem>();
	if (!ActivitySub)
	{
		UE_LOG(LogTemp, Error, TEXT("❌ 无法获取ActivitySubsystem"));
		return;
	}
	
	// 获取红点管理器
	URedDotManager* RedDotManager = ActivitySub->GetRedDotManager();
	if (!RedDotManager)
	{
		UE_LOG(LogTemp, Error, TEXT("❌ 无法获取RedDotManager"));
		return;
	}
	
	// 刷新红点数据
	RedDotManager->RefreshAllRedDots();
	
	// 更新所有按钮的红点显示
	for (int32 i = 0; i < NavContainer->GetChildrenCount(); ++i)
	{
		UWidget* ChildWidget = NavContainer->GetChildAt(i);
		UActivityNavButton* NavButton = Cast<UActivityNavButton>(ChildWidget);
		
		if (NavButton && NavButton->RedDotImage)
		{
			FName ButtonActivityId = NavButton->GetActivityId();
			int32 ActivityId = FCString::Atoi(*ButtonActivityId.ToString());
			
			// 获取该活动的红点数据
			FRedDotData RedDotData = RedDotManager->GetRedDotData(ActivityId);
			
			// 更新按钮的红点显示
			bool bShowRedDot = RedDotData.bShouldShow && RedDotData.DotType != ERedDotType::None;
			NavButton->SetRedDot(bShowRedDot, RedDotData.DotValue);
			
			UE_LOG(LogTemp, Log, TEXT("按钮 [%s] 红点状态更新: 显示=%s, 类型=%d, 数值=%d"), 
				*ButtonActivityId.ToString(), 
				bShowRedDot ? TEXT("是") : TEXT("否"),
				(int32)RedDotData.DotType,
				RedDotData.DotValue);
		}
	}
}

void UActivityNavMenuWidget::UpdateNavItemRedDot(FName ActivityId, const FRedDotData& RedDotData)
{
	// 更新指定导航项的红点状�?
	// UE_LOG(LogTemp, Log, TEXT("ActivityNavMenuWidget: 更新活动[%s]的红点状�?), *ActivityId.ToString());
}

void UActivityNavMenuWidget::RefreshTimeInfos()
{
	// 刷新所有导航项的时间信息显�?
	// 这里应该从ActivitySubsystem获取最新的时间数据
	// UE_LOG(LogTemp, Log, TEXT("ActivityNavMenuWidget: 刷新时间信息显示"));
}

void UActivityNavMenuWidget::UpdateNavItemTimeInfo(FName ActivityId, const FActivityInfoRow& TimeInfo)
{
	// 更新指定导航项的时间信息
	// UE_LOG(LogTemp, Log, TEXT("ActivityNavMenuWidget: 更新活动[%s]的时间信�?), *ActivityId.ToString());
}



void UActivityNavMenuWidget::PopulateNavItemsFromSubsystem()
{
	// 首先尝试获取GameInstance
	UGameInstance* GameInstance = GetGameInstance();
	if (!GameInstance)
	{
		UE_LOG(LogTemp, Error, TEXT("ActivityNavMenuWidget: 无法获取GameInstance"));
		CreateTestData();
		return;
	}
	
	UActivitySubsystem* ActivitySub = GameInstance->GetSubsystem<UActivitySubsystem>();
	if (!ActivitySub)
	{
		// UE_LOG(LogTemp, Warning, TEXT("ActivityNavMenuWidget: 无法获取ActivitySubsystem，尝试直接加载DataTable"));
		
		// 备用方案：直接加载DataTable
		LoadNavItemsFromDataTable();
		return;
	}
	
	// 获取所有活动配�?
	TArray<const FActivityInfoRow*> AllActivities = ActivitySub->GetAllNavItems();
	
	// UE_LOG(LogTemp, Log, TEXT("ActivityNavMenuWidget: 从Subsystem获取�?%d 个活�?), AllActivities.Num());
	
	NavItems.Empty();
	
	for (const FActivityInfoRow* ActivityInfo : AllActivities)
	{
		if (!ActivityInfo) continue;
		
		FActivityNavItem NavItem;
		NavItem.ActivityId = FName(*FString::FromInt(ActivityInfo->ActivityID));
		NavItem.LinkedActivityId = NavItem.ActivityId;
		// 注意：这里不再直接设置IconTexture，而是在创建Widget时从DataTable获取
		
		NavItems.Add(NavItem);
		
		// UE_LOG(LogTemp, Log, TEXT("ActivityNavMenuWidget: 添加导航项 ActivityID=%d, NavId=%s, 标题=%s"), 
            // 暂时注释掉有[[nodiscard]]问题的日志
	}
	
	// UE_LOG(LogTemp, Log, TEXT("ActivityNavMenuWidget: 成功填充 %d 个导航项"), NavItems.Num());
}

void UActivityNavMenuWidget::LoadNavItemsFromDataTable()
{
	// 先尝试直接加载DataTable
	FString InfoPath = TEXT("/Game/UI/Activity/Data/DT_ActivityInfoRow");
	UDataTable* ActivityInfoTable = Cast<UDataTable>(StaticLoadObject(UDataTable::StaticClass(), nullptr, *InfoPath));
	
	if (!ActivityInfoTable)
	{
		UE_LOG(LogTemp, Error, TEXT("ActivityNavMenuWidget: 无法加载ActivityInfo DataTable: %s"), *InfoPath);
		// 如果加载失败，使用硬编码测试数据
		CreateTestData();
		return;
	}
	
	// UE_LOG(LogTemp, Log, TEXT("ActivityNavMenuWidget: 成功加载ActivityInfo DataTable"));
	
	// 获取所有行数据
	static const FString ContextString(TEXT("NavMenuContext"));
	TArray<FActivityInfoRow*> AllRows;
	ActivityInfoTable->GetAllRows<FActivityInfoRow>(ContextString, AllRows);
	
	// UE_LOG(LogTemp, Log, TEXT("ActivityNavMenuWidget: DataTable中共�?%d 行数�?), AllRows.Num());
	
	NavItems.Empty();
	
	// 筛选出导航项（CategoryHeader类型�?
	for (FActivityInfoRow* Row : AllRows)
	{
		if (Row && Row->ActivityType == EActivityType::CategoryHeader)
		{
			FActivityNavItem NavItem;
			NavItem.ActivityId = FName(*FString::FromInt(Row->ActivityID));
			NavItem.LinkedActivityId = NavItem.ActivityId;
			// 注意：图标纹理将在创建Widget时从DataTable获取
			
			NavItems.Add(NavItem);
			
			// UE_LOG(LogTemp, Log, TEXT("ActivityNavMenuWidget: 添加导航项 ActivityID=%d, NavId=%s, 标题=%s"), 
                // 暂时注释掉有[[nodiscard]]问题的日志
		}
	}
	
	// 按SortOrder排序
	NavItems.Sort([](const FActivityNavItem& A, const FActivityNavItem& B) {
		// 这里需要通过ActivityId获取对应的ActivityInfo来比较SortOrder
		// 简化处理：按ActivityId数字排序
		return FCString::Atoi(*A.ActivityId.ToString()) < FCString::Atoi(*B.ActivityId.ToString());
	});
	
	int32 NavItemCount = NavItems.Num();
	// UE_LOG(LogTemp, Log, TEXT("ActivityNavMenuWidget: 直接加载完成，共 %d 个导航项"), NavItemCount);
}

void UActivityNavMenuWidget::LoadAllActivityItemsFromDataTable()
{
	// 直接加载DataTable
	FString InfoPath = TEXT("/Game/UI/Activity/Data/DT_ActivityInfoRow");
	UDataTable* ActivityInfoTable = Cast<UDataTable>(StaticLoadObject(UDataTable::StaticClass(), nullptr, *InfoPath));
	
	if (!ActivityInfoTable)
	{
		UE_LOG(LogTemp, Error, TEXT("❌ ActivityNavMenuWidget: 无法加载ActivityInfo DataTable: %s"), *InfoPath);
		CreateTestData();
		return;
	}
	
	UE_LOG(LogTemp, Warning, TEXT("✅ 成功加载ActivityInfo DataTable"));
	
	// 获取所有行数据
	static const FString ContextString(TEXT("NavMenuContext"));
	TArray<FActivityInfoRow*> AllRows;
	ActivityInfoTable->GetAllRows<FActivityInfoRow>(ContextString, AllRows);
	
	UE_LOG(LogTemp, Warning, TEXT("📊 DataTable中共有 %d 行数据"), AllRows.Num());
	
	NavItems.Empty();
	
	// 为每一行创建导航项（显示所有活动）
	for (FActivityInfoRow* Row : AllRows)
	{
		if (Row)
		{
			FActivityNavItem NavItem;
			NavItem.ActivityId = FName(*FString::FromInt(Row->ActivityID));
			NavItem.LinkedActivityId = NavItem.ActivityId;
			
			NavItems.Add(NavItem);
			
			UE_LOG(LogTemp, Warning, TEXT("添加导航项: ActivityID=%d, NavId=%s"), 
				Row->ActivityID, *NavItem.ActivityId.ToString());
		}
	}
	
	// 按ActivityID排序
	NavItems.Sort([](const FActivityNavItem& A, const FActivityNavItem& B) {
		return FCString::Atoi(*A.ActivityId.ToString()) < FCString::Atoi(*B.ActivityId.ToString());
	});
	
	UE_LOG(LogTemp, Warning, TEXT("✅ 成功加载 %d 个导航项"), NavItems.Num());
}

FText UActivityNavMenuWidget::GetActivityDisplayName(FName ActivityId)
{
	// 从DataTable获取活动显示名称
	FString InfoPath = TEXT("/Game/UI/Activity/Data/DT_ActivityInfoRow");
	UDataTable* ActivityInfoTable = Cast<UDataTable>(StaticLoadObject(UDataTable::StaticClass(), nullptr, *InfoPath));
	
	if (ActivityInfoTable)
	{
		static const FString ContextString(TEXT("NavMenuContext"));
		int32 TargetActivityId = FCString::Atoi(*ActivityId.ToString());
		FName RowName(*FString::FromInt(TargetActivityId));
		
		// 查找活动信息
		FActivityInfoRow* ActivityInfo = ActivityInfoTable->FindRow<FActivityInfoRow>(RowName, ContextString);
		
		// 备用方案：全表遍历
		if (!ActivityInfo)
		{
			TArray<FActivityInfoRow*> AllRows;
			ActivityInfoTable->GetAllRows<FActivityInfoRow>(ContextString, AllRows);
			for (FActivityInfoRow* Row : AllRows)
			{
				if (Row && Row->ActivityID == TargetActivityId)
				{
					ActivityInfo = Row;
					break;
				}
			}
		}
		
		if (ActivityInfo && !ActivityInfo->DisplayName.IsEmpty())
		{
			return ActivityInfo->DisplayName;
		}
	}
	
	// 如果找不到或为空，返回默认名称
	return FText::FromString(FString::Printf(TEXT("活动 %s"), *ActivityId.ToString()));
}

FName UActivityNavMenuWidget::GetDefaultSelectedActivityId()
{
	// 从DataTable中查找设置了bIsDefaultSelected=true的活动
	FString InfoPath = TEXT("/Game/UI/Activity/Data/DT_ActivityInfoRow");
	UDataTable* ActivityInfoTable = Cast<UDataTable>(StaticLoadObject(UDataTable::StaticClass(), nullptr, *InfoPath));
	
	if (ActivityInfoTable)
	{
		static const FString ContextString(TEXT("NavMenuContext"));
		TArray<FActivityInfoRow*> AllRows;
		ActivityInfoTable->GetAllRows<FActivityInfoRow>(ContextString, AllRows);
		
		for (FActivityInfoRow* Row : AllRows)
		{
			if (Row && Row->bIsDefaultSelected)
			{
				UE_LOG(LogTemp, Warning, TEXT("✅ 找到默认选中活动: ID=%d, 标题=%s"), 
					Row->ActivityID, *Row->DisplayName.ToString());
				return FName(*FString::FromInt(Row->ActivityID));
			}
		}
	}
	
	UE_LOG(LogTemp, Warning, TEXT("⚠️ 未找到设置为默认选中的活动"));
	return NAME_None;
}

void UActivityNavMenuWidget::UpdateAllButtonsVisualState()
{
	UE_LOG(LogTemp, Warning, TEXT("🔄 更新所有按钮视觉状态"));
	
	// 遍历NavContainer中的所有子Widget
	for (int32 i = 0; i < NavContainer->GetChildrenCount(); ++i)
	{
		UWidget* ChildWidget = NavContainer->GetChildAt(i);
		UActivityNavButton* NavButton = Cast<UActivityNavButton>(ChildWidget);
		
		if (NavButton && NavButton->MainButton)
		{
			// 根据NavItems中的状态更新按钮视觉
			FName ButtonActivityId = NavButton->GetActivityId();
			bool bShouldBeSelected = false;
			
			// 在NavItems中查找对应的选中状态
			for (const FActivityNavItem& Item : NavItems)
			{
				if (Item.ActivityId == ButtonActivityId)
				{
					bShouldBeSelected = Item.bIsSelected;
					break;
				}
			}
			
			// 更新按钮的选中状态
			NavButton->SetSelected(bShouldBeSelected);
			UE_LOG(LogTemp, Log, TEXT("按钮 [%s] 选中状态更新为: %s"), 
				*ButtonActivityId.ToString(), bShouldBeSelected ? TEXT("选中") : TEXT("未选中"));
		}
	}
}

void UActivityNavMenuWidget::CreateTestData()
{
	UE_LOG(LogTemp, Warning, TEXT("ActivityNavMenuWidget: 使用测试数据"));
	
	NavItems.Empty();
	
	// 创建测试导航项- 模拟实际的DataTable数据结构
	FActivityNavItem TestItem1;
	TestItem1.ActivityId = FName(TEXT("101"));
	TestItem1.LinkedActivityId = TestItem1.ActivityId;
	TestItem1.bIsSelected = false;
	NavItems.Add(TestItem1);
	
	UE_LOG(LogTemp, Log, TEXT("ActivityNavMenuWidget: 创建了 %d 个测试导航项"), NavItems.Num());
}

// ==================== 页面管理实现 ====================

void UActivityNavMenuWidget::SwitchToActivityPage(FName ActivityId)
{
	// UE_LOG(LogTemp, Warning, TEXT("=== SwitchToActivityPage 开始处�?ActivityId: %s ==="), *ActivityId.ToString());
	
	// 检查PageContainer是否存在
	if (!PageContainer)
	{
		UE_LOG(LogTemp, Error, TEXT("ActivityNavMenuWidget: PageContainer未绑定，请在蓝图中设置页面容器"));
		return;
	}
	else
	{
		// UE_LOG(LogTemp, Warning, TEXT("ActivityNavMenuWidget: PageContainer存在，可见�? %d"), (int32)PageContainer->GetVisibility());
	}
	
	// 从缓存中查找页面
	UUserWidget* TargetPage = nullptr;
	if (PageCache.Contains(ActivityId))
	{
		TargetPage = PageCache[ActivityId].Get();
		if (!TargetPage)
		{
			// 如果缓存的对象已被销毁，从缓存中移除
			PageCache.Remove(ActivityId);
			// UE_LOG(LogTemp, Warning, TEXT("ActivityNavMenuWidget: 缓存页面已失效，移除缓存 [%s]"), *ActivityId.ToString());
		}
		else
		{
			// UE_LOG(LogTemp, Log, TEXT("ActivityNavMenuWidget: 从缓存加载页�?[%s]"), *ActivityId.ToString());
		}
	}
	
	// 如果缓存中没有，则创建新页面
	if (!TargetPage)
	{
		// 直接从DataTable加载活动配置，获取TargetPageClass
		FString InfoPath = TEXT("/Game/UI/Activity/Data/DT_ActivityInfoRow");
		UDataTable* ActivityInfoTable = Cast<UDataTable>(StaticLoadObject(UDataTable::StaticClass(), nullptr, *InfoPath));
		
		if (!ActivityInfoTable)
		{
			UE_LOG(LogTemp, Error, TEXT("ActivityNavMenuWidget: 无法加载ActivityInfo DataTable: %s"), *InfoPath);
			
			// 回退到默认页�?
			if (DefaultPageClass)
			{
				TargetPage = CreateActivityPage(DefaultPageClass);
			}
			else
			{
				UE_LOG(LogTemp, Error, TEXT("ActivityNavMenuWidget: 未设置DefaultPageClass"));
				return;
			}
		}
		else
		{
			// 从DataTable中查找对应ActivityID的配�?
			static const FString ContextString(TEXT("ActivityNavContext"));
			TArray<FActivityInfoRow*> AllRows;
			ActivityInfoTable->GetAllRows<FActivityInfoRow>(ContextString, AllRows);
			
			FActivityInfoRow* TargetConfig = nullptr;
			int32 TargetActivityId = FCString::Atoi(*ActivityId.ToString());
			
			for (FActivityInfoRow* Row : AllRows)
			{
				if (Row && Row->ActivityID == TargetActivityId)
				{
					TargetConfig = Row;
					break;
				}
			}
			
			if (!TargetConfig)
			{
				// UE_LOG(LogTemp, Warning, TEXT("ActivityNavMenuWidget: DataTable中未找到ActivityID�?%d 的配置，使用默认页面"), TargetActivityId);
				
				// 回退到默认页�?
				if (DefaultPageClass)
				{
					TargetPage = CreateActivityPage(DefaultPageClass);
				}
				else
				{
					UE_LOG(LogTemp, Error, TEXT("ActivityNavMenuWidget: 未设置DefaultPageClass"));
					return;
				}
			}
			else
			{
				// 使用DataTable中配置的目标页面�?
				if (TargetConfig->TargetPageClass)
				{
					TargetPage = CreateActivityPage(TargetConfig->TargetPageClass);
					// UE_LOG(LogTemp, Log, TEXT("ActivityNavMenuWidget: 使用DataTable配置的页面类 [%s]"), *TargetConfig->TargetPageClass->GetName());
				}
				else
				{
					// UE_LOG(LogTemp, Warning, TEXT("ActivityNavMenuWidget: ActivityID %d 未指定TargetPageClass，使用默认页�?), TargetActivityId);
					
					// 回退到默认页�?
					if (DefaultPageClass)
					{
						TargetPage = CreateActivityPage(DefaultPageClass);
					}
					else
					{
						UE_LOG(LogTemp, Error, TEXT("ActivityNavMenuWidget: 未设置DefaultPageClass"));
						return;
					}
				}
			}
		}
		
		// 缓存新创建的页面
		if (TargetPage)
		{
			PageCache.Add(ActivityId, TargetPage);
			// UE_LOG(LogTemp, Log, TEXT("ActivityNavMenuWidget: 创建并缓存页�?[%s]"), *ActivityId.ToString());
		}
	}
	
	// 显示页面
	if (TargetPage)
	{
		ShowPageInContainer(TargetPage);
		// UE_LOG(LogTemp, Log, TEXT("ActivityNavMenuWidget: 成功切换到页�?[%s]"), *ActivityId.ToString());
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("ActivityNavMenuWidget: 无法创建页面 [%s]"), *ActivityId.ToString());
	}
}

UUserWidget* UActivityNavMenuWidget::CreateActivityPage(TSubclassOf<UUserWidget> PageClass)
{
	if (!PageClass)
	{
		UE_LOG(LogTemp, Error, TEXT("ActivityNavMenuWidget: PageClass为空"));
		return nullptr;
	}
	
	UUserWidget* NewPage = CreateWidget<UUserWidget>(GetWorld(), PageClass);
	if (NewPage)
	{
		// UE_LOG(LogTemp, Log, TEXT("ActivityNavMenuWidget: 成功创建页面实例 [%s]"), *PageClass->GetName());
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("ActivityNavMenuWidget: 创建页面实例失败 [%s]"), *PageClass->GetName());
	}
	
	return NewPage;
}

void UActivityNavMenuWidget::ShowPageInContainer(UUserWidget* PageWidget)
{
	// UE_LOG(LogTemp, Warning, TEXT("=== ShowPageInContainer 开�?==="));
	
	if (!PageContainer || !PageWidget)
	{
		UE_LOG(LogTemp, Error, TEXT("ActivityNavMenuWidget: PageContainer或PageWidget为空"));
		return;
	}
	else
	{
		// UE_LOG(LogTemp, Warning, TEXT("ActivityNavMenuWidget: PageContainer和PageWidget都存�?));
		// UE_LOG(LogTemp, Warning, TEXT("PageContainer子项数量: %d"), PageContainer->GetChildrenCount());
	}
	
	// 清理当前页面
	ClearCurrentPage();
	
	// 添加新页面到容器
	PageContainer->AddChild(PageWidget);
	PageWidget->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
	CurrentPage = PageWidget;
	
	// 重要：设置Canvas Panel Slot填充全屏
	UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(PageWidget->Slot);
	if (CanvasSlot)
	{
		// 设置锚点填充整个容器
		CanvasSlot->SetAnchors(FAnchors(0.0f, 0.0f, 1.0f, 1.0f));
		// 设置偏移量为0，确保完全填充
		CanvasSlot->SetOffsets(FMargin(0.0f, 0.0f, 0.0f, 0.0f));
		// 设置Z轴顺序确保显示在最上层
		CanvasSlot->SetZOrder(0);
		
		// UE_LOG(LogTemp, Log, TEXT("ActivityNavMenuWidget: 成功设置Canvas Slot全屏锚点"));
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("ActivityNavMenuWidget: 无法获取CanvasPanelSlot"));
	}
	
	// UE_LOG(LogTemp, Log, TEXT("ActivityNavMenuWidget: 页面已添加到容器并设为可�?));
	// UE_LOG(LogTemp, Warning, TEXT("PageContainer子项数量(添加�?: %d"), PageContainer->GetChildrenCount());
	// UE_LOG(LogTemp, Warning, TEXT("新页面可见�? %d"), (int32)PageWidget->GetVisibility());
}

void UActivityNavMenuWidget::ClearCurrentPage()
{
	if (CurrentPage.IsValid())
	{
		CurrentPage->RemoveFromParent();
		CurrentPage.Reset();
		// UE_LOG(LogTemp, Log, TEXT("ActivityNavMenuWidget: 当前页面已清�?));
	}
}
