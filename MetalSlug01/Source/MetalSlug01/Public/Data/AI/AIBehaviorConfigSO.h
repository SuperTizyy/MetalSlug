// Copyright (c) 2026.
//
// ========================================================================
// UAIBehaviorConfigSO — 关卡预放 AI 配置（单一真理源）
// ========================================================================
//
// 【v54.4 大厂架构重构】职责重新划分
//
// 旧架构 (v54.3):
   //   - DA_AIBehaviorConfig 里同时有 LevelPlacedBehaviorTree + LevelPlacedAIControllerClass + LevelPlacedWeaponClass
//   - 关卡预放 AI 和大厅 AI 都从同一个 ConfigSO 读
//   → 职责混乱: 一个表里既有「行为配置」(Combat/Perception) 又有「Spawn 配置」(BehaviorTree/AIController)
//
// 新架构 (v54.4):
//   ┌──────────────────────────────────────────────────────────────┐
//   │ 关卡预放 AI (Level 里拖的 BP_GruntAI)                      │
//   │                                                              │
//   │ BP_GruntAI.AIControllerClass = BP_MeleeAIController         │
//   │ BP_MeleeAIController.DefaultMeleeConfig = DA_AIBehavior...  │
//   │ DA_AIBehaviorConfig:                                        │
//   │   - BehaviorTree = BT_MeleeAI  ← 关卡预放 AI 的 BT        │
//   │   - LevelPlacedAIControllerClass = BP_MeleeAIController     │
//   │   - LevelPlacedWeaponClass = BP_Weapon_Knife               │
//   │   - Combat / Perception / Movement (行为参数)               │
//   └──────────────────────────────────────────────────────────────┘
//   ┌──────────────────────────────────────────────────────────────┐
//   │ 大厅 AI (房主从 UI 添加)                                    │
//   │                                                              │
//   │ GM_RoomGameMode.ModeRulesByMode[Melee]:                     │
//   │   - BehaviorTree = BT_MeleeAI  ← 大厅 AI 的 BT (按模式)    │
//   │   - AttackTeamFaction / DefenseTeamFaction                   │
//   │                                                              │
//   │ DA_AIBehaviorConfig:                                         │
//   │   - 仅有 Combat / Perception / Movement (行为参数)           │
//   │   - 没有 BehaviorTree (删了)                                 │
//   │   - 没有 LevelPlacedXxx 字段 (那是关卡预放的)                │
//   └──────────────────────────────────────────────────────────────┘
//
// 大厂原则:
//   - 单一职责: ConfigSO = 关卡预放 AI 的「行为参数」+「预放专属配置」
//   - 大厅 AI 的 BT 来源 = ModeRules.BehaviorTree (按模式)
//   - 关卡预放 AI 的 BT 来源 = ConfigSO.BehaviorTree (按 AI 类型)
//   - 职责分离: ConfigSO 不管大厅 AI 的 Spawn 配置
//
// ========================================================================

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "Systems/AI/AIBehaviorTypes.h"
#include "Weapons/BaseWeapon.h"
#include "AIBehaviorConfigSO.generated.h"

class UAISenseConfig_Sight;
class UAISenseConfig_Hearing;
class UEnvQuery;

/**
 * UAIBehaviorConfigSO
 *
 * 用途: 一类 AI 的「行为参数集」+「关卡预放 AI 专用配置」
 *
 * 【v54.4 重构后字段分类】
 *
 * ■ 行为参数（通用 — 所有 AI 共用）
 *   - Combat     : 攻击范围 / 冷却 / 伤害 / Hysteresis
 *   - Perception: 视野半径 / 角度
 *   - Movement   : 移动速度 / 加速度
 *   - Debug      : 调试开关
 *   - EQS        : 寻敌/寻掩体 EQS (Phase 3)
 *
 * ■ 关卡预放 AI 专用（仅给 Level 预置的 AI 用）
 *   - LevelPlacedBehaviorTree     : 关卡预放 AI 的行为树
 *   - LevelPlacedAIControllerClass: 关卡预放 AI 的 AIController
 *   - LevelPlacedWeaponClass     : 关卡预放 AI 的默认武器
 *
 * ■ 所有 AI 共用（不分来源）
 *   - SpawnInvincibilitySeconds: 复活无敌期
 *
 * 【不在本类中的配置】
 *   - 大厅 AI 的行为树: GM_RoomGameMode.ModeRulesByMode[Mode].BehaviorTree
 *   - 大厅 AI 的 Spawn : FPendingAIEntry (WeaponID/FactionTag 等在内存中)
 *
 * 设计原则:
 *   - 一个 ConfigSO = 一种 AI 类型 (MeleeGrunt / Zombie 等)
 *   - 关卡预放 AI: 在 Level 里摆 BP_GruntAI → 用自己的 ConfigSO → 行为树/武器都从 ConfigSO 来
 *   - 大厅 AI   : 由 ModeRules.BehaviorTree 决定行为树, ConfigSO 只提供行为参数
 *   - BT/BTTask 通过 (Owner->GetController<ABaseAIController>()->GetConfig()) 读
 */
UCLASS(BlueprintType)
class METALSLUG01_API UAIBehaviorConfigSO : public UDataAsset
{
    GENERATED_BODY()

public:
    UAIBehaviorConfigSO();

    // ========================================================================
    // 【v54.4 重构】行为参数（通用 — 所有 AI 共用）
    // ========================================================================

    /** 感知参数 */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Behavior|Perception",
        meta = (DisplayName = "Perception (感知参数 — 所有 AI 共用)"))
    FAIPerceptionParams Perception;

    /** 战斗参数 */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Behavior|Combat",
        meta = (DisplayName = "Combat (战斗参数 — 所有 AI 共用)"))
    FAICombatParams Combat;

    /** 移动参数 */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Behavior|Movement",
        meta = (DisplayName = "Movement (移动参数 — 所有 AI 共用)"))
    FAIMovementParams Movement;

    /** 调试参数 */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Behavior|Debug",
        meta = (DisplayName = "Debug (调试参数)"))
    FAIDebugParams Debug;

    /** 目标选择 EQS */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Behavior|EQS",
        meta = (AllowedClasses = "/Script/AIModule.EnvQuery",
                DisplayName = "FindTargetEQ (寻敌 EQS — Phase 3)"))
    TSoftObjectPtr<UEnvQuery> FindTargetEQ;

    /** 寻掩体 EQS */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Behavior|EQS",
        meta = (AllowedClasses = "/Script/AIModule.EnvQuery",
                DisplayName = "FindCoverEQ (寻掩体 EQS — Phase 3)"))
    TSoftObjectPtr<UEnvQuery> FindCoverEQ;

    /** 目标选择策略 */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Behavior|Combat",
        meta = (DisplayName = "HuntPolicy (目标选择策略)"))
    FAIHuntPolicy HuntPolicy;

    // ========================================================================
    // 【v54.4 重构】关卡预放 AI 专用配置
    // ========================================================================
    //
    // 用途: Level 里预置的 AI (BP_GruntAI) 在 Possess 时从 ConfigSO 读取
    // 大厅 AI (房主 UI 添加) 不读这些字段 — 走 ModeRules
    //
    // 编辑器配置路径:
    //   DA_AIBehaviorConfig_MeleeGrunt → LevelPlacedAI 分类 → 配这 3 个字段
    // ========================================================================

    /**
     * 【v54.4 大厂架构重构】关卡预放 AI 的行为树
     *
     * 职责边界:
     *   - 仅关卡预放 AI (Level 里摆的 BP) 读这个字段
     *   - 大厅 AI 不读: 走 GM_RoomGameMode.ModeRulesByMode[Mode].BehaviorTree
     *
     * 编辑器配置路径:
     *   DA_AIBehaviorConfig_XXX → LevelPlacedAI → LevelPlacedBehaviorTree
     *
     * 零兜底约定:
     *   - IsNull() → Log Error + 拒绝 RunBehaviorTree
     *   - 必须配置, 不允许留空
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "LevelPlacedAI",
        meta = (AllowedClasses = "/Script/AIModule.BehaviorTree",
                DisplayName = "LevelPlacedBehaviorTree (关卡预放 AI 专用 — Level 预置 AI 的行为树)"))
    TSoftObjectPtr<UBehaviorTree> LevelPlacedBehaviorTree;

    /**
     * 【v54.4 大厂架构重构】关卡预放 AI 的默认 AIController Class
     *
     * 用途:
     *   - 关卡预放 AI (Level 中放置的 BP_Pawn) 在 Spawn 时没有「调用方」
     *     所以必须在 ConfigSO 里指定 AIController 类型 (例如 BP_MeleeAIController)
     *   - 大厅 AI 不读这个字段: SpawnAIInternal 从 Request.AIPawnClass 派生 AIController
     *
     * 编辑器配置路径:
     *   DA_AIBehaviorConfig_XXX → LevelPlacedAI → LevelPlacedAIControllerClass
     *
     * 零兜底约定:
     *   - IsNull() → Log Error + 拒绝 Spawn
     *   - 必须配置, 例如 BP_MeleeAIController
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "LevelPlacedAI",
        meta = (AllowAbstract = "false",
                DisplayName = "LevelPlacedAIControllerClass (关卡预放 AI 专用 — AIController 类型)"))
    TSubclassOf<class AAIController> LevelPlacedAIControllerClass;

    /**
     * 【v54.4 大厂架构重构】关卡预放 AI 的默认武器
     *
     * 用途:
     *   - 关卡预放 AI 走 AMeleeAIController::SetupMeleeAI 路径
     *     SetupMeleeAI → WeaponAttachmentComponent::SetMeleeConfig 读这个字段
     *   - 大厅 AI 不读这个字段: 走 FPendingAIEntry.WeaponID (UI 选)
     *
     * 编辑器配置路径:
     *   DA_AIBehaviorConfig_XXX → LevelPlacedAI → LevelPlacedWeaponClass
     *
     * 零兜底约定:
     *   - IsNull() → Log Error + 拒绝生成武器
     *   - 必须配置 (例如 BP_Weapon_Knife)
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "LevelPlacedAI",
        meta = (AllowAbstract = "false",
                DisplayName = "LevelPlacedWeaponClass (关卡预放 AI 专用 — 默认武器 BP)"))
    TSoftClassPtr<class ABaseWeapon> LevelPlacedWeaponClass;

    // ========================================================================
    // 所有 AI 共用
    // ========================================================================

    /**
     * 所有 AI 的复活无敌期 (秒) — 不分来源
     *
     * 适用场景:
     *   1. 关卡预放 AI (Level 预置): AMeleeAIController::SetupMeleeAI 末尾激活
     *   2. 大厅 AI (房主 UI 添加): URoomSpawnSubsystem::SpawnAIInternal 激活
     *   3. AI 复活 (死亡后): URoomSpawnSubsystem::RequestRespawn 激活
     *
     * 编辑器配置路径:
     *   DA_AIBehaviorConfig_XXX → Spawn → SpawnInvincibilitySeconds
     *
     * 取值约定:
     *   - > 0  : 激活该秒数无敌
     *   - <= 0 : 静默跳过 (用户明确选择: 0 = 不需要无敌期)
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Spawn",
        meta = (ClampMin = "0.0", ClampMax = "30.0", UIMin = "0.0", UIMax = "10.0",
                DisplayName = "SpawnInvincibilitySeconds (所有 AI 共用 — 复活无敌期秒数, 0=禁用)"))
    float SpawnInvincibilitySeconds = 2.0f;

    // ========================================================================
    // Getter 方法
    // ========================================================================

    /** 获取难度档位缩放值 */
    UFUNCTION(BlueprintPure, Category = "AI|Config")
    float GetDifficultyScale(EAIDifficultyTier Tier) const;

    /** 难度缩放后的感知参数 */
    UFUNCTION(BlueprintPure, Category = "AI|Config")
    FAIPerceptionParams GetScaledPerception(EAIDifficultyTier Tier) const;

    /** 难度缩放后的战斗参数 */
    UFUNCTION(BlueprintPure, Category = "AI|Config")
    FAICombatParams GetScaledCombat(EAIDifficultyTier Tier) const;

    /** 难度缩放后的移动参数 */
    UFUNCTION(BlueprintPure, Category = "AI|Config")
    FAIMovementParams GetScaledMovement(EAIDifficultyTier Tier) const;
};
