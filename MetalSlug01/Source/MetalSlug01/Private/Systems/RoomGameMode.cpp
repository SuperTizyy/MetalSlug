#include "Systems/RoomGameMode.h"

#include "OnlineSessionSettings.h"
#include "OnlineSubsystem.h"
#include "Systems/RoomPlayerController.h"
#include "Engine/World.h"
#include "Interfaces/OnlineSessionInterface.h"

ARoomGameMode::ARoomGameMode(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// 【强行关闭无缝漫游】：在基础局域网测试中，开启它纯属自找麻烦！
	bUseSeamlessTravel = false;
}

void ARoomGameMode::AddPlayerToRoom(const FString& PlayerName)
{
	// 【智能分配算法】：红队人少就去红队，否则去蓝队
	if (RedTeamNames.Num() <= BlueTeamNames.Num())
	{
		RedTeamNames.Add(PlayerName);
	}
	else
	{
		BlueTeamNames.Add(PlayerName);
	}
	// 【新增】：向全服播报绿字提示！
	BroadcastSystemMessage(FString::Printf(TEXT("玩家【%s】加入了房间"), *PlayerName));
	
	// 名单更新了，立刻通知全房间的人刷新 UI！
	BroadcastRoomUpdate();
}

void ARoomGameMode::BroadcastRoomUpdate()
{
	// 1. 【核心修复】：必须在 for 循环外面先声明并获取 HostName！
	FString HostName = TEXT("");
	if (ARoomPlayerController* HostPC = Cast<ARoomPlayerController>(GetWorld()->GetFirstPlayerController()))
	{
		HostName = HostPC->MyPlayerName;
	}
	
	// ==========================================
	// 【新增核心逻辑】：把最新的总人数更新到大厅广告牌上！
	// ==========================================
	IOnlineSubsystem* OnlineSub = IOnlineSubsystem::Get();
	if (OnlineSub)
	{
		IOnlineSessionPtr Sessions = OnlineSub->GetSessionInterface();
		if (Sessions.IsValid())
		{
			// 拿到当前正在运行的房间 (NAME_GameSession)
			FNamedOnlineSession* Session = Sessions->GetNamedSession(NAME_GameSession);
			if (Session)
			{
				// 计算当前房间里的绝对总人数（红队 + 蓝队，包含了真人和 AI）
				int32 CurrentTotalPlayers = RedTeamNames.Num() + BlueTeamNames.Num();
				
				// 覆写那个名为 TOTAL_PLAYERS_WITH_AI 的标签！
				Session->SessionSettings.Set(FName("TOTAL_PLAYERS_WITH_AI"), CurrentTotalPlayers, EOnlineDataAdvertisementType::ViaOnlineServiceAndPing);
				
				// 提交更新！这一步会让局域网里的其他玩家立刻搜到新的人数！
				Sessions->UpdateSession(NAME_GameSession, Session->SessionSettings, true);
			}
		}
	}

	// 2. 遍历当前世界（房间）里的所有玩家控制器
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		ARoomPlayerController* PC = Cast<ARoomPlayerController>(It->Get());
		if (PC)
		{
			// 呼叫对讲机的 Client RPC，把服务器的名单和房主名字硬塞给他们
			PC->Client_UpdateRoomUI(RedTeamNames, BlueTeamNames, HostName);
            
			// 【顺带补上】：名单刷新后，紧接着把字典里保存的所有人的准备状态重新刷一遍！
			for (const auto& Pair : PlayerReadyStates)
			{
				PC->Client_UpdatePlayerReadyState(Pair.Key, Pair.Value);
			}
		}
	}
}

void ARoomGameMode::ChangePlayerTeam(const FString& PlayerName, bool bToRedTeam)
{
	// 1. 简单粗暴：先把这个玩家从两个队伍里都踢出去（防止分身）
	RedTeamNames.Remove(PlayerName);
	BlueTeamNames.Remove(PlayerName);

	// 2. 根据他的请求，把他加进对应的队伍
	if (bToRedTeam)
	{
		RedTeamNames.AddUnique(PlayerName); // AddUnique 防止重复添加
	}
	else
	{
		BlueTeamNames.AddUnique(PlayerName);
	}

	// 3. 名单发生变化，立刻广播给全房间的所有人！
	BroadcastRoomUpdate();
}

void ARoomGameMode::RemovePlayerFromRoom(const FString& PlayerName)
{
	// 1. 无脑从两个队伍里把这个名字删掉
	RedTeamNames.Remove(PlayerName);
	BlueTeamNames.Remove(PlayerName);
	
	// 【新增】：向全服播报绿字提示！
	BroadcastSystemMessage(FString::Printf(TEXT("玩家【%s】退出了房间"), *PlayerName));

	// 2. 广播给房间里剩下的人，让他们刷新 UI（这步极其关键，否则别人屏幕上还有你）
	BroadcastRoomUpdate();
	
}

void ARoomGameMode::BroadcastChatMessage(const FString& SenderName, const FString& Message)
{
	// 1. 鉴定谁是房主？(服务器上排在第一个的本地玩家就是房主)
	FString HostName = TEXT("");
	if (ARoomPlayerController* HostPC = Cast<ARoomPlayerController>(GetWorld()->GetFirstPlayerController()))
	{
		HostName = HostPC->MyPlayerName;
	}

	// 对比一下，发消息的这个人是不是房主？
	bool bIsHost = (SenderName == HostName);

	// 2. 拿着大喇叭，给房间里所有的对讲机下达指令！
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (ARoomPlayerController* PC = Cast<ARoomPlayerController>(It->Get()))
		{
			// 触发所有人的 Client RPC
			PC->Client_ReceiveChatMessage(SenderName, bIsHost, Message, false);
		}
	}
}

void ARoomGameMode::BroadcastSystemMessage(const FString& Message)
{
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (ARoomPlayerController* PC = Cast<ARoomPlayerController>(It->Get()))
		{
			// 系统消息：没有发送人，bIsHost为false，最末尾的 bIsSystemMsg 为 true！
			PC->Client_ReceiveChatMessage(TEXT(""), false, Message, true);
		}
	}
}

// ----------------------------------------------------
// 【新增】：给 AI 发放唯一身份证并加入名单
// ----------------------------------------------------
void ARoomGameMode::AddAIToRoom(bool bToRedTeam, const FString& CharacterName, int32 Count)
{
	for (int32 i = 0; i < Count; ++i)
	{
		// 核心防同名魔法：给 AI 名字加上自增编号！
		FString UniqueAIName = FString::Printf(TEXT("[AI] %s_%d"), *CharacterName, AINextID);
		
		// 编号自增，确保下一个绝对不重名
		AINextID++;

		// 塞进对应的队伍数组里
		if (bToRedTeam)
		{
			RedTeamNames.AddUnique(UniqueAIName);
		}
		else
		{
			BlueTeamNames.AddUnique(UniqueAIName);
		}
	}

	// 【体验优化】：向全服播报绿字提示，告诉大家房主加了几个 AI！
	FString TeamStr = bToRedTeam ? TEXT("红队") : TEXT("蓝队");
	BroadcastSystemMessage(FString::Printf(TEXT("房主向【%s】部署了 %d 名 AI 士兵 [%s]"), *TeamStr, Count, *CharacterName));

	// 【核心修复】：名字必须和你上面写好的广播函数一模一样！
	BroadcastRoomUpdate();
}

// ----------------------------------------------------
// 【终极广播核心】：强行让所有客户端的 UI 和服务器的数组保持一致！
// ----------------------------------------------------
void ARoomGameMode::BroadcastRoomUIUpdate()
{
	// ==========================================
	// 1. 必须在最前面声明并获取 HostName！
	// ==========================================
	FString HostName = TEXT("");
	if (ARoomPlayerController* HostPC = Cast<ARoomPlayerController>(GetWorld()->GetFirstPlayerController()))
	{
		HostName = HostPC->MyPlayerName;
	}

	// ==========================================
	// 2. 然后才能在下面的循环里使用 HostName！
	// ==========================================
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		ARoomPlayerController* PC = Cast<ARoomPlayerController>(It->Get());
		if (PC)
		{
			// 此时编译器已经认识 HostName 了，绝对不会再报 C2065！
			PC->Client_UpdateRoomUI(RedTeamNames, BlueTeamNames, HostName);
			
			// 紧接着把字典里保存的所有人的准备状态重新刷一遍！
			for (const auto& Pair : PlayerReadyStates)
			{
				PC->Client_UpdatePlayerReadyState(Pair.Key, Pair.Value);
			}
		}
	}
}

void ARoomGameMode::UpdatePlayerReadyState(const FString& PlayerName, bool bIsReady)
{
	// 写入服务器的记忆字典里
	PlayerReadyStates.Add(PlayerName, bIsReady);

	// 全频道广播！通知所有人的 UI 更新这个人！
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (ARoomPlayerController* PC = Cast<ARoomPlayerController>(It->Get()))
		{
			PC->Client_UpdatePlayerReadyState(PlayerName, bIsReady);
		}
	}
}