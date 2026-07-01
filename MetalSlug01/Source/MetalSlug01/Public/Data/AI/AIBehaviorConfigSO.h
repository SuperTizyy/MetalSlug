// Copyright (c) 2026.
//
// Phase 1 数据驱动层 - AI 行为参数配置（替换所有硬编码数值）
// 设计:
//   - 单一 DataAsset = 一类 AI 的"参数集"
//   - 字段按"功能组"分包(Perception/Combat/Movement/Debug)
//   - 全部用 BlueprintReadOnly - BT/BTTask 只能读不能改

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "GameplayTagContainer.h"
#include "Systems/AI/AIBehaviorTypes.h"
#include "AIBehaviorConfigSO.generated.h"

class UBehaviorTree;
class UAISenseConfig_Sight;
class UAISenseConfig_Hearing;
class UEnvQuery;

/**
 * UAIBehaviorConfigSO
 * 一类 AI 的"全部运行参数"集中入口
 *
 * 用途:
 *   - ABaseAIController 通过 UAIRuntimeConfigComponent 持有
 *   - BT/BTTask 通过 (Owner->GetController<ABaseAIController>()->GetConfig()) 读
 *
 * 索引:
 *   - 由 UAIProfileAsset 软引用 (按需加载)
 */
UCLASS(BlueprintType)
class METALSLUG01_API UAIBehaviorConfigSO : public UDataAsset
{
    GENERATED_BODY()

public:
    UAIBehaviorConfigSO();

    /** 行为树软引用 - 用软引用避免常驻内存 */
    UPROPERTY(EditDefaultsOnly, Category = "Behavior",
        meta = (AllowedClasses = "/Script/AIModule.BehaviorTree"))
    TSoftObjectPtr<UBehaviorTree> BehaviorTree;

    /** 感知参数 - 替换 MeleeAIController.cpp 第 43-48 行 */
    UPROPERTY(EditDefaultsOnly, Category = "Perception")
    FAIPerceptionParams Perception;

    /** 战斗参数 - 替换 BaseAIController.cpp 第 93 行 250.0f */
    UPROPERTY(EditDefaultsOnly, Category = "Combat")
    FAICombatParams Combat;

    /** 移动参数 */
    UPROPERTY(EditDefaultsOnly, Category = "Movement")
    FAIMovementParams Movement;

    /** 调试参数 */
    UPROPERTY(EditDefaultsOnly, Category = "Debug")
    FAIDebugParams Debug;

    /** 选目标 EQS - 留接口给 Phase 3 */
    UPROPERTY(EditDefaultsOnly, Category = "EQS",
        meta = (AllowedClasses = "/Script/AIModule.EnvQuery"))
    TSoftObjectPtr<UEnvQuery> FindTargetEQ;

    /** 寻找掩体 EQS */
    UPROPERTY(EditDefaultsOnly, Category = "EQS",
        meta = (AllowedClasses = "/Script/AIModule.EnvQuery"))
    TSoftObjectPtr<UEnvQuery> FindCoverEQ;

    /** 获取难度档位对应的数值缩放 */
    UFUNCTION(BlueprintPure, Category = "AI|Config")
    float GetDifficultyScale(EAIDifficultyTier Tier) const;

    /**
     * 难度缩放后的感知参数 (副本)
     * 设计: 每个 BTTask 拿到的是 computed value, 不再自己计算
     */
    UFUNCTION(BlueprintPure, Category = "AI|Config")
    FAIPerceptionParams GetScaledPerception(EAIDifficultyTier Tier) const;

    UFUNCTION(BlueprintPure, Category = "AI|Config")
    FAICombatParams GetScaledCombat(EAIDifficultyTier Tier) const;

    UFUNCTION(BlueprintPure, Category = "AI|Config")
    FAIMovementParams GetScaledMovement(EAIDifficultyTier Tier) const;
};
