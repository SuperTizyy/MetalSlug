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

void AMeleeAIController::OnPossess(APawn* InPawn)
{
	// 【P0 大厂修复 2026.07.03 19:35】关卡预放 AI 自举
	//
	// 场景: BP_GruntAI 摆在地图上, 引擎自动 Possess 我们的 AIController.
	//       没有任何外部代码会调 SetupMeleeAI.
	//       → 当前 CurrentProfile 为空, 后续感知/BT/武器全失效.
	//
	// 修复: OnPossess 是 Controller 的"自举点" — 没 Profile 就用 DefaultMeleeProfile
	//       (设计师在 BP_MeleeAIController 蓝图里拖入 DA_AIProfile_MeleeGrunt 即可)
	Super::OnPossess(InPawn);

	if (!GetCurrentProfile() && DefaultMeleeProfile)
	{
		UE_LOG(LogMeleeAI, Log, TEXT("[MeleeAI] OnPossess 自举注入 DefaultMeleeProfile=%s, Pawn=%s"),
			*DefaultMeleeProfile->GetName(), *GetNameSafe(InPawn));
		SetupMeleeAI(DefaultMeleeProfile);
	}
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

	// 【P0 大厂修复 2026.07.03 19:35】3. 把 Profile 的武器/角色 ID 写入 Pawn 字段
	//
	// 为什么: 关卡预放 AI 完全不走 GameMode 的 SpawnAIInternal, GameMode 那条
	//        "SetSpawnLoadout" 路径对它无效. AI 必须靠 Controller 把武器"塞给" Pawn.
	//
	// 为什么改 Pawn 字段而不是直接 SpawnActor:
	//   - SpawnAndEquipWeapon 已有完整链路 (查表/挂载/HUD), 不重新实现
	//   - Pawn 的 PossessedBy 是同帧被调, Controller 在 Possess 中写字段刚刚好
	//   - 调用栈安全: SetupMeleeAI 是 Controller 调, PossessedBy 是 Pawn 调,
	//     Controller 不直接 Spawn Actor, 避免重入
	//
	// Layer 安全:
	//   - PossessedBy 三层兜底逻辑会优先读 Pawn.SpawnWeaponID, 走到这层就拿到武器
	if (APawn* MyPawn = GetPawn())
	{
		if (ABaseCharacter* BaseChar = Cast<ABaseCharacter>(MyPawn))
		{
			const FString DesiredCharID = MeleeProfile->CharacterRowName.IsNone()
				? FString() : MeleeProfile->CharacterRowName.ToString();
			const FString DesiredWeaponID = MeleeProfile->WeaponID.IsNone()
				? FString() : MeleeProfile->WeaponID.ToString();
			BaseChar->SetSpawnLoadout(DesiredCharID, DesiredWeaponID);
		}
	}

	// 4. 调 Base 入口 — 感知配置 + BT 启动 (Phase 2 之后共用层统一)
	InitializeFromProfile(MeleeProfile);

	UE_LOG(LogMeleeAI, Log, TEXT("[MeleeAI] Setup 完成, Profile=%s"), *MeleeProfile->GetName());
}
