// Copyright (c) 2026.
//
// Phase 1 数据驱动层 - AI 运行时配置组件
// 设计:
//   - 挂在 AIController 上 (作为组件)
//   - 由 ABaseAIController 在 Possess 后调用 ApplyConfig 注入数据
//   - BT/BTTask 通过 Owner->GetComponentByClass 读 Config
//   - 难度倍率单独维护, GameMode 可热注入

#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Systems/AI/AIBehaviorTypes.h"
#include "AIRuntimeConfigComponent.generated.h"

class UAIBehaviorConfigSO;

/**
 * UAIRuntimeConfigComponent
 * 单一职责: 把 Config 和 Difficulty 注入到当前 Controller / Pawn 上
 *          并对外提供 BlueprintPure 读取接口
 *
 * 与 ABaseAIController 的关系:
 *   - AController 持有此组件
 *   - AController.Possess(APawn) -> 调用 ApplyConfig
 *   - BT/BTTask 节点 可放心 GetConfig() 读 SO
 */
UCLASS(ClassGroup=(AI), meta=(BlueprintSpawnableComponent))
class METALSLUG01_API UAIRuntimeConfigComponent : public UActorComponent
{
    GENERATED_BODY()

public:
    UAIRuntimeConfigComponent();

    /** 由 ABaseAIController 在 Possess 之后调用 */
    UFUNCTION(BlueprintCallable, Category = "AI|Config")
    void ApplyConfig(UAIBehaviorConfigSO* InConfig);

    /** GameMode 注入难度 - 影响所有 GetScaled* 读取 */
    UFUNCTION(BlueprintCallable, Category = "AI|Config")
    void SetDifficultyTier(EAIDifficultyTier NewTier);

    /** 读取当前生效 Config (供 BT/BTTask 用) */
    UFUNCTION(BlueprintPure, Category = "AI|Config")
    const UAIBehaviorConfigSO* GetConfig() const { return Config; }

    UFUNCTION(BlueprintPure, Category = "AI|Config")
    EAIDifficultyTier GetDifficultyTier() const { return DifficultyTier; }

    /** 难度缩放后的参数 - 简化 BTTask 调用 */
    UFUNCTION(BlueprintPure, Category = "AI|Config")
    FAIPerceptionParams GetScaledPerception() const;

    UFUNCTION(BlueprintPure, Category = "AI|Config")
    FAICombatParams GetScaledCombat() const;

    UFUNCTION(BlueprintPure, Category = "AI|Config")
    FAIMovementParams GetScaledMovement() const;

protected:
    /**
     * 当前生效的配置 SO
     * 持有为强引用 - 因为 ApplyConfig 时已经确保资源已加载
     * 不要在这里用 TSoftObjectPtr - Config 生命周期与 Controller 一致
     */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI|Config")
    TObjectPtr<UAIBehaviorConfigSO> Config;

    /** 难度档位 - 默认 Normal */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI|Config")
    EAIDifficultyTier DifficultyTier = EAIDifficultyTier::Normal;
};
