// Copyright (c) 2026.
//
// 【P0 BT 原子库】BTTask — 死亡 (终结节点)
// 架构定位: BT 决策, C++ 原子能力
//   - 此节点是"决策哨兵" — 进入即代表 BT 决定该死
//   - 实际死亡由 ABaseCharacter::Die 自动完成 (HealthComponent 链)
//   - 我们不负责计时, 不负责兜底, 不负责超时 — BT 决策 = 进, 完成 = Succeeded
//   - Selector 选下一个 Sequence / Sequence 失败 = BT 框架自己处理

#include "Systems/AI/Tasks/BTTask_PlayDeath.h"

#include "BehaviorTree/BehaviorTreeComponent.h"
#include "AIController.h"
#include "GameFramework/Pawn.h"

#include "Characters/BaseCharacter.h"

UBTTask_PlayDeath::UBTTask_PlayDeath()
{
	NodeName = TEXT("Play Death");
}

FString UBTTask_PlayDeath::GetStaticDescription() const
{
	return TEXT("终末节点。\n"
		"实际死亡流程由 ABaseCharacter::Die → UnPossess → Destroy 自动完成。\n"
		"被 Decorator_HPThreshold (HP<0.01) 触发。");
}

EBTNodeResult::Type UBTTask_PlayDeath::ExecuteTask(
	UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	AAIController* AIC = OwnerComp.GetAIOwner();
	APawn* AIPawn = AIC ? AIC->GetPawn() : nullptr;
	ABaseCharacter* AIChar = Cast<ABaseCharacter>(AIPawn);

	if (!AIChar)
	{
		return EBTNodeResult::Failed;
	}

	// 上游 Decorator_HPThreshold 已判 HP<0.01
	//   死亡真伪由 ABaseCharacter::Die / HealthComponent 链决定,
	//   BT 节点不做重复触发 (会双 Die)。
	//   BT 走到这里 = Selector 决策 = Succeeded, 真正的销毁由 HealthComponent 自动执行
	return EBTNodeResult::Succeeded;
}
