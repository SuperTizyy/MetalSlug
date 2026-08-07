// 版权声明：在项目设置的描述页面填写您的版权信息。

#include "Systems/Settlement/SettlementSnapshotSubsystem.h"

// ==========================================
// 【v216 大厂架构新增】结算页面快照子系统
// ==========================================

// ==========================================
// Subsystem 生命周期
// ==========================================

void USettlementSnapshotSubsystem::Initialize(FSubsystemCollectionBase& Collection)
{
    Super::Initialize(Collection);

    UE_LOG(LogTemp, Log, TEXT("[SettlementSnapshotSubsystem] Initialize: 跨地图持久, 已就绪."));

    // 大厂原则 — 不在 Initialize 时清空数据:
    //   GameInstance 可能被复用 (PIE 多次启动), 如果上次有未消费的快照, 应该被新一次 WriteSnapshot 覆盖,
    //   而不是在 Initialize 时主动 Clear. Clear 是 ConsumeSnapshot 的语义.
}

void USettlementSnapshotSubsystem::Deinitialize()
{
    // 大厂原则 — Deinitialize 不清空:
    //   GameInstance 销毁时 Subsystem 才销毁. 此时 GameInstance 也跟着销毁, 不需要手动清理.
    //   如果此处清空, 则对运行时再次访问构成 race condition.

    UE_LOG(LogTemp, Log, TEXT("[SettlementSnapshotSubsystem] Deinitialize: 跨地图快照子系统已销毁."));

    Super::Deinitialize();
}

// ==========================================
// 静态访问器
// ==========================================

USettlementSnapshotSubsystem* USettlementSnapshotSubsystem::Get(const UObject* WorldContextObject)
{
    if (!WorldContextObject)
    {
        // 大厂原则 — 0 兜底:
        //   WorldContextObject 为空说明调用方没传, 直接 return nullptr, 不做任何 fallback.
        UE_LOG(LogTemp, Error,
            TEXT("[SettlementSnapshotSubsystem] Get: WorldContextObject 为空. 修复: 调用方必须传入任意 World 上下文对象."));
        return nullptr;
    }

    UWorld* World = GEngine ? GEngine->GetWorldFromContextObject(WorldContextObject, EGetWorldErrorMode::ReturnNull) : nullptr;
    if (!World)
    {
        UE_LOG(LogTemp, Error,
            TEXT("[SettlementSnapshotSubsystem] Get: 无法从 WorldContextObject 获取 World."));
        return nullptr;
    }

    UGameInstance* GameInstance = World->GetGameInstance();
    if (!GameInstance)
    {
        UE_LOG(LogTemp, Error,
            TEXT("[SettlementSnapshotSubsystem] Get: World '%s' 没有 GameInstance."),
            *World->GetName());
        return nullptr;
    }

    USettlementSnapshotSubsystem* SnapshotSub = GameInstance->GetSubsystem<USettlementSnapshotSubsystem>();
    if (!SnapshotSub)
    {
        UE_LOG(LogTemp, Error,
            TEXT("[SettlementSnapshotSubsystem] Get: GameInstance '%s' 没有 USettlementSnapshotSubsystem. "
                 "修复: 检查 Project Settings → Subsystems 是否注册, 或类是否标记 UCLASS."),
            *GameInstance->GetName());
        return nullptr;
    }

    return SnapshotSub;
}

// ==========================================
// 写入
// ==========================================

void USettlementSnapshotSubsystem::WriteSnapshot(const FFinalSettlementSnapshot& InSnapshot)
{
    // 大厂原则 — 不静默创建:
    //   如果 InSnapshot.bIsValid=false (调用方忘记设置), Log Warning + 不写入.
    //   但不强制 return — 让调用方可以部分写入 (后续 UpdateSnapshotWins 增量).
    if (!InSnapshot.bIsValid)
    {
        UE_LOG(LogTemp, Warning,
            TEXT("[SettlementSnapshotSubsystem] WriteSnapshot: InSnapshot.bIsValid=false, 可能是部分写入. "
                 "调用方通常应在写入前设置 bIsValid=true."));
    }

    CachedSnapshot = InSnapshot;

    // 写入时间戳 (调试用)
    if (UWorld* World = GetWorld())
    {
        CachedSnapshot.WriteTimestamp = World->GetTimeSeconds();
    }

    UE_LOG(LogTemp, Display,
        TEXT("[SettlementSnapshotSubsystem] WriteSnapshot: AttackerWins=%d, DefenderWins=%d, "
             "AttackerEntries=%d, DefenderEntries=%d, bIsValid=%d. 跨地图持久."),
        CachedSnapshot.AttackerWins,
        CachedSnapshot.DefenderWins,
        CachedSnapshot.AttackerEntries.Num(),
        CachedSnapshot.DefenderEntries.Num(),
        CachedSnapshot.bIsValid ? 1 : 0);

    // 大厂架构 — 事件驱动:
    OnSettlementSnapshotWritten.Broadcast();
}

void USettlementSnapshotSubsystem::UpdateSnapshotWins(int32 InAttackerWins, int32 InDefenderWins)
{
    // 大厂原则 — 0 兜底:
    //   当前快照 bIsValid=false 说明 MulticastEnterSettlement 没跑或 ConsumeSnapshot 已经被消费.
    //   此时调用 UpdateSnapshotWins 是错误流程, Log Error + return, 不静默创建.
    if (!CachedSnapshot.bIsValid)
    {
        UE_LOG(LogTemp, Error,
            TEXT("[SettlementSnapshotSubsystem] UpdateSnapshotWins: 当前快照 bIsValid=false, "
                 "调用顺序错误. 修复: 必须先调 WriteSnapshot (MulticastEnterSettlement 路径), 再调 UpdateSnapshotWins "
                 "(MulticastShowFinalSettlement 路径). AttackerWins=%d, DefenderWins=%d 不更新."),
            InAttackerWins, InDefenderWins);
        return;
    }

    CachedSnapshot.AttackerWins = InAttackerWins;
    CachedSnapshot.DefenderWins = InDefenderWins;

    UE_LOG(LogTemp, Display,
        TEXT("[SettlementSnapshotSubsystem] UpdateSnapshotWins: AttackerWins=%d, DefenderWins=%d."),
        InAttackerWins, InDefenderWins);

    OnSettlementSnapshotWritten.Broadcast();
}

// ==========================================
// 读取 (Consume 语义)
// ==========================================

bool USettlementSnapshotSubsystem::ConsumeSnapshot(FFinalSettlementSnapshot& OutSnapshot)
{
    // 大厂原则 — Consume 语义:
    //   读后立即清空, 防止下次进入结算页面时拿到旧数据.
    //   0 兜底: bIsValid=false 时 out 保持默认, return false, 不允许"读空快照然后假装成功".

    if (!CachedSnapshot.bIsValid)
    {
        UE_LOG(LogTemp, Error,
            TEXT("[SettlementSnapshotSubsystem] ConsumeSnapshot: 没有待消费的快照. "
                 "修复: 检查 RPC 链路 MulticastEnterSettlement → WriteSnapshot 是否被调用."));
        OutSnapshot = FFinalSettlementSnapshot{};
        return false;
    }

    // 大厂原则 — 完整拷贝 (OutSnapshot 是值类型, 默认 copy):
    OutSnapshot = CachedSnapshot;

    UE_LOG(LogTemp, Display,
        TEXT("[SettlementSnapshotSubsystem] ConsumeSnapshot: 已消费快照 AttackerWins=%d, DefenderWins=%d, "
             "AttackerEntries=%d, DefenderEntries=%d."),
        OutSnapshot.AttackerWins,
        OutSnapshot.DefenderWins,
        OutSnapshot.AttackerEntries.Num(),
        OutSnapshot.DefenderEntries.Num());

    // 立即清空 (Consume 语义):
    CachedSnapshot = FFinalSettlementSnapshot{};

    return true;
}