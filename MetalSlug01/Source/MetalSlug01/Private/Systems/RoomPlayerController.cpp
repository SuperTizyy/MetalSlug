#include "Systems/RoomPlayerController.h"
#include "Characters/BaseCharacter.h"

#include "EnhancedInputComponent.h"
#include "UI/Login/Pages/BattleRoom/RoomInsidePage.h"
#include "UI/MyGameHUD.h"
#include "UI/Game/GameHUDWidget.h"
#include "UI/Game/Widgets/ScoreboardWidget.h"
#include "UI/Game/Widgets/EscMenuWidget.h"
#include "Systems/RoomGameMode.h"
#include "Systems/RoomGameState.h"
#include "Systems/GameFlowSubsystem.h"
#include "UI/Login/Core/AccountSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "OnlineSubsystem.h"
#include "Interfaces/OnlineSessionInterface.h"
#include "UI/Login/Core/RoomPlayerState.h"


void ARoomPlayerController::BeginPlay()
{
	Super::BeginPlay();

	// 初始化 ESC 菜单状态标志
	bIsEscMenuOpen = false;

	if (IsLocalPlayerController())
	{
		// 【新架构：向大管家报到】
		if (UGameFlowSubsystem* FlowSubsystem = GetGameInstance()->GetSubsystem<UGameFlowSubsystem>())
		{
			FlowSubsystem->OnStateChanged.AddDynamic(this, &ARoomPlayerController::OnFlowStateChanged);
			
			// 主动同步状态：刚加载进战斗地图时，强制进入房间态
			FlowSubsystem->TransitToState(EMatchState::InRoom);
		}

		// 延迟 0.5 秒再发，等待底层网络连接稳固
		FTimerHandle DelayHandle;
		GetWorld()->GetTimerManager().SetTimer(DelayHandle, this, &ARoomPlayerController::DelayedSendPlayerInfo, 2.0f, false);
		
		//Server_RequestSpawn();
	}
}

// 延迟 0.5 秒后真正执行的函数
void ARoomPlayerController::DelayedSendPlayerInfo()
{
	FString MyName = TEXT("未知玩家");

	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAccountSubsystem* AccountSub = GI->GetSubsystem<UAccountSubsystem>())
		{
			MyName = AccountSub->GetCurrentLoggedInUser();

			const FAccountRecord* MyRecord = AccountSub->GetAccountRecord(MyName);
			if (MyRecord)
			{
				UE_LOG(LogTemp, Warning, TEXT("[Room] DelayedSendPlayerInfo: Char='%s', W1='%s', W2='%s'"),
					*MyRecord->LastSelectedCharacter, *MyRecord->LastSelectedWeapon1, *MyRecord->LastSelectedWeapon2);
				// 【修复 1】：直接呼叫自身的 RPC，将初始数据推送到服务器！
				Server_SelectLoadout(MyRecord->LastSelectedCharacter, MyRecord->LastSelectedWeapon1, MyRecord->LastSelectedWeapon2);
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("[Room] DelayedSendPlayerInfo: No record for '%s'"), *MyName);
			}
		}
	}
	Server_SendPlayerInfo(MyName);
}

// ----------------------------------------------------
// Server RPC 实现区
// ----------------------------------------------------
// 防作弊校验逻辑（直接返回 true 表示全部放行）
bool ARoomPlayerController::Server_SendPlayerInfo_Validate(const FString& InPlayerName)
{
	return true; 
}


// 1. 发送玩家信息
void ARoomPlayerController::Server_SendPlayerInfo_Implementation(const FString& InPlayerName)
{
	// 1. 记录在 Controller 自己的变量中（备用）
	MyPlayerName = InPlayerName;
	
	// ==========================================
	// 【工业级修复】：强制覆写底层 PlayerState 名称！
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
		UE_LOG(LogTemp, Warning, TEXT("[RoomPlayerController] 警告：尚未获取到 PlayerState！名称同步可能失败。"));
	}

	// 2. 将控制权转交给服务器大脑（GameMode）处理注册逻辑
	if (ARoomGameMode* GM = Cast<ARoomGameMode>(GetWorld()->GetAuthGameMode()))
	{
		GM->AddPlayerToRoom(this, InPlayerName);
	}
}

void ARoomPlayerController::Client_EnterBattleState_Implementation()
{
	// 0. 在切换状态前，先确保倒计时被初始化（解决测试时 PerformGameStart 未被调用的兜底逻辑）
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

	// 每个玩家（客户端）收到服务器的开打指令后，立刻向本地的流程大管家报到！
	if (UGameFlowSubsystem* FlowSubsystem = GetGameInstance()->GetSubsystem<UGameFlowSubsystem>())
	{
		// 管家收到指令，会自动触发 OnFlowStateChanged
		// 继而销毁你的 RoomInsidePage，隐藏鼠标，并呼出战斗准星！
		FlowSubsystem->TransitToState(EMatchState::Battleing);
	}
}

// ----------------------------------------------------
// Client RPC 实现区
// ----------------------------------------------------


// 验证函数直接返回 true
bool ARoomPlayerController::Server_RequestChangeTeam_Validate(bool bToAttackTeam) { return true; }

// 2. 切换队伍
void ARoomPlayerController::Server_RequestChangeTeam_Implementation(bool bToAttackTeam)
{
	if (ARoomGameMode* GM = Cast<ARoomGameMode>(GetWorld()->GetAuthGameMode()))
	{
		// 【修复】：传递 this
		GM->ChangePlayerTeam(this, bToAttackTeam);
	}
}

// ----------------------------------------------------
// UI 点击后触发的本地逻辑
// ----------------------------------------------------
void ARoomPlayerController::LeaveRoom()
{
	// 【情况 A】：如果我是房主 (服务器端拥有最高权限)
	if (HasAuthority()) 
	{
		// 1. 遍历房间里所有人，给其他玩家发“遣散令”
		for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
		{
			ARoomPlayerController* PC = Cast<ARoomPlayerController>(It->Get());
			// 如果这个控制器存在，并且不是房主自己，就命令他退房！
			if (PC && PC != this) 
			{
				PC->Client_ForceLeaveRoom();
			}
		}

		// 2. 给遣散令 0.5 秒的网络传输时间，然后房主自己再走！
		GetWorld()->GetTimerManager().SetTimer(HostLeaveTimer, this, &ARoomPlayerController::ExecuteLeaveRoom, 0.5f, false);
	}
	// 【情况 B】：如果我是普通客户端玩家
	else 
	{
		// 告诉服务器我要走了，把我的名字从别人屏幕抹掉
		Server_LeaveRoom(); 
		// 自己立刻乖乖走人
		ExecuteLeaveRoom(); 
	}
}

// ----------------------------------------------------
// 【新增】接收到房主的遣散令（被迫退房）
// ----------------------------------------------------
void ARoomPlayerController::Client_ForceLeaveRoom_Implementation()
{
	if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, TEXT("房主已解散房间！被迫返回大厅..."));
	
	// 收到命令，乖乖执行走人逻辑
	ExecuteLeaveRoom(); 
}

// ----------------------------------------------------
// 真正执行断网和跳地图的底层逻辑
// ----------------------------------------------------
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
	// 【架构修正】：不在 lambda 中直接调用 Controller 成员。
	// 改为通过 World 重新获取 GameInstance 和 Subsystem，
	// 避免 Controller 在地图切换过程中被销毁导致的崩溃或卡死。
	// ==========================================
	FTimerHandle TravelTimer;
	GetWorld()->GetTimerManager().SetTimer(TravelTimer, [WeakWorld = TWeakObjectPtr<UWorld>(GetWorld())]()
	{
		if (!WeakWorld.IsValid())
		{
			UE_LOG(LogTemp, Warning, TEXT("[RoomPlayerController] World已失效，无法返回大厅"));
			return;
		}

		UWorld* World = WeakWorld.Get();
		if (UGameInstance* GI = World->GetGameInstance())
		{
			if (UGameFlowSubsystem* FlowSubsystem = GI->GetSubsystem<UGameFlowSubsystem>())
			{
				// 告诉管家：我要回大厅！管家会自动调用带着 ?offline 的 OpenLevel 把你送回去。
				FlowSubsystem->TransitToState(EMatchState::MainLobby);
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("[RoomPlayerController] GameFlowSubsystem为空，直接 OpenLevel 回大厅"));
				// 兜底：直接跳地图回登录地图
				UGameplayStatics::OpenLevel(GI, FName("L_Login"), true, TEXT("?offline"));
			}
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[RoomPlayerController] GameInstance为空，无法返回大厅"));
		}
	}, 0.5f, false);
}

// ----------------------------------------------------
// 告诉服务器的 RPC 逻辑
// ----------------------------------------------------
bool ARoomPlayerController::Server_LeaveRoom_Validate() { return true; }

// 4. 离开房间
void ARoomPlayerController::Server_LeaveRoom_Implementation()
{
	if (ARoomGameMode* GM = Cast<ARoomGameMode>(GetWorld()->GetAuthGameMode()))
	{
		// 【修复】：传递 this
		GM->RemovePlayerFromRoom(this); 
	}
}

// ----------------------------------------------------
// 【新增】房主专用的踢人逻辑
// ----------------------------------------------------
bool ARoomPlayerController::Server_KickPlayer_Validate(const FString& PlayerNameToKick) { return true; }



// ----------------------------------------------------
// 【新增】倒霉蛋收到被踢指令
// ----------------------------------------------------
void ARoomPlayerController::Client_BeKicked_Implementation()
{
	// 屏幕飘红字，让玩家死个明白
	if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 5.f, FColor::Red, TEXT("你已被房主移出房间！"));
	
	// 直接复用你刚才写好的完美退房底层逻辑（断网、拿车票、跳地图一气呵成！）
	ExecuteLeaveRoom(); 
}

bool ARoomPlayerController::Server_SendChatMessage_Validate(const FString& Message) { return true; }

void ARoomPlayerController::Server_SendChatMessage_Implementation(const FString& Message)
{
	// 我发了消息，求助服务器大脑帮我全频道广播！
	if (ARoomGameMode* GM = Cast<ARoomGameMode>(GetWorld()->GetAuthGameMode()))
	{
		GM->BroadcastChatMessage(MyPlayerName, Message);
	}
}

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

// ----------------------------------------------------
// 【新增】处理添加 AI 请求
// ----------------------------------------------------
bool ARoomPlayerController::Server_AddAI_Validate(bool bToAttackTeam, const FString& CharacterName, int32 Count) { return true; }

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


// 5. 房主踢人 (这里需要把查找到的 TargetPC 传给大脑)
void ARoomPlayerController::Server_KickPlayer_Implementation(const FString& PlayerNameToKick)
{
	if (!HasAuthority()) return;
	bool bIsAI = PlayerNameToKick.StartsWith(TEXT("[AI]"));

	if (!bIsAI)
	{
		for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
		{
			ARoomPlayerController* TargetPC = Cast<ARoomPlayerController>(It->Get());
			if (TargetPC && TargetPC->MyPlayerName == PlayerNameToKick)
			{
				TargetPC->Client_BeKicked();
				
				// 【修复】：将被踢人的 Controller 传过去
				if (ARoomGameMode* GM = Cast<ARoomGameMode>(GetWorld()->GetAuthGameMode()))
				{
					GM->RemovePlayerFromRoom(TargetPC);
				}
				break;
			}
		}
	}
}

bool ARoomPlayerController::Server_ToggleReady_Validate(bool bIsReady) { return true; }

// 3. 切换准备状态
void ARoomPlayerController::Server_ToggleReady_Implementation(bool bIsReady)
{
	if (ARoomGameMode* GM = Cast<ARoomGameMode>(GetWorld()->GetAuthGameMode()))
	{
		GM->UpdatePlayerReadyState(this, bIsReady);
	}
}

// 开局时提取正确的数据传给 GameMode
void ARoomPlayerController::Server_RequestStartGame_Implementation()
{
	UE_LOG(LogTemp, Warning, TEXT("[Spawn] Server_RequestStartGame called"));
	if (ARoomGameMode* GM = Cast<ARoomGameMode>(GetWorld()->GetAuthGameMode()))
	{
		if (GM->CheckAllPlayersReady())
		{
			for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
			{
				if (ARoomPlayerController* PC = Cast<ARoomPlayerController>(It->Get()))
				{
					PC->Client_EnterBattleState();

					FString TargetChar = TEXT("");
					FString TargetWeapon = TEXT("");

					// 【核心修复】：去玩家自己的 PlayerState 里读取具有唯一真理的数据
					if (ARoomPlayerState* PS = PC->GetPlayerState<ARoomPlayerState>())
					{
						TargetChar = PS->GetSelectedCharacterID();
						// 注意：这里默认将主武器（武器1）传给 GameMode。
						// 如果你的 HandlePlayerRequestSpawn 支持双武器，你可以把 GetSelectedWeapon2ID() 也传进去。
						TargetWeapon = PS->GetSelectedWeapon1ID();
						UE_LOG(LogTemp, Warning, TEXT("[Spawn] PC='%s' sending to GM: Char='%s', Weapon='%s'"),
							*PC->MyPlayerName, *TargetChar, *TargetWeapon);
					}

					GM->HandlePlayerRequestSpawn(PC, TargetChar, TargetWeapon);
				}
			}

			// 【核心修复】：启动服务器倒计时，确保 OnMatchTimerTick 每秒递减 MatchRemainingTime
			GM->StartMatchTimer();
		}
		else
		{
			Client_ReceiveSystemMessage(TEXT("系统提示：房间内有玩家未准备无法开始游戏！"));
		}
	}
}

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

// ----------------------------------------------------
// 【新增】战斗生成逻辑实现
// ----------------------------------------------------
void ARoomPlayerController::Server_RequestSpawn_Implementation()
{
	// 找咱们的服务器大脑 (RoomGameMode) 报到！
	if (ARoomGameMode* GM = Cast<ARoomGameMode>(GetWorld()->GetAuthGameMode()))
	{
		// 告诉大脑：这是我的对讲机，给我发枪！（我们下一步就去 RoomGameMode 里写这个函数）
		// 传两个空字符串，GameMode 就会自动识别出我们要用“测试白模”
		GM->HandlePlayerRequestSpawn(this, TEXT(""), TEXT(""));
	}
}

void ARoomPlayerController::OnFlowStateChanged(EMatchState NewState)
{
	// 【状态 A：正在房间内等待】
	if (NewState == EMatchState::InRoom)
	{
		// 房间里需要用鼠标点 UI
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
	// 【状态 B：房主点击了“开始游戏”，真正打起来了！】
	else if (NewState == EMatchState::Battleing)
	{
		// 战斗时隐藏鼠标，准星锁定屏幕中心
		bShowMouseCursor = false;
		SetInputMode(FInputModeGameOnly());

		// 将烦人的房间 UI 销毁掉！
		if (RoomUIWidget)
		{
			RoomUIWidget->RemoveFromParent();
			RoomUIWidget = nullptr;
		}

		// 战斗开始时，重置所有玩家的计分板数据
		ResetAllPlayerScoreboardStats();
	}
	// 【状态 C：退出战斗，返回房间或其他状态】
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

		// 销毁计分板Widget
		if (ScoreboardWidgetInstance)
		{
			ScoreboardWidgetInstance->RemoveFromParent();
			ScoreboardWidgetInstance = nullptr;
		}
	}
}

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

void ARoomPlayerController::OnPlayerRespawnTimerFinished()
{
	if (!HasAuthority()) return;

	UE_LOG(LogTemp, Warning, TEXT("[Respawn] OnPlayerRespawnTimerFinished for %s"), *GetName());

	// 向服务器请求复活
	Server_RequestSpawn();
}

// 验证函数
bool ARoomPlayerController::Server_SelectLoadout_Validate(const FString& CharacterRowName, const FString& Weapon1RowName, const FString& Weapon2RowName) { return true; }

void ARoomPlayerController::Server_SelectLoadout_Implementation(const FString& CharacterRowName, const FString& Weapon1RowName, const FString& Weapon2RowName)
{
	// 工业级规范：指针校验
	UE_LOG(LogTemp, Warning, TEXT("[Room] Server_SelectLoadout: Char='%s', W1='%s', W2='%s'"),
		*CharacterRowName, *Weapon1RowName, *Weapon2RowName);
	if (ARoomPlayerState* PS = GetPlayerState<ARoomPlayerState>())
	{
		PS->SetPlayerLoadout(CharacterRowName, Weapon1RowName, Weapon2RowName);
	}
}

UGameHUDWidget* ARoomPlayerController::GetGameHUDWidget() const
{
	if (AMyGameHUD* HUD = Cast<AMyGameHUD>(GetHUD()))
	{
		return HUD->GetGameHUDWidget();
	}
	return nullptr;
}

void ARoomPlayerController::Client_TransitToMatchState_Implementation(EMatchState NewState)
{
	// 【客户端专属逻辑】：这行代码只会在对应的那个客户端本地电脑上执行！

	// 0. 在切换状态前，先确保 MatchRemainingTime 被初始化
	//    （防止测试时 PerformGameStart 未被调用导致倒计时一直是 0）
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
			// 2. 调用您之前写好的状态机，利用事件多播 (OnStateChanged) 去驱动 UI 切换！
			FlowSubsystem->TransitToState(NewState);
		}
	}
}

void ARoomPlayerController::SetupInputComponent()
{
	Super::SetupInputComponent();

	// 强制转换为增强输入组件
	if (UEnhancedInputComponent* EnhancedInputComponent = Cast<UEnhancedInputComponent>(InputComponent))
	{
		// 绑定聊天唤醒按键
		if (IA_ToggleChat)
		{
			EnhancedInputComponent->BindAction(IA_ToggleChat, ETriggerEvent::Started, this, &ARoomPlayerController::OnToggleChatAction);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[RoomPlayerController] 未配置 IA_ToggleChat，聊天快捷键无法使用！请在蓝图 BP_RoomPlayerController 中配置"));
		}

		// 绑定Tab键：按下显示计分板，抬起隐藏计分板
		if (IA_ToggleScoreboard)
		{
			UE_LOG(LogTemp, Log, TEXT("[RoomPlayerController] SetupInputComponent: IA_ToggleScoreboard 已绑定"));
			// 按下时显示
			EnhancedInputComponent->BindAction(IA_ToggleScoreboard, ETriggerEvent::Started, this, &ARoomPlayerController::OnScoreboardPressed);
			// 抬起时隐藏
			EnhancedInputComponent->BindAction(IA_ToggleScoreboard, ETriggerEvent::Completed, this, &ARoomPlayerController::OnScoreboardReleased);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[RoomPlayerController] 未配置 IA_ToggleScoreboard，计分板快捷键无法使用！请在蓝图 BP_RoomPlayerController 中配置"));
		}

		// 绑定ESC键：切换ESC菜单显示/隐藏
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

void ARoomPlayerController::OnToggleChatAction()
{
	// 工业级做法：根据当前游戏管家的状态，将聊天唤醒指令下发给正确的 UI 面板
	if (UGameFlowSubsystem* FlowSubsystem = GetGameInstance()->GetSubsystem<UGameFlowSubsystem>())
	{
		EMatchState CurrentState = FlowSubsystem->GetCurrentState();

		if (CurrentState == EMatchState::Battleing)
		{
			// 战斗状态下，让战斗 HUD 激活聊天
			if (UGameHUDWidget* HUDWidget = GetGameHUDWidget())
			{
				// 此函数需要在 GameHUDWidget 中暴露，供 Controller 调用
				HUDWidget->ActivateChatInput();
			}
		}
		else if (CurrentState == EMatchState::InRoom)
		{
			// 房间等待状态下，让房间界面激活聊天
			if (RoomUIWidget)
			{
				// 此函数需要在 RoomInsidePage 中暴露，供 Controller 调用
				RoomUIWidget->ActivateChatInput(); 
			}
		}
	}
}

void ARoomPlayerController::OnScoreboardPressed()
{
	UE_LOG(LogTemp, Log, TEXT("[Scoreboard] OnScoreboardPressed 被调用！当前 bIsEscMenuOpen=%s"), bIsEscMenuOpen ? TEXT("true") : TEXT("false"));

	// 通过GameHUDWidget显示计分板
	if (UGameHUDWidget* HUDWidget = GetGameHUDWidget())
	{
		if (HUDWidget->GetWidget_Scoreboard())
		{
			UE_LOG(LogTemp, Log, TEXT("[Scoreboard] Widget_Scoreboard 存在，开始显示"));
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("[Scoreboard] OnScoreboardPressed: Widget_Scoreboard 为空！"));
		}
		HUDWidget->ShowScoreboard();
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[Scoreboard] OnScoreboardPressed: GameHUDWidget 为空！"));
	}
}

void ARoomPlayerController::OnScoreboardReleased()
{
	UE_LOG(LogTemp, Log, TEXT("[Scoreboard] OnScoreboardReleased 被调用"));

	// 通过GameHUDWidget隐藏计分板
	if (UGameHUDWidget* HUDWidget = GetGameHUDWidget())
	{
		HUDWidget->HideScoreboard();
	}
}

void ARoomPlayerController::OnEscPressed()
{
	// 【诊断日志】：确认按键是否触发
	UE_LOG(LogTemp, Log, TEXT("[ESC] OnEscPressed 被调用！当前 bIsEscMenuOpen=%s"), bIsEscMenuOpen ? TEXT("true") : TEXT("false"));

	// 只有战斗状态下才能打开ESC菜单
	if (UGameFlowSubsystem* FlowSubsystem = GetGameInstance()->GetSubsystem<UGameFlowSubsystem>())
	{
		if (FlowSubsystem->GetCurrentState() != EMatchState::Battleing)
		{
			UE_LOG(LogTemp, Warning, TEXT("[ESC] 当前状态不是 Battleing，拒绝处理"));
			return;
		}
	}

	UE_LOG(LogTemp, Log, TEXT("[ESC] 状态检查通过，准备切换菜单"));

	// ==========================================
	// 【核心修复】：用 bool 标志位代替不可靠的可见性检测！
	// 蓝图中的 Widget 可见性（Visible / SelfHitTestInvisible / Hidden）
	// 都对 GetVisibility() != Collapsed 返回 true，无法区分菜单开关状态。
	// 用成员变量 bIsEscMenuOpen 作为唯一可信真相源，彻底断绝对蓝图配置的依赖。
	// ==========================================
	if (bIsEscMenuOpen)
	{
		UE_LOG(LogTemp, Log, TEXT("[ESC] 执行 HideEscMenu()"));
		HideEscMenu();
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("[ESC] 执行 ShowEscMenu()"));
		ShowEscMenu();
	}
}

void ARoomPlayerController::ShowEscMenu()
{
	bIsEscMenuOpen = true;
	UE_LOG(LogTemp, Log, TEXT("[ESC] ShowEscMenu 开始，bIsEscMenuOpen=%s"), bIsEscMenuOpen ? TEXT("true") : TEXT("false"));

	// 显示ESC菜单并切换输入模式
	if (UGameHUDWidget* HUDWidget = GetGameHUDWidget())
	{
		UE_LOG(LogTemp, Log, TEXT("[ESC] 获取到 GameHUDWidget 地址: %s"), *FString::Printf(TEXT("%p"), HUDWidget));
		HUDWidget->ShowEscMenu();
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[ESC] ShowEscMenu 获取 GameHUDWidget 失败！"));
	}

	// 设置输入模式：UIOnly，鼠标可操作
	SetInputMode(FInputModeUIOnly());
	bShowMouseCursor = true;

	// 暂停游戏
	UGameplayStatics::SetGamePaused(this, true);
	UE_LOG(LogTemp, Log, TEXT("[ESC] ShowEscMenu 完成"));
}

void ARoomPlayerController::HideEscMenu()
{
	bIsEscMenuOpen = false;
	UE_LOG(LogTemp, Log, TEXT("[ESC] HideEscMenu 开始，bIsEscMenuOpen=%s"), bIsEscMenuOpen ? TEXT("true") : TEXT("false"));

	// 隐藏ESC菜单并恢复游戏输入
	if (UGameHUDWidget* HUDWidget = GetGameHUDWidget())
	{
		UE_LOG(LogTemp, Log, TEXT("[ESC] 获取到 GameHUDWidget 地址: %s"), *FString::Printf(TEXT("%p"), HUDWidget));
		HUDWidget->HideEscMenu();
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[ESC] HideEscMenu 获取 GameHUDWidget 失败！"));
	}

	// 恢复输入模式：GameOnly
	SetInputMode(FInputModeGameOnly());
	bShowMouseCursor = false;

	// 恢复游戏
	UGameplayStatics::SetGamePaused(this, false);
	UE_LOG(LogTemp, Log, TEXT("[ESC] HideEscMenu 完成"));
}

