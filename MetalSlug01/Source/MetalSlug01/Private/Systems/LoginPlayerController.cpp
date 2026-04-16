#include "Systems/LoginPlayerController.h"
#include "Systems/GameFlowSubsystem.h"
#include "Blueprint/UserWidget.h"
#include "UI/Login/Core/AccountSubsystem.h"

void ALoginPlayerController::BeginPlay()
{
	Super::BeginPlay();

	// 强制显示鼠标，设置输入模式为仅 UI
	bShowMouseCursor = true;
	FInputModeUIOnly InputMode;
	SetInputMode(InputMode);

	// 获取全局流程管家，并监听状态变化
	if (UGameFlowSubsystem* FlowSubsystem = GetGameInstance()->GetSubsystem<UGameFlowSubsystem>())
	{
		// 1. 订阅管家的广播频道
		FlowSubsystem->OnStateChanged.AddDynamic(this, &ALoginPlayerController::OnFlowStateChanged);
		
		// 2. 确保在 L_Login 地图中才执行 UI 挂载逻辑
		if (GetWorld()->GetMapName().Contains(TEXT("L_Login")))
		{
			// ==========================================
			// 【核心架构修复】：读取全局真实状态，绕过 TransitToState 的拦截
			// ==========================================
			EMatchState CurrentState = FlowSubsystem->GetCurrentState();

			if (CurrentState == EMatchState::MainLobby)
			{
				// 玩家正常退房回到大厅：手动强制调出大厅 UI
				OnFlowStateChanged(EMatchState::MainLobby);
				UE_LOG(LogTemp, Log, TEXT("[LoginPlayerController] 对齐状态：恢复大厅(MainLobby)界面。"));
			}
			else if (CurrentState == EMatchState::Login)
			{
				// 玩家注销账号返回登录页：手动强制调出登录 UI
				OnFlowStateChanged(EMatchState::Login);
				UE_LOG(LogTemp, Log, TEXT("[LoginPlayerController] 对齐状态：注销返回，恢复登录(Login)界面。"));
			}
			else
			{
				// 游戏第一次冷启动：推动管家进入 Login 状态
				FlowSubsystem->TransitToState(EMatchState::Login);
				UE_LOG(LogTemp, Log, TEXT("[LoginPlayerController] 初始化阶段，推入登录(Login)界面。"));
			}
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
		// 【防错机制】如果玩家是从大厅退回登录界面的，先把大厅 UI 销毁
		if (ActiveLobbyWidget)
		{
			ActiveLobbyWidget->RemoveFromParent();
			ActiveLobbyWidget = nullptr;
		}

		// 创建并显示登录 UI
		if (LoginUIClass && !ActiveLoginWidget)
		{
			ActiveLoginWidget = CreateWidget<UUserWidget>(this, LoginUIClass);
			if (ActiveLoginWidget)
			{
				ActiveLoginWidget->AddToViewport();
			}
		}
	}
	// ==========================================
	// 状态 2：主大厅阶段 (依然在这个地图内)
	// ==========================================
	else if (NewState == EMatchState::MainLobby)
	{
		// 【防错机制】登录成功了，先把登录框销毁
		if (ActiveLoginWidget)
		{
			ActiveLoginWidget->RemoveFromParent();
			ActiveLoginWidget = nullptr;
		}

		// 创建并显示大厅 UI (这里用你之前绑定的 LANRoomUIClass)
		if (LANRoomUIClass && !ActiveLobbyWidget)
		{
			ActiveLobbyWidget = CreateWidget<UUserWidget>(this, LANRoomUIClass);
			if (ActiveLobbyWidget)
			{
				ActiveLobbyWidget->AddToViewport();
			}
		}
	}
	// ==========================================
	// 其他状态（例如准备跳转到真正的沙漠战斗地图了）
	// ==========================================
	else 
	{
		// 无论当前挂着什么 UI，统统销毁，干干净净地离开这个关卡
		if (ActiveLoginWidget)
		{
			ActiveLoginWidget->RemoveFromParent();
			ActiveLoginWidget = nullptr;
		}
		if (ActiveLobbyWidget)
		{
			ActiveLobbyWidget->RemoveFromParent();
			ActiveLobbyWidget = nullptr;
		}
	}
}
