// Copyright (c) 2026.
//
// 【v40.4 大厂重构 — 原子化】BTTask — 播放 AI 攻击蒙太奇 实现
//
// 设计原则:
//   - BT 负责决策 (何时进入 Attack Sequence 由 Decorator 判定)
//   - C++ 负责原子能力 (本 Task 触发 OnAIRequestAttack_Simple — 内部含所有副作用)
//   - Task 不再做: BB 写入 / AIController 状态设值 / 防御性节流 / 距离判断
//   - Task 只做: 调 OnAIRequestAttack_Simple → 返回 Succeeded/Failed
//
// 历史 (v22-v40.3) 反模式:
//   - 硬编码 BB Key 名 (FName(AIBlackboardKeyNames::CooldownEndTime))
//     → 策划改 BB Key 名时, Task 写错 Key, Decorator 永远拒判
//   - 写 BB.CooldownEndTime + 设 AIController 状态 (双线入口, 与 OnAIRequestAttack_Simple 内部冲突)
//     → BT 写一次, C++ 内部又写一次, 时序竞争
//   - 双防御节流 (LastAIAttackTimeSeconds + SafeInterval)
//     → BT Decorator_CooldownReady 已做实时冷却, 这是**重复架构**
//     → 配错 BT 时, C++ 节流"再撑一道墙", 根因永远不可见
//
// 新架构 (v40.4 — 单一入口 + 原子化):
//   - Task: 纯调用 OnAIRequestAttack_Simple, 不做任何副作用
//   - OnAIRequestAttack_Simple (AIAttackComponent): 内部统一处理
//     - 播放蒙太奇
//     - 绑 OnMontageEnded 回调
//     - 写 BB.CooldownEndTime (硬编码 Key 名 = 真理源)
//     - SetCurrentlyAttacking(true) / SetInAttackCooldown(true)
//     - OnMontageEnded 回调统一收口 (SetCurrentlyAttacking(false))
//   - BTDecorator_CooldownReady: 实时读 BB.CooldownEndTime vs World.Time
//   - BTTask_WaitMontageFinish: 异步等待蒙太奇结束
//
// 修复链 (v40.4 完整):
//   1. 用户问题: "BT 卡在 BTTask_PlayAttackMontage"
//   2. 真正根因: Task 写 BB Key 是**硬编码** (FName(AIBlackboardKeyNames::CooldownEndTime))
//      → 如果 BB_AI_Melee 资源里 Key 改名 (策划自由命名), Task 写到错的 Key
//      → BTDecorator_CooldownReady 读的是另一个 Key, 永远 > World.Time
//      → 永远拒判, BT 永远进不到 PlayAttackMontage (用户看到"卡在前面")
//   3. v40.4 修复: Task 不再写 BB — 由 OnAIRequestAttack_Simple 内部统一处理 (真理源一致)
//   4. 顺带修复: 删除 AIAttackComponent 中的双防御节流 (重复架构, 违反大厂原则)

#include "Systems/AI/Tasks/BTTask_PlayAttackMontage.h"

#include "BehaviorTree/BehaviorTreeComponent.h"
#include "AIController.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "GameFramework/Pawn.h"

// 基类
#include "Characters/BaseCharacter.h"
#include "Systems/BaseAIController.h"
#include "Systems/AI/AIBehaviorTypes.h"

UBTTask_PlayAttackMontage::UBTTask_PlayAttackMontage()
{
	NodeName = TEXT("Play Attack Montage");
}

FString UBTTask_PlayAttackMontage::GetStaticDescription() const
{
	// 【v133.1】描述展示当前配置的 4 个参数 + 锁定状态
	const TCHAR* TypeStr = (AttackType == EAIAttackType::Heavy) ? TEXT("Heavy") : TEXT("Light");
	const FString ComboStr = (AttackType == EAIAttackType::Light)
		? FString::Printf(TEXT("ComboIndex=%d"), ComboIndex)
		: FString(TEXT("(ComboIndex ignored for Heavy)"));
	const FString LockStr = bLockMovementDuringAttack
		? TEXT("锁脚")
		: TEXT("不锁脚 (边走边打)");

	// 【v133.1】ExplicitMontage 优先级最高, 用了就跳过 Resolver
	if (ExplicitMontage)
	{
		return FString::Printf(
			TEXT("【v133.1 Explicit Montage 路径】绕开 Resolver, 直接播指定蒙太奇\n"
			     "  ExplicitMontage='%s'\n"
			     "  移动=%s\n"
			     "→ 调 BaseCharacter::OnAIRequestAttack_ExplicitMontage\n"
			     "AttackType/ComboIndex 字段被忽略 (ExplicitMontage 优先级最高)"),
			*ExplicitMontage->GetName(),
			*LockStr);
	}

	return FString::Printf(
		TEXT("【v133 可配置】触发 AI 攻击\n"
		     "  AttackType=%s\n"
		     "  %s\n"
		     "  移动=%s\n"
		     "→ 调 BaseCharacter::OnAIRequestAttack_WithOptions\n"
		     "距离/冷却/目标空 全部由上游 Decorator 接管 (不重做)。\n"
		     "BB CooldownEndTime 由 AIAttackComponent 内部统一写入 (单一真理源)。"),
		TypeStr, *ComboStr, *LockStr);
}

EBTNodeResult::Type UBTTask_PlayAttackMontage::ExecuteTask(
	UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	// ============================================================
	// 【v40.5 P0 关键修复】BT 节点入口诊断 Log — 让 BT 实际跑到这里"可见"
	//
	// 背景 (Session1.log 2026.07.13):
	//   用户反馈: "AI 行为树卡在播放攻击蒙太奇上一直闪烁"
	//   根因诊断: BT 实际是否跑到 PlayAttackMontage 完全无日志, 看不到
	//   v40.4 重构时删了 BTTask 里所有 Log — 但**没补**任何节点级日志
	//   → 用户无法判断 BT 是"卡在 PlayAttackMontage"还是"卡在 Decorator 拒判"
	//
	// v40.5 大厂原则 (节点级可观测性):
	//   - BT 节点 = 业务决策单元, 入口必须有 Log (低频: 一次/执行)
	//   - 这是 UE 行为树标准实践 (Epic Lyra / Paragon 都有节点级 Verbose Log)
	//   - 与"重复架构 / 兜底"无关 — 这是可观测性基础设施
	//
	// 实施: 节点 Log 用 Display 级别 (默认可见), 不靠 Verbose (默认隐藏)
	// ============================================================
	UE_LOG(LogTemp, Display,
		TEXT("[BTTask_PlayAttackMontage] ExecuteTask ENTER. AIController=%s AttackType=%s "
		     "ComboIndex=%d bLockMovement=%d ExplicitMontage=%s"),
		*GetNameSafe(OwnerComp.GetAIOwner()),
		(AttackType == EAIAttackType::Heavy) ? TEXT("Heavy") : TEXT("Light"),
		ComboIndex,
		bLockMovementDuringAttack ? 1 : 0,
		ExplicitMontage ? *ExplicitMontage->GetName() : TEXT("(None, 走 Resolver)"));

	// ============================================================
	// 【v40.4 大厂重构 — 原子化】本 Task 只做一件事: 调 OnAIRequestAttack_*
	//
	// 历史 (v22-v40.3) 反模式 (5 个):
	//   1. 硬编码 BB Key (FName(AIBlackboardKeyNames::CooldownEndTime)) — 策划改名时永远写错 Key
	//   2. 写 BB.CooldownEndTime — 违反大厂"事件-观察-决策"分工, 应该是 Decorator 决策而非 Task 写
	//   3. 设 AIController C++ 状态 (SetCurrentlyAttacking/SetInAttackCooldown) — 与 OnAIRequestAttack_Simple 内部对称处理冲突
	//   4. AIAttackComponent 中 LastAIAttackTimeSeconds 节流 — 重复架构, BT Decorator_CooldownReady 已做实时冷却
	//   5. 无 ComboIndex 配置 — 硬编码 ComboIndex=1, 策划无法配置
	//
	// v40.4 修复: 全部 5 个反模式清除, Task 回归 60 行内的纯原子调用
	// v133 扩展: 暴露 AttackType/ComboIndex/bLockMovement 3 个可配置参数
	//
	// 真正"BT 卡住"的根因:
	//   - Task 写 BB Key 是**硬编码** (FName(AIBlackboardKeyNames::CooldownEndTime))
	//   - 策划在 BB_AI_Melee.uasset 可能改名 Key → Task 写到错的 Key
	//   - BTDecorator_CooldownReady 读的是 BB asset 里配置的 Key (可能改名)
	//   - Task 写的 Key ≠ Decorator 读的 Key → Decorator 永远 > World.Time → 永远拒判
	//   - 用户看到"BT 卡在 PlayAttackMontage", 实际是卡在上游 Decorator_CooldownReady
	// ============================================================

	// 1. 基础验证 — AIController + Pawn + Character
	AAIController* AIC = OwnerComp.GetAIOwner();
	if (!AIC)
	{
		UE_LOG(LogTemp, Display,
			TEXT("[BTTask_PlayAttackMontage] ExecuteTask FAIL: AIController 无效"));
		return EBTNodeResult::Failed;
	}

	APawn* AIPawn = AIC->GetPawn();
	if (!AIPawn)
	{
		UE_LOG(LogTemp, Display,
			TEXT("[BTTask_PlayAttackMontage] ExecuteTask FAIL: AIPawn 无效"));
		return EBTNodeResult::Failed;
	}

	// 【v133.9 P0 复活链路诊断】在 IsDead 检查之前, 输出 BT/Pawn/BB 状态
	//    解决 v133.8 仍无效问题: 用户反馈 "复活后不能播放蒙太奇", 实际场景可能是:
	//      - BT 跑进 PlayAttackMontage → 但因 IsDead 或别的原因 RETURN (用户看不到具体)
	//    新的诊断日志让根因一目了然 (不动业务, 只诊断)
	{
		const UBrainComponent* BrainComp = AIC->GetBrainComponent();
		UBlackboardComponent* BBComp = OwnerComp.GetBlackboardComponent();
		UObject* BBTargetObj = BBComp ? BBComp->GetValueAsObject(TEXT("TargetActor")) : nullptr;

		UE_LOG(LogTemp, Display,
			TEXT("[BTTask_PlayAttackMontage] 【v133.9 复活诊断】 ENTER 时 BT 状态: "
			     "BrainComponent=%s (IsRunning=%s) Pawn=%s (IsPendingKill=%d IsActorBeingDestroyed=%d) "
			     "BB.TargetActor=%s (IsValid=%d)"),
			BrainComp ? *BrainComp->GetName() : TEXT("NULL"),
			BrainComp ? (BrainComp->IsRunning() ? TEXT("YES") : TEXT("NO")) : TEXT("N/A"),
			*AIPawn->GetName(),
			AIPawn->IsPendingKillPending() ? 1 : 0,
			AIPawn->IsActorBeingDestroyed() ? 1 : 0,
			BBTargetObj ? *BBTargetObj->GetName() : TEXT("NULL"),
			BBTargetObj ? (IsValid(BBTargetObj) ? 1 : 0) : 0);
	}

	ABaseCharacter* AIChar = Cast<ABaseCharacter>(AIPawn);
	if (!AIChar || AIChar->IsDead())
	{
		UE_LOG(LogTemp, Display,
			TEXT("[BTTask_PlayAttackMontage] ExecuteTask FAIL: AIChar=%s (Dead=%d)"),
			*GetNameSafe(AIChar), AIChar ? AIChar->IsDead() : -1);
		return EBTNodeResult::Failed;
	}

	// 2. 触发攻击 — 按 ExplicitMontage 优先级分流
	//    【v133.1 P0 大厂扩展】三级回退:
	//      优先级 1 (BT 节点直接配): ExplicitMontage != nullptr → 走 OnAIRequestAttack_ExplicitMontage
	//      优先级 2 (武器 BP):        ExplicitMontage == nullptr → 走 OnAIRequestAttack_WithOptions (Resolver)
	//    两种路径共享副作用: 锁脚 + PlayAnimMontage + 绑 OnMontageEnded + 写 BB.CooldownEndTime
	bool bFired = false;
	if (ExplicitMontage)
	{
		// 【v133.1 优先级 1】BT 节点直接指定蒙太奇 (徒手 AI 用)
		//   - 跳过 Resolver, 直接用 BT 配的 Montage
		//   - 适用: 母体 Zombie 抓人, 武器 BP 无 LightAttackMontages
		UE_LOG(LogTemp, Display,
			TEXT("[BTTask_PlayAttackMontage] ExecuteTask 走 ExplicitMontage 路径 (bLockMovement=%d). "
			     "Montage=%s (Asset 路径来自 BT 节点配置)"),
			bLockMovementDuringAttack ? 1 : 0,
			*ExplicitMontage->GetName());

		bFired = AIChar->OnAIRequestAttack_ExplicitMontage(
			ExplicitMontage, bLockMovementDuringAttack);
	}
	else
	{
		// 【v133 优先级 2】BT 节点没配 ExplicitMontage, 走 Resolver 链路
		//   - 传递策划在 BT 编辑器里配的 3 个参数 (AttackType/ComboIndex/bLockMovement)
		//   - 武器 BP 里有 LightAttackMontages/HeavyAttackMontage 时应该走这条路径
		bFired = AIChar->OnAIRequestAttack_WithOptions(
			AttackType, ComboIndex, bLockMovementDuringAttack);
	}

	if (!bFired)
	{
		// OnAIRequestAttack_* 内部已 Log Error/Warning 暴露具体根因
		// (例如: 武器没配 / ComboIndex 没配 / ExplicitMontage 无效 / 玩家无敌期 / 死亡状态)
		// 大厂原则 - 显式优于隐式: 让 BT 看到 Failed, Selector 回退到 Chase 分支
		UE_LOG(LogTemp, Display,
			TEXT("[BTTask_PlayAttackMontage] ExecuteTask FAIL: OnAIRequestAttack_* 返回 false (内部 Log 已暴露根因)"));
		return EBTNodeResult::Failed;
	}

	// 3. 同步 Succeeded — 蒙太奇等待交给 WaitMontageFinish
	//
	// 大厂原则 - 单一职责:
	//   - 本 Task 只负责"触发攻击" — 完成
	//   - 蒙太奇播放是异步事件 → 交给 BTTask_WaitMontageFinish (异步 InProgress)
	//   - BB 写入 + 状态设置由 OnAIRequestAttack_WithOptions 内部统一处理
	UE_LOG(LogTemp, Display,
		TEXT("[BTTask_PlayAttackMontage] ExecuteTask SUCCESS. 蒙太奇由 WaitMontageFinish 接管"));
	return EBTNodeResult::Succeeded;
}