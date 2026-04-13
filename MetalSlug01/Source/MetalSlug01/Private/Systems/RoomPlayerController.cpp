#include "Systems/RoomPlayerController.h"
#include "UI/Login/Pages/BattleRoom/RoomInsidePage.h"
#include "Systems/RoomGameMode.h"
#include "UI/Login/Core/AccountSubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "OnlineSubsystem.h"
#include "Interfaces/OnlineSessionInterface.h"


void ARoomPlayerController::BeginPlay()
{
	Super::BeginPlay();

	// 【极其重要】：控制器在服务器和客户端都会生成。
	// 但 UI 只能给“本地真实的玩家”生成！所以必须加 IsLocalPlayerController() 判断！
	if (IsLocalPlayerController())
	{
		// // 1. 让鼠标显示出来
		// bShowMouseCursor = true;
		// SetInputMode(FInputModeUIOnly());
		
		// 【测试阶段临时修改】：先把鼠标隐藏，把输入模式改回游戏！
		bShowMouseCursor = false;
		SetInputMode(FInputModeGameOnly());

		// 2. 动态生成房间 UI 并显示在屏幕上
		if (RoomUIClass)
		{
			RoomUIWidget = CreateWidget<URoomInsidePage>(this, RoomUIClass);
			if (RoomUIWidget)
			{
				RoomUIWidget->AddToViewport();
			}
		}
		
		// ==========================================
		// 【核心修复】：不要立刻发 RPC！延迟 0.5 秒再发！
		// 等待引擎底层彻底把客户端和服务器连接完毕！
		// ==========================================
		FTimerHandle DelayHandle;
		GetWorld()->GetTimerManager().SetTimer(DelayHandle, this, &ARoomPlayerController::DelayedSendPlayerInfo, 2.0f, false);
		
		// 【新增】：立刻向服务器请求生成 3D 角色和武器！
		Server_RequestSpawn();
	}
}

// 延迟 0.5 秒后真正执行的函数
void ARoomPlayerController::DelayedSendPlayerInfo()
{
	// 3. 获取自己的名字
	FString MyName = TEXT("未知玩家");
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAccountSubsystem* AccountSub = GI->GetSubsystem<UAccountSubsystem>())
		{
			MyName = AccountSub->GetCurrentLoggedInUser();
		}
	}

	// 4. 呼叫服务器：“报告老大，网络通了，这是我的名字！”
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


// 服务器真正执行的逻辑（只有房主的电脑会运行这段代码）
void ARoomPlayerController::Server_SendPlayerInfo_Implementation(const FString& InPlayerName)
{
	// 【新增这行】：让服务器端的这个对讲机记住自己的名字！
	MyPlayerName = InPlayerName;
	
	// 这段代码只会在房主（服务器）的电脑上运行
	// 获取咱们刚才写的服务器大脑 (RoomGameMode)
	ARoomGameMode* GM = Cast<ARoomGameMode>(GetWorld()->GetAuthGameMode());
	if (GM)
	{
		// 让大脑把这个玩家加入名单，并自动广播给所有人
		GM->AddPlayerToRoom(InPlayerName);
	}
}

// ----------------------------------------------------
// Client RPC 实现区
// ----------------------------------------------------


// 验证函数直接返回 true
bool ARoomPlayerController::Server_RequestChangeTeam_Validate(bool bToRedTeam) { return true; }

void ARoomPlayerController::Server_RequestChangeTeam_Implementation(bool bToRedTeam)
{
	// 获取大脑
	ARoomGameMode* GM = Cast<ARoomGameMode>(GetWorld()->GetAuthGameMode());
	if (GM)
	{
		// 告诉大脑：把 "我(MyPlayerName)" 换到请求的队伍里去！
		GM->ChangePlayerTeam(MyPlayerName, bToRedTeam);
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
// 【新增】真正执行断网和跳地图的底层逻辑
// ----------------------------------------------------
void ARoomPlayerController::ExecuteLeaveRoom()
{
	// 1. 销毁本地的 Session（通知底层解散）
	if (IOnlineSubsystem* OnlineSub = IOnlineSubsystem::Get())
	{
		if (IOnlineSessionPtr SessionInterface = OnlineSub->GetSessionInterface())
		{
			SessionInterface->DestroySession(NAME_GameSession);
		}
	}
	
	// 2. 买好返程车票
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAccountSubsystem* AccountSub = GI->GetSubsystem<UAccountSubsystem>())
		{
			AccountSub->bIsReturningFromRoom = true;
		}
	}

	// =========================================================================
	// 【核心修复 2】：绝不能瞬间切地图！
	// 开启一个 0.5 秒的定时器，给 Windows 操作系统足够的时间去安全释放 7777 端口！
	// 只有端口释放干净了，下一次创房别人才能进得来！
	// =========================================================================
	FTimerHandle TravelTimer;
	GetWorld()->GetTimerManager().SetTimer(TravelTimer, [this]()
	{
		UGameplayStatics::OpenLevel(this, FName("L_Login"), true, TEXT("?offline")); 
	}, 0.5f, false);
}

// ----------------------------------------------------
// 告诉服务器的 RPC 逻辑
// ----------------------------------------------------
bool ARoomPlayerController::Server_LeaveRoom_Validate() { return true; }

void ARoomPlayerController::Server_LeaveRoom_Implementation()
{
	if (ARoomGameMode* GM = Cast<ARoomGameMode>(GetWorld()->GetAuthGameMode()))
	{
		// 这里的 MyPlayerName 是咱们上一步加队时存下来的名字
		GM->RemovePlayerFromRoom(MyPlayerName); 
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
	// 收到了服务器广播来的消息，立刻命令 UI 画在屏幕上！
	if (RoomUIWidget)
	{
		RoomUIWidget->AddChatMessage(SenderName, bIsHost, Message, bIsSystemMsg);
	}
}

// ----------------------------------------------------
// 【新增】处理添加 AI 请求
// ----------------------------------------------------
bool ARoomPlayerController::Server_AddAI_Validate(bool bToRedTeam, const FString& CharacterName, int32 Count) { return true; }

void ARoomPlayerController::Server_AddAI_Implementation(bool bToRedTeam, const FString& CharacterName, int32 Count)
{
	// 只有房主才有权限加 AI
	if (!HasAuthority()) return;

	if (ARoomGameMode* GM = Cast<ARoomGameMode>(GetWorld()->GetAuthGameMode()))
	{
		// 让 GameMode 去处理具体的添加逻辑
		GM->AddAIToRoom(bToRedTeam, CharacterName, Count);
	}
}

// ----------------------------------------------------
// 【修改】房主专用的踢人逻辑 (兼容 AI)
// ----------------------------------------------------
void ARoomPlayerController::Server_KickPlayer_Implementation(const FString& PlayerNameToKick)
{
	if (!HasAuthority()) return;

	// 1. 判断是不是踢的 AI
	// 我们之前规定了 AI 的名字以 "[AI]" 开头
	bool bIsAI = PlayerNameToKick.StartsWith(TEXT("[AI]"));

	if (!bIsAI)
	{
		// 2. 如果是真人，执行你原有的狙击逻辑
		for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
		{
			ARoomPlayerController* TargetPC = Cast<ARoomPlayerController>(It->Get());
			if (TargetPC && TargetPC->MyPlayerName == PlayerNameToKick)
			{
				TargetPC->Client_BeKicked();
				break;
			}
		}
	}

	// 3. 呼叫大脑（GameMode），把名字从大名单抹除，并触发房间 UI 刷新广播！
	// 无论真人还是 AI，统一由 GameMode 负责从数组里删除
	if (ARoomGameMode* GM = Cast<ARoomGameMode>(GetWorld()->GetAuthGameMode()))
	{
		GM->RemovePlayerFromRoom(PlayerNameToKick);
	}
}

bool ARoomPlayerController::Server_ToggleReady_Validate(bool bIsReady) { return true; }

void ARoomPlayerController::Server_ToggleReady_Implementation(bool bIsReady)
{
	// 服务器收到请求后，交给大脑 (GameMode) 处理
	if (ARoomGameMode* GM = Cast<ARoomGameMode>(GetWorld()->GetAuthGameMode()))
	{
		GM->UpdatePlayerReadyState(MyPlayerName, bIsReady);
	}
}

void ARoomPlayerController::Client_UpdatePlayerReadyState_Implementation(const FString& PlayerName, bool bIsReady)
{
	// 本地客户端收到服务器的指令，立刻让 UI 刷新那条数据！
	if (RoomUIWidget)
	{
		RoomUIWidget->UpdatePlayerReadyStateUI(PlayerName, bIsReady);
	}
}

void ARoomPlayerController::Client_UpdateRoomUI_Implementation(const TArray<FString>& RedTeam, const TArray<FString>& BlueTeam, const FString& HostName)
{
	if (RoomUIWidget)
	{
		// 将房主名字一起传给 UI 页面
		RoomUIWidget->UpdateTeamLists(RedTeam, BlueTeam, HostName);
	}
}

void ARoomPlayerController::Server_RequestStartGame_Implementation()
{
	if (ARoomGameMode* GM = Cast<ARoomGameMode>(GetWorld()->GetAuthGameMode()))
	{
		bool bAllRealPlayersReady = true;

		// ==========================================
		// 【终极检验】：遍历房间里所有连接的“真实玩家”！
		// （注意：GetPlayerControllerIterator 天然只获取真人，完美避开 AI！）
		// ==========================================
		for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
		{
			ARoomPlayerController* TargetPC = Cast<ARoomPlayerController>(It->Get());
			if (TargetPC)
			{
				// 房主自己（也就是发送请求的这个人）不需要准备，直接跳过检查
				if (TargetPC == this) continue;

				// 在 GameMode 的字典里查找这个真实玩家是否已准备
				bool* bIsReady = GM->PlayerReadyStates.Find(TargetPC->MyPlayerName);
				
				// 如果字典里找不到他，或者他的状态是 false，说明有人没准备！
				if (!bIsReady || !(*bIsReady))
				{
					bAllRealPlayersReady = false;
					break; // 只要发现一个没准备的，立刻跳出循环，绝不留情！
				}
			}
		}

		if (bAllRealPlayersReady)
		{
			// ==========================================
			// 所有人均已准备，执行开始游戏逻辑！
			// ==========================================
			if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 5.0f, FColor::Green, TEXT("所有人均已准备，服务器即将开始游戏！"));
			
			// TODO: 在这里调用你真实的切换地图逻辑，例如：
			// GetWorld()->ServerTravel(TEXT("/Game/Maps/你的战斗地图名字?listen"));
		}
		else
		{
			// ==========================================
			// 有人没准备，给房主客户端发回一条系统提示！
			// ==========================================
			Client_ReceiveSystemMessage(TEXT("系统提示：房间内有玩家未准备无法开始游戏！"));
		}
	}
}

void ARoomPlayerController::Client_ReceiveSystemMessage_Implementation(const FString& Message)
{
	// 拿到服务器发来的警告后，命令大厅 UI 在聊天框里打印黄字！
	if (RoomUIWidget)
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

