#include "Systems/RoomGameState.h"

#include "Net/UnrealNetwork.h"
#include "UI/Login/Core/RoomPlayerState.h"

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

void ARoomGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// 注册同步变量
	DOREPLIFETIME(ARoomGameState, CurrentMatchMode);
	DOREPLIFETIME(ARoomGameState, MatchRemainingTime);
	DOREPLIFETIME(ARoomGameState, HostPlayerName);
	DOREPLIFETIME(ARoomGameState, CurrentRound);
}

void ARoomGameState::OnRep_MatchRemainingTime()
{
	// 当客户端收到服务器同步的最新的倒计时后，广播给 UI 刷新文本
	OnMatchTimeUpdated.Broadcast(MatchRemainingTime);
}

void ARoomGameState::OnRep_CurrentRound()
{
	OnCurrentRoundUpdated.Broadcast(CurrentRound);
}