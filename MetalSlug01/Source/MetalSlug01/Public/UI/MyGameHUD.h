#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "MyGameHUD.generated.h"

UCLASS()
class METALSLUG01_API AMyGameHUD : public AHUD
{
	GENERATED_BODY()

protected:
	virtual void BeginPlay() override;

	// 你当前要看的页面
	UPROPERTY(EditAnywhere, Category = "UI")
	TSubclassOf<class UUserWidget> MainWidgetClass;

private:
	UPROPERTY()
	UUserWidget* MainWidget;
};
