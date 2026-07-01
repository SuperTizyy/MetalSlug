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
	 * 【Phase 1 重构】出生阵营 (用 GameplayTag, 走 IGenericTeamAgentInterface)
	 * 示例值: Faction.Player / Faction.Zombie
	 * 兼容: 同时保留 TeamID int32 用于旧 DataTable 资产迁移期
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
