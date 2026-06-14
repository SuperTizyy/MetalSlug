// 版权声明：在项目设置的描述页面填写您的版权信息。

// ==========================================
// 头文件包含区
// ==========================================
// 引入本控制器头文件
#include "Systems/LoginPlayerController.h"

// 引入 GameFlowSubsystem 头文件（用于订阅状态变化）
#include "Systems/GameFlowSubsystem.h"

// 引入 UE UserWidget 基类
#include "Blueprint/UserWidget.h"

// 引入测试配置类（用于开发期"直通大厅"开关）
#include "Tools/MetalSlugTestSettings.h"

// 引入账号子系统（用于读取当前登录账号 / 触发 MockLogin）
#include "UI/Login/Core/AccountSubsystem.h"


// ==========================================
// 1. 生命周期
// ==========================================

/**
 * ALoginPlayerController::BeginPlay
 *
 * 登录地图控制器的初始化入口
 * 1. 强制显示鼠标 + UIOnly 输入模式
 * 2. 订阅 GameFlowSubsystem 状态变化
 * 3. 特殊检查: 是否开启"开发测试直通大厅"
 *    - 是: 触发 MockLogin + 0.2s 延迟切到 MainLobby
 *    - 否: 0.2s 延迟后根据当前状态挂载登录页或大厅页
 */
void ALoginPlayerController::BeginPlay()
{
	Super::BeginPlay();

	// 强制显示鼠标，设置输入模式为仅 UI
	bShowMouseCursor = true;
	SetInputMode(FInputModeUIOnly());

	FString CurrentMap = GetWorld()->GetMapName();

	if (UGameFlowSubsystem* FlowSubsystem = GetGameInstance()->GetSubsystem<UGameFlowSubsystem>())
	{
		// 1. 订阅管家的广播频道（让本控制器能响应状态切换）
		FlowSubsystem->OnStateChanged.AddDynamic(this, &ALoginPlayerController::OnFlowStateChanged);

		// 2. 确保在 L_Login 地图中才执行逻辑（防止错位关卡调用本类）
		if (GetWorld()->GetMapName().Contains(TEXT("L_Login")))
		{
			// ==========================================
			// 【架构拦截点】: 检查是否开启了开发测试模式
			// ==========================================
			// GetDefault 性能极高，直接获取类的默认对象 (CDO)
			const UMetalSlugTestSettings* TestConfig = GetDefault<UMetalSlugTestSettings>();

			// 如果配置存在，且勾选了"直通大厅"
			if (TestConfig && TestConfig->bSkipLoginDirectToLobby)
			{
				// 1. 处理伪登录: 加入判空逻辑
				//    如果玩家已经有名字了（说明是从房间退回来的），就不再重新生成随机名
				if (UAccountSubsystem* AccountSub = GetGameInstance()->GetSubsystem<UAccountSubsystem>())
				{
					if (AccountSub->GetCurrentLoggedInUser().IsEmpty())
					{
						// 触发 MockLogin 生成测试账号
						AccountSub->MockLoginForTesting();
					}
				}

				// 2. 【核心修复】: 引入 0.2 秒安全延时，并处理 GameInstance 的状态残留问题
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

				// 【极度关键】: 直接 return！切断后续正常的 UI 挂载和状态初始化逻辑
				return;
			}

			// ==========================================
			// 正常的线上业务逻辑: 走到这里说明没开作弊开关
			// ==========================================
			EMatchState CurrentState = FlowSubsystem->GetCurrentState();

			// 0.2 秒安全延时，解决"视口未就绪"导致的 UI 吞噬问题
			FTimerHandle InitHandle;
			GetWorld()->GetTimerManager().SetTimer(InitHandle, FTimerDelegate::CreateLambda([this, CurrentState]()
			{
				if (CurrentState == EMatchState::MainLobby)
				{
					// 当前状态已经是大厅，直接拉起 UI
					OnFlowStateChanged(EMatchState::MainLobby);
				}
				else if (CurrentState == EMatchState::Login)
				{
					// 当前状态是登录，挂载登录页
					OnFlowStateChanged(EMatchState::Login);
				}
				else
				{
					// 其他异常状态，强制切回登录
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


// ==========================================
// 2. 状态变化响应
// ==========================================

/**
 * ALoginPlayerController::OnFlowStateChanged
 *
 * GameFlowSubsystem 状态变化回调
 * - Login: 销毁大厅 UI，创建登录页
 * - MainLobby: 销毁登录 UI，创建大厅页
 * - 其他: 销毁所有 UI
 */
void ALoginPlayerController::OnFlowStateChanged(EMatchState NewState)
{
	// ==========================================
	// 状态 1: 登录阶段
	// ==========================================
	if (NewState == EMatchState::Login)
	{
		// 强杀可能残留的大厅 UI
		if (ActiveLobbyWidget) { ActiveLobbyWidget->RemoveFromParent(); ActiveLobbyWidget = nullptr; }
		if (ActiveLoginWidget) { ActiveLoginWidget->RemoveFromParent(); ActiveLoginWidget = nullptr; }

		// 【关键校验点】: 检查蓝图类是否有效
		if (LoginUIClass)
		{
			// 动态创建登录页 Widget
			ActiveLoginWidget = CreateWidget<UUserWidget>(this, LoginUIClass);
			if (ActiveLoginWidget)
			{
				// 9999 是 ZOrder，确保在所有 UI 之上
				ActiveLoginWidget->AddToViewport(9999);
			}
			else
			{
				if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 20.0f, FColor::Red, TEXT("[LoginPC Debug 错误] CreateWidget 返回空！"));
			}
		}
	}
	// ==========================================
	// 状态 2: 主大厅阶段
	// ==========================================
	else if (NewState == EMatchState::MainLobby)
	{
		// 销毁登录页（如果存在）
		if (ActiveLoginWidget) { ActiveLoginWidget->RemoveFromParent(); ActiveLoginWidget = nullptr; }
		if (ActiveLobbyWidget) { ActiveLobbyWidget->RemoveFromParent(); ActiveLobbyWidget = nullptr; }

		// 动态创建大厅页 Widget
		if (LANRoomUIClass)
		{
			ActiveLobbyWidget = CreateWidget<UUserWidget>(this, LANRoomUIClass);
			if (ActiveLobbyWidget)
			{
				ActiveLobbyWidget->AddToViewport();
			}
		}
	}
	// ==========================================
	// 其他状态: 统统销毁
	// ==========================================
	else
	{
		// 干干净净地离开
		if (ActiveLoginWidget) { ActiveLoginWidget->RemoveFromParent(); ActiveLoginWidget = nullptr; }
		if (ActiveLobbyWidget) { ActiveLobbyWidget->RemoveFromParent(); ActiveLobbyWidget = nullptr; }
	}
}
