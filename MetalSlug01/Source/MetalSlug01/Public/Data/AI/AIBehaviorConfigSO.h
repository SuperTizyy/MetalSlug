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
    // 【v133.4 2026.08.02】AI 血量配置（人类 vs 母体分离）
    // ========================================================================
    //
    // 设计原则 (大厂架构 — 单一真理源 + 职责对等):
    //   - 人类 AI / 母体 AI 是两个独立真理源,不允许互相覆盖
    //   - 真理源: DA_AIBehaviorConfig_XXX → Health → MaxHealth / MotherMaxHealth
    //   - 调用方按 Pawn.bIsMother 分流:
    //     - bIsMother=true  → MotherMaxHealth (母体 AI)
    //     - bIsMother=false → MaxHealth      (人类 AI, 默认)
    //
    // 编辑器配置路径:
    //   DA_AIBehaviorConfig_MeleeGrunt → Health → Max Health (人类 AI)
    //   DA_AIBehaviorConfig_ZombieMother → Health → Mother Max Health (母体 AI)
    //
    // 典型值:
    //   - 人类 AI MaxHealth: 100~200 (与玩家相当)
    //   - 母体 AI MotherMaxHealth: 200~500 (母体更强壮)
    //
    // 【零兜底】字段 <= 0 → HealthComponent->InitializeHealth 拒绝写入, AI 血量 = 默认值 100
    // ========================================================================

    /**
     * 人类 AI 最大血量 — 所有非母体 AI 真理源
     *
     * 适用场景:
     *   - 关卡预放 AI (Level 预置): AMeleeAIController::SetupMeleeAI 末尾写入
     *   - 大厅 AI (房主 UI 添加): URoomSpawnSubsystem::SpawnAIInternal 末尾写入
     *   - AI 复活 (死亡后): URoomSpawnSubsystem::RequestRespawn 触发路径
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Health",
        meta = (ClampMin = "1.0", UIMin = "1.0",
                DisplayName = "MaxHealth (人类 AI 最大血量)"))
    float MaxHealth = 100.0f;

    /**
     * 母体 AI 最大血量 — 母体 AI 真理源
     *
     * 适用场景:
     *   - 关卡预放母体 AI (Level 预置): SetupMeleeAI 末尾按 bIsMother 分流
     *   - 母体 AI 复活链 (RequestRespawn → MutatePawnToMother)
     *
     * 调用方按 Pawn.bIsMother 分流 (与 MaxHealth 严格分离)
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Health",
        meta = (ClampMin = "1.0", UIMin = "1.0",
                DisplayName = "MotherMaxHealth (母体 AI 最大血量)"))
    float MotherMaxHealth = 300.0f;

    // ========================================================================
    // 【v133.5 2026.08.02】母体被主武器击中后降速 (Combat|Mother)
    // ========================================================================
    //
    // 设计原则 (大厂架构 — 单一真理源 + 职责对等):
    //   - 主武器 (EWeaponMeshType::Primary) 击中母体 → 降速 X 秒
    //   - 触发入口: CombatDeathComponent::TakeDamage 末尾 (唯一)
    //   - 状态字段: UMotherSlowComponent (Replicated, 镜像 Invincibility 模式)
    //   - 实际改 MaxWalkSpeed: BaseCharacter 订阅 OnSlowStateChanged
    //
    // 编辑器配置路径:
    //   DA_AIBehaviorConfig_XXX → Combat|Mother → Mother Slow Speed / Slow Duration Seconds
    //
    // 典型值:
    //   - MotherSlowSpeed: 100 (典型降速, 配置 50~200)
    //   - SlowDurationSeconds: 2.0 (默认 2s, 配置 0.5~5.0)
    //
    // 【零兜底】字段 ≤ 0 → 拒绝激活 (强制策划修复)
    // ========================================================================

    /**
     * 母体被主武器击中后的目标移速 (cm/s)
     *
     * 真理源: DA_AIBehaviorConfig_XXX → Combat|Mother → Mother Slow Speed
     *
     * 适用场景:
     *   - 关卡预放母体 AI: 击中后 MaxWalkSpeed = MotherSlowSpeed
     *   - 母体 AI 复活链: 同上
     *
     * 触发链:
     *   1. 武器击中 (Server_ReportHit) → ApplyPointDamage
     *   2. CombatDeathComponent::TakeDamage (服务器: 友军守卫 + 无敌期拦截 + ApplyDamage)
     *   3. TakeDamage 末尾判定: Weapon.MeshType==Primary && Victim.bIsMother=true
     *   4. 调 MotherSlowComponent::ActivateSlow(2.0f)
     *   5. 状态变更 → BaseCharacter 订阅 OnSlowStateChanged → 改 MaxWalkSpeed
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Mother",
        meta = (ClampMin = "0.0", UIMin = "0.0",
                DisplayName = "Mother Slow Speed (母体被主武器击中后移速)"))
    float MotherSlowSpeed = 100.0f;

    /**
     * 母体被主武器击中后降速持续时间 (秒)
     *
     * 真理源: DA_AIBehaviorConfig_XXX → Combat|Mother → Slow Duration Seconds
     *
     * 适用场景: 同 MotherSlowSpeed
     *
     * 默认 2.0 秒 (用户业务规则 2026.08.02)
     * - ≤ 0 → 拒绝激活 (零兜底)
     * - 多次激活取较晚到期 (大厂原则 - 拒绝缩短)
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Mother",
        meta = (ClampMin = "0.0", UIMin = "0.0",
                DisplayName = "Slow Duration Seconds (降速持续时间)"))
    float SlowDurationSeconds = 2.0f;

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

    // ========================================================================
    // 【v107 2026.07.28 生化模式 AI】独立 Zombie 专属参数段
    // ========================================================================
    //
    // 设计原则 (大厂架构 — 模式隔离 + 零复用 Melee 字段):
    //   - 生化模式远程射击参数与刀战近战 AttackRange 等参数语义不同 (远程 = cm 级别, 近战 = 贴身)
    //   - 强制独立字段, 不允许"复用 AttackRange 当射击距离" — 否则策划调刀战误改生化, 业务绑定
    //   - 集合点专属半径也不允许借 Combat.AcceptanceRadius — 集合点距离语义不同 (阵营层级)
    //
    // 真理源链路:
    //   DA_AIBehaviorConfig_ZombieHuman.uasset → ZombieHuman (本字段段)
    //     ↓ URoomZombieRallySubsystem / BTService_UpdateZombieState / BTTask_SelectZombieRallyPoint
    //     ↓ BTTask/Decorator/Service 读 (不重新声明, 强制复用本字段段)

    /**
     * 目标刷新间隔 (秒) — BTService_UpdateZombieTargets 派生频率
     * 典型值 0.2~0.5, 太大响应慢, 太小 CPU 浪费
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Behavior|Zombie",
        meta = (ClampMin = "0.05", ClampMax = "2.0",
                DisplayName = "TargetRefreshIntervalSeconds (目标刷新间隔秒数)"))
    float ZombieTargetRefreshIntervalSeconds = 0.25f;

    /**
     * 集合点到达半径 (cm) — BTTask_SelectZombieRallyPoint 与原生 MoveTo 用
     * 与 FAICombatParams.AcceptanceRadius 严格分离 (语义不同)
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Behavior|Zombie",
        meta = (ClampMin = "10.0", ClampMax = "1000.0",
                DisplayName = "RallyArrivalRadius (集合点到达半径 cm)"))
    float ZombieRallyArrivalRadius = 80.f;

    /**
     * 集合点附近人数统计半径 (cm) — URoomZombieRallySubsystem 选"人类最多点"用
     * 注意: 数值必须 >= ZombieRallyArrivalRadius, 否则 AI 站到点上仍不在统计半径内
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Behavior|Zombie",
        meta = (ClampMin = "100.0", ClampMax = "5000.0",
                DisplayName = "RallyPopulationRadius (集合点人数统计半径 cm)"))
    float ZombieRallyPopulationRadius = 800.f;

    /**
     * 主武器开火距离 (cm) — BTService_UpdateZombieTargets 选母体目标用
     * 母体攻击距离在生化模式独立配置, 与刀战 AttackRange 完全分离
     *
     * 【v133.10 用户业务指令 2026.08.02】扫描半径默认 = 全图大小
     *   - 0 = 扫描全图 (无距离限制, 选最近的有效目标)
     *   - >0 = 按此距离过滤 (cm, 单位米=cm/100)
     *   - 大厂原则: 距离过滤是反大厂默认 — 复活链路敌人可能跨地图复活, 不该被距离限制
     *     单一真理源: 距离判断应该交给 BT Decorator (InAttackRange), 不该在 Service 提前过滤
     *   - 因此默认改为 0 (不过滤), 策划需要时才填值
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Behavior|Zombie",
        meta = (ClampMin = "0.0", ClampMax = "100000.0",
                DisplayName = "PrimaryWeaponFireRange (主武器开火距离 cm, 0=全图)"))
    float ZombiePrimaryFireRange = 0.f;

    /**
     * 移动中射击脉冲时长 (秒) — 人类"边走边打"循环节拍
     * 简化为开火 N 秒 + 暂停短时间 (可选后续用 BTService 节流)
     * 默认 1.0s: 持续开火 1 秒后看 BT 是否切换分支
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Behavior|Zombie",
        meta = (ClampMin = "0.1", ClampMax = "5.0",
                DisplayName = "MoveAndFireBurstSeconds (边走边打脉冲秒数)"))
    float ZombieMoveAndFireBurstSeconds = 1.0f;

    // ========================================================================
    // 【v110 2026.07.30 生化模式 AI】人类 AI 后退配置
    // ========================================================================
    //
    // 设计原则 (大厂架构 — BT 为主 C++ 为辅):
    //   - 何时后退 ← BTDecorator_MotherTooClose (读取本字段)
    //   - 如何后退 ← BTTask_MoveAwayFromTarget (复用 Melee 后退原子能力)
    //   - C++ 只提供距离事实 (BTService_UpdateMotherDistance) 和决策守卫 (Decorator)
    //
    // 业务规则:
    //   - 人类 AI 在 "Human: Last Mother Pursuit" 分支中
    //   - 当与母体距离 < RetreatDistanceThreshold 时, 面朝母体后退
    //   - 后退完成后继续追击/射击
    //
    // 真理源链路:
    //   DA_AIBehaviorConfig_ZombieHuman → ZombieHuman 后退字段
    //     ↓ BTDecorator_MotherTooClose::DistanceThreshold 读取
    //     ↓ BTTask_MoveAwayFromTarget::StepDistance 读取

    /**
     * 后退触发距离阈值 (cm) — BTDecorator_MotherTooClose 使用
     *
     * 语义:
     *   - AI 距离母体 < RetreatDistanceThreshold → 后退
     *   - AI 距离母体 >= RetreatDistanceThreshold → 正常追击/射击
     *
     * 编辑器配置路径:
     *   DA_AIBehaviorConfig_ZombieHuman → Behavior|Zombie|Retreat
     *   → Retreat Distance Threshold
     *
     * 典型值: 200~500cm (根据武器射程和母体攻击范围调整)
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Behavior|Zombie|Retreat",
        meta = (ClampMin = "0.0", ClampMax = "10000.0",
                DisplayName = "Retreat Distance Threshold (后退触发距离阈值 cm)"))
    float RetreatDistanceThreshold = 300.f;

    /**
     * 单次后退距离 (cm) — BTTask_MoveAwayFromTarget 使用
     *
     * 语义:
     *   - 每次后退操作移动的距离
     *   - AI 位置 + (AI-母体)方向 × RetreatStepDistance
     *
     * 编辑器配置路径:
     *   DA_AIBehaviorConfig_ZombieHuman → Behavior|Zombie|Retreat
     *   → Retreat Step Distance
     *
     * 典型值: 100~300cm (太小会反复进退, 太大可能超出地图边界)
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Behavior|Zombie|Retreat",
        meta = (ClampMin = "50.0", ClampMax = "2000.0",
                DisplayName = "Retreat Step Distance (单次后退距离 cm)"))
    float RetreatStepDistance = 150.f;

    /**
     * 后退到位判定半径 (cm) — BTTask_MoveAwayFromTarget 使用
     *
     * 语义:
     *   - 到达目标点 AcceptanceRadius 范围内即视为"后退到位"
     *   - 太小会导致 AI 在目标点附近徘徊
     *   - 太大导致后退不彻底
     *
     * 编辑器配置路径:
     *   DA_AIBehaviorConfig_ZombieHuman → Behavior|Zombie|Retreat
     *   → Retreat Acceptance Radius
     *
     * 典型值: 50~100cm
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Behavior|Zombie|Retreat",
        meta = (ClampMin = "10.0", ClampMax = "500.0",
                DisplayName = "Retreat Acceptance Radius (后退到位判定半径 cm)"))
    float RetreatAcceptanceRadius = 60.f;

    // ========================================================================
    // 【v120 2026.08.01 生化模式 AI】集合点反射式换点配置
    // ========================================================================
    //
    // 业务背景 (用户 2026.08.01 反馈):
    //   "AI 在地图中任意的集合点附近 100cm, 且 AI 附近 50cm 范围内有母体时, 更换 LockedRallyPoint"
    //   这是事件触发的换点, 与 BTTask_SelectZombieRallyPoint 的 PeriodicReselect 是正交的
    //   - PeriodicReselect: BT 调度频率的"账本同步"重选点
    //   - ReflexChange: 母体威胁触发的"防御性换点" — 立即换,不等下一个 BT Tick
    //
    // 设计原则 (大厂架构 — BT 为主 C++ 为辅):
    //   - 何时换点 ← BTService_ReflexRallyChange (读取本字段段)
    //   - 换哪个点 ← URoomZombieRallySubsystem::SelectRallyPoint_Nearest_Excluding (账本)
    //   - 怎么写 BB ← BTService_ReflexRallyChange 效仿 BTTask 写 BB.LockedRallyPoint/Locked/Distance
    //
    // 真理源链路:
    //   DA_AIBehaviorConfig_ZombieHuman → ReflexChange (本字段段)
    //     ↓ BTService_ReflexRallyChange 派生事实
    //     ↓ 触发条件调用 URoomZombieRallySubsystem::SelectRallyPoint_Nearest_Excluding
    //
    // 典型值:
    //   - ReflexChangeRallyPointRadius: 100cm (用户给定)
    //   - ReflexChangeMotherThreatRadius: 50cm (用户给定)

    /**
     * 反射式换点 — 触发半径 1 (cm)
     *
     * 语义: AI 距离当前 LockedRallyPoint <= ReflexChangeRallyPointRadius 时, 视为"已经在集合点附近"
     *       进入反射式换点的判定前半段 (后半段: 母体威胁半径)
     *
     * 典型值: 100cm (用户 2026.08.01 业务给定)
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Behavior|Zombie|ReflexChange",
        meta = (ClampMin = "10.0", ClampMax = "1000.0",
                DisplayName = "ReflexChange RallyPoint Radius (反射式换点触发半径 cm)"))
    float ReflexChangeRallyPointRadius = 100.f;

    /**
     * 反射式换点 — 母体威胁半径 (cm)
     *
     * 语义: AI 附近 < ReflexChangeMotherThreatRadius 范围内存在任何存活母体 → 触发换点
     *       与 BTService_UpdateMotherDistance (派生 DistanceToMother) 严格分离 — 维度不同
     *       - DistanceToMother: AI 与最"目标"母体(锁定)距离 (1 vs 1)
     *       - MotherThreatRadius: AI 与"任何"母体(全账本)距离 (1 vs N)
     *
     * 典型值: 50cm (用户 2026.08.01 业务给定)
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Behavior|Zombie|ReflexChange",
        meta = (ClampMin = "10.0", ClampMax = "1000.0",
                DisplayName = "ReflexChange Mother Threat Radius (反射式换点母体威胁半径 cm)"))
    float ReflexChangeMotherThreatRadius = 50.f;

    /**
     * 反射式换点 — Service 派生 Tick 间隔 (秒)
     *
     * 语义: BTService_ReflexRallyChange 多久检查一次上面两个条件
     *       太小 (例如 0.05s) → CPU 浪费, 母体离 50cm 移动不会这么快
     *       太大 (例如 1.0s) → 响应慢, 母体都走到脸上了才换点
     *
     * 典型值: 0.2s (与 BTService_UpdateZombieTargets 同量大厂标准)
     */
    UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Behavior|Zombie|ReflexChange",
        meta = (ClampMin = "0.05", ClampMax = "2.0",
                DisplayName = "ReflexChange Tick Interval (反射式换点 Tick 间隔秒数)"))
    float ReflexChangeTickIntervalSeconds = 0.2f;
};
