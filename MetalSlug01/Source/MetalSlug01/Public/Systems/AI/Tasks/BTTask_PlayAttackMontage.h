// Copyright (c) 2026.
//
// 【P0 2026.07.08 BT 原子库】BTTask — 播放攻击蒙太奇 (重构自 BTTask_MeleeAttack)
//
// 架构定位 (重要):
//   - 本 Task 是原子能力 — 只负责"播放蒙太奇"
//   - **不做** 距离判断 (上游 Decorator 接管)
//   - **不做** 冷却判断 (Decorator_CooldownReady 接管)
//   - **不做** 目标空判断 (上游 Decorator 接管)
//
// 与旧 BTTask_MeleeAttack 的关键区别:
//   - 旧版 920 行: 距离判断 + 冷却 Token + 目标空判断 + 动画播放 + 兜底
//     → 借 BT 壳做 C++ 决策, BT 编辑器看不见
//   - 新版 80 行: 纯调 OnAIRequestAttack_Simple, BT 编辑器看得见
//
// 冷却管理 (v3 P0 2026.07.09 大厂方案):
//   - 本 Task 攻击开始时一次性写 BB.CooldownEndTime = CurrentTime + AttackCooldown
//   - 删除 bHasAttackToken / 删除 BTService_UpdateBlackboard 冷却分支 / 删除 BTDecorator_HasAttackToken
//   - BTService_UpdateBlackboard 不再维护任何冷却状态: 它只派生距离/HP 等世界事实
//   - **冷却由 BTDecorator_CooldownReady 实时决策** (读 World.Time vs CooldownEndTime, 0 延迟)
//   - 上游使用: Sequence "Attack" 头挂 Decorator_CooldownReady (配 CooldownEndTimeKey)

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTTask_PlayAttackMontage.generated.h"

/**
 * UBTTask_PlayAttackMontage
 * 触发 AI 攻击 — 调 BaseCharacter::OnAIRequestAttack_Simple
 *
 * 使用方式 (BT 编辑器):
 *   Sequence "Attack"
 *   ├─ Decorator: Distance ∈ [AR-10, AR+10] (原生 Compare BB Entries, 读 BB.AttackRange)
 *   ├─ Decorator: WorldTime >= CooldownEndTime (上游判定, BTDecorator_CooldownReady)
 *   ├─ BTTask_FaceTarget
 *   ├─ BTTask_PlayAttackMontage (本节点 — 同步 Succeeded, 一次性写 BB.CooldownEndTime)
 *   └─ BTTask_WaitMontageFinish (异步 InProgress, 等蒙太奇结束)
 *
 * 注意:
 *   - 本节点**同步**完成 — 蒙太奇播放是异步事件, 由 WaitMontageFinish 接管等待
 *   - 蒙太奇结束回调 → BaseCharacter::OnAIAttackMontageEnded (已在 v15 实现)
 *   - 冷却在攻击触发时由本节点一次性写 BB.CooldownEndTime, 由 BTDecorator_CooldownReady 实时决策
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