// ==========================================
// 房间相关枚举
// 作用: 拆出原 StaticTable.h 中的 ERoomState/ERoomMatchMode
// 优势: RoomGameMode/State/PC 只需要此头, 不再被其他无关表污染
//
// [2026.07.10 大厂阵营重构]
//   已删除 ERoomTeam — 阵营表达统一走 FGameplayTag (见 Data/Faction/FactionTags.h)
//   不再保留 None/Attack/Defense 三态, 因为 Offense/Defense 双阵营 + IsValidFaction 已足够表达
// ==========================================
#pragma once

#include "CoreMinimal.h"
#include "RoomEnums.generated.h"

/**
 * @enum ERoomState
 * @brief 房间状态机
 * - WaitingInRoom: 大厅等待选人状态
 * - BattleInProgress: 战斗进行状态
 */
UENUM(BlueprintType)
enum class ERoomState : uint8
{
	WaitingInRoom UMETA(DisplayName = "大厅等待选人状态"),
	BattleInProgress UMETA(DisplayName = "战斗进行状态")
};

/**
 * @enum ERoomMatchMode
 * @brief 房间比赛模式
 * - None: 无模式
 * - Melee: 刀战模式（30 分钟）
 * - Zombie: 生化模式（10 分钟）
 *
 * 阵营 Tag 由 FAIModeRules (ARoomGameMode::ModeRulesByMode) 决定:
 *   - Melee:  AttackTeamFaction=Faction.Offense, DefenseTeamFaction=Faction.Defense
 *   - Zombie: AttackTeamFaction=Faction.Offense (僵尸), DefenseTeamFaction=Faction.Defense (人类)
 */
UENUM(BlueprintType)
enum class ERoomMatchMode : uint8
{
	None		UMETA(DisplayName = "None"),
	Melee		UMETA(DisplayName = "刀战模式"), // 30 分钟
	Zombie		UMETA(DisplayName = "生化模式")  // 10 分钟
};

/**
 * @enum EZombieRoundWinner
 * @brief 生化模式单小局赢家 (用户 2026.08.06 新增)
 *
 * 业务规则:
 *   - 每小局结束时, 判定"是否还有人类活着" 来确定赢家
 *   - 任何 非母体 + 存活 的 Pawn (玩家 + AI) 算人类
 *   - 没有人类活着 → 母体赢
 *   - 倒计时结束但还有人类 → 人类赢
 *
 * 大厂原则 — 单一真理源:
 *   - GameState.RoundWinner (Replicated) 是唯一字段, UI 订阅
 *   - GameHUDWidget 缓存 + OnRep_RoundWinner 触发音效播放
 *   - 不允许其他类创建自定义"哪边赢"判定
 *
 * 大厂原则 — 零兜底:
 *   - None 用于"尚未结算", 调用方拒绝基于 None 触发后续逻辑
 *   - 业务流程强制要求 RoundWinner 必为 Human 或 Mother, 不允许 None 进入"下一局"或"音效播放"
 */
UENUM(BlueprintType)
enum class EZombieRoundWinner : uint8
{
	None		UMETA(DisplayName = "尚未结算"),
	Human		UMETA(DisplayName = "人类赢"),
	Mother		UMETA(DisplayName = "母体赢")
};

/**
 * @enum EHASResult (v134 v4 大厂架构新增)
 * @brief 场上人类存活判定结果 — 解决"开局面板无 Pawn 时不应触发母体赢音效" bug
 *
 * 业务规则:
 *   - HasAliveHuman: 至少有 1 个 Defense 阵营 + 活着的 Pawn
 *   - NoAliveHuman: 场上没有 Defense 阵营 活着的 Pawn (但可能有 Offense 或配置错)
 *   - NoData: 场上没有任何 Pawn (玩家 + AI 都没 Spawn) — 业务上不应触发胜负判定
 *
 * 大厂原则 — 零兜底 (修复 v134 v3 bug):
 *   - 旧版 (bool) 兜底: 0 个存活 Pawn → return false → LifecycleSubsystem 解释为"母体赢" → 错误播母体赢音效
 *   - 新版 (enum) 显式: 0 个 Pawn → 返回 NoData → LifecycleSubsystem 强制跳过本 Tick 结算
 *
 * 单一真理源:
 *   - GameState.HasAliveHumanOnField() 是唯一"人验收者"判定入口
 *   - LifecycleSubsystem 收到 NoData 必须跳过, 不允许走 finish 路径
 */
UENUM(BlueprintType)
enum class EHASResult : uint8
{
	NoData		UMETA(DisplayName = "无数据 (场上 0 Pawn, 跳过本 Tick)"),
	HasAliveHuman	UMETA(DisplayName = "有人类活着"),
	NoAliveHuman	UMETA(DisplayName = "无人存活")
};