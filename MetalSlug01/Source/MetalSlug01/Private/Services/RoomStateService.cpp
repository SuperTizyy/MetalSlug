// ==========================================
// URoomStateService.cpp
// ==========================================
// 房间状态查询门面实现
// ==========================================

#include "Services/RoomStateService.h"
#include "Systems/RoomGameState.h"
#include "Systems/RoomGameMode.h"
#include "Systems/Core/RoomPlayerState.h"
// 【v202.0 大厂架构新增】ABaseAIController 完整类型 (TActorIterator 需要)
#include "Systems/BaseAIController.h"
#include "Engine/GameInstance.h"
#include "Engine/World.h"
#include "EngineUtils.h"  // TActorIterator
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
// 【修复 C1083】项目内的 RoomService 在 Public/Systems/ 而非 Public/Services/
#include "Services/RoomService.h"
// 【2026.07.10 P0 重构】阵营集中定义
#include "Data/Faction/FactionTags.h"
// 【2026.07.11 v28】FPendingAIEntry (AI 占位数据)
#include "Systems/AI/AIBehaviorTypes.h"
// 【v218 大厂架构新增】ABaseCharacter 完整类型 (Pawn->FactionTag fallback 需要)
#include "Characters/BaseCharacter.h"

// 【v215 大厂架构新增】World 生命周期订阅 — 用于在 PIE 多次加载/卸载时正确订阅事件
#include "Engine/Engine.h"

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
        Snapshot.TotalRounds        = GS->TotalRounds; // 【v92 大厂架构】UI 真理源
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
    // 路径 2: AI 占位 / 战斗阶段 AI 数据 — 只在 bIncludeAI=true 时启用
    // ==========================================
    //
    // 【v202.0 大厂架构重构 — 双数据源拆分】
    //   历史 (v28-v201.x): 只读 GM->GetPendingAIInFaction → 大厅阶段有效, 战斗阶段为空
    //     → ScoreboardWidget 战斗时永远不显示 AI 数据 (用户报告)
    //   新 (v202.0):
    //     - 大厅阶段 (PendingAIQueue 非空): 走 BuildAISnapshot(FPendingAIEntry) (RoomInsidePage 等)
    //     - 战斗阶段 (PendingAIQueue 已清空, AIC 已 Spawn): 走 BuildAISnapshotFromController (ScoreboardWidget)
    //     - 两者都允许同时返回: 极端情况下 PendingAIQueue 还有遗留且 AIC 也在
    //
    // 大厂原则:
    //   - 不重复架构: AI 占位 (PendingAIQueue) 与 AI 战斗 (AIC) 是两个独立概念
    //   - 单一真理源: 战斗阶段 AI 数据 = AIC.AIScore/AIKills/AIDeaths/AIAssists (Replicated)
    //   - 零兜底: AIC 为 nullptr → 跳过 (不报错, 因为 PendingAIQueue 可能还有遗留)
    if (bIncludeAI)
    {
        // 2.1 大厅阶段 AI 占位 (PendingAIQueue)
        if (GM)
        {
            const TArray<FPendingAIEntry> PendingAI = GM->GetPendingAIInFaction(FactionTag);
            for (const FPendingAIEntry& Entry : PendingAI)
            {
                Result.Add(BuildAISnapshot(Entry));
            }
        }

        // 2.2 战斗阶段 AI 数据 (TActorIterator<AIC>)
        // 大厂原则 — 数据驱动: 不维护账本 (容易漂移), 直接 GetAllActorsOfClass
        //   - 极端 case: 关卡预放 AI 也包括在内 (它们的 AIC 也是 ABaseAIController 派生)
        //   - 战斗死亡时 AIC 不被销毁 (v116 修复), 仍可读到
        //
        // 【v221.2 大厂架构诊断】添加 Display Log 让用户复测时能看到具体哪条 AI 被过滤
        int32 AICScannedCount = 0;
        int32 AICFilteredByFactionCount = 0;
        int32 AICInvalidFactionCount = 0;
        for (TActorIterator<ABaseAIController> It(World); It; ++It)
        {
            ABaseAIController* AIController = *It;
            if (!AIController) continue;
            AICScannedCount++;

            // 【v218 + v221.1 + v221.2 大厂架构修复 — 客户端 AI 阵营过滤 fallback】
            //   根因 (v218 之前):
            //     - ABaseAIController::CachedFactionTag 非 Replicated (本机权威字段, 与 CachedAIPawnClass/CachedWeaponID 一致)
            //     - 客户端 AIC.CachedFactionTag 永为空 (EmptyTag)
            //     → 客户端 GetFactionSnapshotsWithAI 把所有 AI 过滤掉 (line 209 continue)
            //     → 用户报告: "客户端游戏内 Scoreboard 不显示 AI 信息, 而监听服务器能看到"
            //
            //   修复 (v218):
            //     - CachedFactionTag 为空时 fallback 到 Pawn->FactionTag (ABaseCharacter::FactionTag 是 Replicated, 客户端可见)
            //     - Pawn 也为空 → 跳过 (与 AIC.CachedFactionTag == FactionTag 一致语义: 没法判定就排除)
            //
            //   二次修复 (v221.1):
            //     - ABaseAIController::CachedFactionTag 升级为 Replicated (单一真理源, 客户端可读)
            //     - v218 的 Pawn->FactionTag fallback 链路依赖 Pawn 实例 + FactionTag 字段两个同步时序
            //       任何一个时序窗口失败 → 仍会 continue 跳过 AI (用户报告 "Tab 不显示 AI")
            //     - 升级后 AIC->CachedFactionTag 直接同步, 不依赖 Pawn
            //     - Pawn->FactionTag 保留作为次级 fallback (双 Replicated 保障, 万一 CachedFactionTag 同步比 AIC 还晚)
            //
            //   三次修复 (v221.2 大厂架构):
            //     - 客户端 OnPossess 时如果 AIC.CachedFactionTag 仍空, 用 Pawn.FactionTag 反向补 AIC.CachedFactionTag
            //     - 服务器 SetCachedFactionTag 加 ForceNetUpdate 加速同步
            //     - 0 兜底: 任何路径失败 → Log Warning 暴露, 不静默
            //
            //   大厂原则 — 零兜底:
            //     - 所有 fallback 都失败 → 不静默返回默认值 (会让客户端看到"未知阵营"AI, 业务错乱)
            //     - 显式 continue + Log Warning, 强制配置正确 (虽然升级后这个 case 几乎不会发生)
            // ============================================================
            FGameplayTag ResolvedFactionTag = AIController->CachedFactionTag;

            // v221.1: Pawn->FactionTag fallback (次级, 仅在 CachedFactionTag 仍为空时启用)
            //   - 升级 CachedFactionTag 为 Replicated 后, 主路径已能用
            //   - 但保留次级 fallback 应对 CachedFactionTag 复制极晚于 AIC 实例的极端 case
            if (!FFactionTags::IsValidFaction(ResolvedFactionTag))
            {
                if (const ABaseCharacter* AIOwnedPawn = Cast<ABaseCharacter>(AIController->GetPawn()))
                {
                    if (FFactionTags::IsValidFaction(AIOwnedPawn->FactionTag))
                    {
                        ResolvedFactionTag = AIOwnedPawn->FactionTag;
                    }
                }
            }

            // 阵营过滤: 只返回当前 FactionTag 的 AI
            //   - 用 IsValidFaction 守卫 (ResolvedFactionTag 仍可能为空 — 极罕见 case)
            //   - v221.1 双 Replicated 保障后, fallback 链几乎不会失败
            if (!FFactionTags::IsValidFaction(ResolvedFactionTag))
            {
                AICInvalidFactionCount++;
                UE_LOG(LogTemp, Warning,
                    TEXT("[RoomStateService] 【v221.2】GetFactionSnapshotsInternal: AI '%s' 阵营无法判定 (CachedFactionTag+Pawn->FactionTag 都无效), "
                         "跳过本条. 【零兜底】如持续出现, 检查 (1) 服务器 SetCachedFactionTag 时序 (2) AIC.bReplicates=true "
                         "(3) 服务器 ForceNetUpdate 是否成功 (4) 客户端 OnPossess 时 Pawn.FactionTag 是否已复制."),
                    *AIController->GetName());
                continue;
            }
            if (ResolvedFactionTag != FactionTag)
            {
                AICFilteredByFactionCount++;
                continue;
            }

            Result.Add(BuildAISnapshotFromController(AIController));
        }

        // 【v221.2 大厂架构诊断】让用户复测时能看到完整的 AI 扫描统计
        //   - 已扫描: 客户端有几只 AIC 实例
        //   - 阵营有效: CachedFactionTag + Pawn fallback 至少一个有效
        //   - 阵营过滤: 属于其他阵营,被本函数过滤掉
        //   - 阵营无效: 两侧都拿不到,被跳过 (b端问题)
        //   - 最终 Result: 本 FactionTag 的 AI 数(应等于 阵营有效 - 阵营过滤)
        UE_LOG(LogTemp, Display,
            TEXT("[RoomStateService] 【v221.2】GetFactionSnapshotsInternal: FactionTag='%s' 扫描统计: "
                 "AIC总数=%d, 阵营有效=%d, 阵营过滤=%d, 阵营无效=%d, 加入Result=%d."),
            *FactionTag.ToString(),
            AICScannedCount, AICScannedCount - AICInvalidFactionCount - AICFilteredByFactionCount,
            AICFilteredByFactionCount, AICInvalidFactionCount,
            Result.Num());
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
 * 【v223.0 大厂架构新增】只返回真人玩家 (无 AI 混合)
 * 内部委托 GetFactionSnapshotsInternal(FactionTag, false)
 * 0 兜底: GameState 拿不到 → Log Error + 返回空 (与 GetFactionSnapshotsWithAI 一致)
 */
TArray<FPlayerSnapshot> URoomStateService::GetFactionSnapshots(FGameplayTag FactionTag) const
{
	return GetFactionSnapshotsInternal(FactionTag, /*bIncludeAI=*/false);
}


/**
 * 【2026.07.11 v28】内部辅助: 把一个 FPendingAIEntry 转成 FPlayerSnapshot
 * 大厂原则: 字段一一对应, 不在调用方各自拼凑
 *
 * 【v202.0 大厂架构重构】字段来源拆 AI / 真人 双真理源:
 *   - 历史 (v28-v201.x): Snapshot.Score/Kills/Deaths/Assists 永远 = 0 (没数据源)
 *   - 新 (v202.0):
 *     - 大厅阶段 (AI 还没 Spawn): 从 FPendingAIEntry 读, 默认 0 (AI 还没参加战斗)
 *     - 战斗阶段 (AI 已 Spawn): 从 ABaseAIController 读 (新增 Replicated 字段)
 *     - 真人不受此影响, 用 ARoomPlayerState (已有 Replicated 字段)
 *
 * 大厂原则 — 单一真理源: 真人 / AI 两条独立数据流, 不混合
 *
 * @param Entry 来自 GM->PendingAIQueue 的一条 AI 占位
 * @return FPlayerSnapshot (bIsAI=true)
 */
FPlayerSnapshot URoomStateService::BuildAISnapshot(const FPendingAIEntry& Entry)
{
    FPlayerSnapshot Snap;
    // 【v208 大厂架构重构 — AI PlayerName 单一真理源】
    //   与 BuildAISnapshotFromController 对称: 永远带 "[AI] " 前缀
    //   历史 (v202.0): Snap.PlayerName = Entry.DisplayName (无前缀) → 与 EntryWidget->GetPlayerName() 不一致
    //   新 (v208): Snap.PlayerName = "[AI] " + DisplayName → 与 CreateEntryWidgetFromSnapshot 输出对齐
    //   大厂原则 — 单一真理源: "[AI] " 前缀只在 BuildAISnapshot* 一处拼, 不在 UI 层重复
    Snap.PlayerName          = FString::Printf(TEXT("[AI] %s"), *Entry.DisplayName);
    Snap.FactionTag          = Entry.FactionTag;
    Snap.bIsReady            = false; // AI 无准备概念
    Snap.bIsHost             = false; // AI 永远不是房主
    Snap.bIsAI               = true;  // 标记: AI 占位
    Snap.SequenceID          = Entry.SequenceID;
    // 【v202.0】战斗阶段读 AIC.Replicated 字段; 大厅阶段 (AI 还没 Spawn) 默认 0
    // 后续 PR: 改用 AIC.CachedAIScore/CachedAIKills 等同步 (但目前 Replicated 已够用)
    Snap.Score               = 0;
    Snap.Kills               = 0;
    Snap.Deaths              = 0;
    Snap.Assists             = 0;
    Snap.SelectedCharacterID = Entry.CharacterInfoRowName.ToString();  // 【v49】替换原 CharacterRowName
    Snap.SelectedWeaponID1   = Entry.WeaponID;
    Snap.SelectedWeaponID2   = TEXT("");
    return Snap;
}

/**
 * 【v202.0 大厂架构新增】从 AIC 实时读取并构建 AI 计分快照
 *
 * 与 BuildAISnapshot (FPendingAIEntry) 不同: 本函数读运行时 Replicated 字段
 *
 * 用途:
 *   - 战斗阶段 ScoreboardWidget 显示已 Spawn 的 AI 数据
 *   - 不依赖 AIC 的 OnRep 事件 (UI 自己拉一次就够)
 *
 * 调用方:
 *   - URoomStateService::GetFactionSnapshotsInternal (战斗阶段合并 AI)
 *
 * 大厂原则:
 *   - 不缓存 (CachedFactionTag 等已是真理源, 拉一次足够)
 *   - AIC 为 nullptr 时返回默认空 Snapshot (调用方聚合时跳过)
 */
FPlayerSnapshot URoomStateService::BuildAISnapshotFromController(ABaseAIController* AIController)
{
    FPlayerSnapshot Snap;
    if (!AIController)
    {
        return Snap; // 默认空 (bIsAI=false, 需要调用方手动 set)
    }

    Snap.bIsAI               = true;
    // 【v208 大厂架构重构 — AI PlayerName 单一真理源】
    //   历史 (v202.0): Snap.PlayerName = AIC->GetName() (RawName, 如 "AIC_AI_SWAT_AI_3")
    //     → UpdateOrCreateEntryFromSnapshot 中比较 Entry->GetPlayerName() == Snapshot.PlayerName
    //     → 但 Entry->GetPlayerName() 是 "[AI] AIC_AI_SWAT_AI_3" (带前缀, CreateEntryWidgetFromSnapshot 拼接)
    //     → 比较失败 → 每次 Refresh 找不到现有 Entry → 反复创建新 Widget
    //     → 多个 AI Entry 在 VB 容器里堆积 + KDA 显示错位
    //
    //   新 (v208): Snap.PlayerName 直接含 "[AI] " 前缀 (数据源统一)
    //     - BuildAISnapshotFromController 是单一真理源: AI 名字永远带前缀
    //     - CreateEntryWidgetFromSnapshot 不再拼接前缀 (避免双前缀)
    //     - 比较 Entry->GetPlayerName() == Snapshot.PlayerName 永远命中
    //     - 大厂原则 — 单一真理源: "[AI] " 前缀只在 BuildAISnapshot 一处拼, 不在 UI 层重复
    //
    // 【v218 大厂架构修复】FactionTag 单一真理源改为 Pawn->FactionTag (Replicated)
    //   历史: Snap.FactionTag = AIController->CachedFactionTag (本机权威, 客户端永为空)
    //     → 客户端 Snap.FactionTag 为空 → UI 显示空白阵营标签
    //   新 (v218): Pawn->FactionTag 是 Replicated, 客户端可见 → 单一真理源
    //   - AIC.CachedFactionTag 仍保留, 仅用于服务器端 Spawn 流程 + GameState 阵营判定
    //   - 客户端读取场景一律走 Pawn->FactionTag (镜像 RoomStateService::GetFactionSnapshotsInternal 的 v218 fallback)
    Snap.PlayerName          = FString::Printf(TEXT("[AI] %s"), *AIController->GetName()); // 大厂原则: 显示名 = AIC Name (Server authoritative) + AI 前缀

    // 【v221.1 大厂架构重构 — FactionTag 真理源升级】
    //   历史 (v218): 用 Pawn->FactionTag 作为真理源 (Pawn 已复制 → FactionTag 字段已复制)
    //     → 但依赖 Pawn 实例 + FactionTag 字段两个同步时序, 任一失败 → FactionTag 空
    //   新 (v221.1): 优先用 AIC.CachedFactionTag (v221.1 升级为 Replicated, 不依赖 Pawn)
    //     → 升级后 AIC 实例同步过来时 CachedFactionTag 已就绪, 任何时序都能拿到
    //     → Pawn->FactionTag 作为次级 fallback (Pawn 销毁/未复制时的兜底)
    //
    // 不破坏既有业务:
    //   - AIC->CachedFactionTag 是 Pawn->FactionTag 的运行时段缓存 (v25-v26 大厂原则)
    //   - 两者在 Spawn 路径上一致写入, 客户端读到的是同一个值
    if (FFactionTags::IsValidFaction(AIController->CachedFactionTag))
    {
        Snap.FactionTag = AIController->CachedFactionTag; // v221.1: 主路径, 永远可用
    }
    else if (const ABaseCharacter* AIOwnedPawn = Cast<ABaseCharacter>(AIController->GetPawn()))
    {
        Snap.FactionTag = AIOwnedPawn->FactionTag; // 次级 fallback, 镜像 GetFactionSnapshotsInternal 的 v221.1 fallback 链
    }
    else
    {
        // 0 兜底: 两者都为空 → 不设默认值 (返回空 FactionTag, 调用方 RefreshScoreboard 会 Log Error)
        UE_LOG(LogTemp, Warning,
            TEXT("[RoomStateService] 【v221.1】BuildAISnapshotFromController: AIC '%s' CachedFactionTag 与 Pawn->FactionTag 都无效, "
                 "Snap.FactionTag 留空. 【零兜底】如持续出现, 检查 AIC 网络复制与 Spawn 链路."),
            *AIController->GetName());
    }
    Snap.bIsReady            = false;
    Snap.bIsHost             = false;
    Snap.SequenceID          = 0;

    // 【v202.0 大厂架构】读 AIC 的 Replicated 字段
    // 客户端调用时, 引擎已把服务器字段自动 Replicate 过来 (OnRep_AIScoreboardData 触发过)
    // 0 是默认值 — 字段没同步过时表示 AI 还没击杀
    Snap.Score               = AIController->AIScore;
    Snap.Kills               = AIController->AIKills;
    Snap.Deaths              = AIController->AIDeaths;
    Snap.Assists             = AIController->AIAssists;
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
// 【v215 大厂架构新增】事件驱动刷新入口
// ==========================================

/**
 * 【v215 大厂架构新增】主动 Broadcast OnPlayerSnapshotsChanged
 *
 * 调用方:
 *   - UScoreboardWidget::NativeConstruct (首次订阅时手动触发, 保证 UI 立即有数据)
 *   - UGameHUDWidget::OnEnterSettlement (进入结算时手动触发, 触发一次全量拍照)
 *   - 任何外部代码需要强制 View 立即刷新时
 *
 * 大厂原则:
 *   - 唯一公开的 Broadcast 入口: 严禁外部代码直接调 OnPlayerSnapshotsChanged.Broadcast()
 *   - 0 兜底: World 不存在时 Log Error + return false, 不静默跳过
 */
bool URoomStateService::BroadcastPlayerSnapshotsChanged()
{
    UWorld* World = GetWorld();
    if (!World)
    {
        UE_LOG(LogTemp, Error,
            TEXT("[RoomStateService] BroadcastPlayerSnapshotsChanged: World 不存在, 无法广播. "
                 "【修复】检查调用方是否在 World 已销毁后调用."));
        return false;
    }

    UE_LOG(LogTemp, Verbose,
        TEXT("[RoomStateService] BroadcastPlayerSnapshotsChanged: 主动通知所有订阅者刷新 (EventSubscribers.Num()=%d)"),
        OnPlayerSnapshotsChanged.IsBound() ? 1 : 0);

    OnPlayerSnapshotsChanged.Broadcast();
    return true;
}

/**
 * 【v215 大厂架构新增】事件转发器 — 内部回调, 把上游事件统一转发给 OnPlayerSnapshotsChanged
 *
 * 设计动机:
 *   - 多个上游事件 (AI 分数 OnRep / 真人 PS OnRep / GS 事件) 都触发同一类 UI 刷新
 *   - 把所有上游事件集中到一个转发器, View 只需订阅一个委托
 *   - DRY: View 不需要知道有多少个上游事件源
 *
 * 大厂原则 — 单一事件出口:
 *   - 所有上游事件 → ForwardPlayerSnapshotsChanged → OnPlayerSnapshotsChanged.Broadcast
 *   - 严禁 View 直接订阅上游事件 (会让 View 知道数据层细节)
 */
void URoomStateService::ForwardPlayerSnapshotsChanged()
{
    OnPlayerSnapshotsChanged.Broadcast();
}

// ==========================================
// 【v215 大厂架构新增】Subsystem 生命周期 — Initialize / Deinitialize
// ==========================================
//
// 为什么需要 Initialize:
//   - URoomStateService 是 GameInstanceSubsystem, Initialize 在 GI 创建时调一次
//   - GameInstance 跨 World 持久, 所以 Initialize 在 PIE 第一次启动时跑一次
//   - World 切换时 (StartGame → 切回 Lobby → 再 StartGame), World 自己创建/销毁
//   - RoomGameState 是 World 的一部分, World 销毁时它也销毁
//   - 所以 World 创建后再订阅 RoomGameState 的事件, World 销毁时取消订阅
//
// 大厂原则:
//   - Initialize 不直接订阅 RoomGameState (World 还没创建)
//   - Deinitialize 必须清掉所有动态多播订阅, 否则 World 销毁后回触发野指针
//
// 0 兜底:
//   - 没有 Initialize/Deinitialize = 链接器找不到符号 → LNK2001
//   - 这正是本次修复的原因
//
// 简化版 (本仓库现状):
//   - 当前 .h 中没有 RegisteredPlayerStates / RegisteredAIControllers 字段
//   - 也没有 HandleSettlementStateChanged / HandleTeamKillCountUpdated 回调函数
//   - 所以 Deinitialize 只清理 RoomGameState 上的订阅 (已知项)
//   - 真人群 / AI 群事件订阅走 ARoomPlayerState::OnStateChanged 等的"各自 +1 通知"路径,
//     不在 Subsystem 这边集中管理
//   - 后续如果加上集中管理, 应同时声明字段和回调 — 不能只挂一边
// ==========================================

void URoomStateService::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    // 【v215 大厂架构新增】订阅 World 生命周期 — 监听 World 创建/销毁
    //   - PostWorldInitialization: 新 World 创建后, 找到 RoomGameState 订阅其事件
    //   - PreWorldFinishDestroy: World 销毁前, 取消订阅
    //   - 大厂原则: GameInstanceSubsystem 跨 World 持久, 必须响应 World 切换
    //
    // 这里使用 lambda + 弱引用, 确保 Subsystem 被销毁后 lambda 不会触发野指针
    TWeakObjectPtr<URoomStateService> WeakThis(this);

    FWorldDelegates::OnPostWorldInitialization.AddLambda(
        [WeakThis](UWorld* World, const UWorld::InitializationValues IVS)
        {
            URoomStateService* Self = WeakThis.Get();
            if (!Self)
            {
                // 【v215 0 兜底】Subsystem 已销毁, 不处理
                return;
            }
            Self->SubscribeToWorldEvents();
        });

    FWorldDelegates::OnWorldBeginTearDown.AddLambda(
        [WeakThis](UWorld* World)
        {
            URoomStateService* Self = WeakThis.Get();
            if (!Self)
            {
                return;
            }
            Self->UnsubscribeFromWorldEvents();
        });

    UE_LOG(LogTemp, Log,
        TEXT("[RoomStateService] Initialize: 已订阅 World 生命周期 (PostWorldInitialization / OnWorldBeginTearDown)."));
}

void URoomStateService::Deinitialize()
{
    // 【v215 大厂架构新增】取消所有订阅
    //   - TWeakObjectPtr 保护 lambda 不会触发野指针
    //   - 当前实现只清理 RoomGameState 上的已知订阅项
    UnsubscribeFromWorldEvents();

    Super::Deinitialize();

    UE_LOG(LogTemp, Log,
        TEXT("[RoomStateService] Deinitialize: 已清空所有事件订阅."));
}

// ==========================================
// 【v215 大厂架构新增】SubscribeToWorldEvents / UnsubscribeFromWorldEvents 实现
// ==========================================
//
// 为什么需要这两个方法:
//   - World 创建时 (PostWorldInitialization), RoomGameState 跟着创建
//   - World 销毁时 (OnWorldBeginTearDown), RoomGameState 跟着销毁
//   - 每次都要重新订阅/取消订阅 (RoomGameState 是 World-scoped)
//
// 大厂原则:
//   - SubscribeToWorldEvents 必须找当前 World 的 RoomGameState 并订阅其事件
//   - UnsubscribeFromWorldEvents 必须清掉所有 RoomGameState 上的订阅
//   - 0 兜底: World 没创建 → Log Error, 不静默
//
// 当前简化:
//   - RoomGameState 上的 OnSettlementStateChanged / OnTeamKillCountUpdated 在 .h 里
//     没有 HandleSettlementStateChanged / HandleTeamKillCountUpdated 对应回调
//   - 真实事件链路走"RoomGameState::MulticastRefreshKillCount → ForwardPlayerSnapshotsChanged"
//     这种"上游显式调 Forward"路径, Subsystem 这边不用挂委托
//   - 所以 SubscribeToWorldEvents / UnsubscribeFromWorldEvents 当前是 no-op,
//     保留是为了后续扩展 (例如要 Subsystem 自动感知 World 状态时)
// ==========================================

void URoomStateService::SubscribeToWorldEvents()
{
    ARoomGameState* RoomGS = GetRoomGameState();
    if (!RoomGS)
    {
        // 【v215 0 兜底】World 还没创建 RoomGameState (例如 Login 地图), 不报错但跳过
        //   这是正常情况 — 玩家在 Login 页面时没有 RoomGameState
        //   进入战斗地图后再订阅 (通过 FWorldDelegates::OnPostWorldInitialization)
        return;
    }

    // 当前简化版: 不挂委托, 事件链路走"上游显式调 ForwardPlayerSnapshotsChanged"路径
    // 保留函数是为了:
    //   1. SubscribeToWorldEvents 在 Initialize 的 lambda 里被调, 接口稳定
    //   2. 后续如果改回"集中订阅分发"模式, 这里加 AddDynamic 即可
    UE_LOG(LogTemp, Verbose,
        TEXT("[RoomStateService] SubscribeToWorldEvents: 当前 no-op (事件走 ForwardPlayerSnapshotsChanged 链路)."));
}

void URoomStateService::UnsubscribeFromWorldEvents()
{
    UGameInstance* GI = GetGameInstance();
    if (!GI)
    {
        return;
    }

    UWorld* World = GI->GetWorld();
    if (!World)
    {
        return;
    }

    ARoomGameState* RoomGS = World->GetGameState<ARoomGameState>();
    if (!RoomGS)
    {
        return;
    }

    // 当前 no-op: 没有挂过委托就不需要 RemoveAll
    // 保留对称性: Subscribe/Unsubscribe 成对存在
}

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