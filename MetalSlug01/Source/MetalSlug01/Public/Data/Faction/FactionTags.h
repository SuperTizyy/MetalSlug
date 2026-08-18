// Copyright (c) 2026.
//
// ========================================================================
// FactionTags.h — 阵营 Tag 体系集中定义 (大厂架构单一真理源)
// ========================================================================
//
// 文件功能总览:
//   - 定义 FactionTags 命名空间下的 Native GameplayTag 声明(Offense/Defense)
//   - 提供 FFactionTags 结构体作为集中访问器,所有阵营相关 API 都从此处走
//   - 提供 FGenericTeamId ↔ FGameplayTag 双向转换(适配 UE AIPerception 协议)
//   - 提供 CanDamage 友军伤害守卫,所有扣血入口必经此函数
//
// 大厂原则:
//   - 阵营是通用身份: 玩家和 AI 都可属于 Offense 或 Defense,类型与阵营正交
//   - 零兜底: 不存在 Faction.None 默认值,任一 Character 必须有明确阵营
//   - 显式失败优于默认: 旧 Tag (Player/Zombie/Enemy) 检测到立即 Log Error
//   - 单一真理源: 敌友判定的唯一入口是 FFactionTags::CanDamage / AttitudeBetween
//
// ========================================================================
// FactionTags.h — 阵营 Tag 体系集中定义 (大厂架构单一真理源)
// ========================================================================
//
// 设计原则 (2026.07.10 重构):
//   1. 单一真理源 — 全工程阵营表达只用 FGameplayTag
//   2. 双阵营根 Tag — 只有 Faction.Offense 和 Faction.Defense 两个有效阵营
//   3. 阵营是**通用身份** — 客户端玩家和 AI 都可以属于 Offense 或 Defense
//      哪种模式下哪方是攻/守 = 业务决策, 走 ARoomGameMode::ModeRulesByMode 配置
//   4. 零兜底 — 不存在 Faction.None 这种"未知/默认"值; 任何 Character 必须有明确阵营
//   5. 旧 Tag 显式报错 — Faction.Player/Zombie/Enemy/FriendlyAI 全部 DEPRECATED,
//      业务代码看到这些 Tag 立刻 Log Error, 不允许静默替换
//
// 阵营 ≠ 角色类型 (重要):
//   - 阵营 (Offense/Defense) = 业务分边 (攻/守), 用于敌友判定
//   - 角色类型 (玩家/AI)    = 控制器来源, 由 IsPlayerControlled() / IsAIControlled() 判定
//   两者正交: 玩家可以是 Offense 或 Defense; AI 也可以是 Offense 或 Defense
//
// 不存在以下 enum:
//   - ERoomTeam::None/Attack/Defense — 已彻底删除 (因冗余)
//   - uint8 GetTeamID() — 已删除 (因冗余)
//   - FGenericTeamId 业务代码不再用 — 只保留 UE 接口 override (内部走 FFactionTags)
//
// 唯一使用本头的方式:
//   #include "Data/Faction/FactionTags.h"
//   FactionTag = FFactionTags::Offense();  // 或 Defense()
//
#pragma once

#include "CoreMinimal.h"
#include "GameplayTagContainer.h"
#include "NativeGameplayTags.h"
#include "GenericTeamAgentInterface.h"  // ETeamAttitude::Type

// ==========================================
// 1. UE Native GameplayTags 声明 (启动期自动注册)
// ==========================================

namespace FactionTags
{
	// 攻方 — 刀战红方阵营 / 生化模式僵尸阵营 共用
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Faction_Offense);

	// 守方 — 刀战蓝方阵营 / 生化模式人类阵营 共用
	UE_DECLARE_GAMEPLAY_TAG_EXTERN(TAG_Faction_Defense);
}

// ==========================================
// 2. FFactionTags 集中访问器 (推荐使用 API)
// ==========================================
//
// 设计: 所有阵营 Tag 访问都过这里, 不直接读原生 TAG_Faction_xxx 全局变量
//       改 Tag 名只改这一处, 业务代码不受影响
//
struct FFactionTags
{
	// ===== 主要 API: 静态函数返回 FGameplayTag =====
	// 设计: 用函数而非静态成员, 因为 GameplayTag 系统在模块启动后才填充值
	//       函数内部检查 IsValid, 无效时调 RequestGameplayTag 拉取

	/** 攻方阵营 Tag — Faction.Offense */
	static FGameplayTag Offense();

	/** 守方阵营 Tag — Faction.Defense */
	static FGameplayTag Defense();

	// ===== 判定 API: 用 MatchesTag 严格匹配 (避免 Faction.EnemyBoss 误匹配 Faction.Enemy) =====

	/** 是否为攻方阵营 (Faction.Offense) — 严格匹配, 子 Tag 不算 */
	static bool IsOffense(const FGameplayTag& Tag);

	/** 是否为守方阵营 (Faction.Defense) — 严格匹配 */
	static bool IsDefense(const FGameplayTag& Tag);

	/** 是否属于任何有效阵营 (Offense 或 Defense) */
	static bool IsValidFaction(const FGameplayTag& Tag);

	// ===== 双阵营对比 API =====

	/** 同阵营 (双方都是 Offense 或双方都是 Defense) */
	static bool IsSameSide(const FGameplayTag& A, const FGameplayTag& B);

	/** 异阵营 (攻守对立) */
	static bool IsOppositeSide(const FGameplayTag& A, const FGameplayTag& B);

	// ===== 阵营互转 (仅用于阵营切换业务) =====

	/** 从 Offense 切到 Defense */
	static FGameplayTag OppositeOf(const FGameplayTag& Tag);

	// ===== 阵营 Attitude 判定 (UE 阵营协议层兼容) =====
	//
	// 用途: IGenericTeamAgentInterface::GetTeamAttitudeTowards 用
	//       Offense 对 Defense = Hostile, Offense 对 Offense = Friendly
	//       UE 原生 FGenericTeamId::GetAttitude 已用阵营 ID=0/1 比较, 本函数为新体系提供等价语义

	/** 对外态度: Friendly (同阵营) / Hostile (异阵营) / Neutral (非有效阵营) */
	static ETeamAttitude::Type AttitudeBetween(const FGameplayTag& Self, const FGameplayTag& Other);

	/**
	 * 【2026.07.11 P0 大厂架构】扣血授权检查 — 服务器用"友军伤害守卫"
	 *
	 * 单一真理源: 所有需要扣血的代码路径 (Server_ReportHit / Server_ReportAIAttackHit / TakeDamage)
	 *            都必须先调这个函数, 确认攻击者和受击者**异阵营**后才能扣血.
	 *
	 * 大厂原则:
	 *   - 零兜底: 任一阵营无效 (空 Tag / 旧 Tag) → 返回 false, 调用方必须拒绝扣血
	 *   - 显式记录: 拒绝时打 Log Error (含攻击者/受击者/双方阵营), 帮助策划/程序定位配置错误
	 *   - 同阵营拒扣: Friendly 不允许伤害 Friendly (玩家打队友 → 拒绝)
	 *
	 * @param AttackerFaction  攻击者 Pawn.FactionTag (Self)
	 * @param VictimFaction    受击者 Pawn.FactionTag (Other)
	 * @param Context          调用方上下文 (用于日志定位, e.g. TEXT("ABaseWeapon::Server_ReportHit"))
	 * @param AttackerName     攻击者名字 (日志用)
	 * @param VictimName       受击者名字 (日志用)
	 * @return true = 可以扣血 (异阵营, 均为有效阵营); false = 必须拒扣 (同阵营 / 无效阵营)
	 */
	static bool CanDamage(
		const FGameplayTag& AttackerFaction,
		const FGameplayTag& VictimFaction,
		const TCHAR* Context,
		const FString& AttackerName,
		const FString& VictimName);

	// ===== FGenericTeamId ↔ FGameplayTag 双向转换 (UE AIPerception 接口适配层) =====
	//
	// 用途: UE AIPerception 必须用 FGenericTeamId (整数协议), 但业务代码只认 FGameplayTag
	//       这两个函数是"UE 协议层"的桥接器, 仅在 SetGenericTeamId / GetGenericTeamId
	//       override 实现里调用, 业务代码 (UI/GameMode/Controller) 应该用 Offense()/Defense()
	//       直接拿 FGameplayTag, 走 AttitudeBetween 判定
	//
	// 大厂原则: 不兜底 — 任何非 Offense/Defense 都显式报错

	/** FGameplayTag → FGenericTeamId (Offense=0, Defense=1, 其它 = NoTeam + Error 日志) */
	static FGenericTeamId ToGenericTeamId(const FGameplayTag& Tag);

	/** FGenericTeamId → FGameplayTag (0=Offense, 1=Defense, 其它=EmptyTag + Error 日志) */
	static FGameplayTag FromGenericTeamId(const FGenericTeamId& TeamId);

	// ===== 阵营 UI/统计 对外接口 =====

	/** 阵营的人类可读名 (UI 显示) */
	static FString GetDisplayName(const FGameplayTag& Tag);

	// ===== 旧 Tag 检测 (大厂原则: 不静默兜底) =====
	//
	// 背景: 旧 BP (DA_AIBehaviorConfig_*) 可能仍配 Faction.Player/Zombie/Enemy/FriendlyAI
	//       这些 Tag 标记为 DEPRECATED 但保留注册 (避免 BP 引用崩溃)
	//       业务代码看到这些 Tag 必须显式报错, 让策划/程序修复 Data Asset
	//
	// 注意: 这里**只检测**, 不迁移!
	//       旧版本曾经自动迁移到 Offense/Defense, 那是大厂原则禁止的"静默兜底"
	//       现在改成: 检测到 → Log Error → 调用方必须中断流程
	//
	// 修复方法: 在 DA_AIBehaviorConfig_XXX 中把 FactionTag 从 Faction.Enemy 改为 Faction.Offense/Defense (DA_AIProfile_XXX 已删除)
	//
	/** 检测是否是已废弃的旧阵营 Tag (Faction.Player/Zombie/Enemy/FriendlyAI) */
	static bool IsDeprecatedLegacyFaction(const FGameplayTag& Tag);

	/**
	 * 检测 + 报告 — 调用方首选此 API
	 * 检测到旧 Tag 或空 Tag 时 Log Error 并返回 false, 让调用方决定是否中断流程
	 * @param Tag      待校验的阵营
	 * @param Context  上下文描述 (用于日志定位调用方, 例如 TEXT("BaseAIController::InitializeFromProfile"))
	 * @return true = 阵营有效 (Offense/Defense), false = 是旧 Tag 或空 Tag
	 */
	static bool ValidateFactionOrReportError(const FGameplayTag& Tag, const TCHAR* Context);

	// ===== 历史兼容迁移器 — 已彻底删除 (2026.07.10 v25 大厂原则零兜底) =====
	//
	// 旧版本存在 MigrateFromLegacy(Faction.Player/Zombie/Enemy → Offense/Defense) 函数,
	// 业务代码遇到旧 Tag 时会自动迁移. 这是典型的"静默兜底", 大厂原则严禁:
	//   1. 业务代码读到旧 Tag 不知道, 配置错误被吞掉
	//   2. 策略层改 GameMode 业务逻辑 (红方=玩家 → 蓝方=玩家) 时, 旧迁移代码继续按老规则工作
	//   3. 测试时无法确定实际生效的阵营
	//
	// 新版强制流程:
	//   业务代码 → ValidateFactionOrReportError → 失败 → Log Error → 调用方中断流程
	//                                            → 强制策划/程序修复 Data Asset
	//
};
