#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "RoomGameState.generated.h"

/**
 * 房间全局状态类
 * 引擎会自动将所有连入房间的 PlayerState 存放在原生的 PlayerArray 数组中。
 * 这里未来可以放置房间的全局数据，如“当前对局阶段(等待、开战、结算)”、“总比分”等。
 */
UCLASS()
class METALSLUG01_API ARoomGameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	ARoomGameState();

	// 提供一个极其方便的辅助函数：获取特定队伍的所有玩家
	// 因为数据分散在每个人自己的 PlayerState 里了，所以我们需要遍历查询
	UFUNCTION(BlueprintCallable, Category = "Room|Query")
	TArray<class ARoomPlayerState*> GetPlayersInTeam(ERoomTeam TargetTeam) const;
	
	// 记录当前房间的房主名称，全服同步！
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Room|Global")
	FString HostPlayerName;
};