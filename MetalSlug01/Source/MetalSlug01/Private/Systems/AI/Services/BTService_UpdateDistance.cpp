// Copyright (c) 2026.
//
// 【P0 v22 BT 原子库】BTService — 距离观察 (派生原始事实)
//
// 架构定位:
//   - 本 Service 只派生世界事实 (DistanceToTarget + bHasTarget + AttackRange)
//   - 决策全部交给 BTDecorator_C++ 决策节点

#include "Systems/AI/Services/BTService_UpdateDistance.h"

#include "BehaviorTree/BehaviorTreeComponent.h"
#include "AIController.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "GameFramework/Pawn.h"

#include "Data/AI/AIBehaviorConfigSO.h"
#include "Systems/AI/AIRuntimeConfigComponent.h"
#include "Systems/BaseAIController.h"

UBTService_UpdateDistance::UBTService_UpdateDistance()
{
	NodeName = TEXT("Update Distance");

	// 派生量周期: 0.1s = 10Hz
	Interval = 0.1f;
	RandomDeviation = 0.f;
	bTickIntervals = true;

	// 配置风格一致: 全部走 FBlackboardKeySelector
	TargetKey.AddObjectFilter(this,
		GET_MEMBER_NAME_CHECKED(UBTService_UpdateDistance, TargetKey),
		AActor::StaticClass());

	DistanceKey.AddFloatFilter(this,
		GET_MEMBER_NAME_CHECKED(UBTService_UpdateDistance, DistanceKey));

	HasTargetKey.AddBoolFilter(this,
		GET_MEMBER_NAME_CHECKED(UBTService_UpdateDistance, HasTargetKey));

	AttackRangeKey.AddFloatFilter(this,
		GET_MEMBER_NAME_CHECKED(UBTService_UpdateDistance, AttackRangeKey));
}

/**
 * @brief 生成 BT 节点描述 — 展示距离/有目标/攻击范围派生
 * @return 多行描述,展示 Interval 频率与 3 个 BB Key 派生语义
 */
FString UBTService_UpdateDistance::GetStaticDescription() const
{
	return FString::Printf(
		TEXT("每 %.2fs 派生 BB:\n"
			 "- DistanceToTarget (cm)\n"
			 "- bHasTarget\n"
			 "- AttackRange (cm, 派生 ConfigSO)"),
		Interval);
}

/**
 * @brief Service 周期 Tick — 每 0.1s 派生 BB.Distance / BB.bHasTarget / BB.AttackRange
 * @param OwnerComp BT 组件引用
 * @param NodeMemory Service 节点内存(本类未使用)
 * @param DeltaSeconds 距上次 Tick 的间隔秒
 *
 * 大厂架构定位:本 Service 只派生世界事实,不参与决策(决策交给 Decorator).
 * 三组 BB Key:TargetActor(读) / DistanceToTarget + bHasTarget(派生) / AttackRange(派生 ConfigSO).
 * 零目标时 Distance=-1 / bHasTarget=false, BT 装饰器看到 -1 立即拒判.
 *
 * v40.5 P0 节点级可观测性:Verbose Log 默认隐藏;用户反馈"AI 静止"时开启即可看到决策数据.
 */
void UBTService_UpdateDistance::TickNode(
	UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	Super::TickNode(OwnerComp, NodeMemory, DeltaSeconds);

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

	APawn* AIPawn = AIC->GetPawn();
	if (!AIPawn)
	{
		BB->SetValueAsFloat(DistanceKey.SelectedKeyName, -1.f);
		BB->SetValueAsBool(HasTargetKey.SelectedKeyName, false);
		return;
	}

	// 读目标
	UObject* TargetObj = BB->GetValueAsObject(TargetKey.SelectedKeyName);
	AActor* TargetActor = Cast<AActor>(TargetObj);

	if (!TargetActor)
	{
		BB->SetValueAsFloat(DistanceKey.SelectedKeyName, -1.f);
		BB->SetValueAsBool(HasTargetKey.SelectedKeyName, false);

		// 【v40.5 P0 诊断】TargetActor 空, BT 无目标 — 这是 AI 静止的最常见根因
		//   (OnTargetDetected 只在感知系统 bSensed=true 时写, 丢失时清空)
		//   BTService_RefreshTarget (0.3s) 会重新扫描, 但如果 ScanForNearestEnemy 找不到, 这里一直 -1
		UE_LOG(LogTemp, Verbose,
			TEXT("[BTService_UpdateDistance] %s: TargetActor=空, BB.Distance=-1, bHasTarget=false. "
			     "等待 BTService_RefreshTarget 重新扫描. (这是 AI 静止的最常见根因)"),
			*AIC->GetName());
		return;
	}

	// 算距离
	float Distance = ABaseAIController::ComputeActorCenterDistance(AIPawn, TargetActor);

	BB->SetValueAsFloat(DistanceKey.SelectedKeyName, Distance);
	BB->SetValueAsBool(HasTargetKey.SelectedKeyName, true);

	// 派生 AttackRange (含难度缩放)
	if (AttackRangeKey.SelectedKeyName != NAME_None)
	{
		if (const ABaseAIController* BaseAIC = Cast<ABaseAIController>(AIC))
		{
			if (const UAIRuntimeConfigComponent* RuntimeConfig = BaseAIC->GetRuntimeConfig())
			{
				if (const UAIBehaviorConfigSO* Config = RuntimeConfig->GetConfig())
				{
					float AR = RuntimeConfig->GetScaledCombat().AttackRange;
					BB->SetValueAsFloat(AttackRangeKey.SelectedKeyName, AR);

					// 【v40.5 P0 诊断】距离更新 — 简化版 (Verbose, 不刷屏)
					UE_LOG(LogTemp, Verbose,
						TEXT("[BTService_UpdateDistance] %s: Target=%s D=%.0fcm AR=%.0fcm bHasTarget=1"),
						*AIC->GetName(), *TargetActor->GetName(), Distance, AR);
				}
			}
		}
	}
}
