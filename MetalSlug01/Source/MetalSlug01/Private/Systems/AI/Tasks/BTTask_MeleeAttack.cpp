// Copyright (c) 2026.

#include "Systems/AI/Tasks/BTTask_MeleeAttack.h"

#include "AIController.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"
#include "GameFramework/Actor.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Character.h"
#include "Components/CapsuleComponent.h"
#include "NavigationSystem.h"      // 【P0 修复】NavMesh 投影
#include "TimerManager.h"

#include "Characters/BaseCharacter.h"
#include "Systems/AI/AIRuntimeConfigComponent.h"
#include "Systems/AI/AIBehaviorTypes.h"

EBTNodeResult::Type UBTTask_MeleeAttack::ExecuteTask(
	UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	// 读取 BB 里的目标
	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	if (!BB)
	{
		return EBTNodeResult::Failed;
	}

	// 通过 BlackboardKey.SelectedKeyName 直接拿到绑定的 BB Key 名
	const FName TargetKeyName = BlackboardKey.SelectedKeyName;
	UObject* TargetObj = BB->GetValueAsObject(TargetKeyName);
	if (!TargetObj)
	{
		// 【P0 修复 2026.07.03】目标为空 → Failed (不再 InProgress)
		// 旧版坑: 返回 InProgress 但没 MoveTo 触发, BT 卡死
		// 新版: Failed → Selector 选兄弟节点 MoveTo 追上去
		return EBTNodeResult::Failed;
	}

	// 【类型安全】TargetObj 是 UObject*, 必须 Cast 到 AActor* 才能调 GetActorLocation
	AActor* TargetActor = Cast<AActor>(TargetObj);
	if (!TargetActor)
	{
		return EBTNodeResult::Failed;
	}

	AActor* Pawn = OwnerComp.GetAIOwner() ? OwnerComp.GetAIOwner()->GetPawn() : nullptr;
	if (!Pawn)
	{
		return EBTNodeResult::Failed;
	}

	ABaseCharacter* AIChar = Cast<ABaseCharacter>(Pawn);
	if (!AIChar || AIChar->IsDead())
	{
		return EBTNodeResult::Failed;
	}

	// 【P0 修复】距离计算用胶囊体中心, 不用脚底
	const float AttackRange = GetAttackRange(OwnerComp);
	const float Distance = FVector::Dist(
		GetCenterLocation(TargetActor), GetCenterLocation(Pawn));

	if (Distance > AttackRange)
	{
		// 【P0 大厂架构修复 2026.07.03】距离不够 → Failed, 让 BT Selector 选兄弟节点 (MoveTo)
		// 旧版坑: 返回 InProgress 永远卡死, BT 树期望上层有 MoveTo 但 Attack task 永远不退出
		// 新版: 单一职责 — Attack 只负责打, 追是另一个 Task 的事
		// 设计: BT_MeleeAI 树结构建议: Selector → [Sequence(MoveTo+Attack), Idle]
		//      Attack Failed → Selector 回退, 选 MoveTo 兄弟 → 追到位 → Attack 成功
		return EBTNodeResult::Failed;
	}

	// 距离够, 执行攻击
	if (PerformAttack(OwnerComp))
	{
		return EBTNodeResult::InProgress; // 攻击动画播放中, 等待 Tick 完成
	}

	return EBTNodeResult::Failed;
}

void UBTTask_MeleeAttack::TickTask(UBehaviorTreeComponent& OwnerComp,
	uint8* NodeMemory, float DeltaSeconds)
{
	// ExecuteTask 返回 InProgress 后, 每帧这里检查:
	// 1. 攻击动画是否播完 (bIsAttacking == false)
	// 2. 距离是否仍在 AttackRange 内
	// 两个条件同时满足时执行攻击, 然后用 FinishLatentTask(Succeeded) 退出, 让 BT 重跑

	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	if (!BB)
	{
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		return;
	}

	AActor* Pawn = OwnerComp.GetAIOwner() ? OwnerComp.GetAIOwner()->GetPawn() : nullptr;
	if (!Pawn)
	{
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		return;
	}

	ABaseCharacter* AIChar = Cast<ABaseCharacter>(Pawn);
	if (!AIChar || AIChar->IsDead())
	{
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		return;
	}

	const FName TargetKeyName = BlackboardKey.SelectedKeyName;
	UObject* TargetObj = BB->GetValueAsObject(TargetKeyName);
	AActor* TargetActor = Cast<AActor>(TargetObj);
	if (!TargetActor)
	{
		FinishLatentTask(OwnerComp, EBTNodeResult::Failed);
		return;
	}

	const float AttackRange = GetAttackRange(OwnerComp);
	// 【P0 修复】用胶囊体中心计算距离, 不用脚底
	const float Distance = FVector::Dist(
		GetCenterLocation(TargetActor), GetCenterLocation(Pawn));

	if (Distance > AttackRange)
	{
		// 【P0 修复 2026.07.03】敌人跑出攻击范围 → Succeeded 退出, 让 BT 重跑
		// 旧版: 持续 InProgress 永远不退出, BT 永远等不到 MoveTo 重新触发
		// 新版: Succeeded 表示"这次攻击序列结束", BT 重跑后会选 MoveTo 追上去
		FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
		return;
	}

	if (Distance <= AttackRange && PerformAttack(OwnerComp))
	{
		// 攻击已触发, 等待下一次可攻击窗口 (Tick 重入)
		return;
	}

	// 攻击失败 / 冷却中: 退出, BT 重跑
	FinishLatentTask(OwnerComp, EBTNodeResult::Succeeded);
}

bool UBTTask_MeleeAttack::PerformAttack(UBehaviorTreeComponent& OwnerComp)
{
	// 检查 Token (防抖)
	if (!IsAttackTokenReady(OwnerComp))
	{
		return false;
	}

	AActor* Pawn = OwnerComp.GetAIOwner() ? OwnerComp.GetAIOwner()->GetPawn() : nullptr;
	ABaseCharacter* AIChar = Cast<ABaseCharacter>(Pawn);
	if (!AIChar || AIChar->IsDead())
	{
		return false;
	}

	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	const FName TargetKeyName = BlackboardKey.SelectedKeyName;
	UObject* TargetObj = BB ? BB->GetValueAsObject(TargetKeyName) : nullptr;
	if (!TargetObj)
	{
		return false;
	}

	// 【核心】执行轻击攻击
	// LightAttack_Pressed() 是 protected, 但 AIChar 是同包类的访问是允许的
	// (本类和 BaseCharacter 都在 MetalSlug01 模块内, protected 跨类访问需要 friend 或 public)
	// 解决: 调用 public 接口 OnAIRequestAttack (在 BaseCharacter 加)
	AIChar->OnAIRequestAttack();

	// 消耗 Token
	ConsumeAttackToken(OwnerComp);

	return true;
}

void UBTTask_MeleeAttack::ConsumeAttackToken(UBehaviorTreeComponent& OwnerComp)
{
	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	if (!BB)
	{
		return;
	}

	// 【修复】消耗 Token：设置 bHasAttackToken = true（表示冷却中）
	// Timer 冷却结束后设为 false，允许下一次攻击
	BB->SetValueAsBool(FName(AIBlackboardKeyNames::bHasAttackToken), true);

	AActor* Pawn = OwnerComp.GetAIOwner() ? OwnerComp.GetAIOwner()->GetPawn() : nullptr;
	if (!Pawn)
	{
		return;
	}

	UWorld* World = Pawn->GetWorld();
	if (!World)
	{
		return;
	}

	const float Cooldown = GetAttackCooldown(OwnerComp);
	if (Cooldown <= 0.0f)
	{
		// 冷却为 0 时，立即重置 Token
		BB->SetValueAsBool(FName(AIBlackboardKeyNames::bHasAttackToken), false);
		return;
	}

	// 【修复】防重复设置 Timer：先清空已有 Timer，再设新的
	FTimerManager& TimerManager = World->GetTimerManager();
	TimerManager.ClearTimer(AttackCooldownTimerHandle);

	// 使用 Lambda 捕获，冷却结束后自动重置 Token
	TWeakObjectPtr<UBlackboardComponent> WeakBB(BB);
	TimerManager.SetTimer(AttackCooldownTimerHandle,
		[WeakBB]
		{
			if (WeakBB.IsValid())
			{
				WeakBB->SetValueAsBool(FName(AIBlackboardKeyNames::bHasAttackToken), false);
			}
		}, Cooldown, false);
}

float UBTTask_MeleeAttack::GetAttackRange(UBehaviorTreeComponent& OwnerComp) const
{
	AActor* Pawn = OwnerComp.GetAIOwner() ? OwnerComp.GetAIOwner()->GetPawn() : nullptr;
	ABaseCharacter* AIChar = Cast<ABaseCharacter>(Pawn);
	if (!AIChar)
	{
		return 180.f;
	}

	if (UAIRuntimeConfigComponent* Config =
		AIChar->GetController() ?
		AIChar->GetController()->FindComponentByClass<UAIRuntimeConfigComponent>() : nullptr)
	{
		if (Config->GetConfig())
		{
			return Config->GetScaledCombat().AttackRange;
		}
	}

	return 180.f;
}

float UBTTask_MeleeAttack::GetAttackCooldown(UBehaviorTreeComponent& OwnerComp) const
{
	AActor* Pawn = OwnerComp.GetAIOwner() ? OwnerComp.GetAIOwner()->GetPawn() : nullptr;
	ABaseCharacter* AIChar = Cast<ABaseCharacter>(Pawn);
	if (!AIChar)
	{
		return 1.2f;
	}

	if (UAIRuntimeConfigComponent* Config =
		AIChar->GetController() ?
		AIChar->GetController()->FindComponentByClass<UAIRuntimeConfigComponent>() : nullptr)
	{
		if (Config->GetConfig())
		{
			return Config->GetScaledCombat().AttackCooldown;
		}
	}

	return 1.2f;
}

bool UBTTask_MeleeAttack::IsAttackTokenReady(UBehaviorTreeComponent& OwnerComp) const
{
	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	if (!BB)
	{
		return true;
	}

	return !BB->GetValueAsBool(FName(AIBlackboardKeyNames::bHasAttackToken));
}

UBTTask_MeleeAttack::UBTTask_MeleeAttack()
{
	NodeName = TEXT("Melee Attack (刀战攻击)");

	// 本节点监控 TargetActor BB Key
	// BlackboardKey 是本类加的 FBlackboardKeySelector 成员
	BlackboardKey.AddObjectFilter(this,
		GET_MEMBER_NAME_CHECKED(UBTTask_MeleeAttack, BlackboardKey),
		ABaseCharacter::StaticClass());

	// 允许 Tick
	bNotifyTick = true;

	// 不需要 tick 以外的通知
	bNotifyTaskFinished = false;
}

FString UBTTask_MeleeAttack::GetStaticDescription() const
{
	return FString(TEXT("检查距离够近后调用 OnAIRequestAttack(), "
		"Tick 等待攻击动画完成。用 BB bHasAttackToken 防抖, "
		"冷却时长由 AIBehaviorConfigSO.Combat.AttackCooldown 配置。"));
}

/**
 * 【P0 修复 2026.07.03】取 Actor 中心点 — 不用脚底, 用胶囊体中心或包围盒中心
 *
 * 为什么:
 *   GetActorLocation() 返回 RootComponent 位置, 多数 Pawn 是脚底
 *   两个脚底相对时, 距离虚高 ~88cm (胶囊体高度一半)
 *   结果: AI 距离 547cm 永远到不了 AttackRange=180cm
 *
 * 修复:
 *   - 优先用胶囊体中心 (Character + CapsuleComponent)
 *   - 兜底: 没胶囊体就向上偏移 88cm
 *   - 普通 Actor 用包围盒中心
 */
FVector UBTTask_MeleeAttack::GetCenterLocation(const AActor* Actor) const
{
	if (!Actor)
	{
		return FVector::ZeroVector;
	}

	// 优先: 角色 + 胶囊体中心
	if (const ACharacter* Char = Cast<ACharacter>(Actor))
	{
		if (const UCapsuleComponent* Capsule = Char->GetCapsuleComponent())
		{
			return Capsule->GetComponentLocation();
		}
		// 没胶囊体就偏移半个胶囊高度
		FVector Loc = Char->GetActorLocation();
		Loc.Z += 88.f;
		return Loc;
	}

	// 兜底: 包围盒中心
	FVector Origin, Extent;
	Actor->GetActorBounds(false, Origin, Extent);
	return Origin;
}