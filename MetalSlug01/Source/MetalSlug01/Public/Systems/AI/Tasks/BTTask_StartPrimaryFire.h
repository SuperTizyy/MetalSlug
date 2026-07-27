// Copyright (c) 2026.
//
// 【v109 2026.07.30 生化模式 AI 大厂镜像方案】BTTask — 主武器开火
//
// 职责:
//   - 调 CurrentWeapon->StartFire (Actor RPC 入口,镜像玩家 100%)
//
// 大厂原则 — 与玩家 BP_BaseCharacter 镜像:
//   - 玩家: IA_Fire → BaseCharacter::OnFirePressed → Weapon->Server_StartFire RPC
//   - AI  :  BT → BaseCharacter::GetAimRayFromCrosshairOrEyes (AI 路径)
//            → Weapon->StartFire (服务器本地直接调)
//   - 两条路径最终都调 Weapon->StartFire(Origin, Direction) 同一入口
//   - URangedLineStrategy 完全信任入参射线 — AI 不需要任何特殊处理
//
// 历史:
//   - v107: BT 显式算 (Target-Self) + SelfLocation → 起点错误(脚底)
//   - v108: 改传 ZeroVector → Strategy 嵌套 if/else 死代码,从未真正跑
//   - v109: 用 BaseCharacter::GetAimRayFromCrosshairOrEyes + Weapon->StartFire
//
// 配套:
//   - BTTask_StopPrimaryFire (同模块, 用于全自动武器的循环停火)
//   - BTTask_ReloadPrimaryWeapon (同模块, 用于换弹入口)

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTTask_StartPrimaryFire.generated.h"

/**
 * 主武器开火 Task — AI 远程射击入口 (v109 大厂镜像方案)
 *
 * 编辑器配置:
 *   - 无 Key Selector 字段
 *   - 射线算法 = BaseCharacter::GetAimRayFromCrosshairOrEyes (内置玩家/AI 自动分流)
 *   - AI 路径 = BaseAimRotation + Weapon Mesh Muzzle Socket (BT 已控制 AI 朝向)
 *
 * 大厂原则 — 与玩家 BP_BaseCharacter 镜像:
 *   - 不需要 TargetActor Key (跟玩家一样,玩家路径也不用在 BT 里指定 target)
 *   - 射线算法抽到 BaseCharacter,BT 不再算"方向" / "起点" 这些关注点
 *   - 调用入口 = Weapon->StartFire (Actor RPC, 与玩家路径完全一致)
 *   - 没武器/Pawn/无 AIController → 失败 (零兜底)
 */
UCLASS(Blueprintable, meta = (DisplayName = "Start Primary Fire (主武器开火 — v109 镜像)"))
class METALSLUG01_API UBTTask_StartPrimaryFire : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_StartPrimaryFire();

	virtual FString GetStaticDescription() const override;

	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
};