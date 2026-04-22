#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h" // 【极其关键】：必须包含这个头文件，才能继承 FTableRowBase
#include "GameFramework/Character.h" // 用于识别角色的 3D 蓝图类
// 你的 .generated.h 必须放在最后一行
#include "StaticTable.generated.h"

class ABaseWeapon;

// ==========================================
// 【新增】：定义房间的状态机枚举
// ==========================================
UENUM(BlueprintType)
enum class ERoomState : uint8
{
	WaitingInRoom UMETA(DisplayName = "大厅等待选人状态"),
	BattleInProgress UMETA(DisplayName = "战斗进行状态")
};

// 【规范】：使用强类型枚举定义队伍，比 bool 或 int 更具可读性和扩展性
UENUM(BlueprintType)
enum class ERoomTeam : uint8
{
	None    UMETA(DisplayName = "未分配"),
	Attack  UMETA(DisplayName = "攻方"),
	Defense UMETA(DisplayName = "守方")
};

/** * @enum ERoomMatchMode
 * @brief 房间比赛模式枚举 
 */
UENUM(BlueprintType)
enum class ERoomMatchMode : uint8
{
	None		UMETA(DisplayName = "None"),
	Melee		UMETA(DisplayName = "刀战模式"), // 30分钟
	Zombie		UMETA(DisplayName = "生化模式")  // 10分钟
};

// ==========================================
// 武器类型枚举（用于区分主武器、副武器、近战武器）
// ==========================================
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
USTRUCT(BlueprintType)
struct FCharacterInfo : public FTableRowBase // 【极其关键】：必须继承 FTableRowBase，引擎才会认它是一行表格数据！
{
	GENERATED_BODY()

public:
	// 1. 角色展示名称 (比如 "小男孩"、"机甲战士")
	// 使用 FText 是为了以后做多语言翻译准备
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character Data")
	FText CharacterName;

	// 2. 角色头像图标 (供大厅 UI 显示用)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character Data")
	class UTexture2D* AvatarIcon;

	// 3. 真实的 3D 角色蓝图类 (用于 ServerTravel 传送到战斗地图后，真正生成出来的 3D 小人)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character Data")
	TSoftClassPtr<class ABaseCharacter> CharacterBlueprint;

	// 4. 技能描述
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character Data")
	FString SkillDescription;
	

};

// ==========================================
// 武器信息配置表 (Data-Driven 核心底座)
// ==========================================
USTRUCT(BlueprintType)
struct FWeaponInfo : public FTableRowBase // 同样必须继承 FTableRowBase
{
	GENERATED_BODY()

public:
	// 1. 武器展示名称 
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Data")
	FText WeaponName;

	// 2. 武器图标 (供大厅 UI 背包和切换界面显示用)
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Data")
	class UTexture2D* WeaponIcon;

	// 3. 真实的 3D 武器蓝图类 (用于传送到战斗地图后，真正生成并装备给玩家的武器)
	// 如果你已经写了 ABaseWeapon 基类，可以把 AActor 换成 ABaseWeapon
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Data")
	TSubclassOf<class ABaseWeapon> WeaponBlueprint; 
	
	// 4. 武器类型（主武器、副武器、近战武器）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Data")
	EWeaponType WeaponType;

	//轻击头部伤害
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Data")
	float LightHeadDamage;
	
	//轻击身体伤害
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Data")
	float LightBodyDamage;

	//重击伤害
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Data")
	float BludgeoningDamage;

	// 6. 武器描述
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon Data")
	FString Description;
};


//数据驱动地图表
USTRUCT(BlueprintType)
struct FMapInfoRow : public FTableRowBase
{
	GENERATED_BODY()

	// 1. 真实关卡名 (极其重要：必须和 .umap 文件名一模一样，用于底层 OpenLevel)
	// 比如填 "L_DesertMap"
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Map")
	FName LevelName;

	// 2. 玩家在 UI 上看到的展示名 (支持多语言)
	// 比如填 "黄沙废墟"
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Map")
	FText DisplayName;

	// 3. 地图缩略图 (UI 上展示的漂亮图片)
	// 推荐使用 TSoftObjectPtr (软引用)，防止一开游戏就把所有地图图片全加载进内存爆掉！
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Map")
	TSoftObjectPtr<UTexture2D> MapThumbnail;

	// 4. (可选) 地图描述、最大支持人数等
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Map")
	FText MapDescription;
};

// ==========================================
// ACE 排名类型枚举（决定 HUD 上 ACE 文字颜色）
// ==========================================
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
// 击杀图标信息表 (Data-Driven 核心底座)
// ==========================================
USTRUCT(BlueprintType)
struct FKillIconInfo : public FTableRowBase
{
	GENERATED_BODY()

public:
	// 击杀方式枚举（作为查找键）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kill Icon Data")
	EKillMethod KillMethod;

	// 击杀图标（用于 HUD 击杀信息显示）
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Kill Icon Data")
	class UTexture2D* KillIcon;
};
