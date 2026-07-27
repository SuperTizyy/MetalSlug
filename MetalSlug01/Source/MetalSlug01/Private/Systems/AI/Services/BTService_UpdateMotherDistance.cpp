// Copyright (c) 2026.
//
// 【v110 2026.07.30 生化模式 AI】BTService — 更新人类与母体距离实现
//
// 职责:
//   - 读取 BB.NearestMotherTarget (母体目标)
//   - 计算 AI 与母体的平面距离 (忽略 Z 轴)
//   - 写入 BB.DistanceToMother (Float)
//
// 大厂原则:
//   - 只派生事实 (计算距离), 不做决策 (决策归 Decorator)
//   - 周期由 Service 自己的 Interval 控制 (UE 编辑器暴露给策划)

#include "Systems/AI/Services/BTService_UpdateMotherDistance.h"

#include "AIController.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "GameFramework/Pawn.h"

#include "Characters/BaseCharacter.h"
#include "Systems/AI/AIBehaviorTypes.h"

UBTService_UpdateMotherDistance::UBTService_UpdateMotherDistance()
{
	NodeName = TEXT("Update Mother Distance");

	// 默认间隔 0.2s — 策划可在 UE 编辑器调整
	Interval = 0.2f;
	RandomDeviation = 0.f;
	bTickIntervals = true;

	// BB Key 过滤器
	MotherTargetKey.AddObjectFilter(this,
		GET_MEMBER_NAME_CHECKED(UBTService_UpdateMotherDistance, MotherTargetKey),
		AActor::StaticClass());
	DistanceKey.AddFloatFilter(this,
		GET_MEMBER_NAME_CHECKED(UBTService_UpdateMotherDistance, DistanceKey));
}


FString UBTService_UpdateMotherDistance::GetStaticDescription() const
{
	return FString::Printf(
		TEXT("每 %.2fs 更新 BB.DistanceToMother:\n"
			 "- 读取 BB.NearestMotherTarget\n"
			 "- 计算 AI 与母体平面距离 (忽略 Z 轴)\n"
			 "- 写入 BB.DistanceToMother\n"
			 "【BT 为主】Service 只派生事实, 决策归 Decorator."),
		Interval);
}


float UBTService_UpdateMotherDistance::ComputeFlatDistanceToMother(
	const FVector& AILocation, const FVector& MotherLocation) const
{
	// 平面距离 (忽略 Z 轴) — 与 BTService_UpdateZombieTargets 同算法
	const float DX = AILocation.X - MotherLocation.X;
	const float DY = AILocation.Y - MotherLocation.Y;
	return FMath::Sqrt(DX * DX + DY * DY);
}


void UBTService_UpdateMotherDistance::TickNode(
	UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	Super::TickNode(OwnerComp, NodeMemory, DeltaSeconds);

	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	if (!BB)
	{
		UE_LOG(LogBehaviorTree, Error,
			TEXT("[BTService_UpdateMotherDistance] BB 为空, Service 不工作!"));
		return;
	}

	AAIController* AIC = OwnerComp.GetAIOwner();
	if (!AIC)
	{
		UE_LOG(LogBehaviorTree, Error,
			TEXT("[BTService_UpdateMotherDistance] AIC 为空, Service 不工作!"));
		return;
	}

	APawn* AIPawn = AIC->GetPawn();
	if (!AIPawn)
	{
		UE_LOG(LogBehaviorTree, Error,
			TEXT("[BTService_UpdateMotherDistance] %s: Pawn 为空, Service 不工作!"),
			*AIC->GetName());
		return;
	}

	// ──────────────────────────────────────────────
	// 1. 读取 BB.NearestMotherTarget
	// ──────────────────────────────────────────────
	UObject* MotherObj = BB->GetValueAsObject(MotherTargetKey.SelectedKeyName);
	AActor* MotherActor = Cast<AActor>(MotherObj);

	if (!MotherActor)
	{
		// 无母体目标 → 写入 -1 表示"无目标"
		BB->SetValueAsFloat(DistanceKey.SelectedKeyName, -1.f);

		UE_LOG(LogBehaviorTree, Verbose,
			TEXT("[BTService_UpdateMotherDistance] %s: NearestMotherTarget 为空, DistanceToMother=-1"),
			*AIC->GetName());
		return;
	}

	// ──────────────────────────────────────────────
	// 2. 计算平面距离
	// ──────────────────────────────────────────────
	const FVector AILoc = AIPawn->GetActorLocation();
	const FVector MotherLoc = MotherActor->GetActorLocation();
	const float Distance = ComputeFlatDistanceToMother(AILoc, MotherLoc);

	// ──────────────────────────────────────────────
	// 3. 写入 BB.DistanceToMother
	// ──────────────────────────────────────────────
	BB->SetValueAsFloat(DistanceKey.SelectedKeyName, Distance);

	UE_LOG(LogBehaviorTree, Verbose,
		TEXT("[BTService_UpdateMotherDistance] %s: DistanceToMother=%.0fcm (Mother=%s)"),
		*AIC->GetName(),
		Distance,
		*MotherActor->GetName());
}
