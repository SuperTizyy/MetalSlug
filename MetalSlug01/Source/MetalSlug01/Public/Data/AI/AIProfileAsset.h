// Copyright (c) 2026.
//
// Phase 1 数据驱动层 - AI 身份标识
// 设计: PrimaryDataAsset, 可接入 Asset Manager 做按需加载
//       通过 FGameplayTag ProfileTag 做存档/成就/掉落反查

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "Systems/AI/AIBehaviorTypes.h"  // 【Phase 2】注入 EAIRole / FAIHuntPolicy
#include "AIProfileAsset.generated.h"

class UAIBehaviorConfigSO;

/**
 * Profile 异步加载完成回调签名
 * 设计: 不依赖 StreamableManager 头文件, 让本头能被更广范围 include
 */
DECLARE_DELEGATE(FOnAIBehaviorConfigLoaded);

/**
 * UAIProfileAsset - AI 的"身份证 + 名片"
 *
 * 作用:
 *   - 给每个 AI 一个全局唯一标识 (ProfileTag)
 *   - 提供"按需加载"的 Config 软引用
 *   - 让策划能在编辑器点选"我想要的 AI 类型"
 *
 * 注意:
 *   - 此处不持有 BehaviorTree / SightConfig 硬引用
 *   - 仅持有一个指向 UAIBehaviorConfigSO 的软引用 (内存友好)
 */
UCLASS(BlueprintType)
class METALSLUG01_API UAIProfileAsset : public UPrimaryDataAsset
{
    GENERATED_BODY()

public:
    /** 业务唯一标识 - 用于存档反查、AI 生成表查询 */
    UPROPERTY(EditDefaultsOnly, Category = "Identity",
        meta = (Categories = "AI.Profile"))
    FGameplayTag ProfileTag;

    /** 显示名 (策划阅读用) */
    UPROPERTY(EditDefaultsOnly, Category = "Identity")
    FText DisplayName;

    /** AI 等级 (影响经验/掉落, 与难度无关) */
    UPROPERTY(EditDefaultsOnly, Category = "Identity",
        meta = (ClampMin = "1", ClampMax = "99"))
    int32 Level = 1;

    /** 大阵营 (用于 IGenericTeamAgentInterface) - 留空表示"中立" */
    UPROPERTY(EditDefaultsOnly, Category = "Identity",
        meta = (Categories = "Faction"))
    FGameplayTag FactionTag;

    /**
     * 【Phase 2 模式化】AI 角色身份
     * 设计: 区分刀战里的"普通肉搏"和生化里的"母体/僵尸/人类AI"
     *       GameMode 的选目标策略/AI Controller 类都按这个决定
     *       不影响 Combat/Movement 等数值 (那些仍在 BehaviorConfigSO 里)
     */
    UPROPERTY(EditDefaultsOnly, Category = "Identity")
    EAIRole AIRole = EAIRole::Soldier;

    /**
     * 【Phase 2 模式化】目标选择策略
     * 替代 RoomGameMode::RequestTargetForAI 巨型 if/else
     * 留空 = 默认 NearestDistance (向后兼容)
     */
    UPROPERTY(EditDefaultsOnly, Category = "Identity")
    FAIHuntPolicy HuntPolicy;

    /**
     * 关联行为配置 - 软引用按需加载
     */
    UPROPERTY(EditDefaultsOnly, Category = "Config",
        meta = (AllowedClasses = "/Script/MetalSlug01.AIBehaviorConfigSO"))
    TSoftObjectPtr<UAIBehaviorConfigSO> BehaviorConfig;

    /**
     * 【Phase 2】AI Controller 子类
     * 替代 RoomGameMode 里写死的 AMeleeAIController::StaticClass()
     * 缺省时 = UAIProfileAsset::DefaultControllerClass (Soldier 通用)
     * 生化时 = AZombieAIController::StaticClass()
     *
     * 注意: 在 Runtime 层允许留空 (走兜底), 但 Editor 默认留空渲染为 Default.
     */
    UPROPERTY(EditDefaultsOnly, Category = "Config",
        meta = (AllowAbstract = "false"))
    TSubclassOf<class AAIController> ControllerClass;

    /** 同步加载 Config (用于 PIE 测试 / 单机关卡) */
    UFUNCTION(BlueprintCallable, Category = "AI|Profile")
    UAIBehaviorConfigSO* LoadBehaviorConfigSync();

    /**
     * 异步加载 Config
     * @param OnLoaded 加载完成后回调 (无参数)
     * 设计: 使用 DECLARE_DELEGATE 而非 FStreamableDelegate, 避免 Public 头传染 StreamableManager.h
     */
    void LoadBehaviorConfigAsync(FOnAIBehaviorConfigLoaded OnLoaded);

    /** Asset Manager 注入 - 让 BA 知道本资源属于哪个 Pool */
    virtual FPrimaryAssetId GetPrimaryAssetId() const override;
};
