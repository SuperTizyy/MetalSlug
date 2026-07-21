// ==================================
// 武器切换音效配置 DataAsset 实现
// ==================================
#include "Data/Config/WeaponSoundMapAsset.h"
#include "Sound/SoundBase.h"

const FWeaponSlotSoundEntry* UWeaponSoundMapAsset::FindSoundForSlot(EWeaponSlotType Slot) const
{
	switch (Slot)
	{
	case EWeaponSlotType::Primary:
		return &PrimarySlot;

	case EWeaponSlotType::Secondary:
		return &SecondarySlot;

	case EWeaponSlotType::Melee:
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
