// Copyright (c) 2026.
//
// 【v119 2026.08.03 BT 原子库】母体加速技能冷却装饰器 (实时决策)
//
// 架构定位:
//   - 决策节点, 每次 BT 评估时立刻算"技能冷却是否结束" — 0 延迟
//   - 不依赖任何 Service 周期写 Token
//
// 决策算法:
//   - 读 BB.MotherSkillCooldownEndTime (Float, GameTime 秒, BTTask_ActivateMotherSpeedBoost 写一次)
//   - 读 World.Time
//   - WorldTime >= CooldownEndTime → 冷却结束, 可施放技能
//
// 上层职责分工 (v119 大厂方案):
//   - BTTask_ActivateMotherSpeedBoost: 技能触发时一次性写 BB.MotherSkillCooldownEndTime = Now + Cooldown
//   - BTDecorator_MotherSkillReady (本类): 实时算"冷却是否结束", 不需要 Token
//
// 与 BTDecorator_CooldownReady 完全镜像:
//   - CooldownReady: 攻击冷却 (写 BB.CooldownEndTime)
//   - MotherSkillReady: 技能冷却 (写 BB.MotherSkillCooldownEndTime)
//
// 使用方式 (BT 编辑器):
//   Sequence "MotherSkill"
//   ├─ BTDecorator_MotherSkillReady (本装饰器, 配 CooldownEndTimeKey = MotherSkillCooldownEndTime)
//   └─ BTTask_ActivateMotherSpeedBoost

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTDecorator.h"
#include "BehaviorTree/BehaviorTreeTypes.h"
#include "BTDecorator_MotherSkillReady.generated.h"

struct FBlackboardKeySelector;

/**
 * UBTDecorator_MotherSkillReady
 * 母体加速技能冷却判断 — 实时决策节点
 *
 * 读取 BB.MotherSkillCooldownEndTime (Float, GameTime 秒)
 * 计算: WorldTime >= CooldownEndTime
 * 返回: true = 冷却结束, 可施放技能
 *
 * 关键设计 (大厂原则):
 *   - 实时算 (与 BTService 0.1s 周期写 Token 是反模式对比)
 *   - 配 FBlackboardKeySelector (改 Key 名走 Details 面板自动同步, 无硬编码)
 */
UCLASS(Blueprintable, meta = (DisplayName = "Mother Skill Ready (母体技能冷却就绪)"))
class METALSLUG01_API UBTDecorator_MotherSkillReady : public UBTDecorator
{
	GENERATED_BODY()

public:
	UBTDecorator_MotherSkillReady();

	/** @brief BT 编辑器静态描述 (显示 CooldownEndTimeKey 名) */
	virtual FString GetStaticDescription() const override;

	/**
	 * 技能冷却截止时间 BB Key — Float, GameTime 秒
	 * 由 BTTask_ActivateMotherSpeedBoost 在技能触发时一次性写入 (Now + SkillCooldown)
	 * 默认 0 表示"从未施放过技能", 首次评估时直接放行
	 */
	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector CooldownEndTimeKey;

protected:
	/**
	 * 计算装饰器条件: WorldTime >= BB.MotherSkillCooldownEndTime
	 * 这是大厂"事件-观察-决策"分工中的决策层, 完全实时 (无 Service 周期滞后)
	 */
	virtual bool CalculateRawConditionValue(UBehaviorTreeComponent& OwnerComp,
		uint8* NodeMemory) const override;
};
