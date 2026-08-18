// Copyright (c) 2026.
//
// 【v122 2026.08.01 生化模式 AI】BTService — 反射式换点 + 首次选点
//
// 业务背景 (用户 2026.08.01 反馈):
//   "当 AI 在地图中任意的集合点附近 100 厘米, 且 AI 附近 50 厘米范围内有母体时, 更换 LockedRallyPoint 的点位"
//
// 职责 (单一职责 + 大厂原则 — BT 为主 C++ 为辅):
//   - 周期性 (默认 0.2s) 派生两个事实:
//     1) AI 距任意集合点 ≤ ReflexChangeRallyPointRadius (100cm) — v122 语义修正
//     2) AI 附近 (50cm) 范围内存在任何存活母体
//   - 两条件同时满足 → 触发反射式换点 / 首次选点:
//     a) 调 URoomZombieRallySubsystem::SelectRallyPoint_Nearest_Excluding 选下一个最近点
//     b) 调 URoomZombieRallySubsystem::LockRallyPointForAI 同步账本 (允许首次登记)
//     c) 写 BB.LockedRallyPoint / BB.DistanceToRallyPoint
//     d) StopMovement 打断旧 MoveTo 异步任务 (UE 5.6 标准做法)
//   - 条件不满足 → 退出, 不写 BB
//
// 【v122 根因修复】"LockedRallyPoint 运行时没值":
//   旧 (v120) 误读用户原话: 条件 1 = "AI 距离当前 BB.LockedRallyPoint ≤ TriggerRadius"
//   - 出生时 BB.LockedRallyPoint = nullptr
//   - 条件 1 永远拒判
//   - 永远没值
//   新 (v122) 正确语义: 条件 1 = "AI 距任意集合点 ≤ TriggerRadius" (走账本遍历, 不依赖 BB)
//   - 出生时也能触发"首次选点"
//   - 已锁点时正常触发"换点"
//
// 与 BTTask_SelectZombieRallyPoint 的关系:
//   - BTTask_SelectZombieRallyPoint 已删除 (v122)
//   - 本 Service 是唯一集合点写入入口 (账本 + BB)
//
// 决策路径 (大厂原则 — 决策归 BT, 写 BB 归本 Service):
//   - 何时换点 ← 本 Service 派生配置 + 触发动作
//   - 换哪个点 ← URoomZombieRallySubsystem (账本唯一真理源)
//   - 怎么写 BB ← 本 Service 效仿 BTTask_SelectZombieRallyPoint 写 BB.Locked 3 Key
//
// 真理源链路:
//   ConfigSO.ReflexChangeRallyPointRadius / ReflexChangeMotherThreatRadius / ReflexChangeTickIntervalSeconds
//     ↓ BTService_ReflexRallyChange 派生
//     ↓ URoomZombieRallySubsystem::SelectRallyPoint_Nearest_Excluding + LockRallyPointForAI
//     ↓ 写 BB.LockedRallyPoint / DistanceToRallyPoint
//
// 抗抖动 (v120 防控):
//   - 条件 1 (100cm) + 条件 2 (50cm) 是硬阈值, 不平滑 (避免抖动)
//   - Tick 频率 0.2s, 不主动连续重选 (订阅账本不会重选)
//   - 触发后立即换点, 母体继续靠近 → 下一 Tick 再次触发 → 换新点 = 业务真实意图
//     (抗抖动不要加冷却 — 那是兜底, 掩盖业务问题)
//
// 抗死亡 (v120 防御):
//   - AI 死亡 → 静默退出 (与其他 Service 镜像)
//   - 当前 LockedRallyPoint 已销毁 → 跳过 (TWeakObjectPtr 失效保护, 等同账本自动清)
//   - 母体已死亡 → 跳过 (IsDead 校验)
//   - Subsystem 缺失 → Log Error + 退出 (不允许 fallback)
//
// 刀战模式 0 影响:
//   - BT_MeleeAI 不挂本 Service (本 Service 跑在 BT_ZombieModeAI 根节点)
//   - 即使误挂也不影响: 刀战 AI Pawn.bIsMother=false, 我们对母体威胁检测只看 MotherCharacters 账本
//     账本在刀战模式永远空 → 条件 2 永远 false → 触发永远不命中 → 0 副作用

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTService.h"
#include "BehaviorTree/BehaviorTreeTypes.h"
#include "BTService_ReflexRallyChange.generated.h"

class AZombieRallyPoint;
class URoomZombieRallySubsystem;
class URoomMotherMutationSubsystem;
class ABaseCharacter;
class AController;
class UBlackboardComponent;

/**
 * 生化模式 AI — 反射式换点 Service
 *
 * 编辑器配置 (BT 根节点挂):
 *   - LockedRallyPointKey:    BB.LockedRallyPoint (Object, 读当前锁点)
 *   - DistanceToRallyPointKey: BB.DistanceToRallyPoint (Float, 写新距离)
 *   - RallyPointTriggerRadius: 100cm (策划可调, 默认 100, 与 ConfigSO 同语义)
 *   - MotherThreatRadius:      50cm  (策划可调, 默认 50, 与 ConfigSO 同语义)
 *
 * 使用方式 (BT 编辑器):
 *   1. 打开 BT_ZombieModeAI.uasset, 根节点 (Root) 上挂本 Service
 *   2. 设置 Interval = 0.2 (或用 ConfigSO.ReflexChangeTickIntervalSeconds 默认值)
 *   3. 配 2 个 Black Key: LockedRallyPoint / DistanceToRallyPoint
 *   4. Service 自身 0 Notify (无 Tick 通知需求, 事件触发即可)
 */
UCLASS(Blueprintable, meta = (DisplayName = "Reflex Rally Change (反射式换点)"))
class METALSLUG01_API UBTService_ReflexRallyChange : public UBTService
{
	GENERATED_BODY()

public:
	UBTService_ReflexRallyChange();

	/** @brief BT 编辑器静态描述 (显示 TriggerRadius / MotherThreatRadius / Tick 频率) */
	virtual FString GetStaticDescription() const override;

	/** BB.LockedRallyPoint (Object Key) — 读取当前锁点 / 写入新锁点 */
	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector LockedRallyPointKey;

	/** BB.DistanceToRallyPoint (Float Key) — 写入新点到 AI 的距离 */
	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector DistanceToRallyPointKey;

	/**
	 * 触发半径 1 (cm) — AI 距离当前 LockedRallyPoint ≤ 此值, 进入判定前半段
	 *
	 * 默认 100cm, 与 ConfigSO.ReflexChangeRallyPointRadius 同步
	 * 策划在 BT 编辑器可单独 override (不破坏 ConfigSO 真理源)
	 *
	 * 0 = 强制用 ConfigSO 值
	 * > 0 = 用本字段值 (override)
	 */
	UPROPERTY(EditAnywhere, Category = "Config",
		meta = (ClampMin = "0.0", ClampMax = "1000.0",
		        DisplayName = "RallyPoint Trigger Radius (反射式换点触发半径 cm, 0=用ConfigSO)"))
	float RallyPointTriggerRadius = 0.f;

	/**
	 * 触发半径 2 (cm) — AI 附近 ≤ 此值 范围内存在任何存活母体, 进入判定后半段
	 *
	 * 默认 50cm, 与 ConfigSO.ReflexChangeMotherThreatRadius 同步
	 * 0 = 强制用 ConfigSO 值
	 * > 0 = 用本字段值 (override)
	 */
	UPROPERTY(EditAnywhere, Category = "Config",
		meta = (ClampMin = "0.0", ClampMax = "1000.0",
		        DisplayName = "Mother Threat Radius (反射式换点母体威胁半径 cm, 0=用ConfigSO)"))
	float MotherThreatRadius = 0.f;

	/**
	 * 【v125 2026.08.01】已访问过的集合点距离权重倍数
	 *
	 * 1.0 = 无惩罚 (纯按距离)
	 * 4.0 = 已访问过的点距离视为实际 2 倍 (score 平方后 4 倍)
	 * 默认 4.0 — 抗 A↔B 来回切换的核心
	 *
	 * 业务背景 (用户 2026.08.01 反馈):
	 *   "就算之前呆过的也行, 优先没呆过的" → 用 VisitBiasMultiplier 体现偏好
	 *   0 = 强制用 URoomZombieRallySubsystem::DefaultVisitBiasMultiplier (默认 4.0)
	 */
	UPROPERTY(EditAnywhere, Category = "Config",
		meta = (ClampMin = "0.0", ClampMax = "32.0",
		        DisplayName = "Visit Bias Multiplier (已访问过的点距离权重, 0=用Subsystem默认)"))
	float VisitBiasMultiplier = 0.f;

protected:
	virtual void TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory,
		float DeltaSeconds) override;

private:
	/**
	 * 平面距离平方 (Z 轴忽略) — 与 URoomZombieRallySubsystem::ComputeFlatDistanceSq 镜像
	 * 楼层差异不应影响"附近"判定
	 */
	static float ComputeFlatDistanceSq(const FVector& A, const FVector& B);

	/**
	 * 【v122 寻址】检查触发条件 1: AI 距任意集合点 ≤ TriggerRadius
	 *
	 * 用户原话语义: "AI 在地图中任意的集合点附近 100 厘米"
	 * 即查找 AI 周围任意集合点 ≤ TriggerRadius, 找到即视为"AI 已在集合点附近"
	 *
	 * 旧 (v120) 误读为 "AI 距 BB.LockedRallyPoint ≤ TriggerRadius", 出生时 LockedRallyPoint=nullptr
	 *   → 永远拒判 → 永远没值. 已彻底重构.
	 *
	 * @param SelfPawn      AI 自身 Pawn
	 * @param RallySys      集合点账本 (走单一真理源, 不 GetAllActorsOfClass)
	 * @param BB            Blackboard 组件 (保留参数, 防御性扩展)
	 * @param TriggerRadius 触发半径 cm (由 TickNode 入口派生, ConfigSO 或 Service override)
	 * @param OutPointID    返回 AI 附近最近的集合点 PointID (供 PerformReflexChange 写 BB)
	 * @return true = 满足条件 1
	 *
	 * 大厂原则 - 零兜底: 距离比较严格用传入的 TriggerRadius, 不在 helper 内硬编码
	 */
	bool CheckRallyPointProximity(ABaseCharacter* SelfPawn,
		URoomZombieRallySubsystem* RallySys, UBlackboardComponent* BB,
		float TriggerRadius, FString& OutPointID) const;

	/**
	 * 检查触发条件 2: AI 附近 MotherThreatRadius 范围内存在任何存活母体
	 *
	 * @return true = 满足条件 2
	 */
	bool CheckMotherThreatProximity(ABaseCharacter* SelfPawn,
		URoomMotherMutationSubsystem* MotherSys, float ThreatRadius) const;

	/**
	 * 触发换点 — 调 Subsystem 选点 + 同步账本 + 写 BB
	 *
	 * @return true = 成功换点, false = 失败 (无其他点 / 账本拒绝, 已 Log Error)
	 */
	bool PerformReflexChange(ABaseCharacter* SelfPawn, AController* AIC,
		UBlackboardComponent* BB, URoomZombieRallySubsystem* RallySys,
		const FString& CurrentPointID, float VisitedBias);
};
