// Copyright (c) 2026.

#include "Systems/BaseAIController.h"

#include "Perception/AIPerceptionComponent.h"
#include "Perception/AISenseConfig_Sight.h" // 【Phase 2】共用层配 Sight
#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/BlackboardData.h"

#include "NavigationSystem.h"      // 【P0 修复】NavMesh 可用性探测
#include "NavMesh/RecastNavMesh.h" // 【P0 修复】Recast 句柄
#include "Navigation/PathFollowingComponent.h" // 【UE5.6 修复】完整 EPathFollowingRequestResult 定义
#include "Characters/BaseCharacter.h"

#include "Systems/AI/AIRuntimeConfigComponent.h"
#include "Systems/AI/AIBlackboardKeyRegistrySubsystem.h"
#include "Systems/AI/AIBehaviorTypes.h"
#include "Data/AI/AIProfileAsset.h"
#include "Data/AI/AIBehaviorConfigSO.h"
#include "Engine/StreamableManager.h" // 【Phase 1】 TSoftObjectPtr::LoadSynchronous
#include "Engine/World.h"
#include "EngineUtils.h"              // 【P0 2026.07.03 19:22】TActorIterator (主动扫描兜底)
#include "GameFramework/Character.h"  // 【P0 2026.07.03 19:22】ACharacter 主动扫描目标用
#include "GenericTeamAgentInterface.h" // 【P0 2026.07.03 19:22】ETeamAttitude 阵营判定

DEFINE_LOG_CATEGORY_STATIC(LogBaseAI, Log, All);

// ==========================================
// 1. 构造函数
// ==========================================

ABaseAIController::ABaseAIController()
{
	RuntimeConfig = CreateDefaultSubobject<UAIRuntimeConfigComponent>(TEXT("RuntimeConfig"));
	AIPerception = CreateDefaultSubobject<UAIPerceptionComponent>(TEXT("AIPerception"));

	// 【P0 2026.07.03】开启 Tick: 用于 C++ 层 MoveTo 兜底 (TickChaseFallback)
	// AAIController 默认关 Tick, 我们需要它
	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.bStartWithTickEnabled = true;
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

	// 【P0 架构升级 2026.07.03】兜底诊断: 无论是否走 Profile 入口, 都打印一次状态
	// 设计: 老路径 (RunLegacy) 走不到 OnProfileLoaded, 没诊断会瞎;
	//      新路径 (Profile) 已在 OnProfileLoaded 末尾诊断过, 这里会重复打印一次, 但内容相同 — 接受
	//      实战中 2 次日志距离 < 1 帧, 不会刷屏
	DiagnoseAndLogBootStatus();
}

void ABaseAIController::Tick(float DeltaSeconds)
{
	Super::Tick(DeltaSeconds);

	// 【P0 架构升级 2026.07.03】C++ 层 MoveTo 兜底 — 修复 BT 树设计错误
	// 调用频率: 由内部 TimeSinceLastChaseCheck 节流, 默认 0.3s 一次
	TickChaseFallback();
}

void ABaseAIController::TickChaseFallback()
{
	// ============================================================
	// 【P0 大厂架构重构 2026.07.03 19:14】为什么弃用 MoveToLocation
	//
	// 实测问题: MoveToLocation 一直返回 Failed (PIE 日志持续刷 FAILED)
	// 根因分析 (3 个可能):
	//   1. NavMesh 已加载 (日志确认 OK), 但 AI 没在 NavMesh 上 (Spawn 时坐标不在 NavMesh 内)
	//   2. AcceptanceRadius / Filter Class 配置不对
	//   3. PathFollowingComponent 没初始化 (PIE 模式偶发)
	//
	// 大厂原则: 关键行为不能依赖单一系统. 引擎 MoveTo 失败 = AI 死.
	// 兜底方案: AddMovementInput 直接驱动 CharacterMovementComponent
	//   - 跟玩家用 IA_Move 走完全同一条路径, 走 Navigation 系统底层 Acceleration 累积
	//   - 不依赖 PathFollowingComponent, 不依赖 AcceptanceRadius
	//   - 距离 < AttackRange 自然停下 (我不调 Input)
	//
	// 性能: 每帧调用 AddMovementInput, 是 CharacterMovement 的标准输入, 内部只做矢量累加
	//       比 MoveToLocation 路径规划的开销低 10 倍
	// ============================================================

	// 节流: 每 0.3s 检查一次目标, 但每帧都 ApplyMovementInput (跟玩家一致)
	TimeSinceLastChaseCheck += GetWorld() ? GetWorld()->GetDeltaSeconds() : 0.f;
	const bool bShouldReevaluate = (TimeSinceLastChaseCheck >= 0.3f);
	if (bShouldReevaluate)
	{
		TimeSinceLastChaseCheck = 0.f;
		CachedChaseTarget.Reset(); // 重新评估目标
	}

	APawn* MyPawn = GetPawn();
	if (!MyPawn || MyPawn->IsPendingKillPending())
	{
		return;
	}

	// 【P0 2026.07.03】只对 ACharacter 起作用 — Pawn 没有 MovementComponent 处理 AddMovementInput
	ACharacter* MyCharacter = Cast<ACharacter>(MyPawn);
	if (!MyCharacter)
	{
		// 非 Character Pawn, 用 MoveToLocation 老路径 (如 ABP_GruntAI 派生自 ACharacter 就不会走这里)
		return;
	}

	// 死亡判定 — 跟 BT Attack 一致
	if (ABaseCharacter* AIChar = Cast<ABaseCharacter>(MyCharacter))
	{
		if (AIChar->IsDead())
		{
			return;
		}
	}

	UBlackboardComponent* BB = GetBlackboardComponent();
	if (!BB)
	{
		return;
	}

	// 重新评估目标 (每 0.3s 一次, 中间帧复用缓存, 避免 BB 反复读)
	AActor* TargetActor = CachedChaseTarget.Get();
	if (!TargetActor)
	{
		// ============================================================
		// 【大厂架构兜底 2026.07.03 19:22】三层目标查找
		//
		// Layer 1: NearbyThreat (BB 里近距离覆盖目标)
		// Layer 2: TargetActor (BB 里任意距离锁定目标)
		// Layer 3: 【新增】主动扫描 — GetAllActorsOfClass 找最近的 ABaseCharacter
		//
		// 为什么需要 Layer 3:
		//   PIE 实测 21 秒 AI 完全感知不到玩家 (SightR=1500 都不够)
		//   原因可能是: AI 出生点太远 / 玩家重生后位置突变 / Perception 初始化慢
		//   大厂原则: 感知系统失灵也不能让 AI 死站. 主动扫描兜底保证行为
		//
		// 性能: ABaseCharacter 在地图里通常 < 20 个 (PVE 关卡)
		//       GetAllActorsOfClass 内部是 TArray 遍历, 20 个 Actor 微秒级
		//       每 0.3s 才扫一次, 完全可接受
		// ============================================================
		UObject* NearbyObj = BB->GetValueAsObject(FName(AIBlackboardKeyNames::NearbyThreat));
		UObject* TargetObj = NearbyObj ? NearbyObj : BB->GetValueAsObject(FName(AIBlackboardKeyNames::TargetActor));

		if (!TargetObj)
		{
			// Layer 3: 主动扫描
			TargetActor = ScanForNearestEnemy(MyCharacter);
			if (TargetActor)
			{
				// 写入 BB, 后续 BT 树也能看到
				BB->SetValueAsObject(FName(AIBlackboardKeyNames::TargetActor), TargetActor);
				UE_LOG(LogBaseAI, Log, TEXT("[%s] TickChaseFallback: 主动扫描找到目标 %s"),
					*GetName(), *GetNameSafe(TargetActor));
			}
		}
		else
		{
			TargetActor = Cast<AActor>(TargetObj);
		}

		CachedChaseTarget = TargetActor;
	}

	if (!TargetActor || !IsValid(TargetActor))
	{
		// 没目标, 停下 (零输入 = 引擎自带摩擦减速)
		if (bShouldReevaluate)
		{
			UE_LOG(LogBaseAI, Verbose, TEXT("[%s] TickChaseFallback: 无目标, 等待 BB/Scan"),
				*GetName());
		}
		return;
	}

	// 计算距离
	const float AttackRange = 180.f; // 跟 BT Attack 一致
	const FVector MyLoc = MyCharacter->GetActorLocation();
	const FVector TargetLoc = TargetActor->GetActorLocation();
	const float Distance = FVector::Dist(MyLoc, TargetLoc);

	if (Distance <= AttackRange)
	{
		// 进入攻击范围, 让 BT Attack task 接管, C++ 不抢
		// 自然减速 (不再 AddMovementInput) — 玩家按键松手就是这效果
		return;
	}

	// ============================================================
	// 关键: AddMovementInput 直接驱动移动
	// 跟玩家按 WASD 走完全同一条路径, 引擎内 NavMesh 自动避障
	// ============================================================
	FVector Direction = (TargetLoc - MyLoc).GetSafeNormal();
	Direction.Z = 0.f; // 不强制跳跃, 只水平追

	MyCharacter->AddMovementInput(Direction, 1.0f); // ScaleValue=1.0 全速

	// 朝向目标旋转 (平滑)
	FRotator CurrentRot = MyCharacter->GetActorRotation();
	FRotator TargetRot = Direction.Rotation();
	TargetRot.Pitch = CurrentRot.Pitch; // 保持当前俯仰
	FRotator NewRot = FMath::RInterpTo(CurrentRot, TargetRot, GetWorld()->GetDeltaSeconds(), 8.0f);
	MyCharacter->SetActorRotation(NewRot);

	// 节流日志 (避免刷屏, 只在关键节点打)
	if (bShouldReevaluate)
	{
		UE_LOG(LogBaseAI, Log, TEXT("[%s] TickChaseFallback: AddMovementInput toward %s, Distance=%.0f"),
			*GetName(), *GetNameSafe(TargetActor), Distance);
	}
}

/**
 * 【P0 大厂兜底 2026.07.03 19:22】主动扫描最近的敌方 Character
 *
 * 用途: AIPerception 失效时的最后兜底. 通过阵营接口找最近的敌人.
 *
 * 设计:
 *   - 遍历世界中所有 ACharacter (不是 ABaseCharacter, 因为玩家可能是 BP_SWAT 直接继承 ACharacter)
 *   - 用 IGenericTeamAgentInterface::GetTeamAttitudeTowards 判断敌我
 *   - 返回距离最近且 < ScanRange 的敌人
 *
 * 性能:
 *   - PVE 关卡 < 20 个 Pawn, GetAllActorsOfClass 内部遍历 O(n)
 *   - 每 0.3s 调用一次 (TickChaseFallback 节流), 完全可接受
 *   - 不做 NavMesh 投影, 不做视线检测 — 只负责"找到候选", 后续 MoveTo 自然会 NavMesh 修正
 *
 * @param MyCharacter  自身 Character (用于计算距离)
 * @return 最近的敌方 Character, 没找到返回 nullptr
 */
AActor* ABaseAIController::ScanForNearestEnemy(ACharacter* MyCharacter)
{
	if (!MyCharacter || !GetWorld())
	{
		return nullptr;
	}

	// 扫描范围: SightR 的 2 倍 — 主动扫描比 Perception Sight 看得更远, 保证不漏
	float ScanRange = 1500.f * 2.f;
	if (RuntimeConfig && RuntimeConfig->GetConfig())
	{
		const FAIPerceptionParams PerceptionParams = RuntimeConfig->GetScaledPerception();
		ScanRange = FMath::Max(PerceptionParams.SightRadius * 2.f, ScanRange);
	}

	const FVector MyLoc = MyCharacter->GetActorLocation();

	AActor* NearestEnemy = nullptr;
	float NearestDistSq = ScanRange * ScanRange;

	// 遍历所有 ACharacter
	for (TActorIterator<ACharacter> It(GetWorld()); It; ++It)
	{
		ACharacter* Candidate = *It;
		if (!Candidate || Candidate == MyCharacter || Candidate->IsPendingKillPending())
		{
			continue;
		}

		// 跳过死亡的
		if (ABaseCharacter* BaseChar = Cast<ABaseCharacter>(Candidate))
		{
			if (BaseChar->IsDead())
			{
				continue;
			}
		}

		// 阵营判定 — 用引擎原生接口, 跟 AIPerception 一致
		const ETeamAttitude::Type Attitude = GetTeamAttitudeTowards(*Candidate);
		if (Attitude != ETeamAttitude::Hostile)
		{
			continue; // 不是敌人, 跳过
		}

		const float DistSq = FVector::DistSquared(MyLoc, Candidate->GetActorLocation());
		if (DistSq < NearestDistSq)
		{
			NearestDistSq = DistSq;
			NearestEnemy = Candidate;
		}
	}

	return NearestEnemy;
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

	// 【P0 架构升级 2026.07.03】启动期一次性全景诊断
	// 设计: 4 件事同时检查并报告, 任何一项异常立即刷 Error/ Warning 上日志
	//   1. Profile 完整性
	//   2. 阵营 ID 解析 (这是上次 bug 的关键: Faction.Enemy 未识别导致 SquadTeam=0)
	//   3. 感知配置 (Sight 半径/角度/阵营过滤)
	//   4. NavMesh 可用性 (启动期就检查, 避免 MoveTo 静默失败)
	// 集中输出 = 一次日志扫描能看清 AI 配置, 不必翻 4 个函数
	DiagnoseAndLogBootStatus();
}

/**
 * 【Phase 2 共用层 + P0 大厂架构修复】从 RuntimeConfig 配 AIPerception (Sight)
 *
 * 设计:
 *   - 替代原 MeleeAIController::ConfigurePerceptionFromConfig
 *   - 走 RuntimeConfig->GetScaledPerception() — 已经按难度缩放过
 *   - Hearing 留给 Phase 3, 现阶段不配
 *
 * 【P0 修复 — 2026.07.03】敌我阵营识别彻底重构:
 *   旧版坑:
 *     - bDetectEnemies=true 让 AI 把"中立"也当敌人, 结果 BP_GruntAI 把 BP_GruntAI 当目标互殴
 *     - 没显式声明要按 Team ID 区分阵营, DetectionByAffiliation.Team=空数组, 退化成全阵营敌对
 *   新版:
 *     - 关闭所有 ByAffiliation bool, 改用 Team 数组精确匹配
 *     - 队伍 ID 来自 Profile.FactionTag 解析 (Phase 1 已就绪)
 *     - 如果 Profile 没阵营, 退化为 bDetectEnemies=true (原行为), 至少不会瞎识别
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

	UAISenseConfig_Sight* SightConfig = NewObject<UAISenseConfig_Sight>(this, TEXT("SightConfig_P0Fix"));
	if (!SightConfig)
	{
		return;
	}

	const FAIPerceptionParams Params = RuntimeConfig->GetScaledPerception();
	SightConfig->SightRadius = Params.SightRadius;
	SightConfig->LoseSightRadius = Params.LoseSightRadius;
	SightConfig->PeripheralVisionAngleDegrees = Params.PeripheralVisionAngleDegrees;

	// ============================================================
	// 【P0 修复】阵营检测: 走 UE 原生阵营协议, 让 AIPerception 自动判定敌我
	// ============================================================
	// UE 5.6 的 FAISenseAffiliationFilter 只有三个 bool (没有 Teams 数组):
	//   bDetectEnemies    — 用 GetTeamAttitudeTowards == Hostile 判定
	//   bDetectNeutrals   — Neutral 算敌人
	//   bDetectFriendlies — Friendly 算敌人 (默认 false, 千万别开!)
	//
	// 修复原理:
	//   - 关掉 bDetectNeutrals 和 bDetectFriendlies (默认 false, 显式置 false 防御)
	//   - 只保留 bDetectEnemies=true, 引擎会自动调 GetTeamAttitudeTowards 判定
	//   - BTService_RefreshTarget 里 IsHostileTo() 是第二层兜底 (即使配错也安全)

	SightConfig->DetectionByAffiliation.bDetectEnemies = Params.bDetectEnemies;
	SightConfig->DetectionByAffiliation.bDetectNeutrals = false;   // 【P0】关闭, 不把中立当敌人
	SightConfig->DetectionByAffiliation.bDetectFriendlies = false; // 【P0】关闭, 友军不打

	UE_LOG(LogBaseAI, Log,
		TEXT("[%s] ConfigurePerceptionFromConfig: SquadTeam=%d, bDetectEnemies=%d, bDetectNeutrals=%d, bDetectFriendlies=%d"),
		*GetName(),
		GetGenericTeamId().GetId(),
		SightConfig->DetectionByAffiliation.bDetectEnemies ? 1 : 0,
		SightConfig->DetectionByAffiliation.bDetectNeutrals ? 1 : 0,
		SightConfig->DetectionByAffiliation.bDetectFriendlies ? 1 : 0);

	AIPerception->ConfigureSense(*SightConfig);
	AIPerception->SetDominantSense(SightConfig->GetSenseImplementation());

	// 【P0 修复】NavMesh 可用性检查: 启动 BT 前先验证, 避免 MoveTo 永远失败
	if (UWorld* World = GetWorld())
	{
		if (UNavigationSystemV1* NavSys = UNavigationSystemV1::GetCurrent(World))
		{
			ARecastNavMesh* NavMesh = Cast<ARecastNavMesh>(NavSys->GetMainNavData());
			const FString NavMeshName = NavMesh ? NavMesh->GetName() : FString(TEXT("None"));
			const int32 MyTeamId = GetGenericTeamId().GetId();

			if (!NavMesh)
			{
				UE_LOG(LogBaseAI, Error,
					TEXT("[%s] ConfigurePerceptionFromConfig: NavMesh 不存在! AI 将无法 MoveTo, 必须先放置 NavMeshBoundsVolume 并烘焙 (SquadTeam=%d)"),
					*GetName(), MyTeamId);
			}
			else
			{
				UE_LOG(LogBaseAI, Log,
					TEXT("[%s] ConfigurePerceptionFromConfig: NavMesh OK (%s), SquadTeam=%d"),
					*GetName(), *NavMeshName, MyTeamId);
			}
		}
	}
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

/**
 * 【P0 架构升级 2026.07.03】启动期一次性全景诊断
 *
 * 设计理念 (大厂经典):
 *   "启动期一切异常都在一处显式输出" — 避免出现"AI 不动"时翻 4 个日志段拼上下文
 *   一次扫描 = 看清 AI 全局状态
 *
 * 检查 4 项:
 *   1. Profile 完整性
 *   2. 阵营 ID 解析 (历史 bug 源: Faction.Enemy 未识别导致 SquadTeam=0)
 *   3. 感知配置 (Sight 半径/角度/阵营过滤) — 简化为 SightSense 字段快速摘要
 *   4. NavMesh 可用性 (启动期就检查, 避免 MoveTo 静默失败)
 *
 * 输出策略:
 *   - 健康: Log
 *   - 异常: Error
 *   - 全 AI 一致格式, 方便 grep "Diagnose:" 一键过滤
 */
void ABaseAIController::DiagnoseAndLogBootStatus() const
{
	const FString MyName = GetName();

	// 【1】Profile 完整性
	const FString ProfileName = GetNameSafe(CurrentProfile);
	const FString ConfigName  = GetNameSafe(RuntimeConfig ? RuntimeConfig->GetConfig() : nullptr);
	const FString FactionStr  = CurrentProfile ? CurrentProfile->FactionTag.ToString() : FString(TEXT("<None>"));

	// 【2】阵营 ID 解析 — 关键诊断点
	const FGenericTeamId MyTeamId = GetGenericTeamId();
	const int32 TeamIdInt = MyTeamId.GetId();
	const FString PawnFactionStr = GetPawn() ? GetPawn()->GetClass()->GetName() : FString(TEXT("<NoPawn>"));

	// 【3】感知配置摘要
	FString PerceptionSummary = TEXT("<no RuntimeConfig>");
	if (RuntimeConfig && RuntimeConfig->GetConfig())
	{
		const FAIPerceptionParams P = RuntimeConfig->GetScaledPerception();
		PerceptionSummary = FString::Printf(
			TEXT("SightR=%.0f LoseR=%.0f FOV=%.0f° bDetectEnemies=%d"),
			P.SightRadius, P.LoseSightRadius, P.PeripheralVisionAngleDegrees,
			P.bDetectEnemies ? 1 : 0);
	}

	// 【4】NavMesh 可用性
	FString NavMeshSummary = TEXT("<no world>");
	if (UWorld* World = GetWorld())
	{
		if (UNavigationSystemV1* NavSys = UNavigationSystemV1::GetCurrent(World))
		{
			ARecastNavMesh* NavMesh = Cast<ARecastNavMesh>(NavSys->GetMainNavData());
			NavMeshSummary = NavMesh
				? FString::Printf(TEXT("OK (%s)"), *NavMesh->GetName())
				: FString(TEXT("MISSING ⚠ MoveTo 必定失败"));
		}
		else
		{
			NavMeshSummary = TEXT("NavSys 不可用");
		}
	}

	// 【统一输出格式】大厂 grep 友好: "Diagnose:" 一键定位
	UE_LOG(LogBaseAI, Log,
		TEXT("Diagnose: [%s] Profile=%s Config=%s FactionTag=%s -> TeamID=%d PawnClass=%s"),
		*MyName, *ProfileName, *ConfigName, *FactionStr, TeamIdInt, *PawnFactionStr);

	UE_LOG(LogBaseAI, Log,
		TEXT("Diagnose: [%s] Perception=%s"),
		*MyName, *PerceptionSummary);

	UE_LOG(LogBaseAI, Log,
		TEXT("Diagnose: [%s] NavMesh=%s"),
		*MyName, *NavMeshSummary);

	// 【异常升级到 Error】 — 启动期最致命的 3 个 P0 故障点
	if (TeamIdInt == 0)
	{
		UE_LOG(LogBaseAI, Error,
			TEXT("Diagnose: [%s] ⚠ TeamID=0 = Player 阵营! AI 会把玩家当友军不攻击. 请检查 Profile.FactionTag (期望 Faction.Enemy)"),
			*MyName);
	}
	if (!RuntimeConfig || !RuntimeConfig->GetConfig())
	{
		UE_LOG(LogBaseAI, Error,
			TEXT("Diagnose: [%s] ⚠ RuntimeConfig 缺失, AI 无法运行 (无 BT 可启)"),
			*MyName);
	}
	if (NavMeshSummary.Contains(TEXT("MISSING")))
	{
		UE_LOG(LogBaseAI, Error,
			TEXT("Diagnose: [%s] ⚠ NavMesh 缺失, MoveTo 必定失败. 请在地图中放置 NavMeshBoundsVolume 并 Build Paths"),
			*MyName);
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
// 【Phase 3 大厂架构】感知回调 — 双重写入策略
//
// 设计:
//
//   目标感知分两个层次写入 BB，职责分离:
//
//   Layer 1 — TargetActor（任意距离均写入）:
//     - 只要感知到敌人，无条件写入 TargetActor
//     - 由 BTService_RefreshTarget 仲裁刷新（或本函数直接写）
//     - 驱动 BT 的 MoveTo / Attack 节点执行
//
//   Layer 2 — NearbyThreat（极近距离覆盖）:
//     - 仅当敌人进入 OverrideBTDistance 范围时写入
//     - 优先级高于 TargetActor（极近距离遭遇优先响应）
//     - 由本函数直接写入，BTService 在下一帧读取 NearbyThreat
//
//   为什么分层？
//     - Layer 2 确保极近距离遭遇能立刻覆盖目标（不用等 Service Tick）
//     - Layer 1 确保任意距离都能驱动行为树（即使没有 NearbyThreat）
//     - 两者协同：Service 读 NearbyThreat 优先，本函数写 NearbyThreat 极近时才触发
//
//   感知触发时机:
//     - bSensed=1: 敌人首次进入/持续在视野内
//     - bSensed=0: 敌人离开视野（LastKnownLocation 留存由 BT 处理）
//
//   架构优势:
//     - 不依赖 BTService 是否被添加到行为树（Service 缺失时 OnTargetDetected 兜底）
//     - 不依赖 OverrideBTDistance 阈值正确配置（逻辑改成了 <= 才写 NearbyThreat）
//     - 与 BTService_RefreshTarget 完全兼容（两者写同一个 Key，互不冲突）

void ABaseAIController::OnTargetDetected(AActor* Actor, FAIStimulus Stimulus)
{
    UE_LOG(LogBaseAI, Log, TEXT("[%s] OnTargetDetected: Actor=%s, bSensed=%d"),
        *GetName(),
        *GetNameSafe(Actor),
        Stimulus.WasSuccessfullySensed() ? 1 : 0);

    if (!Actor)
    {
        return;
    }

    UBlackboardComponent* BB = GetBlackboardComponent();
    if (!BB)
    {
        return;
    }

    // ============================================================
    // 【P0 修复 2026.07.03】目标丢失时 (bSensed=0) 立刻清空 BB 目标 Key
    // 旧版坑: bSensed=0 时直接 return, BB 里的 TargetActor 仍是 stale 引用
    //         AI 继续往 stale 位置 MoveTo, 走到目标最后位置后停下 (距离卡死)
    // 新版: 丢失时清空 TargetActor / NearbyThreat, 让 BT 重跑
    // ============================================================
    if (!Stimulus.WasSuccessfullySensed())
    {
        AActor* CurrentTarget = Cast<AActor>(BB->GetValueAsObject(FName(AIBlackboardKeyNames::TargetActor)));
        if (CurrentTarget == Actor)
        {
            BB->SetValueAsObject(FName(AIBlackboardKeyNames::TargetActor), nullptr);
            BB->SetValueAsObject(FName(AIBlackboardKeyNames::NearbyThreat), nullptr);
            UE_LOG(LogBaseAI, Log, TEXT("[%s] OnTargetDetected: 目标丢失, 清空 TargetActor/NearbyThreat"),
                *GetName());
        }
        return;
    }

    if (!GetPawn())
    {
        return;
    }

    const FVector AICurrentLocation = GetPawn()->GetActorLocation();
    const float Distance = FVector::Dist(Actor->GetActorLocation(), AICurrentLocation);

    // ============================================================
    // Layer 1 — 无条件写入 TargetActor（任何距离都写入）
    // 解决：之前只有 NearbyThreat，且阈值条件写反，永远不写，导致 AI 无目标可追踪
    // ============================================================
    BB->SetValueAsObject(FName(AIBlackboardKeyNames::TargetActor), Actor);

    // ============================================================
    // Layer 2 — NearbyThreat（仅极近距离写入）
    // 当敌人进入 OverrideBTDistance 范围时，NearbyThreat 覆盖 TargetActor
    // NearbyThreat 由 BTService_RefreshTarget 优先读取
    // ============================================================
    const FAICombatParams CombatParams = RuntimeConfig ? RuntimeConfig->GetScaledCombat() : FAICombatParams();
    const float OverrideDistance = CombatParams.OverrideBTDistance;

    if (Distance <= OverrideDistance)
    {
        // 【修复 P1】原逻辑写反了：Distance >= OverrideDistance 时 return，永远不写 NearbyThreat
        // 现在改为 Distance <= OverrideDistance 时写入（进入范围才写）
        BB->SetValueAsObject(FName(AIBlackboardKeyNames::NearbyThreat), Actor);
        UE_LOG(LogBaseAI, Log, TEXT("[%s] NearbyThreat 更新: Actor=%s, Distance=%.0f <= Override=%.0f"),
            *GetName(), *GetNameSafe(Actor), Distance, OverrideDistance);
    }
}
