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
 * @file AIRuntimeConfigComponent.h
 * @brief AI 运行时配置组件 — 把 DataAsset Config 和 Difficulty Tier 注入到当前 Controller
 *
 * 大厂架构定位:
 *   - 单一职责: 数据注入 + BlueprintPure 读取, 不参与 AI 决策
 *   - 真理源: Controller.Possess(APawn) → ApplyConfig(Config) 写入 Config 字段
 *   - 调用方: BT / BTTask / 任何需要读 AI 参数的代码, 通过 GetComponentByClass 获取
 *   - 与 ConfigSO 的关系: ConfigSO 是数据资产 (编辑器配置), Component 是运行时入口
 *
 * 设计动机 (数据驱动层):
 *   - 分离"配置"和"逻辑": 策划在 ConfigSO 里调数值, 不需要改 C++
 *   - 难度倍率: GameMode 可在运行时切换 DifficultyTier, 影响所有 GetScaled* 接口
 *   - 不需要 Tick: Config 是被动读取, GetScaled* 是 BlueprintPure, 调用时计算
 *
 * 零兜底:
 *   - Config 为空 → GetScaled* 返回默认结构体 (零值), 调用方自行检查
 *   - ApplyConfig(null) → Config = nullptr, 这是合法的"清空配置"操作
 */

/**
 * UAIRuntimeConfigComponent
 * 单一职责: 把 Config 和 Difficulty 注入到当前 Controller / Pawn 上
 *          并对外提供 BlueprintPure 读取接口
 *
 * 与 ABaseAIController 的关系:
 *   - AController 持有此组件
 *   - AController.Possess(APawn) -> 调用 ApplyConfig
 *   - BT/BTTask 节点 可放心 GetConfig() 读 SO
 *
 * 大厂架构角色:
 *   - 真理源: Config 字段 = 运行时 AI 行为参数的唯一入口
 *   - 数据驱动: 策划在 DataAsset 改参数 → 立即生效 (无需重启游戏)
 *   - 难度倍率: DifficultyTier 影响所有 GetScaled* 接口返回值的倍率缩放
 *   - 零 Tick: 纯读取型组件, 不参与运行时计算
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

    /**
     * 读取当前生效 Config (供 BT/BTTask 用)
     *
     * @return 当前 Config, 可能为 null (未 ApplyConfig 或已清空)
     * @note 调用方应检查返回值, 不要假设 Config 非空
     */
    UFUNCTION(BlueprintPure, Category = "AI|Config")
    const UAIBehaviorConfigSO* GetConfig() const { return Config; }

    UFUNCTION(BlueprintPure, Category = "AI|Config")
    EAIDifficultyTier GetDifficultyTier() const { return DifficultyTier; }

    /**
     * @brief 读取难度缩放后的感知参数
     *
     * @return Config.Perception × DifficultyTier 倍率; Config 为空时返回默认零值
     */
    UFUNCTION(BlueprintPure, Category = "AI|Config")
    FAIPerceptionParams GetScaledPerception() const;

    /**
     * @brief 读取难度缩放后的战斗参数 (攻击范围/冷却/伤害倍率等)
     *
     * @return Config.Combat × DifficultyTier 倍率; Config 为空时返回默认零值
     */
    UFUNCTION(BlueprintPure, Category = "AI|Config")
    FAICombatParams GetScaledCombat() const;

    /**
     * @brief 读取难度缩放后的移动参数 (巡逻半径/距离维持阈值等)
     *
     * @return Config.Movement × DifficultyTier 倍率; Config 为空时返回默认零值
     */
    UFUNCTION(BlueprintPure, Category = "AI|Config")
    FAIMovementParams GetScaledMovement() const;

protected:
    /**
     * 当前生效的配置 SO
     * 持有为强引用 - 因为 ApplyConfig 时已经确保资源已加载
     * 不要在这里用 TSoftObjectPtr - Config 生命周期与 Controller 一致
     *
     * 数据流:
     *   - 写入: ABaseAIController::Possess → ApplyConfig
     *   - 读取: BT/BTTask 通过 GetConfig() / GetScaled* 接口读取
     *   - 不复制: 这是 Controller 本地状态, 不跨网络
     */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI|Config")
    TObjectPtr<UAIBehaviorConfigSO> Config;

    /** 难度档位 - 默认 Normal
     *
     * 数据流:
     *   - 写入: GameMode 可调用 SetDifficultyTier 运行时切换
     *   - 读取: GetScaled* 接口读取此字段用于倍率缩放
     *   - 不复制: 难度档位是服务器权威决策, 客户端不需要单独配置
     */
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI|Config")
    EAIDifficultyTier DifficultyTier = EAIDifficultyTier::Normal;
};
