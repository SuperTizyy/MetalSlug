// 版权声明: 在项目设置的描述页面填写您的版权信息。
//
// 文件: BTTask_FaceTarget.h
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

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTTask_FaceTarget.generated.h"

/**
 * UBTTask_FaceTarget — 行为树原子 Task
 *
 * 功能: 让 AI 持续面朝目标 (BB.TargetActor)
 *
 * 使用方式 (BT 编辑器):
 *   Sequence "Attack"
 *   ├─ BTTask_FaceTarget  (面向目标)
 *   ├─ BTTask_PlayAttackMontage
 *   └─ BTTask_WaitMontageFinish
 *
 * 设计说明:
 *   - 面朝是"持续状态"，返回 InProgress，由 AbortTask 清理
 *   - TargetActor 为空 → Log Error + Failed
 */
UCLASS()
class METALSLUG01_API UBTTask_FaceTarget : public UBTTaskNode
{
	GENERATED_BODY()

public:
	// 构造函数
	UBTTask_FaceTarget();

	// 静态描述
	virtual FString GetStaticDescription() const override;

protected:
	// 执行任务
	virtual EBTNodeResult::Type ExecuteTask(
		UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;

	// 任务中止回调
	virtual EBTNodeResult::Type AbortTask(
		UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;

	// BB Key 选择器 — 面朝的目标
	UPROPERTY(EditAnywhere, Category = "Blackboard")
	struct FBlackboardKeySelector TargetActorKey;
};
