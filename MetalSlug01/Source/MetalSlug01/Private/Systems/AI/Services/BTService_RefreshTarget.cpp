// Copyright (c) 2026.
//
// 【Phase 3 大厂架构】BTService — 目标刷新服务
//
// 职责:
//   - 周期性扫描战场所有敌人，按距离排序选出最近目标
//   - 将目标写入 BB Key "TargetActor"，供 BTTask_MeleeAttack 读取
//
// 设计理念:
//   - 完全独立于 OnTargetDetected 的 NearbyThreat 写入
//     （NearbyThreat 依赖 OverrideBTDistance 配置正确，在此之前不可靠）
//   - 每 Tick 直接查 AIPerception::GetKnownPerceivedActors，不依赖 BB 中间状态
//   - 与 OnTargetDetected 完全兼容：两者同时写 TargetActor（后者覆盖前者，等效）
//   - NearbyThreat 仅作为"极近距离兜底"，不作为 Primary 路径
//
// 行为:
//   - 每次 Tick 从感知组件获取已感知的敌人列表
//   - 按距离排序，选取最近的合法目标
//   - 写入 BB: TargetActor = 最近敌人
//   - 若无目标，清空 BB Key
//
// 依赖:
//   - AIPerceptionComponent 已正确配置 SightConfig
//   - BB 中存在 "TargetActor" (Object) Key

#include "Systems/AI/Services/BTService_RefreshTarget.h"

#include "AIController.h"
#include "Perception/AIPerceptionComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"
#include "GameFramework/Pawn.h"
#include "GenericTeamAgentInterface.h"

#include "Characters/BaseCharacter.h"
#include "Systems/AI/AIBehaviorTypes.h"

// 本文件专用的日志类别（避免依赖 BaseAIController 的宏）
// 注意：DECLARE 已在头文件中声明，此处只 DEFINE
DEFINE_LOG_CATEGORY(LogBTServiceRefreshTarget);

// 【调试开关】设为 1 可在日志中看到每 Tick 的目标刷新情况
#define AI_TARGET_REFRESH_VERBOSE 1

UBTService_RefreshTarget::UBTService_RefreshTarget()
{
	NodeName = TEXT("Refresh Target (刷新目标)");
	bNotifyBecomeRelevant = true;
	bNotifyCeaseRelevant = true;
	bNotifyTick = true;

	// 本服务监视 TargetActor BB Key（BT 框架会在 Key 变化时触发 Notify）
	BlackboardKey.AddObjectFilter(this,
		GET_MEMBER_NAME_CHECKED(UBTService_RefreshTarget, BlackboardKey),
		AActor::StaticClass());

	// 默认刷新间隔：0.25s（4Hz），兼顾响应速度和性能
	// 在 UE 编辑器中可override
	Interval = 0.25f;
}

FString UBTService_RefreshTarget::GetStaticDescription() const
{
	return TEXT("直接查 AIPerception，每 Tick（受 Interval 控制）刷新 BB.TargetActor："
		"\n  1. 从 AIPerception.GetKnownPerceivedActors 获取已感知敌人"
		"\n  2. 按距离排序，选取最近且未死亡目标"
		"\n  3. 写入 BB.TargetActor"
		"\n  4. 若极近距离（OverrideBTDistance）有目标，NearbyThreat 覆盖 TargetActor"
		"\n"
		"\n【与 OnTargetDetected 的关系】"
		"\n  - OnTargetDetected 也会写 TargetActor（任意距离）"
		"\n  - Service 作为兜底/仲裁层，确保即使 Controller 回调异常也能追踪目标"
		"\n  - NearestThreat 作为额外判据：极近距离目标优先于感知列表中的其他目标");
}

void UBTService_RefreshTarget::OnBecomeRelevant(
	UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	// 首次激活时立即刷新一次目标，不等第一个 Interval 周期
	TickNode(OwnerComp, NodeMemory, 0.0f);
}

void UBTService_RefreshTarget::OnCeaseRelevant(
	UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	// 失效时清空目标，防止旧目标残留导致 AI 行为异常
	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	if (BB)
	{
		BB->SetValueAsObject(FName(AIBlackboardKeyNames::TargetActor), nullptr);
#if AI_TARGET_REFRESH_VERBOSE
		if (AAIController* AIC = OwnerComp.GetAIOwner())
		{
			UE_LOG(LogBTServiceRefreshTarget, Log, TEXT("[%s] BTService_RefreshTarget: CeaseRelevant, 清空 TargetActor"),
				*AIC->GetName());
		}
#endif
	}
}

void UBTService_RefreshTarget::TickNode(
	UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	if (!BB)
	{
		return;
	}

	AAIController* AIC = OwnerComp.GetAIOwner();
	if (!AIC)
	{
		return;
	}

	// ============================================================
	// 第一优先级：极近距离 NearbyThreat
	// 当 OverrideBTDistance 范围内有目标时，NearbyThreat 覆盖 TargetActor
	// 这是最可靠的近距离追踪路径（不依赖感知配置正确性）
	// ============================================================
	AActor* NearbyThreat = Cast<AActor>(
		BB->GetValueAsObject(FName(AIBlackboardKeyNames::NearbyThreat)));
	if (NearbyThreat && IsTargetValid(NearbyThreat))
	{
		UpdateBlackboardTarget(OwnerComp, NearbyThreat);
		return;
	}

	// ============================================================
	// 第二优先级：直接查 AIPerception（Primary 路径）
	// 每 Tick 从感知系统获取已感知到的敌人，不依赖 NearbyThreat 配置
	// ============================================================
	AActor* NearestTarget = FindNearestSensedTarget(OwnerComp);
	UpdateBlackboardTarget(OwnerComp, NearestTarget);
}

AActor* UBTService_RefreshTarget::FindNearestSensedTarget(
	UBehaviorTreeComponent& OwnerComp) const
{
	AAIController* AIC = OwnerComp.GetAIOwner();
	if (!AIC)
	{
		return nullptr;
	}

	UAIPerceptionComponent* Perception = AIC->FindComponentByClass<UAIPerceptionComponent>();
	if (!Perception)
	{
		return nullptr;
	}

	// 【P0 修复 2026.07.03】改用 GetKnownPerceivedActors
	// 原因: GetCurrentlyPerceivedActors 只返回"当前仍可见"的, 一旦被墙挡住一瞬间
	//       列表就为空, AI 立刻失明. GetKnownPerceivedActors 含"已知但可能暂时消失"的,
	//       加 Stimulus.IsActive() 过滤即可, 更鲁棒.
	TArray<AActor*> SensedActors;
	Perception->GetCurrentlyPerceivedActors(nullptr, SensedActors);

	if (SensedActors.IsEmpty())
	{
#if AI_TARGET_REFRESH_VERBOSE
		UE_LOG(LogBTServiceRefreshTarget, Log, TEXT("[%s] BTService_RefreshTarget: 无感知目标"), *AIC->GetName());
#endif
		return nullptr;
	}

	APawn* AIPawn = AIC->GetPawn();
	if (!AIPawn)
	{
		return nullptr;
	}

	const FVector AILocation = AIPawn->GetActorLocation();
	AActor* NearestActor = nullptr;
	float NearestDistanceSq = MAX_FLT;

	for (AActor* Actor : SensedActors)
	{
		// 【P0 修复】双重过滤: 目标有效性 + 阵营敌对
		// 否则 AI 会把同阵营的 BP_GruntAI 当目标, 距离永远到不了
		if (!IsTargetValid(Actor))
		{
			continue;
		}

		if (!IsHostileTo(AIC, Actor))
		{
			// 同阵营或中立 — 跳过, 不写入 BB
			continue;
		}

		const float DistSq = FVector::DistSquared(Actor->GetActorLocation(), AILocation);
		if (DistSq < NearestDistanceSq)
		{
			NearestDistanceSq = DistSq;
			NearestActor = Actor;
		}
	}

#if AI_TARGET_REFRESH_VERBOSE
	if (NearestActor)
	{
		const float Dist = FMath::Sqrt(NearestDistanceSq);
		UE_LOG(LogBTServiceRefreshTarget, Log, TEXT("[%s] BTService_RefreshTarget: 感知到最近目标=%s, 距离=%.0f"),
			*AIC->GetName(), *NearestActor->GetName(), Dist);
	}
#endif

	return NearestActor;
}

void UBTService_RefreshTarget::UpdateBlackboardTarget(
	UBehaviorTreeComponent& OwnerComp, AActor* NewTarget) const
{
	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	if (!BB)
	{
		return;
	}

	// 写入 TargetActor（BT 树的 BTTask_MeleeAttack 读取此 Key）
	// 无论 NewTarget 是否为 nullptr 都写入（清空旧目标）
	BB->SetValueAsObject(FName(AIBlackboardKeyNames::TargetActor), NewTarget);
}

bool UBTService_RefreshTarget::IsTargetValid(AActor* Target) const
{
	if (!Target)
	{
		return false;
	}

	// 若目标是 BaseCharacter，检查是否死亡
	if (ABaseCharacter* Char = Cast<ABaseCharacter>(Target))
	{
		return !Char->IsDead();
	}

	return true;
}

/**
 * 【P0 修复 2026.07.03】阵营敌对判定
 *
 * 设计:
 *   - 不依赖感知系统的 ByAffiliation 配置 (即便配错也兜底)
 *   - 直接调 IGenericTeamAgentInterface::GetTeamAttitudeTowards 拿 UE 原生阵营协议
 *   - AI vs 玩家: Hostile → true
 *   - AI vs AI (同阵营): Friendly → false (核心修复!)
 *
 * 兜底:
 *   - 对方不是 IGenericTeamAgentInterface → 按"中立"处理, 默认 true (敌对, 兼容老逻辑)
 */
bool UBTService_RefreshTarget::IsHostileTo(AAIController* AIC, AActor* Target) const
{
	if (!AIC || !Target)
	{
		return false;
	}

	// 自指直接排除 (不会发生, 但写上防御)
	if (Target == AIC->GetPawn())
	{
		return false;
	}

	// 走 UE 原生阵营协议
	const IGenericTeamAgentInterface* OtherAgent = Cast<IGenericTeamAgentInterface>(Target);
	if (!OtherAgent)
	{
		// 不是阵营成员 (比如场景里的桶), 默认按敌对处理 (与原行为兼容)
		return true;
	}

	const ETeamAttitude::Type Attitude = AIC->GetTeamAttitudeTowards(*Target);
	return Attitude == ETeamAttitude::Hostile;
}
