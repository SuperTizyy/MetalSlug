// Copyright (c) 2026.
//
// 【v40.9.1 2026.07.14】BTTask — 攻击后环绕原子能力 (Pick Circle Point)
//
// 【关键修复 — 用数学算点, 不依赖 NavMesh】
//   旧实现 (v40.9): 调 NavMesh.GetRandomReachablePointInRadius (Target, StrafeRadius)
//     - 失败根因 1: 地图没 NavMeshBoundsVolume (Session1.log line 109/117 显式报错)
//     - 失败根因 2: GetRandomReachablePointInRadius 在 TargetPosition 半径内有 voxel 才返回 true
//     - 失败根因 3: TargetActor.GetActorLocation() 可能返回 UE BB uninitialized sentinel
//                   (这次会话看到 TargetActor.Z=1389.335 — 不是真实坐标)
//
//   新实现 (v40.9.1):
//     1. 不调 NavMesh! 用纯数学: Point = PawnPos + RandomDirection × StrafeRadius
//     2. UE 原生 MoveTo 自己处理 navmesh (你的 BT 里下一个节点)
//        - 如果选出来点不可达 → MoveTo 自动 Failed → Selector 退到 Chase → AI 重追
//        - 大厂原则: 数据生成与导航寻径分离 — 这是 Unreal 官方倡导架构
//     3. 检测 TargetActor 失效 (nullptr / sentinel Z) → 拒绝静默写 (0,0,0)
//        → 改为 Log Warning + Failed, 让 BT Selector 兜底回 Chase
//
// 职责 (v40.9.1 保持不变):
//   - 读 BB.TargetActor
//   - 选自己 Pawn 周围 360° 随机方向 + StrafeRadius 距离的点
//   - 写入 BB.CirclePoint
//   - 同步返回 Succeeded
//
// 大厂原则:
//   - BT 负责"何时环绕" (Sequence 内), C++ 提供原子能力 (数学算点)
//   - UE 没有"绕自己选 360° 点"的原生节点, 必须自建
//   - 单一真理源: StrafeRadius 来自 ConfigSO.Combat
//   - 零兜底: 任何环节失败 → Failed, 不静默用默认值

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BehaviorTree/BehaviorTreeTypes.h"
#include "BTTask_PickCirclePoint.generated.h"

/**
 * UBTTask_PickCirclePoint
 * 在 Pawn 自身周围以 StrafeRadius (ConfigSO.Combat) 为半径随机选点, 写入 BB.CirclePoint.
 * 不依赖 NavMesh — 这是与 v40.9 的关键区别.
 */
UCLASS(Blueprintable, meta = (DisplayName = "Pick Circle Point (攻击后环绕)"))
class METALSLUG01_API UBTTask_PickCirclePoint : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_PickCirclePoint();

	virtual FString GetStaticDescription() const override;

	/**
	 * 目标 BB Key (Object) — 仅用于诊断日志 (确认我们确实有目标)
	 * 真正环绕的"中心"是 Pawn 自己位置 (不是 Target)
	 *
	 * 设计: AI 是绕自己挥刀走的, 不是绕目标走 — 这是 v40.9.1 重构的关键认知
	 *       (人类格斗也是"绕到对手侧翼"= 绕自己, 不是绕对方)
	 */
	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector TargetActorKey;

	/**
	 * 环绕点 BB Key (Vector) — 输出目标点
	 * 下一个节点 (Move To BB Key=CirclePoint) 绑这个 Key
	 */
	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector CirclePointKey;

	/**
	 * 【新增 v40.9.1】是否绕 Pawn 自己 (true) 还是绕 TargetActor (false)
	 *
	 * 设计取舍:
	 *   - true (默认, 推荐): 绕 Pawn 自己 → AI 走 L 形迂回, 拟人感强
	 *   - false: 绕 Target → 在对手周围画弧, 但 InstaMoveTo 通常僵直, 看着生硬
	 *
	 * 大厂设计: 默认值根据业务最优选 (绕自己更拟人), 用户可切换测试
	 */
	UPROPERTY(EditAnywhere, Category = "Behavior")
	bool bOrbitSelf = true;

protected:
	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
};
