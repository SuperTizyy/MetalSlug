#include "UI/MyGameHUD.h"
#include "Blueprint/UserWidget.h"
#include "UI/Game/GameHUDWidget.h"

void AMyGameHUD::BeginPlay()
{
	Super::BeginPlay();

	if (!MainWidgetClass)
	{
		return;
	}

	MainWidget = CreateWidget<UUserWidget>(GetWorld(), MainWidgetClass);
	if (MainWidget)
	{
		MainWidget->AddToViewport();
	}

	// 创建游戏HUD
	CreateGameHUD();
}

void AMyGameHUD::CreateGameHUD()
{
	if (!GameHUDWidgetClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("GameHUDWidgetClass is not set in MyGameHUD"));
		return;
	}

	GameHUDWidget = CreateWidget<UGameHUDWidget>(GetWorld(), GameHUDWidgetClass);
	if (GameHUDWidget)
	{
		// 游戏HUD添加到视口（默认隐藏，由游戏逻辑控制显示）
		GameHUDWidget->AddToViewport();
	}
}
