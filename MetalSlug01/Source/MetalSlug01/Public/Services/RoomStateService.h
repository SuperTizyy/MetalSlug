// ==========================================
// URoomStateService.h
// ==========================================
// 房间状态查询门面（View 唯一调用入口）
// 职责: 为 View 层屏蔽 RoomGameState/RoomPlayerState 的细节
// 架构: L2 Service Layer（CQRS - 读取端，与 URoomService 写入端互补）
// ==========================================

#pragma once

// UE 引擎核心最小化头文件
#include "CoreMinimal.h"
// 引入 UGameInstanceSubsystem 头文件
#include "Subsystems/GameInstanceSubsystem.h"
// 引入房间相关枚举（ERoomState/ERoomMatchMode — ERoomTeam 已于 2026.07.10 删除）
#include "Data/Enums/RoomEnums.h"
#include "GameplayTagContainer.h" // 【2026.07.10 P0 重构】FGameplayTag 阵营
// 自动生成的反射头文件
#include "RoomStateService.generated.h"

class ARoomGameState;
class ARoomPlayerState;
class ABaseAIController;

/**
 * 【v215 大厂架构新增】玩家快照变更事件委托
 *
 * 触发时机 (服务器 + 客户端均会触发):
 *   - AIController 击杀分数/击杀数/死亡数/助攻数 Replicated 字段变更 (OnRep_AIScoreboardData)
 *   - ARoomGameState::OnPlayerArrayChanged (真人进出房)
 *   - ARoomGameState::OnSettlementStateChanged (进入/离开结算状态)
 *   - ARoomGameState::OnTeamKillCountUpdated (队伍总击杀变更)
 *
 * 大厂原则 — 事件驱动 (替代 Tick 拉取):
 *   - View (UScoreboardWidget) 订阅此委托, 数据一变立刻刷新
 *   - 比 0.5s Tick 拉取延迟低 N 倍, 且不浪费 CPU
 *
 * 注意: 已进入结算冻结状态 (FrozenSnapshots) 后, 订阅方应忽略此事件
 *   - 冻结后所有刷新都从 FrozenSnapshots 读取, 不再触发新数据拉取
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnPlayerSnapshotsChangedDelegate);

/**
 * @struct FPlayerSnapshot
 * @brief 玩家状态快照（供 UI 显示，View 不感知 PlayerState）
 *
 * 设计目的:
 * - View 只读 POJO 数据，避免暴露 ARoomPlayerState 引用造成 View 误改
 * - 解耦 PlayerState 的 UPROPERTY 字段（随时可能重构）
 *
 * 【2026.07.11 v28】新增 bIsAI 字段 — 让 UI 区分渲染真人 vs AI 占位
 *   - 真人 (PlayerArray): bIsAI=false
 *   - AI 占位 (PendingAIQueue): bIsAI=true, SequenceID 用作显示顺序
 */
USTRUCT(BlueprintType)
struct FPlayerSnapshot
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "RoomStateService")
    FString PlayerName;

    /**
     * 【2026.07.10 P0 重构】阵营用 FGameplayTag 替代 ERoomTeam
     * 有效值: Faction.Offense / Faction.Defense
     */
    UPROPERTY(BlueprintReadOnly, Category = "RoomStateService")
    FGameplayTag FactionTag;

    UPROPERTY(BlueprintReadOnly, Category = "RoomStateService")
    bool bIsReady = false;

    UPROPERTY(BlueprintReadOnly, Category = "RoomStateService")
    bool bIsHost = false;

    /**
     * 【2026.07.11 v28】是否 AI 占位
     *   - true: 大厅阶段从 PendingAIQueue 来的 AI 占位
     *   - false: 真人玩家 (来自 PlayerArray)
     * 用途: UI 渲染时区分 (AI 显示 [AI] 前缀, 不显示准备按钮等)
     */
    UPROPERTY(BlueprintReadOnly, Category = "RoomStateService")
    bool bIsAI = false;

    /**
     * 【2026.07.11 v28】入队序号 (AI 占位专用, 真人恒为 0)
     * 大厂原则: UI 显示顺序严格按入队顺序, 不被 ListIndex 影响
     */
    UPROPERTY(BlueprintReadOnly, Category = "RoomStateService")
    int32 SequenceID = 0;

    UPROPERTY(BlueprintReadOnly, Category = "RoomStateService")
    int32 Score = 0;

    UPROPERTY(BlueprintReadOnly, Category = "RoomStateService")
    int32 Kills = 0;

    UPROPERTY(BlueprintReadOnly, Category = "RoomStateService")
    int32 Deaths = 0;

    UPROPERTY(BlueprintReadOnly, Category = "RoomStateService")
    int32 Assists = 0;

    UPROPERTY(BlueprintReadOnly, Category = "RoomStateService")
    FString SelectedCharacterID;

    UPROPERTY(BlueprintReadOnly, Category = "RoomStateService")
    FString SelectedWeaponID1;

    UPROPERTY(BlueprintReadOnly, Category = "RoomStateService")
    FString SelectedWeaponID2;

    /**
     * 【v52 P0】第 3 个武器槽位 (近战武器)
     *
     * 大厂原则: 与 ARoomPlayerState::SelectedWeaponID3 对称,
     * 真理源 = 大厅运行时态写入, 由 URoomStateService::BuildSnapshot 同步复制
     *
     * 用途:
     *   - HUD 可查询"玩家当前槽位的近战武器"
     *   - Scoreboard 显示"主+副+近战"全部 3 把
     */
    UPROPERTY(BlueprintReadOnly, Category = "RoomStateService")
    FString SelectedWeaponID3;
};

/**
 * @struct FMatchSnapshot
 * @brief 比赛状态快照（供 UI 显示）
 */
USTRUCT(BlueprintType)
struct FMatchSnapshot
{
    GENERATED_BODY()

    UPROPERTY(BlueprintReadOnly, Category = "RoomStateService")
    ERoomMatchMode MatchMode = ERoomMatchMode::Melee;

    UPROPERTY(BlueprintReadOnly, Category = "RoomStateService")
    int32 RemainingSeconds = 0;

    UPROPERTY(BlueprintReadOnly, Category = "RoomStateService")
    int32 CurrentRound = 0;

    /**
     * 【v92 大厂架构重构】总局数 (UI 显示用, 替换旧的 CurrentRound UI 用途)
     *
     * 大厂原则 — 职责分离:
     *   - CurrentRound: 内部计数器 (Subsystem 使用)
     *   - TotalRounds: UI 真理源 (UI 静态显示 "总局数：xx")
     */
    UPROPERTY(BlueprintReadOnly, Category = "RoomStateService")
    int32 TotalRounds = 5;

    UPROPERTY(BlueprintReadOnly, Category = "RoomStateService")
    int32 AttackerTotalKills = 0;

    UPROPERTY(BlueprintReadOnly, Category = "RoomStateService")
    int32 DefenderTotalKills = 0;

    UPROPERTY(BlueprintReadOnly, Category = "RoomStateService")
    int32 AttackerWins = 0;

    UPROPERTY(BlueprintReadOnly, Category = "RoomStateService")
    int32 DefenderWins = 0;

    UPROPERTY(BlueprintReadOnly, Category = "RoomStateService")
    FString HostPlayerName;
};

/**
 * @class URoomStateService
 * @brief 房间状态查询门面（GameInstance 子系统）
 *
 * 【大厂标准架构：CQRS 读取端】
 * - URoomService: 写入端（业务编排/RPC 路由）
 * - URoomStateService: 读取端（聚合快照/隐藏 PlayerState/GameState 细节）
 *
 * 【为什么需要它】
 * - View（RoomInsidePage）当前直接 Cast PlayerState/GameState，违反"View 不感知数据层"
 * - 替换为：View 调 RoomStateService.GetMatchSnapshot() / GetPlayerSnapshots()
 * - 未来若引入 ViewModel 中转，本类就是 ViewModel 的数据源
 *
 * 【大厂对应】
 * - Riot: GameDataService
 * - Lyra: ULyraPlayerStateExtensions
 */
UCLASS()
class METALSLUG01_API URoomStateService : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    /**
     * @brief 静态访问器
     */
    UFUNCTION(BlueprintCallable, Category = "RoomStateService", meta = (WorldContext = "WorldContextObject"))
    static URoomStateService* Get(const UObject* WorldContextObject);

    // ==========================================
    // 比赛级查询
    // ==========================================

    /**
     * @brief 获取当前比赛的整体快照
     * @return FMatchSnapshot 比赛数据（不在线/无 GameState 时返回默认值）
     */
    UFUNCTION(BlueprintPure, Category = "RoomStateService")
    FMatchSnapshot GetMatchSnapshot() const;

    /**
     * @brief 当前是否处于 InRoom/Battleing 状态（即有 RoomGameState）
     */
    UFUNCTION(BlueprintPure, Category = "RoomStateService")
    bool IsInRoom() const;

    /**
     * @brief 获取倒计时剩余秒数（封装 GameState::GetMatchRemainingSeconds）
     */
    UFUNCTION(BlueprintPure, Category = "RoomStateService")
    int32 GetMatchRemainingSeconds() const;

    // ==========================================
    // 玩家级查询
    // ==========================================

    /**
     * 【2026.07.10 P0 重构】获取攻方阵营 (Faction.Offense) 所有**真人**玩家的快照列表
     *
     * 【2026.07.11 v29 大厂架构修订】只返回真人 — 不再合并 AI 占位
     * 业务规则: 真人 + AI 是两条独立数据流, UI 渲染走 KnownPlayerStates + GetPendingAIInFaction 显式合并
     *
     * 旧 (v28) 错误做法:
     *   合并真人 + AI 占位 → 测试模式 (MockLogin) PlayerArray 为空 → 函数返回空 → Box 不显示
     *
     * 新 (v29) 做法:
     *   只返回真人 (GS->PlayerArray.GetPlayersInFaction) — 测试模式返回空也是合法真实状态
     *   AI 占位查询走独立 API: GM->GetPendingAIInFaction(阵营) (UI 自己合并)
     */
    UFUNCTION(BlueprintPure, Category = "RoomStateService")
    TArray<FPlayerSnapshot> GetAttackFactionSnapshots() const;

    /**
     * 【2026.07.10 P0 重构】获取守方阵营 (Faction.Defense) 所有**真人**玩家的快照列表
     *
     * 【2026.07.11 v29 大厂架构修订】只返回真人 — 不再合并 AI 占位
     * 业务规则: 真人 + AI 是两条独立数据流, UI 渲染走 KnownPlayerStates + GetPendingAIInFaction 显式合并
     *
     * 旧 (v28) 错误做法:
     *   合并真人 + AI 占位 → 测试模式 (MockLogin) PlayerArray 为空 → 函数返回空 → Box 不显示
     *
     * 新 (v29) 做法:
     *   只返回真人 (GS->PlayerArray.GetPlayersInFaction) — 测试模式返回空也是合法真实状态
     *   AI 占位查询走独立 API: GM->GetPendingAIInFaction(阵营) (UI 自己合并)
     */
    UFUNCTION(BlueprintPure, Category = "RoomStateService")
    TArray<FPlayerSnapshot> GetDefenseFactionSnapshots() const;

    /**
     * 【2026.07.11 v29】显式合并真人 + AI 占位的快照 (供需要"总人数"的查询方用)
     *
     * 大厂原则: 显式 API > 隐式参数 (可读性优先, 不会出现"忘了设 bIncludeAI" 的 bug)
     *
     * @param FactionTag 阵营
     * @return 真人 + AI 占位的快照 (大厅阶段才有 AI 数据, 战斗阶段 AI 已 Spawn, 队列为空)
     */
    UFUNCTION(BlueprintPure, Category = "RoomStateService")
    TArray<FPlayerSnapshot> GetFactionSnapshotsWithAI(FGameplayTag FactionTag) const;

    /**
     * @brief 获取本地玩家的快照（用于 UI 高亮自己的状态）
     */
    UFUNCTION(BlueprintPure, Category = "RoomStateService")
    FPlayerSnapshot GetLocalPlayerSnapshot() const;

    /**
     * @brief 本地玩家是否已准备
     */
    UFUNCTION(BlueprintPure, Category = "RoomStateService")
    bool IsLocalPlayerReady() const;

    /**
     * @brief 本地玩家是否房主
     */
    UFUNCTION(BlueprintPure, Category = "RoomStateService")
    bool IsLocalPlayerHost() const;

    /**
     * @brief 本地玩家当前所属阵营 (FGameplayTag)
     * 【2026.07.10 P0 重构】替代 GetLocalPlayerTeam(ERoomTeam)
     */
    UFUNCTION(BlueprintPure, Category = "RoomStateService")
    FGameplayTag GetLocalPlayerFaction() const;

    // ==========================================
    // 队伍统计查询
    // ==========================================

    /**
     * @brief 攻方 (Faction.Offense) 已准备人数
     */
    UFUNCTION(BlueprintPure, Category = "RoomStateService")
    int32 GetAttackReadyCount() const;

    /**
     * @brief 守方 (Faction.Defense) 已准备人数
     */
    UFUNCTION(BlueprintPure, Category = "RoomStateService")
    int32 GetDefenseReadyCount() const;

    // ==========================================
    // 【v215 大厂架构新增】事件驱动刷新
    // ==========================================

    /**
     * 【v215 大厂架构新增】玩家快照变更事件
     *
     * View (UScoreboardWidget) 订阅此事件实现增量刷新, 替代 0.5s Tick 拉取
     *
     * 触发时机:
     *   - AI 计分板数据 Replicated (OnRep_AIScoreboardData → Broadcast)
     *   - 真人 PlayerArray 变更 (OnPlayerArrayChanged → Broadcast)
     *   - 进入/离开结算状态 (OnSettlementStateChanged → Broadcast)
     *   - 队伍总击杀变更 (OnTeamKillCountUpdated → Broadcast)
     *
     * 大厂原则 — 单一真理源 + 事件驱动:
     *   - 数据源 (AIC/PS/GS) 自己变更时主动通知
     *   - View 不需要轮询, 也不依赖 Tick 拉取
     *   - 旧做法 (NativeTick 0.5s 拉取) 在 60Hz 渲染下浪费 CPU, 且不及时
     */
    UPROPERTY(BlueprintAssignable, Category = "RoomStateService")
    FOnPlayerSnapshotsChangedDelegate OnPlayerSnapshotsChanged;

    /**
     * 【v215 大厂架构新增】主动 Broadcast 一次 OnPlayerSnapshotsChanged
     *
     * 使用场景:
     *   - ScoreboardWidget 在 NativeConstruct 时手动调用, 触发首次全量刷新
     *   - 外部代码 (如 HUDWidget) 在状态切换时主动触发, 保证 UI 同步
     *
     * 大厂原则 — 显式优于默认:
     *   - Broadcast 入口集中在一处, 不允许外部代码直接 Broadcast 委托
     *   - 0 兜底: GameInstance 不存在时 Log Error + return false
     */
    UFUNCTION(BlueprintCallable, Category = "RoomStateService")
    bool BroadcastPlayerSnapshotsChanged();

public:
    /**
     * 【v215 大厂架构新增】事件转发器 (公开 API) — 把上游事件统一转发给 OnPlayerSnapshotsChanged
     *
     * 大厂原则 — 单一事件出口:
     *   - 所有上游事件 → ForwardPlayerSnapshotsChanged → OnPlayerSnapshotsChanged.Broadcast
     *   - 严禁 View 直接订阅上游事件 (会让 View 知道数据层细节)
     *   - 公开此 API 是为了让 ABaseAIController / ARoomPlayerState / ARoomGameState
     *     在它们的 OnRep_* 和服务器 AddXxx 函数中触发一次转发, 不让它们直接 Broadcast
     *     URoomStateService 的 OnPlayerSnapshotsChanged (那会破坏封装)
     */
    UFUNCTION()
    void ForwardPlayerSnapshotsChanged();

    /**
     * 【v215 大厂架构新增】订阅当前 World 的所有上游事件
     *
     * 上游事件源:
     *   - ARoomGameState::OnSettlementStateChanged (进入/离开结算)
     *   - ARoomGameState::OnTeamKillCountUpdated (队伍总击杀变更)
     *   - ARoomPlayerState::OnStateChanged (真人准备/阵营变更)
     *   - ARoomPlayerState::OnScoreboardDataChanged (真人计分板数据)
     *   - ABaseAIController::OnAIScoreboardDataChanged (AI 计分板数据 Replicated)
     *
     * 大厂原则:
     *   - 唯一订阅入口: Initialize 时调用一次, PIE World 切换时重新订阅
     *   - 0 兜底: GameState 为空时 Log Error + return, 不静默跳过
     *   - DRY: 每个事件都转发到 ForwardPlayerSnapshotsChanged, 不需要业务代码重复订阅
     */
    void SubscribeToWorldEvents();

    /**
     * 【v215 大厂架构新增】取消订阅当前 World 的所有上游事件
     *
     * 时机: Deinitialize / World 销毁 / 切换房间
     *
     * 大厂原则:
     *   - 唯一解绑入口: 防止重复订阅导致的回调泄漏
     *   - 0 兜底: 已解绑的状态允许 no-op (幂等)
     */
    void UnsubscribeFromWorldEvents();

public:
    /**
     * 【v215 大厂架构新增】Subsystem 初始化 — 订阅 World 生命周期
     *
     * 大厂原则:
     *   - GameInstance Subsystem 的 Initialize 只调一次 (GameInstance 生命周期内)
     *   - World 切换时需要重新订阅 — 通过 FWorldDelegates 处理
     */
    virtual void Initialize(FSubsystemCollectionBase& Collection) override;

    /**
     * 【v215 大厂架构新增】Subsystem 销毁 — 取消所有订阅
     */
    virtual void Deinitialize() override;

private:
    /**
     * @brief 内部辅助: 获取当前 World 的 RoomGameState
     */
    ARoomGameState* GetRoomGameState() const;

    /**
     * @brief 内部辅助: 获取当前本地玩家的 PlayerState
     */
    ARoomPlayerState* GetLocalPlayerState() const;

    /**
     * @brief 内部辅助: 把一个 ARoomPlayerState 转成 FPlayerSnapshot
     */
    static FPlayerSnapshot BuildSnapshot(ARoomPlayerState* PS, bool bIsHost);

    /**
     * 【2026.07.11 v28】内部辅助: 把一个 FPendingAIEntry 转成 FPlayerSnapshot
     * 大厂原则: 字段一一对应, 不在调用方各自拼凑
     */
    static FPlayerSnapshot BuildAISnapshot(const struct FPendingAIEntry& Entry);

    /**
     * 【v202.0 大厂架构新增】从运行时 AIC 读取并构建 AI 计分快照
     *
     * 与 BuildAISnapshot (FPendingAIEntry) 不同: 本函数读 AIC 的 Replicated 字段
     *   - 战斗阶段 AI 已 Spawn → 读 AIC.AIScore/AIKills/AIDeaths/AIAssists (Replicated)
     *   - 大厅阶段 AIC 不存在 → 走 BuildAISnapshot(FPendingAIEntry) 路径
     *
     * 大厂原则 — 单一真理源: AIC.CachedFactionTag (运行时) 替代 FPendingAIEntry.FactionTag (配置期)
     *
     * @param AIController 已 Spawn 的 AI 控制器 (nullptr 时返回空 Snapshot)
     * @return FPlayerSnapshot (bIsAI=true, 战斗阶段数据)
     */
    static FPlayerSnapshot BuildAISnapshotFromController(class ABaseAIController* AIController);

    /**
     * 【2026.07.11 v29 大厂架构重构】内部统一查询 — 单一真理源分离
     *
     * 历史 (v28 错误做法):
     *   把真人 + AI 占位合并 → UI 拿到空 (测试模式 PlayerArray 没填) → Box 不显示 (用户反馈 bug)
     *
     * 新 (v29) 原则:
     *   - 单一真理源: 真人 (PlayerArray) 与 AI 占位 (PendingAIQueue) 是两条独立流
     *   - 显式意图: bIncludeAI 参数明确"我要哪条"
     *   - 零兜底: 默认 bIncludeAI=false (旧 API 兼容, 只真人)
     *
     * 设计动机:
     *   - UI 渲染 (URoomInsidePage): 真人用 KnownPlayerStates (事件订阅流), AI 用 GM->GetPendingAIInFaction
     *     不依赖本函数 (v29 经验: 合并函数易在测试模式下数据空)
     *   - ScoreboardWidget / MatchReadyCheck: 只关心真人 (bIncludeAI=false)
     *   - 需要 AI 数据: 显式调 GM->GetPendingAIInFaction(阵营), 不隐式合并
     *
     * @param bIncludeAI true=真人+AI, false=只真人 (UI 不该走 true)
     */
    TArray<FPlayerSnapshot> GetFactionSnapshotsInternal(FGameplayTag FactionTag, bool bIncludeAI) const;
};