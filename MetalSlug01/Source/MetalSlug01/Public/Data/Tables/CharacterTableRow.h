// ==========================================
// 角色信息表行
// 关联: DT_CharacterList
// 替代: 原 StaticTable.h 中的 FCharacterInfo 段
// ==========================================
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "CharacterTableRow.generated.h"

class ABaseCharacter;
class UTexture2D;

/**
 * @struct FCharacterInfo
 * @brief 角色信息数据表行
 * 用途: 关联 DT_CharacterList，让 UI 选人/3D 角色生成数据驱动
 */
USTRUCT(BlueprintType)
struct FCharacterInfo : public FTableRowBase
{
	GENERATED_BODY()

public:
	/** 角色展示名称 (FText 便于多语言) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character Data")
	FText CharacterName;

	/** 角色头像图标 (供大厅 UI 显示) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character Data")
	TObjectPtr<UTexture2D> AvatarIcon;

	/** 真实 3D 角色蓝图类 (TSoftClassPtr 避免一次性加载所有角色) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character Data")
	TSoftClassPtr<ABaseCharacter> CharacterBlueprint;

	/** 技能描述 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character Data")
	FString SkillDescription;
};
