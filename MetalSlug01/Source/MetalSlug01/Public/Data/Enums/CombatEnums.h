// ==========================================
// 战斗相关枚举
// 作用: 拆出原 StaticTable.h 中的 EWeaponMeshType/EKillMethod/EACERankType/EKillStreakType
// 优势: 武器/战斗 UI 只需要此头, 与房间枚举解耦
// ==========================================
#pragma once

#include "CoreMinimal.h"
#include "CombatEnums.generated.h"

/**
 * @enum EWeaponMeshType
 * @brief 武器模型类型 (v56 重构 — 同时决定战斗策略)
 *
 * 用户在 BP 中手动放置 StaticMeshComponent 和/或 SkeletalMeshComponent，
 * 然后配置此枚举告诉 C++ 使用哪个组件，以及使用哪种战斗策略。
 *
 * v56 设计:
 *   - MeshType 直接决定战斗策略（不需要单独的 WeaponType）
 *   - Melee → StaticMesh + BoxTrace (近战)
 *   - Primary/Secondary → SkeletalMesh + LineTrace (远程)
 */
UENUM(BlueprintType)
enum class EWeaponMeshType : uint8
{
	None        UMETA(DisplayName = "无（未配置）"),
	Melee       UMETA(DisplayName = "近战武器（静态网格体）"),
	Primary     UMETA(DisplayName = "主武器（骨骼网格体）"),
	Secondary   UMETA(DisplayName = "副武器（骨骼网格体）")
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
 * @brief 连杀类型（用于连杀图标 + 击杀音效显示）
 * - None: 无
 * - Headshot: 爆头
 * - OneKill ~ FiveKills: 一杀到五杀 (封顶语义: >= 5 杀都归 FiveKills)
 *
 * 业务规则 (用户 2026.07.26):
 *   - "连杀超过五杀,再激活连杀还是五杀图标和声音"
 *   - 即: 第 6 / 7 / 8 杀都显示 FiveKills 图标 + 播放 FiveKills 音效,不区分
 *   - 实现方式: 镜像函数 GetKillStreakType 用 Kills >= 5 → FiveKills (封顶,无新枚举值)
 */
UENUM(BlueprintType)
enum class EKillStreakType : uint8
{
	None          UMETA(DisplayName = "无"),
	Headshot      UMETA(DisplayName = "爆头"),
	OneKill       UMETA(DisplayName = "一杀"),
	TwoKills      UMETA(DisplayName = "二杀"),
	ThreeKills    UMETA(DisplayName = "三杀"),
	FourKills     UMETA(DisplayName = "四杀"),
	FiveKills     UMETA(DisplayName = "五杀")
};

/**
 * @enum EFireMode
 * @brief 枪械开火模式 (v60 大厂主流 — 数据驱动)
 *
 * 真理源: FWeaponInfo::FireMode (DT_WeaponInfo 字段, BP 策划可调)
 * 读取路径: WeaponFireComponent::InitializeFromWeaponConfig → 写入 CurrentFireMode
 *
 * 语义:
 *   - Semi: 半自动 — 每次按下扳机打一发, 松开/连按都按"按一次打一发"算
 *   - Auto: 全自动 — 按住扳机持续打, 射速由 FireRateRPM 决定
 *   - Burst3: 三连发 — 每次按下打 3 发 (CSGO Glock/M4A1 模式, 留作未来扩展)
 *
 * 大厂原则:
 *   - 加新模式只需扩展枚举 + 在 WeaponFireComponent 内加分支, 不改外部接口
 *   - 模式是武器固有属性 (DT 配), 不是角色属性 — 玩家/AI 共用同一逻辑
 */
UENUM(BlueprintType)
enum class EFireMode : uint8
{
	Semi    UMETA(DisplayName = "半自动 (按一次打一发)"),
	Auto    UMETA(DisplayName = "全自动 (按住持续打)"),
	Burst3  UMETA(DisplayName = "三连发 (按一次打三发)")
};
