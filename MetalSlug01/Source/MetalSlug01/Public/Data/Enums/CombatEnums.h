// ==========================================
// 战斗相关枚举
// 作用: 拆出原 StaticTable.h 中的 EWeaponType/EKillMethod/EACERankType/EKillStreakType
// 优势: 武器/战斗 UI 只需要此头, 与房间枚举解耦
// ==========================================
#pragma once

#include "CoreMinimal.h"
#include "CombatEnums.generated.h"

/**
 * @enum EWeaponType
 * @brief 武器类型（用于区分主武器、副武器、近战武器）
 */
UENUM(BlueprintType)
enum class EWeaponType : uint8
{
	None        UMETA(DisplayName = "无"),
	Primary     UMETA(DisplayName = "主武器"),
	Secondary   UMETA(DisplayName = "副武器"),
	Melee       UMETA(DisplayName = "近战武器")
};

/**
 * @enum EACERankType
 * @brief ACE 排名类型（决定 HUD 上 ACE 文字颜色）
 * - None: 无 ACE
 * - White: 队内第一（白色）
 * - Gold: 全场第一（金色）
 */
UENUM(BlueprintType)
enum class EACERankType : uint8
{
	None    UMETA(DisplayName = "无ACE（不显示金色或白色）"),
	White   UMETA(DisplayName = "队内第一（白色）"),
	Gold    UMETA(DisplayName = "全场第一（金色）")
};

/**
 * @enum EKillMethod
 * @brief 击杀方式（用于击杀信息显示）
 */
UENUM(BlueprintType)
enum class EKillMethod : uint8
{
	None                UMETA(DisplayName = "无"),
	PrimaryHeadshot     UMETA(DisplayName = "主武器爆头"),
	PrimaryWeapon       UMETA(DisplayName = "主武器"),
	SecondaryHeadshot   UMETA(DisplayName = "副武器爆头"),
	SecondaryWeapon     UMETA(DisplayName = "副武器"),
	MeleeHeadshot       UMETA(DisplayName = "近战武器爆头"),
	MeleeWeapon         UMETA(DisplayName = "近战武器")
};

/**
 * @enum EKillStreakType
 * @brief 连杀类型（用于连杀图标显示）
 * - None: 无
 * - Headshot: 爆头
 * - OneKill ~ FiveKills: 一杀到五杀
 */
UENUM(BlueprintType)
enum class EKillStreakType : uint8
{
	None        UMETA(DisplayName = "无"),
	Headshot    UMETA(DisplayName = "爆头"),
	OneKill     UMETA(DisplayName = "一杀"),
	TwoKills    UMETA(DisplayName = "二杀"),
	ThreeKills  UMETA(DisplayName = "三杀"),
	FourKills   UMETA(DisplayName = "四杀"),
	FiveKills   UMETA(DisplayName = "五杀")
};
