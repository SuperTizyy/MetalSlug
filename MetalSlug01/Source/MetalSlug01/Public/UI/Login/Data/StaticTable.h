// 版权声明：在项目设置的描述页面填写您的版权信息。

#pragma once

// ==========================================
// 头文件包含说明
// ==========================================
#include "CoreMinimal.h"
#include "Engine/DataTable.h" // 【极其关键】: 必须包含这个头文件，才能继承 FTableRowBase
#include "GameFramework/Character.h" // 用于识别角色的 3D 蓝图类
// 你的 .generated.h 必须放在最后一行
#include "StaticTable.generated.h"

class ABaseWeapon;


// ==========================================
// 【新增】: 定义房间的状态机枚举
// ==========================================

/**
 * @enum ERoomState
 * @brief 房间状态机
 * - WaitingInRoom: 大厅等待选人状态
 * - BattleInProgress: 战斗进行状态
 */
UENUM(BlueprintType)
enum class ERoomState : uint8
{
	WaitingInRoom UMETA(DisplayName = "大厅等待选人状态"),
	BattleInProgress UMETA(DisplayName = "战斗进行状态")
};


/**
 * @enum ERoomTeam
 * @brief 队伍类型
 * - None: 未分配
 * - Attack: 攻方
 * - Defense: 守方
 * 规范: 使用强类型枚举定义队伍，比 bool 或 int 更具可读性和扩展性
 */
UENUM(BlueprintType)
enum class ERoomTeam : uint8
{
	None    UMETA(DisplayName = "未分配"),
	Attack  UMETA(DisplayName = "攻方"),
	Defense UMETA(DisplayName = "守方")
};


/**
 * @enum ERoomMatchMode
 * @brief 房间比赛模式
 * - None: 无模式
 * - Melee: 刀战模式（30 分钟）
 * - Zombie: 生化模式（10 分钟）
 */
UENUM(BlueprintType)
enum class ERoomMatchMode : uint8
{
	None		UMETA(DisplayName = "None"),
	Melee		UMETA(DisplayName = "刀战模式"), // 30 分钟
	Zombie		UMETA(DisplayName = "生化模式")  // 10 分钟
};


// ==========================================
// 武器类型枚举（用于区分主武器、副武器、近战武器）
// ==========================================

/**
 * @enum EWeaponType
 * @brief 武器类型
 * - None: 无
 * - Primary: 主武器
 * - Secondary: 副武器
 * - Melee: 近战武器
 */
UENUM(BlueprintType)
enum class EWeaponType : uint8
{
	None        UMETA(DisplayName = "无"),
	Primary     UMETA(DisplayName = "主武器"),
	Secondary   UMETA(DisplayName = "副武器"),
	Melee       UMETA(DisplayName = "近战武器")
};


// ==========================================
// 角色信息配置表 (Data-Driven 核心底座)
// ==========================================

/**
 * @struct FCharacterInfo
 * @brief 角色信息数据表行
 * 用途: 关联 DT_CharacterList，让 UI 选人/3D 角色生成数据驱动
 */
USTRUCT(BlueprintType)
struct FCharacterInfo : public FTableRowBase // 【极其关键】: 必须继承 FTableRowBase，引擎才会认它是一行表格数据
{
	GENERATED_BODY()

public:
	/**
	 * 角色展示名称（比如"小男孩"、"机甲战士"）
	 * 使用 FText 是为了以后做多语言翻译准备
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character Data")
	FText CharacterName;

	/**
	 * 角色头像图标（供大厅 UI 显示用）
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character Data")
	class UTexture2D* AvatarIcon;

	/**
	 * 真实的 3D 角色蓝图类
	 * 用途: 用于 ServerTravel 传送到战斗地图后，真正生成出来的 3D 小人
	 * 使用 TSoftClassPtr 软引用: 防止一开游戏就把所有角色蓝图全加载进内存爆掉
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character Data")
	TSoftClassPtr<class ABaseCharacter> CharacterBlueprint;

	/**
	 * 技能描述
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character Data")
	FString SkillDescription;

};


// ==========================================
// 武器信息配置表 (Data-Driven 核心底座)
// ==========================================

/**
 * @struct FWeaponInfo
 * @brief 武器信息数据表行
 * 用途: 关联 DT_WeaponList，让 UI 选武器/3D 武器生成数据驱动
 */
USTRUCT(BlueprintType)
struct FWeaponInfo : public FTableRowBase // 同样必须继承 FTableRowBase
{
	GENERATED_BODY()

public:
	/**
	 * 武器展示名称
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Data")
	FText WeaponName;

	/**
	 * 武器图标（供大厅 UI 背包和切换界面显示用）
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Data")
	class UTexture2D* WeaponIcon;

	/**
	 * 真实的 3D 武器蓝图类
	 * 用途: 用于传送到战斗地图后，真正生成并装备给玩家的武器
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Data")
	TSubclassOf<class ABaseWeapon> WeaponBlueprint;

	/**
	 * 武器类型（主武器、副武器、近战武器）
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Data")
	EWeaponType WeaponType;

	/**
	 * 轻击头部伤害
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Data")
	float LightHeadDamage;

	/**
	 * 轻击身体伤害
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Data")
	float LightBodyDamage;

	/**
	 * 重击伤害
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Data")
	float BludgeoningDamage;

	/**
	 * 武器描述
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Data")
	FString Description;
};


// ==========================================
// 数据驱动地图表
// ==========================================

/**
 * @struct FMapInfoRow
 * @brief 地图信息数据表行
 * 用途: 关联 DT_MapInfo，让创房面板的地图下拉框数据驱动
 */
USTRUCT(BlueprintType)
struct FMapInfoRow : public FTableRowBase
{
	GENERATED_BODY()

	/**
	 * 真实关卡名
	 * 极其重要: 必须和 .umap 文件名一模一样，用于底层 OpenLevel
	 * 比如填 "L_DesertMap"
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Map")
	FName LevelName;

	/**
	 * 玩家在 UI 上看到的展示名（支持多语言）
	 * 比如填 "黄沙废墟"
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Map")
	FText DisplayName;

	/**
	 * 地图缩略图（UI 上展示的漂亮图片）
	 * 推荐使用 TSoftObjectPtr（软引用），防止一开游戏就把所有地图图片全加载进内存爆掉
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Map")
	TSoftObjectPtr<UTexture2D> MapThumbnail;

	/**
	 * （可选）地图描述、最大支持人数等
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Map")
	FText MapDescription;
};


// ==========================================
// ACE 排名类型枚举（决定 HUD 上 ACE 文字颜色）
// ==========================================

/**
 * @enum EACERankType
 * @brief ACE 排名类型
 * - None: 无 ACE（不显示金色或白色）
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


// ==========================================
// 击杀方式枚举（用于击杀信息显示）
// ==========================================

/**
 * @enum EKillMethod
 * @brief 击杀方式
 * - PrimaryHeadshot: 主武器爆头
 * - PrimaryWeapon: 主武器
 * - SecondaryHeadshot: 副武器爆头
 * - SecondaryWeapon: 副武器
 * - MeleeHeadshot: 近战武器爆头
 * - MeleeWeapon: 近战武器
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


// ==========================================
// 武器挂载配置表 (Data-Driven 核心底座)
// ==========================================

/**
 * @struct FWeaponAttachmentConfig
 * @brief 武器挂载配置数据表行
 * 用途: 关联 DT_WeaponAttachment，让 BaseCharacter 自动根据角色+武器挑选挂载点/偏移
 */
USTRUCT(BlueprintType)
struct FWeaponAttachmentConfig : public FTableRowBase
{
	GENERATED_BODY()

public:
	/**
	 * 唯一标识符（用于查找，例如 "Warrior_Knife01"）
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attachment Config")
	FString ConfigID;

	/**
	 * 目标角色蓝图（可选。如果为空，则表示该配置适用于所有角色）
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attachment Config")
	TSoftClassPtr<class ABaseCharacter> TargetCharacter;

	/**
	 * 目标武器蓝图（可直接选择具体武器蓝图）
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attachment Config")
	TSubclassOf<class ABaseWeapon> WeaponBlueprint;

	/**
	 * 挂载到角色的哪个目标插槽上
	 * 例如 "WeaponSocket_R", "WeaponSocket_L", "Back_Socket"
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attachment Config")
	FName SocketName;

	/**
	 * 相对于插槽的相对位置偏移（本地空间）
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attachment Config", meta = (MakeDefaultValue = "0, 0, 0"))
	FVector RelativeLocation = FVector::ZeroVector;

	/**
	 * 相对于插槽的相对旋转偏移（欧拉角，本地空间）
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attachment Config", meta = (MakeDefaultValue = "0, 0, 0"))
	FRotator RelativeRotation = FRotator::ZeroRotator;

	/**
	 * 相对于插槽的相对缩放偏移
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Attachment Config", meta = (MakeDefaultValue = "1, 1, 1"))
	FVector RelativeScale = FVector(1.0f);
};


// ==========================================
// 击杀图标信息表 (Data-Driven 核心底座)
// ==========================================

/**
 * @struct FKillIconInfo
 * @brief 击杀图标信息数据表行
 * 用途: 关联 DT_KillIconInfo，让 HUD 根据击杀方式显示对应图标
 */
USTRUCT(BlueprintType)
struct FKillIconInfo : public FTableRowBase
{
	GENERATED_BODY()

public:
	/**
	 * 击杀方式枚举（作为查找键）
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kill Icon Data")
	EKillMethod KillMethod;

	/**
	 * 击杀图标（用于 HUD 击杀信息显示）
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kill Icon Data")
	class UTexture2D* KillIcon;
};


// ==========================================
// 击杀连杀类型枚举（用于连杀图标显示）
// ==========================================

/**
 * @enum EKillStreakType
 * @brief 连杀类型
 * - None: 无
 * - Headshot: 爆头
 * - OneKill ~ FiveKills: 一杀到五杀
 */
UENUM(BlueprintType)
enum class EKillStreakType : uint8
{
	None        UMETA(DisplayName = "无"),
	Headshot    UMETA(DisplayName = "爆头"),
	OneKill     UMETA(DisplayName = "一杀"),
	TwoKills    UMETA(DisplayName = "二杀"),
	ThreeKills  UMETA(DisplayName = "三杀"),
	FourKills   UMETA(DisplayName = "四杀"),
	FiveKills   UMETA(DisplayName = "五杀")
};


// ==========================================
// 击杀连杀图标信息表 (Data-Driven 核心底座)
// ==========================================

/**
 * @struct FKillStreakIconInfo
 * @brief 连杀图标信息数据表行
 * 用途: 关联 DT_KillStreakIconInfo，让 HUD 根据连杀数显示对应图标
 */
USTRUCT(BlueprintType)
struct FKillStreakIconInfo : public FTableRowBase
{
	GENERATED_BODY()

public:
	/**
	 * 连杀类型枚举（作为查找键）
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kill Streak Icon Data")
	EKillStreakType StreakType;

	/**
	 * 连杀图标（用于 HUD 连杀图标显示）
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kill Streak Icon Data")
	class UTexture2D* StreakIcon;
};
