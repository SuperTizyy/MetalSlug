#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "RoomGameMode.generated.h"


/**
 * 房间大厅的专属 GameMode（只在服务器/房主端运行）
 * 负责管理权威的红蓝队名单，并广播给所有人
 */
UCLASS()
class METALSLUG01_API ARoomGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	// 构造函数
	ARoomGameMode(const FObjectInitializer& ObjectInitializer);
	
	// 服务器上保存的权威名单
	TArray<FString> RedTeamNames;
	TArray<FString> BlueTeamNames;

	// 处理新玩家加入的逻辑
	void AddPlayerToRoom(const FString& PlayerName);

	// 把最新名单广播给房间里的所有玩家
	void BroadcastRoomUpdate();
	
	// 处理玩家主动请求换队伍
	void ChangePlayerTeam(const FString& PlayerName, bool bToRedTeam);
	
	// 处理玩家离开房间
	void RemovePlayerFromRoom(const FString& PlayerName);
	
	// 广播玩家聊天
	void BroadcastChatMessage(const FString& SenderName, const FString& Message);
	// 广播系统绿字提示
	void BroadcastSystemMessage(const FString& Message);
	
	// 【新增】：添加 AI 玩家
	void AddAIToRoom(bool bToRedTeam, const FString& CharacterName, int32 Count);
	
	// 【新增】：全频道广播函数（每当名单有变动，立刻通知所有人刷新UI）
	void BroadcastRoomUIUpdate();
	
	// 【新增】：用一个字典(Map)记录所有人的准备状态 (名字 -> 是否准备)
	UPROPERTY()
	TMap<FString, bool> PlayerReadyStates;

	// 【新增】：更新某个人的准备状态并广播
	void UpdatePlayerReadyState(const FString& PlayerName, bool bIsReady);
	
private:
	// 【新增】：AI 的唯一编号生成器，防止同名 AI 无法精准踢出
	int32 AINextID = 1;
};
