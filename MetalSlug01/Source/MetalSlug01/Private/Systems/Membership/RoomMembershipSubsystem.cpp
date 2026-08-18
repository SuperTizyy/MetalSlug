// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file RoomMembershipSubsystem.cpp
 * @brief 房间成员管理子系统实现 — 玩家/AI 入队/换队/踢人
 */
#include "Systems/Membership/RoomMembershipSubsystem.h"
#include "Systems/RoomGameMode.h"
#include "Systems/RoomGameState.h"
#include "Systems/Core/RoomPlayerState.h"
#include "Systems/LoginPlayerController.h"
#include "Systems/RoomPlayerController.h"
#include "Services/RoomService.h"
#include "Data/Faction/FactionTags.h"
#include "Characters/BaseCharacter.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"

// ==========================================
// UWorldSubsystem 基础
// ==========================================

URoomMembershipSubsystem* URoomMembershipSubsystem::Get(const UObject* WorldContextObject)
{
	if (!WorldContextObject) return nullptr;
	if (UWorld* World = WorldContextObject->GetWorld())
	{
		return World->GetSubsystem<URoomMembershipSubsystem>();
	}
	return nullptr;
}

/**
 * @brief 子系统创建守卫 — Server-only(仅服务器端创建)
 *
 * 客户端不创建 Membership Subsystem — 成员管理是 GameMode 职责
 * 镜像 v31.5 风格: NetMode != NM_Client 才允许创建
 */
bool URoomMembershipSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (UWorld* World = Cast<UWorld>(Outer))
	{
		return World->GetNetMode() != NM_Client;
	}
	return false;
}

// ==========================================
// 玩家管理 (v31.5 重构 — 修复 PlayerStateClass 字段缺失 bug)
// ==========================================

void URoomMembershipSubsystem::UpdatePlayerReadyState(AController* RequestingController, bool bIsReady)
{
	if (ARoomPlayerState* PS = RequestingController->GetPlayerState<ARoomPlayerState>())
	{
		PS->bIsReady = bIsReady;
		PS->OnRep_IsReady();
	}
}

/**
 * @brief 检查除房主外所有玩家是否都已准备(用于开局门控)
 * @return true=所有非房主玩家 bIsReady=true(或无有效阵营), false=存在未准备玩家
 *
 * 房主豁免 — 房主不需要点准备也能开局(业务规则)
 * 无有效阵营玩家也视为已准备(防止空槽位永远卡门控)
 * 任何非准备玩家 → 立即 Log Warning + return false
 */
bool URoomMembershipSubsystem::CheckAllPlayersReady()
{
	ARoomGameState* GS = GetWorld()->GetGameState<ARoomGameState>();
	if (!GS || GS->PlayerArray.Num() == 0) return false;

	APlayerController* HostPC = Cast<APlayerController>(GetWorld()->GetFirstPlayerController());

	for (APlayerState* GenericPS : GS->PlayerArray)
	{
		if (ARoomPlayerState* PS = Cast<ARoomPlayerState>(GenericPS))
		{
			if (PS->GetPlayerController() == HostPC) continue; // 房主豁免

			if (PS->CurrentFactionTag.IsValid() && !PS->bIsReady)
			{
				UE_LOG(LogTemp, Warning, TEXT("[RoomMembership] 拦截开局: 玩家 '%s' 未准备!"), *PS->GetPlayerName());
				return false;
			}
		}
	}
	return true;
}

/**
 * AddPlayerToRoom - v31.5 大厂重构
 *
 * 修复历史:
 *   - v29.7: 显式 Spawn PS (测试模式绕开 PostLogin)
 *   - v31.5: 移除 cpp 中对"自身不存在的 PlayerStateClass 字段"的引用
 *            改为函数参数传入 — 调用方必须显式提供 PlayerStateClass
 *
 * @param PlayerStateClass ARoomPlayerState 派生类 (用于显式 Spawn)
 *                         调用方必须从 GameMode.PlayerStateClass 传入
 *                         单一真理源: GameMode.PlayerStateClass → Subsystem 函数参数
 */
void URoomMembershipSubsystem::AddPlayerToRoom(AController* RequestingController, const FString& PlayerName,
	TSubclassOf<APlayerState> PlayerStateClass)
{
	if (!RequestingController)
	{
		UE_LOG(LogTemp, Error, TEXT("[RoomMembership] AddPlayerToRoom: RequestingController is null"));
		return;
	}

	if (!PlayerStateClass)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[RoomMembership] AddPlayerToRoom: PlayerStateClass 参数为 null. "
				 "调用方必须从 ARoomGameMode::PlayerStateClass 传入. "
				 "请在 UE 编辑器打开 BP GM_RoomGameMode → Class Defaults → Player State Class = ARoomPlayerState (或其 BP 派生类)."));
		return;
	}

	if (!PlayerStateClass->IsChildOf(ARoomPlayerState::StaticClass()))
	{
		UE_LOG(LogTemp, Error,
			TEXT("[RoomMembership] AddPlayerToRoom: PlayerStateClass='%s' 不是 ARoomPlayerState 派生. 拒绝."),
			*PlayerStateClass->GetName());
		return;
	}

	ARoomGameState* GS = GetWorld()->GetGameState<ARoomGameState>();
	if (!GS)
	{
		UE_LOG(LogTemp, Error, TEXT("[RoomMembership] AddPlayerToRoom: GS is null"));
		return;
	}

	// 谁第一个进房间（房主建房时），谁的名字就刻在 GameState 上
	if (GS->HostPlayerName.IsEmpty())
	{
		GS->HostPlayerName = PlayerName;
	}

	// 【v29 大厂架构】显式 Spawn PS (测试模式绕开 PostLogin)
	ARoomPlayerState* PS = RequestingController->GetPlayerState<ARoomPlayerState>();
	if (!PS)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[RoomMembership] AddPlayerToRoom: Controller '%s' 没有 PlayerState — 显式 Spawn"),
			*RequestingController->GetName());

		FActorSpawnParameters SpawnParams;
		SpawnParams.Owner = RequestingController;
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

		PS = GetWorld()->SpawnActor<ARoomPlayerState>(
			PlayerStateClass, FTransform::Identity, SpawnParams);
		if (!PS)
		{
			UE_LOG(LogTemp, Error,
				TEXT("[RoomMembership] AddPlayerToRoom: SpawnActor<%s> 失败, PlayerName=%s"),
				*PlayerStateClass->GetName(), *PlayerName);
			return;
		}
		RequestingController->SetPlayerState(PS);
	}

	GS->PlayerArray.AddUnique(PS);
	PS->SetPlayerName(PlayerName);

	// 【v29.7 大厂原则 - 彻底移除 auto-balance】
	if (PS->bHasExplicitlyChosenTeam)
	{
		UE_LOG(LogTemp, Log,
			TEXT("[RoomMembership] AddPlayerToRoom: 玩家 '%s' 已显式选阵营 '%s'"),
			*PlayerName, *PS->CurrentFactionTag.ToString());
	}
	else
	{
		UE_LOG(LogTemp, Log,
			TEXT("[RoomMembership] AddPlayerToRoom: 玩家 '%s' 未显式选阵营, 保持默认 '%s'"),
			*PlayerName, *PS->CurrentFactionTag.ToString());
	}

	PS->OnRep_FactionTag();
	BroadcastSystemMessage(FString::Printf(TEXT("玩家【%s】加入了房间"), *PlayerName));
	URoomService::BroadcastPlayerJoined(GetWorld(), PlayerName);
}

void URoomMembershipSubsystem::RemovePlayerFromRoom(AController* RequestingController)
{
	if (!RequestingController) return;

	FString LeftPlayerName;
	if (ARoomPlayerState* PS = RequestingController->GetPlayerState<ARoomPlayerState>())
	{
		LeftPlayerName = PS->GetPlayerName();
		BroadcastSystemMessage(FString::Printf(TEXT("玩家【%s】退出了房间"), *LeftPlayerName));
	}

	if (!LeftPlayerName.IsEmpty())
	{
		URoomService::BroadcastPlayerLeft(GetWorld(), LeftPlayerName);
	}

	ARoomGameState* GS = GetWorld()->GetGameState<ARoomGameState>();
	if (GS && !GS->HostPlayerName.IsEmpty() && GS->HostPlayerName.Equals(LeftPlayerName, ESearchCase::IgnoreCase))
	{
		UE_LOG(LogTemp, Log, TEXT("[RoomMembership] 房主 [%s] 离房, 自动转交房主权限"), *LeftPlayerName);
		TransferHostTo(TEXT(""));
	}
}

void URoomMembershipSubsystem::ChangePlayerTeam(AController* RequestingController, bool bToAttackTeam)
{
	if (!RequestingController)
	{
		UE_LOG(LogTemp, Error, TEXT("[RoomMembership] ChangePlayerTeam: RequestingController is null"));
		return;
	}

	ARoomPlayerState* PS = RequestingController->GetPlayerState<ARoomPlayerState>();
	if (!PS)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[RoomMembership] ChangePlayerTeam: Controller '%s' 没有 ARoomPlayerState"),
			*RequestingController->GetName());
		return;
	}

	const FGameplayTag NewFactionTag = bToAttackTeam ? FFactionTags::Offense() : FFactionTags::Defense();
	const FGameplayTag OldFactionTag = PS->CurrentFactionTag;

	if (PS->CurrentFactionTag == NewFactionTag)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[RoomMembership] ChangePlayerTeam: 玩家 '%s' 已经在 '%s'"),
			*RequestingController->GetName(), *NewFactionTag.ToString());
		return;
	}

	PS->CurrentFactionTag = NewFactionTag;
	PS->Server_MarkTeamExplicitlyChosen();

	// Pawn.FactionTag 必须同步 (AIPerception 读它)
	if (ABaseCharacter* PlayerChar = Cast<ABaseCharacter>(PS->GetPawn()))
	{
		PlayerChar->SyncFactionTagFromController(RequestingController);
	}

	PS->OnRep_FactionTag();
}

/**
 * @brief 转让房主权限给指定玩家(或自动选举下一任)
 * @param NewHostPlayerName 目标玩家名, 空字符串表示自动选下一任
 * @return true=转让成功, false=找不到候选(避免房主被清空)
 *
 * 自动选举规则: NewHostPlayerName 空 → 选 PlayerArray 中除当前房主外第一个非空名字玩家
 * 失败兜底: 找不到候选 → Log Warning + return false, 保留旧房主(防止 HostPlayerName 被清空)
 * 成功路径: 更新 GS.HostPlayerName + 广播系统消息 + 触发 HostChanged 事件
 */
bool URoomMembershipSubsystem::TransferHostTo(const FString& NewHostPlayerName)
{
	ARoomGameState* GS = GetWorld()->GetGameState<ARoomGameState>();
	if (!GS)
	{
		UE_LOG(LogTemp, Error, TEXT("[RoomMembership] TransferHostTo: GS is null"));
		return false;
	}

	const FString OldHostName = GS->HostPlayerName;
	FString CurrentHostName = TEXT("");
	if (ARoomPlayerController* HostPC = Cast<ARoomPlayerController>(GetWorld()->GetFirstPlayerController()))
	{
		CurrentHostName = HostPC->MyPlayerName;
	}
	FString TargetHost = NewHostPlayerName;

	if (TargetHost.IsEmpty())
	{
		// 自动选下一个玩家 (除房主外第一个 PlayerArray 成员)
		for (APlayerState* GenericPS : GS->PlayerArray)
		{
			if (ARoomPlayerState* PS = Cast<ARoomPlayerState>(GenericPS))
			{
				FString PSName = PS->GetPlayerName();
				if (!PSName.IsEmpty() && !PSName.Equals(OldHostName, ESearchCase::IgnoreCase))
				{
					TargetHost = PSName;
					break;
				}
			}
		}
	}

	if (TargetHost.IsEmpty())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[RoomMembership] TransferHostTo: 找不到候选房主 (OldHost='%s'), 拒绝清空"),
			*OldHostName);
		return false;
	}

	GS->HostPlayerName = TargetHost;
	BroadcastSystemMessage(FString::Printf(TEXT("房主权限已转交给【%s】"), *TargetHost));
	URoomService::BroadcastHostChanged(GetWorld(), TargetHost.Equals(CurrentHostName, ESearchCase::IgnoreCase));
	return true;
}

/**
 * @brief 广播聊天消息到所有房间玩家 PC(房间内聊天)
 * @param SenderName 发送者玩家名
 * @param Message 聊天内容
 *
 * 自动判定发送者是否为房主, 给 Client_ReceiveChatMessage 传 bIsHost 标志
 * 所有 ARoomPlayerController 收到 Client_ReceiveChatMessage, 由其 UI 层负责展示
 */
void URoomMembershipSubsystem::BroadcastChatMessage(const FString& SenderName, const FString& Message)
{
	FString HostName = TEXT("");
	if (ARoomPlayerController* HostPC = Cast<ARoomPlayerController>(GetWorld()->GetFirstPlayerController()))
	{
		HostName = HostPC->MyPlayerName;
	}

	const bool bIsHost = (SenderName == HostName);

	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (ARoomPlayerController* PC = Cast<ARoomPlayerController>(It->Get()))
		{
			PC->Client_ReceiveChatMessage(SenderName, bIsHost, Message, false);
		}
	}
}

void URoomMembershipSubsystem::BroadcastSystemMessage(const FString& Message)
{
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (ARoomPlayerController* PC = Cast<ARoomPlayerController>(It->Get()))
		{
			PC->Client_ReceiveChatMessage(TEXT(""), false, Message, true);
		}
	}
}