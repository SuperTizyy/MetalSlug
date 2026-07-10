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