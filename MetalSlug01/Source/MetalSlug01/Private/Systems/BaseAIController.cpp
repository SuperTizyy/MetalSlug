// Copyright (c) 2026.

#include "Systems/BaseAIController.h"

#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISenseConfig_Sight.h" // 【Phase 2】共用层配 Sight
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/BlackboardData.h"

#include "Characters/BaseCharacter.h"

#include "Systems/AI/AIRuntimeConfigComponent.h"
#include "Systems/AI/AIBlackboardKeyRegistrySubsystem.h"
#include "Systems/AI/AIBehaviorTypes.h"
#include "Data/AI/AIProfileAsset.h"
#include "Data/AI/AIBehaviorConfigSO.h"
#include "Engine/StreamableManager.h" // 【Phase 1】 TSoftObjectPtr::LoadSynchronous
#include "Engine/World.h"

DEFINE_LOG_CATEGORY_STATIC(LogBaseAI, Log, All);

// ==========================================
// 1. 构造函数
// ==========================================

ABaseAIController::ABaseAIController()
{
	RuntimeConfig = CreateDefaultSubobject<UAIRuntimeConfigComponent>(TEXT("RuntimeConfig"));
	AIPerception = CreateDefaultSubobject<UAIPerceptionComponent>(TEXT("AIPerception"));
}


// ==========================================
// 2. UE 生命周期
// ==========================================

void ABaseAIController::BeginPlay()
{
	Super::BeginPlay();

	if (AIPerception)
	{
		AIPerception->OnTargetPerceptionUpdated.AddDynamic(this, &ABaseAIController::OnTargetDetected);
	}

	// 【Phase 1 重构】 未走 Profile 入口, 但有 Pawn 时尝试 fallback
	if (GetPawn() && !CurrentProfile && !bBehaviorTreeStarted)
	{
		RunLegacyBehaviorTree();
	}
}

void ABaseAIController::OnPossess(APawn* InPawn)
{
	Super::OnPossess(InPawn);

	if (!RuntimeConfig)
	{
		RuntimeConfig = InPawn ? InPawn->FindComponentByClass<UAIRuntimeConfigComponent>() : nullptr;
	}
}

void ABaseAIController::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);
}


// ==========================================
// 3. Profile 入口
// ==========================================

void ABaseAIController::InitializeFromProfile(UAIProfileAsset* InProfile)
{
	if (!InProfile)
	{
		UE_LOG(LogBaseAI, Warning, TEXT("[%s] InitializeFromProfile(null)"), *GetName());
		return;
	}

	CurrentProfile = InProfile;
	UE_LOG(LogBaseAI, Log, TEXT("[%s] InitializeFromProfile: Profile=%s FactionTag=%s ConfigAsset=%s"),
		*GetName(), *InProfile->GetName(),
		*InProfile->FactionTag.ToString(),
		*InProfile->BehaviorConfig.ToSoftObjectPath().ToString());

	// 把 Profile.FactionTag 应用到阵营协议
	if (InProfile->FactionTag.IsValid())
	{
		SetGenericTeamId(ABaseCharacter::ResolveGenericTeamIdFromTag(InProfile->FactionTag));
	}

	// Config 同步加载 (BT 由它异步加载)
	UAIBehaviorConfigSO* Config = InProfile->LoadBehaviorConfigSync();
	UE_LOG(LogBaseAI, Log, TEXT("[%s] InitializeFromProfile: LoadBehaviorConfigSync -> %s"),
		*GetName(), *GetNameSafe(Config));
	if (RuntimeConfig)
	{
		RuntimeConfig->ApplyConfig(Config);
	}

	if (!Config)
	{
		UE_LOG(LogBaseAI, Warning, TEXT("[%s] Profile has no Config"), *GetName());
		return;
	}

	// BT 异步加载完成后启动 (走 AIProfileAsset 的 FOnAIBehaviorConfigLoaded)
	// 设计: UAIProfileAsset 负责异步加载它持有的 SoftObjectPtr<AIBehaviorConfigSO>
	//       加载完成后 config 已 Apply 到 RuntimeConfig (base 已同步一次, 这里直接异步监听完成)
	ProfileLoadedDelegate = FOnAIBehaviorConfigLoaded::CreateUObject(this, &ABaseAIController::OnProfileLoaded);
	InProfile->LoadBehaviorConfigAsync(ProfileLoadedDelegate);
	UE_LOG(LogBaseAI, Log, TEXT("[%s] InitializeFromProfile: LoadBehaviorConfigAsync dispatched"), *GetName());
}

void ABaseAIController::OnProfileLoaded()
{
	UE_LOG(LogBaseAI, Log, TEXT("[%s] OnProfileLoaded: CurrentProfile=%s RuntimeConfig=%s GetConfig=%s"),
		*GetName(),
		*GetNameSafe(CurrentProfile),
		*GetNameSafe(RuntimeConfig),
		*GetNameSafe(RuntimeConfig ? RuntimeConfig->GetConfig() : nullptr));

	if (!CurrentProfile || !RuntimeConfig || !RuntimeConfig->GetConfig())
	{
		UE_LOG(LogBaseAI, Warning, TEXT("[%s] OnProfileLoaded: missing components, fallback RunLegacy"), *GetName());
		RunLegacyBehaviorTree();
		return;
	}

	// 【Phase 2 共用层】感知配置 — 收归 Base, 任何模式都走这里
	// 旧: MeleeAIController::ConfigurePerceptionFromConfig 单独配
	// 新: Base 统一从 RuntimeConfig->GetScaledPerception() 读, 自动配
	ConfigurePerceptionFromConfig();

	StartBehaviorTreeFromProfile();
}

/**
 * 【Phase 2 共用层】从 RuntimeConfig 配 AIPerception (Sight)
 *
 * 设计:
 *   - 替代原 MeleeAIController::ConfigurePerceptionFromConfig
 *   - 走 RuntimeConfig->GetScaledPerception() — 已经按难度缩放过
 *   - Hearing 留给 Phase 3, 现阶段不配
 *
 * 注意:
 *   - 必须 RuntimeConfig 和 Config 都非空才执行 (兜底)
 *   - SightConfig 是 NewObject, 由 UE 反射管理 GC, 不会泄漏
 */
void ABaseAIController::ConfigurePerceptionFromConfig()
{
	if (!AIPerception || !RuntimeConfig || !RuntimeConfig->GetConfig())
	{
		return;
	}

	UAISenseConfig_Sight* SightConfig = NewObject<UAISenseConfig_Sight>(this, TEXT("SightConfig_Phase2"));
	if (!SightConfig)
	{
		return;
	}

	const FAIPerceptionParams Params = RuntimeConfig->GetScaledPerception();
	SightConfig->SightRadius = Params.SightRadius;
	SightConfig->LoseSightRadius = Params.LoseSightRadius;
	SightConfig->PeripheralVisionAngleDegrees = Params.PeripheralVisionAngleDegrees;
	SightConfig->DetectionByAffiliation.bDetectEnemies = Params.bDetectEnemies;
	SightConfig->DetectionByAffiliation.bDetectNeutrals = Params.bDetectNeutrals;
	SightConfig->DetectionByAffiliation.bDetectFriendlies = Params.bDetectFriendlies;

	AIPerception->ConfigureSense(*SightConfig);
	AIPerception->SetDominantSense(SightConfig->GetSenseImplementation());
}

void ABaseAIController::StartBehaviorTreeFromProfile()
{
	UE_LOG(LogBaseAI, Log, TEXT("[%s] StartBehaviorTreeFromProfile: enter"), *GetName());

	if (!RuntimeConfig || !RuntimeConfig->GetConfig())
	{
		UE_LOG(LogBaseAI, Warning, TEXT("[%s] StartBehaviorTreeFromProfile: no config"), *GetName());
		return;
	}

	UBehaviorTree* BT = RuntimeConfig->GetConfig()->BehaviorTree.LoadSynchronous();
	UE_LOG(LogBaseAI, Log, TEXT("[%s] StartBehaviorTreeFromProfile: BT=%s"),
		*GetName(), *GetNameSafe(BT));

	if (!BT)
	{
		UE_LOG(LogBaseAI, Warning, TEXT("[%s] Config has no BehaviorTree, fallback"), *GetName());
		RunLegacyBehaviorTree();
		return;
	}

	RunBehaviorTree(BT);
	bBehaviorTreeStarted = true;
	UE_LOG(LogBaseAI, Log, TEXT("[%s] RunBehaviorTree called, BB=%s BBAsset=%s"),
		*GetName(),
		*GetNameSafe(GetBlackboardComponent()),
		*GetNameSafe(BT->GetBlackboardAsset()));

	RegisterBlackboardKeys();
}

/**
 * 派生类入口 (Phase 1 推荐用)
 * 设计: 派生类做完自己专属的感知/GAS/装备配置后调用本方法
 * 内部会复用已 ApplyConfig 的 Config, 不会重复加载
 */
void ABaseAIController::StartBehaviorTreeFromConfig()
{
	if (bBehaviorTreeStarted)
	{
		return;
	}
	StartBehaviorTreeFromProfile();
}

void ABaseAIController::RunLegacyBehaviorTree()
{
	// 兼容路径: 没有 Profile 时, 直接看 Pawn 是否在调试 BP 里硬塞了 BT (GameMode 可直接 RunBehaviorTree).
	// 不在 BaseAIController 内硬启动任何 BT — 让外部决定.
	if (!bBehaviorTreeStarted && GetPawn())
	{
		// 仅做 BB key 注册, BT 启动交给外部 GameMode
		RegisterBlackboardKeys();
	}
}

void ABaseAIController::RegisterBlackboardKeys()
{
	UBlackboardComponent* BBComp = GetBlackboardComponent();
	if (!BBComp)
	{
		return;
	}
	UBlackboardData* BBData = BBComp->GetBlackboardAsset();
	if (!BBData)
	{
		return;
	}
	if (UWorld* World = GetWorld())
	{
		if (UAIBlackboardKeyRegistrySubsystem* Registry =
			World->GetSubsystem<UAIBlackboardKeyRegistrySubsystem>())
		{
			Registry->RegisterBlackboardData(BBData);
		}
	}
}

void ABaseAIController::SetDifficultyTier(EAIDifficultyTier NewTier)
{
	if (RuntimeConfig)
	{
		RuntimeConfig->SetDifficultyTier(NewTier);
	}
}

UAIBlackboardKeyRegistrySubsystem* ABaseAIController::GetKeyRegistry() const
{
	if (UWorld* World = GetWorld())
	{
		return World->GetSubsystem<UAIBlackboardKeyRegistrySubsystem>();
	}
	return nullptr;
}


// ==========================================
// 4. 【Phase 1 重构】阵营协议实现
// ==========================================
// 设计: AI 也是 IGenericTeamAgentInterface 的实现方.
//       阵营从 Profile.FactionTag 推, 不要再造一份 uint8 TeamID.

void ABaseAIController::SetGenericTeamId(const FGenericTeamId& NewTeamID)
{
	// 把 TeamID 存进 Pawn (让 Pawn 暴露给 AIPerception 直接读)
	if (APawn* MyPawn = GetPawn())
	{
		if (IGenericTeamAgentInterface* PawnAgent = Cast<IGenericTeamAgentInterface>(MyPawn))
		{
			PawnAgent->SetGenericTeamId(NewTeamID);
		}
	}
}

FGenericTeamId ABaseAIController::GetGenericTeamId() const
{
	if (const APawn* MyPawn = GetPawn())
	{
		if (const IGenericTeamAgentInterface* PawnAgent = Cast<IGenericTeamAgentInterface>(MyPawn))
		{
			return PawnAgent->GetGenericTeamId();
		}
	}
	return FGenericTeamId(255); // AI 默认敌对, 直到外部设置
}

ETeamAttitude::Type ABaseAIController::GetTeamAttitudeTowards(const AActor& Other) const
{
	if (const IGenericTeamAgentInterface* OtherAgent = Cast<IGenericTeamAgentInterface>(&Other))
	{
		const FGenericTeamId OtherId = OtherAgent->GetGenericTeamId();
		const FGenericTeamId MyId = GetGenericTeamId();

		// 标准 FGenericTeamId::GetAttitude
		return FGenericTeamId::GetAttitude(MyId, OtherId);
	}
	return ETeamAttitude::Neutral;
}


// ==========================================
// 5. 感知触发入口
// ==========================================
//
// 【Phase 3 大厂架构】极近距离感知回调
//   - 引擎通过 IGenericTeamAgentInterface 把敌我对错处理完, 触发时 Target 必定是敌人
//   - 当敌人进入 OverrideBTDistance 范围（极近距离），写入 BB: NearbyThreat
//   - NearbyThreat 的优先级高于 TargetActor（BT 遭遇优先响应）
//   - 若敌人同时是 TargetActor，NearbyThreat 覆盖写入（BT Service 下一帧刷新会重新仲裁）

void ABaseAIController::OnTargetDetected(AActor* Actor, FAIStimulus Stimulus)
{
	UE_LOG(LogBaseAI, Log, TEXT("[%s] OnTargetDetected: Actor=%s, bSensed=%d"),
		*GetName(),
		*GetNameSafe(Actor),
		Stimulus.WasSuccessfullySensed() ? 1 : 0);

	if (!Stimulus.WasSuccessfullySensed() || !GetPawn() || !Actor)
	{
		return;
	}

	// 距离阈值从 RuntimeConfig->GetScaledCombat() 读
	// 策划在 DA_MeleeGrunt.Combat.OverrideBTDistance 填; 默认 250cm
	const FAICombatParams CombatParams = RuntimeConfig ? RuntimeConfig->GetScaledCombat() : FAICombatParams();
	const float OverrideDistance = CombatParams.OverrideBTDistance;

	const float Distance = FVector::Dist(Actor->GetActorLocation(), GetPawn()->GetActorLocation());
	if (Distance >= OverrideDistance)
	{
		return; // 超出极近距离阈值，不写入 NearbyThreat
	}

	UBlackboardComponent* BB = GetBlackboardComponent();
	if (!BB)
	{
		return;
	}

	// 【Phase 3 大厂架构】Key 名走命名空间常量（取代旧 ImmediateTarget）
	const FName Key = AIBlackboardKeyNames::NearbyThreat;
	BB->SetValueAsObject(Key, Actor);
}
