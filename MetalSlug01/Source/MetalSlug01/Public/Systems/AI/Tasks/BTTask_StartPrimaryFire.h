// Copyright (c) 2026.
//
// 【v130 2026.08.02】BTTask — 主武器开火
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
//   - v130: 【新】新增 TargetActorKey (BB Object Key)
//           当 TargetActor 有效 → 射线 = Muzzle Socket → Target Actor 位置
//           → 解决"AI 水平射击, 不跟随 Target 上下位置"问题 (用户 2026.08.02 反馈)
//           当 TargetActor 无效 → fallback 到 v109 GetAimRayFromCrosshairOrEyes
//
// 配套:
//   - BTTask_StopPrimaryFire (同模块, 用于全自动武器的循环停火)
//   - BTTask_ReloadPrimaryWeapon (同模块, 用于换弹入口)

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BehaviorTree/BehaviorTreeTypes.h"  // FBlackboardKeySelector
#include "BTTask_StartPrimaryFire.generated.h"

/**
 * 主武器开火 Task — AI 远程射击入口
 *
 * 编辑器配置:
 *   - TargetActorKey (可选): BB.Object Key (默认空 = 走 v109 旧行为)
 *     - 配 = "TargetActor" → 射线 = Muzzle → Target Actor 位置
 *       (解决"AI 水平射击不跟随 Target 上下"问题 — 用户 2026.08.02 反馈)
 *     - 不配 = 走 GetAimRayFromCrosshairOrEyes AI 路径 (v109 行为, 仅水平)
 *   - 大厂原则 - Target Data 优先:
 *     - TargetActor 有效 → 3D 追踪射线 (跟随 Target 上下)
 *     - TargetActor 无效 → fallback AI BaseAimRotation (水平射线,防止 AI 不开火)
 *
 * 大厂原则 — 与玩家 BP_BaseCharacter 镜像:
 *   - 调用入口 = Weapon->StartFire (Actor RPC, 与玩家路径完全一致)
 *   - 没武器/Pawn/无 AIController → 失败 (零兜底)
 *
 * v130 新增能力:
 *   - 解决"AI 水平射击" (用户反馈)
 *   - 与玩家镜像: 玩家 Crosshair Ray 是"屏幕中心 → 世界坐标" 也是完整 3D
 *   - AI 现在能完整 3D 追踪 Target (CS:GO / Apex / Valorant 等大厂标准)
 */
UCLASS(Blueprintable, meta = (DisplayName = "Start Primary Fire (主武器开火 — v130 3D追踪)"))
class METALSLUG01_API UBTTask_StartPrimaryFire : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_StartPrimaryFire();

	virtual FString GetStaticDescription() const override;

	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;

	/**
	 * 【v130 新增】目标 Actor BB Key (Object 类型)
	 * - 配 TargetActor 时, 射线方向 = Muzzle Socket → Target Actor 位置
	 *   → AI 跟随 Target 上下位置 (玩家目标跳到高处, 射线自动抬高)
	 * - 不配时, fallback 到 v109 GetAimRayFromCrosshairOrEyes AI 路径
	 *   → 仅水平射线 (BaseAimRotation)
	 *
	 * 大厂原则 - 数据驱动:
	 *   - 上层 Decorator (HasLineOfSight / InAttackRange) 已确认 TargetActor 存在
	 *   - 这里直接读, 不再二次校验 (避免重复, 单一真理源)
	 */
	UPROPERTY(EditAnywhere, Category = "Target")
	FBlackboardKeySelector TargetActorKey;
};