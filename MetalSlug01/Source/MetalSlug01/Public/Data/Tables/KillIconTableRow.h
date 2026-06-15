// ==========================================
// 击杀图标/连杀图标表行
// 关联: DT_KillIconInfo / DT_KillStreakIconInfo
// 替代: 原 StaticTable.h 中的 FKillIconInfo / FKillStreakIconInfo 段
// ==========================================
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "../Enums/CombatEnums.h" // 依赖 EKillMethod / EKillStreakType
#include "KillIconTableRow.generated.h"

class UTexture2D;

/**
 * @struct FKillIconInfo
 * @brief 击杀图标信息表行
 * 用途: 关联 DT_KillIconInfo, 让 HUD 根据击杀方式显示对应图标
 */
USTRUCT(BlueprintType)
struct FKillIconInfo : public FTableRowBase
{
	GENERATED_BODY()

public:
	/** 击杀方式枚举 (查找键) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kill Icon Data")
	EKillMethod KillMethod;

	/** 击杀图标 (供 HUD 击杀信息显示) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kill Icon Data")
	TObjectPtr<UTexture2D> KillIcon;
};

/**
 * @struct FKillStreakIconInfo
 * @brief 连杀图标信息表行
 * 用途: 关联 DT_KillStreakIconInfo, 让 HUD 根据连杀数显示对应图标
 */
USTRUCT(BlueprintType)
struct FKillStreakIconInfo : public FTableRowBase
{
	GENERATED_BODY()

public:
	/** 连杀类型枚举 (查找键) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kill Streak Icon Data")
	EKillStreakType StreakType;

	/** 连杀图标 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kill Streak Icon Data")
	TObjectPtr<UTexture2D> StreakIcon;
};
