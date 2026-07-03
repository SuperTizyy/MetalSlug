// Copyright (c) 2026.

#include "Data/AI/AIProfileAsset.h"
#include "Data/AI/AIBehaviorConfigSO.h"
#include "Engine/AssetManager.h"
#include "Engine/StreamableManager.h"

/**
 * 【P0 大厂架构 2026.07.06 19:25】攻击间隔的有效获取
 *
 * 优先级（短路求值）:
 *   1. Profile.AttackInterval > 0     → 用这个（策划最直观的位置）
 *   2. Profile.AttackInterval <= 0    → 用 ConfigSO 难度缩放后的 AttackCooldown
 *   3. ConfigSO 拿不到                 → 兜底 1.5s
 *
 * 兜底链设计意义:
 *   - 大厂常见做法: Profile 是策划第一接触面, ConfigSO 是底层数据, 难度表是运行时调节
 *   - 让 Profile 作为 "user override" 兜在最前面, 改动最直观
 *   - 没改 Profile 时走 ConfigSO 默认 (向后兼容, 老 DA 不需要重新配置)
 */
float UAIProfileAsset::GetEffectiveAttackInterval() const
{
    // 优先级 1: Profile 自己设了 (大于 0)
    if (AttackInterval > 0.0f)
    {
        return AttackInterval;
    }

    // 优先级 2: 回退 ConfigSO
    // 注: 难度缩放在 GetScaledCombat 内部完成, 这里需要 Controller 的难度档位
    //      但 Profile 是 DataAsset, 不依赖 Controller 实例
    //      所以这里直接读 ConfigSO 原始 AttackCooldown, 难度缩放由 AIController 的
    //      GetEffectiveAttackInterval (持有 Controller) 在调用方处理
    if (UAIBehaviorConfigSO* Config = const_cast<UAIProfileAsset*>(this)->LoadBehaviorConfigSync())
    {
        return Config->Combat.AttackCooldown;
    }

    // 优先级 3: 兜底
    return 1.5f;
}

UAIBehaviorConfigSO* UAIProfileAsset::LoadBehaviorConfigSync()
{
    if (BehaviorConfig.IsValid())
    {
        return BehaviorConfig.Get();
    }
    return BehaviorConfig.LoadSynchronous();
}

void UAIProfileAsset::LoadBehaviorConfigAsync(FOnAIBehaviorConfigLoaded OnLoaded)
{
    if (BehaviorConfig.IsNull())
    {
        return;
    }

    // 已加载则直接回调
    if (BehaviorConfig.IsValid())
    {
        if (OnLoaded.IsBound())
        {
            OnLoaded.Execute();
        }
        return;
    }

    // 异步加载 (cpp 内部构造一次性 FStreamableDelegate, 把 UE 强类型适配到我们的 DECLARE_DELEGATE)
    FStreamableManager& Streamable = UAssetManager::GetStreamableManager();
    TWeakObjectPtr<UAIProfileAsset> WeakThis(this);
    FStreamableDelegate StreamableDone = FStreamableDelegate::CreateLambda(
        [WeakThis, OnLoaded]()
        {
            if (OnLoaded.IsBound())
            {
                OnLoaded.Execute();
            }
        });
    Streamable.RequestAsyncLoad(BehaviorConfig.ToSoftObjectPath(), StreamableDone);
}

FPrimaryAssetId UAIProfileAsset::GetPrimaryAssetId() const
{
    static const FName AIProfileTypeName(TEXT("AIProfile"));
    const FName TagName = ProfileTag.IsValid() ? ProfileTag.GetTagName() : GetFName();
    return FPrimaryAssetId(AIProfileTypeName, TagName);
}
