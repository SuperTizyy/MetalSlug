// 版权声明：在项目设置的描述页面填写您的版权信息


#include "MetalSlug01GameModeBase.h"
#include "UI/MyGameHUD.h"  // 引入我们之前编写的 HUD 头文件
#include "UObject/ConstructorHelpers.h"

// AMyBaseGameMode 构造函数
// 初始化游戏模式的各项基本配置，包括HUD类、玩家控制器类等
AMyBaseGameMode::AMyBaseGameMode()
{
	// 1. 指定 HUD 类（核心：确保客户端生成我们自定义的 HUD 管理器）
	// 注意：如果是 C++ 类，直接使用 StaticClass()
	HUDClass = AMyGameHUD::StaticClass();

	// 2. 如果你的 HUD 是蓝图实现的（例如 WBP_MyHUD），则需要路径搜索：
	// 通过 FClassFinder 查找蓝图类，确保能够正确加载蓝图资源
	static ConstructorHelpers::FClassFinder<AHUD> HUDBlueprintClass(TEXT("/Script/Engine.Blueprint'/Game/UI/WBP_MyGameHUD.WBP_MyGameHUD_C'"));
	if (HUDBlueprintClass.Class != nullptr)
	{
		HUDClass = HUDBlueprintClass.Class;
	}


	// 3. 指定默认的 PlayerController（负责处理输入切换逻辑）
	// PlayerControllerClass = AMyPlayerController::StaticClass();

	// 4. 指定默认的 Pawn（玩家控制的角色）
	// DefaultPawnClass = AMyCharacter::StaticClass();
}

// BeginPlay - 游戏开始时执行的初始化逻辑
// 在游戏开始前执行必要的逻辑处理，如日志输出、初始化状态等
void AMyBaseGameMode::BeginPlay()
{
	Super::BeginPlay();
    
	// 可以在此处打印日志，确认 HUD 是否正确加载
	if (HUDClass)
	{
		UE_LOG(LogTemp, Log, TEXT("GameMode: HUD 类已初始化为 %s"), *HUDClass->GetName());
	}
}