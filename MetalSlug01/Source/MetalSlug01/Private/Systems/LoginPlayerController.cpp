#include "Systems/LoginPlayerController.h"
#include "Systems/GameFlowSubsystem.h"
#include "Blueprint/UserWidget.h"
#include "Tools/MetalSlugTestSettings.h"
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

		// 2. 确保在 L_Login 地图中才执行逻辑
		if (GetWorld()->GetMapName().Contains(TEXT("L_Login")))
		{
			// ==========================================
			// 【架构拦截点】：检查是否开启了开发测试模式
			// ==========================================
			// GetDefault 性能极高，直接获取类的默认对象 (CDO)
			const UMetalSlugTestSettings* TestConfig = GetDefault<UMetalSlugTestSettings>();
			
			// 如果配置存在，且勾选了"直通大厅"
			if (TestConfig && TestConfig->bSkipLoginDirectToLobby)
			{
				// 1. 处理伪登录：加入判空逻辑。如果玩家已经有名字了（说明是从房间退回来的），就不再重新生成随机名。
				if (UAccountSubsystem* AccountSub = GetGameInstance()->GetSubsystem<UAccountSubsystem>())
				{
					if (AccountSub->GetCurrentLoggedInUser().IsEmpty())
					{
						AccountSub->MockLoginForTesting();
					}
				}

				// 2. 【核心修复】：引入 0.2 秒安全延时，并处理 GameInstance 的状态残留问题
				FTimerHandle TestInitHandle;
				GetWorld()->GetTimerManager().SetTimer(TestInitHandle, FTimerDelegate::CreateLambda([this, FlowSubsystem]()
				{
					// 检查管家目前是不是已经是大厅状态（退房后残留的状态）
					if (FlowSubsystem->GetCurrentState() == EMatchState::MainLobby)
					{
						// 如果状态没变，TransitToState 不会工作，必须强制手动拉起大厅 UI
						OnFlowStateChanged(EMatchState::MainLobby);
					}
					else
					{
						// 如果是游戏刚启动，走标准的状态切换流程
						FlowSubsystem->TransitToState(EMatchState::MainLobby);
					}
				}), 0.2f, false);

				// 【极度关键】：直接 return！切断后续正常的 UI 挂载和状态初始化逻辑
				return;
			}
			
			// ==========================================
			// 正常的线上业务逻辑：走到这里说明没开作弊开关
			// ==========================================
			EMatchState CurrentState = FlowSubsystem->GetCurrentState();

			// 0.2 秒安全延时，解决"视口未就绪"导致的 UI 吞噬问题
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
