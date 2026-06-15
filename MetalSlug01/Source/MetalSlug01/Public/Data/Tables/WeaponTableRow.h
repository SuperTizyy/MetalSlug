// ==========================================
// 武器信息表行 + 挂载配置表行
// 关联: DT_WeaponList / DT_WeaponAttachment
// 替代: 原 StaticTable.h 中的 FWeaponInfo / FWeaponAttachmentConfig 段
// ==========================================
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "../Enums/CombatEnums.h" // 依赖 EWeaponType
#include "WeaponTableRow.generated.h"

class ABaseWeapon;
class ABaseCharacter;
class UTexture2D;

/**
 * @struct FWeaponInfo
 * @brief 武器信息数据表行
 * 用途: 关联 DT_WeaponList, 让 UI 选武器/3D 武器生成数据驱动
 */
USTRUCT(BlueprintType)
struct FWeaponInfo : public FTableRowBase
{
	GENERATED_BODY()

public:
	/** 武器展示名称 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Data")
	FText WeaponName;

	/** 武器图标 (供大厅 UI 背包和切换界面显示) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Data")
	TObjectPtr<UTexture2D> WeaponIcon;

	/** 3D 武器蓝图类 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Data")
	TSoftClassPtr<ABaseWeapon> WeaponBlueprint;

	/** 武器类型 (主武器/副武器/近战) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Data")
	EWeaponType WeaponType;

	/** 轻击头部伤害 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Data")
	float LightHeadDamage;

	/** 轻击身体伤害 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Data")
	float LightBodyDamage;

	/** 重击伤害 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Data")
	float BludgeoningDamage;

	/** 武器描述 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Data")
	FString Description;
};

/**
 * @struct FWeaponAttachmentConfig
 * @brief 武器挂载配置表行
 * 用途: 关联 DT_WeaponAttachment, 让 BaseCharacter 根据角色+武器挑选挂载点
 */
USTRUCT(BlueprintType)
struct FWeaponAttachmentConfig : public FTableRowBase
{
	GENERATED_BODY()

public:
	/** 唯一标识符 (例如 "Warrior_Knife01") */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attachment Config")
	FString ConfigID;

	/** 目标角色蓝图 (可选, 空=所有角色) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attachment Config")
	TSoftClassPtr<ABaseCharacter> TargetCharacter;

	/** 目标武器蓝图 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attachment Config")
	TSoftClassPtr<ABaseWeapon> WeaponBlueprint;

	/** 挂载插槽名 (例如 "WeaponSocket_R") */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attachment Config")
	FName SocketName;

	/** 相对位置偏移 (本地空间) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attachment Config")
	FVector RelativeLocation = FVector::ZeroVector;

	/** 相对旋转偏移 (欧拉角) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attachment Config")
	FRotator RelativeRotation = FRotator::ZeroRotator;

	/** 相对缩放 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attachment Config")
	FVector RelativeScale = FVector(1.0f);
};
