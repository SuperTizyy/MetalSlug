// Copyright (c) 2026. All Rights Reserved.
//
// 【v107 2026.07.28 生化模式 AI】集合点 Actor 实现
//
// 实现原则:
//   - 纯数据 Actor, 不做任何运行时业务 (账本归 Subsystem)
//   - 构造函数设默认值
//   - BeginPlay/EndPlay 自动注册/注销到 URoomZombieRallySubsystem

#include "World/Objectives/ZombieRallyPoint.h"
#include "Systems/Zombie/RoomZombieRallySubsystem.h"
#include "Engine/World.h"

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


void AZombieRallyPoint::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 自动注销 (服务器权威)
	if (URoomZombieRallySubsystem* RallySys = URoomZombieRallySubsystem::Get(this))
	{
		RallySys->UnregisterRallyPoint(this);
	}

	Super::EndPlay(EndPlayReason);
}