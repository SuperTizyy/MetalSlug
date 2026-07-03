// Copyright (c) 2026.
//
// 【P0 2026.07.09 BT 原子库】BTDecorator — 冷却时间判断 (实时决策)
//
// 架构定位:
//   - 决策节点, 每次 BT 评估时立刻算"能不能攻击" — 0 延迟
//   - 不依赖任何 Service 周期写 Token (上层反模式: BTService 写 bHasAttackToken)
//
// 决策算法:
//   - 读 BB.CooldownEndTime (Float, GameTime 秒, BTTask_PlayAttackMontage 写一次)
//   - 读 World.Time
//   - WorldTime >= CooldownEndTime → 冷却结束, 可攻击
//
// 上层职责分工 (P0 2026.07.09 大厂方案):
//   - BTTask_PlayAttackMontage: 攻击触发时一次性写 BB.CooldownEndTime = Now + CD
//   - BTDecorator_CooldownReady (本类): 实时算"已冷却?", 不需要 Token
//   - 删除 bHasAttackToken BB Key (中间态冗余, 由 decorator 自决)
//
// 使用方式 (BT 编辑器):
//   Sequence "Attack"
//   ├─ Decorator_CooldownReady (本装饰器, 配 CooldownEndTimeKey)
//   └─ ... 攻击任务

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTDecorator.h"
#include "BehaviorTree/BehaviorTreeTypes.h"
#include "BTDecorator_CooldownReady.generated.h"

struct FBlackboardKeySelector;

/**
 * UBTDecorator_CooldownReady
 * 冷却时间判断 — 实时决策节点
 *
 * 读取 BB.CooldownEndTime (Float, GameTime 秒)
 * 计算: WorldTime >= CooldownEndTime
 * 返回: true = 冷却结束, 可攻击
 *
 * 关键设计 (大厂原则):
 *   - 实时算 (与 BTService 0.1s 周期写 Token 是反模式对比)
 *   - 配 FBlackboardKeySelector (改 Key 名走 Details 面板自动同步, 无硬编码)
 */
UCLASS(Blueprintable, meta = (DisplayName = "Cooldown Ready (冷却就绪)"))
class METALSLUG01_API UBTDecorator_CooldownReady : public UBTDecorator
{
	GENERATED_BODY()

public:
	UBTDecorator_CooldownReady();

	virtual FString GetStaticDescription() const override;

	/**
	 * 冷却截止时间 BB Key — Float, GameTime 秒
	 * 由 BTTask_PlayAttackMontage 在攻击触发时一次性写入 (Now + AttackCooldown)
	 * 默认 0 表示"从未攻击过", 首次评估时直接放行
	 */
	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector CooldownEndTimeKey;

protected:
	/**
	 * 计算装饰器条件: WorldTime >= BB.CooldownEndTime
	 * 这是大厂"事件-观察-决策"分工中的决策层, 完全实时 (无 Service 周期滞后)
	 */
	virtual bool CalculateRawConditionValue(UBehaviorTreeComponent& OwnerComp,
		uint8* NodeMemory) const override;
};
