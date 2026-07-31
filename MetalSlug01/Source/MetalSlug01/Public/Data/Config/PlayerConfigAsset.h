// ==========================================
// 玩家角色战斗参数配置资产
// 关联: DA_PlayerConfig (PrimaryAsset)
// 用途: 统一管理玩家角色的所有战斗相关参数
// 大厂原则:
//   1. 单一真理源: 所有参数从本资产读取, 消除硬编码
//   2. 数据驱动: 策划可在一个地方调整所有参数
//   3. 类型安全: 直接引用, 无需按行查找
//   4. DataAsset: 单个配置资产, 适合游戏全局配置
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
 *
 * 读取路径:
 *   ARoomGameMode::PlayerConfigAsset → DA_PlayerConfig
 *   ↓
 *   RoomSpawnSubsystem::ApplyCharacterConfigToCharacter → 初始化各 Component
 *   GameHUDWidget → 初始化 KillStreakWidget
 *
 * 大厂原则 - 零兜底:
 *   - GM->PlayerConfigAsset 未配置时 Log Error 拒绝初始化
 *   - 配置资产本身有默认值, 策划未改则用默认值
 */
UCLASS(BlueprintType)
class METALSLUG01_API UPlayerConfigAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	// ============================================================
	// 1. 生命值系统 (HealthComponent)
	// ============================================================
	/** 最大生命值 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Config|Health", meta = (ClampMin = "1.0", UIMin = "1.0"))
	float MaxHealth = 100.0f;

	/**
	 * 【v133.4 2026.08.02】母体玩家最大血量 — 玩家变异为母体后专用
	 *
	 * 设计原则 (大厂架构 — 单一真理源 + 职责对等):
	 *   - 与 MaxHealth 严格分离: 母体 / 人类是两个独立真理源,不允许互相覆盖
	 *   - 真理源: DA_PlayerConfig → Config|Health → MotherMaxHealth
	 *   - 调用方按 Pawn.bIsMother 分流:
	 *     - bIsMother=true  → MotherMaxHealth (玩家变异为母体后)
	 *     - bIsMother=false → MaxHealth      (人类玩家)
	 *
	 * 编辑器配置路径:
	 *   DA_PlayerConfig → Config|Health → MotherMaxHealth
	 *
	 * 典型值: 200~500 (母体血量比人类高, 体现变异增强感)
	 *
	 * 应用时机:
	 *   1. RoomMotherMutationSubsystem::MutateCharacterToMother 变异完成后 (集中调度入口)
	 *   2. RoomSpawnSubsystem::MutatePawnToMother 末尾 (单一真理源路径)
	 *
	 * 【零兜底】字段 <= 0 → Log Error 强制修复, 不允许 0 血
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Config|Health", meta = (ClampMin = "1.0", UIMin = "1.0"))
	float MotherMaxHealth = 200.0f;

	// ============================================================
	// 2. 母体被主武器击中后降速 (Combat|Mother)
	// ============================================================
	//
	// 【v133.5 2026.08.02 大厂架构 — 真理源镜像 ConfigSO】
	//   - 玩家母体被主武器击中 → 降速到 MotherSlowSpeed
	//   - 持续时长 = SlowDurationSeconds
	//   - 触发链: CombatDeathComponent::TakeDamage 末尾
	//   - 状态: UMotherSlowComponent::ActivateSlow
	//
	// 真理源: DA_PlayerConfig → Config|Combat|Mother → Mother Slow Speed / Slow Duration Seconds
	//
	// 典型值:
	//   - MotherSlowSpeed: 100 (cm/s)
	//   - SlowDurationSeconds: 2.0 (秒)
	//
	// 【零兜底】字段 ≤ 0 → 拒绝激活 (强制策划修复)

	/**
	 * 玩家母体被主武器击中后的目标移速 (cm/s)
	 *
	 * 真理源: DA_PlayerConfig → Config|Combat|Mother → Mother Slow Speed
	 *
	 * 适用场景: 玩家母体被主武器击中后降速
	 *
	 * 默认 100 (用户业务规则 2026.08.02)
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Config|Combat|Mother",
		meta = (ClampMin = "0.0", UIMin = "0.0"))
	float MotherSlowSpeed = 100.0f;

	/**
	 * 玩家母体被主武器击中后降速持续时间 (秒)
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
	// 2. 能量系统 (EnergyComponent)
	// ============================================================
	/** 最大能量值 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Config|Energy", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float MaxEnergy = 100.0f;

	// ============================================================
	// 3. 复活与无敌期系统
	// ============================================================
	/**
	 * 复活延迟时间 (秒)
	 * 语义: 死亡后等待多久重生
	 * 必须大于 WeaponDestroyDelaySeconds (保证武器溶解完成)
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Config|Respawn", meta = (ClampMin = "1.0", UIMin = "1.0"))
	float RespawnDelaySeconds = 3.0f;

	/**
	 * 复活无敌期时长 (秒)
	 * 语义: 重生后多久内无敌
	 * 配 0 = 禁用无敌期
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Config|Respawn", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float SpawnInvincibilitySeconds = 2.0f;

	/**
	 * 武器销毁延迟 (秒)
	 * 语义: 死亡后武器掉地多久自动销毁
	 * 必须小于 RespawnDelaySeconds (保证重生前清理完毕)
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Config|Respawn", meta = (ClampMin = "0.5", UIMin = "0.5"))
	float WeaponDestroyDelaySeconds = 3.0f;

	// ============================================================
	// 4. 击杀奖励系统 (BaseCharacter)
	// ============================================================
	/** 每次击杀奖励的生命值 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Config|KillReward", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float HealthRewardPerKill = 10.0f;

	/** 每次击杀奖励的能量值 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Config|KillReward", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float EnergyRewardPerKill = 25.0f;

	// ============================================================
	// 5. 自动回复系统 (HealthRegenComponent)
	// ============================================================
	/** 生命回复速度 (每秒) - 配 0 = 禁用自动回血 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Config|Regen", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float HealthRegenRate = 0.0f;

	/** 能量回复速度 (每秒) - 配 0 = 禁用自动回能量 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Config|Regen", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float EnergyRegenRate = 0.0f;

	/**
	 * 停止移动后开始回复的延迟 (秒)
	 * 【v100.1 大厂架构 — 业务规则 2026.07.26】母体"不被打不移动 5 秒"开始回血
	 * BP 子类可覆盖 (刀战模式默认 0 = 不开启自动回血, 仍走 bEnableAutoRegen=false 路径)
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Config|Regen", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float RegenerationDelay = 5.0f;

	// ============================================================
	// 6. 助攻系统 (BaseCharacter)
	// ============================================================
	/**
	 * 助攻判定时间窗口 (秒)
	 * 语义: 造成伤害后 N 秒内目标死亡, 算助攻
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Config|Assist", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float AssistTimeWindow = 5.0f;

	// ============================================================
	// 7. 连杀系统 (KillStreakWidget)
	// ============================================================
	/**
	 * 连杀超时时间 (秒)
	 * 语义: N 秒内无击杀则连杀计数重置为 0
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Config|KillStreak", meta = (ClampMin = "1.0", UIMin = "1.0"))
	float KillStreakDuration = 10.0f;

	/**
	 * 图标自动隐藏时间 (秒)
	 * 语义: 击杀后图标显示多久自动隐藏
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Config|KillStreak", meta = (ClampMin = "1.0", UIMin = "1.0"))
	float KillStreakIconDisplayDuration = 3.0f;
	// ============================================================
	// 8. 移动速度系统 (CharacterMovementComponent)
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
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Config|Movement", meta = (ClampMin = "100.0", UIMin = "100.0"))
	float MaxWalkSpeed = 600.0f;

	/**
	 * 【v133.3 2026.08.02】母体玩家行走速度 (MaxWalkSpeed) — 玩家变异为母体后专用
	 *
	 * 设计原则 (大厂架构 — 单一真理源 + 职责对等):
	 *   - 与 MaxWalkSpeed 严格分离: 母体 / 人类是两个独立真理源,不允许互相覆盖
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
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Config|Movement", meta = (ClampMin = "100.0", UIMin = "100.0"))
	float MotherMaxWalkSpeed = 800.0f;
};
