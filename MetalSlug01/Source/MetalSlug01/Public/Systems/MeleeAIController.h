// Copyright (c) 2026.
//
// ========================================================================
// AMeleeAIController — 关卡预放 AI 控制器（单一职责）
// ========================================================================
//
// 【v54.4 大厂架构重构】职责重新划分
//
// 单一职责: 本控制器只负责「关卡预放 AI」路径的入口
//   - OnPossess → SetupMeleeAI → InitializeFromConfig → 启动 BT + 武器 + 无敌期
//
// 大厅入队 AI (房主从 UI 添加) 不走这里:
//   - 走 SpawnAIInternal → InitializeFromConfig(EffectiveConfig) (Base 直接调)
//   - 大厅 AI 的 BT 来源 = GM.ModeRulesByMode[Mode].BehaviorTree (按游戏模式)
//
// 配置路径:
//   Level 里拖 BP_GruntAI (Pawn)
//   → BP_GruntAI.AIControllerClass = BP_MeleeAIController
//   → BP_MeleeAIController.DefaultMeleeConfig = DA_AIBehaviorConfig_MeleeGrunt
//      DA_AIBehaviorConfig_MeleeGrunt:
//        LevelPlacedBehaviorTree = BT_MeleeAI         ← 关卡预放 AI 的行为树
//        LevelPlacedWeaponClass = BP_Weapon_Knife    ← 关卡预放 AI 的武器
//        LevelPlacedAIControllerClass = BP_MeleeAIController (自己)
//        Combat / Perception (行为参数)
//        SpawnInvincibilitySeconds = 2.0          ← 所有 AI 共用
// ========================================================================

#pragma once

#include "CoreMinimal.h"
#include "BaseAIController.h"
#include "MeleeAIController.generated.h"

class UAIBehaviorConfigSO;

/**
 * @file MeleeAIController.h
 * @brief 关卡预放 AI 控制器 (AMeleeAIController) — Level-Placed 路径单一入口
 *
 * 大厂架构角色:
 *   - 单一职责: 仅处理「Level 预置 AI」的 Possess 逻辑
 *   - 大厅入队 AI 不走这里, 走 SpawnAIInternal → InitializeFromConfig
 *
 * 与其他组件的关系:
 *   - 上游: ABaseAIController (基类, 走 OnPossess 链路)
 *   - 配套: UAIBehaviorConfigSO.LevelPlacedBehaviorTree + LevelPlacedWeaponClass (真理源)
 *   - 下游: ABaseCharacter Pawn (执行武器生成 + 无敌期)
 *
 * v54.4 大厂重构:
 *   - 直接接 UAIBehaviorConfigSO, 移除中间层 UAIProfileAsset
 *   - 关卡预放 AI 唯一初始化入口: OnPossess → SetupMeleeAI(DefaultMeleeConfig)
 */

/**
 * AMeleeAIController — 关卡预放 AI 控制器
 *
 * 【v54.4 重构】职责明确: 只处理「Level 预置 AI」的 Possess 逻辑
 *
 * 入口:
 *   OnPossess (关卡预放路径) → SetupMeleeAI(DefaultMeleeConfig) → InitializeFromConfig → 启动 BT
 *
 * 大厅路径 (不经过这里):
 *   SpawnAIInternal → InitializeFromConfig(EffectiveConfig) (Base 直接调)
 *
 * 【v54.4 大厂架构重构】
 *   - DefaultMeleeConfig 字段保留 (BP_MeleeAIController 里配)
 *   - 但 ConfigSO 里的 BehaviorTree 已重命名为 LevelPlacedBehaviorTree
 *   - ConfigSO 里的 DefaultWeaponClass 已重命名为 LevelPlacedWeaponClass
 *   - ConfigSO 里的 DefaultAIControllerClass 已重命名为 LevelPlacedAIControllerClass
 */

UCLASS()
class METALSLUG01_API AMeleeAIController : public ABaseAIController
{
	GENERATED_BODY()

public:
	AMeleeAIController();

	/**
	 * 【v54.4 大厂架构重构】关卡预放 AI 的默认 ConfigSO
	 *
	 * 编辑器填法:
	 *   - 打开 BP_MeleeAIController (蓝图子类) → Class Defaults
	 *   - 拖入 DA_AIBehaviorConfig_MeleeGrunt 到 Default Melee Config
	 *   - 关卡里摆的 BP_GruntAI 出生时自动拿到 ConfigSO (含 LevelPlacedBehaviorTree / LevelPlacedWeaponClass)
	 *
	 * 大厂原则: 设计师改资产即可, 0 行代码
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AI|Config",
		meta = (DisplayName = "Default Melee Config (DA_AIBehaviorConfig_*, 关卡预放 AI 用)"))
	TObjectPtr<UAIBehaviorConfigSO> DefaultMeleeConfig = nullptr;

	/**
	 * 关卡预放 AI 专用入口 — SetupMeleeAI
	 *
	 * 【v54.4 重构】直接接 UAIBehaviorConfigSO
	 *   - 从 DefaultMeleeConfig 读取 LevelPlacedBehaviorTree (BT) + LevelPlacedWeaponClass (武器)
	 *
	 * 调用链:
	 *   OnPossess (关卡预放路径) → SetupMeleeAI(DefaultMeleeConfig) → InitializeFromConfig → 启动 BT
	 *
	 * 大厅路径 (不经过这里):
	 *   SpawnAIInternal → InitializeFromConfig(EffectiveConfig) → 启动 BT (BT 从 ModeRules 拿)
	 */
	UFUNCTION(BlueprintCallable, Category = "AI|Melee")
	void SetupMeleeAI(UAIBehaviorConfigSO* MeleeConfig);

	/**
	 * OnPossess 钩子 — 关卡预放 AI 自动注入 DefaultMeleeConfig
	 */
	virtual void OnPossess(APawn* InPawn) override;
};