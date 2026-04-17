#include "UI/MyGameHUD.h"
#include "Blueprint/UserWidget.h"
#include "Systems/GameFlowSubsystem.h"
#include "UI/Game/GameHUDWidget.h"

void AMyGameHUD::BeginPlay()
{
	Super::BeginPlay();

	// 创建游戏HUD
	CreateGameHUD();

	// ==========================================
	// 【核心】：向管家订阅状态改变的“报纸”
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
	if (!GameHUDWidgetClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("GameHUDWidgetClass is not set in MyGameHUD"));
		return;
	}

	GameHUDWidget = CreateWidget<UGameHUDWidget>(GetWorld(), GameHUDWidgetClass);
	if (GameHUDWidget)
	{
		// 游戏HUD添加到视口（默认先完全隐藏，等战斗态再切出来）
		GameHUDWidget->SetVisibility(ESlateVisibility::Collapsed);
		GameHUDWidget->AddToViewport();
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
	// 只有进入战斗状态，才显示血条/准星 HUD
	if (NewState == EMatchState::Battleing)
	{
		if (GameHUDWidget)
		{
			// HitTestInvisible: 允许鼠标穿透它点击到后面的 3D 世界
			GameHUDWidget->SetVisibility(ESlateVisibility::HitTestInvisible);
		}

		// 【极其关键】：交出鼠标控制权给玩家，准备打架！
		if (APlayerController* PC = GetOwningPlayerController())
		{
			PC->SetShowMouseCursor(false);
			PC->SetInputMode(FInputModeGameOnly());
		}
		
		UE_LOG(LogTemp, Log, TEXT("[MyGameHUD] 切换至战斗UI，已隐藏鼠标！"));
	}
	else 
	{
		// 如果是房间态 (InRoom) 或其他状态，则隐藏战斗 HUD
		if (GameHUDWidget)
		{
			GameHUDWidget->SetVisibility(ESlateVisibility::Collapsed);
		}
	}
}
