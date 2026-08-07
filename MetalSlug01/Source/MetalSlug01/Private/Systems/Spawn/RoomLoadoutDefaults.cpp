// ==========================================
// FRoomLoadoutDefaults 实现 — 业务默认常量定义
// ==========================================
//
// 【v212 大厂架构】所有 Loadout 业务默认值集中在此文件
//
// 修改业务默认武器时, 改这里的常量即可 (单一真理源)
// ==========================================

#include "Systems/Spawn/RoomLoadoutDefaults.h"

// 主武器默认 (当前 v209/v54.3 沿用 DT 第 1 行, 未启用 v212 集中)
const FString FRoomLoadoutDefaults::PrimaryDefaultRowName = TEXT("BQ001");

// 近战武器默认 (用户 2026.08.09 明确指定 JZ001)
//
// 业务含义: "玩家没选近战武器, 默认这把"
// 配置约束: DT_WeaponInfo 必须有 RowName=JZ001 行, 否则零兜底报错
const FString FRoomLoadoutDefaults::MeleeDefaultRowName = TEXT("JZ001");
