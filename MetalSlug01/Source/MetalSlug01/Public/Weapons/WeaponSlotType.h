// ==========================================
// 武器槽位类型 (大厂架构 v52 — 生化模式运行时切换)
//
// 【设计动机】
//   旧 UWeaponAttachmentComponent::CurrentWeapon 单字段 → 角色只有一把武器
//   生化模式需求: 玩家可拥有主武器(枪) + 副武器 + 近战(刀) 三把武器, 战斗中按 Q 循环切换
//
// 【大厂原则 — 单一真理源】
//   - 槽位真理源 = PlayerState.SelectedWeaponID1 / SelectedWeaponID2 / SelectedWeaponID3 (大厅已选)
//   - 运行时切槽位 = 改 CurrentWeaponSlot + 自动显示/隐藏对应武器
//   - 不强制 AI 路径装备多武器 (AI 仍走单武器 CurrentWeapon 旧路径)
//
// 【槽位语义】
//   Primary:   主武器 (枪械) — 真理源 SelectedWeaponID1
//   Secondary: 副武器 (预留/扩展槽位) — 真理源 SelectedWeaponID2
//   Melee:     近战武器 (刀/铲/斧) — 真理源 SelectedWeaponID3
// ==========================================
#pragma once

#include "CoreMinimal.h"
#include "WeaponSlotType.generated.h"

/**
 * @enum EWeaponSlotType
 * @brief 武器槽位类型 (运行时切换粒度)
 *
 * 【v60.7 大厂架构 — 枚举值严格对齐业务顺序 + 数组下标】
 *   旧 (v52-v60.6) 反模式:
 *     enum { None=0, Primary=1, Melee=2, Secondary=3 }
 *     - Melee 排第二, Secondary 排第三 (反业务顺序: 玩家视角是 Primary→Secondary→Melee)
 *     - 按键 2 触发 Secondary 时, 日志显示 "槽位 3" (值=3), 与按键 2 矛盾 → 用户困惑
 *     - 按键 3 触发 Melee 时, 日志显示 "槽位 2" (值=2), 与按键 3 矛盾 → 用户困惑
 *
 *   新 (v60.7) 大厂原则 — 单一真理源 + 业务顺序:
 *     enum { None=0, Primary=1, Secondary=2, Melee=3 }
 *     - uint8 值严格对应业务顺序 (1=主, 2=副, 3=近)
 *     - 数组下标 = uint8 值 - 1 (Primary=1→[0], Secondary=2→[1], Melee=3→[2])
 *     - 按键 1/2/3 完美对应: 按键 N → uint8=N → 数组 [N-1]
 *     - 日志显示 uint8 = 槽位号 (一致!)
 *
 * 索引顺序 (与 WeaponsInSlot 数组下标严格对齐):
 *   - 0 = Primary
 *   - 1 = Secondary
 *   - 2 = Melee
 */
UENUM(BlueprintType)
enum class EWeaponSlotType : uint8
{
	None      UMETA(DisplayName = "无"),
	Primary   UMETA(DisplayName = "主武器 (枪械)"),
	Secondary UMETA(DisplayName = "副武器 (扩展槽)"),
	Melee     UMETA(DisplayName = "近战武器")
};

/**
 * 槽位枚举转可读字符串 (v60.6 — 日志可观测性)
 *
 * 大厂原则 — 显式优于隐式:
 *   - 日志必须用名字 (Primary/Secondary/Melee) 而不是 uint8 值 (1/2/3)
 *   - 避免用户看到 "目标槽位 2 没有武器" 困惑是 Primary 还是 Melee
 *   - 与 DisplayName 一致, BP 编辑器也用同一套
 */
inline const TCHAR* LexToString(EWeaponSlotType Slot)
{
	switch (Slot)
	{
	case EWeaponSlotType::Primary:   return TEXT("Primary");
	case EWeaponSlotType::Secondary: return TEXT("Secondary");
	case EWeaponSlotType::Melee:     return TEXT("Melee");
	case EWeaponSlotType::None:
	default:                          return TEXT("None");
	}
}