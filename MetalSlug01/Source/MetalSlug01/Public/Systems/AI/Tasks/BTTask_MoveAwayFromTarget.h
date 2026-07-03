// Copyright (c) 2026.
//
// 【P0 v23.2 BT 原子库】BTTask — 退一步 (面朝敌人后退)
//
// v23.2 修复 "回头走" 根因:
//   v23 之前 MoveTo 期间 AI 朝向 = 移动方向 = 朝 RetreatPoint
//   → 看起来 AI 背对玩家走 ("回头走")
//
// v23.2 标准实现:
//   ExecuteTask:
//     1. 保存 Pawn 原 Movement 设置 (OrientRotationToMovement / UseControllerDesiredRotation)
//     2. 临时:
//        - OrientRotationToMovement = false (不让 MoveTo 抢 AI 朝向)
//        - UseControllerDesiredRotation = true (让 Controller 控制朝向)
//     3. AIC->SetFocus(TargetActor, EAIFocusPriority::Gameplay)
//        (Gameplay 优先级最高, 压过 MoveTo 默认 MoveFocus, AI 必定朝 Target)
//     4. MoveToLocation(RetreatPoint)
//   TickTask 完成/Abort:
//     1. ClearFocus(EAIFocusPriority::Gameplay)
//     2. 恢复原 Movement 设置
//   退出条件:
//     - MoveTo 到达 AcceptanceRadius → Succeeded (恢复设置)
//     - MoveTo Waiting 超 MaxWaitTime → 强制 Succeeded (恢复设置)
//     - AbortTask → Aborted (恢复设置)
//
// "面朝敌人后退" 的标準做法 (Uncharted / Last of Us 同款):
//   - 朝向: FocalPoint 控制, 不受 MoveTo 干扰
//   - 移动: MoveToLocation 走 StepBackLoc (远离玩家的方向)
//   - 结果: AI 面朝玩家, 身体后退

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BehaviorTree/BehaviorTreeTypes.h"
#include "BTTask_MoveAwayFromTarget.generated.h"

class AAIController;

UCLASS(Blueprintable, meta = (DisplayName = "Move Away From Target (退一步)"))
class METALSLUG01_API UBTTask_MoveAwayFromTarget : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_MoveAwayFromTarget();

	virtual FString GetStaticDescription() const override;

	/**
	 * 退步距离 (cm) — 默认 5cm, 刚好脱离 BTDecorator_TooClose (D <= AR) 决策
	 */
	UPROPERTY(EditAnywhere, Category = "Config",
		meta = (ClampMin = "1.0", ClampMax = "100.0"))
	float StepDistance = 5.f;

	UPROPERTY(EditAnywhere, Category = "Config",
		meta = (ClampMin = "1.0", ClampMax = "50.0"))
	float AcceptanceRadius = 5.f;

	UPROPERTY(EditAnywhere, Category = "Config",
		meta = (ClampMin = "0.5", ClampMax = "5.0"))
	float MaxWaitTime = 2.f;

	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector TargetKey;

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

		// v23.2: 保存 Pawn 原 Movement 设置 (ExecuteTask 时存, 完成/Abort 时恢复)
		bool bMovementSettingsSaved = false;
		bool bSavedOrientRotationToMovement = false;
		bool bSavedUseControllerDesiredRotation = false;
	};

	FORCEINLINE FTaskMemory& GetTaskMemory(uint8* NodeMemory) const
	{
		return *reinterpret_cast<FTaskMemory*>(NodeMemory);
	}

	FVector ComputeStepBackLocation(APawn* AIPawn, AActor* TargetActor) const;
	bool StartMoveTo(UBehaviorTreeComponent& OwnerComp, const FVector& Dest);
	void CheckArrival(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory);

	// v23.2: 恢复 Pawn 原 Movement 设置
	void RestoreMovementSettings(FTaskMemory& Mem, AAIController* AIC) const;
};
