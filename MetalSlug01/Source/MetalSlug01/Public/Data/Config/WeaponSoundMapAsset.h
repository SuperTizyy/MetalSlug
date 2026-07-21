// ==================================
// 武器切换音效配置 DataAsset
// 关联: DA_WeaponSoundMap (PrimaryAsset)
// 用途: 按 EWeaponSlotType 映射 USoundBase (策划不碰代码)
// 大厂原则:
//   1. 单一真理源: GM->WeaponSoundMapAsset → DA_WeaponSoundMap → 按 EWeaponSlotType 查 Sound
//   2. 数据驱动: 策划在编辑器配置, 不用改代码
//   3. 零兜底: 未配 Slot → Log Error + return, 触发调用方报错
// ==================================
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "Weapons/WeaponSlotType.h"
#include "WeaponSoundMapAsset.generated.h"

class USoundBase;

/**
 * 单个槽位的音效条目
 * 大厂原则: 每个槽位 = {Sound, VolumeMultiplier}
 *   - Sound: 实际 Sound Cue / Wave / MetaSound (USoundBase 通用基类)
 *   - VolumeMultiplier: 缩放, 默认 1.0 (策划可调)
 */
USTRUCT(BlueprintType)
struct FWeaponSlotSoundEntry
{
	GENERATED_BODY()

	/**
	 * 切换到该槽位时播放的音效
	 * 真理源: USoundBase (兼容 SoundCue / SoundWave / MetaSoundSource)
	 * 大厂原则: 必填 (未配 → Log Error, 调用方拒绝播放)
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sound")
	TObjectPtr<USoundBase> Sound = nullptr;

	/**
	 * 音量缩放 (默认 1.0)
	 * 大厂原则: 0~2 区间, 用 CVar 全局音量 + 这里倍数
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sound", meta = (ClampMin = "0.0", UIMin = "0.0"))
	float VolumeMultiplier = 1.0f;

	/**
	 * 音高缩放 (默认 1.0)
	 * 大厂原则: 0.5~2 区间
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Sound", meta = (ClampMin = "0.5", ClampMax = "2.0"))
	float PitchMultiplier = 1.0f;
};

/**
 * @class UWeaponSoundMapAsset
 * @brief 武器切换音效配置资产
 *
 * 业务用例:
 *   玩家按 1/2/3 键切换武器 → Server_SwitchToWeaponSlot 写 CurrentWeaponSlot
 *   → 读 GM->WeaponSoundMapAsset → 按 EWeaponSlotType 查 FWeaponSlotSoundEntry
 *   → PlaySoundAtLocation 或 Multicast_PlayEquipSound 推给所有客户端
 *
 * 大厂原则 - 单一真理源:
 *   - GameMode 持有此 Asset 字段 (集中配置, 与 PlayerConfigAsset / WeaponDataTable 等同位置)
 *   - 运行时通过 UWeaponAttachmentComponent::GetWeaponSoundForSlot(EWeaponSlotType) 查询
 *   - 不在 WeaponAttachmentComponent / BaseCharacter 上重复配置
 *
 * 大厂原则 - 零兜底:
 *   - Asset 未配置 → Log Error 拒绝查询 (调用方收到 nullptr, 拒绝播放)
 *   - 某个 Slot 字段未配 → Log Error + return nullptr (拒绝"静默用默认音效")
 */
UCLASS(BlueprintType)
class METALSLUG01_API UWeaponSoundMapAsset : public UDataAsset
{
	GENERATED_BODY()

public:
	// ============================================================
	// 按 EWeaponSlotType 配置切换音效 (3 个槽位)
	// ============================================================

	/** 主武器 (EWeaponSlotType::Primary) — 默认填 Weapon_Hand_Move_02 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Slots")
	FWeaponSlotSoundEntry PrimarySlot;

	/** 副武器 (EWeaponSlotType::Secondary) — 允许空 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Slots")
	FWeaponSlotSoundEntry SecondarySlot;

	/** 近战武器 (EWeaponSlotType::Melee) — 默认填 Sword_Attack_Short_1 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Slots")
	FWeaponSlotSoundEntry MeleeSlot;

	/**
	 * 查询某个槽位的音效条目
	 * 大厂原则 - 零兜底: Asset 未配 / Slot 未配 → Log Error + return nullptr
	 *
	 * @param Slot 槽位类型
	 * @return 该槽位的音效条目 (Sound 可为 null 但 entry 永远非空, 字段已 default)
	 */
	const FWeaponSlotSoundEntry* FindSoundForSlot(EWeaponSlotType Slot) const;
};
