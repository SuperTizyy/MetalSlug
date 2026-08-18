// Copyright (c) 2026.
//
// 文件作用: AZombieAIController 实现 — 生化模式 AI 控制器
// 设计: 完全复用 ABaseAIController 基础能力, 仅 override 生化专属钩子
//
// 【Phase 2 模式化】生化模式专用 AI Controller 实现
//
// 关键设计:
//   - 复用 ABaseAIController 全部基础能力
//   - 仅在 OnPossess/OnTargetDetected 钩子上挂"感染"事件订阅
//   - 0 硬编码数值, 全部从 Profile 走

#include "Systems/ZombieAIController.h"

#include "Characters/BaseCharacter.h"
#include "Data/AI/AIBehaviorConfigSO.h"
#include "Data/Faction/FactionTags.h"
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
 * 不在 GameMode 里写死这个方法的调用 — GameMode 通过 ConfigSO.LevelPlacedAIControllerClass 反射决定
 * 调用 SetupMeleeAI 还是 SetupZombieAI (未来有其他模式同理)
 */
void AZombieAIController::SetupZombieAI(UAIBehaviorConfigSO* ZombieConfig)
{
	if (!ZombieConfig)
	{
		return;
	}

	// 【2026.07.11 v26.5 大厂原则】关卡预放路径兜底 — 缓存 Pawn.FactionTag 到 CachedFactionTag
	// (与 MeleeAIController::SetupMeleeAI 同样的兜底逻辑, 详见 MeleeAIController.cpp 注释)
	if (!CachedFactionTag.IsValid())
	{
		if (ABaseCharacter* MyPawn = Cast<ABaseCharacter>(GetPawn()))
		{
			if (MyPawn->FactionTag.IsValid())
			{
				CachedFactionTag = MyPawn->FactionTag;
				UE_LOG(LogZombieAI, Log,
					TEXT("[%s] SetupZombieAI: 缓存 Pawn.FactionTag='%s' → CachedFactionTag"),
					*GetName(), *CachedFactionTag.ToString());
			}
		}
	}

	// 1. ApplyConfig (与 MeleeAIController 同链路)
	if (RuntimeConfig)
	{
		RuntimeConfig->ApplyConfig(ZombieConfig);
	}

	// 2. Faction 设置 — 走 IGenericTeamAgentInterface
	// 【v54 大厂架构重构】真理源优先级链 (ConfigSO 不持有 FactionTag — 阵营是运行时属性)
	//   1. CachedFactionTag (运行时真理源, Spawn 时已写入 — 大厅 AI/已复活 AI)
	//   2. Pawn.FactionTag (关卡预放 AI 的 BP_GruntAI 细节面板配置的阵营)
	//   3. Log Error + 不设阵营 (强制修复 BP 配置)
	FGameplayTag EffectiveFaction = FGameplayTag::EmptyTag;
	if (CachedFactionTag.IsValid())
	{
		EffectiveFaction = CachedFactionTag;
	}
	else if (ABaseCharacter* MyPawn = Cast<ABaseCharacter>(GetPawn()))
	{
		if (MyPawn->FactionTag.IsValid()
			&& FFactionTags::ValidateFactionOrReportError(MyPawn->FactionTag,
				TEXT("ZombieAIController::SetupZombieAI")))
		{
			EffectiveFaction = MyPawn->FactionTag;
			SetCachedFactionTag(EffectiveFaction);
			SetCachedIsMother(false); // 【v109.1 大厂架构】新 Spawn 的生化 AI 初始为非母体
		}
	}

	if (EffectiveFaction.IsValid())
	{
		SetGenericTeamId(FFactionTags::ToGenericTeamId(EffectiveFaction));
	}
	else
	{
		UE_LOG(LogZombieAI, Error,
			TEXT("[ZombieAI] SetupZombieAI: AI '%s' 阵营派生失败. "
			     "CachedFactionTag + Pawn.FactionTag 都为空. "
			     "【v54 大厂架构】不允许任何兜底. "
			     "【修复路径】关卡预放 AI: 打开 BP_GruntAI.uasset 细节面板 → Faction Tag 字段配 Faction.Offense/Faction.Defense. "
			     "大厅入队 AI: 检查 RoomService::RequestAddAI 的 FactionTag 参数."),
			*GetName());
	}

	// 3. 调 Base 入口走 Config 注入 (感知配置 + BT 启动)
	// 【v54 大厂架构重构】参数从 Profile 改 Config (UAIBehaviorConfigSO)
	InitializeFromConfig(ZombieConfig);

	// 4. 配置感知 (走 Config 里读到的 SightParams, 不再硬编码)
	//    Base.OnTargetDetected 距离阈值也从 RuntimeConfig->GetScaledCombat().AttackRange 读
	//    【P0 v5 2026.07.07】OverrideBTDistance 字段已删除, NearbyThreat 触发距离统一 = AttackRange
	if (AIPerception && RuntimeConfig && RuntimeConfig->GetConfig())
	{
		// Biochemical AI 默认视觉参数与刀战不同 — 走 ConfigSO 配
		// 旧版 MeleeAIController 写了 ConfigurePerceptionFromConfig
		// 这里我们用同样的"读 RuntimeConfig -> 配 Sight"模式
		// (具体实现在 BaseAIController::InitializeFromConfig 之后, 由 AI 自行配; 此处只是预留扩展点)
	}

	UE_LOG(LogZombieAI, Log, TEXT("[ZombieAI] Setup 完成, Config=%s, Faction=%s"),
		*ZombieConfig->GetName(),
		EffectiveFaction.IsValid() ? *EffectiveFaction.ToString() : TEXT("<INVALID>"));
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
