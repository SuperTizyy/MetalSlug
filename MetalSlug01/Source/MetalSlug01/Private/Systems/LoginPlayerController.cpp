#include "Systems/LoginPlayerController.h"
#include "Systems/GameFlowSubsystem.h"
#include "Blueprint/UserWidget.h"
#include "UI/Login/Core/AccountSubsystem.h"

void ALoginPlayerController::BeginPlay()
{
	Super::BeginPlay();

	// 强制显示鼠标，设置输入模式为仅 UI
	bShowMouseCursor = true;
	SetInputMode(FInputModeUIOnly());

	FString CurrentMap = GetWorld()->GetMapName();

	if (UGameFlowSubsystem* FlowSubsystem = GetGameInstance()->GetSubsystem<UGameFlowSubsystem>())
	{
		// 1. 订阅管家的广播频道
		FlowSubsystem->OnStateChanged.AddDynamic(this, &ALoginPlayerController::OnFlowStateChanged);

		// 2. 确保在 L_Login 地图中才执行 UI 挂载逻辑
		if (GetWorld()->GetMapName().Contains(TEXT("L_Login")))
		{
			
			EMatchState CurrentState = FlowSubsystem->GetCurrentState();

			// ==========================================
			// 【终极架构修复】：引入 0.2 秒安全延时！
			// 彻底解决 UE 底层的"视口未就绪 (Viewport Not Ready)"导致的 UI 吞噬问题。
			// 确保地图和摄像机完全准备完毕后，再进行 UI 挂载。
			// ==========================================
			FTimerHandle InitHandle;
			GetWorld()->GetTimerManager().SetTimer(InitHandle, FTimerDelegate::CreateLambda([this, CurrentState]()
			{
				if (CurrentState == EMatchState::MainLobby)
				{
					OnFlowStateChanged(EMatchState::MainLobby);
				}
				else if (CurrentState == EMatchState::Login)
				{
					OnFlowStateChanged(EMatchState::Login);
				}
				else
				{
					if (UGameFlowSubsystem* FS = GetGameInstance()->GetSubsystem<UGameFlowSubsystem>())
					{
						FS->TransitToState(EMatchState::Login);
					}
				}
			}), 0.2f, false);
		}
		else
		{
			if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 10.0f, FColor::Red, FString::Printf(TEXT("[LoginPC Debug] 不在 L_Login 地图中，跳过 UI 挂载逻辑")));
		}
	}
}

void ALoginPlayerController::OnFlowStateChanged(EMatchState NewState)
{
	// ==========================================
	// 状态 1：登录阶段
	// ==========================================
	if (NewState == EMatchState::Login)
	{
		// 强杀可能残留的大厅 UI
		if (ActiveLobbyWidget) { ActiveLobbyWidget->RemoveFromParent(); ActiveLobbyWidget = nullptr; }
		if (ActiveLoginWidget) { ActiveLoginWidget->RemoveFromParent(); ActiveLoginWidget = nullptr; }

		// 【关键校验点】：检查蓝图类是否有效！
		if (LoginUIClass)
		{
			
			ActiveLoginWidget = CreateWidget<UUserWidget>(this, LoginUIClass);
			if (ActiveLoginWidget)
			{
				ActiveLoginWidget->AddToViewport(9999);
			}
			else
			{
				if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 20.0f, FColor::Red, TEXT("[LoginPC Debug 错误] CreateWidget 返回空！"));
			}
		}
	}
	// ==========================================
	// 状态 2：主大厅阶段
	// ==========================================
	else if (NewState == EMatchState::MainLobby)
	{
		if (ActiveLoginWidget) { ActiveLoginWidget->RemoveFromParent(); ActiveLoginWidget = nullptr; }
		if (ActiveLobbyWidget) { ActiveLobbyWidget->RemoveFromParent(); ActiveLobbyWidget = nullptr; }

		if (LANRoomUIClass)
		{
			ActiveLobbyWidget = CreateWidget<UUserWidget>(this, LANRoomUIClass);
			if (ActiveLobbyWidget)
			{
				ActiveLobbyWidget->AddToViewport();
			}
		}
	}
	else
	{
		// 统统销毁，干干净净地离开
		if (ActiveLoginWidget) { ActiveLoginWidget->RemoveFromParent(); ActiveLoginWidget = nullptr; }
		if (ActiveLobbyWidget) { ActiveLobbyWidget->RemoveFromParent(); ActiveLobbyWidget = nullptr; }
	}
}
