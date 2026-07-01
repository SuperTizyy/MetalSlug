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

// ==========================================
// 【大厂 P0 修复 2026.07.03】测试房主模式
// ==========================================

/**
 * URoomService::EnterSkipToHostMode
 *
 * 显式 API: 同步把本机标记为"独立进程房主", 用于"勾选跳过登录"测试场景
 *
 * 行为:
 *   1. 幂等: 若已是 Host 直接返回
 *   2. 设 bIsHost = true + 广播 OnHostChanged(true)
 *   3. 广播 OnPlayerJoined(LocalAccountName) — 触发本机玩家标签显示
 *   4. 服务器 (Authority) 同步 GameState->HostPlayerName = 本机账号
 *      → 客户端 OnRep_HostPlayerName 自动触发 (在 LAN Room 模式)
 *
 * 设计动机:
 *   旧架构"勾选跳过登录"只调 MockLoginForTesting + TransitToState(MainLobby),
 *   但 URoomService::bIsHost 永远为 false (没人调 NotifyBecameHost),
 *   导致 RoomInsidePage 永远把本机当成普通玩家, 房主按钮全 Collapsed。
 *   新架构用显式 API 标 Host, 复用了所有下游 UI 的房主识别路径,
 *   业务流自洽, 不再依赖隐式副作用。
 */
void URoomService::EnterSkipToHostMode()
{
	// 幂等保护: 已是 Host 不重复广播
	if (bIsHost)
	{
		UE_LOG(LogTemp, Log,
			TEXT("[RoomService] EnterSkipToHostMode: 已是 Host, 跳过重复标定 (LocalAccount=%s)"),
			*GetCurrentAccountName());
		return;
	}

	const FString LocalAccountName = GetCurrentAccountName();
	if (LocalAccountName.IsEmpty())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[RoomService] EnterSkipToHostMode: LocalAccountName 为空! 请确保先调 MockLoginForTesting"));
		// 即便为空, 仍标 Host, 后续 RefreshRoomUI 会兜底
	}

	// 1. 标 Host + 广播
	bIsHost = true;
	OnHostChanged.Broadcast(true);
	UE_LOG(LogTemp, Log,
		TEXT("[RoomService] EnterSkipToHostMode: 本机已成为测试房主 (LocalAccount=%s)"),
		*LocalAccountName);

	// 2. 广播本机加入 — 触发本机玩家标签立即显示 (走订阅者, 不依赖 PlayerState 同步时延)
	OnPlayerJoined.Broadcast(LocalAccountName);

	// 3. 服务器权威: 同步 GameState->HostPlayerName, 让 LAN Room 模式 OnRep 链路也能工作
	if (UWorld* World = GetWorld())
	{
		if (ARoomGameState* GS = World->GetGameState<ARoomGameState>())
		{
			// 仅当 PC 有 Authority (PIE ListenServer 或独立进程) 才写 HostPlayerName
			if (APlayerController* PC = World->GetFirstPlayerController())
			{
				if (PC->HasAuthority() && !LocalAccountName.IsEmpty())
				{
					GS->HostPlayerName = LocalAccountName;
					UE_LOG(LogTemp, Log,
						TEXT("[RoomService] EnterSkipToHostMode: 已同步 GameState->HostPlayerName=%s"),
						*LocalAccountName);
				}
			}
		}
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
