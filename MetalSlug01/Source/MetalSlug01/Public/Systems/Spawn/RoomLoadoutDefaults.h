// ==========================================
// FRoomLoadoutDefaults — Loadout 业务默认 RowName 集中管理
// ==========================================
//
// 【v212 大厂架构 — 业务默认值单一真理源 (Single Source of Truth for Business Defaults)】
//
// 根因 (用户 2026.08.09):
//   "玩家如果没选近战武器, 那就默认使用 DT_WeaponInfo 的 RowName=JZ001 的武器,
//    选了就把所选的近战武器带入游戏中"
//
// 旧版 (v209 / v211) 问题:
//   - RoomSpawnSubsystem::ResolveDefaultWeaponRowName(1) 用 "DT 第 2 行" 兜底
//   - RoomInsidePage::InitializeTempSelectedWeaponsByDefault 用 "DT 第 1 个 Melee 类型" 兜底
//   - 两处 "业务默认" 各写各的, 业务默认值 = DT 行序, 隐式约定不明确
//   - 策划改 DT 第 2 行武器 → 业务默认悄悄改变 → 玩家行为变了但代码看不出原因
//
// v212 修复 (大厂架构):
//   1. 显式声明业务默认值 = RowName=JZ001 (用户明确指定)
//   2. 集中在 FRoomLoadoutDefaults 常量类, 所有调用方共享同一真理源
//   3. 零兜底: DT_WeaponInfo 找不到 JZ001 → Log Error + 拒绝 Spawn (不 fallback 到第 1 行)
//   4. 零重复: 客户端 UI 预填 + 服务器 Spawn 兜底用同一个常量
//
// 调用方:
//   - URoomInsidePage::InitializeTempSelectedWeaponsByDefault 读 MeleeDefaultRowName
//   - URoomSpawnSubsystem::HandlePlayerRequestSpawn 读 MeleeDefaultRowName (替换 ResolveDefaultWeaponRowName(1))
//
// 大厂原则:
//   - 业务默认值集中管理 (避免 "DT 行序约定" 这种隐性耦合)
//   - 用户业务需求文字 → 代码常量直接映射 (JZ001 ← "玩家没选近战武器默认这把")
//   - 修改业务默认只改一处 (FRoomLoadoutDefaults::MeleeDefaultRowName)
//
// ==========================================

#pragma once

#include "CoreMinimal.h"

/**
 * FRoomLoadoutDefaults
 *
 * 业务默认 Loadout RowName 常量集中管理。
 *
 * 命名规范: 业务默认值用 RowName 字符串 (DT_WeaponInfo 行名) 而非 DT 行序号,
 *   这样 DT 行序调整不会影响业务默认行为。
 */
struct METALSLUG01_API FRoomLoadoutDefaults
{
	/**
	 * 玩家未选主武器时, 默认带入游戏的主武器 RowName
	 *
	 * 注意: 主武器的"业务默认"目前由两个路径决定:
	 *   1. 存档层 (AccountSub->SaveLastSelectedWeapon(WSlot, DefaultWeapon)) — 第一次进大厅自动存档
	 *   2. 服务器 Spawn 兜底 (ResolveDefaultWeaponRowName(0) → DT 第 1 行)
	 *
	 * 如果要走 v212 单一真理源, 可在后续版本统一替换为 PrimaryDefaultRowName
	 * 当前未启用, 保留 v209 v54.3 的历史行为
	 */
	static const FString PrimaryDefaultRowName;

	/**
	 * 玩家未选近战武器时, 默认带入游戏的近战武器 RowName (用户 2026.08.09 明确指定)
	 *
	 * 用户原话:
	 *   "玩家如果没选近战武器, 那就默认使用 DT_WeaponInfo 的 RowName=JZ001 的武器"
	 *
	 * 零兜底约束:
	 *   - DT_WeaponInfo 找不到 JZ001 → Log Error + 拒绝 Spawn (不 fallback 到第 1 个 Melee)
	 *   - 不允许运行时计算 DT 行序号 (避免策划改 DT 行序时业务默认悄悄改变)
	 */
	static const FString MeleeDefaultRowName;
};
