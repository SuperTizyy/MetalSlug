// ==========================================
// 武器信息表行 + 挂载配置表行
// 关联: DT_WeaponList / DT_WeaponAttachment
// 替代: 原 StaticTable.h 中的 FWeaponInfo / FWeaponAttachmentConfig 段
// ==========================================
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "../Enums/CombatEnums.h" // 依赖 EWeaponMeshType
#include "WeaponTableRow.generated.h"

class ABaseWeapon;
class ABaseCharacter;
class UTexture2D;
class UAnimMontage;

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

	/** 武器模型类型 (近战/主武器/副武器) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Data",
		meta = (DisplayName = "武器类型 (决定战斗策略)"))
	EWeaponMeshType MeshType = EWeaponMeshType::None;

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

	// ==========================================
	// v60 — 枪械射击配置 (弹药/射速/换弹/开火模式/蒙太奇)
	// 真理源: 单一 DT 行, 策划改一次全武器生效
	// ==========================================

	/**
	 * 弹匣容量 (一发弹药消耗 = 一次 LineTrace)
	 * 真理源: 仅 DT, 不在 BaseWeapon 字段复制
	 * 默认 30 (M16/AK 常见值)
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Data|Fire",
		meta = (ClampMin = "1", UIMin = "1", DisplayName = "弹匣容量"))
	int32 MagazineSize = 30;

	/**
	 * 备用弹匣总弹药 (换弹时从这里取, 不会自动补充)
	 * 默认 120 (4 个弹匣)
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Data|Fire",
		meta = (ClampMin = "0", UIMin = "0", DisplayName = "备用弹药"))
	int32 ReserveAmmo = 120;

	/**
	 * 射速 (每分钟发射数 — Rounds Per Minute)
	 * 计算公式: TimeBetweenShotsSeconds = 60.0f / FireRateRPM
	 * 默认 600 RPM (10 发/秒 — M16/AK 常见值)
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Data|Fire",
		meta = (ClampMin = "1.0", UIMin = "1.0", DisplayName = "射速 (RPM)"))
	float FireRateRPM = 600.0f;

	/**
	 * 换弹时间 (秒)
	 * 启动后锁定 ReloadTimeSeconds 秒, 期间禁止开火
	 * 默认 2.0 秒
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Data|Fire",
		meta = (ClampMin = "0.1", UIMin = "0.1", DisplayName = "换弹时间 (秒)"))
	float ReloadTimeSeconds = 2.0f;

	/**
	 * 【v60.14 新增】武器射程 (cm)
	 *
	 * 含义: 子弹从相机射出后，能命中的最大距离
	 * 用途:
	 *   - 枪械 (MeshType=Primary/Secondary): RangedLineStrategy 用此值作为射线终点
	 *   - AI: BTDecorator_InAttackRange 用 ConfigSO.AttackRange（不是这个值）
	 *
	 * 设计原则 (大厂标准):
	 *   - 这个值是"射击有效距离"，不是"AI 攻击触发距离"
	 *   - AI 走到 D ≤ AR 后停止，AR 由 AIBehaviorConfigSO 配置
	 *   - 武器射程 > AI 攻击距离时，玩家需要更精确地瞄准才能命中 AI
	 *
	 * 默认 1500cm (15米 — 手枪常见有效射程)
	 * 建议配置:
	 *   - 手枪: 800~1500cm
	 *   - 步枪: 2500~5000cm
	 *   - 狙击: 10000cm+
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Data|Fire",
		meta = (ClampMin = "100.0", UIMin = "100.0", DisplayName = "武器射程 (cm)"))
	float AttackRange = 1500.0f;

	/**
	 * 【v60.16 新增】枪口偏移 (cm)
	 *
	 * 含义: 枪口相对于角色中心的偏移距离
	 * 计算公式:
	 *   射线起点 = 相机位置 + 相机方向 × (TAL - MuzzleOffset)
	 *   (TAL = TargetArmLength，枪口到相机距离 = TAL - MuzzleOffset)
	 *
	 * 用途:
	 *   - 枪械 (MeshType=Primary/Secondary): RangedLineStrategy 用此值计算射线起点
	 *   - 确保射线从枪口位置射出，而不是从相机位置
	 *
	 * 设计原则:
	 *   - 不同武器枪口到肩膀的距离不同（手枪 vs 步枪 vs 狙击）
	 *   - 策划可按武器类型配置
	 *
	 * 默认 50cm (手枪常见值)
	 * 建议配置:
	 *   - 手枪: 40~60cm
	 *   - 步枪: 60~80cm
	 *   - 狙击: 80~120cm
	 *
	 * 注意: MuzzleOffset 应该小于角色的 TargetArmLength，否则枪口会跑到相机后面
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Data|Fire",
		meta = (ClampMin = "0.0", UIMin = "0.0", DisplayName = "枪口偏移 (cm)"))
	float MuzzleOffset = 50.0f;

	/**
	 * 腰射 (Hip) 状态下播放的开火蒙太奇
	 * 留空 → 不播放蒙太奇 (只走射线)
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Data|Animation")
	TObjectPtr<UAnimMontage> FireMontageHip;

	/**
	 * 腰射状态下的换弹蒙太奇
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Data|Animation")
	TObjectPtr<UAnimMontage> ReloadMontageHip;

	/**
	 * 是否能在换弹时被其他输入打断 (false = 强制等 ReloadTimeSeconds)
	 * 大厂默认: false (换弹不允许被打断, 避免动画撕裂)
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Data|Fire",
		meta = (DisplayName = "换弹可被打断"))
	bool bReloadCanBeInterrupted = false;
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
