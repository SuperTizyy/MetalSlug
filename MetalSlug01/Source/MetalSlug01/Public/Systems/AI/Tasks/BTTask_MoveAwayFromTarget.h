// Copyright (c) 2026.
//
// 【P0 v23.2 BT 原子库 + v40.10 重构】BTTask — 退一步 (面朝敌人后退)
//
// v23.2 标准实现 (回头走修复):
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
//
// v40.10 大厂重构:
//   Save/Restore Movement 配置抽离到 UAIFacingMoveHelper 公共 API
//   - FTaskMemory 不再持有 Movement 字段, 持有 FMovementOrientationSnapshot (Helper 数据)
//   - Configure/Restore 由 Helper 负责 (单一真理源)

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BehaviorTree/BehaviorTreeTypes.h"
#include "Systems/AI/AIFacingMoveHelper.h" // v40.10: 复用工具
#include "BTTask_MoveAwayFromTarget.generated.h"

class AAIController;

UCLASS(Blueprintable, meta = (DisplayName = "Move Away From Target (退一步)"))
class METALSLUG01_API UBTTask_MoveAwayFromTarget : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_MoveAwayFromTarget();

	/** @brief BT 编辑器静态描述 (显示 StepDistance + AcceptanceRadius + MaxWaitTime) */
	virtual FString GetStaticDescription() const override;

	/**
	 * 退步距离 (cm) — 默认 5cm, 刚好脱离 BTDecorator_TooClose (D <= AR) 决策
	 *
	 * v246.2 移除 ClampMax (用户反馈: 不能设置最大限度)
	 *   - 旧版 ClampMax = 100cm, 但某些关卡 (大场地 Boss 战) AI 需要退更远
	 *   - 仅保留下限 1cm (避免 0 / 负数导致 MoveTo 无方向)
	 *   - 设计师可按场景自由设置 (50/100/300/500cm 均可)
	 */
	UPROPERTY(EditAnywhere, Category = "Config",
		meta = (ClampMin = "1.0"))
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

	/** @brief Tick: 异步等 MoveTo 到达 / 超时, 满足条件恢复 Movement 设置并 Succeeded */
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

		// v40.10: 朝向快照 (由 UAIFacingMoveHelper::ConfigureFacingMove 写入, RestoreFacingMove 清空)
		FMovementOrientationSnapshot FacingSnapshot;
	};

	FORCEINLINE FTaskMemory& GetTaskMemory(uint8* NodeMemory) const
	{
		return *reinterpret_cast<FTaskMemory*>(NodeMemory);
	}

	FVector ComputeStepBackLocation(APawn* AIPawn, AActor* TargetActor) const;
	/** @brief 启动后退 MoveTo (异步): 朝向 SnapToTarget + MoveToLocation(RetreatPoint) */
	bool StartMoveTo(UBehaviorTreeComponent& OwnerComp, const FVector& Dest);
	/** @brief Tick 检查: 到达 AcceptanceRadius 或超 MaxWaitTime → 恢复 Movement 设置 + FinishLatentTask */
	void CheckArrival(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory);
};
