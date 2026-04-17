#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "MyGameHUD.generated.h"

class UUserWidget;
class UGameHUDWidget;

UCLASS()
class METALSLUG01_API AMyGameHUD : public AHUD
{
	GENERATED_BODY()

protected:
	virtual void BeginPlay() override;
	
	// 工业级防泄漏，在 Actor 销毁时解绑委托
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
	// 获取游戏HUD Widget（供角色调用）
	UGameHUDWidget* GetGameHUDWidget() const { return GameHUDWidget; }

protected:
	// 主菜单Widget类
	UPROPERTY(EditAnywhere, Category = "UI")
	TSubclassOf<class UUserWidget> MainWidgetClass;

	// 游戏HUD Widget类
	UPROPERTY(EditAnywhere, Category = "UI")
	TSubclassOf<UGameHUDWidget> GameHUDWidgetClass;


private:
	UPROPERTY()
	UUserWidget* MainWidget;

	UPROPERTY()
	UGameHUDWidget* GameHUDWidget;

	// 创建游戏HUD
	void CreateGameHUD();
	
	// 状态机响应回调
	UFUNCTION()
	void OnGameFlowStateChanged(EMatchState NewState);
};
