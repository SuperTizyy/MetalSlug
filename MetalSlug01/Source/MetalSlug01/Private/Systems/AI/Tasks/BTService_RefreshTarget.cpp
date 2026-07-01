// Copyright (c) 2026.

#include "Systems/AI/Tasks/BTService_RefreshTarget.h"

#include "AIController.h"
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "GameFramework/Pawn.h"

#include "Systems/RoomGameMode.h"
#include "Characters/BaseCharacter.h"
#include "Systems/AI/AIBehaviorTypes.h"
#include "Systems/AI/AIRuntimeConfigComponent.h"
#include "Systems/BaseAIController.h"
#include "Data/AI/AIProfileAsset.h"

DEFINE_LOG_CATEGORY_STATIC(LogBTServiceRefresh, Log, All);

UBTService_RefreshTarget::UBTService_RefreshTarget()
{
	// 【架构说明】
	// NodeName 供 UE 编辑器显示
	NodeName = TEXT("Refresh Target (GameMode Arbitration)");

	// 【刷新频率】
	// 0.3s：足够快（玩家感觉不到 AI 反应迟钝），足够慢（不会每帧调 GameMode）
	Interval = 0.3f;
	RandomDeviation = 0.05f; // 加点抖动防止多 AI 同帧刷新

	// 【BB Key】
	// 父类 UBTService_BlackboardBase 持有 BlackboardKey (FBlackboardKeySelector)
	// AddObjectFilter 限定可绑定的 Key 类型
	BlackboardKey.AddObjectFilter(this, GET_MEMBER_NAME_CHECKED(UBTService_RefreshTarget, BlackboardKey), ABaseCharacter::StaticClass());
}

void UBTService_RefreshTarget::OnBecomeRelevant(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	Super::OnBecomeRelevant(OwnerComp, NodeMemory);

	UE_LOG(LogBTServiceRefresh, Log, TEXT("[%s] OnBecomeRelevant (BT=%s)"),
		*GetNameSafe(OwnerComp.GetAIOwner()),
		OwnerComp.GetCurrentTree() ? *OwnerComp.GetCurrentTree()->GetName() : TEXT("nullptr"));

	// Service 激活时立即刷一次目标（不要等 Interval）
	if (AAIController* AIC = OwnerComp.GetAIOwner())
	{
		RefreshTargetForAI(OwnerComp, AIC->GetPawn());
	}
}

void UBTService_RefreshTarget::OnCeaseRelevant(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	Super::OnCeaseRelevant(OwnerComp, NodeMemory);

	UE_LOG(LogBTServiceRefresh, Log, TEXT("[%s] OnCeaseRelevant"),
		*GetNameSafe(OwnerComp.GetAIOwner()));

	// Service 停止时释放锁定（AI 死亡或切换 BT 时触发）
	if (AAIController* AIC = OwnerComp.GetAIOwner())
	{
		ReleaseCurrentTarget(OwnerComp, AIC->GetPawn());
	}
}

void UBTService_RefreshTarget::RefreshTargetForAI(UBehaviorTreeComponent& OwnerComp, AActor* Pawn)
{
	if (!Pawn)
	{
		return;
	}

	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	if (!BB)
	{
		UE_LOG(LogBTServiceRefresh, Warning, TEXT("[%s] RefreshTargetForAI: no BB"), *GetNameSafe(Pawn));
		return;
	}

	UWorld* World = Pawn->GetWorld();
	if (!World)
	{
		return;
	}

	ARoomGameMode* GM = World->GetAuthGameMode<ARoomGameMode>();
	if (!GM)
	{
		UE_LOG(LogBTServiceRefresh, Warning, TEXT("[%s] RefreshTargetForAI: no RoomGameMode (skip arbitration)"), *GetNameSafe(Pawn));
		// 没有 RoomGameMode（编辑器拖入的测试 AI）：把 BB 写成 nullptr，不崩
		// 父类 UBTService_BlackboardBase 提供无参 GetSelectedBlackboardKey()
		BB->SetValueAsObject(GetSelectedBlackboardKey(), nullptr);
		return;
	}

	// 【核心调用】向 GameMode 仲裁层申请目标
	// 返回值三种情况：
	//   - 有效 Actor：仲裁分配的目标
	//   - nullptr：没有可用敌人（全部死亡或超出范围）
	ABaseCharacter* AI = Cast<ABaseCharacter>(Pawn);
	if (!AI)
	{
		UE_LOG(LogBTServiceRefresh, Warning, TEXT("[%s] RefreshTargetForAI: not ABaseCharacter"), *GetNameSafe(Pawn));
		return;
	}

	UE_LOG(LogBTServiceRefresh, Log, TEXT("[%s] RefreshTargetForAI: TeamId=%d, calling GameMode arbitration"),
		*GetNameSafe(Pawn), (int32)AI->GetGenericTeamId().GetId());

	// 如果当前持有目标，先检查是否应该放弃
	// 避免每次刷新都重新分配（节省 GameMode 开销）
	const FName TargetKey = GetSelectedBlackboardKey();
	if (ShouldAbandonTarget(OwnerComp, Pawn, BB, TargetKey))
	{
		// 目标失效，释放锁定，让 GameMode 重新分配
		GM->ReleaseTarget(AI);
		BB->SetValueAsObject(TargetKey, nullptr);
		UE_LOG(LogBTServiceRefresh, Log, TEXT("[%s] RefreshTargetForAI: abandon old target"), *GetNameSafe(Pawn));
		return;
	}

	// 【检查当前目标是否仍被锁定】（防止 AI 持有目标后，其他 AI 把这个目标抢走）
	// 如果当前目标被其他 AI 锁定了，说明我们该换目标了
	if (UObject* CurrentTarget = BB->GetValueAsObject(TargetKey))
	{
		if (ABaseCharacter* CurrentChar = Cast<ABaseCharacter>(CurrentTarget))
		{
			// 目标被其他人抢走了，释放并重新申请
			if (GM->IsTargetLockedByOthers(CurrentChar, AI))
			{
				GM->ReleaseTarget(AI);
				BB->SetValueAsObject(TargetKey, nullptr);
			}
			else
			{
				// 目标仍属于我们，不需要刷新
				return;
			}
		}
	}

	// 申请新目标
	ABaseCharacter* NewTarget = GM->RequestTargetForAI(AI);
	BB->SetValueAsObject(TargetKey, NewTarget);

	// 同步更新战斗状态
	const bool bInCombat = (NewTarget != nullptr);
	BB->SetValueAsBool(FName(AIBlackboardKeyNames::bIsInCombat), bInCombat);

	UE_LOG(LogBTServiceRefresh, Log,
		TEXT("[%s] RefreshTargetForAI: NewTarget=%s, bInCombat=%d"),
		*Pawn->GetName(),
		(NewTarget ? *NewTarget->GetName() : TEXT("nullptr")),
		bInCombat);
}

void UBTService_RefreshTarget::ReleaseCurrentTarget(UBehaviorTreeComponent& OwnerComp, AActor* Pawn)
{
	if (!Pawn)
	{
		return;
	}

	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	if (!BB)
	{
		return;
	}

	ABaseCharacter* AI = Cast<ABaseCharacter>(Pawn);
	if (!AI)
	{
		return;
	}

	const FName TargetKey = GetSelectedBlackboardKey();
	UObject* CurrentTarget = BB->GetValueAsObject(TargetKey);

	if (CurrentTarget)
	{
		// 通知 GameMode 释放锁定
		UWorld* World = Pawn->GetWorld();
		if (ARoomGameMode* GM = World ? World->GetAuthGameMode<ARoomGameMode>() : nullptr)
		{
			GM->ReleaseTarget(AI);
		}

		BB->SetValueAsObject(TargetKey, nullptr);
		BB->SetValueAsBool(FName(AIBlackboardKeyNames::bIsInCombat), false);
	}
}

bool UBTService_RefreshTarget::ShouldAbandonTarget(UBehaviorTreeComponent& OwnerComp,
	AActor* Pawn, UBlackboardComponent* BB, const FName TargetKey) const
{
	UObject* CurrentTarget = BB->GetValueAsObject(TargetKey);
	if (!CurrentTarget)
	{
		return true; // 没有目标
	}

	ABaseCharacter* TargetChar = Cast<ABaseCharacter>(CurrentTarget);
	if (!TargetChar)
	{
		return true; // 不是角色
	}

	// 目标死亡 → 放弃
	if (TargetChar->IsDead())
	{
		return true;
	}

	// 超出最大追击距离 → 放弃
	// 从 Controller.CurrentProfile.HuntPolicy.MaxChaseDistance 读取
	if (ABaseCharacter* AIChar = Cast<ABaseCharacter>(Pawn))
	{
		if (ABaseAIController* BaseAIC = Cast<ABaseAIController>(AIChar->GetController()))
		{
			if (UAIProfileAsset* Profile = BaseAIC->GetCurrentProfile())
			{
				const float MaxDist = Profile->HuntPolicy.MaxChaseDistance;
				if (MaxDist > 0.f)
				{
					const float Dist = FVector::Dist(
						TargetChar->GetActorLocation(), Pawn->GetActorLocation());
					if (Dist > MaxDist)
					{
						return true;
					}
				}
			}
		}
	}

	return false;
}

UObject* UBTService_RefreshTarget::GetValidTargetOrNull(UBehaviorTreeComponent& OwnerComp,
	AActor* Pawn, UBlackboardComponent* BB, const FName TargetKey) const
{
	UObject* Obj = BB->GetValueAsObject(TargetKey);
	if (!Obj) return nullptr;

	if (ABaseCharacter* Char = Cast<ABaseCharacter>(Obj))
	{
		if (Char->IsDead()) return nullptr;
		return Char;
	}
	return nullptr;
}
