// ==========================================
// 活动运行时状态
// 职责: 存放 Activity 子系统层的运行时内存缓存结构
// 与 UActivitySaveGame（持久化层）严格区分, 不混入 SaveGame
// 关联: UActivitySubsystem / URedDotManager
// ==========================================
#pragma once

#include "CoreMinimal.h"
#include "Data/Tables/ItemTableRow.h"  // FRedDotData
#include "Data/Enums/ActivityEnums.h"   // EActivityStatus
#include "ActivityRuntimeState.generated.h"

/**
 * @struct FActivityRuntimeState
 * @brief 活动运行时状态结构
 * @details 包含活动的动态运行时信息, 不保存到 SaveGame
 * @note 这些状态在程序运行时动态计算和更新, 属于 Activity 子系统内存缓存范畴
 */
USTRUCT(BlueprintType)
struct FActivityRuntimeState
{
	GENERATED_BODY()

public:
	/** 活动当前状态 */
	UPROPERTY(BlueprintReadOnly, Category = "Runtime|Time")
	EActivityStatus CurrentStatus = EActivityStatus::Active;

	/** 距离开始的剩余时间（秒） */
	UPROPERTY(BlueprintReadOnly, Category = "Runtime|Time")
	float TimeUntilStart = 0.0f;

	/** 距离结束的剩余时间（秒） */
	UPROPERTY(BlueprintReadOnly, Category = "Runtime|Time")
	float TimeUntilEnd = 0.0f;

	/** 是否处于开始前提醒期 */
	UPROPERTY(BlueprintReadOnly, Category = "Runtime|Time")
	bool bInPreNoticePeriod = false;

	/** 是否处于结束前提醒期 */
	UPROPERTY(BlueprintReadOnly, Category = "Runtime|Time")
	bool bInEndWarningPeriod = false;

	/** 当前循环周期索引 */
	UPROPERTY(BlueprintReadOnly, Category = "Runtime|Time")
	int32 CurrentCycleIndex = 0;

	/** 在 UI 中是否被选中 */
	UPROPERTY(BlueprintReadOnly, Category = "Runtime|UI")
	bool bIsSelectedInUI = false;

	/** UI 显示的红点数据 */
	UPROPERTY(BlueprintReadOnly, Category = "Runtime|UI")
	FRedDotData UIDotData;

	/** 是否在导航中显示 */
	UPROPERTY(BlueprintReadOnly, Category = "Runtime|UI")
	bool bShowInNavigation = true;

	FActivityRuntimeState() = default;
};
