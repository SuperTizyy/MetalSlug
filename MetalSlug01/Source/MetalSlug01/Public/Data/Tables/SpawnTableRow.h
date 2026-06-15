// ==========================================
// 玩家生成/出生数据表行
// 关联: DT_PlayerSpawnData
// 替代: 原 StaticTable.h 中的 FPlayerSpawnData 段
// ==========================================
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
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

	/** 出生阵营 (0=攻方, 1=守方) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawn Data")
	int32 TeamID = 0;

	/** 是否主机玩家 (用于权威生成逻辑) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawn Data")
	bool bIsHost = false;
};
