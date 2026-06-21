// MetalSlug01. All Rights Reserved.
#include "Systems/Session/SessionResult.h"
#include "OnlineSessionSettings.h"

FRoomSessionResult::FRoomSessionResult(const FOnlineSessionSearchResult& InResult)
{
	if (!InResult.IsValid()) return;

	// ---- 提取房间名称 ----
	InResult.Session.SessionSettings.Get(FName("ROOM_NAME"), RoomName);
	if (RoomName.IsEmpty()) RoomName = TEXT("未命名房间");

	// ---- 提取房主账号 ----
	InResult.Session.SessionSettings.Get(FName("HOST_ACCOUNT"), HostAccount);

	// ---- 提取游戏模式 ----
	InResult.Session.SessionSettings.Get(FName("GAME_MODE"), GameMode);

	// ---- 提取地图显示名和资源名 ----
	InResult.Session.SessionSettings.Get(FName("MAP_NAME"), MapName);
	LevelName = FName(*MapName);

	// ---- 计算最大人数 ----
	MaxPlayers = InResult.Session.SessionSettings.NumPublicConnections;
	if (MaxPlayers <= 0) MaxPlayers = 10;

	// ---- 计算当前人数 ----
	int32 CurrentRealPlayers = MaxPlayers - InResult.Session.NumOpenPublicConnections;
	if (CurrentRealPlayers <= 0) CurrentRealPlayers = 1;

	// ---- 提取含 AI 人数 ----
	InResult.Session.SessionSettings.Get(FName("TOTAL_PLAYERS_WITH_AI"), CurrentPlayers);
	if (CurrentPlayers <= 0) CurrentPlayers = CurrentRealPlayers;

	// ---- 提取战斗状态 ----
	InResult.Session.SessionSettings.Get(FName("ROOM_STATE"), bIsInBattle);

	// ---- 生成签名 ----
	Signature = GenerateSignature();

	// ---- 保存原始结果（用于 JoinSession） ----
	RawSearchResult = MakeShared<FOnlineSessionSearchResult>(InResult);
}

FString FRoomSessionResult::GenerateSignature() const
{
	return FString::Printf(TEXT("%s_%d_%d_%d"), *RoomName, CurrentPlayers, MaxPlayers, bIsInBattle ? 1 : 0);
}

bool FRoomSessionResult::IsValid() const
{
	return !RoomName.IsEmpty() && RawSearchResult.IsValid() && RawSearchResult->IsValid();
}
