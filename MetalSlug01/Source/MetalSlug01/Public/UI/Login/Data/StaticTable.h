// ==========================================
// 【已废弃】: 兼容层, 推荐新代码直接 include 对应的子表头
// ==========================================
// 完整类型/枚举已拆分到以下子头:
//   - Data/Enums/RoomEnums.h         -> ERoomState / ERoomTeam / ERoomMatchMode
//   - Data/Enums/CombatEnums.h       -> EWeaponType / EACERankType / EKillMethod / EKillStreakType
//   - Data/Tables/CharacterTableRow.h -> FCharacterInfo
//   - Data/Tables/WeaponTableRow.h    -> FWeaponInfo / FWeaponAttachmentConfig
//   - Data/Tables/MapTableRow.h       -> FMapInfoRow
//   - Data/Tables/SpawnTableRow.h     -> FPlayerSpawnData
//   - Data/Tables/KillIconTableRow.h  -> FKillIconInfo / FKillStreakIconInfo
//
// 【迁移建议】:
//   - 只用 ERoom* 的文件:    #include "Data/Enums/RoomEnums.h"
//   - 只用 EWeaponType 等:   #include "Data/Enums/CombatEnums.h"
//   - 角色表行相关:          #include "Data/Tables/CharacterTableRow.h"
//   - 等等
//
// 本文件保留仅为兼容旧 include, 内部已经聚合所有子头
// ==========================================
#pragma once

#include "CoreMinimal.h"

// 聚合子头, 一次性 include 后行为与原 StaticTable.h 完全一致
#include "Data/Enums/RoomEnums.h"
#include "Data/Enums/CombatEnums.h"
#include "Data/Tables/CharacterTableRow.h"
#include "Data/Tables/WeaponTableRow.h"
#include "Data/Tables/MapTableRow.h"
#include "Data/Tables/SpawnTableRow.h"
#include "Data/Tables/KillIconTableRow.h"

// 注意: 本文件不再包含 StaticTable.generated.h
// 原因: UHT 只为含 UCLASS/USTRUCT 的 header 生成 generated.h, 旧版 StaticTable.h 的定义已全部迁移到子表头
// 旧 .cpp 继续 #include "UI/Login/Data/StaticTable.h" 仍可工作, 编译器不需要 generated.h
