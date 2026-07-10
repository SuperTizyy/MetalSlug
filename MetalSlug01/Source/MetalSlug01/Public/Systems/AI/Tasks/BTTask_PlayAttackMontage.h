// Copyright (c) 2026.
//
// 【v40.4 大厂重构 — 原子化】BTTask — 播放 AI 攻击蒙太奇
//
// 架构定位 (重要):
//   - 本 Task 是**纯原子能力** — 只负责"触发 AI 攻击动画播放"
//   - **不做** 距离判断 (上游 BTDecorator_InAttackRange 接管)
//   - **不做** 冷却判断 (上游 BTDecorator_CooldownReady 接管)
//   - **不做** 目标空判断 (上游 Decorator 接管)
//   - **不做** BB CooldownEndTime 写入 (BTTask 一次性写违反大厂原则 - 应该是事件驱动而非轮询决策)
//   - **不做** AIController C++ 状态设值 (SetCurrentlyAttacking/SetInAttackCooldown - 由 OnAIRequestAttack_Simple 内部对称处理)
//
// 与旧 BTTask_PlayAttackMontage (v22-v40.3) 的关键区别:
//   - 旧版 102 行: 距离/冷却/目标空判断 + BB CooldownEndTime 硬编码 + AIController 状态设值 + 双防御节流
//     → 借 BT 壳做 C++ 决策, BT 编辑器看不见, 违反单一职责 + 单一真理源
//   - 新版 60 行: 纯原子调用 OnAIRequestAttack_Simple, BT 编辑器 100% 可见
//
// 冷却管理 (v40.4 终极方案 — 全部走 BT):
//   - BTDecorator_CooldownReady: 实时读 World.Time vs BB.CooldownEndTime, 0 延迟决策
//   - BTTask_PlayAttackMontage **不再**写 CooldownEndTime — 让 OnAIRequestAttack_Simple 内部对称写
//   - OnAIRequestAttack_Simple 内部在 PlayAnimMontage 成功后写 BB.CooldownEndTime (单一真理源)
//
// C++ 状态管理 (v40.4 — 单一入口):
//   - SetCurrentlyAttacking/SetInAttackCooldown 由 OnAIRequestAttack_Simple 内部对称设置 (攻击触发 → true, 蒙太奇结束 → false)
//   - BTTask 不再直接操控 AIController C++ 状态 (避免 BT/C++ 双向同步冲突)
//
// 双防御节流 (v40.4 删除):
//   - AIAttackComponent::OnAIRequestAttack_Simple 中 LastAIAttackTimeSeconds + SafeInterval 节流**删除**
//   - 原因: BTDecorator_CooldownReady 已做实时冷却, 这是**重复架构** (防御性节流掩盖 BT 配置错)
//   - 0 兜底原则: BT 配错应立即暴露, 不允许 C++ 兜底"再撑一道墙"

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTTask_PlayAttackMontage.generated.h"

/**
 * UBTTask_PlayAttackMontage — v40.4 原子化重构
 *
 * 触发 AI 攻击 — 调 BaseCharacter::OnAIRequestAttack_Simple
 *
 * 使用方式 (BT 编辑器):
 *   Sequence "Attack"
 *   ├─ Decorator: BTDecorator_HPThreshold          (HP > 0)
 *   ├─ Decorator: BTDecorator_InAttackRange        (距离 ∈ [AR-Hyst, AR+Hyst])
 *   ├─ Decorator: BTDecorator_CooldownReady        (WorldTime >= BB.CooldownEndTime)
 *   ├─ BTTask_FaceTarget                           (面向目标)
 *   ├─ BTTask_PlayAttackMontage (本节点 — 纯原子, 同步 Succeeded)
 *   └─ BTTask_WaitMontageFinish                    (异步 InProgress, 等蒙太奇结束)
 *
 * 注意 (v40.4 重构后):
 *   - 本节点**只做一件事**: 调 OnAIRequestAttack_Simple
 *   - 不写 BB (不再硬编码 CooldownEndTime Key 名 — 由 C++ 内部对称处理)
 *   - 不设 AIController C++ 状态 (由 OnAIRequestAttack_Simple 内部对称处理)
 *   - 不节流 (由 BTDecorator_CooldownReady 上游决策)
 *   - 失败原因全部由 OnAIRequestAttack_Simple 内部 Log Error + return false 暴露
 *
 * 大厂原则对照 (v40.4):
 *   - 单一职责: Task 只调一个 C++ 函数
 *   - 单一真理源: BB Key 名由 AIAttackComponent::OnAIRequestAttack_Simple 内部硬编码 (代码一致性)
 *   - 零兜底: 不做任何防御性节流, BT 配置错立即暴露
 *   - 关注点分离: BT 决策 / C++ 原子能力 严格分工
 */
UCLASS(Blueprintable, meta = (DisplayName = "Play Attack Montage (播放攻击动画)"))
class METALSLUG01_API UBTTask_PlayAttackMontage : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_PlayAttackMontage();

	virtual FString GetStaticDescription() const override;

protected:
	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp,
		uint8* NodeMemory) override;
};