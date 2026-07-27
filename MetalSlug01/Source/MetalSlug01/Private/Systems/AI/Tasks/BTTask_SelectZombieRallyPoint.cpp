// Copyright (c) 2026.
//
// 【v107 2026.07.28 生化模式 AI】BTTask — 集合点选择与锁定实现

#include "Systems/AI/Tasks/BTTask_SelectZombieRallyPoint.h"

#include "AIController.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "GameFramework/Pawn.h"

#include "Characters/BaseCharacter.h"
#include "World/Objectives/ZombieRallyPoint.h"
#include "Systems/Zombie/RoomZombieRallySubsystem.h"

UBTTask_SelectZombieRallyPoint::UBTTask_SelectZombieRallyPoint()
{
	NodeName = TEXT("Select Zombie Rally Point");

	LockedRallyPointKey.AddObjectFilter(this, GET_MEMBER_NAME_CHECKED(UBTTask_SelectZombieRallyPoint, LockedRallyPointKey), AActor::StaticClass());
	bRallyPointLockedKey.AddBoolFilter(this, GET_MEMBER_NAME_CHECKED(UBTTask_SelectZombieRallyPoint, bRallyPointLockedKey));
	DistanceToRallyPointKey.AddFloatFilter(this, GET_MEMBER_NAME_CHECKED(UBTTask_SelectZombieRallyPoint, DistanceToRallyPointKey));
}


FString UBTTask_SelectZombieRallyPoint::GetStaticDescription() const
{
	const TCHAR* ModeStr = (SelectionMode == EZombieRallySelectionMode::Nearest)
		? TEXT("Nearest (最近集合点)")
		: TEXT("MostPopulated (人类最多集合点)");
	return FString::Printf(TEXT("选点策略=%s\n锁账本一局内只选一次, 失败 = Failed"), ModeStr);
}


EBTNodeResult::Type UBTTask_SelectZombieRallyPoint::ExecuteTask(
	UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	// 1. 基础验证
	AAIController* AIC = OwnerComp.GetAIOwner();
	if (!AIC)
	{
		UE_LOG(LogBehaviorTree, Error,
			TEXT("[BTTask_SelectZombieRallyPoint] AIController=null, 拒绝执行."));
		return EBTNodeResult::Failed;
	}

	ABaseCharacter* SelfPawn = Cast<ABaseCharacter>(AIC->GetPawn());
	if (!SelfPawn)
	{
		UE_LOG(LogBehaviorTree, Error,
			TEXT("[BTTask_SelectZombieRallyPoint] AIC='%s' Pawn=null, 拒绝执行."),
			*AIC->GetName());
		return EBTNodeResult::Failed;
	}

	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	if (!BB)
	{
		UE_LOG(LogBehaviorTree, Error,
			TEXT("[BTTask_SelectZombieRallyPoint] AIC='%s' BB=null, 拒绝执行."),
			*AIC->GetName());
		return EBTNodeResult::Failed;
	}

	// 2. 已锁点 → 不重新选 (一局内只选一次, 强制约定)
	if (BB->GetValueAsBool(bRallyPointLockedKey.SelectedKeyName))
	{
		UE_LOG(LogBehaviorTree, Verbose,
			TEXT("[BTTask_SelectZombieRallyPoint] AIC='%s' 已锁定, 跳过选点."),
			*AIC->GetName());
		return EBTNodeResult::Succeeded;
	}

	return PerformSelection(OwnerComp, SelfPawn, AIC);
}


EBTNodeResult::Type UBTTask_SelectZombieRallyPoint::PerformSelection(
	UBehaviorTreeComponent& OwnerComp, ABaseCharacter* SelfPawn, AController* AIC)
{
	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	URoomZombieRallySubsystem* RallySys = URoomZombieRallySubsystem::Get(this);

	if (!RallySys)
	{
		UE_LOG(LogBehaviorTree, Error,
			TEXT("[BTTask_SelectZombieRallyPoint] AIC='%s' RallySubsystem=null, 拒绝选点. "
				 "【修复】检查地图是否加载 URoomZombieRallySubsystem (默认 WorldSubsystem 自动生成)."),
			*AIC->GetName());
		return EBTNodeResult::Failed;
	}

	// 3. 选点 (按 SelectionMode 策略)
	AZombieRallyPoint* Selected = nullptr;
	switch (SelectionMode)
	{
	case EZombieRallySelectionMode::Nearest:
		Selected = RallySys->SelectRallyPoint_Nearest(SelfPawn);
		break;
	case EZombieRallySelectionMode::MostPopulated:
		Selected = RallySys->SelectRallyPoint_MostPopulated(SelfPawn);
		break;
	}

	if (!Selected)
	{
		// 【零兜底】无可用集合点 → Failed, BT 拒判, AI 不行动
		UE_LOG(LogBehaviorTree, Error,
			TEXT("[BTTask_SelectZombieRallyPoint] AIC='%s' 选点失败 (账本空). "
				 "【修复】在地图里放置至少一个 BP_ZombieRallyPoint 子类, PointID 唯一非空."),
			*AIC->GetName());
		return EBTNodeResult::Failed;
	}

	// 4. 锁定账本 (一局内只锁一次)
	const bool bLocked = RallySys->LockRallyPointForAI(AIC, Selected->PointID);
	if (!bLocked)
	{
		// LockRallyPointForAI 内部已 Log Error, 这里不再重复
		return EBTNodeResult::Failed;
	}

	// 5. 写 BB
	const float Dist = FVector::Dist(SelfPawn->GetActorLocation(), Selected->GetActorLocation());

	BB->SetValueAsObject(LockedRallyPointKey.SelectedKeyName, Selected);
	BB->SetValueAsBool(bRallyPointLockedKey.SelectedKeyName, true);
	BB->SetValueAsFloat(DistanceToRallyPointKey.SelectedKeyName, Dist);

	UE_LOG(LogBehaviorTree, Display,
		TEXT("[BTTask_SelectZombieRallyPoint] AIC='%s' 锁定 PointID='%s' 距离=%.0fcm."),
		*AIC->GetName(), *Selected->PointID, Dist);

	return EBTNodeResult::Succeeded;
}