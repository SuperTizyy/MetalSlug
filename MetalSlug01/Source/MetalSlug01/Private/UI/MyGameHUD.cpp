#include "UI/MyGameHUD.h"
#include "Blueprint/UserWidget.h"

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
}
