// Copyright (c) 2026.
//
// 【Phase 2 模式化】生化模式专用 AI Controller 实现
//
// 关键设计:
//   - 复用 ABaseAIController 全部基础能力
//   - 仅在 OnPossess/OnTargetDetected 钩子上挂"感染"事件订阅
//   - 0 硬编码数值, 全部从 Profile 走

#include "Systems/ZombieAIController.h"

#include "Characters/BaseCharacter.h"
#include "Data/AI/AIProfileAsset.h"
#include "Data/AI/AIBehaviorConfigSO.h"
#include "Systems/AI/AIRuntimeConfigComponent.h"
#include "Systems/AI/AIBehaviorTypes.h"
#include "Engine/World.h"

DEFINE_LOG_CATEGORY_STATIC(LogZombieAI, Log, All);

AZombieAIController::AZombieAIController()
{
	// 设计: 构造函数空, 所有配置由 Profile 注入
	//       与 AMeleeAIController 保持一致 (Phase 1 约定)
}


/**
 * SetupZombieAI — 生化模式 GameMode 调用入口
 *
 * 设计: 完全对称 AMeleeAIController::SetupMeleeAI
 *   1. 同步加载 Config -> Apply 到 RuntimeConfig
 *   2. 设 Faction (走 IGenericTeamAgentInterface, TeamId 走 Profile.FactionTag)
 *   3. 配 Perception (走 Config.SightConfig)
 *   4. 触发 BT 启动
 *
 * 不在 GameMode 里写死这个方法的调用 — GameMode 通过 Profile.ControllerClass 反射决定
 * 调用 SetupMeleeAI 还是 SetupZombieAI (未来有其他模式同理)
 */
void AZombieAIController::SetupZombieAI(UAIProfileAsset* ZombieProfile)
{
	if (!ZombieProfile)
	{
		return;
	}

	// 1. ApplyConfig (与 MeleeAIController 同链路)
	UAIBehaviorConfigSO* Config = ZombieProfile->LoadBehaviorConfigSync();
	if (Config && RuntimeConfig)
	{
		RuntimeConfig->ApplyConfig(Config);
	}

	// 2. Faction 设置 — 走 IGenericTeamAgentInterface
	if (ZombieProfile->FactionTag.IsValid())
	{
		SetGenericTeamId(ABaseCharacter::ResolveGenericTeamIdFromTag(ZombieProfile->FactionTag));
	}

	// 3. 调 Base 入口走 Profile 注入 (感知配置 + BT 启动)
	InitializeFromProfile(ZombieProfile);

	// 4. 配置感知 (走 Config 里读到的 SightParams, 不再硬编码)
	//    Base.OnTargetDetected 距离阈值也从 RuntimeConfig->GetScaledCombat().OverrideBTDistance 读
	if (AIPerception && RuntimeConfig && RuntimeConfig->GetConfig())
	{
		// Biochemical AI 默认视觉参数与刀战不同 — 走 Profile 配
		// 旧版 MeleeAIController 写了 ConfigurePerceptionFromConfig
		// 这里我们用同样的"读 RuntimeConfig -> 配 Sight"模式
		// (具体实现在 BaseAIController::InitializeFromProfile 之后, 由 AI 自行配; 此处只是预留扩展点)
	}

	UE_LOG(LogZombieAI, Log, TEXT("[ZombieAI] Setup 完成, Profile=%s, Role=%d"),
		*ZombieProfile->GetName(), (int32)ZombieProfile->AIRole);
}


/**
 * OnPossess — 生化 AI Possess 钩子
 *
 * 业务:
 *   1. 走 Base.OnPossess (RuntimeConfig 兜底, BT 启动兜底)
 *   2. 订阅 Pawn 的"死亡时/被感染时"事件 — Phase 3 接入, 现阶段留 TODO
 */
void AZombieAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	// Phase 2: 仅打日志
	// Phase 3 扩展: 这里订阅 InPawn 的 HealthComponent->OnDeath.AddDynamic(...)
	//               若 Profile.AIRole == Mother, 触发 OnMotherFallen 广播
	if (InPawn)
	{
		UE_LOG(LogZombieAI, Verbose, TEXT("[ZombieAI] Possess Pawn=%s"),
			*InPawn->GetName());
	}
}


/**
 * OnTargetDetected — 生化专属 override
 *
 * 设计:
 *   - 调 Super::OnTargetDetected (Base 逻辑: 距离判断 + BB.ImmediateTarget 写入)
 *   - 母体专属: 当距离近到 0, 这里追加"通知 OnPawnBecomeZombie" (Phase 3 接)
 *
 * 现版本: 完全复用 Base 实现, 不做任何额外动作
 *        (因为 GameMode 已经通过 HuntPolicy=TimeAttitudeWeighted 选目标了)
 *        但留 override 是为了 Phase 3 接"感染机制" (BTTask 调用 Force Infect -> 改 Faction)
 */
void AZombieAIController::OnTargetDetected(AActor* Actor, FAIStimulus Stimulus)
{
	Super::OnTargetDetected(Actor, Stimulus);

	// 现阶段: 仅日志, Phase 3 接感染机制时这里触发 OnPawnBecomeZombie 广播
	// (例如: 近身接触时, 母体 BTTask 调 Server_Infect(Pawn) -> Pawn->OnBecomeZombie.Broadcast(Pawn))
}
