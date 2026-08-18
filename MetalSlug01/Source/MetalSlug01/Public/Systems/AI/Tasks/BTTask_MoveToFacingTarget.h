// Copyright (c) 2026.
//
// 【大厂架构 v40.10 2026.07.31 + v40.12 2026.08.01】BTTask — MoveTo Facing Target
//
// 职责 (单一职责):
//   - 读取 BB.MoveLocationKey (移动目的地, 必填 — 决定 AI 走到哪)
//   - 读取 BB.TargetActorKey (面向的目标, 可选 — 决定 AI 移动期间是否面朝目标)
//   - 朝向目标 (如果有效), 走 MoveLocationKey 位置 (两个参数独立)
//   - 到达 AcceptanceRadius 后 Succeeded
//
// 【v40.12 2026.08.01 行为变更】TargetActor 可选 (v40.10~v40.11 是必填硬拒判)
//   业务背景 (用户 2026.08.01 反馈):
//     "Move To Facing Target 这个节点, 在 AI 进入游戏时就应该能让 AI 移动到 MoveLocationKey 设置的点位,
//      就算 TargetActorKey 为空也要运行."
//   旧行为: TargetActor 为空/失效 → Failed, AI 站着不动 → 违反业务期望
//   新行为: TargetActor 为空/失效 → Log Warning + 跳过朝向机制 + 继续 MoveToLocation
//   - 移动本身照常执行 (走到 MoveLocationKey 指定的 RallyPoint / 阵位点)
//   - 朝向 = 移动方向 (UE 原生 MoveTo 行为, OrientRotationToMovement=true 时默认)
//   - AI 进入游戏即可移动 ✅ (满足 Pre-Mutation 期间 BB.TargetActor 清空但 BB.RallyPoint 已写入的场景)
//
// 与现有 Task 的边界 (大厂原则 - 零重复):
//   - BTTask_MoveAwayFromTarget: 退一步, 移动目的地 = AI 位置 + 远离方向 × 距离 (动态算)
//   - BTTask_CircleAroundTarget: 攻击后环绕, 移动目的地 = 几何 + NavMesh 投影
//   - BTTask_FaceTarget:         只朝向, 不移动
//   - BTTask_MoveToFacingTarget: 移动目的地由 BB 提供 (策划/BT 上游写入), 朝向由 BB.TargetActor 可选决定
//
// 何时用:
//   - BT 上游 (Service / Task) 算好目标点写入 BB.MoveLocationKey (例如 RallyPoint, 阵位点, 包抄点)
//   - 移动过程中如果想面朝敌人 (有目标时) → 配 TargetActorKey
//   - 移动过程中不需要面朝任何人 (例如集合点) → TargetActorKey 可配但 BB 值允许为空
//
// 何时不用:
//   - 想让 AI 朝向移动方向 (默认 MoveTo 行为) → 用 UE 原生 BTTask_MoveTo
//   - 只想让 AI 持续面朝目标不移动 → 用 BTTask_FaceTarget
//   - 想让 AI 远离目标后退 → 用 BTTask_MoveAwayFromTarget
//
// 大厂原则落地:
//   - 朝向机制: 复用 UAIFacingMoveHelper (单一真理源, 不重写 Save/Restore Movement 代码)
//   - 零兜底: TargetActor 失效 → Log Warning + 跳过朝向 (不算兜底, 是合法的"无目标移动"分支)
//              MoveLocation 失效 → Log Error + Failed (真没救了, 必须告诉开发者)
//   - 抗抖动: TargetActor 有效时复用 v23.2 标准方案 (OrientRotationToMovement = false), 不会回头走
//   - 对称性: bFacingConfigured 守卫 Configure/Restore 配对, 没 Configure 就不 Restore (状态零污染)

// 【v126 2026.08.01】智能避障路径规划 (用户业务规则驱动)
//
// 业务需求 (用户 2026.08.01):
//   "Move To Facing Target 智能选择路径, 路径上有人就重新规划一条避开人的路径."
//
// 设计原则 (大厂 - 单一职责 + 配置驱动 + 抗抖动 5 层):
//   1. UE 原生 MoveTo (PathFollowingComponent) 已有 dynamic replan 能力 (NavLink/动态障碍)
//      - 但它依赖 NavModifier Component / Dynamic NavMesh, 不支持"任何 Pawn 都避"
//   2. v126 在启动 MoveTo 前**额外**检测一次: 算路径 → 检测与 EvadeActorClasses 相交 → 重 plan 绕路
//      - 启动后不每帧重算 (避免抖动 / 高 CPU)
//      - 启动后 UE 原生 Replan 兜底 (移动期间新出现障碍自动重 plan)
//   3. EvadeActorClasses 参数化 (用户决策):
//      - 默认: ABaseCharacter (避开所有活角色)
//      - 策划可加: BP_SWAT_C (避玩家) / BP_GruntAI (避同阵营 AI) / 自定义类
//      - 不区分 Faction — 大厂原则 (配置驱动 > 运行时分支)
//   4. 抗抖动 5 层:
//      - L1 比较钝化: 球-线段相交用 squared distance, 不用 sqrt
//      - L2 距离 EMA: 不需要 (一次性规划, 不是持续检测)
//      - L3 死区迟滞: 不需要 (路径检测是几何判定, 不是状态判定)
//      - L4 节流: MaxReplanAttempts = 3 限制重算次数 (CPU 保护)
//      - L5 兜底: 重 plan 仍失败 → Succeeded (让 BT 上层决策重选 MoveLocation)
//
// 已知限制 (大厂透明):
//   - 路径避障仅在启动 MoveTo 前一次性规划, 启动后新出现的障碍走 UE 原生 Replan (NavMesh Dynamic)
//   - 如果策划想要 "每帧重新规划", 那是 performance cliff, 不是 v126 业务目标
//
// 路径规划算法 (大厂 - 计算几何 + NavMesh):
//   1. UE 原生 FindPathSync 算初始路径 → 拿 Path->PathPoints
//   2. 遍历每条线段 (PathPoints[i] → PathPoints[i+1]) 与 EvadeActorClasses 求交
//   3. 求交用球-线段 closest point (Squared Dist ≤ EvadeRadius²)
//   4. 有相交 → 算 Reroute Point = 中点向左/右偏移 EvadeRadius + EvadeMargin
//   5. 重新算路径: Start → Reroute → Original Dest
//   6. 仍相交 → ReplanAttempts++ → 重试, 超 MaxReplanAttempts → 走 UE 原生 MoveTo (兜底)
//   7. ReplanState 写回 FTaskMemory 供调试

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BehaviorTree/BehaviorTreeTypes.h"
#include "Systems/AI/AIFacingMoveHelper.h" // v40.10: 复用工具
#include "BTTask_MoveToFacingTarget.generated.h"

class AAIController;
class ABaseCharacter;

UCLASS(Blueprintable, meta = (DisplayName = "Move To Facing Target (边移动边面向目标)"))
class METALSLUG01_API UBTTask_MoveToFacingTarget : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_MoveToFacingTarget();

	/** @brief BT 编辑器静态描述 (显示 AcceptanceRadius + MaxWaitTime + 智能避障参数) */
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

	/**
	 * 【v126 2026.08.01】智能避障 — 避开这些 Actor 类别
	 *
	 * 业务背景 (用户 2026.08.01):
	 *   "路径上有人就重新规划一条避开人的路径."
	 *   "给我参数让我来选择具体避开什么类型角色."
	 *
	 * 默认 ABaseCharacter (避开所有活角色 — 玩家 + AI)
	 * 策划可在 BT 编辑器 Details 面板按需调整:
	 *   - 加 BP_SWAT_C: 只避玩家 (AI 之间相互穿过可接受)
	 *   - 加 BP_GruntAI: 只避同阵营 AI
	 *   - 全清空: 关闭避障, 等同 v40.12 行为
	 *
	 * 大厂原则:
	 *   - 配置驱动 > 运行时分支 (用户决策)
	 *   - 默认值兜底最严: 全部避开 (避免与任何活角色重叠)
	 */
	UPROPERTY(EditAnywhere, Category = "Config|Evade",
		meta = (DisplayName = "Evade Actor Classes (避开这些类别的 Actor)"))
	TArray<TSubclassOf<AActor>> EvadeActorClasses;

	/**
	 * 【v126 2026.08.01】智能避障 — 检测半径 (cm)
	 *
	 * 路径段 (从 AI 到 MoveLocationKey) 与 EvadeActorClasses Actor 中心距离 ≤ 此值时视为"路径上有人".
	 * 默认 80cm — 与 Character 胶囊体半径 (40cm) × 2 一致 (留 0 余量)
	 *
	 * 太小: 规划路径仍可能擦肩而过
	 * 太大: 过度避障, AI 离玩家很远就绕路 (破坏战术配合)
	 * 推荐: 50~150cm
	 */
	UPROPERTY(EditAnywhere, Category = "Config|Evade",
		meta = (ClampMin = "20.0", ClampMax = "300.0",
		        DisplayName = "Evade Radius (避开检测半径 cm)"))
	float EvadeRadius = 80.f;

	/**
	 * 【v126 2026.08.01】智能避障 — 最多重规划次数
	 *
	 * 启动 MoveTo 前检测到路径被挡 → 重 plan 一次 (绕路) → 重 plan 仍被挡 → 再来
	 * 超过此次数 → 走 UE 原生 MoveTo (兜底, 让引擎处理) → Succeeded 让 BT 决策
	 *
	 * 默认 3 (【v127 2026.08.01 改回 3】由 v126.1 的 0 改回 3):
	 *   - v126.1 改 0 是临时为排查 "不走" bug — 已确认根因 (绕圈圈)
	 *   - v127 算法是反向绕,可解决绕圈圈 (用户验证)
	 *   - 默认 3 = 开箱即用智能避障
	 *
	 * 经验值: 大部分场景 1~2 次就足够
	 * 抗抖动: 限制 CPU 消耗, 防止极端拓扑下死循环
	 */
	UPROPERTY(EditAnywhere, Category = "Config|Evade",
		meta = (ClampMin = "0", ClampMax = "10",
		        DisplayName = "Max Replan Attempts (最多重规划次数, 0=关闭避障)"))
	int32 MaxReplanAttempts = 3;

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

		// v40.10: 朝向快照 (由 UAIFacingMoveHelper 写入, 任务结束/Abort 时清空)
		FMovementOrientationSnapshot FacingSnapshot;

		// 【v40.12 2026.08.01 新增】本次执行是否成功 ConfigureFacingMove (TargetActor 有效时才为 true).
		//   - true:  RestoreFacingMove 必须调用, 否则 Movement 配置泄漏
		//   - false: 跳过 RestoreFacingMove (大厂对称 — 没 Configure 就不 Restore, 避免误恢复别人设的)
		bool bFacingConfigured = false;

		// 【v126 2026.08.01 新增】智能避障调试信息 — 供后续日志 / HUD 显示
		//   - ReplanAttemptsUsed: 实际重规划次数 (0 = 直接通过, >0 = 触发了绕路)
		//   - bEvadeEnabled: EvadeActorClasses 非空 且 MaxReplanAttempts > 0
		bool bEvadeEnabled = false;
		int32 ReplanAttemptsUsed = 0;
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

	/** @brief 启动面向移动 (异步): 朝向 + 智能避障规划 + MoveToLocation(Dest) */
	bool StartMoveTo(UBehaviorTreeComponent& OwnerComp, const FVector& Dest);
	/** @brief Tick 检查: 到达 AcceptanceRadius 或超 MaxWaitTime → 恢复 Movement + FinishLatentTask */
	void CheckArrival(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory);

	/**
	 * 【v126 2026.08.01】智能避障 — 返回第一个挡路 Actor (供 Reroute Point 计算)
	 *
	 * 与 DetectActorsOnPath 同算法, 但返回 Actor 指针 (而非 bool)
	 * 实现用 const, 因为 TActorIterator 不改 this
	 *
	 * 大厂原则:
	 *   - 用 TActorIterator 而非 GetAllActorsOfClass (UE 5.6 标准, 性能优化)
	 *   - 球-线段相交用 SquaredDistance (避免 sqrt)
	 *
	 * @param IgnoreActor 必须排除的 Actor (大厂 v126.1 修复):
	 *   - 调用方传入 AIPawn (AI 自己) — 防止"AI 自己挡自己"的反模式
	 *   - 调用方还可传入 MoveLocation Actor (BB.Object Key 指向的目的地 Actor)
	 *     — 防止"目的地就是 EvadeActorClasses 内的 Actor"时的死循环
	 *   - nullptr = 不过滤 (调试用)
	 */
	AActor* DetectActorsOnPathImpl(
		const UWorld* World,
		const FVector& StartLoc,
		const FVector& EndLoc,
		TSubclassOf<AActor> OnlyClass,
		float CheckRadius,
		const AActor* IgnoreActor) const;

	/**
	 * 【v126 2026.08.01】智能避障 — 检测路径上是否有 EvadeActorClasses 中的 Actor
	 *
	 * @param IgnoreActor 必须排除的 Actor (大厂 v126.1): AI 自己 + MoveLocationActor
	 *
	 * @return true = 路径上有人 (需要重 plan)
	 *
	 * 大厂原则:
	 *   - 零兜底: Path 算不出 → Log Warning + return false (没人, 不重 plan)
	 *   - 性能: 球-线段相交是 O(Points × Actors), 一次性计算可接受
	 */
	bool DetectActorsOnPath(
		const UWorld* World,
		const FVector& StartLoc,
		const FVector& EndLoc,
		TSubclassOf<AActor> OnlyClass,
		float CheckRadius,
		const AActor* IgnoreActor) const;

	/**
	 * 【v127 2026.08.01】智能避障 — 计算 Reroute Point (反向绕障算法)
	 *
	 * v126 旧算法 (用户 2026.08.01 反馈 "绕圈圈"):
	 *   - Reroute = Actor 中心 + 固定侧向偏移 (Perpendicular of (Actor→AI))
	 *   - 行为: AI 围绕 Actor 绕圈 — 母体不动 → AI 永远在母体旁边转
	 *
	 * v127 新算法 (用户决策 — 智能反向绕):
	 *   1. BackDir = -ForwardDir (AI 想去方向的反方向)
	 *   2. Reroute = StartLoc + BackDir × BackDistance + Perpendicular × SideDistance
	 *   3. 算法意义: "AI 先向后退一点 + 侧偏 = 离开挡路 Actor 再绕过去"
	 *
	 * 参数:
	 *   StartLoc          — AI 当前位置
	 *   ActorLoc          — 挡路 Actor 中心
	 *   AvoidanceRadius   — 避开半径 (EvadeRadius)
	 *   OriginalDest      — 原始目的地 (用于算 ForwardDir / BackDir)
	 *   MinRerouteDistance — 最小 Reroute 距离 (距 StartLoc)
	 *
	 * @return Reroute Point (UE 原生 MoveTo 用这个点作为目标)
	 *
	 * 大厂原则 - 反向绕 vs 侧向绕:
	 *   - 侧向绕: 适合 AI 不依赖 Director 路径 (例如 FPS 喷射器躲避)
	 *   - 反向绕: 适合 AI 想"穿过"挡路 Actor 去某处 (典型: 集合点转移)
	 *   - 用户业务是后者 → 反向绕
	 */
	FVector ComputeReroutePoint(
		const FVector& StartLoc,
		const FVector& ActorLoc,
		float AvoidanceRadius,
		const FVector& OriginalDest,
		float MinRerouteDistance) const;

	/**
	 * 【v126 2026.08.01】智能避障 — 在 StartMoveTo 之前调用
	 *
	 * 流程:
	 *   1. UE FindPathSync 算初始路径
	 *   2. 检测路径 × EvadeActorClasses (循环, 每类都要测)
	 *   3. 有相交 → 算 Reroute → FindPathSync(Start → Reroute → End 串行) 再检测
	 *   4. 重 plan 次数 ≤ MaxReplanAttempts
	 *   5. 最终路径写入 OutFinalDest, StartMoveTo 用这个 dest (引擎自动重算最终路径)
	 *
	 * 注: 简化实现 — 用分段 MoveTo (UE AIController 支持 MoveToSegment)
	 *     或者把 Reroute 作为 dest 一次, 让 UE 引擎自己二次规划
	 * 选简化: 用 Reroute 作为单次 MoveToLocation, UE 引擎内部会自己规划到 Reroute 的路径
	 *
	 * @param IgnoreActor 必须排除的 Actor (v126.1):
	 *   - AI 自己 (防止"自己挡自己")
	 *   - MoveLocationActor (BB.Object Key 指向的目的地 Actor)
	 *
	 * @return 调整后的目的地 (可能 = OriginalDest, 也可能 = ReroutePoint)
	 */
	FVector PlanPathAvoidingActors(
		UWorld* World,
		const FVector& StartLoc,
		const FVector& OriginalDest,
		int32 MaxAttempts,
		int32& OutAttemptsUsed,
		const AActor* IgnoreActor);
};
