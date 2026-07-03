// Copyright (c) 2026.
//
// 【Phase 2 模式化】刀战 AI 控制器 — 仅做"刀战模式入口"标识
// 实际感知/阵营/BT 全部走 ABaseAIController
// 保留这个类只为 BP 兼容 + 未来刀战专属逻辑扩展点

#pragma once

#include "CoreMinimal.h"
#include "BaseAIController.h"
#include "MeleeAIController.generated.h"

class UAIProfileAsset;

UCLASS()
class METALSLUG01_API AMeleeAIController : public ABaseAIController
{
	GENERATED_BODY()

public:
	AMeleeAIController();

	/**
	 * 【2026.07.03 19:35 大厂 P0】关卡预放置 AI 的 Profile 默认值
	 *
	 * 为什么需要这个:
	 *   - 关卡预放的 AI (BP_GruntAI 摆在地上的) 不走 SpawnAIInternal
	 *   - 引擎只自动调 AIController::Possess(AI), 完全不进我们的 Spawn 代码
	 *   - 旧代码 PossessedBy 找不到 PlayerState → 跳过武器 Spawn
	 *
	 * 编辑器填法:
	 *   - 打开 BP_MeleeAIController (蓝图子类) → Class Defaults
	 *   - 拖入 DA_AIProfile_MeleeGrunt 到 Default Melee Profile
	 *   - 关卡里摆的 BP_GruntAI 出生时自动拿到 WQ001 刀
	 *
	 * 大厂原则: 设计师改资产即可, 0 行代码
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "AI|Profile",
		meta = (DisplayName = "Default Melee Profile (关卡预放 AI 用)"))
	TObjectPtr<UAIProfileAsset> DefaultMeleeProfile = nullptr;

	/**
	 * 刀战模式 GameMode 调用入口
	 * Phase 2 之后仅做"Profile 入口调用", 实际配置由 Base 完成
	 * 保留: BP 仍可 Cast 它, 未来可加刀战专属逻辑扩展点
	 */
	UFUNCTION(BlueprintCallable, Category = "AI|Melee")
	void SetupMeleeAI(UAIProfileAsset* MeleeProfile);

	/**
	 * 【2026.07.03 19:35 大厂 P0】OnPossess 钩子
	 *
	 * 目的: 关卡预放的 AI 没有任何外部代码显式调 SetupMeleeAI,
	 *       所以 OnPossess 里兜底: 如果 CurrentProfile 还空, 自动注入 DefaultMeleeProfile
	 *
	 * 流程:
	 *   1. Super::OnPossess (基类 RuntimeConfig 初始化)
	 *   2. 如果没 CurrentProfile 且 DefaultMeleeProfile 不空 → 走 SetupMeleeAI
	 *   3. SetupMeleeAI 会把 Profile 的武器/角色字段写入 Pawn → PossessedBy 自动装备
	 *
	 * 重入安全:
	 *   - SpawnAIInternal 路径在 Possess 之后才调 InitializeFromProfile, 所以 OnPossess 时
	 *     CurrentProfile 一定是 nullptr (不会重复注入)
	 *   - 关卡预放路径, OnPossess 是唯一注入点
	 */
	virtual void OnPossess(APawn* InPawn) override;
};
