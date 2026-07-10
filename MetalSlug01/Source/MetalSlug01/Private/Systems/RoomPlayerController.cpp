// 版权声明：在项目设置的描述页面填写您的版权信息。

// ==========================================
// 头文件包含区
// ==========================================
// 【UE规范】本控制器头文件必须第一个引入
#include "Systems/RoomPlayerController.h"

// 引入账号在线状态权威表（RoomPlayerController.h 只有前向声明，此处需要完整定义）
#include "Systems/Room/AccountRoomAuthority.h"

// 引入角色基类（用于 Spawn 相关操作）
#include "Characters/BaseCharacter.h"

// 【2026.07.11 v28】Server_KickPlayer 需要 AAIController 完整类型 (Destroy/GetName), 加完整 include
#include "AIController.h"

// 引入 Enhanced Input 相关头文件
#include "EnhancedInputComponent.h"

// 引入房间内 UI 页面类（用于类型转换和调用 UI 接口）
#include "UI/Login/Pages/BattleRoom/RoomInsidePage.h"

// 引入自定义 HUD 类（用于获取 GameHUDWidget）
#include "UI/MyGameHUD.h"

// 引入战斗 HUD Widget
#include "UI/Game/GameHUDWidget.h"

// 引入计分板 Widget
#include "UI/Game/Widgets/ScoreboardWidget.h"

// 引入 ESC 菜单 Widget
#include "UI/Game/Widgets/EscMenuWidget.h"

// 引入房间 GameMode（用于调用房间管理接口）
#include "Systems/RoomGameMode.h"

// 引入房间 GameState（用于查询比赛信息）
#include "Systems/RoomGameState.h"

// 引入房间 PlayerState（用于 InitPlayerState 强制类型）
#include "Systems/Core/RoomPlayerState.h"

// 【v31.4】URoomSpawnSubsystem (复活路径真理源 + Loadout 同步)
#include "Systems/Spawn/RoomSpawnSubsystem.h"

// 引入 GameFlowSubsystem（流程大管家）
#include "Systems/GameFlowSubsystem.h"

// 引入房间业务服务（EnterSkipToHostMode 显式标房主 - 用于 RoomPC 战斗地图兜底）
#include "Services/RoomService.h"

// 引入账号子系统（用于获取登录用户信息）
// 【架构修正】PlayerController 也走 AccountService 门面访问账号数据
#include "Systems/Account/AccountSubsystem.h"
#include "Services/AccountService.h"
#include "Systems/Account/LocalAccountRepository.h"

// 【P0】RoomPlayerController 也走 SessionManager 门面访问会话
#include "Systems/Session/SessionManagerSubsystem.h"

// 引入 UE 静态函数库（用于 OpenLevel 等）
#include "Kismet/GameplayStatics.h"

// 引入在线子系统（用于管理 Session）
#include "OnlineSubsystem.h"

// 引入在线会话接口（用于创建/销毁/搜索 Session）
#include "Interfaces/OnlineSessionInterface.h"

// 引入房间 PlayerState（用于读写玩家个人数据）
#include "Systems/Core/RoomPlayerState.h"


// ==========================================
// 1. 生命周期
// ==========================================

/**
 * ARoomPlayerController::BeginPlay
 *
 * 控制器初始化入口
 * 1. 初始化 ESC 菜单状态标志
 * 2. 订阅 GameFlowSubsystem 状态变化
 * 3. 主动调用 TransitToState(InRoom)（刚加载进战斗地图时）
 * 4. 延迟 2 秒发送玩家信息（等待网络稳固）
 */
void ARoomPlayerController::BeginPlay()
{
	Super::BeginPlay();

	// ==========================================
	// 【DEBUG-SET-5-A】RoomPC 上线: 切图后第一个 ARoomPlayerController BeginPlay 触发
	// ==========================================
	UE_LOG(LogTemp, Error,
		TEXT("[DEBUG-S5-A][RoomPC::BeginPlay] PID=%u WorldName=%s IsLocal=%d NetMode=%d"),
		FPlatformProcess::GetCurrentProcessId(),
		*GetWorld()->GetName(),
		IsLocalPlayerController() ? 1 : 0,
		(int32)GetWorld()->GetNetMode());

	// 初始化 ESC 菜单状态标志为关闭
	bIsEscMenuOpen = false;

	// 仅本地玩家控制器才需要订阅（Dedicated Server 端 Controller 不订阅）
	if (IsLocalPlayerController())
	{
		// ==========================================
		// 【大厂 P0 修复 2026.07.03】删除无条件 TransitToState(InRoom)
		// ==========================================
		// 旧代码问题:
		//   RoomPC::BeginPlay 无条件调用 TransitToState(InRoom)
		//   → 当 PIE 入口是战斗地图 (Japanese_Temple_Demo) 时, 此时状态机还是 PreLogin (0)
		//   → 立刻被强行推到 InRoom (4), 完全跳过 Login → MainMenu → MainLobby
		//   → bSkipLoginDirectToLobby=false 时, 玩家想看到登录页 → 看不到
		//   → TargetRoomMapName 还是 NAME_None → 报错 "TargetRoomMapName is NONE"
		//
		// 新规则 (大厂分层架构):
		//   - 状态机推进 100% 由 GameFlowSubsystem 统一调度
		//     → 任何状态切换都必须经 GameFlow::TransitToState()
		//     → 启动入口是 PostLoadMapWithWorld, 不是 RoomPC::BeginPlay
		//   - RoomPC 只负责"监听状态变化 → 响应", 不能主动 push 状态
		//     → "刚加载进战斗地图必须进入 InRoom" 的逻辑
		//     → 应该由 PostLoadMapWithWorld (LogGameFlow 启动点) + RoomGameMode 处理
		//     → 不应该让 RoomPC 在 BeginPlay 越权做事
		//   - 若确实需要在战斗地图启动后立刻进入 InRoom 状态
		//     → 应在 RoomGameMode::StartPlay / OnPostLogin 通过 GameFlow::TransitToState
		//     → 不在 RoomPC::BeginPlay (PC 还没 Possess Pawn, 时序也不对)
		//
		// 本次修复只做"删除 RoomPC 的越权调度", 状态推进由 GameFlow 启动入口完成
		// ==========================================
		if (UGameFlowSubsystem* FlowSubsystem = GetGameInstance()->GetSubsystem<UGameFlowSubsystem>())
		{
			// 仅订阅, 不再主动调用 TransitToState
			FlowSubsystem->OnStateChanged.AddDynamic(this, &ARoomPlayerController::OnFlowStateChanged);

			// 【大厂补充日志】让本次 BeginPlay 的人能清楚"为什么不主动切到 InRoom"
			UE_LOG(LogTemp, Log,
				TEXT("[RoomPC::BeginPlay] 仅订阅 OnStateChanged, 状态推进交由 GameFlow 启动入口 (CurrentState=%d)"),
				(int32)FlowSubsystem->GetCurrentState());

			// ==========================================
			// 【大厂 P0 修复 2026.07.03】自我同步 (Self-sync): 订阅后立即消费一次当前状态
			// ==========================================
			// 时序竞争场景:
			//   1. Subsystem::HandlePostLoadMapWithWorld 在 PostLoadMap 触发 Broadcast(InRoom)
			//   2. 我们在 BeginPlay 才 AddDynamic → 已经错过这次广播
			//   3. RoomPC 永远不知道"InRoom", RoomUIWidget 不创建 → 玩家卡屏
			//
			// 解决方案:
			//   BeginPlay 时主动检查 CurrentState, 如果物理位置上应该 InRoom 但实际不是,
			//   主动调一次 OnFlowStateChanged(CurrentState) 触发 UI 创建
			//
			// 大厂原则 (Self-healing):
			//   - 不要相信外部时序, 自己兜底同步一次
			//   - OnFlowStateChanged 内部有幂等保护 (RoomUIWidget 已存在则不再创建), 多次调用安全
			// ==========================================
			const EMatchState NowState = FlowSubsystem->GetCurrentState();
			if (NowState == EMatchState::InRoom || NowState == EMatchState::Battleing)
			{
				UE_LOG(LogTemp, Warning,
					TEXT("[RoomPC::BeginPlay] 自我同步: CurrentState=%d, 主动调 OnFlowStateChanged (修复订阅晚于广播的时序竞争)"),
					(int32)NowState);
				OnFlowStateChanged(NowState);
			}
		}

		// 延迟 2 秒再发玩家信息（等待底层网络连接稳固 + 存档读取完成）
		FTimerHandle DelayHandle;
		GetWorld()->GetTimerManager().SetTimer(DelayHandle, this, &ARoomPlayerController::DelayedSendPlayerInfo, 2.0f, false);

		// 旧版: 立即请求生成
		// Server_RequestSpawn();
	}
}

/**
 * ARoomPlayerController::DelayedSendPlayerInfo
 *
 * 延迟 2 秒后真正执行的玩家信息发送
 * 1. 读取当前登录账号名
 * 2. 从 AccountSubsystem 拿到上次的角色/武器偏好
 * 3. 调 Server_SelectLoadout 上传偏好
 * 4. 调 Server_SendPlayerInfo 上传名字
 */
void ARoomPlayerController::DelayedSendPlayerInfo()
{
	// 默认显示名
	FString MyName = TEXT("未知玩家");

	if (UGameInstance* GI = GetGameInstance())
	{
		// 走新架构: AccountService 拿会话用户名, Repository 拿偏好记录
		UAccountService* AccountService = UAccountService::Get(this);
		ULocalAccountRepository* Repo = GI->GetSubsystem<ULocalAccountRepository>();
		if (AccountService && Repo)
		{
			// 1) 读取当前登录的用户名
			MyName = AccountService->GetCurrentUser();

			// 2) 从仓储层拿档案
			const FAccountRecord* MyRecord = Repo->FindRecord(MyName);
			if (MyRecord)
			{
				UE_LOG(LogTemp, Warning, TEXT("[Room] DelayedSendPlayerInfo: Char='%s', W1='%s', W2='%s'"),
					*MyRecord->LastSelectedCharacter, *MyRecord->LastSelectedWeapon1, *MyRecord->LastSelectedWeapon2);
				// 【修复 1】: 直接呼叫自身的 RPC，将初始数据推送到服务器！
				Server_SelectLoadout(MyRecord->LastSelectedCharacter, MyRecord->LastSelectedWeapon1, MyRecord->LastSelectedWeapon2);
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("[Room] DelayedSendPlayerInfo: No record for '%s'"), *MyName);
			}
		}
	}

	// 把玩家名发给服务器
	Server_SendPlayerInfo(MyName);

	// ==========================================
	// 【新增】同步"账号在线"状态到房主权威表
	// 1. 把 MyName 缓存到成员变量, 用于退房时通知
	// 2. 用本地 FGuid 生成 SessionId, 房主用它做"重连"判定
	// 3. 调 Server_NotifyAccountLogin → 房主 TMap 注册/判定
	// ==========================================
	MyAccountUsername = MyName;
	MyAccountSessionId = FGuid::NewGuid().ToString(EGuidFormats::Digits);

	if (!MyAccountUsername.IsEmpty())
	{
		Server_NotifyAccountLogin(MyAccountUsername, MyAccountSessionId);

		// 启动客户端心跳定时器(5s 一次)
		// 房主端: Server_Heartbeat 直接走本地执行,刷新 LastHeartbeatAt
		// 客户端端: 走网络发到房主
		if (GetWorld())
		{
			GetWorld()->GetTimerManager().SetTimer(
				HeartbeatTimerHandle,
				this,
				&ARoomPlayerController::SendHeartbeat,
				5.0f,    // 5 秒一次
				true);   // 循环
		}

		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Cyan,
				FString::Printf(TEXT("[Account] 已通知房主: %s 上线 (Session=%s)"),
					*MyAccountUsername, *MyAccountSessionId));
		}
	}
}


// ==========================================
// 2. Server RPC 实现区
// ==========================================

/**
 * 验证函数: 防作弊校验（直接返回 true 表示全部放行）
 */
bool ARoomPlayerController::Server_SendPlayerInfo_Validate(const FString& InPlayerName)
{
	return true;
}

/**
 * Server_SendPlayerInfo_Implementation
 *
 * 服务器端: 接收并保存玩家名
 * 1. 写入 MyPlayerName 备用
 * 2. 【关键】强制覆写底层 PlayerState 名称（解决默认是计算机名的 Bug）
 * 3. 通知 GameMode.AddPlayerToRoom 完成注册
 */
void ARoomPlayerController::Server_SendPlayerInfo_Implementation(const FString& InPlayerName)
{
	// 1. 记录在 Controller 自己的变量中（备用）
	MyPlayerName = InPlayerName;

	// ==========================================
	// 【工业级修复】: 强制覆写底层 PlayerState 名称！
	// 虚幻引擎默认会把本地玩家名字设为操作系统计算机名 (如 YiYuanDesktop-XXXX)。
	// 只有显式调用 SetPlayerName，UI 层 PS->GetPlayerName() 才能拿到真实的账号名！
	// ==========================================
	if (APlayerState* PS = GetPlayerState<APlayerState>())
	{
		PS->SetPlayerName(InPlayerName);
		UE_LOG(LogTemp, Log, TEXT("[RoomPlayerController] 成功将玩家底层名称同步为: %s"), *InPlayerName);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[RoomPlayerController] 警告: 尚未获取到 PlayerState！名称同步可能失败。"));
	}

	// 2. 将控制权转交给服务器大脑（GameMode）处理注册逻辑
	if (ARoomGameMode* GM = Cast<ARoomGameMode>(GetWorld()->GetAuthGameMode()))
	{
		GM->AddPlayerToRoom(this, InPlayerName);
	}
}

/**
 * Client_EnterBattleState_Implementation
 *
 * 服务器通知客户端"进入战斗状态"
 * 1. 兜底初始化倒计时（防止测试时 PerformGameStart 未被调用）
 * 2. 通知本地 GameFlowSubsystem 切换到 Battleing
 */
void ARoomPlayerController::Client_EnterBattleState_Implementation()
{
	// 0. 在切换状态前，先确保倒计时被初始化
	if (UWorld* World = GetWorld())
	{
		if (ARoomGameState* RoomGS = World->GetGameState<ARoomGameState>())
		{
			// 只有当倒计时未启动时（GetMatchRemainingSeconds <= 0）才执行兜底初始化
			if (RoomGS->GetMatchRemainingSeconds() <= 0)
			{
				// 根据当前模式设置不同的倒计时长度
				switch (RoomGS->CurrentMatchMode)
				{
				case ERoomMatchMode::Melee:
					// 刀战模式: 30 分钟一局
					RoomGS->MatchEndTime = GetWorld()->GetTimeSeconds() + (30 * 60);
					RoomGS->CurrentRound = 0;
					break;
				case ERoomMatchMode::Zombie:
					// 生化模式: 10 分钟一回合，共 5 回合
					RoomGS->MatchEndTime = GetWorld()->GetTimeSeconds() + (10 * 60);
					RoomGS->CurrentRound = 5;
					break;
				default:
					break;
				}

				// 广播初始化完成
				RoomGS->OnMatchModeChanged.Broadcast(RoomGS->CurrentMatchMode);
				RoomGS->OnCurrentRoundUpdated.Broadcast(RoomGS->CurrentRound);
			}
		}
	}

	// 每个玩家（客户端）收到服务器的开打指令后，立刻向本地的流程大管家报到！
	// 管家收到指令，会自动触发 OnStateChanged -> 销毁 RoomInsidePage，隐藏鼠标，呼出战斗准星
	if (UGameFlowSubsystem* FlowSubsystem = GetGameInstance()->GetSubsystem<UGameFlowSubsystem>())
	{
		FlowSubsystem->TransitToState(EMatchState::Battleing);
	}
}


// ==========================================
// 3. Client RPC 实现区
// ==========================================

/**
 * 验证函数: 切换队伍
 */
bool ARoomPlayerController::Server_RequestChangeTeam_Validate(bool bToAttackTeam) { return true; }

/**
 * Server_RequestChangeTeam_Implementation
 *
 * 服务器端: 接收玩家换队请求
 * 中转到 RoomGameMode.ChangePlayerTeam
 */
void ARoomPlayerController::Server_RequestChangeTeam_Implementation(bool bToAttackTeam)
{
	if (ARoomGameMode* GM = Cast<ARoomGameMode>(GetWorld()->GetAuthGameMode()))
	{
		// 【修复】: 传递 this 而非 nullptr
		GM->ChangePlayerTeam(this, bToAttackTeam);
	}
}


// ==========================================
// 4. UI 点击后触发的本地逻辑
// ==========================================

/**
 * ARoomPlayerController::LeaveRoom
 *
 * 玩家点击"离开房间"按钮时调用
 * 两种情况:
 *   A) 房主: 遍历所有人发 Client_ForceLeaveRoom, 0.5s 后自己也走
 *   B) 普通玩家: 通知服务器 + 自己立刻走
 */
void ARoomPlayerController::LeaveRoom()
{
	// 【情况 A】: 如果我是房主 (服务器端拥有最高权限)
	if (HasAuthority())
	{
		// 1. 遍历房间里所有人，给其他玩家发"遣散令"
		for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
		{
			ARoomPlayerController* PC = Cast<ARoomPlayerController>(It->Get());
			// 如果这个控制器存在，并且不是房主自己，就命令他退房！
			if (PC && PC != this)
			{
				PC->Client_ForceLeaveRoom();
			}
		}

		// 2. 给遣散令 0.5 秒的网络传输时间，然后房主自己再走
		GetWorld()->GetTimerManager().SetTimer(HostLeaveTimer, this, &ARoomPlayerController::ExecuteLeaveRoom, 0.5f, false);
	}
	// 【情况 B】: 如果我是普通客户端玩家
	else
	{
		// 告诉服务器我要走了，把我的名字从别人屏幕抹掉
		Server_LeaveRoom();
		// 自己立刻乖乖走人
		ExecuteLeaveRoom();
	}
}

/**
 * Client_ForceLeaveRoom_Implementation
 *
 * 接收到房主的遣散令（被迫退房）
 */
void ARoomPlayerController::Client_ForceLeaveRoom_Implementation()
{
	if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, TEXT("房主已解散房间！被迫返回大厅..."));

	// 收到命令，乖乖执行走人逻辑
	ExecuteLeaveRoom();
}

/**
 * ARoomPlayerController::ExecuteLeaveRoom
 *
 * 【大厂标准 - P0 修复 v2】真正执行退房底层逻辑 (单地图常驻模式)
 *
 * 设计原则:
 *   - 状态机只负责状态 (GameFlowSubsystem 不再在 MainLobby 跳图)
 *   - 业务方 (本函数) 主动 OpenLevel 回 L_Login + 主动预约 MainLobby
 *
 * 流程 (修复版):
 *   1. 销毁本地的 Session (DestroyRoom 内部清理 NetDriver)
 *   2. 主动告诉 GameFlowSubsystem: "下一张地图加载完后, 我要 MainLobby"
 *      → 关键: 这一步不再依赖脆弱的本地 Lambda, 而是写到跨地图持久的 Subsystem
 *   3. 主动 OpenLevel(L_Login, ?offline) - 跳回常驻大厅地图
 *   4. 新地图加载完成后:
 *      - 新 GameInstance 的 BootToLogin 会先把状态设到 Login (显示 LoginPage)
 *      - 然后 GameFlowSubsystem::HandlePostLoadMapWithWorld 触发
 *      - 看到 bHasPendingStateOnNextLoad=true → 强制 TransitToState(MainLobby)
 *      - UIViewService 自动 ShowPanel(LANRoom) → 玩家看到大厅页 😊
 *
 * 为什么从"本地 Lambda 订阅"改成"Subsystem 持久化"?
 *   - 旧实现: RoomPC 在 ExecuteLeaveRoom 里订阅 PostLoadMapWithWorld
 *     → OpenLevel 触发 PC EndPlay → EndPlay 抢先 Remove 委托 → 永远收不到回调 😡
 *   - 新实现: GameFlowSubsystem 自己订阅 PostLoadMapWithWorld
 *     → Subsystem 跨地图持久, 不会被 EndPlay 解绑
 *     → 业务方只需要登记意图 (RequestStateOnNextLoad), 不需要关心订阅生命周期
 *
 * 单一大厂铁律: 跨地图的意图必须放在跨地图持久的层 (Subsystem), 不能放在即将销毁的 Actor (PC) 上
 */
void ARoomPlayerController::ExecuteLeaveRoom()
{
	// 1. 【P0】销毁 Session: 走 SessionManager->DestroyRoom (异步, 不直调 OnlineSubsystem)
	if (UGameInstance* GI = GetGameInstance())
	{
		if (USessionManagerSubsystem* SessionManager = GI->GetSubsystem<USessionManagerSubsystem>())
		{
			SessionManager->DestroyRoom(FOnDestroyRoomComplete());
		}
	}

	// 2. 【大厂标准】主动 OpenLevel 回 L_Login
	//    ?offline 参数: 强制 UE 引擎清理底层 NetDriver, 防止端口死锁
	UWorld* World = GetWorld();
	if (!World) return;

	// 3. 【大厂标准 - P0 修复】告诉 GameFlowSubsystem: 下一张地图加载完后, 我要 MainLobby
	//    这一步替代了旧的 PostLoadMapWithWorld Lambda 订阅
	//    关键: 不再依赖脆弱的"PC 生命周期内 Lambda", 而是写到跨地图持久的 Subsystem
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UGameFlowSubsystem* FlowSubsystem = GI->GetSubsystem<UGameFlowSubsystem>())
		{
			FlowSubsystem->RequestStateOnNextLoad(EMatchState::MainLobby);
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("[RoomPlayerController] ExecuteLeaveRoom: GameFlowSubsystem 不可用, 退房后可能跳错页面"));
		}
	}

	// 4. 【P0 兼容】保留旧的 PostLoadMapWithWorld Lambda 作为兜底 (防御性编程)
	//    虽然 Subsystem 已经接管, 但万一未来有边界情况 (如多次 OpenLevel) 让 Subsystem 错过了
	//    这个兜底可以保证最后一道防线仍然能切到 MainLobby
	//    EndPlay 中仍然会 Remove, 但因为 Subsystem 那层已经处理了, 不会重复触发 (TransitToState 有幂等保护)
	if (!LeaveRoomSafeHandle.IsValid())
	{
		LeaveRoomSafeHandle = FCoreUObjectDelegates::PostLoadMapWithWorld.AddLambda(
			[WeakSelf = TWeakObjectPtr<ARoomPlayerController>(this), this](UWorld* LoadedWorld)
			{
				// 防御 1: 自己是否还活着 (新地图加载期间可能销毁)
				if (!WeakSelf.IsValid()) return;

				// 防御 2: 加载的必须是 L_Login (目标地图)
				if (!LoadedWorld || !LoadedWorld->GetMapName().Contains(TEXT("L_Login"))) return;

				// 防御 3: 拿到新 World 的 GI + GameFlowSubsystem
				UGameInstance* NewGI = LoadedWorld->GetGameInstance();
				if (!NewGI) return;

				UGameFlowSubsystem* NewFlow = NewGI->GetSubsystem<UGameFlowSubsystem>();
				if (!NewFlow)
				{
					UE_LOG(LogTemp, Error, TEXT("[RoomPlayerController] PostLoadMapWithWorld(兜底): 新地图找不到 GameFlowSubsystem"));
					return;
				}

				// 兜底: 如果当前还是 Login (说明 Subsystem 那层没处理), 强制切到 MainLobby
				// 大厂设计: 幂等保护, 即使 Subsystem 已经切到 MainLobby, 再调一次也是 no-op
				if (NewFlow->GetCurrentState() != EMatchState::MainLobby)
				{
					UE_LOG(LogTemp, Log, TEXT("[RoomPlayerController] PostLoadMapWithWorld(兜底): 主动切到 MainLobby"));
					NewFlow->TransitToState(EMatchState::MainLobby);
				}

				// 一次性事件, 处理完后立即解绑
				FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(LeaveRoomSafeHandle);
				LeaveRoomSafeHandle.Reset();
			});
	}

	// 5. 最后才执行 OpenLevel (确保订阅/预约在先, 不会错过事件)
	UGameplayStatics::OpenLevel(World, FName("L_Login"), true, TEXT("?offline"));
}// ==========================================
// 5. 告诉服务器的 RPC 逻辑
// ==========================================

/**
 * 验证函数: 离开房间
 */
bool ARoomPlayerController::Server_LeaveRoom_Validate() { return true; }

/**
 * Server_LeaveRoom_Implementation
 *
 * 普通玩家告诉服务器自己离开
 * 中转到 RoomGameMode.RemovePlayerFromRoom
 */
void ARoomPlayerController::Server_LeaveRoom_Implementation()
{
	if (ARoomGameMode* GM = Cast<ARoomGameMode>(GetWorld()->GetAuthGameMode()))
	{
		// 【修复】: 传递 this 而非 nullptr
		GM->RemovePlayerFromRoom(this);
	}
}

/**
 * 验证函数: 踢人
 */
bool ARoomPlayerController::Server_KickPlayer_Validate(const FString& PlayerNameToKick) { return true; }

/**
 * Client_BeKicked_Implementation
 *
 * 倒霉蛋收到被踢指令
 * 复用 ExecuteLeaveRoom 走底层逻辑
 */
void ARoomPlayerController::Client_BeKicked_Implementation()
{
	// 屏幕飘红字，让玩家死个明白
	if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, TEXT("你已被房主移出房间！"));

	// 直接复用之前写好的完美退房底层逻辑（断网、拿车票、跳地图一气呵成）
	ExecuteLeaveRoom();
}

/**
 * 验证函数: 发送聊天
 */
bool ARoomPlayerController::Server_SendChatMessage_Validate(const FString& Message) { return true; }

/**
 * Server_SendChatMessage_Implementation
 *
 * 玩家发消息，向服务器大脑请求全频道广播
 */
void ARoomPlayerController::Server_SendChatMessage_Implementation(const FString& Message)
{
	// 我发了消息，求助服务器大脑帮我全频道广播！
	if (ARoomGameMode* GM = Cast<ARoomGameMode>(GetWorld()->GetAuthGameMode()))
	{
		GM->BroadcastChatMessage(MyPlayerName, Message);
	}
}

/**
 * Client_ReceiveChatMessage_Implementation
 *
 * 客户端接收到聊天消息
 * 路由逻辑: 优先发到战斗 HUD（如果在战斗中），否则回退到房间 UI
 */
void ARoomPlayerController::Client_ReceiveChatMessage_Implementation(const FString& SenderName, bool bIsHost, const FString& Message, bool bIsSystemMsg)
{
	// 只有在战斗状态（GameHUDWidget 可见）时，才路由到战斗 HUD
	// 房间状态下 GameHUDWidget 为 Collapsed（指针不为空但不可见），需要回退到 RoomUIWidget
	UGameHUDWidget* HUDWidget = GetGameHUDWidget();
	if (HUDWidget && HUDWidget->GetVisibility() != ESlateVisibility::Collapsed)
	{
		HUDWidget->AddChatMessage(SenderName, bIsHost, Message);
	}
	// 回退到 RoomUIWidget（房间状态）
	else if (RoomUIWidget)
	{
		RoomUIWidget->AddChatMessage(SenderName, bIsHost, Message, bIsSystemMsg);
	}
}


// ==========================================
// 6. 处理添加 AI 请求
// ==========================================

/**
 * 验证函数: 添加 AI (大厅阶段)
 * 【2026.07.11 v28 重构】RPC 改名 + 改签名, 走 FAISpawnRequest 通道
 */
bool ARoomPlayerController::Server_QueueAIForBattleSpawn_Validate(const FAISpawnRequest& Request) { return true; }

/**
 * Server_QueueAIForBattleSpawn_Implementation
 *
 * 【2026.07.11 v28 大厂架构重构】服务器端: 接收"添加 AI"请求
 *
 * 旧 (v24) 行为: GM->AddAIToRoom 立刻 Spawn Pawn, 大厅阶段 AI 已经站在场景里
 * 新 (v28) 行为: GM->QueueAIForBattleSpawn 只入队, 战斗开始时统一 Spawn
 *
 * 大厂原则:
 *   - 显式意图: RPC 入队 = 业务请求, 不允许"静默立刻生成"
 *   - 零兜底: 字段非法 → GameMode::QueueAIForBattleSpawn 显式 Error + 拒绝入队
 */
void ARoomPlayerController::Server_QueueAIForBattleSpawn_Implementation(const FAISpawnRequest& Request)
{
	// 只有房主才有权限加 AI
	if (!HasAuthority()) return;

	if (ARoomGameMode* GM = Cast<ARoomGameMode>(GetWorld()->GetAuthGameMode()))
	{
		// 让 GameMode 去处理入队 (不生成 Actor)
		GM->QueueAIForBattleSpawn(Request);
	}
}

/**
 * Server_KickPlayer_Implementation
 *
 * 房主踢人 RPC
 * 【2026.07.11 v28 大厂架构重构】区分真人 vs AI 占位
 *
 * 旧 (v27) 判定: PlayerNameToKick.StartsWith(TEXT("[AI]"))
 *   旧 AIName 是 "[AI]Grunt_1" 格式, 但新 (v28) AIName 是 "AI_GruntAI_1" 格式
 *   → StartsWith("[AI]") 永远 false → AI 名字走"踢真人"分支, no-op
 *
 * 新 (v28) 判定: 走 GameMode.PendingAIQueue (元数据) 或 AIController (已生成)
 *   - 真人: 走 PlayerController 迭代
 *   - AI 占位 (大厅阶段): 从 PendingAIQueue 移除
 *   - AI 已生成 (战斗阶段): 找 AIController, Destroy Controller (v24 复用, 不能简单 Destroy, 需重启)
 *
 * 大厂原则: 单一真理源, 判定逻辑集中此处, 不允许调用方自己判
 */
void ARoomPlayerController::Server_KickPlayer_Implementation(const FString& PlayerNameToKick)
{
	if (!HasAuthority()) return;

	// ==========================================
	// 阶段 1: 检查是否是 AI 占位 (大厅阶段 PendingAIQueue 里有这个 DisplayName)
	// ==========================================
	if (ARoomGameMode* GM = Cast<ARoomGameMode>(GetWorld()->GetAuthGameMode()))
	{
		if (GM->IsPendingAIByName(PlayerNameToKick))
		{
			// AI 占位 → 从队列移除 (不生成 Actor)
			GM->RemovePendingAIByName(PlayerNameToKick);
			UE_LOG(LogTemp, Log, TEXT("[KickPlayer] 移除 AI 占位 '%s' 成功"), *PlayerNameToKick);
			return;
		}
	}

	// ==========================================
	// 阶段 2: 检查是否是已生成的 AI (战斗阶段, 有 AIController)
	// ==========================================
	bool bIsAIController = false;
	for (FConstControllerIterator It = GetWorld()->GetControllerIterator(); It; ++It)
	{
		if (AAIController* AIC = Cast<AAIController>(It->Get()))
		{
			// AI Controller 的名字格式: AIC_AI_GruntAI_1 (SpawnAIInternal 里拼的)
			// PlayerNameToKick 是 "AI_GruntAI_1" 格式
			const FString AICName = AIC->GetName();
			if (AICName.EndsWith(PlayerNameToKick))
			{
				// 大厂原则 - 显式意图: 战斗阶段 AI 是真人在打, 不允许"房主踢 AI"瞬间消失
				// (战斗进行时踢 AI 等于作弊)
				// 但用户当前是测试期, 先支持踢, 后续可加"战斗中禁止踢 AI"规则
				UE_LOG(LogTemp, Warning,
					TEXT("[KickPlayer] 战斗阶段踢 AI '%s' (AIC='%s') — 立即销毁 Controller, 残留 Pawn 由 UE GC"),
					*PlayerNameToKick, *AICName);
				AIC->Destroy();
				bIsAIController = true;
				break;
			}
		}
	}
	if (bIsAIController) return;

	// ==========================================
	// 阶段 3: 真人玩家 — 走原有逻辑
	// ==========================================
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		ARoomPlayerController* TargetPC = Cast<ARoomPlayerController>(It->Get());
		if (TargetPC && TargetPC->MyPlayerName == PlayerNameToKick)
		{
			// ==========================================
			// 【2026-06-30 P0 修复】踢人前必须先清理 AccountAuthority 的 TMap 条目
			// ----------------------------------------------------------------------
			// 旧根因：
			//   Server_KickPlayer 只通知了被踢客户端 Client_BeKicked() + 调 RemovePlayerFromRoom
			//   → 但 Client_BeKicked 走 ExecuteLeaveRoom 直接跳图，没经过 AccountAuthority 清理
			//   → AccountAuthority.OnlineAccounts 残留被踢玩家 (Username, OldSessionId)
			//   → 被踢玩家重进时，DelayedSendPlayerInfo 生成 NEW SessionId
			//   → HandleLoginRequest 走"情况 2: 同 Username 不同 SessionId" → 拒绝
			//   → Client_LoginResult(bReject=true) → 弹 Overlay_LANRoomConflict
			// 修复：在告知客户端被踢前，在房主权威表里显式调 HandleLogoutRequest
			//       清掉对应的 (Username, SessionId) 条目，给"想重新加入"留出通道
			// ==========================================
			if (AccountAuthority.IsValid())
			{
				if (!TargetPC->MyAccountUsername.IsEmpty() && !TargetPC->MyAccountSessionId.IsEmpty())
				{
					AccountAuthority->HandleLogoutRequest(
						TargetPC->MyAccountUsername,
						TargetPC->MyAccountSessionId);

					UE_LOG(LogTemp, Log,
						TEXT("[Authority] Server_KickPlayer: 已清理 TMap 中 [%s] 的旧 SessionId (允许重进)"),
						*TargetPC->MyAccountUsername);
				}
				else
				{
					// 【兜底】：万一被踢 PC 上 Username/SessionId 为空，走模糊匹配清理
					AccountAuthority->HandleControllerDestroyed(TargetPC);
				}
			}

			// 通知目标客户端被踢
			TargetPC->Client_BeKicked();

			// 【修复】: 将被踢人的 Controller 传过去，让 GameMode 移除其数据
			if (ARoomGameMode* GM = Cast<ARoomGameMode>(GetWorld()->GetAuthGameMode()))
			{
				GM->RemovePlayerFromRoom(TargetPC);
			}
			break;
		}
	}
}

/**
 * 验证函数: 切换准备
 */
bool ARoomPlayerController::Server_ToggleReady_Validate(bool bIsReady) { return true; }

/**
 * Server_ToggleReady_Implementation
 *
 * 玩家切换准备状态
 */
void ARoomPlayerController::Server_ToggleReady_Implementation(bool bIsReady)
{
	if (ARoomGameMode* GM = Cast<ARoomGameMode>(GetWorld()->GetAuthGameMode()))
	{
		GM->UpdatePlayerReadyState(this, bIsReady);
	}
}

/**
 * Server_RequestStartGame_Implementation
 *
 * 房主点击"开始游戏"时调用
 * 1. 校验所有玩家已准备
 * 2. 通知所有客户端进入战斗状态 (UI 切换)
 * 3. 启动 GameMode PerformGameStart — 倒计时结束后由 SpawnAllPlayersIntoBattle 统一处理所有 Spawn
 *
 * 【v48 大厂架构修复】
 *   旧版在这里立即调 GM->HandlePlayerRequestSpawn 每个玩家，导致:
 *     - 玩家立即 Spawn → 然后 60s 后 SpawnAllPlayersIntoBattle 又跑一遍
 *     - AI Spawn 被 MatchStartDelay 延迟 (60s)
 *     - 用户关闭 PIE 前 AI 永远不出现
 *   新版: 移除即时玩家 Spawn, 全部交给 SpawnAllPlayersIntoBattle 统一处理
 */
void ARoomPlayerController::Server_RequestStartGame_Implementation()
{
	UE_LOG(LogTemp, Warning, TEXT("[Spawn] Server_RequestStartGame called"));

	if (ARoomGameMode* GM = Cast<ARoomGameMode>(GetWorld()->GetAuthGameMode()))
	{
		// 1. 校验所有玩家已准备
		if (GM->CheckAllPlayersReady())
		{
			// 2. 通知所有客户端切换到战斗状态 (UI 切换: 大厅UI 销毁, 战斗 HUD 显示)
			for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
			{
				if (ARoomPlayerController* PC = Cast<ARoomPlayerController>(It->Get()))
				{
					PC->Client_EnterBattleState();
				}
			}

			// 3. 启动 GameMode PerformGameStart
			//    - 立即广播 OnBattleStarted (AI BT 激活)
			//    - 立即设 CurrentRoomState = BattleInProgress
			//    - 倒计时 MatchStartDelay 秒后回调 SpawnAllPlayersIntoBattle
			//      → 这里才统一处理玩家 Spawn + AI Spawn
			GM->PerformGameStart();
		}
		else
		{
			// 有人没准备，提示房主
			Client_ReceiveSystemMessage(TEXT("系统提示: 房间内有玩家未准备无法开始游戏！"));
		}
	}
}

/**
 * Client_ReceiveSystemMessage_Implementation
 *
 * 接收服务器系统提示
 * 路由: 战斗 HUD 或房间 UI
 */
void ARoomPlayerController::Client_ReceiveSystemMessage_Implementation(const FString& Message)
{
	// 只有在战斗状态（GameHUDWidget 可见）时，才路由到战斗 HUD
	UGameHUDWidget* HUDWidget = GetGameHUDWidget();
	if (HUDWidget && HUDWidget->GetVisibility() != ESlateVisibility::Collapsed)
	{
		HUDWidget->AddSystemMessage(Message);
	}
	// 回退到 RoomUIWidget（房间状态）
	else if (RoomUIWidget)
	{
		RoomUIWidget->AddSystemMessageToChat(Message);
	}
}


// ==========================================
// 7. 战斗生成逻辑
// ==========================================

/**
 * Server_RequestSpawn_Implementation
 *
 * 玩家向服务器请求生成 3D 角色（用于测试 / 复活）
 *
 * 【v36 零兜底改造】不再传空字符串让 GM 走缓存兜底
 *   旧实现: GM->HandlePlayerRequestSpawn(this, TEXT(""), TEXT(""))
 *           → HandlePlayerRequestSpawn 内部用缓存补 (Step 0 合并)
 *           → 这违反"零兜底"原则 (调用方传空, 让 GM 猜)
 *   新实现: 调 Server_RequestSpawnWithLoadout(CharID, WeaponID), 让调用方显式传
 *
 * 大厂原则:
 *   - 调用方必须显式传 Loadout, 不允许"故意传空"
 *   - 复活场景: PlayerController 知道自己的 SelectedCharID/SelectedWeapon1ID
 *   - 测试场景: UI 应该显式给默认值 (例如 JS001/WQ001)
 */
void ARoomPlayerController::Server_RequestSpawn_Implementation()
{
	// 【v36】改为显式传 Loadout
	const FString CharID = GetPlayerState<ARoomPlayerState>()
		? GetPlayerState<ARoomPlayerState>()->GetSelectedCharacterID()
		: FString();
	const FString WeaponID = GetPlayerState<ARoomPlayerState>()
		? GetPlayerState<ARoomPlayerState>()->GetSelectedWeapon1ID()
		: FString();

	if (CharID.IsEmpty() || WeaponID.IsEmpty())
	{
		UE_LOG(LogTemp, Error,
			TEXT("[RoomPlayerController] Server_RequestSpawn: PlayerState.SelectedCharID/WeaponID1 为空, 拒绝 Spawn. "
				 "【v36 零兜底】不再传空字符串让 GM 走缓存. "
				 "【修复】UI 必须在点开始游戏前写入 SelectedCharID/SelectedWeapon1ID (RoomLifecycle 阶段)."));
		return;
	}

	if (ARoomGameMode* GM = Cast<ARoomGameMode>(GetWorld()->GetAuthGameMode()))
	{
		GM->HandlePlayerRequestSpawn(this, CharID, WeaponID);
	}
}


// ==========================================
// 8. 状态监听
// ==========================================

/**
 * OnFlowStateChanged
 *
 * GameFlowSubsystem 状态变化回调
 * InRoom: 显示房间 UI（鼠标UIOnly + 房间 UI 创建）
 * Battleing: 隐藏房间 UI（鼠标GameOnly）+ 重置计分板
 * 其他: 销毁所有 UI + 恢复游戏状态
 */
void ARoomPlayerController::OnFlowStateChanged(EMatchState NewState)
{
	// ==========================================
	// 【DEBUG-SET-5-B】RoomPC 收到状态变化广播: 它是怎么处理 UI 的？
	// ==========================================
	UE_LOG(LogTemp, Error,
		TEXT("[DEBUG-S5-B][RoomPC::OnFlowStateChanged] PID=%u WorldName=%s NewState=%d RoomUIWidget=%p RoomUIInViewport=%d"),
		FPlatformProcess::GetCurrentProcessId(),
		*GetWorld()->GetName(),
		(int32)NewState,
		RoomUIWidget,
		(RoomUIWidget && RoomUIWidget->IsInViewport()) ? 1 : 0);

	// 【状态 A: 正在房间内等待】
	if (NewState == EMatchState::InRoom)
	{
		// 房间里需要用鼠标点 UI
		bShowMouseCursor = true;
		SetInputMode(FInputModeUIOnly());

		// 动态创建 RoomInsidePage
		if (RoomUIClass && !RoomUIWidget)
		{
			RoomUIWidget = CreateWidget<URoomInsidePage>(this, RoomUIClass);
			if (RoomUIWidget)
			{
				RoomUIWidget->AddToViewport();
			}
		}
	}
	// 【大厂 P0 兜底修复 2026.07.03】状态机漏推到 InRoom, 但 World 已是战斗地图
	// ----------------------------------------------------------------------
	// 场景: Joiner 路径 LANRoomPage 漏写 RequestStateOnNextLoad(InRoom)
	//       → PostLoadMapWithWorld 路径 B Broadcast(CurrentState=MainLobby)
	//       → RoomPC 收到的是 MainLobby 而不是 InRoom
	//       → 走到"状态 C: 退出战斗"分支, 把 HUD 关掉, 不创建 RoomUI
	//
	// 修复: 当 NewState=MainLobby 但 World 是战斗地图 (GameMode 是 RoomGameMode 派生)
	//       按 InRoom 处理 → 创建 RoomInsidePage + 设置 UI 输入
	//
	// 设计原则: "物理位置"是真理, 状态机值是同步用的派生量
	//           World 已经加载到战斗地图 = 物理上已经在 InRoom, 必须显示房间 UI
	//
	// 【大厂 P0 修复 2026.07.03】防御性房主身份标定
	// 场景: 勾选"跳过登录"后, GameFlow 启动时已调 RoomService.EnterSkipToHostMode()
	//       但本机可能不是第一次走此分支 (例如: 玩家从 L_Login 进了战斗地图, 又被退回到 MainLobby)
	//       → 这里再调一次 EnterSkipToHostMode() 是幂等的 (内部 if (bIsHost) return 保护)
	//       → 防御性兜底: 万一 RoomService 状态被异常清空, 也能恢复
	// ==========================================
	else if (NewState == EMatchState::MainLobby && GetWorld())
	{
		const FString CurMapName = GetWorld()->GetMapName();
		const bool bIsInBattleMap =
			CurMapName.Contains(TEXT("Japanese_Temple")) ||
			CurMapName.Contains(TEXT("Room")) ||
			CurMapName.Contains(TEXT("Battle")) ||
			CurMapName.Contains(TEXT("Combat"));
		if (bIsInBattleMap)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[RoomPC][P0-Fallback] NewState=MainLobby 但 World=%s 是战斗地图, 按 InRoom 处理 (Joiner 容错)"),
				*CurMapName);

			// 主动同步 GameFlowSubsystem 的状态 (避免后续 PostLoadMapWithWorld 又 Broadcast MainLobby)
			if (UGameInstance* GI = GetGameInstance())
			{
				if (UGameFlowSubsystem* FlowSubsystem = GI->GetSubsystem<UGameFlowSubsystem>())
				{
					// 【防御性房主身份标定 2026.07.03】
					// 场景: 勾选跳过登录时 GameFlow 已标 Host, 但跨图/状态重置后可能丢失
					// 这里幂等再调一次, 内部 bIsHost 已为 true 时直接 return
					if (URoomService* RoomService = URoomService::Get(this))
					{
						RoomService->EnterSkipToHostMode();
					}

					if (FlowSubsystem->GetCurrentState() != EMatchState::InRoom)
					{
						FlowSubsystem->TransitToState(EMatchState::InRoom);
						// 注意: TransitToState 会递归触发 OnFlowStateChanged(InRoom)
						// 这里直接 return, 不要继续走下方"状态 C"分支
						return;
					}
				}
			}

			// 如果 GameFlowSubsystem 不可用 (理论上不可能), 自己手动走房间 UI 创建
			bShowMouseCursor = true;
			SetInputMode(FInputModeUIOnly());
			if (RoomUIClass && !RoomUIWidget)
			{
				RoomUIWidget = CreateWidget<URoomInsidePage>(this, RoomUIClass);
				if (RoomUIWidget)
				{
					RoomUIWidget->AddToViewport();
				}
			}
		}
		else
		{
			// 普通 MainLobby (L_Login 大厅) → 销毁 RoomUI (玩家已离开战斗地图回到大厅)
			if (RoomUIWidget)
			{
				RoomUIWidget->RemoveFromParent();
				RoomUIWidget = nullptr;
			}
			if (bIsEscMenuOpen)
			{
				bIsEscMenuOpen = false;
				if (UGameHUDWidget* HUDWidget = GetGameHUDWidget())
				{
					HUDWidget->HideEscMenu();
				}
			}
			if (ScoreboardWidgetInstance)
			{
				ScoreboardWidgetInstance->RemoveFromParent();
				ScoreboardWidgetInstance = nullptr;
			}
		}
	}
	// 【状态 B: 房主点击了"开始游戏"，真正打起来了】
	else if (NewState == EMatchState::Battleing)
	{
		// 战斗时隐藏鼠标，准星锁定屏幕中心
		bShowMouseCursor = false;
		SetInputMode(FInputModeGameOnly());

		// 将烦人的房间 UI 销毁掉
		if (RoomUIWidget)
		{
			RoomUIWidget->RemoveFromParent();
			RoomUIWidget = nullptr;
		}

		// 战斗开始时，重置所有玩家的计分板数据
		ResetAllPlayerScoreboardStats();
	}
	// 【状态 C: 退出战斗，返回房间或其他状态】
	else
	{
		// 退出战斗态时，重置 ESC 菜单标志位并恢复游戏状态
		if (bIsEscMenuOpen)
		{
			bIsEscMenuOpen = false;

			// 隐藏 ESC 菜单
			if (UGameHUDWidget* HUDWidget = GetGameHUDWidget())
			{
				HUDWidget->HideEscMenu();
			}

			// 恢复输入模式（ESC 菜单关闭时状态已同步，无需重复 SetGamePaused）
		}

		// 销毁计分板 Widget
		if (ScoreboardWidgetInstance)
		{
			ScoreboardWidgetInstance->RemoveFromParent();
			ScoreboardWidgetInstance = nullptr;
		}
	}
}

/**
 * ResetAllPlayerScoreboardStats
 *
 * 服务器端: 重置所有玩家的计分板数据
 * 触发时机: 进入战斗状态时
 */
void ARoomPlayerController::ResetAllPlayerScoreboardStats()
{
	// 只有服务器才有权限重置计分板数据
	if (!HasAuthority())
	{
		return;
	}

	// 遍历所有玩家控制器
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		ARoomPlayerController* PC = Cast<ARoomPlayerController>(It->Get());
		if (PC)
		{
			if (ARoomPlayerState* PS = PC->GetPlayerState<ARoomPlayerState>())
			{
				PS->ResetScoreboardStats();
			}
		}
	}
}


// ==========================================
// 9. 复活系统
// ==========================================

/**
 * StartRespawnTimer
 *
 * 服务器端: 启动玩家复活倒计时
 * 关键: 定时器挂在 Controller 上而非 Character，避免死亡被销毁
 */
void ARoomPlayerController::StartRespawnTimer(float InDelaySeconds)
{
	if (!HasAuthority()) return;

	UE_LOG(LogTemp, Warning, TEXT("[Respawn] Starting respawn timer for %s, delay=%.1fs"), *GetName(), InDelaySeconds);

	// 清理旧定时器
	GetWorldTimerManager().ClearTimer(RespawnTimerHandle);

	// 在 Controller 上启动复活定时器（不会随角色死亡而被销毁）
	GetWorldTimerManager().SetTimer(
		RespawnTimerHandle,
		this,
		&ARoomPlayerController::OnPlayerRespawnTimerFinished,
		InDelaySeconds,
		false);
}

/**
 * OnPlayerRespawnTimerFinished
 *
 * 复活定时器到期回调
 * 调 Server_RequestSpawn 重生角色
 */
void ARoomPlayerController::OnPlayerRespawnTimerFinished()
{
	if (!HasAuthority()) return;

	UE_LOG(LogTemp, Warning, TEXT("[Respawn] OnPlayerRespawnTimerFinished for %s"), *GetName());

	// 向服务器请求复活
	Server_RequestSpawn();
}

/**
 * 验证函数: 选择 Loadout
 */
bool ARoomPlayerController::Server_SelectLoadout_Validate(const FString& CharacterRowName, const FString& Weapon1RowName, const FString& Weapon2RowName) { return true; }

/**
 * Server_SelectLoadout_Implementation
 *
 * 玩家把选中的角色/武器偏好发给服务器
 * 服务器写入 PlayerState 用于开局时按这个生成
 */
void ARoomPlayerController::Server_SelectLoadout_Implementation(const FString& CharacterRowName, const FString& Weapon1RowName, const FString& Weapon2RowName)
{
	UE_LOG(LogTemp, Warning, TEXT("[Room] Server_SelectLoadout: Char='%s', W1='%s', W2='%s'"),
		*CharacterRowName, *Weapon1RowName, *Weapon2RowName);
	if (ARoomPlayerState* PS = GetPlayerState<ARoomPlayerState>())
	{
		PS->SetPlayerLoadout(CharacterRowName, Weapon1RowName, Weapon2RowName);

		// v31.4 P0: 同步到 URoomSpawnSubsystem 缓存 (复活路径的真理源)
		if (URoomSpawnSubsystem* SpawnSys = URoomSpawnSubsystem::Get(this))
		{
			SpawnSys->SetPlayerSpawnData(GetUniqueID(), CharacterRowName, Weapon1RowName);
		}
	}
}


// ==========================================
// 10. 辅助接口
// ==========================================

/**
 * GetGameHUDWidget
 *
 * 通过 HUD 单例获取当前激活的 GameHUDWidget
 * @return GameHUDWidget 指针（找不到返回 nullptr）
 */
UGameHUDWidget* ARoomPlayerController::GetGameHUDWidget() const
{
	if (AMyGameHUD* HUD = Cast<AMyGameHUD>(GetHUD()))
	{
		return HUD->GetGameHUDWidget();
	}
	return nullptr;
}

/**
 * Client_TransitToMatchState_Implementation
 *
 * 服务器命令某个客户端切换全局状态
 * 1. 兜底初始化倒计时
 * 2. 通知本地 GameFlowSubsystem 切换状态
 */
void ARoomPlayerController::Client_TransitToMatchState_Implementation(EMatchState NewState)
{
	// 【客户端专属逻辑】: 这行代码只会在对应的那个客户端本地电脑上执行

	// 0. 在切换状态前，先确保 MatchRemainingTime 被初始化
	if (UWorld* World = GetWorld())
	{
		if (ARoomGameState* RoomGS = World->GetGameState<ARoomGameState>())
		{
			if (RoomGS->GetMatchRemainingSeconds() <= 0)
			{
				switch (RoomGS->CurrentMatchMode)
				{
				case ERoomMatchMode::Melee:
					RoomGS->MatchEndTime = GetWorld()->GetTimeSeconds() + (30 * 60);
					RoomGS->CurrentRound = 0;
					break;
				case ERoomMatchMode::Zombie:
					RoomGS->MatchEndTime = GetWorld()->GetTimeSeconds() + (10 * 60);
					RoomGS->CurrentRound = 5;
					break;
				default:
					break;
				}
				RoomGS->OnMatchModeChanged.Broadcast(RoomGS->CurrentMatchMode);
				RoomGS->OnCurrentRoundUpdated.Broadcast(RoomGS->CurrentRound);
			}
		}
	}

	// 1. 获取当前客户端本地的 GameInstance 及其挂载的 GameFlowSubsystem
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UGameFlowSubsystem* FlowSubsystem = GI->GetSubsystem<UGameFlowSubsystem>())
		{
			// 2. 调用状态机，利用事件多播 (OnStateChanged) 去驱动 UI 切换
			FlowSubsystem->TransitToState(NewState);
		}
	}
}


// ==========================================
// 11. Enhanced Input 回调
// ==========================================

/**
 * SetupInputComponent
 *
 * 重写 UE 原生函数: 设置输入组件
 * 把 Enhanced Input 动作绑定到对应的回调
 */
void ARoomPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	// 强制转换为增强输入组件
	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(InputComponent))
	{
		// 绑定聊天唤醒按键（T）
		if (IA_ToggleChat)
		{
			EnhancedInputComponent->BindAction(IA_ToggleChat, ETriggerEvent::Started, this, &ARoomPlayerController::OnToggleChatAction);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[RoomPlayerController] 未配置 IA_ToggleChat，聊天快捷键无法使用！请在蓝图 BP_RoomPlayerController 中配置"));
		}

		// 绑定 Tab 键: 按下显示计分板，抬起隐藏计分板
		if (IA_ToggleScoreboard)
		{
			UE_LOG(LogTemp, Log, TEXT("[RoomPlayerController] SetupInputComponent: IA_ToggleScoreboard 已绑定"));
			EnhancedInputComponent->BindAction(IA_ToggleScoreboard, ETriggerEvent::Started, this, &ARoomPlayerController::OnScoreboardPressed);
			EnhancedInputComponent->BindAction(IA_ToggleScoreboard, ETriggerEvent::Completed, this, &ARoomPlayerController::OnScoreboardReleased);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[RoomPlayerController] 未配置 IA_ToggleScoreboard，计分板快捷键无法使用！请在蓝图 BP_RoomPlayerController 中配置"));
		}

		// 绑定 ESC 键: 切换 ESC 菜单显示/隐藏
		if (IA_ToggleEscMenu)
		{
			UE_LOG(LogTemp, Log, TEXT("[RoomPlayerController] SetupInputComponent 成功绑定 ESC 键"));
			EnhancedInputComponent->BindAction(IA_ToggleEscMenu, ETriggerEvent::Started, this, &ARoomPlayerController::OnEscPressed);
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("[RoomPlayerController] IA_ToggleEscMenu 未配置！ESC 菜单快捷键无法使用！"));
		}
	}
}

/**
 * OnToggleChatAction
 *
 * T 键按下: 根据当前状态路由到正确的 UI 聊天输入
 */
void ARoomPlayerController::OnToggleChatAction()
{
	// 工业级做法: 根据当前游戏管家的状态，将聊天唤醒指令下发给正确的 UI 面板
	if (UGameFlowSubsystem* FlowSubsystem = GetGameInstance()->GetSubsystem<UGameFlowSubsystem>())
	{
		EMatchState CurrentState = FlowSubsystem->GetCurrentState();

		if (CurrentState == EMatchState::Battleing)
		{
			// 战斗状态下，让战斗 HUD 激活聊天
			if (UGameHUDWidget* HUDWidget = GetGameHUDWidget())
			{
				HUDWidget->ActivateChatInput();
			}
		}
		else if (CurrentState == EMatchState::InRoom)
		{
			// 房间等待状态下，让房间界面激活聊天
			if (RoomUIWidget)
			{
				RoomUIWidget->ActivateChatInput();
			}
		}
	}
}

/**
 * OnScoreboardPressed
 *
 * Tab 按下: 显示计分板
 */
void ARoomPlayerController::OnScoreboardPressed()
{
	UE_LOG(LogTemp, Log, TEXT("[Scoreboard] OnScoreboardPressed 被调用！当前 bIsEscMenuOpen=%s"), bIsEscMenuOpen ? TEXT("true") : TEXT("false"));

	// 通过 GameHUDWidget 显示计分板
	if (UGameHUDWidget* HUDWidget = GetGameHUDWidget())
	{
		HUDWidget->ShowScoreboard();
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[Scoreboard] OnScoreboardPressed: GameHUDWidget 为空！"));
	}
}

/**
 * OnScoreboardReleased
 *
 * Tab 松开: 隐藏计分板
 */
void ARoomPlayerController::OnScoreboardReleased()
{
	UE_LOG(LogTemp, Log, TEXT("[Scoreboard] OnScoreboardReleased 被调用"));

	// 通过 GameHUDWidget 隐藏计分板
	if (UGameHUDWidget* HUDWidget = GetGameHUDWidget())
	{
		HUDWidget->HideScoreboard();
	}
}


// ==========================================
// 12. ESC 菜单控制
// ==========================================

/**
 * OnEscPressed
 *
 * ESC 按下: 切换 ESC 菜单
 * 关键: 用 bool 标志位而非可见性检测
 */
void ARoomPlayerController::OnEscPressed()
{
	UE_LOG(LogTemp, Log, TEXT("[ESC] OnEscPressed 被调用！当前 bIsEscMenuOpen=%s"), bIsEscMenuOpen ? TEXT("true") : TEXT("false"));

	// 只有战斗状态下才能打开 ESC 菜单
	if (UGameFlowSubsystem* FlowSubsystem = GetGameInstance()->GetSubsystem<UGameFlowSubsystem>())
	{
		if (FlowSubsystem->GetCurrentState() != EMatchState::Battleing)
		{
			UE_LOG(LogTemp, Warning, TEXT("[ESC] 当前状态不是 Battleing，拒绝处理"));
			return;
		}
	}

	// 用 bIsEscMenuOpen 标志位做唯一可信真相源
	if (bIsEscMenuOpen)
	{
		HideEscMenu();
	}
	else
	{
		ShowEscMenu();
	}
}

/**
 * ShowEscMenu
 *
 * 显示 ESC 菜单（仅本机切换 InputMode，不影响其他客户端，不使用全局 Pause）
 */
void ARoomPlayerController::ShowEscMenu()
{
	bIsEscMenuOpen = true;

	// 显示 ESC 菜单并切换输入模式
	if (UGameHUDWidget* HUDWidget = GetGameHUDWidget())
	{
		HUDWidget->ShowEscMenu();
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[ESC] ShowEscMenu 获取 GameHUDWidget 失败！"));
	}

	// 设置输入模式: UIOnly，鼠标可操作（仅阻塞本机输入，不影响其他客户端）
	SetInputMode(FInputModeUIOnly());
	bShowMouseCursor = true;
}

/**
 * HideEscMenu
 *
 * 隐藏 ESC 菜单（仅本机恢复 InputMode，不使用全局 Pause）
 */
void ARoomPlayerController::HideEscMenu()
{
	bIsEscMenuOpen = false;

	// 隐藏 ESC 菜单并恢复游戏输入
	if (UGameHUDWidget* HUDWidget = GetGameHUDWidget())
	{
		HUDWidget->HideEscMenu();
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[ESC] HideEscMenu 获取 GameHUDWidget 失败！"));
	}

	// 恢复输入模式: GameOnly
	SetInputMode(FInputModeGameOnly());
	bShowMouseCursor = false;
}


// ==========================================
// 13. 账号在线状态同步 RPC（新增）
// ==========================================

/**
 * Server_NotifyAccountLogin_Validate
 * 验证函数: 账号和 SessionId 不能为空
 */
bool ARoomPlayerController::Server_NotifyAccountLogin_Validate(const FString& Username, const FString& SessionId)
{
	return !Username.IsEmpty() && !SessionId.IsEmpty();
}


/**
 * Server_NotifyAccountLogin_Implementation
 *
 * 房主端: 收到客户端的"上线通知" → 调权威表处理
 * 1. 拿到 (或创建) AccountRoomAuthority 单例
 * 2. 调 HandleLoginRequest 做冲突判定
 * 3. HandleLoginRequest 内部会发 Client_LoginResult 回包
 *
 * 注意: 房主自己 HasAuthority()==true, Server_* 直接本地执行
 */
void ARoomPlayerController::Server_NotifyAccountLogin_Implementation(const FString& Username, const FString& SessionId)
{
	// 防御: 仅在房主(权威端)处理
	if (!HasAuthority())
	{
		return;
	}

	// 拿到或创建权威表
	if (!AccountAuthority.IsValid())
	{
		// 仅当权威表还没建时才新建
		AccountAuthority = NewObject<UAccountRoomAuthority>(this);

		// 启动心跳扫描定时器(5s 扫一次, 15s 超时清理)
		// 内部有 ClearTimer 防御, 多次调用安全
		AccountAuthority->StartSweepTimer(GetWorld());
	}

	// 调用权威表的核心入口(里面会发 Client_LoginResult)
	AccountAuthority->HandleLoginRequest(Username, SessionId, this);

	// 每次有玩家登录后，同步账号列表到 SessionSettings 并广播
	// 注意: HandleLoginRequest 内部已处理完同号冲突，这里只负责同步列表
	SyncRoomAccountsToSession();

	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Cyan,
			FString::Printf(TEXT("[Authority] 收到上线通知: %s (Session=%s)"), *Username, *SessionId));
	}
}


/**
 * Server_NotifyAccountLogout_Validate
 */
bool ARoomPlayerController::Server_NotifyAccountLogout_Validate(const FString& Username, const FString& SessionId)
{
	return true; // 离线下通知允许空,方便异常断线时调
}


/**
 * Server_NotifyAccountLogout_Implementation
 *
 * 房主端: 收到客户端的"下线通知" → 清理权威表
 */
void ARoomPlayerController::Server_NotifyAccountLogout_Implementation(const FString& Username, const FString& SessionId)
{
	if (!HasAuthority())
	{
		return;
	}

	if (AccountAuthority.IsValid())
	{
		AccountAuthority->HandleLogoutRequest(Username, SessionId);

		// 每次有玩家下线后，同步账号列表到 SessionSettings 并广播
		SyncRoomAccountsToSession();
	}
}


// 【P0 清理】删除未使用的 RPC 实现: Server_RequestIsAccountOnline / Client_ReceiveIsAccountOnline
// (YAGNI: 等真有需求时再加回)


/**
 * SyncRoomAccountsToSession
 *
 * 房主端专用: 将 AccountRoomAuthority 中的所有在线账号列表同步到 SessionSettings
 * 并调用 UpdateSession 广播到局域网，供其他客户端在加入前查询
 *
 * 写入流程:
 * 1. 从 AccountSubsystem 获取 HOST_ACCOUNT（房主自己的账号）
 * 2. 从 AccountRoomAuthority 获取所有已登录玩家的账号
 * 3. 合并后写入 ROOM_ACCOUNTS（HOST_ACCOUNT 在第一位）
 * 4. 调用 UpdateSession 推送更新
 *
 * 调用场景:
 * - Server_NotifyAccountLogin_Implementation (玩家加入时)
 * - Server_NotifyAccountLogout_Implementation (玩家离开时)
 * - EndPlay (PC 销毁时的兜底清理)
 */
void ARoomPlayerController::SyncRoomAccountsToSession()
{
	if (!HasAuthority() || !AccountAuthority.IsValid())
	{
		return;
	}

	// 【架构修正】走 AccountService 门面
	FString HostAccountName = TEXT("");
	if (UAccountService* AccountService = UAccountService::Get(this))
	{
		HostAccountName = AccountService->GetCurrentUser();
	}

	// 获取在线子系统接口
	IOnlineSubsystem* OnlineSub = IOnlineSubsystem::Get();
	if (!OnlineSub || !OnlineSub->GetSessionInterface().IsValid())
	{
		return;
	}

	// 委托给 AccountRoomAuthority 完成实际的 Session 同步
	AccountAuthority->SyncAccountsToSessionSettings(OnlineSub->GetSessionInterface(), HostAccountName);
}


/**
 * Client_LoginResult_Implementation
 *
 * 客户端: 收到房主回包
 * - bReject=true    → 弹模态对话框(拒绝进房, 玩家点确认后回登录页)
 * - bSuccess=true   → 静默成功
 * - 其他           → 用 Reason 做普通提示
 */
void ARoomPlayerController::Client_LoginResult_Implementation(bool bSuccess, const FString& Reason, bool bReject)
{
	// 拒绝进房通知: 弹模态对话框
	if (bReject)
	{
		// 调 BP 可调函数, 内部会找到当前 LoginPage 并弹框
		HandleForcedKickNotification();
		return;
	}

	// 普通成功/失败: 用 Debug 提示
	if (!bSuccess)
	{
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 5.0f, FColor::Red,
				FString::Printf(TEXT("[Account] 上线失败: %s"), *Reason));
		}
	}
	else
	{
		if (GEngine)
		{
			GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor::Green,
				TEXT("[Account] 上线成功"));
		}
	}
}


/**
 * Server_Heartbeat_Implementation
 *
 * 房主端: 收到客户端心跳,刷新 LastHeartbeatAt
 * 房主自己 HasAuthority()==true 时直接本地执行
 */
void ARoomPlayerController::Server_Heartbeat_Implementation(const FString& Username, const FString& SessionId)
{
	if (!HasAuthority())
	{
		return;
	}

	if (AccountAuthority.IsValid())
	{
		AccountAuthority->HandleHeartbeat(Username, SessionId);
	}
}


/**
 * SendHeartbeat
 *
 * 客户端定时器回调: 5 秒一次发心跳给房主
 * 房主自己走 Server_* 直接本地执行,不消耗网络
 */
void ARoomPlayerController::SendHeartbeat()
{
	// 没账号就不发(可能玩家没登录就退房了)
	if (MyAccountUsername.IsEmpty() || MyAccountSessionId.IsEmpty())
	{
		return;
	}

	// 调 Server RPC(房主端直接本地执行)
	Server_Heartbeat(MyAccountUsername, MyAccountSessionId);
}


// ==========================================
// 14. UI 跳转入口（新增）
// ==========================================

/**
 * HandleForcedKickNotification
 *
 * BP 可调的 UI 跳转入口
 * 职责: 优先找 LANRoomPage 弹模态对话框(玩家在大厅/选房时被拒)
 *       兜底找 LoginPage 弹模态对话框(理论上不应该走到这里)
 *
 * 实现: 用 UObjectIterator 找 UUserWidget 实例
 *       (不直接 include .h, 改用反射查找, 降低编译期耦合)
 *
 * 注意: LANRoomPage 的 [确认] 按钮只回大厅(不退出账号)
 *       LoginPage 的 [确认] 按钮回登录页(也不退出账号)
 *       想退出账号 → 用户自己点 GameMenuPage 的 Btn_BackToLogin
 */
void ARoomPlayerController::HandleForcedKickNotification()
{
	// ==========================================
	// 1. 优先找 LANRoomPage (玩家在大厅/选房时被拒的场景)
	// ==========================================
	if (UUserWidget* LANPageWidget = FindWidgetByClassName(TEXT("LANRoomPage")))
	{
		UFunction* ShowFunc = LANPageWidget->FindFunction(TEXT("ShowLANRoomConflictDialog"));
		if (ShowFunc)
		{
			LANPageWidget->ProcessEvent(ShowFunc, nullptr);

			if (GEngine)
			{
				GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Yellow,
					TEXT("[Account] 已通知 LANRoomPage 弹模态对话框(回大厅)"));
			}
			return;
		}
	}

	// ==========================================
	// 2. 兜底: 理论上 LANRoomPage 一定能找到(冲突就发生在大厅/选房时)
	//    找不到时走 ExecuteLeaveRoom 直接回大厅(不弹框, 因为没有 UI 可弹)
	// ==========================================
	if (GEngine)
	{
		GEngine->AddOnScreenDebugMessage(-1, 5.0f, FColor::Yellow,
			TEXT("[Account] 强踢通知: 屏幕上没有 LANRoomPage, 直接走 ExecuteLeaveRoom 兜底"));
	}

	// 直接走 ExecuteLeaveRoom(已经包含了 DestroySession + 0.5s 后回大厅)
	ExecuteLeaveRoom();
}


/**
 * FindWidgetByClassName
 *
 * 辅助函数: 在当前 World 上按类名查找 UUserWidget 实例
 * 不直接 include 头文件, 用反射降低耦合
 *
 * @param ClassName 短类名(例如 "LANRoomPage")
 * @return 找到的第一个 UUserWidget 实例, 找不到返回 nullptr
 */
UUserWidget* ARoomPlayerController::FindWidgetByClassName(const FString& ClassName) const
{
	UWorld* MyWorld = GetWorld();
	if (!MyWorld)
	{
		return nullptr;
	}

	// 1. 尝试用反射包路径找类(快速路径)
	UClass* TargetClass = FindObject<UClass>(nullptr, *FString::Printf(TEXT("/Script/MetalSlug01.%s"), *ClassName));
	if (!TargetClass)
	{
		// 2. 退路: 遍历 UClass 找同名类
		for (TObjectIterator<UClass> It; It; ++It)
		{
			if (It->GetName() == ClassName)
			{
				TargetClass = *It;
				break;
			}
		}
	}

	if (!TargetClass)
	{
		return nullptr;
	}

	// 3. 遍历当前 World 上的 UUserWidget, 找到第一个匹配的
	for (TObjectIterator<UUserWidget> It; It; ++It)
	{
		UUserWidget* Widget = *It;
		if (Widget && Widget->GetWorld() == MyWorld && Widget->IsA(TargetClass))
		{
			return Widget;
		}
	}

	return nullptr;
}


// ==========================================
// 15. 生命周期钩子（新增）
// ==========================================

/**
 * EndPlay
 *
 * PC 销毁时:
 * 1. 房主端: 通知权威表清理
 * 2. 客户端: 主动发下线通知(若有)
 *
 * 关键: 用弱引用检查 AccountAuthority 防止野指针
 */
void ARoomPlayerController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 0. 停掉心跳定时器(防止回调里访问已销毁的 PC)
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(HeartbeatTimerHandle);
	}

	// 0.5 【大厂标准 - P0 兜底】清理退房流程订阅的 PostLoadMapWithWorld 全局委托
	//     原因: PC 在 OpenLevel 完成前被销毁 (例如玩家断线), Lambda 会变成野指针
	//           必须主动 Remove, 否则引擎下次跳图会回调到一个失效的 PC
	if (LeaveRoomSafeHandle.IsValid())
	{
		FCoreUObjectDelegates::PostLoadMapWithWorld.Remove(LeaveRoomSafeHandle);
		LeaveRoomSafeHandle.Reset();
	}

	// 1. 客户端: 发下线通知(无论房主是否在, 都发, 不等回包)
	//    注意: PC 已开始销毁, RPC 可能发不出去, 但还是尝试一下
	if (!MyAccountUsername.IsEmpty() && !MyAccountSessionId.IsEmpty())
	{
		Server_NotifyAccountLogout(MyAccountUsername, MyAccountSessionId);
	}

	// 2. 房主端: 通知权威表清理本 PC 的所有记录
	if (HasAuthority() && AccountAuthority.IsValid())
	{
		AccountAuthority->HandleControllerDestroyed(this);

		// 房主下线后，同步账号列表更新（只含 HOST_ACCOUNT，无其他在线玩家）
		SyncRoomAccountsToSession();
	}

	// 3. 【P1 修复】解绑 GameFlow 状态订阅, 防止 PC 销毁后野指针回调
	//    (AddDynamic 是 UFUNCTION 反射, 不通过 GC, 必须显式 RemoveDynamic)
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UGameFlowSubsystem* FlowSubsystem = GI->GetSubsystem<UGameFlowSubsystem>())
		{
			FlowSubsystem->OnStateChanged.RemoveDynamic(this, &ARoomPlayerController::OnFlowStateChanged);
		}
	}

	Super::EndPlay(EndPlayReason);
}


// ==========================================
// 【P0】WithValidation 验证实现 - 防止客户端伪造 RPC 参数
// ==========================================

/**
 * Server_RequestStartGame_Validate
 * 客户端无参数, 无需校验, 但 WithValidation 要求有 Validate 函数
 */
bool ARoomPlayerController::Server_RequestStartGame_Validate()
{
	return true;
}

/**
 * Server_RequestSpawn_Validate
 * 客户端无参数, 无需校验
 */
bool ARoomPlayerController::Server_RequestSpawn_Validate()
{
	return true;
}
