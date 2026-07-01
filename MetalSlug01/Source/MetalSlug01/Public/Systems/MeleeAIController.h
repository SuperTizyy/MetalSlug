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
	 * 刀战模式 GameMode 调用入口
	 * Phase 2 之后仅做"Profile 入口调用", 实际配置由 Base 完成
	 * 保留: BP 仍可 Cast 它, 未来可加刀战专属钩子 (如怒气爆发/暴击)
	 */
	UFUNCTION(BlueprintCallable, Category = "AI|Melee")
	void SetupMeleeAI(UAIProfileAsset* MeleeProfile);
};
