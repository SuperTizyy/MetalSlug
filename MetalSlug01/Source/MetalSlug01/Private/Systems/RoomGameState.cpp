#include "Systems/RoomGameState.h"

#include "Net/UnrealNetwork.h"
#include "UI/Login/Core/RoomPlayerState.h"

ARoomGameState::ARoomGameState()
{
	// 暂无特殊初始化
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
	DOREPLIFETIME(ARoomGameState, HostPlayerName); // 别忘了加这行！
}