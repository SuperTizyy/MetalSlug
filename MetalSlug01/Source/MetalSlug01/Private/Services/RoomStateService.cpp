// ==========================================
// URoomStateService.cpp
// ==========================================
// 房间状态查询门面实现
// ==========================================

#include "Services/RoomStateService.h"
#include "Systems/RoomGameState.h"
#include "Systems/RoomGameMode.h"
#include "Systems/Core/RoomPlayerState.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
// 【修复 C1083】项目内的 RoomService 在 Public/Systems/ 而非 Public/Services/
#include "Services/RoomService.h"
// 【2026.07.10 P0 重构】阵营集中定义
#include "Data/Faction/FactionTags.h"
// 【2026.07.11 v28】FPendingAIEntry (AI 占位数据)
#include "Systems/AI/AIBehaviorTypes.h"

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

TArray<FPlayerSnapshot> URoomStateService::GetAttackFactionSnapshots() const
{
    // 【P0 v29 大厂架构决策】旧实现被回滚 (仅 Snapshot = 真人)
    //
    // 历史:
    //   v28 错误做法: 合并真人 (PlayerArray) + AI 占位 (PendingAIQueue) → AllSnapshots
    //     → 测试模式 EnterSkipToHostMode **不会 add PlayerArray** (只 set HostPlayerName)
    //     → 真人路径永远为空 → Box_AttackTeam/DefenseTeam 不显示 UI 标签 ← 用户反馈的 bug
    //
    // v29 修复: 数据流重新分离 — UI 渲染用 KnownPlayerStates (真人事件订阅流,可靠) +
    //          显式 GetPendingAIInFaction(阵营) (AI 占位, 大厅才存在), 两者由 UI 自己合并
    //
    // 单一真理源 (v29 落地):
    //   - 真人: GS->PlayerArray (ARoomPlayerState) — 由 UE 引擎自动 add
    //   - AI 占位: GM->GetPendingAIInFaction — 用户入队写入,战斗 Spawn 清空
    return GetFactionSnapshotsInternal(FFactionTags::Offense(), /*bIncludeAI=*/false);
}

TArray<FPlayerSnapshot> URoomStateService::GetDefenseFactionSnapshots() const
{
    // 同上 v29 大厂决策 (见 GetAttackFactionSnapshots 注释)
    return GetFactionSnapshotsInternal(FFactionTags::Defense(), /*bIncludeAI=*/false);
}


/**
 * 【2026.07.11 v29 大厂架构重构】统一查询入口 — 单一真理源分离
 *
 * 历史 (v28):
 *   旧版合并真人 + AI 占位 → UI 拿不到测试模式的真人 (PlayerArray 在 MockLogin 下没填)
 *   → Bug: Box_AttackTeam/Box_DefenseTeam 不显示 UI 标签
 *
 * 新 (v29) 原则:
 *   - 单一真理源: 真人 (PlayerArray) 与 AI 占位 (PendingAIQueue) 是**两条独立数据流**
 *   - 显式意图: 调用方通过参数 bIncludeAI 显式声明"我要哪条"
 *   - 零兜底: 调用方不传 bIncludeAI → 默认只真人 (UI 渲染 KnownPlayerStates 也是只真人)
 *
 * 设计动机 (大厂原则 - 减熵):
 *   - UI 渲染路径: 不依赖这个函数, 而是
 *     a) KnownPlayerStates (真人事件订阅链, 历史可靠)
 *     b) GM->GetPendingAIInFaction (AI 占位, 大厅独有)
 *   - 此函数只供 ScoreboardWidget / MatchReadyCheck 等**纯净查询方**用 (它们只关心真人)
 *   - AI 占位查询走独立 API: GM->GetPendingAIInFaction(阵营)
 *
 * @param FactionTag 阵营
 * @param bIncludeAI 是否合并 AI 占位
 *        - true: 真人 + AI (战斗 Scoreboard 不该用这个, 因战斗时 PendingAIQueue 已被清空)
 *        - false: 只真人 (默认, 旧 API 兼容)
 * @return 真人快照 (或真人 + AI 快照)
 */
TArray<FPlayerSnapshot> URoomStateService::GetFactionSnapshotsInternal(FGameplayTag FactionTag, bool bIncludeAI) const
{
    TArray<FPlayerSnapshot> Result;

    // 【P0 2026.07.10】无效阵营 — 显式报错, 不静默返回空
    if (!FFactionTags::IsValidFaction(FactionTag))
    {
        UE_LOG(LogTemp, Error,
            TEXT("[RoomStateService] GetFactionSnapshots: FactionTag='%s' 非有效阵营, 返回空数组"),
            *FactionTag.ToString());
        return Result;
    }

    UWorld* World = GetWorld();
    if (!World)
    {
        return Result;
    }

    ARoomGameState* GS = World->GetGameState<ARoomGameState>();
    ARoomGameMode*   GM = World->GetAuthGameMode<ARoomGameMode>();

    // ==========================================
    // 路径 1: 真人玩家 (从 PlayerArray)
    // ==========================================
    if (GS)
    {
        const TArray<ARoomPlayerState*> Members = GS->GetPlayersInFaction(FactionTag);
        Result.Reserve(Members.Num() + 4);
        for (ARoomPlayerState* PS : Members)
        {
            if (!PS) continue;
            const bool bIsHost = PS->GetPlayerName() == GS->HostPlayerName;
            Result.Add(BuildSnapshot(PS, bIsHost));
        }
    }

    // ==========================================
    // 路径 2: AI 占位 (从 GameMode.PendingAIQueue) — 只在 bIncludeAI=true 时启用
    // ==========================================
    if (bIncludeAI && GM)
    {
        const TArray<FPendingAIEntry> PendingAI = GM->GetPendingAIInFaction(FactionTag);
        for (const FPendingAIEntry& Entry : PendingAI)
        {
            Result.Add(BuildAISnapshot(Entry));
        }
    }

    return Result;
}


/**
 * 【2026.07.11 v29】新增公共 API: GetFactionSnapshotsWithAI — 显式合并真人 + AI
 *
 * 用途:
 *   - UI 房主邀请 AI 时: 想知道"现在攻方总共有多少人 (含 AI 占位), 还能加几个?"
 *   - 大厂原则: 显式 API > 隐式 bIncludeAI 参数 (可读性优先)
 *
 * @param FactionTag 阵营
 * @return 真人 + AI 占位快照 (大厅阶段才有 AI 数据)
 */
TArray<FPlayerSnapshot> URoomStateService::GetFactionSnapshotsWithAI(FGameplayTag FactionTag) const
{
    return GetFactionSnapshotsInternal(FactionTag, /*bIncludeAI=*/true);
}


/**
 * 【2026.07.11 v28】内部辅助: 把一个 FPendingAIEntry 转成 FPlayerSnapshot
 * 大厂原则: 字段一一对应, 不在调用方各自拼凑
 *
 * @param Entry 来自 GM->PendingAIQueue 的一条 AI 占位
 * @return FPlayerSnapshot (bIsAI=true)
 */
FPlayerSnapshot URoomStateService::BuildAISnapshot(const FPendingAIEntry& Entry)
{
    FPlayerSnapshot Snap;
    Snap.PlayerName          = Entry.DisplayName;
    Snap.FactionTag          = Entry.FactionTag;
    Snap.bIsReady            = false; // AI 无准备概念
    Snap.bIsHost             = false; // AI 永远不是房主
    Snap.bIsAI               = true;  // 标记: AI 占位
    Snap.SequenceID          = Entry.SequenceID;
    Snap.Score               = 0;
    Snap.Kills               = 0;
    Snap.Deaths              = 0;
    Snap.Assists             = 0;
    Snap.SelectedCharacterID = Entry.CharacterInfoRowName.ToString();  // 【v49】替换原 CharacterRowName
    Snap.SelectedWeaponID1   = Entry.WeaponID;
    Snap.SelectedWeaponID2   = TEXT("");
    return Snap;
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

FGameplayTag URoomStateService::GetLocalPlayerFaction() const
{
    const ARoomPlayerState* PS = GetLocalPlayerState();
    // 【2026.07.10 P0 重构】直接读 PS->CurrentFactionTag (FGameplayTag)
    // 无 GameState/PlayerState 时返回空 Tag, 调用方需用 FFactionTags::IsValidFaction check
    return PS ? PS->CurrentFactionTag : FGameplayTag::EmptyTag;
}

// ==========================================
// 队伍统计查询
// ==========================================

int32 URoomStateService::GetAttackReadyCount() const
{
    int32 Count = 0;
    for (const FPlayerSnapshot& Snap : GetAttackFactionSnapshots())
    {
        if (Snap.bIsReady) ++Count;
    }
    return Count;
}

int32 URoomStateService::GetDefenseReadyCount() const
{
    int32 Count = 0;
    for (const FPlayerSnapshot& Snap : GetDefenseFactionSnapshots())
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
    // 【2026.07.10 P0 重构】FactionTag 替代 Team (ERoomTeam 已删除)
    Snap.FactionTag          = PS->CurrentFactionTag;
    Snap.bIsReady            = PS->bIsReady;
    Snap.bIsHost             = bIsHost;
    Snap.Score               = PS->GetScore();
    Snap.Kills               = PS->GetKills();
    Snap.Deaths              = PS->GetDeaths();
    Snap.Assists             = PS->GetAssists();
    Snap.SelectedCharacterID = PS->GetSelectedCharacterID();
    Snap.SelectedWeaponID1   = PS->GetSelectedWeapon1ID();
    Snap.SelectedWeaponID2   = PS->GetSelectedWeapon2ID();
    // 【v52 P0】第 3 把武器 (近战), 大厅运行时态写入, 大厂原则 — 与 ARoomPlayerState 字段对称
    Snap.SelectedWeaponID3   = PS->GetSelectedWeapon3ID();
    return Snap;
}