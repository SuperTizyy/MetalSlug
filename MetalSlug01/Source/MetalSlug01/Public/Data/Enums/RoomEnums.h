// ==========================================
// 房间相关枚举
// 作用: 拆出原 StaticTable.h 中的 ERoomState/ERoomTeam/ERoomMatchMode
// 优势: RoomGameMode/State/PC 只需要此头, 不再被其他无关表污染
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
 * @enum ERoomTeam
 * @brief 队伍类型
 * - None: 未分配
 * - Attack: 攻方
 * - Defense: 守方
 * 规范: 强类型枚举比 bool/int 更具可读性和扩展性
 */
UENUM(BlueprintType)
enum class ERoomTeam : uint8
{
	None    UMETA(DisplayName = "未分配"),
	Attack  UMETA(DisplayName = "攻方"),
	Defense UMETA(DisplayName = "守方")
};

/**
 * @enum ERoomMatchMode
 * @brief 房间比赛模式
 * - None: 无模式
 * - Melee: 刀战模式（30 分钟）
 * - Zombie: 生化模式（10 分钟）
 */
UENUM(BlueprintType)
enum class ERoomMatchMode : uint8
{
	None		UMETA(DisplayName = "None"),
	Melee		UMETA(DisplayName = "刀战模式"), // 30 分钟
	Zombie		UMETA(DisplayName = "生化模式")  // 10 分钟
};
