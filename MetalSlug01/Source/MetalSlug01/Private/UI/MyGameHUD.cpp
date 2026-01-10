// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/MyGameHUD.h"
#include "UI/GameRewardMainWidget.h"
#include "Blueprint/UserWidget.h"
#include "UI/Activity/Root/ActivityRoot.h"

AMyGameHUD::AMyGameHUD()
{
	// 构造函数逻辑
	// 这里的路径必须是你蓝图 WBP_RewardMain 存放的真实路径
	// 注意：点击资源右键 "Copy Reference"，去掉类名后缀，只保留路径
	static ConstructorHelpers::FClassFinder<UGameRewardMainWidget> WidgetAsset(TEXT("WidgetBlueprint'/Game/UI/WBP_RewardMain.WBP_RewardMain_C'"));
    
	if (WidgetAsset.Succeeded())
	{
		RewardWidgetClass = WidgetAsset.Class;
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("无法找到 UI 蓝图资源！请检查路径。"));
	}
}

void AMyGameHUD::BeginPlay()
{
	Super::BeginPlay();

	UE_LOG(LogTemp, Error, TEXT("AMyGameHUD::BeginPlay CALLED"));
	
	// 原签到逻辑
	if (RewardWidgetClass && !CachedRewardWidget)
	{
		CachedRewardWidget = CreateWidget<UGameRewardMainWidget>(GetWorld(), RewardWidgetClass);
		// 默认先不显示
	}

	// 新增：创建 ActivityRoot
	if (ActivityRootClass && !CachedActivityRootWidget)
	{
		CachedActivityRootWidget = CreateWidget<UActivityRoot>(GetWorld(), ActivityRootClass);
		if (CachedActivityRootWidget)
		{
			CachedActivityRootWidget->AddToViewport();
		}
	}
}

void AMyGameHUD::ToggleRewardUI(bool bShow)
{
	if (!CachedRewardWidget) return;

	APlayerController* PC = GetOwningPlayerController();
	if (!PC) return;

	if (bShow)
	{
		// 1. 添加到视口，设置高层级 Z-Order 为 10
		CachedRewardWidget->AddToViewport(10);

		// 2. 规范化输入模式：UI Only，并锁定鼠标到窗口
		FInputModeUIOnly InputMode;
		InputMode.SetWidgetToFocus(CachedRewardWidget->TakeWidget());
		InputMode.SetLockMouseToViewportBehavior(EMouseLockMode::DoNotLock);
        
		PC->SetInputMode(InputMode);
		PC->bShowMouseCursor = true; // 显示鼠标
	}
	else
	{
		// 3. 移除 UI 并恢复游戏输入模式
		CachedRewardWidget->RemoveFromParent();
        
		FInputModeGameOnly GameMode;
		PC->SetInputMode(GameMode);
		PC->bShowMouseCursor = false; // 隐藏鼠标
	}
}