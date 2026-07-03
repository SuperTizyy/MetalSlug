// Copyright (c) 2026.

#include "Data/AI/AIBehaviorConfigSO.h"

UAIBehaviorConfigSO::UAIBehaviorConfigSO()
{
    // 默认值已经在 USTRUCT 字段 initializer 中
}

float UAIBehaviorConfigSO::GetDifficultyScale(EAIDifficultyTier Tier) const
{
    switch (Tier)
    {
    case EAIDifficultyTier::Easy:   return 0.7f;
    case EAIDifficultyTier::Normal: return 1.0f;
    case EAIDifficultyTier::Hard:   return 1.4f;
    case EAIDifficultyTier::Insane: return 1.8f;
    default:                        return 1.0f;
    }
}

FAIPerceptionParams UAIBehaviorConfigSO::GetScaledPerception(EAIDifficultyTier Tier) const
{
    const float Scale = GetDifficultyScale(Tier);
    FAIPerceptionParams Out = Perception;
    // 难度提升视野
    Out.SightRadius    *= Scale;
    Out.LoseSightRadius = FMath::Max(Out.LoseSightRadius * Scale, Out.SightRadius + 100.f);
    Out.HearingRadius  *= Scale;
    return Out;
}

FAICombatParams UAIBehaviorConfigSO::GetScaledCombat(EAIDifficultyTier Tier) const
{
    const float Scale = GetDifficultyScale(Tier);
    FAICombatParams Out = Combat;
    Out.Damage           *= Scale;
    Out.AttackRange      *= Scale;
    Out.AttackCooldown   /= FMath::Max(Scale, KINDA_SMALL_NUMBER); // 高难度冷却更短
    // 【P0 v5 2026.07.07】OverrideBTDistance 字段已从 FAICombatParams 删除 (用户硬要求)
    //   距离语义统一到 AttackRange (一值三用: AI 停下距离 / 攻击触发距离 / NearbyThreat 阈值)
    //   见 ABaseAIController::UpdateNearbyThreatByDistance 与 TickChaseFallback
    return Out;
}

FAIMovementParams UAIBehaviorConfigSO::GetScaledMovement(EAIDifficultyTier Tier) const
{
    const float Scale = GetDifficultyScale(Tier);
    FAIMovementParams Out = Movement;
    Out.WalkSpeed *= Scale;
    Out.RunSpeed  *= Scale;
    return Out;
}
