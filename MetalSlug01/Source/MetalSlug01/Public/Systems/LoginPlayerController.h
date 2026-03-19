#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "LoginPlayerController.generated.h"

class UUserWidget;

/**
 * 专门负责主菜单/登录地图的玩家控制器
 * 负责验票，并决定向玩家展示登录页还是局域网大厅
 */
UCLASS()
class METALSLUG01_API ALoginPlayerController : public APlayerController
{
	GENERATED_BODY()

protected:
	virtual void BeginPlay() override;

	// 登录界面的蓝图类
	UPROPERTY(EditDefaultsOnly, Category = "UI Config")
	TSubclassOf<UUserWidget> LoginUIClass;

	// 局域网大厅界面的蓝图类
	UPROPERTY(EditDefaultsOnly, Category = "UI Config")
	TSubclassOf<UUserWidget> LANRoomUIClass;
};