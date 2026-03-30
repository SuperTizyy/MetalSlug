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
	TSubclassOf<ACharacter> CharacterBlueprint; 

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
