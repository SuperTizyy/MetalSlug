// Copyright (c) 2026. All Rights Reserved.
//
// 【v107 2026.07.28 生化模式 AI】集合点 Actor
//
// 设计原则 (大厂架构 — 单一职责 + 零兜底):
//   - 单一字段段: PointID (显式身份) + 统计半径 + 到达半径 — 不复用 PlayerStart
//   - 复用 ATargetPoint (UE 内置, 自带 Sprite + 可视化编辑)
//   - 不持任何业务账本 (账本归 URoomZombieRallySubsystem)
//   - 不持有"谁在我这里守卫" (账本归 Subsystem)
//
// 与 PlayerStart 严格分离:
//   - PlayerStart 用途 = 出生点 (角色重生位置), 与集合点语义完全不同
//   - 复用 PlayerStart 会污染出生点账本 + 选点半径无法独立配置
//
// 编辑器配置:
//   在地图里放置 BP_ZombieRallyPoint 子类, 拖入地图关键防御位置
//   必填字段:
//     - PointID (FString) — 唯一 ID, 用于锁定账本 key (例如 "RallyA", "RallyB")
//     - PopulationRadius (float, cm) — 人数统计半径 (默认 800)
//   可选字段:
//     - ArrivalRadiusOverride (float, cm) — 单点覆盖, 0 = 用 ConfigSO 默认值

#pragma once

#include "CoreMinimal.h"
#include "Engine/TargetPoint.h"
#include "ZombieRallyPoint.generated.h"

/**
 * @file ZombieRallyPoint.h
 * @brief 生化模式 AI 集合点 Actor — 人类玩家守卫的据点定义
 *
 * 大厂架构定位:
 *   - 单一职责: 仅作为"集合点"数据容器 (PointID + 半径配置), 不持有任何运行时业务状态
 *   - 真理源: PointID (字符串) — URoomZombieRallySubsystem 用这个作为账本 key
 *   - 自动注册: BeginPlay → URoomZombieRallySubsystem::RegisterRallyPoint; EndPlay → 自动注销
 *   - 与 PlayerStart 严格分离: 集合点 ≠ 出生点, 语义不同, 账本不同
 *
 * 设计动机:
 *   - 复用 ATargetPoint (UE 内置 SceneComponent + Sprite), 避免重复造轮子
 *   - 字段最小化: 只暴露 PointID / PopulationRadius / ArrivalRadiusOverride, 不暴露运行时账本
 *   - 不持任何业务数据: "谁在我这里" / "当前锁定数" 等都归 Subsystem 账本管理
 *
 * 零兜底:
 *   - PointID 必须唯一 + 非空 (重复/空 → Subsystem::RegisterRallyPoint 拒绝 + Log Error)
 *   - PopulationRadius 必须 > 0 (<=0 → Subsystem 拒绝注册)
 *
 * 编辑器配置:
 *   - 在地图里放置 BP_ZombieRallyPoint 子类, 拖入地图关键防御位置
 *   - 必填: PointID (唯一 ID) + PopulationRadius (统计半径)
 *   - 可选: ArrivalRadiusOverride (单点覆盖, 0 = 用 ConfigSO 默认值)
 */

/**
 * 生化模式集合点 — AI 守卫的人类据点
 *
 * 大厂原则:
 *   - 纯数据 Actor, 没有 Tick, 没有 RPC
 *   - 真理源 = PointID (字符串) — Subsystem 用这个注册到账本
 *   - 重叠检测: PopulationRadius 用 SphereOverlap 算附近人类数
 *
 * 必填 (零兜底):
 *   - PointID 必须唯一 + 非空 (重复/空 → Subsystem 拒绝注册)
 *
 * 数据流:
 *   - 写入: 编辑器配置 PointID / PopulationRadius / ArrivalRadiusOverride
 *   - 读取: URoomZombieRallySubsystem 通过 RegisterRallyPoint 读取所有字段
 *   - 不复制: 纯服务器数据, 客户端不需要单独配置 (服务器权威)
 */
UCLASS(Blueprintable)
class METALSLUG01_API AZombieRallyPoint : public ATargetPoint
{
	GENERATED_BODY()

public:
	AZombieRallyPoint();

	/**
	 * 集合点唯一 ID — 账本 key
	 *
	 * 强制要求 (零兜底):
	 *   - 非空字符串
	 *   - 同一对局内唯一 (重复 → Subsystem::RegisterRallyPoint 拒绝 + Log Error)
	 *
	 * 用途:
	 *   - Subsystem 用 PointID 作为 TMap key (账本)
	 *   - BTTask_SelectZombieRallyPoint 写入 BB.LockedRallyPoint
	 *   - 锁定账本用 PointID 串行化 (避免持有 Actor 弱引用的脏数据)
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Zombie|Rally",
		meta = (DisplayName = "PointID (集合点唯一 ID)"))
	FString PointID;

	/**
	 * 人数统计半径 (cm) — 选"人类最多集合点"时, 用这个半径统计附近存活人类数
	 *
	 * 默认 800cm, 必须 > 0 (零兜底: <=0 → Subsystem 拒绝注册)
	 * 通常应该 >= ConfigSO.ZombieRallyArrivalRadius, 否则 AI 站到点上仍不在统计半径内
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Zombie|Rally",
		meta = (ClampMin = "100.0", ClampMax = "5000.0",
		        DisplayName = "PopulationRadius (人数统计半径 cm)"))
	float PopulationRadius = 800.f;

	/**
	 * 单点覆盖到达半径 (cm)
	 *
	 * 0 = 用 ConfigSO.ZombieRallyArrivalRadius 默认值
	 * > 0 = 用本字段覆盖 (单点特殊配置, 例如出生点旁的点放宽到 100cm 方便落点)
	 *
	 * 注意: 单纯覆盖 BTTask 选点用, 不影响 Subsystem 的人数统计
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Zombie|Rally",
		meta = (ClampMin = "0.0", ClampMax = "1000.0",
		        DisplayName = "ArrivalRadiusOverride (单点到达半径覆盖 cm, 0=用 ConfigSO 默认)"))
	float ArrivalRadiusOverride = 0.f;

	/** 调试可视化 (Editor only, 运行时不影响逻辑) */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Zombie|Rally|Debug")
	bool bDrawDebug = false;

protected:
	/**
	 * 【v107 自动注册】BeginPlay 时向 URoomZombieRallySubsystem 注册
	 * EndPlay 时自动注销
	 *
	 * 设计动机:
	 *   - 关卡预放 Actor 不用手动管理生命周期
	 *   - 账本自动反映场景内可用集合点
	 *   - 地图卸载时自动清理 (Subsystem 与 World 同生命周期)
	 */
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
};