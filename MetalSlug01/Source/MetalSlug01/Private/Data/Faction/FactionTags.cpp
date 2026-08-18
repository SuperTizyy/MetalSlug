// Copyright (c) 2026.
//
// ========================================================================
// FactionTags.cpp — 阵营 Tag 体系实现文件
// ========================================================================
//
// 文件功能总览:
//   - 注册 Native GameplayTag(Faction.Offense / Faction.Defense)
//   - 实现 FFactionTags 结构体的全部静态 API
//   - 实现 CanDamage 扣血授权守卫 (大厂架构 - 单一节流点)
//
// 大厂原则:
//   - 零兜底: 所有无效阵营路径都 Log Error,绝不允许静默返回默认值
//   - 显式优于默认: OppositeOf / AttitudeBetween / FromGenericTeamId 失败均显式报错
//   - 友军伤害守卫: 所有扣血路径(Server_ReportHit / TakeDamage / AIAttack)
//     必须先调 CanDamage,同阵营拒绝扣血
//
// ========================================================================
// FactionTags.cpp — 阵营 Tag 体系实现
// ========================================================================

#include "Data/Faction/FactionTags.h"
#include "GameplayTagsManager.h"
#include "Logging/LogMacros.h"

DEFINE_LOG_CATEGORY_STATIC(LogFactionTags, Log, All);

namespace FactionTags
{
	// Native GameplayTag 注册
	UE_DEFINE_GAMEPLAY_TAG(TAG_Faction_Offense, "Faction.Offense");
	UE_DEFINE_GAMEPLAY_TAG(TAG_Faction_Defense, "Faction.Defense");
}

FGameplayTag FFactionTags::Offense()
{
	// Native tag 在模块加载时已注册, 直接返回即可
	return FactionTags::TAG_Faction_Offense;
}

FGameplayTag FFactionTags::Defense()
{
	return FactionTags::TAG_Faction_Defense;
}

/**
 * @brief 严格判定 Tag 是否等于 Faction.Offense (用 == 而非 MatchesTag)
 * @param Tag 待校验的阵营 Tag
 * @return true = 是攻方,false = 不是(包括 Defense/EmptyTag/子 Tag)
 *
 * 设计意图:用 == 严格匹配,避免 Faction.Offense.X 子 Tag 误判为 Offense
 */
bool FFactionTags::IsOffense(const FGameplayTag& Tag)
{
	// 严格匹配 — 用 == 而非 MatchesTag, 避免 Faction.Offense.X 子 Tag 误判
	return Tag == Offense();
}

bool FFactionTags::IsDefense(const FGameplayTag& Tag)
{
	return Tag == Defense();
}

/**
 * @brief 校验 Tag 是否为有效阵营 (Offense 或 Defense)
 * @param Tag 待校验的阵营 Tag
 * @return true = 有效阵营 (Offense/Defense),false = 无效
 *
 * 大厂原则:这是阵营校验的"真理入口",AttitudeBetween / CanDamage / OppositeOf 都依赖此函数
 * 不允许任何宽松匹配,空 Tag 和未知 Tag 都视为无效
 */
bool FFactionTags::IsValidFaction(const FGameplayTag& Tag)
{
	return IsOffense(Tag) || IsDefense(Tag);
}

/**
 * @brief 判定两个阵营 Tag 是否属于同一方
 * @param A 阵营 Tag A
 * @param B 阵营 Tag B
 * @return true = 同阵营 (A==B 且均有效),false = 异阵营或任一无效
 *
 * 边界:任一 Tag 无效 → 返回 false (不算同阵营),调用方必须先 check IsValidFaction
 */
bool FFactionTags::IsSameSide(const FGameplayTag& A, const FGameplayTag& B)
{
	if (!IsValidFaction(A) || !IsValidFaction(B))
	{
		// 任一无效阵营 → 不算同阵营 (调用方必须 check 有效阵营)
		return false;
	}
	return A == B;
}

/**
 * @brief 判定两个阵营 Tag 是否属于敌对双方
 * @param A 阵营 Tag A
 * @param B 阵营 Tag B
 * @return true = 异阵营 (A!=B 且均有效),false = 同阵营或任一无效
 *
 * 与 IsSameSide 互补:IsSameSide(A,B) XOR IsOppositeSide(A,B) (均有效时)
 * 边界:任一 Tag 无效 → 返回 false (不算敌对),与 IsSameSide 行为对称
 */
bool FFactionTags::IsOppositeSide(const FGameplayTag& A, const FGameplayTag& B)
{
	if (!IsValidFaction(A) || !IsValidFaction(B))
	{
		return false;
	}
	return A != B;
}

FGameplayTag FFactionTags::OppositeOf(const FGameplayTag& Tag)
{
	if (Tag == Offense())
	{
		return Defense();
	}
	if (Tag == Defense())
	{
		return Offense();
	}

	// 非有效阵营 → 不允许静默返回, 显式报错
	UE_LOG(LogFactionTags, Error,
		TEXT("[FFactionTags::OppositeOf] 输入阵营无效: '%s' — 必须为 Faction.Offense 或 Faction.Defense, "
		     "调用方需 check(FFactionTags::IsValidFaction(Tag))"),
		*Tag.ToString());
	return FGameplayTag::EmptyTag;
}

ETeamAttitude::Type FFactionTags::AttitudeBetween(const FGameplayTag& Self, const FGameplayTag& Other)
{
	// 大厂原则: 阵营协议层无 invalid 兜底 — 任一无效 → 中立, 但同时 Error 日志
	if (!IsValidFaction(Self) || !IsValidFaction(Other))
	{
		UE_LOG(LogFactionTags, Error,
			TEXT("[FFactionTags::AttitudeBetween] 阵营无效: Self='%s' Other='%s' — 应当调用前 check IsValidFaction"),
			*Self.ToString(), *Other.ToString());
		return ETeamAttitude::Neutral;
	}
	return IsSameSide(Self, Other) ? ETeamAttitude::Friendly : ETeamAttitude::Hostile;
}

/**
 * @brief 扣血授权守卫 — 检查攻击者能否对受击者造成伤害
 * @param AttackerFaction 攻击者阵营 Tag
 * @param VictimFaction 受击者阵营 Tag
 * @param Context 调用方上下文(用于日志定位,允许 nullptr)
 * @param AttackerName 攻击者名称(日志用)
 * @param VictimName 受击者名称(日志用)
 * @return true = 允许扣血,false = 拒绝(同阵营/任一无效)
 *
 * 大厂原则 - 单一节流点:所有扣血路径必须先调此函数 (Server_ReportHit/TakeDamage/AIAttack)
 * 三层守卫:阵营无效 → 拒绝;同阵营 → 拒绝(用户核心需求:队友之间不能有队伤);异阵营 → 通过
 * 零兜底:任何无效路径都 Log Error 强制修复,不允许静默 fallback
 */
bool FFactionTags::CanDamage(
	const FGameplayTag& AttackerFaction,
	const FGameplayTag& VictimFaction,
	const TCHAR* Context,
	const FString& AttackerName,
	const FString& VictimName)
{
	// 【2026.07.11 P0 大厂架构】扣血授权守卫
	//
	// 大厂原则 (零兜底 + 显式优先于默认):
	//   1. 攻击者阵营无效 → 拒绝扣血 + Log Error (强制修复 Pawn.FactionTag 同步链路)
	//   2. 受击者阵营无效 → 拒绝扣血 + Log Error (同上)
	//   3. 同阵营 → 拒绝扣血 + Log Warning (用户需求: 队友之间不能有队伤)
	//   4. 异阵营 + 均有效 → 通过
	//
	// 为什么阵营无效也要拒绝而非走默认:
	//   - 默认行为就是"当成 Friendly 不打" 或 "当成 Hostile 随便打", 无论哪种都是兜底
	//   - 我们必须显式知道谁打了谁, 配置错误必须立刻暴露 (大厂原则)
	//   - 敌人误判成友军, 玩家看到 AI 偷懒; 友军误判成敌人, 玩家看到自己人打自己人
	//     两种都是 BUG, 必须拒绝执行扣血, 让策划/程序修复配置
	//
	// 调用方 (大厂原则 - 单一真理源):
	//   - ABaseWeapon::Server_ReportHit_Implementation (玩家/AI 攻击共用通道)
	//   - ABaseCharacter::Server_ReportAIAttackHit_Implementation (AI 备用通道)
	//   - ABaseCharacter::TakeDamage (兜底拦截, 防止漏网之鱼)
	const TCHAR* Ctx = Context ? Context : TEXT("FFactionTags::CanDamage");

	// ===== 守卫 1: 攻击者阵营必须有效 =====
	if (!ValidateFactionOrReportError(AttackerFaction, Ctx))
	{
		UE_LOG(LogFactionTags, Error,
			TEXT("[%s] 拒绝扣血: Attacker='%s' 阵营无效 ('%s'). "
			     "大厂原则 - 零兜底: 攻击者必须显式有效阵营, 禁止 fallback. "
			     "修复: 确认 ABaseCharacter::SyncFactionTagFromController 链路已正确同步 Pawn.FactionTag"),
			Ctx, *AttackerName, *AttackerFaction.ToString());
		return false;
	}

	// ===== 守卫 2: 受击者阵营必须有效 =====
	if (!ValidateFactionOrReportError(VictimFaction, Ctx))
	{
		UE_LOG(LogFactionTags, Error,
			TEXT("[%s] 拒绝扣血: Victim='%s' 阵营无效 ('%s'). "
			     "大厂原则 - 零兜底: 受击者也必须显式有效阵营, 禁止 fallback. "
			     "修复: 确认受击者 Pawn 的 FactionTag 同步链路 (v27 已加 Replicated)"),
			Ctx, *VictimName, *VictimFaction.ToString());
		return false;
	}

	// ===== 守卫 3: 同阵营不互砍 (用户核心需求) =====
	// 走 IsSameSide 严格匹配, 不会误判子 Tag
	if (IsSameSide(AttackerFaction, VictimFaction))
	{
		UE_LOG(LogFactionTags, Warning,
			TEXT("[%s] 拒绝扣血 - 同阵营 (Friendly Fire 守卫): Attacker='%s' (Faction=%s) "
			     "-> Victim='%s' (Faction=%s). 队友之间不允许有队伤."),
			Ctx,
			*AttackerName, *AttackerFaction.ToString(),
			*VictimName, *VictimFaction.ToString());
		return false;
	}

	// ===== 守卫全部通过: 异阵营 + 均有效 =====
	return true;
}

// ===== FGenericTeamId ↔ FGameplayTag 双向转换 (UE AIPerception 接口适配层) =====
//
// 背景: UE 的 AIPerception 必须用 FGenericTeamId (整数协议), 不能直接传 FGameplayTag
//       业务代码只认 FGameplayTag, 所以这两个函数是"UE 协议层"的桥接器
//       业务代码应该用 Offense()/Defense() 直接拿到 FGameplayTag, 走 AttitudeBetween 判定
//       只有在 SetGenericTeamId / GetGenericTeamId override 实现里才调这两个
//
// 大厂原则: 不兜底 — 任何非 Offense/Defense 都显式报错, 让调用方中断

FGenericTeamId FFactionTags::ToGenericTeamId(const FGameplayTag& Tag)
{
	if (Tag == Offense()) { return FGenericTeamId(0); } // Offense -> Team 0
	if (Tag == Defense()) { return FGenericTeamId(1); } // Defense -> Team 1

	// 非有效阵营 — 显式报错 (大厂原则: 无兜底)
	UE_LOG(LogFactionTags, Error,
		TEXT("[FFactionTags::ToGenericTeamId] 非有效阵营 Tag='%s' — 必须为 Faction.Offense 或 Faction.Defense"),
		*Tag.ToString());
	return FGenericTeamId::NoTeam;
}

FGameplayTag FFactionTags::FromGenericTeamId(const FGenericTeamId& TeamId)
{
	const int32 Id = TeamId.GetId();
	if (Id == 0) { return Offense(); }
	if (Id == 1) { return Defense(); }

	// 其它 ID (NoTeam 等) 视为"无阵营" — 大厂原则: 显式记录, 不静默默认
	//   注意: NoTeam 是 UE 协议层的合法状态 (AI 未配置阵营时), 不是错误
	//         仅当 Id 是 0/1/NoTeam 之外的值才是错误
	if (Id != FGenericTeamId::NoTeam.GetId())
	{
		UE_LOG(LogFactionTags, Error,
			TEXT("[FFactionTags::FromGenericTeamId] 非法 FGenericTeamId=%d, 必须为 0 (Offense) / 1 (Defense) / 255 (NoTeam)"),
			Id);
	}
	return FGameplayTag::EmptyTag;
}

FString FFactionTags::GetDisplayName(const FGameplayTag& Tag)
{
	if (Tag == Offense())
	{
		return TEXT("攻方");
	}
	if (Tag == Defense())
	{
		return TEXT("守方");
	}
	// 无效阵营 — 显式报告, 不返回空串
	UE_LOG(LogFactionTags, Error,
		TEXT("[FFactionTags::GetDisplayName] 未知阵营 Tag='%s'"),
		*Tag.ToString());
	return FString::Printf(TEXT("UNKNOWN(%s)"), *Tag.ToString());
}

bool FFactionTags::IsDeprecatedLegacyFaction(const FGameplayTag& Tag)
{
	// 【2026.07.10 P0 重构】检测旧 Tag (大厂原则: 不静默兜底, 只检测不替换)
	const FName TagName = Tag.GetTagName();
	return TagName == FName(TEXT("Faction.Player"))
		|| TagName == FName(TEXT("Faction.Zombie"))
		|| TagName == FName(TEXT("Faction.Enemy"))
		|| TagName == FName(TEXT("Faction.FriendlyAI"));
}

bool FFactionTags::ValidateFactionOrReportError(const FGameplayTag& Tag, const TCHAR* Context)
{
	// 大厂原则: 零兜底
	//   1. 空 Tag → 报错
	//   2. 旧 Tag → 报错 (强制修复 Data Asset)
	//   3. 非 Offense/Defense → 报错 (业务应该用 Offense/Defense)
	//   4. 有效 → 返回 true

	if (!Tag.IsValid())
	{
		UE_LOG(LogFactionTags, Error,
			TEXT("[%s] 阵营无效: 空 Tag — 调用方必须显式设 Offense 或 Defense, 不允许默认空"),
			Context ? Context : TEXT("FFactionTags::ValidateFactionOrReportError"));
		return false;
	}

	if (IsDeprecatedLegacyFaction(Tag))
	{
		UE_LOG(LogFactionTags, Error,
			TEXT("[%s] 阵营使用了已废弃的旧 Tag='%s' — 必须改为 Faction.Offense 或 Faction.Defense。"
			     "【v54 修复】打开对应的 DataAsset (DA_AIBehaviorConfig_XXX, DA_AIProfile_XXX 已删除), "
			     "把 FactionTag 字段从 '%s' 改为 Faction.Offense 或 Faction.Defense"),
			Context ? Context : TEXT("FFactionTags::ValidateFactionOrReportError"),
			*Tag.ToString(),
			*Tag.ToString());
		return false;
	}

	if (!IsValidFaction(Tag))
	{
		UE_LOG(LogFactionTags, Error,
			TEXT("[%s] 阵营非有效 Tag='%s' — 必须为 Faction.Offense 或 Faction.Defense, 不允许其他值"),
			Context ? Context : TEXT("FFactionTags::ValidateFactionOrReportError"),
			*Tag.ToString());
		return false;
	}

	return true;
}