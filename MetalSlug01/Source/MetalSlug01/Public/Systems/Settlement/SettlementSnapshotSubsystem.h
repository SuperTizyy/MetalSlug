// 版权声明：在项目设置的描述页面填写您的版权信息。

#pragma once

// ==========================================
// 【v216 大厂架构新增】结算页面快照子系统
// ==========================================
//
// 业务背景:
//   玩家进入结算页面时, 必须立即 OpenLevel 切到 L_Login 关卡.
//   切图后旧 RoomGameState / GameHUDWidget 会被销毁, 结算页面要在 L_Login 上显示.
//   旧的"冻结快照"机制 (v215) 仅存在 UScoreboardWidget 内存中, 切图后 widget 被销毁 → 数据丢失.
//
// 大厂架构 (v216):
//   - 单例 GameInstanceSubsystem, 跨地图持久 (生命周期 = GameInstance, 不随 World 销毁)
//   - 写入入口: RoomGameState::MulticastEnterSettlement_Implementation
//   - 读取入口: UScoreboardWidget::ApplyPendingSnapshot (在 L_Login 上构造时拉取)
//   - 0 兜底: 必须写入完整快照数据, 缺字段 Log Error
//   - 单一真理源: 进入结算 → 写入 Snapshot. 任何切图/重连不影响.
//
// 大厂原则 — 与 UScoreboardWidget::FreezeSnapshot 的区别:
//   - v215 FreezeSnapshot: widget 内存冻结, 切图后丢失 (旧房间关卡使用)
//   - v216 SettlementSnapshot: GameInstance 持久, 切图后保留 (L_Login 上使用)
//   - v216 不替代 v215, 而是在新地图上拉取 Snapshot 后, 仍然调 FreezeSnapshot 把数据再次冻结到 widget 内存
//

#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "Data/Enums/RoomEnums.h"  // ERoomMatchMode / EZombieRoundWinner
#include "SettlementSnapshotSubsystem.generated.h"

class ARoomPlayerState;
class ABaseAIController;

/**
 * @struct FFactionSnapshotEntry
 * @brief 阵营 + 玩家快照 (单一真理源)
 *
 * 不再依赖 ARoomPlayerState / ABaseAIController 指针 (切图后失效)
 * 只保存纯 POJO 数据, 跨地图持久, 跨 World 安全
 */
USTRUCT(BlueprintType)
struct FFactionSnapshotEntry
{
    GENERATED_BODY()

    /** 玩家/AI 名称 */
    UPROPERTY(BlueprintReadOnly, Category = "Settlement")
    FString DisplayName = TEXT("");

    /** 是否为 AI */
    UPROPERTY(BlueprintReadOnly, Category = "Settlement")
    bool bIsAI = false;

    /** 击杀数 */
    UPROPERTY(BlueprintReadOnly, Category = "Settlement")
    int32 Kills = 0;

    /** 死亡数 */
    UPROPERTY(BlueprintReadOnly, Category = "Settlement")
    int32 Deaths = 0;

    /** 助攻数 */
    UPROPERTY(BlueprintReadOnly, Category = "Settlement")
    int32 Assists = 0;

    /** 总得分 (击杀*1 + 助攻*0.5 等, 取决于业务规则) */
    UPROPERTY(BlueprintReadOnly, Category = "Settlement")
    int32 Score = 0;

    /** 是否为攻方 (true) / 守方 (false) */
    UPROPERTY(BlueprintReadOnly, Category = "Settlement")
    bool bIsAttacker = false;

    /** 阵营标签 (用于一致性校验 / 调试) */
    UPROPERTY(BlueprintReadOnly, Category = "Settlement")
    FString FactionTagName = TEXT("");
};

/**
 * @struct FFinalSettlementSnapshot
 * @brief 完整结算快照 (跨地图持久)
 *
 * 设计: 一次性写入, UScoreboardWidget 在 L_Login 上构造时一次性拉取.
 * 切图后冻结数据 = 此结构体 (而非依赖任何 UWorld 对象).
 */
USTRUCT(BlueprintType)
struct FFinalSettlementSnapshot
{
    GENERATED_BODY()

    /** 当前游戏模式 (刀战/生化) */
    UPROPERTY(BlueprintReadOnly, Category = "Settlement")
    ERoomMatchMode MatchMode = ERoomMatchMode::Melee;

    /** 攻方胜局数 */
    UPROPERTY(BlueprintReadOnly, Category = "Settlement")
    int32 AttackerWins = 0;

    /** 守方胜局数 */
    UPROPERTY(BlueprintReadOnly, Category = "Settlement")
    int32 DefenderWins = 0;

    /** 当局攻方击杀数 */
    UPROPERTY(BlueprintReadOnly, Category = "Settlement")
    int32 AttackerKills = 0;

    /** 当局守方击杀数 */
    UPROPERTY(BlueprintReadOnly, Category = "Settlement")
    int32 DefenderKills = 0;

    /** 当局胜负方 (刀战: Attacker/Mother vs Defender/Human) */
    UPROPERTY(BlueprintReadOnly, Category = "Settlement")
    EZombieRoundWinner RoundWinner = EZombieRoundWinner::None;

    /** 攻方阵营所有玩家 + AI 快照 */
    UPROPERTY(BlueprintReadOnly, Category = "Settlement")
    TArray<FFactionSnapshotEntry> AttackerEntries;

    /** 守方阵营所有玩家 + AI 快照 */
    UPROPERTY(BlueprintReadOnly, Category = "Settlement")
    TArray<FFactionSnapshotEntry> DefenderEntries;

    /** 写入时间戳 (调试用, 用于区分多次写入) */
    UPROPERTY(BlueprintReadOnly, Category = "Settlement")
    double WriteTimestamp = 0.0;

    /** 是否已写入 (UScoreboardWidget 用此判断是否拉取) */
    UPROPERTY(BlueprintReadOnly, Category = "Settlement")
    bool bIsValid = false;
};

/**
 * @class USettlementSnapshotSubsystem
 * @brief 结算快照持久化子系统 (跨地图)
 *
 * 大厂架构 (v216):
 *   - GameInstance 子系统, 跨地图持久
 *   - 单一写入入口: WriteSnapshot (服务器在 MulticastEnterSettlement 时调)
 *   - 单一读取入口: ConsumeSnapshot (UScoreboardWidget 在 L_Login 上构造时拉取)
 *   - 读取后清空 (Consume 语义), 防止下次进入结算页面时拿到旧数据
 *
 * 调用链 (大厂原则):
 *   服务器: MulticastEnterSettlement_Implementation
 *     → WriteSnapshot (此处写入, 跨地图持久)
 *     → OpenLevel(L_Login, ?offline)
 *     → RequestStateOnNextLoad(SettlementPage)
 *   客户端: MulticastEnterSettlement_Implementation (同一 RPC)
 *     → WriteSnapshot (跨地图持久)
 *     → OpenLevel(L_Login, ?offline)
 *     → RequestStateOnNextLoad(SettlementPage)
 *   新地图加载: HandlePostLoadMapWithWorld 触发 UIViewService::ShowPanel(SettlementPanel)
 *     → UScoreboardWidget::NativeConstruct
 *     → ConsumeSnapshot (一次性消费)
 *     → FreezeSnapshot (写入 widget 内存冻结)
 */
UCLASS()
class METALSLUG01_API USettlementSnapshotSubsystem : public UGameInstanceSubsystem
{
    GENERATED_BODY()

public:
    // ==========================================
    // Subsystem 生命周期
    // ==========================================

    virtual void Initialize(FSubsystemCollectionBase& Collection) override;
    virtual void Deinitialize() override;

    // ==========================================
    // 静态访问器 (大厂标准)
    // ==========================================

    /**
     * @brief 静态获取 SettlementSnapshotSubsystem
     * @param WorldContextObject 任意 World 上下文对象
     * @return 有效时返回 Subsystem, 否则 nullptr
     *
     * 调用方约定:
     *   - 返回前必须判空
     *   - 0 兜底: WorldContextObject 为空 → Log Error + return nullptr
     */
    UFUNCTION(BlueprintPure, Category = "Settlement", meta = (WorldContext = "WorldContextObject"))
    static USettlementSnapshotSubsystem* Get(const UObject* WorldContextObject);

    // ==========================================
    // 写入 (服务器 → 客户端, 同一 RPC 路径)
    // ==========================================

    /**
     * @brief 写入完整结算快照
     *
     * 写入时机: RoomGameState::MulticastEnterSettlement_Implementation (服务器和客户端都执行)
     * 大厂原则 — 0 兜底:
     *   - 重复写入时, 后写入覆盖前者 (幂等保护)
     *   - 写入后 bIsValid=true
     *
     * @param InSnapshot 完整快照数据
     */
    UFUNCTION(BlueprintCallable, Category = "Settlement")
    void WriteSnapshot(const FFinalSettlementSnapshot& InSnapshot);

    /**
     * @brief 仅更新胜负局数 (用于 3 秒后 MulticastShowFinalSettlement 时增量更新)
     *
     * 写入时机: RoomGameState::MulticastShowFinalSettlement_Implementation
     * 大厂原则:
     *   - 如果当前快照 bIsValid=false (MulticastEnterSettlement 没跑), Log Error + return, 不静默创建
     *   - 仅更新 AttackerWins / DefenderWins 字段, 其他字段保留
     *
     * @param InAttackerWins 攻方胜局数
     * @param InDefenderWins 守方胜局数
     */
    UFUNCTION(BlueprintCallable, Category = "Settlement")
    void UpdateSnapshotWins(int32 InAttackerWins, int32 InDefenderWins);

    // ==========================================
    // 读取 (UScoreboardWidget 在 L_Login 上构造时调用)
    // ==========================================

    /**
     * @brief 一次性消费快照 (读后清空)
     *
     * 大厂原则 — Consume 语义:
     *   - 读后立即清空 (bIsValid=false), 防止下次进入结算页面时拿到旧数据
     *   - UScoreboardWidget 拿到快照后立刻调 FreezeSnapshot 冻结到 widget 内存
     *
     * @param OutSnapshot 输出快照
     * @return 是否读到有效快照 (bIsValid=true 才算成功)
     */
    UFUNCTION(BlueprintCallable, Category = "Settlement")
    bool ConsumeSnapshot(FFinalSettlementSnapshot& OutSnapshot);

    /**
     * @brief 查看快照是否有效 (不消费)
     * @return true = 有待消费的快照, false = 空
     */
    UFUNCTION(BlueprintPure, Category = "Settlement")
    bool HasPendingSnapshot() const { return CachedSnapshot.bIsValid; }

    // ==========================================
    // 事件 (大厂架构 — 事件驱动)
    // ==========================================

    /**
     * 快照写入后触发 (MulticastEnterSettlement / MulticastShowFinalSettlement 都触发)
     * 监听者: USettlementSnapshotSubsystem 内部 / 调试 UI
     */
    DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnSettlementSnapshotWritten);
    UPROPERTY(BlueprintAssignable, Category = "Settlement")
    FOnSettlementSnapshotWritten OnSettlementSnapshotWritten;

private:
    /**
     * 缓存的快照 (跨地图持久, 在 ConsumeSnapshot 时清空)
     */
    UPROPERTY()
    FFinalSettlementSnapshot CachedSnapshot;
};
