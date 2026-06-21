// 版权声明：在项目设置的描述页面填写您的版权信息。

#include "Services/RoomService.h"
#include "Systems/RoomPlayerController.h"
#include "Systems/RoomGameMode.h"
#include "Systems/RoomGameState.h"
#include "Systems/Core/RoomPlayerState.h"
#include "Systems/Account/AccountSubsystem.h"
#include "Services/UIViewService.h"
#include "Systems/Session/SessionManagerSubsystem.h"
#include "GameFramework/PlayerController.h"
#include "Engine/World.h"
#include "Engine/GameInstance.h"

// ==========================================
// 静态访问器
// ==========================================

URoomService* URoomService::Get(const UObject* WorldContextObject)
{
	if (!WorldContextObject) return nullptr;
	UWorld* World = WorldContextObject->GetWorld();
	if (!World) return nullptr;
	UGameInstance* GI = World->GetGameInstance();
	if (!GI) return nullptr;
	return GI->GetSubsystem<URoomService>();
}

// ==========================================
// 内部路由
// ==========================================

APlayerController* URoomService::GetEffectivePC() const
{
	UWorld* World = GetWorld();
	if (!World) return nullptr;
	return World->GetFirstPlayerController();
}

ARoomGameMode* URoomService::GetRoomGameMode() const
{
	UWorld* World = GetWorld();
	if (!World) return nullptr;
	return Cast<ARoomGameMode>(World->GetAuthGameMode());
}

FString URoomService::GetCurrentAccountName() const
{
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UAccountSubsystem* AccountSub = GI->GetSubsystem<UAccountSubsystem>())
		{
			return AccountSub->GetCurrentLoggedInUser();
		}
	}
	return TEXT("");
}

// ==========================================
// 公共 API（标准联机模式优先 RPC，独立进程模式走 GameMode）
// ==========================================

void URoomService::RequestChangeTeam(bool bToAttackTeam)
{
	if (ARoomPlayerController* PC = Cast<ARoomPlayerController>(GetEffectivePC()))
	{
		PC->Server_RequestChangeTeam(bToAttackTeam);
		return;
	}
	// 独立进程模式：直接调 GameMode
	if (ARoomGameMode* GM = GetRoomGameMode())
	{
		GM->ChangePlayerTeam(GetEffectivePC(), bToAttackTeam);
	}
}

void URoomService::RequestReady(bool bIsReady)
{
	if (ARoomPlayerController* PC = Cast<ARoomPlayerController>(GetEffectivePC()))
	{
		PC->Server_ToggleReady(bIsReady);
		return;
	}
	if (ARoomGameMode* GM = GetRoomGameMode())
	{
		GM->UpdatePlayerReadyState(GetEffectivePC(), bIsReady);
	}
}

void URoomService::RequestSendChatMessage(const FString& Message)
{
	if (Message.IsEmpty()) return;

	if (ARoomPlayerController* PC = Cast<ARoomPlayerController>(GetEffectivePC()))
	{
		PC->Server_SendChatMessage(Message);
		return;
	}
	// 独立进程模式：直接调 GameMode 广播
	if (ARoomGameMode* GM = GetRoomGameMode())
	{
		GM->BroadcastChatMessage(GetCurrentAccountName(), Message);
	}
}

void URoomService::RequestAddAI(bool bToAttackTeam, const FString& CharacterName, int32 Count)
{
	if (ARoomPlayerController* PC = Cast<ARoomPlayerController>(GetEffectivePC()))
	{
		PC->Server_AddAI(bToAttackTeam, CharacterName, Count);
		return;
	}
	if (ARoomGameMode* GM = GetRoomGameMode())
	{
		GM->AddAIToRoom(bToAttackTeam, CharacterName, Count);
	}
}

void URoomService::RequestSelectLoadout(const FString& CharacterRowName, const FString& Weapon1RowName, const FString& Weapon2RowName)
{
	if (ARoomPlayerController* PC = Cast<ARoomPlayerController>(GetEffectivePC()))
	{
		PC->Server_SelectLoadout(CharacterRowName, Weapon1RowName, Weapon2RowName);
		return;
	}
	// 独立进程模式：直接写 PlayerState
	if (APlayerController* PC = GetEffectivePC())
	{
		if (ARoomPlayerState* PS = PC->GetPlayerState<ARoomPlayerState>())
		{
			PS->SetPlayerLoadout(CharacterRowName, Weapon1RowName, Weapon2RowName);
		}
	}
}

void URoomService::RequestStartGame()
{
	if (ARoomPlayerController* PC = Cast<ARoomPlayerController>(GetEffectivePC()))
	{
		PC->Server_RequestStartGame();
		return;
	}
	// 独立进程模式：直接调 GameMode
	if (APlayerController* PC = GetEffectivePC())
	{
		if (PC->HasAuthority())
		{
			if (ARoomGameMode* GM = GetRoomGameMode())
			{
				GM->RequestStartGame(PC);
			}
		}
	}
}

void URoomService::RequestLeaveRoom()
{
	// 【大厂架构】委托 SessionManagerSubsystem 销毁 Session（Service 不直接调 OnlineSubsystem）
	if (UGameInstance* GI = GetGameInstance())
	{
		if (USessionManagerSubsystem* SessionManager = GI->GetSubsystem<USessionManagerSubsystem>())
		{
			SessionManager->DestroyRoom(FOnDestroyRoomComplete());
		}
	}

	// 状态切换交给 LANRoomPresenter 监听 SessionManager 的 OnDestroyRoomComplete 完成回调
	// Service 不应自己跨层调 GameFlowSubsystem
}

// ==========================================
// 【P0 架构升级】身份同步 + 事件广播
// ==========================================

void URoomService::NotifyBecameHost()
{
	if (!bIsHost)
	{
		bIsHost = true;
		// 【P0】身份变化主动广播, 替代 View 定时器轮询
		OnHostChanged.Broadcast(true);
	}
}

void URoomService::NotifyBecameClient()
{
	if (bIsHost)
	{
		bIsHost = false;
		OnHostChanged.Broadcast(false);
	}
}

void URoomService::BroadcastHostChanged(const UObject* WorldContextObject, bool bIsHostNow)
{
	if (URoomService* Service = Get(WorldContextObject))
	{
		if (Service->bIsHost != bIsHostNow)
		{
			Service->bIsHost = bIsHostNow;
			Service->OnHostChanged.Broadcast(bIsHostNow);
		}
	}
}

void URoomService::BroadcastPlayerJoined(const UObject* WorldContextObject, const FString& PlayerName)
{
	if (URoomService* Service = Get(WorldContextObject))
	{
		Service->OnPlayerJoined.Broadcast(PlayerName);
	}
}

void URoomService::BroadcastPlayerLeft(const UObject* WorldContextObject, const FString& PlayerName)
{
	if (URoomService* Service = Get(WorldContextObject))
	{
		Service->OnPlayerLeft.Broadcast(PlayerName);
	}
}
