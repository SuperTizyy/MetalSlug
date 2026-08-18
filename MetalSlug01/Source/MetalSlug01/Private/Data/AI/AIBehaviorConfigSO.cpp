// Copyright (c) 2026.
//
// ========================================================================
// AIBehaviorConfigSO.cpp — AI 行为配置 DataAsset 实现文件
// ========================================================================
//
// 本文件实现 UAIBehaviorConfigSO 的难度缩放逻辑:
//   - GetDifficultyScale: 按档位返回缩放系数 (Easy/Normal/Hard/Insane)
//   - GetScaledPerception: 难度系数作用于视野/听觉半径
//   - GetScaledCombat: 难度系数作用于伤害/攻击距离/攻击冷却
//   - GetScaledMovement: 难度系数作用于移动速度 (漫游半径不缩放,语义不同)
//
// 大厂原则:
//   - 单一真理源: ConfigSO.Combat 是攻击参数唯一来源,所有 BT/BTTask 必须读这里
//   - 数据驱动: 不在代码中硬编码阈值,策划可在编辑器调整 + 难度系数联动
// ========================================================================

#include "Data/AI/AIBehaviorConfigSO.h"

// 构造函数: 默认值已经在 USTRUCT 字段 initializer 中声明
UAIBehaviorConfigSO::UAIBehaviorConfigSO()
{
    // 默认值已经在 USTRUCT 字段 initializer 中
}

// ============================================
// 【v54.3 删除】GetDefaultWeaponRowNames 函数已彻底删除
// ============================================
//
// 删除原因 (用户决策 2026.07.16):
//   - 真理源从 DefaultWeaponRowName (FName + DT_WeaponInfo 中间层) 改为 DefaultWeaponClass (TSoftClassPtr<ABaseWeapon>)
//   - 编辑器从 Dropdown 改为资产选择器, 不再需要 GetRowNames 回调
//   - 单一真理源: 一个字段直接决定武器 BP, 不需要查 DT 反查
// ============================================

// ============================================
// 【难度缩放 API】GetDifficultyScale / GetScaled* 系列
// 职责: 根据 EAIDifficultyTier 档位返回缩放后的配置副本
// 大厂原则: 返回副本 (Out),不修改 ConfigSO 原值,避免运行时污染数据资产
// ============================================

/**
 * @brief 根据难度档位返回线性缩放系数
 * @param Tier 难度档位枚举 (Easy=0.7 / Normal=1.0 / Hard=1.4 / Insane=1.8)
 * @return 缩放系数 (Normal 为基线 1.0)
 * @note 未知档位默认返回 1.0 (Normal),保证未知输入不会产生极端放大
 */
float UAIBehaviorConfigSO::GetDifficultyScale(EAIDifficultyTier Tier) const
{
    // 按档位返回难度缩放系数 — 单一真理源,BT/Service 都从这里派生
    switch (Tier)
    {
    case EAIDifficultyTier::Easy:   return 0.7f;   // 简单:各项数值降为 70%
    case EAIDifficultyTier::Normal: return 1.0f;   // 普通:基线
    case EAIDifficultyTier::Hard:   return 1.4f;   // 困难:放大 40%
    case EAIDifficultyTier::Insane: return 1.8f;   // 疯狂:放大 80%
    default:                        return 1.0f;   // 未知档位回退 Normal,绝不允许返回 0
    }
}

/**
 * @brief 难度缩放后的感知参数 (视野半径 / 听觉半径)
 * @param Tier 难度档位
 * @return 副本,LoseSightRadius 保证 >= SightRadius + 100(防止 Lose 比 See 早触发)
 * @note 大厂原则: 不修改原 Perception 字段,返回 Out 副本
 */
FAIPerceptionParams UAIBehaviorConfigSO::GetScaledPerception(EAIDifficultyTier Tier) const
{
    const float Scale = GetDifficultyScale(Tier);  // 先取缩放系数
    FAIPerceptionParams Out = Perception;          // 拷贝原值(避免污染 ConfigSO)
    // 难度提升视野 — 三个半径都按 Scale 放大
    Out.SightRadius    *= Scale;
    // LoseSightRadius 必须大于 SightRadius + 100,否则 Lose 比 See 早触发导致感知抖动
    Out.LoseSightRadius = FMath::Max(Out.LoseSightRadius * Scale, Out.SightRadius + 100.f);
    Out.HearingRadius  *= Scale;
    return Out;
}

/**
 * @brief 难度缩放后的战斗参数 (伤害 / 攻击距离 / 攻击冷却)
 * @param Tier 难度档位
 * @return 副本,冷却值已除以 Scale(高难度冷却更短)
 * @note P0 v5: OverrideBTDistance 字段已删除,距离统一到 AttackRange
 */
FAICombatParams UAIBehaviorConfigSO::GetScaledCombat(EAIDifficultyTier Tier) const
{
    const float Scale = GetDifficultyScale(Tier);  // 取难度缩放
    FAICombatParams Out = Combat;                  // 拷贝原值
    Out.Damage           *= Scale;                 // 高难度伤害更高
    Out.AttackRange      *= Scale;                 // 高难度攻击距离更远
    Out.AttackCooldown   /= FMath::Max(Scale, KINDA_SMALL_NUMBER); // 高难度冷却更短(除而非乘)
    // 【P0 v5 2026.07.07】OverrideBTDistance 字段已从 FAICombatParams 删除 (用户硬要求)
    //   距离语义统一到 AttackRange (一值三用: AI 停下距离 / 攻击触发距离 / NearbyThreat 阈值)
    //   见 ABaseAIController::UpdateNearbyThreatByDistance 与 TickChaseFallback
    return Out;
}

/**
 * @brief 难度缩放后的移动参数 (行走速度)
 * @param Tier 难度档位
 * @return 副本,仅 WalkSpeed 被缩放;WanderRadius 不缩放
 * @note 大厂原则: WanderRadius 是地图配置参数(设计意图),不应被难度影响
 */
FAIMovementParams UAIBehaviorConfigSO::GetScaledMovement(EAIDifficultyTier Tier) const
{
    const float Scale = GetDifficultyScale(Tier);  // 取难度缩放
    FAIMovementParams Out = Movement;              // 拷贝原值
    Out.WalkSpeed *= Scale;                        // 高难度移动更快
    // WanderRadius 不缩放: 漫游半径是地图配置参数 (设计意图), 不应该被难度影响
    // AI 在战斗外随机漫游, 难度主要影响战斗行为, 不影响漫游范围
    return Out;
}
