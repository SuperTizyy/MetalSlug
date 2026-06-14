// 版权声明：在项目设置的描述页面填写您的版权信息。

// ==========================================
// 头文件包含区
// ==========================================
// 引入本控制器头文件
#include "Systems/RoomPlayerController.h"

// 引入角色基类（用于 Spawn 相关操作）
#include "Characters/BaseCharacter.h"

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

// 引入 GameFlowSubsystem（流程大管家）
#include "Systems/GameFlowSubsystem.h"

// 引入账号子系统（用于获取登录用户信息）
#include "UI/Login/Core/AccountSubsystem.h"

// 引入 UE 静态函数库（用于 OpenLevel/SetGamePaused 等）
#include "Kismet/GameplayStatics.h"

// 引入在线子系统（用于管理 Session）
#include "OnlineSubsystem.h"

// 引入在线会话接口（用于创建/销毁/搜索 Session）
#include "Interfaces/OnlineSessionInterface.h"

// 引入房间 PlayerState（用于读写玩家个人数据）
#include "UI/Login/Core/RoomPlayerState.h"


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

	// 初始化 ESC 菜单状态标志为关闭
	bIsEscMenuOpen = false;

	// 仅本地玩家控制器才需要订阅（Dedicated Server 端 Controller 不订阅）
	if (IsLocalPlayerController())
	{
		// 【新架构：向大管家报到】
		// 通过 GameInstance 拿到 UGameFlowSubsystem 单例
		if (UGameFlowSubsystem* FlowSubsystem = GetGameInstance()->GetSubsystem<UGameFlowSubsystem>())
		{
			// 订阅管家的状态变化广播
			FlowSubsystem->OnStateChanged.AddDynamic(this, &ARoomPlayerController::OnFlowStateChanged);

			// 主动同步状态: 刚加载进战斗地图时，强制进入房间态
			// 目的: 触发 RoomUI 的挂载
			FlowSubsystem->TransitToState(EMatchState::InRoom);
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
		// 拿到账号子系统
		if (UAccountSubsystem* AccountSub = GI->GetSubsystem<UAccountSubsystem>())
		{
			// 读取当前登录的用户名
			MyName = AccountSub->GetCurrentLoggedInUser();

			// 从账号记录中拿到上次的角色/武器偏好
			const FAccountRecord* MyRecord = AccountSub->GetAccountRecord(MyName);
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
 * 真正执行断网和跳地图的底层逻辑
 * 1. 销毁本地的 Session（通知底层解散局域网）
 * 2. 0.5s 延时后通过 GameFlowSubsystem 切换到 MainLobby
 *
 * 【架构修正】: 不在 lambda 中直接调用 Controller 成员。
 * 改为通过 World 重新获取 GameInstance 和 Subsystem，
 * 避免 Controller 在地图切换过程中被销毁导致的崩溃或卡死。
 */
void ARoomPlayerController::ExecuteLeaveRoom()
{
	// 1. 销毁本地的 Session（通知底层解散局域网）
	if (IOnlineSubsystem* OnlineSub = IOnlineSubsystem::Get())
	{
		if (IOnlineSessionPtr SessionInterface = OnlineSub->GetSessionInterface())
		{
			SessionInterface->DestroySession(NAME_GameSession);
		}
	}

	// ==========================================
	// 【架构修正】: 不在 lambda 中直接调用 Controller 成员
	// ==========================================
	// 使用 TWeakObjectPtr<UWorld> 捕获当前 World，防止 World 已销毁时强引用导致崩溃
	FTimerHandle TravelTimer;
	GetWorld()->GetTimerManager().SetTimer(TravelTimer, [WeakWorld = TWeakObjectPtr<UWorld>(GetWorld())]()
	{
		// 安全检查: World 是否还有效
		if (!WeakWorld.IsValid())
		{
			UE_LOG(LogTemp, Warning, TEXT("[RoomPlayerController] World已失效，无法返回大厅"));
			return;
		}

		UWorld* World = WeakWorld.Get();
		if (UGameInstance* GI = World->GetGameInstance())
		{
			// 优先走"管家"通道，让管家自动调用带 ?offline 的 OpenLevel
			if (UGameFlowSubsystem* FlowSubsystem = GI->GetSubsystem<UGameFlowSubsystem>())
			{
				FlowSubsystem->TransitToState(EMatchState::MainLobby);
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("[RoomPlayerController] GameFlowSubsystem为空，直接 OpenLevel 回大厅"));
				// 兜底: 直接跳地图回登录地图
				UGameplayStatics::OpenLevel(GI, FName("L_Login"), true, TEXT("?offline"));
			}
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[RoomPlayerController] GameInstance为空，无法返回大厅"));
		}
	}, 0.5f, false);
}


// ==========================================
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
 * 验证函数: 添加 AI
 */
bool ARoomPlayerController::Server_AddAI_Validate(bool bToAttackTeam, const FString& CharacterName, int32 Count) { return true; }

/**
 * Server_AddAI_Implementation
 *
 * 服务器端: 处理添加 AI 请求
 * 只有房主（拥有 authority）才能添加 AI
 */
void ARoomPlayerController::Server_AddAI_Implementation(bool bToAttackTeam, const FString& CharacterName, int32 Count)
{
	// 只有房主才有权限加 AI
	if (!HasAuthority()) return;

	if (ARoomGameMode* GM = Cast<ARoomGameMode>(GetWorld()->GetAuthGameMode()))
	{
		// 让 GameMode 去处理具体的添加逻辑
		GM->AddAIToRoom(bToAttackTeam, CharacterName, Count);
	}
}

/**
 * Server_KickPlayer_Implementation
 *
 * 房主踢人 RPC
 * 区分 AI（[AI] 前缀）和真实玩家
 */
void ARoomPlayerController::Server_KickPlayer_Implementation(const FString& PlayerNameToKick)
{
	if (!HasAuthority()) return;

	// 判断是否 AI（AI 名字以 [AI] 开头）
	bool bIsAI = PlayerNameToKick.StartsWith(TEXT("[AI]"));

	// 当前只实现了踢真实玩家（AI 踢人逻辑由 GameMode 内部完成）
	if (!bIsAI)
	{
		// 遍历所有玩家控制器，找到目标
		for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
		{
			ARoomPlayerController* TargetPC = Cast<ARoomPlayerController>(It->Get());
			if (TargetPC && TargetPC->MyPlayerName == PlayerNameToKick)
			{
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
 * 2. 遍历所有玩家: 通知 Client_EnterBattleState + HandlePlayerRequestSpawn
 * 3. 启动服务器端倒计时
 */
void ARoomPlayerController::Server_RequestStartGame_Implementation()
{
	UE_LOG(LogTemp, Warning, TEXT("[Spawn] Server_RequestStartGame called"));
	if (ARoomGameMode* GM = Cast<ARoomGameMode>(GetWorld()->GetAuthGameMode()))
	{
		// 1. 校验所有玩家已准备
		if (GM->CheckAllPlayersReady())
		{
			// 2. 遍历所有玩家
			for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
			{
				if (ARoomPlayerController* PC = Cast<ARoomPlayerController>(It->Get()))
				{
					// 通知该玩家客户端切换到战斗状态
					PC->Client_EnterBattleState();

					FString TargetChar = TEXT("");
					FString TargetWeapon = TEXT("");

					// 【核心修复】: 去玩家自己的 PlayerState 里读取具有唯一真理的数据
					if (ARoomPlayerState* PS = PC->GetPlayerState<ARoomPlayerState>())
					{
						TargetChar = PS->GetSelectedCharacterID();
						// 注意: 这里默认将主武器（武器1）传给 GameMode
						TargetWeapon = PS->GetSelectedWeapon1ID();
						UE_LOG(LogTemp, Warning, TEXT("[Spawn] PC='%s' sending to GM: Char='%s', Weapon='%s'"),
							*PC->MyPlayerName, *TargetChar, *TargetWeapon);
					}

					// 让 GameMode 为该玩家生成 3D 角色和武器
					GM->HandlePlayerRequestSpawn(PC, TargetChar, TargetWeapon);
				}
			}

			// 【核心修复】: 启动服务器倒计时，确保 OnMatchTimerTick 每秒递减
			GM->StartMatchTimer();
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
 */
void ARoomPlayerController::Server_RequestSpawn_Implementation()
{
	// 找咱们的服务器大脑 (RoomGameMode) 报到
	if (ARoomGameMode* GM = Cast<ARoomGameMode>(GetWorld()->GetAuthGameMode()))
	{
		// 传两个空字符串，GameMode 就会自动识别出我们要用"测试白模"
		GM->HandlePlayerRequestSpawn(this, TEXT(""), TEXT(""));
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

			// 恢复输入模式和游戏状态
			SetInputMode(FInputModeGameOnly());
			bShowMouseCursor = false;
			UGameplayStatics::SetGamePaused(this, false);
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
 * 显示 ESC 菜单（暂停游戏 + UIOnly 输入 + 鼠标显示）
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

	// 设置输入模式: UIOnly，鼠标可操作
	SetInputMode(FInputModeUIOnly());
	bShowMouseCursor = true;

	// 暂停游戏
	UGameplayStatics::SetGamePaused(this, true);
}

/**
 * HideEscMenu
 *
 * 隐藏 ESC 菜单（恢复游戏 + GameOnly 输入 + 鼠标隐藏）
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

	// 恢复游戏
	UGameplayStatics::SetGamePaused(this, false);
}
