// Copyright (c) 2026.

/**
 * @file AIRuntimeConfigComponent.cpp
 * @brief UAIRuntimeConfigComponent 实现 — 数据驱动层的"写入 + 缩放读取"两个职责
 *
 * 大厂原则落地:
 *   - 零 Tick: 构造函数关闭 Tick, Config 是被动读取
 *   - 单一职责: 本文件只做"赋值"和"委托给 ConfigSO 的 GetScaled* 函数"
 *   - 零兜底: Config 为空时 GetScaled* 返回默认零值结构体 (不 Log, 不崩, 调用方自行检查)
 *
 * 性能:
 *   - ApplyConfig / SetDifficultyTier 是 O(1) 赋值
 *   - GetScaled* 是 BlueprintPure, 每次调用都会执行 ConfigSO 内的倍率计算 (无缓存, 符合 ConfigSO 设计意图)
 */

#include "Systems/AI/AIRuntimeConfigComponent.h"
#include "Data/AI/AIBehaviorConfigSO.h"

UAIRuntimeConfigComponent::UAIRuntimeConfigComponent()
{
    // 不需要 Tick - Config 是被动读取
    PrimaryComponentTick.bCanEverTick = false;
}

/**
 * @brief 注入当前生效的 AI 行为配置 SO (由 ABaseAIController 在 Possess 后调用)
 *
 * @param InConfig  要注入的配置 SO, 可为 null (表示清空配置)
 *
 * @note 这是 Config 字段唯一的写入入口, 真理源原则
 * @note 不校验 InConfig 是否有效 — 调用方负责传入合法 SO
 */
void UAIRuntimeConfigComponent::ApplyConfig(UAIBehaviorConfigSO* InConfig)
{
    Config = InConfig;
}

/**
 * @brief 设置难度档位 — 影响后续所有 GetScaled* 接口返回值的倍率缩放
 *
 * @param NewTier  新的难度档位 (Easy / Normal / Hard / Insane 等)
 *
 * @note GameMode 可在运行时调用此函数实现"动态难度调整"
 * @note 不复制: 这是服务器权威决策, 不需要同步到客户端
 */
void UAIRuntimeConfigComponent::SetDifficultyTier(EAIDifficultyTier NewTier)
{
    DifficultyTier = NewTier;
}

/**
 * @brief 读取难度缩放后的感知参数
 *
 * @return Config.Perception × DifficultyTier 倍率; Config 为空时返回默认零值结构体
 *
 * @note Config 为空时静默返回零值 — 这是允许的"业务空"状态, 调用方应检查
 */
FAIPerceptionParams UAIRuntimeConfigComponent::GetScaledPerception() const
{
    return Config ? Config->GetScaledPerception(DifficultyTier) : FAIPerceptionParams();
}

/**
 * @brief 读取难度缩放后的战斗参数 (攻击范围/冷却/伤害倍率等)
 *
 * @return Config.Combat × DifficultyTier 倍率; Config 为空时返回默认零值结构体
 *
 * @note Config 为空时静默返回零值 — 这是允许的"业务空"状态, 调用方应检查
 */
FAICombatParams UAIRuntimeConfigComponent::GetScaledCombat() const
{
    return Config ? Config->GetScaledCombat(DifficultyTier) : FAICombatParams();
}

/**
 * @brief 读取难度缩放后的移动参数 (巡逻半径/距离维持阈值等)
 *
 * @return Config.Movement × DifficultyTier 倍率; Config 为空时返回默认零值结构体
 *
 * @note Config 为空时静默返回零值 — 这是允许的"业务空"状态, 调用方应检查
 */
FAIMovementParams UAIRuntimeConfigComponent::GetScaledMovement() const
{
    return Config ? Config->GetScaledMovement(DifficultyTier) : FAIMovementParams();
}
