// Copyright (c) 2026.
//
// 【大厂架构 v40.10 2026.07.31】BTTask — MoveTo Facing Target
//
// 职责 (单一职责):
//   - 读取 BB.TargetActorKey (面向的目标) + BB.MoveLocationKey (移动目的地)
//   - 朝向目标, 走 MoveLocationKey 位置 (两个参数独立, 这是与 BTTask_MoveAwayFromTarget 的本质区别)
//   - 到达 AcceptanceRadius 后 Succeeded
//
// 与现有 Task 的边界 (大厂原则 - 零重复):
//   - BTTask_MoveAwayFromTarget: 退一步, 移动目的地 = AI 位置 + 远离方向 × 距离 (动态算)
//   - BTTask_CircleAroundTarget: 攻击后环绕, 移动目的地 = 几何 + NavMesh 投影
//   - BTTask_FaceTarget:         只朝向, 不移动
//   - BTTask_MoveToFacingTarget: 移动目的地由 BB 提供 (策划/BT 上游写入)
//
// 何时用:
//   - BT 上游 (Service / Task) 算好目标点写入 BB.MoveLocationKey (例如 RallyPoint, 阵位点, 包抄点)
//   - 想让 AI 走过去且过程中始终面向 BB.TargetActor (例如面对敌人时走入掩体)
//
// 何时不用:
//   - 想让 AI 朝向移动方向 (默认 MoveTo 行为) → 用 UE 原生 BTTask_MoveTo
//   - 只想让 AI 持续面朝目标不移动 → 用 BTTask_FaceTarget
//   - 想让 AI 远离目标后退 → 用 BTTask_MoveAwayFromTarget
//
// 大厂原则落地:
//   - 朝向机制: 复用 UAIFacingMoveHelper (单一真理源, 不重写 Save/Restore Movement 代码)
//   - 零兜底: TargetActor/MoveLocation 无效 → Log Error + Failed
//   - 抗抖动: 复用 v23.2 标准方案 (OrientRotationToMovement = false), 不会回头走

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BehaviorTree/BehaviorTreeTypes.h"
#include "Systems/AI/AIFacingMoveHelper.h" // v40.10: 复用工具
#include "BTTask_MoveToFacingTarget.generated.h"

class AAIController;

UCLASS(Blueprintable, meta = (DisplayName = "Move To Facing Target (边移动边面向目标)"))
class METALSLUG01_API UBTTask_MoveToFacingTarget : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_MoveToFacingTarget();

	virtual FString GetStaticDescription() const override;

	/**
	 * BB Key 选择器 — 面向的目标 Actor (OBject 类型, 派生 AActor).
	 * 移动过程中 AI 持续朝向此目标.
	 */
	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector TargetActorKey;

	/**
	 * BB Key 选择器 — 移动目的地 (双类型支持).
	 *
	 * 支持两种 BB Key 类型, 运行时按 Key 实际类型分支处理:
	 *   - Vector  Key: 直接读 BB.Key.GetValueAsVector() 作为目的地 (例如 RallyPoint, TacticalPosition)
	 *   - Object Key (AActor 派生): 读 BB.Key.GetValueAsObject() 然后取 Actor->GetActorLocation()
	 *                                (典型用法: 直接用 TargetActor 作为目的地, AI 走过去且过程中面朝目标)
	 *
	 * 不需要 AddVectorFilter / AddObjectFilter 二选一 — UE 编辑器会把两种类型都列出来,
	 * 策划选哪个就按哪个类型处理. 这是大厂原则 (数据驱动, 不强制单一类型).
	 *
	 * 注意: Object 类型时取的是 Actor 的当前位置 (GetActorLocation), 不是 BoundCenter.
	 *       如果需要更精确的对准 (例如目标在 Character 身上), 自行用 Vector Key + 上游 Service 写入.
	 */
	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector MoveLocationKey;

	/**
	 * 到达半径 (cm) — AI 距离 MoveLocationKey 小于此值时视为到达 → Succeeded.
	 * 默认 50cm, 与 UE 原生 MoveTo 节点默认值一致.
	 */
	UPROPERTY(EditAnywhere, Category = "Config",
		meta = (ClampMin = "1.0", ClampMax = "500.0"))
	float AcceptanceRadius = 50.f;

	/**
	 * 被阻挡超时 (秒) — MoveTo 进入 Waiting (NavLink / 障碍等待) 超过此时间 → 强制 Succeeded.
	 * 默认 2s, 与 BTTask_MoveAwayFromTarget 一致.
	 */
	UPROPERTY(EditAnywhere, Category = "Config",
		meta = (ClampMin = "0.5", ClampMax = "10.0"))
	float MaxWaitTime = 2.f;

protected:
	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp,
		uint8* NodeMemory) override;

	virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory,
		float DeltaSeconds) override;

	virtual EBTNodeResult::Type AbortTask(UBehaviorTreeComponent& OwnerComp,
		uint8* NodeMemory) override;

	virtual uint16 GetInstanceMemorySize() const override
	{
		return sizeof(FTaskMemory);
	}

private:
	struct FTaskMemory
	{
		bool bMoveStarted = false;
		float WaitTime = 0.f;

		// v40.10: 朝向快照 (由 UAIFacingMoveHelper 写入, 任务结束/Abort 时清空)
		FMovementOrientationSnapshot FacingSnapshot;
	};

	FORCEINLINE FTaskMemory& GetTaskMemory(uint8* NodeMemory) const
	{
		return *reinterpret_cast<FTaskMemory*>(NodeMemory);
	}

	/**
	 * UE BB Key 存在性 + SelectedKeyName 双重检查 (大厂 v40.8 防御).
	 * 单一真理源: 这套检查模式被 BTTask_FindRandomLocation / BTTask_CircleAroundTarget 共用.
	 * @return true = 全部通过, false = 失败 (内部已 Log Error)
	 */
	bool ValidateBlackboardKeys(
		const UBlackboardComponent& BB,
		const AAIController& AIC,
		EBTNodeResult::Type& OutResult) const;

	bool StartMoveTo(UBehaviorTreeComponent& OwnerComp, const FVector& Dest);
	void CheckArrival(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory);
};
