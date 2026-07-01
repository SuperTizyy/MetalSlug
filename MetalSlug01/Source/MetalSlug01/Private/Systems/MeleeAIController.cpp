// Copyright (c) 2026.
//
// 【Phase 2 模式化】刀战 AI 控制器
//
// 设计 (Phase 2 之后):
//   - 自身不写任何"配感知"代码 (已上收 ABaseAIController)
//   - 不写任何"选目标"代码 (走 RoomGameMode::RequestTargetForAI 多态)
//   - 仅保留 SetupMeleeAI 作为"刀战专属入口"的钩子 (BP 兼容性 + 未来刀战专属逻辑的扩展点)
//
// 与 Phase 1 区别:
//   - Phase 1: MeleeAIController 写死了 ConfigurePerceptionFromConfig + SightConfig 字段
//   - Phase 2: 全部上收 Base; MeleeAIController 仅留空类壳子 (BTTask 仍可 Cast 它调 BP)
//
// 未来: 若刀战加"怒气爆发"等专属机制, 在这里加; 否则可以删除直接用 ABaseAIController

#include "Systems/MeleeAIController.h"

#include "Systems/AI/AIRuntimeConfigComponent.h"
#include "Systems/AI/AIBehaviorTypes.h"
#include "Data/AI/AIBehaviorConfigSO.h"
#include "Data/AI/AIProfileAsset.h"
#include "Characters/BaseCharacter.h"

DEFINE_LOG_CATEGORY_STATIC(LogMeleeAI, Log, All);

AMeleeAIController::AMeleeAIController()
{
	// 构造函数空 — 所有配置由 Profile 注入
}

void AMeleeAIController::SetupMeleeAI(UAIProfileAsset* MeleeProfile)
{
	if (!MeleeProfile)
	{
		return;
	}

	// 1. 同步加载 Config -> Apply 到 RuntimeConfig (与 Phase 1 一致)
	UAIBehaviorConfigSO* Config = MeleeProfile->LoadBehaviorConfigSync();
	if (Config && RuntimeConfig)
	{
		RuntimeConfig->ApplyConfig(Config);
	}

	// 2. 设置 Faction (走 IGenericTeamAgentInterface)
	if (MeleeProfile->FactionTag.IsValid())
	{
		SetGenericTeamId(ABaseCharacter::ResolveGenericTeamIdFromTag(MeleeProfile->FactionTag));
	}

	// 3. 调 Base 入口 — 感知配置 + BT 启动 (Phase 2 之后共用层统一)
	InitializeFromProfile(MeleeProfile);

	UE_LOG(LogMeleeAI, Log, TEXT("[MeleeAI] Setup 完成, Profile=%s"), *MeleeProfile->GetName());
}
