#include "Systems/RoomGameState.h"

#include "Net/UnrealNetwork.h"
#include "UI/Login/Core/RoomPlayerState.h"
#include "Characters/BaseCharacter.h"

ARoomGameState::ARoomGameState()
{
	// 确保GameState本身开启同步
	bReplicates = true;
}

TArray<ARoomPlayerState*> ARoomGameState::GetPlayersInTeam(ERoomTeam TargetTeam) const
{
	TArray<ARoomPlayerState*> TeamMembers;

	// 【架构规范】：直接利用引擎底层的 PlayerArray，永远不会出现名单不一致的问题
	for (APlayerState* PS : PlayerArray)
	{
		if (ARoomPlayerState* RoomPS = Cast<ARoomPlayerState>(PS))
		{
			if (RoomPS->CurrentTeam == TargetTeam)
			{
				TeamMembers.Add(RoomPS);
			}
		}
	}

	return TeamMembers;
}

ARoomPlayerState* ARoomGameState::GetTeamTopACPlayer(ERoomTeam TargetTeam) const
{
	ARoomPlayerState* TopPlayer = nullptr;
	int32 TopAC = -1;

	for (APlayerState* PS : PlayerArray)
	{
		if (ARoomPlayerState* RoomPS = Cast<ARoomPlayerState>(PS))
		{
			if (RoomPS->CurrentTeam != TargetTeam)
			{
				continue;
			}

			ABaseCharacter* Char = Cast<ABaseCharacter>(RoomPS->GetPawn());
			if (!Char || Char->GetIsDead())
			{
				continue;
			}

			if (Char->GetAC() > TopAC)
			{
				TopAC = Char->GetAC();
				TopPlayer = RoomPS;
			}
		}
	}

	return TopPlayer;
}

ARoomPlayerState* ARoomGameState::GetOverallTopACPlayer() const
{
	ARoomPlayerState* TopPlayer = nullptr;
	int32 TopAC = -1;

	for (APlayerState* PS : PlayerArray)
	{
		if (ARoomPlayerState* RoomPS = Cast<ARoomPlayerState>(PS))
		{
			ABaseCharacter* Char = Cast<ABaseCharacter>(RoomPS->GetPawn());
			if (!Char || Char->GetIsDead())
			{
				continue;
			}

			if (Char->GetAC() > TopAC)
			{
				TopAC = Char->GetAC();
				TopPlayer = RoomPS;
			}
		}
	}

	return TopPlayer;
}

void ARoomGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// 注册同步变量
	DOREPLIFETIME(ARoomGameState, CurrentMatchMode);
	DOREPLIFETIME(ARoomGameState, MatchEndTime);
	DOREPLIFETIME(ARoomGameState, HostPlayerName);
	DOREPLIFETIME(ARoomGameState, CurrentRound);
	DOREPLIFETIME(ARoomGameState, AttackerTotalKills);
	DOREPLIFETIME(ARoomGameState, DefenderTotalKills);
	DOREPLIFETIME(ARoomGameState, AttackerWins);
	DOREPLIFETIME(ARoomGameState, DefenderWins);
}

void ARoomGameState::OnRep_CurrentRound()
{
	OnCurrentRoundUpdated.Broadcast(CurrentRound);
}

int32 ARoomGameState::GetMatchRemainingSeconds() const
{
	// 如果尚未设置有效时间戳，直接返回 0
	if (MatchEndTime <= 0.0f)
	{
		return 0;
	}

	// 使用内置的获取服务器预估世界时间的方法，自动消除客户端与服务器端的时间差
	float CurrentServerTime = GetServerWorldTimeSeconds();
	
	// 计算剩余秒数并钳制到0以上，避免出现负数倒计时
	int32 RemainingSeconds = FMath::Max(0, FMath::RoundToInt(MatchEndTime - CurrentServerTime));
	return RemainingSeconds;
}

void ARoomGameState::OnRep_TeamKillCount()
{
	FString NetModeStr;
	switch (GetNetMode())
	{
	case NM_Standalone: NetModeStr = TEXT("Standalone"); break;
	case NM_ListenServer: NetModeStr = TEXT("ListenServer"); break;
	case NM_DedicatedServer: NetModeStr = TEXT("DedicatedServer"); break;
	case NM_Client: NetModeStr = TEXT("Client"); break;
	default: NetModeStr = TEXT("Unknown"); break;
	}
	UE_LOG(LogTemp, Log, TEXT("[RoomGameState] OnRep_TeamKillCount 触发！NetMode=%s, AttackerTotalKills=%d, DefenderTotalKills=%d"),
		*NetModeStr, AttackerTotalKills, DefenderTotalKills);
	OnTeamKillCountUpdated.Broadcast(AttackerTotalKills, DefenderTotalKills);
}

void ARoomGameState::AddTeamKill(ERoomTeam Team)
{
	// 仅在服务器端执行
	if (!HasAuthority())
	{
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("[RoomGameState] AddTeamKill 被调用！Team=%d, Before: AttackerTotalKills=%d, DefenderTotalKills=%d"),
		(int32)Team, AttackerTotalKills, DefenderTotalKills);

	if (Team == ERoomTeam::Attack)
	{
		AttackerTotalKills++;
	}
	else if (Team == ERoomTeam::Defense)
	{
		DefenderTotalKills++;
	}

	UE_LOG(LogTemp, Log, TEXT("[RoomGameState] AddTeamKill 执行完毕！After: AttackerTotalKills=%d, DefenderTotalKills=%d, IsNetModeServer=%d, IsNetModeRemote=%d"),
		AttackerTotalKills, DefenderTotalKills, (int32)GetNetMode() == NM_DedicatedServer, (int32)GetNetMode() == NM_Client);

	// 【核心修复】：强制广播给所有客户端（包括 Listen Server 主机自身）
	// 原因：OnRep_TeamKillCount 在 Listen Server 本地不会触发，导致房主 UI 永远不更新
	// NetMulticast 从服务器向所有连接的客户端广播，确保每个客户端都能收到击杀数刷新通知
	MulticastRefreshKillCount(AttackerTotalKills, DefenderTotalKills);
}

void ARoomGameState::MulticastRefreshKillCount_Implementation(int32 AttackerKills, int32 DefenderKills)
{
	UE_LOG(LogTemp, Log, TEXT("[RoomGameState] MulticastRefreshKillCount 被调用！AttackerKills=%d, DefenderKills=%d"), AttackerKills, DefenderKills);
	OnTeamKillCountUpdated.Broadcast(AttackerKills, DefenderKills);
}

void ARoomGameState::ResetTeamKillStats()
{
	// 仅在服务器端执行
	if (!HasAuthority())
	{
		return;
	}

	AttackerTotalKills = 0;
	DefenderTotalKills = 0;
}

void ARoomGameState::OnRep_WinStats()
{
	UE_LOG(LogTemp, Log, TEXT("[RoomGameState] OnRep_WinStats 触发！AttackerWins=%d, DefenderWins=%d"),
		AttackerWins, DefenderWins);
	OnWinStatsUpdated.Broadcast(AttackerWins, DefenderWins);
}

void ARoomGameState::TriggerSettlement()
{
	// 仅在服务器端执行
	if (!HasAuthority())
	{
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("[RoomGameState] TriggerSettlement: 倒计时结束，开始结算！AttackerKills=%d, DefenderKills=%d"),
		AttackerTotalKills, DefenderTotalKills);

	// 步骤1：判断当局胜负，累加胜局数
	if (AttackerTotalKills > DefenderTotalKills)
	{
		AttackerWins++;
		UE_LOG(LogTemp, Log, TEXT("[RoomGameState] 攻方获胜！AttackerWins=%d"), AttackerWins);
	}
	else if (DefenderTotalKills > AttackerTotalKills)
	{
		DefenderWins++;
		UE_LOG(LogTemp, Log, TEXT("[RoomGameState] 守方获胜！DefenderWins=%d"), DefenderWins);
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("[RoomGameState] 当局平局，双方均不得分。"));
	}

	// 步骤2：立刻广播"进入结算状态"事件，让所有客户端显示比分面板
	// 【网络架构修复】：使用 NetMulticast 替代原有的直接 Broadcast
	// 原因：纯客户端进程的 OnEnterSettlement.Broadcast() 不会触发，导致 Text_GameOver 不显示
	MulticastEnterSettlement(AttackerTotalKills, DefenderTotalKills);

	// 步骤3：通过 World Timer 延迟3秒，然后广播"显示最终结果"事件
	UWorld* World = GetWorld();
	if (World)
	{
		// 先清除可能存在的旧定时器（防止重复触发）
		World->GetTimerManager().ClearTimer(SettlementTimerHandle);
		World->GetTimerManager().SetTimer(SettlementTimerHandle, this, &ARoomGameState::BroadcastFinalSettlement, 3.0f, false);
	}
}

void ARoomGameState::BroadcastFinalSettlement()
{
	// 【网络架构修复】：使用 NetMulticast 替代原有的 HasAuthority + Broadcast 方案
	// 原问题：在 Listen Server 中，纯客户端进程的 HasAuthority() 返回 false，导致 OnShowFinalSettlement 从未广播给房主以外的玩家
	// 解决方案：NetMulticast RPC 在服务器端调用时，引擎自动将函数调用复制到所有连接的客户端
	MulticastShowFinalSettlement(AttackerWins, DefenderWins);
}

void ARoomGameState::MulticastShowFinalSettlement_Implementation(int32 InAttackerWins, int32 InDefenderWins)
{
	UE_LOG(LogTemp, Log, TEXT("[RoomGameState] MulticastShowFinalSettlement: 攻方胜%d局, 守方胜%d局"), InAttackerWins, InDefenderWins);

	// 广播最终结算事件，附带双方的总胜局数（所有客户端均会执行此行）
	OnShowFinalSettlement.Broadcast(InAttackerWins, InDefenderWins);
}

void ARoomGameState::MulticastEnterSettlement_Implementation(int32 InAttackerKills, int32 InDefenderKills)
{
	UE_LOG(LogTemp, Log, TEXT("[RoomGameState] MulticastEnterSettlement: 攻方=%d, 守方=%d"), InAttackerKills, InDefenderKills);

	// 广播进入结算事件，让所有客户端显示 Text_GameOver
	OnEnterSettlement.Broadcast(InAttackerKills, InDefenderKills);
}