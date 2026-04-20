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