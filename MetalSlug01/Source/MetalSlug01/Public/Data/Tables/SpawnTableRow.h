// ==========================================
// 头文件包含区
// ==========================================
#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "GameplayTagContainer.h"
#include "SpawnTableRow.generated.h"

class ABaseWeapon;
class ABaseCharacter;

/**
 * @struct FPlayerSpawnData
 * @brief 玩家出生数据
 * 用途: 关联 DT_PlayerSpawnData, 记录每个玩家在战斗地图的出生配置
 */
USTRUCT(BlueprintType)
struct FPlayerSpawnData : public FTableRowBase
{
	GENERATED_BODY()

	/** 目标玩家 PlayerID */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawn Data")
	FString PlayerID;

	/** 出生时装备的武器蓝图 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawn Data")
	TSoftClassPtr<ABaseWeapon> StartingWeapon;

	/** 出生时使用的角色蓝图 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawn Data")
	TSoftClassPtr<ABaseCharacter> StartingCharacter;

	/**
	 * 【2026.07.10 P0 重构】出生阵营 — FGameplayTag, 单一真理源
	 *
	 * 有效值: Faction.Offense / Faction.Defense
	 * 阵营是**通用身份** — 玩家和 AI 都可配这两个 Tag, 与角色类型正交
	 *
	 * 兼容说明:
	 *   - 历史兼容: TeamID int32 字段保留, 旧 DataTable 资产迁移期使用
	 *   - 旧字段 (TeamID) 字段已 DEPRECATED, 业务代码应读 FactionTag
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawn Data",
		meta = (Categories = "Faction"))
	FGameplayTag FactionTag;

	/** 旧字段 - 保留 1 次迁移期, 之后 DataTable 资产迁移完毕可移除 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawn Data|Deprecated")
	int32 TeamID = 0;

	/** 是否主机玩家 (用于权威生成逻辑) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawn Data")
	bool bIsHost = false;
};
