// ==================================
// 武器切换音效配置 DataAsset 实现文件
// ==================================
// 文件功能总览:
//   - 实现 UWeaponSoundMapAsset::FindSoundForSlot: 按 EWeaponSlotType
//     返回对应槽位的音效条目(Primary / Secondary / Melee)
//   - EWeaponSlotType::None 视为非法输入,Log Error + return nullptr
//
// 大厂原则:
//   - 零兜底: None 槽位不允许静默 fallback 到默认音效
//   - 数据驱动: 真实 Sound 资产由策划在 DA_WeaponSoundMap.uasset 配置
// ==================================
#include "Data/Config/WeaponSoundMapAsset.h"
#include "Sound/SoundBase.h"

/**
 * @brief 按槽位类型查询音效条目
 * @param Slot 槽位类型(Primary / Secondary / Melee)
 * @return 该槽位的音效条目指针(永不为 nullptr,但内部 Sound 字段可能为 nullptr)
 *         None 输入返回 nullptr + Log Error
 * @note 大厂原则: 不接受 None,调用方必须传入合法槽位
 */
const FWeaponSlotSoundEntry* UWeaponSoundMapAsset::FindSoundForSlot(EWeaponSlotType Slot) const
{
	// 按槽位类型分支返回对应的音效条目指针
	switch (Slot)
	{
	case EWeaponSlotType::Primary:
		// 主武器槽位 — 默认填 Weapon_Hand_Move_02
		return &PrimarySlot;

	case EWeaponSlotType::Secondary:
		// 副武器槽位 — 允许空(策划可不配)
		return &SecondarySlot;

	case EWeaponSlotType::Melee:
		// 近战武器槽位 — 默认填 Sword_Attack_Short_1
		return &MeleeSlot;

	case EWeaponSlotType::None:
	default:
		// 大厂原则 - 零兜底: 不接受 None, 让调用方 Log Error 强制修复
		UE_LOG(LogTemp, Error,
			TEXT("[UWeaponSoundMapAsset] FindSoundForSlot: Slot=None — 拒绝查询. ")
			TEXT("【v76 大厂原则】调用方应传入合法槽位 (Primary / Secondary / Melee)."));
		return nullptr;
	}
}
