// Copyright (c) 2026.
//
// 【Phase 3 大厂架构】BTTask — 刀战近战攻击任务节点
//
// 定位:
//   - 挂在 BT 的"攻击"叶节点
//   - 检查 AI 与目标距离是否在 AttackRange 内
//   - 若在范围内，执行攻击（调用 BaseCharacter::MeleeAttack）
//   - 若超出范围，返回 InProgress（让 MoveTo 先追上）
//
// 职责边界（绝对不写）:
//   - 不做寻路（MoveTo 交给 BT MoveTo 节点）
//   - 不做目标选择（TargetActor 由 BTService 维护）
//   - 不做目标刷新（RefreshTarget Service 负责）
//
// 攻击节流:
//   - 用 bHasAttackToken BB Key 做冷却保护
//   - 在 Service 刷新目标时不清空 Token，攻击完成后清空
//   - Token 冷却时长由 AIBehaviorConfigSO.Combat.AttackCooldown 控制
//
// 依赖:
//   - BB 中 TargetActor (Object) Key 有值
//   - BB 中 bHasAttackToken (bool) Key 存在
//   - ABaseCharacter::MeleeAttack() 方法已实现（空实现也行）

#pragma once

#include "CoreMinimal.h"
#include "Engine/TimerHandle.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTTask_MeleeAttack.generated.h"

class ABaseCharacter;

/**
 * UBTTask_MeleeAttack
 *
 * 刀战 AI 的唯一攻击入口
 *
 * 节点逻辑（每帧 tick）：
 *   1. 读取 BB.TargetActor
 *   2. 检查 bHasAttackToken（防抖）
 *   3. 计算距离是否在 AttackRange 内
 *      - 否 → 返回 InProgress（等待 MoveTo）
 *      - 是 → 执行 MeleeAttack()，设置 Token，返回 Succeeded
 *
 * 使用方式（UE 编辑器）：
 *   Sequence（锁定目标分支）内作为最后一个节点
 *   通常结构：
 *     Sequence
 *       ├─ MoveTo TargetActor
 *       └─ MeleeAttack（接下面）
 */
UCLASS(Blueprintable, meta = (DisplayName = "Melee Attack (刀战攻击)"))
class METALSLUG01_API UBTTask_MeleeAttack : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_MeleeAttack();

	/**
	 * 节点名称（UE 编辑器显示）
	 */
	virtual FString GetStaticDescription() const override;

	/**
	 * BB Key 监视器 — 继承自 BTNode 的标准做法
	 * 任务节点不像 Service_BlackboardBase 那样自带, 这里手动加一个
	 */
	UPROPERTY(EditAnywhere, Category = "Blackboard")
	struct FBlackboardKeySelector BlackboardKey;

protected:
	/**
	 * 节点执行入口
	 * @return Succeeded = 攻击完成（或距离不够等待）
	 *         Failed = 目标为空 / 非法
	 *         InProgress = 距离不够，在等 MoveTo 追上（AI 继续等待子树）
	 */
	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp,
		uint8* NodeMemory) override;

	/**
	 * 节点 Tick（每帧）
	 * 持续检测：进入 AttackRange 后立即执行攻击
	 */
	virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory,
		float DeltaSeconds) override;

private:
	/**
	 * 【P0 修复】取 Actor 中心点 (胶囊体中心 / 包围盒中心 / 偏移)
	 * 取代直接用 GetActorLocation() (脚底位置导致距离虚高)
	 */
	FVector GetCenterLocation(const AActor* Actor) const;

	/**
	 * 执行真正的攻击逻辑
	 * 条件：Token 可用 + 距离在 AttackRange 内
	 */
	bool PerformAttack(UBehaviorTreeComponent& OwnerComp);

	/**
	 * 消耗攻击令牌（写入 BB bHasAttackToken = true）
	 * 攻击冷却结束后由 GameMode 定时器或 Service 清空
	 */
	void ConsumeAttackToken(UBehaviorTreeComponent& OwnerComp);

	/**
	 * 从配置里读取 AttackRange（优先用 RuntimeConfig 缩放值）
	 */
	float GetAttackRange(UBehaviorTreeComponent& OwnerComp) const;

	/**
	 * 从配置里读取 AttackCooldown
	 */
	float GetAttackCooldown(UBehaviorTreeComponent& OwnerComp) const;

	/**
	 * 检查 Token 是否可用
	 */
	bool IsAttackTokenReady(UBehaviorTreeComponent& OwnerComp) const;

private:
	/** 攻击冷却 Timer 句柄（防止重复设置） */
	FTimerHandle AttackCooldownTimerHandle;
};
