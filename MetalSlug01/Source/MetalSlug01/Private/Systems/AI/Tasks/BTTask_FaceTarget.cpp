// 版权声明: 在项目设置的描述页面填写您的版权信息。
//
// 文件: BTTask_FaceTarget.cpp
// 作用: 行为树原子 Task — 让 AI 面朝当前目标 (BB.TargetActor)
//
// 创建日期: 2026-07-13
// 负责人: <架构师>
// 关联 Phase: Phase AI — BT 为主 C++ 为辅重构
//
// 职责:
//   - 读取 BB.TargetActor
//   - 用 AIController::SetFocus 让 AI 持续朝向目标
//   - 不切 Chase/Attack/Retreat，只负责"面朝"
//
// 大厂原则:
//   - 单一职责: 只负责"面朝"，不负责移动/攻击/距离判定
//   - 零兜底: TargetActor 为空 → Log Error + Failed，不允许静默成功

#include "Systems/AI/Tasks/BTTask_FaceTarget.h"
#include "AIController.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"

UBTTask_FaceTarget::UBTTask_FaceTarget()
{
	NodeName = TEXT("Face Target");

	// 注册 BB Key 选择器
	TargetActorKey.AddObjectFilter(this,
		GET_MEMBER_NAME_CHECKED(UBTTask_FaceTarget, TargetActorKey),
		AActor::StaticClass());
}

/**
 * @brief 生成 BT 节点描述 — 展示原子化"面朝目标"语义与中止清理
 * @return 多行描述,展示 InProgress 返回 + AbortTask 清焦点 + TargetActor 零兜底
 */
FString UBTTask_FaceTarget::GetStaticDescription() const
{
	return TEXT("【原子化】让 AI 持续面朝 TargetActor。\n"
		"返回 InProgress，AbortTask 清理焦点。\n"
		"TargetActor 为空 → Log Error + Failed。");
}

EBTNodeResult::Type UBTTask_FaceTarget::ExecuteTask(
	UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	// 防御 1: Controller 必须有效
	AAIController* AIC = OwnerComp.GetAIOwner();
	if (!AIC)
	{
		UE_LOG(LogBehaviorTree, Error, TEXT("[BTTask_FaceTarget] Controller 无效"));
		return EBTNodeResult::Failed;
	}

	// 防御 2: BB 必须有效
	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	if (!BB)
	{
		UE_LOG(LogBehaviorTree, Error, TEXT("[BTTask_FaceTarget] Blackboard 无效"));
		return EBTNodeResult::Failed;
	}

	// 防御 3: TargetActor 必须有效
	UObject* TargetObj = BB->GetValueAsObject(TargetActorKey.SelectedKeyName);
	AActor* TargetActor = Cast<AActor>(TargetObj);
	if (!TargetActor || TargetActor->IsActorBeingDestroyed())
	{
		UE_LOG(LogBehaviorTree, Error,
			TEXT("[BTTask_FaceTarget] TargetActor 为空，无法 FaceTarget"));
		return EBTNodeResult::Failed;
	}

	// 用 AIController::SetFocus 持续朝向目标 (Gameplay 优先级最高)
	AIC->SetFocus(TargetActor, EAIFocusPriority::Gameplay);

	// 面朝是"持续状态"，返回 InProgress，由 AbortTask 清理
	return EBTNodeResult::InProgress;
}

EBTNodeResult::Type UBTTask_FaceTarget::AbortTask(
	UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	if (AAIController* AIC = OwnerComp.GetAIOwner())
	{
		// 清理焦点，避免残留
		AIC->ClearFocus(EAIFocusPriority::Gameplay);
	}

	return EBTNodeResult::Aborted;
}
