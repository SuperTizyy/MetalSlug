// Copyright (c) 2026.
//
// 【v119 2026.08.03 BT 原子库】BTTask — 激活母体加速技能
//
// 架构定位 (重要):
//   - 本 Task 是**纯原子能力** — 只负责"激活母体加速技能"
//   - **不做** 冷却判断 (上游 BTDecorator_MotherSkillReady 接管)
//   - **不做** 母体身份判断 (上游 Decorator 接管)
//   - **不做** BB.CooldownEndTime 写入 (由 MotherSkillComponent 内部写)
//
// 职责分工 (v119 大厂方案):
//   1. Decorator: BTDecorator_MotherSkillReady → 实时读 World.Time vs BB.MotherSkillCooldownEndTime, 0 延迟决策
//   2. BTTask_ActivateMotherSpeedBoost (本节点): 纯原子调用 MotherSkillComponent::ActivateSkill
//
// 与 BTDecorator_CooldownReady + BTTask_PlayAttackMontage 完全镜像:
//   - CooldownReady: Decorator 读冷却 → PlayAttackMontage Task 激活攻击
//   - MotherSkillReady: Decorator 读冷却 → ActivateMotherSpeedBoost Task 激活技能
//
// 使用方式 (BT 编辑器):
//   Sequence "MotherSkill"
//   ├─ Decorator: BTDecorator_MotherSkillReady    (冷却是否结束)
//   ├─ BTTask_ActivateMotherSpeedBoost (本节点 — 纯原子, 同步 Succeeded)
//
// 大厂原则对照 (v119):
//   - 单一职责: Task 只调一个 C++ 函数
//   - 零兜底: 冷却结束才进本节点, ActivateSkill 内部处理所有防御
//   - 关注点分离: BT 决策 / C++ 原子能力 严格分工

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTTask_ActivateMotherSpeedBoost.generated.h"


/**

* UBTTask_ActivateMotherSpeedBoost — v119 原子化
 *
 * 激活母体加速技能 — 调 BaseCharacter::OnActivateMotherSpeedBoost
 *
 * 使用方式 (BT 编辑器):
 *   Sequence "MotherSkill"
 *   ├─ Decorator: BTDecorator_MotherSkillReady    (冷却是否结束)
 *   ├─ BTTask_ActivateMotherSpeedBoost (本节点 — 纯原子, 同步 Succeeded)
 *
 * 注意:
 *   - 本节点只做一件事: 调 OnActivateMotherSpeedBoost
 *   - 冷却判断由上游 BTDecorator_MotherSkillReady 决策
 *   - 失败原因全部由 OnActivateMotherSpeedBoost 内部 Log Error + return false 暴露
 */
UCLASS(Blueprintable, meta = (DisplayName = "Activate Mother Speed Boost (激活母体加速技能)"))
class METALSLUG01_API UBTTask_ActivateMotherSpeedBoost : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_ActivateMotherSpeedBoost();

	virtual FString GetStaticDescription() const override;

protected:
	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp,
		uint8* NodeMemory) override;
};
