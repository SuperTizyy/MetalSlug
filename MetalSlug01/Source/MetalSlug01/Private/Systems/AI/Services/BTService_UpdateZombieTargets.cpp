// Copyright (c) 2026.
//
// 【v107 2026.07.28 生化模式 AI】BTService — 母体/人类目标选择实现

#include "Systems/AI/Services/BTService_UpdateZombieTargets.h"

#include "AIController.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "GameFramework/Pawn.h"

#include "Characters/BaseCharacter.h"
#include "Systems/BaseAIController.h"
#include "Systems/Mother/RoomMotherMutationSubsystem.h"
#include "Systems/Spawn/RoomSpawnSubsystem.h"
#include "Systems/AI/AIBehaviorTypes.h"
#include "Data/AI/AIBehaviorConfigSO.h"
#include "Systems/AI/AIRuntimeConfigComponent.h"

UBTService_UpdateZombieTargets::UBTService_UpdateZombieTargets()
{
	NodeName = TEXT("Update Zombie Targets");

	Interval = 0.25f;
	RandomDeviation = 0.f;
	bTickIntervals = true;

	NearestHumanTargetKey.AddObjectFilter(this, GET_MEMBER_NAME_CHECKED(UBTService_UpdateZombieTargets, NearestHumanTargetKey), AActor::StaticClass());
	NearestMotherTargetKey.AddObjectFilter(this, GET_MEMBER_NAME_CHECKED(UBTService_UpdateZombieTargets, NearestMotherTargetKey), AActor::StaticClass());
	TargetActorKey.AddObjectFilter(this, GET_MEMBER_NAME_CHECKED(UBTService_UpdateZombieTargets, TargetActorKey), AActor::StaticClass());
	PrimaryFireRangeKey.AddFloatFilter(this, GET_MEMBER_NAME_CHECKED(UBTService_UpdateZombieTargets, PrimaryFireRangeKey));
	HomeLocationKey.AddVectorFilter(this, GET_MEMBER_NAME_CHECKED(UBTService_UpdateZombieTargets, HomeLocationKey));
}


FString UBTService_UpdateZombieTargets::GetStaticDescription() const
{
	return FString::Printf(
		TEXT("每 %.2fs 按距离最近选目标:\n"
			 "- 母体 → 最近存活人类 (BB.NearestHumanTarget + BB.TargetActor)\n"
			 "- 人类 → 最近存活母体 (BB.NearestMotherTarget + BB.TargetActor)\n"
			 "- 未知身份 → 清目标 + 写 HomeLocation (Pre-Mutation 期间不动)"),
		Interval);
}


namespace
{
	/**
	 * 平面距离 (Z 忽略) — 与 URoomZombieRallySubsystem 同算法, 避免楼层差误判
	 */
	FORCEINLINE float ZombieFlatDistSq(const FVector& A, const FVector& B)
	{
		const float DX = A.X - B.X;
		const float DY = A.Y - B.Y;
		return DX * DX + DY * DY;
	}
}


void UBTService_UpdateZombieTargets::TickNode(
	UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	Super::TickNode(OwnerComp, NodeMemory, DeltaSeconds);

	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	if (!BB)
	{
		UE_LOG(LogBehaviorTree, Error,
			TEXT("[BTService_UpdateZombieTargets] BB 为空, Service 不工作!"));
		return;
	}

	AAIController* AIC = OwnerComp.GetAIOwner();
	if (!AIC)
	{
		UE_LOG(LogBehaviorTree, Error,
			TEXT("[BTService_UpdateZombieTargets] AIC 为空, Service 不工作!"));
		return;
	}

	ABaseCharacter* SelfPawn = Cast<ABaseCharacter>(AIC->GetPawn());
	if (!SelfPawn)
	{
		UE_LOG(LogBehaviorTree, Error,
			TEXT("[BTService_UpdateZombieTargets] %s: Pawn 不是 ABaseCharacter, Service 不工作!"), *AIC->GetName());
		return;
	}

	// ──────────────────────────────────────────────
	// 1. 派生 PrimaryFireRange (真理源 ConfigSO)
	// ──────────────────────────────────────────────
	float PrimaryFireRange = 0.f;
	if (const ABaseAIController* BaseAIC = Cast<ABaseAIController>(AIC))
	{
		if (const UAIRuntimeConfigComponent* RuntimeConfig = BaseAIC->GetRuntimeConfig())
		{
			if (const UAIBehaviorConfigSO* Config = RuntimeConfig->GetConfig())
			{
				PrimaryFireRange = Config->ZombiePrimaryFireRange;
				BB->SetValueAsFloat(PrimaryFireRangeKey.SelectedKeyName, PrimaryFireRange);
			}
		}
	}

	// ──────────────────────────────────────────────
	// 2. 收集场景角色 + 选目标
	// ──────────────────────────────────────────────
	const FVector SelfLoc = SelfPawn->GetActorLocation();
	const float MaxDistSq = PrimaryFireRange * PrimaryFireRange;

	URoomMotherMutationSubsystem* MotherSys = URoomMotherMutationSubsystem::Get(this);
	URoomSpawnSubsystem* SpawnSys = URoomSpawnSubsystem::Get(this);
	if (!MotherSys || !SpawnSys)
	{
		// 【零兜底】Subsystem 缺失 = 配置错, 清理 BB 让 BT 知道"无目标"
		BB->ClearValue(TargetActorKey.SelectedKeyName);
		BB->ClearValue(NearestHumanTargetKey.SelectedKeyName);
		BB->ClearValue(NearestMotherTargetKey.SelectedKeyName);

		UE_LOG(LogBehaviorTree, Error,
			TEXT("[BTService_UpdateZombieTargets] %s: Subsystem 缺失 (MotherSys=%d SpawnSys=%d)! "
			     L"【根因】1) Mode!=Zombie; 2) MotherMutationSubsystem::InitializeSubsystem 未调用."),
			*AIC->GetName(),
			!!MotherSys, !!SpawnSys);
		return;
	}

	const TArray<ABaseCharacter*> AllChars = SpawnSys->GetAllBattleCharacters();

	ABaseCharacter* BestTarget = nullptr;
	float BestDistSq = TNumericLimits<float>::Max();

	if (SelfPawn->bIsMother)
	{
		// 母体 → 最近存活人类
		for (ABaseCharacter* Char : AllChars)
		{
			if (!IsValid(Char))   { continue; }
			if (Char == SelfPawn) { continue; }
			if (Char->IsDead())   { continue; }
			if (Char->bIsMother) { continue; } // 母体不要母体

			const float DistSq = ZombieFlatDistSq(SelfLoc, Char->GetActorLocation());
			if (MaxDistSq > 0.f && DistSq > MaxDistSq) { continue; }

			if (DistSq < BestDistSq)
			{
				BestDistSq = DistSq;
				BestTarget = Char;
			}
		}

		BB->SetValueAsObject(NearestHumanTargetKey.SelectedKeyName, BestTarget);
		BB->SetValueAsObject(TargetActorKey.SelectedKeyName, BestTarget);
		BB->ClearValue(NearestMotherTargetKey.SelectedKeyName);

		UE_LOG(LogBehaviorTree, Display,
			TEXT("[BTService_UpdateZombieTargets] %s: 母体, 扫描 %d 个角色, 找到人类目标=%s (Dist=%.0f)"),
			*AIC->GetName(), AllChars.Num(),
			BestTarget ? *BestTarget->GetName() : TEXT("NONE"),
			BestTarget ? FMath::Sqrt(BestDistSq) : -1.f);
	}
	else if (SelfPawn->bIsHuman)
	{
		// 人类 → 最近存活母体
		const TArray<TWeakObjectPtr<ABaseCharacter>>& Mothers = MotherSys->GetMotherCharacters();
		for (const TWeakObjectPtr<ABaseCharacter>& WeakMother : Mothers)
		{
			ABaseCharacter* Mother = WeakMother.Get();
			if (!IsValid(Mother))  { continue; }
			if (Mother == SelfPawn) { continue; }
			if (Mother->IsDead())  { continue; }

			const float DistSq = ZombieFlatDistSq(SelfLoc, Mother->GetActorLocation());
			if (MaxDistSq > 0.f && DistSq > MaxDistSq) { continue; }

			if (DistSq < BestDistSq)
			{
				BestDistSq = DistSq;
				BestTarget = Mother;
			}
		}

		BB->SetValueAsObject(NearestMotherTargetKey.SelectedKeyName, BestTarget);
		BB->SetValueAsObject(TargetActorKey.SelectedKeyName, BestTarget);
		BB->ClearValue(NearestHumanTargetKey.SelectedKeyName);

		UE_LOG(LogBehaviorTree, Display,
			TEXT("[BTService_UpdateZombieTargets] %s: 人类, 扫描 %d 个母体, 找到母体目标=%s (Dist=%.0f)"),
			*AIC->GetName(), Mothers.Num(),
			BestTarget ? *BestTarget->GetName() : TEXT("NONE"),
			BestTarget ? FMath::Sqrt(BestDistSq) : -1.f);
	}
	else
	{
		// 未知身份 (变异前 / 刚变异过渡期)
		// → 清 TargetActor + 写 HomeLocation = SelfPawn 当前位置 (出生点)
		BB->ClearValue(TargetActorKey.SelectedKeyName);
		BB->ClearValue(NearestHumanTargetKey.SelectedKeyName);
		BB->ClearValue(NearestMotherTargetKey.SelectedKeyName);
		BB->SetValueAsVector(HomeLocationKey.SelectedKeyName, SelfLoc);

		UE_LOG(LogBehaviorTree, Display,
			TEXT("[BTService_UpdateZombieTargets] %s: ★ 未知身份 (bIsMother=0 bIsHuman=0), 无有效目标."
			     L" 写入 HomeLocation=(%s) 作为 Pre-Mutation 期间默认移动目标."),
			*AIC->GetName(), *SelfLoc.ToString());
	}
}