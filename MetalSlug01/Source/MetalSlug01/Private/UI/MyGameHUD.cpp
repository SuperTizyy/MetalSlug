// 版权声明：在项目设置的描述页面填写您的版权信息


#include "UI/MyGameHUD.h"
#include "UI/GameRewardMainWidget.h"
#include "Blueprint/UserWidget.h"
#include "UI/Activity/Root/ActivityRoot.h"

// AMyGameHUD 构造函数
// 初始化HUD组件，查找并设置奖励界面的蓝图类
AMyGameHUD::AMyGameHUD()
{
	// 构造函数逻辑
	// 这里的路径必须是你蓝图 WBP_RewardMain 存放的真实路径
	// 注意：点击资源右键 "Copy Reference"，去掉类名后缀，只保留路径
	static ConstructorHelpers::FClassFinder<UGameRewardMainWidget> WidgetAsset(TEXT("WidgetBlueprint'/Game/UI/WBP_RewardMain.WBP_RewardMain_C'"));
    
	if (WidgetAsset.Succeeded())
	{
		// 成功找到蓝图类，设置为奖励界面类
		RewardWidgetClass = WidgetAsset.Class;
	}
	else
	{
		// 未找到蓝图类，输出错误日志
		UE_LOG(LogTemp, Error, TEXT("无法找到 UI 蓝图资源！请检查路径。"));
	}
}

// BeginPlay - HUD创建时执行的初始化逻辑
// 创建奖励界面和活动系统界面的实例，并添加到视口
void AMyGameHUD::BeginPlay()
{
	Super::BeginPlay();

	UE_LOG(LogTemp, Error, TEXT("AMyGameHUD::BeginPlay CALLED"));
	
	// 原奖励界面逻辑
	if (RewardWidgetClass && !CachedRewardWidget)
	{
		// 创建奖励界面实例并缓存
		CachedRewardWidget = CreateWidget<UGameRewardMainWidget>(GetWorld(), RewardWidgetClass);
		// 默认先不显示
	}

	// 新增：创建活动系统根界面
	if (ActivityRootClass && !CachedActivityRootWidget)
	{
		// 创建活动系统根界面实例并缓存
		CachedActivityRootWidget = CreateWidget<UActivityRoot>(GetWorld(), ActivityRootClass);
		if (CachedActivityRootWidget)
		{
			// 将活动系统界面添加到视口显示
			CachedActivityRootWidget->AddToViewport();
		}
	}
}

// ToggleRewardUI - 切换奖励界面显示状态
// 根据参数显示或隐藏奖励界面，并设置相应的输入模式
void AMyGameHUD::ToggleRewardUI(bool bShow)
{
	// 检查是否有缓存的奖励界面
	if (!CachedRewardWidget) return;

	// 获取玩家控制器
	APlayerController* PC = GetOwningPlayerController();
	if (!PC) return;

	if (bShow)
	{
		// 显示奖励界面
		// 1. 添加到视口，设置高层级 Z-Order 为 10
		CachedRewardWidget->AddToViewport(10);

		// 2. 设置输入模式为UI Only，并锁定鼠标到窗口
		FInputModeUIOnly InputMode;
		InputMode.SetWidgetToFocus(CachedRewardWidget->TakeWidget());
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
        
		// 应用新的输入模式
		PC->SetInputMode(InputMode);
		PC->bShowMouseCursor = true; // 显示鼠标
	}
	else
	{
		// 隐藏奖励界面
		// 3. 移除 UI 并恢复游戏输入模式
		CachedRewardWidget->RemoveFromParent();
        
		// 恢复仅游戏输入模式
		FInputModeGameOnly GameMode;
		PC->SetInputMode(GameMode);
		PC->bShowMouseCursor = false; // 隐藏鼠标
	}
}