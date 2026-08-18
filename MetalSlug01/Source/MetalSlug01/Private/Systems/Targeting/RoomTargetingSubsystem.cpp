// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file RoomTargetingSubsystem.cpp
 * @brief AI 仇恨账本 + 目标选择子系统实现
 */
#include "Systems/Targeting/RoomTargetingSubsystem.h"
#include "Systems/RoomGameState.h"
#include "Systems/Core/RoomPlayerState.h"
#include "Systems/BaseAIController.h"
#include "Data/AI/AIBehaviorConfigSO.h"
#include "Characters/BaseCharacter.h"
#include "GenericTeamAgentInterface.h"
#include "GameFramework/PlayerState.h"
#include "Kismet/GameplayStatics.h"

// ==========================================
// UWorldSubsystem 基础
// ==========================================

URoomTargetingSubsystem* URoomTargetingSubsystem::Get(const UObject* WorldContextObject)
{
	if (!WorldContextObject) return nullptr;
	if (UWorld* World = WorldContextObject->GetWorld())
	{
		return World->GetSubsystem<URoomTargetingSubsystem>();
	}
	return nullptr;
}

/**
 * @brief 子系统创建守卫 — Server-only(仅服务器端创建)
 *
 * 镜像 v31.5 风格: NetMode != NM_Client 才允许创建
 * AI 仇恨账本纯服务端管理, 客户端通过 GameState 复制感知
 */
bool URoomTargetingSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (UWorld* World = Cast<UWorld>(Outer))
	{
		return World->GetNetMode() != NM_Client;
	}
	return false;
}

// ==========================================
// 账本查询
// ==========================================

int32 URoomTargetingSubsystem::GetAttackerCount(ABaseCharacter* TargetEnemy)
{
	int32 Count = 0;
	if (TargetEnemy)
	{
		for (const auto& Pair : AIHuntingMap)
		{
			if (IsValid(Pair.Key) && !Pair.Key->IsDead() && Pair.Value == TargetEnemy)
			{
				Count++;
			}
		}
	}
	return Count;
}

/**
 * @brief 判断目标是否被任意 AI 锁定(用于 BT 装饰器快速判定)
 * @param TargetEnemy 候选目标
 * @return true=至少有一个存活的 AI 正在追该目标
 *
 * 等价于 GetAttackerCount(TargetEnemy) > 0
 * 常用于 BTDecorator_TargetAvailable 之类的节点
 */
bool URoomTargetingSubsystem::IsTargetLocked(ABaseCharacter* TargetEnemy)
{
	return TargetEnemy && GetAttackerCount(TargetEnemy) > 0;
}

/**
 * @brief 判断目标是否被排除自己之外的其他 AI 锁定(反扎堆判定)
 * @param TargetEnemy 候选目标
 * @param ExcludeAI 要排除的 AI(通常是请求者自己)
 * @return true=有其他 AI 在追这个目标
 *
 * 用于 RequestTargetForAI 的反扎堆过滤 — 避免 AI 都打同一个目标
 */
bool URoomTargetingSubsystem::IsTargetLockedByOthers(ABaseCharacter* TargetEnemy, ABaseCharacter* ExcludeAI)
{
	if (!TargetEnemy) return false;
	int32 Count = 0;
	for (const auto& Pair : AIHuntingMap)
	{
		if (IsValid(Pair.Key) && !Pair.Key->IsDead() && Pair.Value == TargetEnemy && Pair.Key != ExcludeAI)
		{
			Count++;
		}
	}
	return Count > 0;
}

// ==========================================
// 账本管理
// ==========================================

void URoomTargetingSubsystem::ReleaseTarget(ABaseCharacter* RequestingAI)
{
	if (IsValid(RequestingAI) && AIHuntingMap.Contains(RequestingAI))
	{
		AIHuntingMap.Remove(RequestingAI);
	}
}

/**
 * @brief 清空所有 AI 仇恨账本(回合切换 / 重置场景)
 *
 * 调用时机:
 * - 比赛结束 → 清空避免下一局继承
 * - Round 切换 → 清空让 AI 重新选目标
 *
 * 大厂原则: 单一入口清空, 避免散落 TMap.Empty() 写法不一致
 */
void URoomTargetingSubsystem::ClearAllHunting()
{
	AIHuntingMap.Empty();
}

TArray<ABaseCharacter*> URoomTargetingSubsystem::GetAllAliveEnemiesFor(ABaseCharacter* RequestingAI)
{
	TArray<ABaseCharacter*> AliveEnemies;
	if (!RequestingAI) return AliveEnemies;

	TArray<AActor*> AllCharacters;
	UGameplayStatics::GetAllActorsOfClass(this, ABaseCharacter::StaticClass(), AllCharacters);
	for (AActor* Actor : AllCharacters)
	{
		ABaseCharacter* Char = Cast<ABaseCharacter>(Actor);
		if (Char && Char != RequestingAI && !Char->GetIsDead()
			&& RequestingAI->GetTeamAttitudeTowards(*Char) == ETeamAttitude::Hostile)
		{
			AliveEnemies.Add(Char);
		}
	}
	return AliveEnemies;
}

FAIHuntPolicy URoomTargetingSubsystem::GetEffectiveHuntPolicy(ABaseCharacter* AI) const
{
	if (!AI)
	{
		return FAIHuntPolicy();
	}
	AController* Ctrl = AI->GetController();
	if (ABaseAIController* BaseAIC = Cast<ABaseAIController>(Ctrl))
	{
		// 【v54 大厂架构重构】HuntPolicy 已从 UAIProfileAsset 迁移到 UAIBehaviorConfigSO
		// 旧: BaseAIC->GetCurrentProfile()->HuntPolicy (Profile 已删除)
		// 新: BaseAIC->GetConfig()->HuntPolicy (ConfigSO 真理源)
		if (const UAIBehaviorConfigSO* Config = BaseAIC->GetConfig())
		{
			return Config->HuntPolicy;
		}
		// 【v54 零兜底】Config 为空 — 拒绝 fallback, 直接返回默认 HuntPolicy 并 Log Error
		UE_LOG(LogTemp, Error,
			TEXT("[RoomTargeting] GetEffectiveHuntPolicy: AI=%s 的 ConfigSO 为空. "
				 "【v54 零兜底】未配置 ConfigSO 的 AI 不能参与目标选择, 返回默认 HuntPolicy (NearestDistance). "
				 "【修复】检查 SetupMeleeAI/InitializeFromConfig 是否正确写入 Config, 或 DA_AIBehaviorConfig_*.uasset 是否有配置."),
			*AI->GetName());
	}
	return FAIHuntPolicy();
}

// ==========================================
// 目标评分
// ==========================================

float URoomTargetingSubsystem::ScoreCandidateForAI(ABaseCharacter* RequestingAI,
	ABaseCharacter* Candidate, const FAIHuntPolicy& Policy) const
{
	if (!RequestingAI || !Candidate) return 0.f;

	// 距离维度
	float DistanceScore = 0.f;
	if (Policy.DistanceWeight > 0.f && Policy.MaxChaseDistance > 0.f)
	{
		const float Dist = FVector::Dist(Candidate->GetActorLocation(), RequestingAI->GetActorLocation());
		DistanceScore = FMath::Clamp(1.f - (Dist / Policy.MaxChaseDistance), 0.f, 1.f);
	}

	// 积分维度
	float PlayerScoreScore = 0.f;
	if (Policy.ScoreWeight > 0.f)
	{
		const float PS_Score = Candidate->GetPlayerState() ? Candidate->GetPlayerState()->GetScore() : 0.f;
		PlayerScoreScore = FMath::Clamp(PS_Score / 100.f, 0.f, 1.f);
	}

	// 剩余时间维度 (秒数越多分越低, 因为越后期越不重要)
	float TimeRemainingScore = 0.f;
	if (Policy.TimeRemainingWeight > 0.f)
	{
		if (ARoomGameState* GS = GetWorld()->GetGameState<ARoomGameState>())
		{
			const float Secs = GS->GetMatchRemainingSeconds();
			const float MaxSecs = 600.f;
			TimeRemainingScore = 1.f - FMath::Clamp(Secs / MaxSecs, 0.f, 1.f);
		}
	}

	// 反扎堆维度 (惩罚: 被其他人锁定时扣分)
	float AntiHuddle = 0.f;
	if (Policy.AntiHuddleWeight > 0.f)
	{
		const bool bLockedByOthers = const_cast<URoomTargetingSubsystem*>(this)->IsTargetLockedByOthers(Candidate, RequestingAI);
		AntiHuddle = bLockedByOthers ? Policy.AntiHuddleWeight : 0.f;
	}

	return FMath::Clamp(
		Policy.DistanceWeight       * DistanceScore +
		Policy.ScoreWeight          * PlayerScoreScore +
		Policy.TimeRemainingWeight  * TimeRemainingScore -
		Policy.AntiHuddleWeight     * AntiHuddle,
		0.f, 1.f);
}

// ==========================================
// 目标选择主入口
// ==========================================

ABaseCharacter* URoomTargetingSubsystem::RequestTargetForAI(ABaseCharacter* RequestingAI, const TArray<ABaseCharacter*>& InCandidates)
{
	if (!RequestingAI || InCandidates.Num() == 0)
	{
		return nullptr;
	}

	const FAIHuntPolicy Policy = GetEffectiveHuntPolicy(RequestingAI);

	// 距离预过滤
	TArray<ABaseCharacter*> ValidCandidates = InCandidates;
	ValidCandidates.RemoveAll([&Policy, RequestingAI](ABaseCharacter* Enemy)
	{
		return !Enemy || FVector::Dist(Enemy->GetActorLocation(), RequestingAI->GetActorLocation()) > Policy.MaxChaseDistance;
	});
	if (ValidCandidates.Num() == 0)
	{
		return nullptr;
	}

	// 【2026.07.13 v40.7 反扎堆严格化】删除 fallback 到 LockedEnemies
	//
	// 旧版: Pool = UnlockedEnemies.Num() > 0 ? UnlockedEnemies : LockedEnemies
	//   问题: 当所有敌人都被锁定时, AI 仍会从 LockedEnemies 抢别人目标 → 扎堆
	//
	// 新版: 只选 UnlockedEnemies, 无 unlocked → return nullptr
	//   - Service 层接收到 nullptr → 清 BB.TargetActor → BT 全部分支拒判 → AI 静止
	//   - 这是用户业务需求: "如果找不到任何没被锁定的敌人, 就去除 TargetActor 内的值"
	TArray<ABaseCharacter*> UnlockedEnemies;
	for (ABaseCharacter* Enemy : ValidCandidates)
	{
		if (!IsTargetLockedByOthers(Enemy, RequestingAI))
		{
			UnlockedEnemies.Add(Enemy);
		}
	}

	if (UnlockedEnemies.Num() == 0)
	{
		// 严格反扎堆: 无 unlocked 候选 → 不抢别人目标, 返回 nullptr
		// 调用方 (BTService_RefreshTarget) 收到 nullptr 会清空 BB.TargetActor
		return nullptr;
	}

	// 按评分选最佳
	ABaseCharacter* BestTarget = nullptr;
	float BestScore = -1.0f;
	for (ABaseCharacter* Candidate : UnlockedEnemies)
	{
		const float Score = ScoreCandidateForAI(RequestingAI, Candidate, Policy);
		if (Score > BestScore)
		{
			BestScore = Score;
			BestTarget = Candidate;
		}
	}

	// 写入账本
	if (BestTarget)
	{
		AIHuntingMap.Add(RequestingAI, BestTarget);
	}

	return BestTarget;
}