
#include "MetalSlug01GameModeBase.h"
#include "UI/MyGameHUD.h"  


#include "MetalSlug01GameModeBase.h"
#include "UObject/ConstructorHelpers.h"

AMyGameModeBase::AMyGameModeBase()
{
	// 1. 设置默认生成的角色类 (Default Pawn)
	// 假设你的玩家蓝图路径是 /Content/Blueprints/BP_MSPlayer.uasset
	// 注意：路径结尾必须带有 _C 才是引用蓝图生成的类
	static ConstructorHelpers::FClassFinder<APawn> PlayerPawnBPClass(TEXT("/Script/Engine.Blueprint'/Game/Blueprints/Characters/BP_MSPlayer.BP_MSPlayer_C'"));
    
	if (PlayerPawnBPClass.Class != nullptr)
	{
		DefaultPawnClass = PlayerPawnBPClass.Class;
	}

	// 2. 如果你有自定义的 PlayerController，也在这里设置
	// PlayerControllerClass = AMSPlayerController::StaticClass();

	//hud
	HUDClass = AMyGameHUD::StaticClass();
}