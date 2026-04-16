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
	if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 10.0f, FColor::Yellow, FString::Printf(TEXT("[LoginPC Debug] 当前地图: %s"), *CurrentMap));

	if (UGameFlowSubsystem* FlowSubsystem = GetGameInstance()->GetSubsystem<UGameFlowSubsystem>())
	{
		// 1. 订阅管家的广播频道
		FlowSubsystem->OnStateChanged.AddDynamic(this, &ALoginPlayerController::OnFlowStateChanged);
		if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 10.0f, FColor::Cyan, TEXT("[LoginPC Debug] 已订阅 OnStateChanged 事件！"));

		// 2. 确保在 L_Login 地图中才执行 UI 挂载逻辑
		if (GetWorld()->GetMapName().Contains(TEXT("L_Login")))
		{
			if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 10.0f, FColor::Green, TEXT("[LoginPC Debug] 在 L_Login 地图中，执行 UI 挂载逻辑"));

			EMatchState CurrentState = FlowSubsystem->GetCurrentState();

			// 【雷达 4】：证明控制器已经成功重生
			if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 8.0f, FColor::Magenta, FString::Printf(TEXT("[系统雷达 4] LoginPC 醒来，当前管家状态: %d"), (int32)CurrentState));

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
	// 【雷达 5】：证明 UI 渲染器已被成功触发
	if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 8.0f, FColor::Orange, FString::Printf(TEXT("[系统雷达 5] 正在执行 UI 重绘，目标状态: %d"), (int32)NewState));

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
			if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 10.0f, FColor::Cyan, TEXT("[LoginPC Debug] LoginUIClass 有效，准备创建 Login Widget！"));

			ActiveLoginWidget = CreateWidget<UUserWidget>(this, LoginUIClass);
			if (ActiveLoginWidget)
			{
				ActiveLoginWidget->AddToViewport(9999);
				
				// 【雷达 6】：完美成功
				if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 10.0f, FColor::Green, TEXT("[系统雷达 6] 成功挂载 Login 页面！完美收官！"));
			}
			else
			{
				if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 20.0f, FColor::Red, TEXT("[LoginPC Debug 错误] CreateWidget 返回空！"));
			}
		}
		else
		{
			// 【雷达 7】：致命配置丢失
			if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 20.0f, FColor::Red, TEXT("[系统雷达 7 - 致命错误] LoginUIClass 为空！请立刻打开 BP_LoginPlayerController 重新选中 WBP_LoginPage！"));
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
