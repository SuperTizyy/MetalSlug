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
// 【v133 大厂扩展 — BT 编辑器可配置】
//   - AttackType (Light / Heavy): 选择轻击连击还是重击 (默认 Light)
//   - ComboIndex (1/2/3...): 轻击连击第几段 (默认 1 — 与 v40.4 行为兼容)
//   - bLockMovementDuringAttack (true / false): 攻击期间是否锁脚
//     - true  → 攻击时 MaxWalkSpeed=0 (CS:GO/Apex 标准: 站着挥刀)
//     - false → 攻击时保持 MaxWalkSpeed (当前 AI 路径默认行为: 边走边挥刀)
//   - 这 3 个参数全部在 BT 编辑器里配置, C++ 默认值与原行为一致 (零兜底兼容)
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
// 【v133 P0】EAIAttackType 公共枚举从 AIBehaviorTypes.h 取, 不在本文件重复定义
// 单一真理源 — 任何模块都可以引用这个枚举, 不形成循环依赖
#include "Systems/AI/AIBehaviorTypes.h"
// 【v133.1 P0】ExplicitMontage 字段需要 UAnimMontage 完整类型 (UPROPERTY 反射)
#include "Animation/AnimMontage.h"
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

	// ============================================================
	// 【v133 P0 大厂扩展 — BT 编辑器可配置】3 个 UPROPERTY
	// ============================================================

	/**
	 * 攻击类型 (Light / Heavy)
	 *
	 * 大厂原则 — 默认值保持原行为兼容:
	 *   - 默认 Light (与 v40.4 行为一致 — 永远调 ResolveLightAttackMontage)
	 *   - 策划选 Heavy → 调 ResolveHeavyAttackMontage (ComboIndex 字段被忽略)
	 */
	UPROPERTY(EditAnywhere, Category = "Attack",
		meta = (DisplayName = "Attack Type (攻击类型)"))
	EAIAttackType AttackType = EAIAttackType::Light;

	/**
	 * 轻击连击段索引 (1/2/3...)
	 *
	 * 仅 AttackType=Light 时生效. Heavy 时被 Resolver 忽略.
	 *
	 * 大厂原则 — 默认值保持原行为兼容:
	 *   - 默认 1 (与 v40.4 行为一致 — 永远 ComboIndex=1)
	 *   - 策划选 2/3 → 调 ResolveLightAttackMontage(ComboIndex=2/3)
	 *   - 必须 < Weapon.LightAttackMontages.Num(), 否则 Resolver Log Error 拒绝兜底
	 */
	UPROPERTY(EditAnywhere, Category = "Attack",
		meta = (DisplayName = "Combo Index (轻击段索引, 1/2/3...)",
		        ClampMin = "1", ClampMax = "10"))
	int32 ComboIndex = 1;

	/**
	 * 攻击期间是否锁定移动
	 *
	 * 大厂原则 — 默认 false 与当前 AI 路径一致:
	 *   - 默认 false (与 v40.4 行为一致 — AI 边走边挥刀, 不锁脚)
	 *   - 选 true → 攻击时临时 MaxWalkSpeed=0, 蒙太奇结束回调复原 (CS:GO/Apex 标准)
	 *   - 选 false → 攻击时 MaxWalkSpeed 保持原值 (MetalSlug 默认 AI 行为)
	 *
	 * 实现位置: AIAttackComponent::OnAIRequestAttack_WithOptions 内部
	 * 锁/解绑时机: PlayAnimMontage 成功前 Set, OnMontageEnded 回调中复原
	 */
	UPROPERTY(EditAnywhere, Category = "Attack",
		meta = (DisplayName = "Lock Movement During Attack (攻击期间锁脚)"))
	bool bLockMovementDuringAttack = false;

	/**
	 * 【v133.1 P0 大厂扩展 — 不拿武器的 AI 直接指定蒙太奇】
	 *
	 * 业务背景 (用户 2026.08.02 反馈):
	 *   "母体不拿武器, 就是播放抓人蒙太奇"
	 *   - 母体是徒手攻击 (Zombie Mutant), 武器 BP 没配 LightAttackMontages
	 *   - 用 Resolver 找不到任何蒙太奇 → Log Error 拒绝兜底 → 母体永远无法攻击
	 *   - 必须在 BT 编辑器里直接指定蒙太奇资产, 完全绕过武器 BP 配置
	 *
	 * 大厂原则 — 单一真理源 + 三级回退:
	 *   - 优先级 1 (最高): ExplicitMontage (BT 节点直接配) → 跳过 Resolver
	 *   - 优先级 2: Resolver按 AttackType/ComboIndex 查武器 BP
	 *   - 优先级 3: 全部失败 → Log Error 放弃攻击
	 *
	 * 优先级 1 适用场景:
	 *   - 徒手 AI (母体/Zombie 抓人/无武器)
	 *   - 测试用临时蒙太奇
	 *   - 武器 BP 还没配好的过渡期
	 *
	 * 优先级 1 不适用场景:
	 *   - 拿武器的 AI 应该用武器 BP 真真理源 (改武器动画不用改 BT)
	 *   - 多 AI 共享同一武器改动画改一处
	 */
	UPROPERTY(EditAnywhere, Category = "Attack",
		meta = (DisplayName = "Explicit Montage Override (直接指定蒙太奇, 优先于武器 BP)"))
	TObjectPtr<UAnimMontage> ExplicitMontage = nullptr;

protected:
	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp,
		uint8* NodeMemory) override;
};