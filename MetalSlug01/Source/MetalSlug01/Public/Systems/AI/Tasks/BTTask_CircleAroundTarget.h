// Copyright (c) 2026.
//
// 【v40.9.3 2026.07.14】BTTask — 攻击后环绕 (一体化版, 自包含)
//
// 【关键设计】
//   旧设计 (v40.9-v40.9.2): 拆 3 个 Task — Pick / MoveTo / WaitPause
//     问题: 你 BT 编辑器里必须挂 3 个节点 + 每个 Details 都要配 Key → 容易漏挂
//     症状: AI 原地攻击不动
//
//   新设计 (v40.9.3): 一个 Task 自包含完成 pick → MoveTo → Wait
//     优点:
//       - BT 里只需挂 1 个节点, Name="Circle Around Target (一体化)"
//       - 不需要 BB.CirclePoint Key (不需要 BB 改资源)
//       - 不需要 Move To 节点 (C++ 内调 AIC->MoveToLocation)
//       - 大厂原则: "高内聚, 低耦合" — 一个 task 完成一件事(环绕)
//
//   唯一依赖:
//     - BB.TargetActor (你 BT 里已经在用, 没新增)
//     - ConfigSO.Combat.StrafeRadius / CirclePauseSeconds
//
// 大厂原则:
//   - BT 负责决策"何时环绕" (Sequence 内)
//   - C++ 自包含原子能力 — pick + move + wait 一体化, 不依赖外部 BB / 节点编排
//   - 零兜底: TargetActor 空/ConfigSO 空 → Failed (Selector 兜底回 Chase)
//   - 抗抖动: UE MoveToLocation 自带 navmesh 检测, 失败自动回退到当前位置

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BehaviorTree/BehaviorTreeTypes.h"
#include "BTTask_CircleAroundTarget.generated.h"

/**
 * UBTTask_CircleAroundTarget
 * 一体化环绕 Task: 选点 → MoveTo → 短暂停顿, 全部在内部完成.
 * 只需要 BB.TargetActor (AttackSequence 里已经在用).
 */
UCLASS(Blueprintable, meta = (DisplayName = "Circle Around Target (一体化攻击后环绕)"))
class METALSLUG01_API UBTTask_CircleAroundTarget : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_CircleAroundTarget();

	/** @brief BT 编辑器静态描述 (显示 OrbitAngleDegrees + 停顿秒数) */
	virtual FString GetStaticDescription() const override;

	/**
	 * 目标 BB Key (Object) — 想要绕的目标
	 * 攻击目标 / 锁定目标都填这个 — 通常 = BB.TargetActor
	 */
	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector TargetActorKey;

	/**
	 * 【v40.9.4 重构 — 语义变更】环绕角 (角度制), 在 [0, 180] 范围内.
	 *
	 * 旧 (v40.9.3 误名): MaxStrafeRadius — 字面是半径, 实际是半径上限
	 *   旧代码用它当"距离值"绕 Pawn 自己, 所以设 300 走不出 300 — 错位语义
	 *
	 * 新 (v40.9.4): 真正的"绕目标角度" — 控制 AI 走到的位置相对 (Pawn→Target) 方向偏转多少度
	 *   - 0°   = AI 仍停在原地 (绕 0°, 不动)
	 *   - 45°  = AI 走到目标的侧翼 (左或右 45°)
	 *   - 90°  = AI 走到目标的正侧方
	 *   - 135° = AI 走到目标的"肩膀外"
	 *   - 180° = AI 绕到目标背后 (背刺位)
	 *
	 * 随机方向: 50% 概率走"左侧", 50% 概率走"右侧", 但角度值不变 (45 就是 45)
	 *
	 * 为什么大厂要这样做:
	 *   - 策划语义明确 — 看到 180 就知道是"背后"
	 *   - 距离自然保持 (半径 = Pawn→Target 实时距离, 不变)
	 *   - 不会被"AI 走太远"导致绕一大圈
	 */
	UPROPERTY(EditAnywhere, Category = "Behavior", meta = (ClampMin = "0.0", ClampMax = "180.0"))
	float OrbitAngleDegrees = 180.f;

	/**
	 * 【v40.9.4 新增】半价保护 — 移动后 AI 与目标距离允许的浮动范围 (cm).
	 * 距离算法: 移动后距离 ∈ [CurrentDist - Min, CurrentDist + Max]
	 * - CurrentDist = 移动前 AI 与目标距离
	 * - Min = 当前距离 × (1 - MinRadiusShrinkRatio) — 防止 AI 越走越近
	 * - Max = 当前距离 × (1 + MaxRadiusExpandRatio) — 防止 AI 走太远
	 *
	 * 默认 0.15 = 距离浮动 ±15%. 这保证 "打一下走一下" 但不会一直缩小/扩大包围圈.
	 */
	UPROPERTY(EditAnywhere, Category = "Behavior", meta = (ClampMin = "0.0", ClampMax = "0.5"))
	float MinRadiusShrinkRatio = 0.15f;

	UPROPERTY(EditAnywhere, Category = "Behavior", meta = (ClampMin = "0.0", ClampMax = "0.5"))
	float MaxRadiusExpandRatio = 0.15f;

	/**
	 * 停顿秒数. 默认 0.4s.
	 */
	UPROPERTY(EditAnywhere, Category = "Behavior", meta = (ClampMin = "0.0", ClampMax = "3.0"))
	float CirclePauseSeconds = 0.4f;

protected:
	/** Task 内部状态 */
	enum class EPhase : uint8
	{
		PickPoint,        // 选点
		MoveToPoint,      // MoveTo (异步)
		PauseAndFinish    // 停顿 (异步, 然后 Succeeded)
	};

	struct FCircleMemory
	{
		EPhase Phase = EPhase::PickPoint;
		FVector TargetPoint = FVector::ZeroVector;
		float ElapsedPause = 0.f;
		FAIRequestID MoveRequestID = FAIRequestID::InvalidRequest;
		FVector CachedPawnLocation = FVector::ZeroVector; // 用于验证 Mid-Move 时 AI 是否被卡住
		TWeakObjectPtr<AActor> TargetActorPtr; // 【v40.9.4】Tick 阶段面朝目标用
	};

	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual void TickTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;
	virtual EBTNodeResult::Type AbortTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
	virtual uint16 GetInstanceMemorySize() const override { return sizeof(FCircleMemory); }

private:
	// 大厂原则: 用 ConfigSO 而非蓝图配置默认值, 但允许 BT 编辑器覆盖 (策划可调)
	// 真实读取时: MaxStrafeRadius = (ConfigSO.StrafeRadius) 还是 (编辑器值)?
	// 我们用编辑器值优先 + ConfigSO fallback
};
