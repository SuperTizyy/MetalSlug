// ==========================================
// 玩家角色战斗参数配置资产
// 关联: DA_PlayerConfig (PrimaryAsset)
// 用途: 统一管理玩家角色的所有战斗相关参数
// 大厂原则:
//   1. 单一真理源: 所有参数从本资产读取, 消除硬编码
//   2. 数据驱动: 策划可在一个地方调整所有参数
//   3. 类型安全: 直接引用, 无需按行查找
//   4. DataAsset: 单个配置资产, 适合游戏全局配置
//
// 【v120 2026.08.03 大厂架构重构 — 合并所有通用配置字段】
//
// 重构背景:
//   - 玩家和 AI 共用的母体相关字段原本散落在两个资产中 (AIBehaviorConfigSO + PlayerConfigAsset)
//   - 母体是"类型"(玩家/AI 变异后), 不是"阵营" — 配置应统一管理
//   - 重构后: 所有通用母体参数统一在本资产, AI ConfigSO 保持独立但引用同一字段名
//
// 合并字段 (从 AIBehaviorConfigSO 迁移, 玩家/AI 共用):
//   MaxHealth / MotherMaxHealth — 合并 (与已有字段合并)
//   MotherSlowSpeed / SlowDurationSeconds — 已有, 保持
//   MotherSkillSpeedMultiplier / Duration / Cooldown — 已有, 保持
//   SpawnInvincibilitySeconds — 已有, 保持 (玩家/AI 共用)
//
// 不合并字段 (AI ConfigSO 专属, 不迁移):
//   - AttackRange / AttackCooldown / Perception 等纯 AI 行为参数 (与玩家无关)
//   - LevelPlacedBehaviorTree / LevelPlacedAIControllerClass (AI 专属)
//   - Zombie* / Retreat* / ReflexChange* 等纯生化模式 AI 参数 (与玩家无关)
//
// 调用方分流 (大厂原则 — 单一真理源):
//   玩家路径: 全部走本资产 (ApplyCharacterConfigToCharacter / CombatDeathComponent / PlayerComboComponent)
//   AI 路径  : 全部走本资产 (ApplyAICharacterConfigToCharacter / CombatDeathComponent / BTTask)
//
// 大厂原则 - 零兜底:
//   - GM->PlayerConfigAsset 未配置时 Log Error 拒绝初始化
//   - 配置资产本身有默认值, 策划未改则用默认值
// ==========================================
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "PlayerConfigAsset.generated.h"

/**
 * @class UPlayerConfigAsset
 * @brief 玩家角色战斗参数配置资产
 *
 * 包含以下模块的参数:
 *   - 生命值系统 (HealthComponent)
 *   - 能量系统 (EnergyComponent)
 *   - 复活与无敌期系统 (BaseCharacter + HealthComponent)
 *   - 击杀奖励系统 (BaseCharacter)
 *   - 自动回复系统 (BaseCharacter + HealthRegenComponent)
 *   - 助攻系统 (BaseCharacter)
 *   - 连杀系统 (KillStreakWidget)
 *   - 移动速度系统 (CharacterMovementComponent)
 *   - 母体专属系统 (降速 / 加速技能)
 *     * 降速: 玩家母体被主武器击中后降速
 *     * 加速: 玩家按 E 键激活母体加速技能
 *
 * 读取路径:
 *   ARoomGameMode::PlayerConfigAsset → DA_PlayerConfig
 *   ↓
 *   RoomSpawnSubsystem::ApplyCharacterConfigToCharacter → 初始化各 Component
 *   GameHUDWidget → 初始化 KillStreakWidget
 *   CombatDeathComponent::TakeDamage → 母体降速参数
 *   PlayerComboComponent::UseSkill → 母体加速技能参数
 */
UCLASS(BlueprintType)
class METALSLUG01_API UPlayerConfigAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	// ============================================================
	// 1. 生命值系统 (HealthComponent)
	// ============================================================
	/** 最大生命值 — 玩家/AI 人类共用 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Config|Health",
		meta = (ClampMin = "1.0", UIMin = "1.0"))
	float MaxHealth = 100.0f;

	/**
	 * 【v133.4 2026.08.02】【v120 2026.08.03 合并】母体最大血量 — 玩家/AI 母体共用
	 *
	 * 设计原则 (大厂架构 — 单一真理源 + 职责对等):
	 *   - 与 MaxHealth 严格分离: 母体 / 人类是两个独立真理源, 不允许互相覆盖
	 *   - 真理源: DA_PlayerConfig → Config|Health → MotherMaxHealth
	 *   - 调用方按 Pawn.bIsMother 分流:
	 *     - bIsMother=true  → MotherMaxHealth (玩家/AI 变异为母体后)
	 *     - bIsMother=false → MaxHealth      (人类玩家/AI)
	 *
	 * 编辑器配置路径:
	 *   DA_PlayerConfig → Config|Health → MotherMaxHealth
	 *
	 * 典型值: 200~500 (母体血量比人类高, 体现变异增强感)
	 *
	 * 应用时机:
	 *   1. RoomMotherMutationSubsystem::MutateCharacterToMother 变异完成后 (玩家)
	 *   2. RoomSpawnSubsystem::MutatePawnToMother 末尾 (AI 母体)
	 *
	 * 【零兜底】字段 <= 0 → Log Error 强制修复, 不允许 0 血
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Config|Health",
		meta = (ClampMin = "1.0", UIMin = "1.0"))
	float MotherMaxHealth = 200.0f;

	// ============================================================
	// 2. 母体降速系统 (Combat|Mother — 玩家/AI 共用配置)
	// ============================================================
	//
	// 【v120 2026.08.03 大厂架构重构 — 从 AIBehaviorConfigSO 迁移, 玩家/AI 共用】
	//
	// 设计原则 (大厂架构 — 单一真理源 + 职责对等):
	//   - 玩家母体 + AI 母体共用同一配置字段 (MotherSlowSpeed / SlowDurationSeconds)
	//   - 不再散落在两个资产中 (AI ConfigSO 有, PlayerConfigAsset 也有)
	//   - 调用方统一读本资产 (CombatDeathComponent::TakeDamage)
	//
	// 触发链:
	//   1. 武器击中 (Server_ReportHit) → ApplyPointDamage
	//   2. CombatDeathComponent::TakeDamage (服务器: 友军守卫 + 无敌期拦截 + ApplyDamage)
	//   3. TakeDamage 末尾判定: Weapon.MeshType==Primary && Victim.bIsMother=true
	//   4. 调 MotherSlowComponent::ActivateSlow(SlowDurationSeconds, CurrentSpeed)
	//   5. 状态变更 → BaseCharacter 订阅 OnSlowStateChanged → 改 MaxWalkSpeed
	//
	// 典型值:
	//   - MotherSlowSpeed: 100 (cm/s)
	//   - SlowDurationSeconds: 2.0 (秒)
	//
	// 【零兜底】字段 ≤ 0 → MotherSlowComponent::ActivateSlow 拒绝激活 (强制策划修复)

	/**
	 * 母体被主武器击中后的目标移速 (cm/s) — 玩家/AI 共用
	 *
	 * 真理源: DA_PlayerConfig → Config|Combat|Mother → Mother Slow Speed
	 *
	 * 适用场景: 玩家/AI 母体被主武器击中后降速
	 *
	 * 默认 100 (用户业务规则 2026.08.02)
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Config|Combat|Mother",
		meta = (ClampMin = "0.0", UIMin = "0.0"))
	float MotherSlowSpeed = 100.0f;

	/**
	 * 母体被主武器击中后降速持续时间 (秒) — 玩家/AI 共用
	 *
	 * 真理源: DA_PlayerConfig → Config|Combat|Mother → Slow Duration Seconds
	 *
	 * 默认 2.0 秒 (用户业务规则 2026.08.02)
	 *
	 * - ≤ 0 → 拒绝激活 (零兜底)
	 * - 多次激活取较晚到期 (大厂原则 - 拒绝缩短)
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Config|Combat|Mother",
		meta = (ClampMin = "0.0", UIMin = "0.0"))
	float SlowDurationSeconds = 2.0f;

	// ============================================================
	// 3. 母体加速技能 (Combat|Mother — 玩家按 E 键触发)
	// ============================================================
	//
	// 【v120 2026.08.03 大厂架构重构 — 从 AIBehaviorConfigSO 迁移】
	//
	// 设计原则 (大厂架构 — 镜像 MotherSlowComponent 模式):
	//   - 母体加速技能: 移速瞬间变快, 持续 N 秒后结束, 进入冷却期
	//   - 状态字段: UMotherSkillComponent (Replicated, 镜像 MotherSlowComponent)
	//   - 实际改 MaxWalkSpeed: BaseCharacter 订阅 OnSkillStateChanged
	//
	// 触发链:
	//   1. 玩家按 E 键 (IA_MotherSkill)
	//   2. PlayerComboComponent::UseSkill()
	//   3. MotherSkillComponent::ActivateSkill(SpeedMultiplier, Duration, Cooldown, CurrentSpeed)
	//   4. 状态变更 → BaseCharacter 订阅 OnSkillStateChanged → 改 MaxWalkSpeed
	//
	// 典型值:
	//   - MotherSkillSpeedMultiplier: 2.0 (加速 2 倍)
	//   - SkillDurationSeconds: 2.0 (持续 2 秒, 用户业务规则 2026.08.03)
	//   - SkillCooldownSeconds: 10.0 (冷却 10 秒, 用户业务规则 2026.08.03)
	//
	// 【零兜底】字段 ≤ 0 → MotherSkillComponent::ActivateSkill 拒绝激活 (强制策划修复)

	/**
	 * 母体加速技能 — 移速倍率 (相对于当前 MaxWalkSpeed)
	 *
	 * 真理源: DA_PlayerConfig → Config|Combat|Mother → Mother Skill Speed Boost Multiplier
	 *
	 * 语义:
	 *   - 当前 MaxWalkSpeed × MotherSkillSpeedMultiplier = 技能激活时速度
	 *   - 例如: 600cm/s × 2.0 = 1200cm/s (加速 2 倍)
	 *
	 * 默认 2.0x (用户业务规则 2026.08.03)
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Config|Combat|Mother",
		meta = (ClampMin = "1.0", UIMin = "1.0"))
	float MotherSkillSpeedMultiplier = 2.0f;

	/**
	 * 母体加速技能持续时间 (秒)
	 *
	 * 真理源: DA_PlayerConfig → Config|Combat|Mother → Duration Seconds
	 *
	 * 默认 2.0 秒 (用户业务规则 2026.08.03)
	 * - ≤ 0 → 拒绝激活 (零兜底)
	 * - 多次激活取较晚到期 (大厂原则 - 拒绝缩短)
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Config|Combat|Mother",
		meta = (ClampMin = "0.0", UIMin = "0.0"))
	float MotherSkillDurationSeconds = 2.0f;

	/**
	 * 母体加速技能冷却时间 (秒)
	 *
	 * 真理源: DA_PlayerConfig → Config|Combat|Mother → Cooldown Seconds
	 *
	 * 默认 10.0 秒 (用户业务规则 2026.08.03)
	 * - ≤ 0 → 拒绝激活 (零兜底)
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Config|Combat|Mother",
		meta = (ClampMin = "0.0", UIMin = "0.0"))
	float MotherSkillCooldownSeconds = 10.0f;

	// ============================================================
	// 4. 能量系统 (EnergyComponent)
	// ============================================================
	/** 最大能量值 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Config|Energy",
		meta = (ClampMin = "0.0", UIMin = "0.0"))
	float MaxEnergy = 100.0f;

	// ============================================================
	// 5. 复活与无敌期系统
	// ============================================================
	/**
	 * 复活延迟时间 (秒)
	 * 语义: 死亡后等待多久重生
	 * 必须大于 WeaponDestroyDelaySeconds (保证武器溶解完成)
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Config|Respawn",
		meta = (ClampMin = "1.0", UIMin = "1.0"))
	float RespawnDelaySeconds = 3.0f;

	/**
	 * 复活无敌期时长 (秒)
	 * 语义: 重生后多久内无敌
	 * 配 0 = 禁用无敌期
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Config|Respawn",
		meta = (ClampMin = "0.0", UIMin = "0.0"))
	float SpawnInvincibilitySeconds = 2.0f;

	/**
	 * 武器销毁延迟 (秒)
	 * 语义: 死亡后武器掉地多久自动销毁
	 * 必须小于 RespawnDelaySeconds (保证重生前清理完毕)
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Config|Respawn",
		meta = (ClampMin = "0.5", UIMin = "0.5"))
	float WeaponDestroyDelaySeconds = 3.0f;

	// ============================================================
	// 6. 击杀奖励系统 (BaseCharacter)
	// ============================================================
	/** 每次击杀奖励的生命值 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Config|KillReward",
		meta = (ClampMin = "0.0", UIMin = "0.0"))
	float HealthRewardPerKill = 10.0f;

	/** 每次击杀奖励的能量值 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Config|KillReward",
		meta = (ClampMin = "0.0", UIMin = "0.0"))
	float EnergyRewardPerKill = 25.0f;

	// ============================================================
	// 7. 自动回复系统 (HealthRegenComponent)
	// ============================================================
	/** 生命回复速度 (每秒) - 配 0 = 禁用自动回血 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Config|Regen",
		meta = (ClampMin = "0.0", UIMin = "0.0"))
	float HealthRegenRate = 0.0f;

	/** 能量回复速度 (每秒) - 配 0 = 禁用自动回能量 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Config|Regen",
		meta = (ClampMin = "0.0", UIMin = "0.0"))
	float EnergyRegenRate = 0.0f;

	/**
	 * 停止移动后开始回复的延迟 (秒)
	 * 【v100.1 大厂架构 — 业务规则 2026.07.26】母体"不被打不移动 5 秒"开始回血
	 * BP 子类可覆盖 (刀战模式默认 0 = 不开启自动回血, 仍走 bEnableAutoRegen=false 路径)
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Config|Regen",
		meta = (ClampMin = "0.0", UIMin = "0.0"))
	float RegenerationDelay = 5.0f;

	// ============================================================
	// 8. 助攻系统 (BaseCharacter)
	// ============================================================
	/**
	 * 助攻判定时间窗口 (秒)
	 * 语义: 造成伤害后 N 秒内目标死亡, 算助攻
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Config|Assist",
		meta = (ClampMin = "0.0", UIMin = "0.0"))
	float AssistTimeWindow = 5.0f;

	// ============================================================
	// 9. 连杀系统 (KillStreakWidget)
	// ============================================================
	/**
	 * 连杀超时时间 (秒)
	 * 语义: N 秒内无击杀则连杀计数重置为 0
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Config|KillStreak",
		meta = (ClampMin = "1.0", UIMin = "1.0"))
	float KillStreakDuration = 10.0f;

	/**
	 * 图标自动隐藏时间 (秒)
	 * 语义: 击杀后图标显示多久自动隐藏
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Config|KillStreak",
		meta = (ClampMin = "1.0", UIMin = "1.0"))
	float KillStreakIconDisplayDuration = 3.0f;

	// ============================================================
	// 10. 移动速度系统 (CharacterMovementComponent)
	// ============================================================
	/**
	 * 人类玩家行走速度 (MaxWalkSpeed)
	 * 语义: 玩家正常移动时的最大速度
	 * 恢复场景: 攻击动画结束后恢复到此速度
	 *
	 * 大厂原则 - 零兜底:
	 *   - 策划未配时使用默认值 600.0f
	 *   - 所有玩家角色 Spawn 时从本配置读取
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Config|Movement",
		meta = (ClampMin = "100.0", UIMin = "100.0"))
	float MaxWalkSpeed = 600.0f;

	/**
	 * 【v133.3 2026.08.02】母体玩家行走速度 (MaxWalkSpeed) — 玩家变异为母体后专用
	 *
	 * 设计原则 (大厂架构 — 单一真理源 + 职责对等):
	 *   - 与 MaxWalkSpeed 严格分离: 母体 / 人类是两个独立真理源, 不允许互相覆盖
	 *   - 真理源: DA_PlayerConfig → Movement → MotherMaxWalkSpeed
	 *   - 调用方按 Pawn.bIsMother 分流:
	 *     - bIsMother=true  → MotherMaxWalkSpeed (玩家变异为母体后)
	 *     - bIsMother=false → MaxWalkSpeed (人类玩家)
	 *
	 * 编辑器配置路径:
	 *   DA_PlayerConfig → Config|Movement → MotherMaxWalkSpeed
	 *
	 * 典型值: 800~1200 cm/s (母体比人类快, 体现变异加速感)
	 *
	 * 应用时机:
	 *   1. RoomSpawnSubsystem::ApplyCharacterConfigToCharacter Spawn 时 (按 bIsMother 分流)
	 *   2. RoomMotherMutationSubsystem::MutateCharacterToMother 变异完成后 (立即切换)
	 *   3. PlayerComboComponent 攻击蒙太奇结束恢复速度 (按 bIsMother 分流)
	 *
	 * 【零兜底】如果 MotherMaxWalkSpeed <= 0, 母体 Pawn 不会移动, Log Error 强制修复
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Config|Movement",
		meta = (ClampMin = "100.0", UIMin = "100.0"))
	float MotherMaxWalkSpeed = 800.0f;
};
