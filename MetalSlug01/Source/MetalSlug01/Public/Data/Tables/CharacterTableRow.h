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
class UAIBehaviorConfigSO;
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

    /**
     * 【v54.2 大厂架构新增 — 真理源配置】该角色关联的 AI 行为配置 DataAsset
     *
     * 单一真理源链:
     *   - DT_CharacterInfo[CharacterName] → ConfigSoftRef → ConfigSO 资产 (DA_AIBehaviorConfig_*.uasset)
     *   - RoomService::RequestAddAI 反查时 LoadSynchronous 拿到 ConfigSO 实例
     *   - 写入 FAISpawnRequest.Config 透传给 SpawnAIInternal
     *   - 所有 AI 路径 (关卡预放 / 大厅入队 / 复活) 走同一个 Config 真理源
     *
     * 必配 (零兜底):
     *   - RoomService 反查 ConfigSoftRef 为空 → Log Error + 拒绝入队 (强制修复 DT)
     *   - 否则 SpawnInvincibilitySeconds 等"所有 AI 共用"字段没有真理源
     */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character Data",
        meta = (DisplayName = "ConfigSoftRef (该角色关联的 AI 行为配置 — 必须配)"))
    TSoftObjectPtr<UAIBehaviorConfigSO> ConfigSoftRef;

    /** 技能描述 */
    UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Character Data")
    FString SkillDescription;
};
