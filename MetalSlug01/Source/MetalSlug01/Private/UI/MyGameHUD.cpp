#include "UI/MyGameHUD.h"
#include "Blueprint/UserWidget.h"
#include "Systems/GameFlowSubsystem.h"
#include "UI/Game/GameHUDWidget.h"

void AMyGameHUD::BeginPlay()
{
	Super::BeginPlay();

	// 提前在后台创建所有游戏HUD并隐藏，避免战斗瞬间卡顿（对象池/预加载思维）
	CreateGameHUD();

	// ==========================================
	// 向管家订阅状态改变的“报纸”
	// ==========================================
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UGameFlowSubsystem* FlowSubsystem = GI->GetSubsystem<UGameFlowSubsystem>())
		{
			FlowSubsystem->OnStateChanged.AddDynamic(this, &AMyGameHUD::OnGameFlowStateChanged);
			
			// 防呆设计：刚出生时主动问一下现在的状态，同步初始表现
			OnGameFlowStateChanged(FlowSubsystem->GetCurrentState());
		}
	}
}

void AMyGameHUD::CreateGameHUD()
{
	// 【工业规范】：UI的创建必须严格绑定到真实的玩家控制器，而不是宽泛的 GetWorld()
	APlayerController* PC = GetOwningPlayerController();
	if (!PC)
	{
		UE_LOG(LogTemp, Error, TEXT("[MyGameHUD] 严重错误：未获取到本地 PlayerController！"));
		return;
	}

	// 1. 创建并隐藏主 HUD
	if (GameHUDWidgetClass)
	{
		GameHUDWidget = CreateWidget<UGameHUDWidget>(PC, GameHUDWidgetClass);
		if (GameHUDWidget)
		{
			GameHUDWidget->SetVisibility(ESlateVisibility::Collapsed);
			GameHUDWidget->AddToViewport(0); // Z-Order为0，贴在屏幕底层
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[MyGameHUD] GameHUDWidgetClass 蓝图中未配置！"));
	}

	// 2. 【修复】：创建并隐藏准星
	if (CrosshairWidgetClass)
	{
		CrosshairWidget = CreateWidget<UUserWidget>(PC, CrosshairWidgetClass);
		if (CrosshairWidget)
		{
			CrosshairWidget->SetVisibility(ESlateVisibility::Collapsed);
			CrosshairWidget->AddToViewport(1); // Z-Order为1，确保准星压在血条等元素上方
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[MyGameHUD] CrosshairWidgetClass 蓝图中未配置！"));
	}
}

void AMyGameHUD::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 【工业规范】：Actor 被销毁前（例如切换地图或退出游戏），必须退订报纸，否则会报野指针崩溃！
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UGameFlowSubsystem* FlowSubsystem = GI->GetSubsystem<UGameFlowSubsystem>())
		{
			FlowSubsystem->OnStateChanged.RemoveDynamic(this, &AMyGameHUD::OnGameFlowStateChanged);
		}
	}
	Super::EndPlay(EndPlayReason);
}


// ==========================================
// 状态机核心调度逻辑
// ==========================================
void AMyGameHUD::OnGameFlowStateChanged(EMatchState NewState)
{
	// 【架构重构】：HUD 只负责 UI 元素的展示与隐藏。
	// 已将鼠标控制权交还给 PlayerController，避免代码职责互相覆盖。

	if (NewState == EMatchState::Battleing)
	{
		if (GameHUDWidget)
		{
			// 规范：使用 SelfHitTestInvisible，允许自身的按钮点击（如果有），但无视背景
			GameHUDWidget->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		}
		
		if (CrosshairWidget)
		{
			// 准星必须是纯穿透，否则会阻挡鼠标射线导致无法开火
			CrosshairWidget->SetVisibility(ESlateVisibility::HitTestInvisible);
		}
		
		UE_LOG(LogTemp, Log, TEXT("[MyGameHUD] 切换至战斗UI，成功展示主HUD与准星"));
	}
	else 
	{
		// 如果是房间态或其它状态，隐藏所有战斗 UI
		if (GameHUDWidget) GameHUDWidget->SetVisibility(ESlateVisibility::Collapsed);
		if (CrosshairWidget) CrosshairWidget->SetVisibility(ESlateVisibility::Collapsed);
	}
}
