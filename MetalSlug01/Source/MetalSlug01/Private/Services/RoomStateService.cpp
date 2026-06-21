// ==========================================
// URoomStateService.cpp
// ==========================================
// 房间状态查询门面实现
// ==========================================

#include "Services/RoomStateService.h"
#include "Systems/RoomGameState.h"
#include "Systems/Core/RoomPlayerState.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
// 【修复 C1083】项目内的 RoomService 在 Public/Systems/ 而非 Public/Services/
#include "Services/RoomService.h"

// ==========================================
// 静态访问器
// ==========================================

URoomStateService* URoomStateService::Get(const UObject* WorldContextObject)
{
    if (!WorldContextObject)
    {
        return nullptr;
    }

    UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
    if (!World)
    {
        return nullptr;
    }

    UGameInstance* GI = World->GetGameInstance();
    return GI ? GI->GetSubsystem<URoomStateService>() : nullptr;
}

// ==========================================
// 比赛级查询
// ==========================================

FMatchSnapshot URoomStateService::GetMatchSnapshot() const
{
    FMatchSnapshot Snapshot; // 默认值（MatchMode=None/各种 0）

    if (const ARoomGameState* GS = GetRoomGameState())
    {
        Snapshot.MatchMode          = GS->CurrentMatchMode;
        Snapshot.RemainingSeconds   = GS->GetMatchRemainingSeconds();
        Snapshot.CurrentRound       = GS->CurrentRound;
        Snapshot.AttackerTotalKills = GS->AttackerTotalKills;
        Snapshot.DefenderTotalKills = GS->DefenderTotalKills;
        Snapshot.AttackerWins       = GS->AttackerWins;
        Snapshot.DefenderWins       = GS->DefenderWins;
        Snapshot.HostPlayerName     = GS->HostPlayerName;
    }

    return Snapshot;
}

bool URoomStateService::IsInRoom() const
{
    // 仅当 World 已加载 RoomGameState 才算"在房间内"
    return GetRoomGameState() != nullptr;
}

int32 URoomStateService::GetMatchRemainingSeconds() const
{
    const ARoomGameState* GS = GetRoomGameState();
    return GS ? GS->GetMatchRemainingSeconds() : 0;
}

// ==========================================
// 玩家级查询
// ==========================================

TArray<FPlayerSnapshot> URoomStateService::GetAttackTeamSnapshots() const
{
    TArray<FPlayerSnapshot> Result;
    const ARoomGameState* GS = GetRoomGameState();
    if (!GS)
    {
        return Result;
    }

    const TArray<ARoomPlayerState*> Members = GS->GetPlayersInTeam(ERoomTeam::Attack);
    Result.Reserve(Members.Num());
    for (ARoomPlayerState* PS : Members)
    {
        if (!PS) continue;
        const bool bIsHost = PS->GetPlayerName() == GS->HostPlayerName;
        Result.Add(BuildSnapshot(PS, bIsHost));
    }
    return Result;
}

TArray<FPlayerSnapshot> URoomStateService::GetDefenseTeamSnapshots() const
{
    TArray<FPlayerSnapshot> Result;
    const ARoomGameState* GS = GetRoomGameState();
    if (!GS)
    {
        return Result;
    }

    const TArray<ARoomPlayerState*> Members = GS->GetPlayersInTeam(ERoomTeam::Defense);
    Result.Reserve(Members.Num());
    for (ARoomPlayerState* PS : Members)
    {
        if (!PS) continue;
        const bool bIsHost = PS->GetPlayerName() == GS->HostPlayerName;
        Result.Add(BuildSnapshot(PS, bIsHost));
    }
    return Result;
}

FPlayerSnapshot URoomStateService::GetLocalPlayerSnapshot() const
{
    ARoomPlayerState* PS = GetLocalPlayerState();
    const ARoomGameState* GS = GetRoomGameState();
    const bool bIsHost = (PS && GS) && (PS->GetPlayerName() == GS->HostPlayerName);
    return PS ? BuildSnapshot(PS, bIsHost) : FPlayerSnapshot();
}

bool URoomStateService::IsLocalPlayerReady() const
{
    const ARoomPlayerState* PS = GetLocalPlayerState();
    return PS && PS->bIsReady;
}

bool URoomStateService::IsLocalPlayerHost() const
{
    const URoomService* RoomSvc = URoomService::Get(this);
    return RoomSvc && RoomSvc->IsHost();
}

ERoomTeam URoomStateService::GetLocalPlayerTeam() const
{
    const ARoomPlayerState* PS = GetLocalPlayerState();
    return PS ? PS->CurrentTeam : ERoomTeam::None;
}

// ==========================================
// 队伍统计查询
// ==========================================

int32 URoomStateService::GetAttackReadyCount() const
{
    int32 Count = 0;
    for (const FPlayerSnapshot& Snap : GetAttackTeamSnapshots())
    {
        if (Snap.bIsReady) ++Count;
    }
    return Count;
}

int32 URoomStateService::GetDefenseReadyCount() const
{
    int32 Count = 0;
    for (const FPlayerSnapshot& Snap : GetDefenseTeamSnapshots())
    {
        if (Snap.bIsReady) ++Count;
    }
    return Count;
}

// ==========================================
// 内部辅助
// ==========================================

ARoomGameState* URoomStateService::GetRoomGameState() const
{
    UWorld* World = GetWorld();
    if (!World) return nullptr;
    return World->GetGameState<ARoomGameState>();
}

ARoomPlayerState* URoomStateService::GetLocalPlayerState() const
{
    UWorld* World = GetWorld();
    if (!World) return nullptr;

    const APlayerController* LocalPC = World->GetFirstPlayerController();
    return LocalPC ? LocalPC->GetPlayerState<ARoomPlayerState>() : nullptr;
}

FPlayerSnapshot URoomStateService::BuildSnapshot(ARoomPlayerState* PS, bool bIsHost)
{
    if (!PS)
    {
        return FPlayerSnapshot();
    }

    FPlayerSnapshot Snap;
    Snap.PlayerName          = PS->GetPlayerName();
    Snap.Team                = PS->CurrentTeam;
    Snap.bIsReady            = PS->bIsReady;
    Snap.bIsHost             = bIsHost;
    Snap.Score               = PS->GetScore();
    Snap.Kills               = PS->GetKills();
    Snap.Deaths              = PS->GetDeaths();
    Snap.Assists             = PS->GetAssists();
    Snap.SelectedCharacterID = PS->GetSelectedCharacterID();
    Snap.SelectedWeaponID1   = PS->GetSelectedWeapon1ID();
    Snap.SelectedWeaponID2   = PS->GetSelectedWeapon2ID();
    return Snap;
}