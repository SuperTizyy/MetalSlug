// Copyright (c) 2026. All Rights Reserved.
//
// 【v107 2026.07.28 生化模式 AI】集合点 Actor 实现
//
// 实现原则:
//   - 纯数据 Actor, 不做任何运行时业务 (账本归 Subsystem)
//   - 构造函数设默认值
//   - BeginPlay/EndPlay 自动注册/注销到 URoomZombieRallySubsystem

/**
 * @file ZombieRallyPoint.cpp
 * @brief AZombieRallyPoint 实现 — 集合点 Actor 生命周期与自动注册
 *
 * 大厂原则落地:
 *   - 单一职责: 本文件只做"自动注册/注销", 不持有任何业务账本
 *   - 服务器权威: 只在服务器注册到 Subsystem (账本服务器权威)
 *   - 自动生命周期: BeginPlay → Register; EndPlay → Unregister, 不需要关卡脚本管理
 *   - 零兜底: PointID 空/重复由 Subsystem 拒绝 + Log Error, 本 Actor 不容错
 */

#include "World/Objectives/ZombieRallyPoint.h"
#include "Systems/Zombie/RoomZombieRallySubsystem.h"
#include "Engine/World.h"

/**
 * @brief 构造函数 — 设置默认值 + 关闭 Tick/复制
 *
 * 关键点:
 *   - PointID 默认为空字符串 (不预填示例值, 强制策划显式配 — 大厂原则)
 *   - 复用 ATargetPoint 自带 USceneComponent 根 + Sprite 可视化
 *   - 关闭 Tick (纯数据 Actor, 不需要每帧逻辑)
 *   - 关闭 Replicates (纯客户端可视化 + 服务器账本输入, 不需要跨网络同步)
 */
AZombieRallyPoint::AZombieRallyPoint()
{
	// 默认非空 PointID — 编辑器必须显式配 (Blueprint 子类拖入地图时提示)
	// 大厂原则: 不给"看似能跑"的默认值 (POINT_01 这种), 强制显式配置
	PointID = TEXT("");

	// 复用 ATargetPoint 自带组件 (USceneComponent 根 + Sprite 可视化)
	PrimaryActorTick.bCanEverTick = false;
	bReplicates = false; // 纯客户端可视化 + 服务器账本输入, 不需要复制
	SetReplicateMovement(false);
}


/**
 * @brief 引擎回调 — 自动注册到 URoomZombieRallySubsystem 账本 (服务器权威)
 *
 * 流程:
 *   1. Super::BeginPlay() — 父类初始化
 *   2. 取 World + URoomZombieRallySubsystem
 *   3. 调 Subsystem->RegisterRallyPoint(this), 由 Subsystem 校验 PointID/PopulationRadius
 *
 * @note 只在服务器执行注册逻辑 (账本服务器权威, 客户端不参与账本维护)
 * @note 客户端 BeginPlay 也会触发, 但 Subsystem->RegisterRallyPoint 内部会检测 NetMode, 客户端拒绝
 */
void AZombieRallyPoint::BeginPlay()
{
	Super::BeginPlay();

	// 【v107 大厂原则】自动注册到 Subsystem 账本
	// 只在服务器注册 (账本服务器权威)
	if (UWorld* World = GetWorld())
	{
		if (URoomZombieRallySubsystem* RallySys = URoomZombieRallySubsystem::Get(this))
		{
			RallySys->RegisterRallyPoint(this);
		}
	}
}


/**
 * @brief 引擎回调 — 自动注销 Subsystem 账本条目 (清理, 防止脏数据)
 *
 * @param EndPlayReason  销毁原因 (Destroyed / RemovedFromWorld / EndPlayInEditor 等)
 *
 * @note 即使 EndPlayReason 是 RemovedFromWorld (地图卸载) 也会触发注销, 保证账本与场景同步
 * @note 注销失败不会影响 EndPlay 流程 (Subsystem 可能已先销毁, 例如 World 卸载场景)
 */
void AZombieRallyPoint::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 自动注销 (服务器权威)
	if (URoomZombieRallySubsystem* RallySys = URoomZombieRallySubsystem::Get(this))
	{
		RallySys->UnregisterRallyPoint(this);
	}

	Super::EndPlay(EndPlayReason);
}