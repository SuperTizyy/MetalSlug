// 版权声明：在项目设置的描述页面填写您的版权信息。

// ==========================================
// 头文件包含区
// ==========================================
// 引入本类头文件
#include "Systems/RoomGameMode.h"

// 引入在线会话设置（用于配置 Session 选项）
#include "OnlineSessionSettings.h"

// 引入在线子系统（用于创建/管理网络会话）
#include "OnlineSubsystem.h"

// 引入房间玩家控制器（用于类型转换）
#include "Systems/RoomPlayerController.h"

// 【P0】URoomService::BroadcastPlayerJoined/Left 事件广播
#include "Services/RoomService.h"

// 引入 World 头文件（用于获取 World 实例）
#include "Engine/World.h"

// 引入在线会话接口（用于实现 Session 的增删查）
#include "Interfaces/OnlineSessionInterface.h"

// 引入角色基类
#include "Characters/BaseCharacter.h"

// 引入 PlayerStart（出生点）
#include "GameFramework/PlayerStart.h"

// 引入 PlayerState 基类
#include "GameFramework/PlayerState.h"

// 引入 Kismet 静态函数库（用于 OpenLevel / GetAllActorsOfClass 等）
#include "Kismet/GameplayStatics.h"

// 引入房间相关枚举
#include "Data/Enums/CombatEnums.h"
#include "Data/Enums/RoomEnums.h"
#include "Data/Tables/CharacterTableRow.h"

// 引入武器基类
#include "Weapons/BaseWeapon.h"

// 【Phase 1 新增】MeleeAIController (AI 真实生成需要 Spawn)
#include "Systems/MeleeAIController.h"

// 引入 GameFlowSubsystem（流程大管家）
#include "Systems/GameFlowSubsystem.h"

// 引入房间 GameState
#include "Systems/RoomGameState.h"

// 引入自定义 HUD 类
#include "UI/MyGameHUD.h"

// 引入房间 PlayerState
#include "Systems/Core/RoomPlayerState.h"

// 引入胶囊体组件（用于获取角色位置）
#include "Components/CapsuleComponent.h"

// 【Phase 2】AI 数据驱动层
#include "Systems/BaseAIController.h"
#include "Systems/AI/AIRuntimeConfigComponent.h"
// 【v54 大厂架构重构】UAIProfileAsset 已删除, 不再 include
#include "Data/AI/AIBehaviorConfigSO.h"
#include "Data/Config/PlayerConfigAsset.h"
#include "GameFramework/PlayerController.h"
#include "AIController.h"

// 【v31.5 大厂架构】四个 Room Subsystem (从 RoomGameMode 拆出的业务下沉层)
#include "Systems/Lifecycle/RoomLifecycleSubsystem.h"
#include "Systems/Membership/RoomMembershipSubsystem.h"
#include "Systems/Spawn/RoomSpawnSubsystem.h"
#include "Systems/Targeting/RoomTargetingSubsystem.h"


// ==========================================
// 1. 构造函数
// ==========================================

/**
 * ARoomGameMode 构造函数
 *
 * 目的: 配置默认的玩家类、控制器类、HUD 类等
 * 时机: 在游戏进入战斗地图、GameMode 被实例化时由引擎自动调用
 */
ARoomGameMode::ARoomGameMode(const FObjectInitializer& ObjectInitializer)
	: Super(ObjectInitializer)
{
	// 【强行关闭无缝漫游】
	// 在基础局域网测试中，开启无缝漫游容易导致 Spawn 时序错乱，纯属自找麻烦
	bUseSeamlessTravel = false;

	// 【新增初始化】: 默认大厅等待状态，强行开启跳过测试开关
	// 目的: 开发期不配置房间也能直接开打
	CurrentRoomState = ERoomState::WaitingInRoom;
	bSkipRoomPhaseForTesting = true;
	bBattleStartedBroadcasted = false;

	// 配置引擎的标准框架类
	GameStateClass = ARoomGameState::StaticClass();
	PlayerStateClass = ARoomPlayerState::StaticClass();

	// 【核心修复】: 必须显式指定战斗地图使用的 PlayerController 类
	// 如果不设置，引擎会复用 L_Login 地图的 ALoginPlayerController，
	// 导致客户端无法正常生成玩家，引发 "Couldn't spawn player" 崩溃
	PlayerControllerClass = ARoomPlayerController::StaticClass();

	// 必须从底层硬编码绑定默认的 HUD 类，确保 MyGameHUD 会伴随玩家出生
	HUDClass = AMyGameHUD::StaticClass();

	// 【P0 2026.07.07 v9 大厂架构】启动期单元自检
	// 距离决策函数 ComputeArrivalDecision 的 12 个状态组合全部跑一遍
	// 失败立即 Error 日志, 但不影响游戏启动 (避免游戏卡住)
	// 目的: 任何 v9 状态机改错, 启动期就能发现, 不用等 PIE 看 AI 行为
	ABaseAIController::SelfTestArrivalDecision();
}


// ==========================================
// v31.3 P0: GameMode → Subsystem 数据注入
// ==========================================

/**
 * InitGame - UE override, World 已就绪后第一次调用
 *   - 这是把 GameMode 配置注入 Subsystem 的最早安全时机
 */
void ARoomGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);
	InjectSubsystemConfigs();
}

/**
 * PostInitProperties - UE override, 属性构造后调用
 *   - 保险措施: 编辑器实例化时也注入 (用于 Subsystem 在 CDO 阶段被访问的场景)
 */
void ARoomGameMode::PostInitProperties()
{
	Super::PostInitProperties();
	// 注意: PostInitProperties 可能在 World 不存在时调用, 内部 Get() 会失败
	// InitGame 是主入口, 这里只是兜底
	if (UWorld* World = GetWorld())
	{
		InjectSubsystemConfigs();
	}
}

/**
 * InjectSubsystemConfigs - 把 GameMode 配置 (DT / AI Profile / ModeRules) 注入 Subsystem
 *
 * 真理源设计 (v31.3):
 *   - 编辑器面板: GameMode UPROPERTY (策划在 BP_RoomGameMode 拖入)
 *   - 运行时真理: Subsystem 内部副本 (Fast 访问, 不走 UPROPERTY 反序列化)
 *   - 流向: GameMode → Subsystem (单向, 只在 InitGame 时一次性注入)
 *
 * 【v54 大厂架构重构 — 删除 Profile 注入】
 *   - ProfilesByMode / DefaultProfileTag 字段已删除
 *   - UAIProfileAsset 整个类已删除
 *   - 关卡预放 AI 走 ConfigSO (BaseAIController.GetConfig()), 不经过 Subsystem 中转
 *   - 大厅入队 AI 走 Request (UI 直接传所有参数), 不经过 Subsystem 反查
 *   - 这里只注入 ModeRules (运行时需要)
 *
 * 不注入的后果 (v55):
 *   - SpawnSubsystem.ModeRules 为空 → 大厅 AI Spawn 时 AIControllerClass / BehaviorTree 为空 → 拒绝 Spawn
 *   - SpawnSubsystem.CharacterDataTable = null → HandlePlayerRequestSpawn 查表失败
 *   - SpawnSubsystem.WeaponDataTable = null → 武器 Spawn 失败
 */
void ARoomGameMode::InjectSubsystemConfigs()
{
	URoomSpawnSubsystem* SpawnSys = URoomSpawnSubsystem::Get(this);
	if (!SpawnSys)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[RoomGameMode] InjectSubsystemConfigs: URoomSpawnSubsystem 不可用 (World 未就绪), 跳过注入"));
		return;
	}

	// 1. 注入数据表 (DT)
	SpawnSys->SetCharacterDataTable(CharacterDataTable);
	SpawnSys->SetWeaponDataTable(WeaponDataTable);

	// 【v41 大厂架构】注入玩家角色战斗参数配置资产
	SpawnSys->SetPlayerConfigAsset(PlayerConfigAsset.LoadSynchronous());

	// 2. 【v55 大厂架构重构】注入 ModeRules (AIControllerClass + BehaviorTree 已移到这里 — DefaultControllerClass 已删除)
	SpawnSys->SetModeRules(ModeRulesByMode);

	// 3. 注入复活延迟秒数 (3 秒倒计时无敌期)
	SpawnSys->SetRespawnDelaySeconds(RespawnDelaySeconds);

	// 【v56 诊断日志】打印 GM 的 ModeRulesByMode 内容
	FString ModeRulesDump;
	for (const auto& Pair : ModeRulesByMode)
	{
		if (!ModeRulesDump.IsEmpty()) ModeRulesDump += TEXT("; ");
		ModeRulesDump += FString::Printf(TEXT("Mode=%d(BehaviorTree=%s, AIClass=%s)"),
			(int32)Pair.Key,
			*GetNameSafe(Pair.Value.BehaviorTree.Get()),
			*GetNameSafe(Pair.Value.AIControllerClass));
	}

	UE_LOG(LogTemp, Display,
		TEXT("[RoomGameMode][v56-Diag] InjectSubsystemConfigs: GM_ModeRulesByMode 详情: [%s]"),
		*ModeRulesDump);

	UE_LOG(LogTemp, Log,
		TEXT("[RoomGameMode] InjectSubsystemConfigs 完成: DT=%s/%s ModeRules=%d 条 复活延迟=%.2fs (【v54】Profile 字段已删除)"),
		*GetNameSafe(CharacterDataTable),
		*GetNameSafe(WeaponDataTable),
		ModeRulesByMode.Num(),
		RespawnDelaySeconds);
}


// ==========================================
// 2. 玩家管理
// ==========================================

/**
 * AddPlayerToRoom - v31.5 Refactored - delegates to URoomMembershipSubsystem
 *
 * 大厂原则 (v31.5):
 *   - PlayerStateClass 从 this->PlayerStateClass 传递 (GameMode 唯一真理源)
 *   - 不在 Subsystem 内部访问"不存在的字段"
 */
void ARoomGameMode::AddPlayerToRoom(AController* RequestingController, const FString& PlayerName)
{
	if (URoomMembershipSubsystem* MemSys = URoomMembershipSubsystem::Get(this))
	{
		MemSys->AddPlayerToRoom(RequestingController, PlayerName, PlayerStateClass);
	}
}


/**
 * ChangePlayerTeam - v31.1 Refactored - delegates to URoomMembershipSubsystem
 */
void ARoomGameMode::ChangePlayerTeam(AController* RequestingController, bool bToAttackTeam)
{
	if (URoomMembershipSubsystem* MemSys = URoomMembershipSubsystem::Get(this))
	{
		MemSys->ChangePlayerTeam(RequestingController, bToAttackTeam);
	}
}


/**
 * RemovePlayerFromRoom - v31.1 Refactored - delegates to URoomMembershipSubsystem
 */
void ARoomGameMode::RemovePlayerFromRoom(AController* RequestingController)
{
	if (URoomMembershipSubsystem* MemSys = URoomMembershipSubsystem::Get(this))
	{
		MemSys->RemovePlayerFromRoom(RequestingController);
	}
}


/**
 * BroadcastChatMessage - v31.1 Refactored - delegates to URoomMembershipSubsystem
 */
void ARoomGameMode::BroadcastChatMessage(const FString& SenderName, const FString& Message)
{
	if (URoomMembershipSubsystem* MemSys = URoomMembershipSubsystem::Get(this))
	{
		MemSys->BroadcastChatMessage(SenderName, Message);
	}
}


/**
 * BroadcastSystemMessage - v31.1 Refactored - delegates to URoomMembershipSubsystem
 */
void ARoomGameMode::BroadcastSystemMessage(const FString& Message)
{
	if (URoomMembershipSubsystem* MemSys = URoomMembershipSubsystem::Get(this))
	{
		MemSys->BroadcastSystemMessage(Message);
	}
}


/**
 * GetModeRules - v31.2 delegates to URoomSpawnSubsystem
 */
bool ARoomGameMode::GetModeRules(ERoomMatchMode Mode, FAIModeRules& OutRules) const
{
	if (URoomSpawnSubsystem* SpawnSys = URoomSpawnSubsystem::Get(this))
	{
		return SpawnSys->GetModeRules(Mode, OutRules);
	}
	return false;
}


/**
 * SpawnAIInternal - v31.2 delegates to URoomSpawnSubsystem
 *
 * 【v54 大厂架构重构】参数从 Profile 改 Config (UAIBehaviorConfigSO)
 */
int32 ARoomGameMode::SpawnAIInternal(const FAISpawnRequest& Request, UAIBehaviorConfigSO* Config, AAIController* OptionalExistingController)
{
	if (URoomSpawnSubsystem* SpawnSys = URoomSpawnSubsystem::Get(this))
	{
		return SpawnSys->SpawnAIInternal(Request, Config, OptionalExistingController);
	}
	return 0;
}


/**
 * UpdatePlayerReadyState - v31.2 delegates to URoomMembershipSubsystem
 */
void ARoomGameMode::UpdatePlayerReadyState(AController* RequestingController, bool bIsReady)
{
	if (URoomMembershipSubsystem* MemSys = URoomMembershipSubsystem::Get(this))
	{
		MemSys->UpdatePlayerReadyState(RequestingController, bIsReady);
	}
}

/**
 * QueueAIForBattleSpawn - v31.2 delegates to URoomSpawnSubsystem
 */
int32 ARoomGameMode::QueueAIForBattleSpawn(const FAISpawnRequest& Request)
{
	if (URoomSpawnSubsystem* SpawnSys = URoomSpawnSubsystem::Get(this))
	{
		return SpawnSys->QueueAIForBattleSpawn(Request);
	}
	return 0;
}


/**
 * GetPendingAIInFaction - v31.2 delegates to URoomSpawnSubsystem
 */
TArray<FPendingAIEntry> ARoomGameMode::GetPendingAIInFaction(FGameplayTag FactionTag) const
{
	if (URoomSpawnSubsystem* SpawnSys = URoomSpawnSubsystem::Get(this))
	{
		return SpawnSys->GetPendingAIInFaction(FactionTag);
	}
	return TArray<FPendingAIEntry>();
}


/**
 * ConsumePendingAIForBattleSpawn - v31.2 delegates to URoomSpawnSubsystem
 */
TArray<FAISpawnRequest> ARoomGameMode::ConsumePendingAIForBattleSpawn()
{
	if (URoomSpawnSubsystem* SpawnSys = URoomSpawnSubsystem::Get(this))
	{
		return SpawnSys->ConsumePendingAIForBattleSpawn();
	}
	return TArray<FAISpawnRequest>();
}


/**
 * 【v54 大厂架构重构 — 已删除】ResolveProfileExact
 *
 * 历史 (v53 及之前):
 *   - 这是 RoomGameMode → RoomSpawnSubsystem 的委派
 *   - 内部走 SpawnSubsystem->ResolveProfileExact(Mode, ProfileTag)
 *
 * v54 重构 (用户决策 2026.07.16):
 *   - UAIProfileAsset 已删除, FAIProfileRegistry 已删除, ResolveProfileExact 已删除
 *   - 这个委派函数整个删除 — 调用方如果有, 必须改成走 ConfigSO
 */


/**
 * BuildSpawnRequestFromPending - v31.2 delegates to URoomSpawnSubsystem
 */
FAISpawnRequest ARoomGameMode::BuildSpawnRequestFromPending(const FPendingAIEntry& Entry) const
{
	if (URoomSpawnSubsystem* SpawnSys = URoomSpawnSubsystem::Get(this))
	{
		return SpawnSys->BuildSpawnRequestFromPending(Entry);
	}
	return FAISpawnRequest();
}


/**
 * IsPendingAIByName - v31.2 delegates to URoomSpawnSubsystem
 */
bool ARoomGameMode::IsPendingAIByName(const FString& DisplayName) const
{
	if (URoomSpawnSubsystem* SpawnSys = URoomSpawnSubsystem::Get(this))
	{
		return SpawnSys->IsPendingAIByName(DisplayName);
	}
	return false;
}


/**
 * RemovePendingAIByName - v31.2 delegates to URoomSpawnSubsystem
 */
bool ARoomGameMode::RemovePendingAIByName(const FString& DisplayName)
{
	if (URoomSpawnSubsystem* SpawnSys = URoomSpawnSubsystem::Get(this))
	{
		return SpawnSys->RemovePendingAIByName(DisplayName);
	}
	return false;
}


/**
 * GetAllPendingAI - v50 delegates to URoomSpawnSubsystem
 *
 * 历史: v28-v49 直接返回 GameMode 自身 PendingAIQueue 字段
 * v50: GameMode 不再持有 PendingAIQueue 字段, 委派给 Subsystem
 */
TArray<FPendingAIEntry> ARoomGameMode::GetAllPendingAI() const
{
	if (URoomSpawnSubsystem* SpawnSys = URoomSpawnSubsystem::Get(this))
	{
		return SpawnSys->GetAllPendingAI();
	}
	return TArray<FPendingAIEntry>();
}


/**
 * RequestTargetForAI - v31.2 delegates to URoomTargetingSubsystem
 */
ABaseCharacter* ARoomGameMode::RequestTargetForAI(ABaseCharacter* RequestingAI)
{
	if (!RequestingAI) return nullptr;
	if (URoomTargetingSubsystem* TargetingSys = URoomTargetingSubsystem::Get(this))
	{
		TArray<ABaseCharacter*> Candidates = TargetingSys->GetAllAliveEnemiesFor(RequestingAI);
		return TargetingSys->RequestTargetForAI(RequestingAI, Candidates);
	}
	return nullptr;
}

/**
 * GetEffectiveHuntPolicy - v31.2 delegates to URoomTargetingSubsystem
 */
FAIHuntPolicy ARoomGameMode::GetEffectiveHuntPolicy(ABaseCharacter* AI) const
{
	if (URoomTargetingSubsystem* TargetingSys = URoomTargetingSubsystem::Get(this))
	{
		return TargetingSys->GetEffectiveHuntPolicy(AI);
	}
	return FAIHuntPolicy();
}

/**
 * ScoreCandidateForAI - v31.2 delegates to URoomTargetingSubsystem (3-arg signature)
 *
 * 大厂原则 (v31.5):
 *   - 候选敌人必须 const (Subsystem 内部不修改 Candidate 状态, 仅读)
 *   - 头文件声明同步改为 const ABaseCharacter*, 与 cpp 委派签名一致
 */
float ARoomGameMode::ScoreCandidateForAI(ABaseCharacter* RequestingAI,
	const ABaseCharacter* Candidate,
	const FAIHuntPolicy& HuntPolicy) const
{
	if (URoomTargetingSubsystem* TargetingSys = URoomTargetingSubsystem::Get(this))
	{
		return TargetingSys->ScoreCandidateForAI(RequestingAI, const_cast<ABaseCharacter*>(Candidate), HuntPolicy);
	}
	return 0.f;
}


/**
 * GetAllAliveEnemiesFor - v31.2 delegates to URoomTargetingSubsystem
 */
TArray<ABaseCharacter*> ARoomGameMode::GetAllAliveEnemiesFor(ABaseCharacter* RequestingAI)
{
	if (URoomTargetingSubsystem* TargetingSys = URoomTargetingSubsystem::Get(this))
	{
		return TargetingSys->GetAllAliveEnemiesFor(RequestingAI);
	}
	return TArray<ABaseCharacter*>();
}


/**
 * ReleaseTarget - v31.2 delegates to URoomTargetingSubsystem
 */
void ARoomGameMode::ReleaseTarget(ABaseCharacter* RequestingAI)
{
	if (URoomTargetingSubsystem* TargetingSys = URoomTargetingSubsystem::Get(this))
	{
		TargetingSys->ReleaseTarget(RequestingAI);
	}
}


/**
 * GetAttackerCount - v31.2 delegates to URoomTargetingSubsystem
 */
int32 ARoomGameMode::GetAttackerCount(ABaseCharacter* TargetEnemy)
{
	if (URoomTargetingSubsystem* TargetingSys = URoomTargetingSubsystem::Get(this))
	{
		return TargetingSys->GetAttackerCount(TargetEnemy);
	}
	return 0;
}


/**
 * IsTargetLocked - v31.2 delegates to URoomTargetingSubsystem
 */
bool ARoomGameMode::IsTargetLocked(ABaseCharacter* TargetEnemy)
{
	if (URoomTargetingSubsystem* TargetingSys = URoomTargetingSubsystem::Get(this))
	{
		return TargetingSys->IsTargetLocked(TargetEnemy);
	}
	return false;
}


/**
 * IsTargetLockedByOthers - v31.2 delegates to URoomTargetingSubsystem
 */
bool ARoomGameMode::IsTargetLockedByOthers(ABaseCharacter* TargetEnemy, ABaseCharacter* ExcludeAI)
{
	if (URoomTargetingSubsystem* TargetingSys = URoomTargetingSubsystem::Get(this))
	{
		return TargetingSys->IsTargetLockedByOthers(TargetEnemy, ExcludeAI);
	}
	return false;
}


/**
 * CheckAllPlayersReady - v31.2 delegates to URoomMembershipSubsystem
 */
bool ARoomGameMode::CheckAllPlayersReady()
{
	if (URoomMembershipSubsystem* MemSys = URoomMembershipSubsystem::Get(this))
	{
		return MemSys->CheckAllPlayersReady();
	}
	return false;
}


// ==========================================
// 4. 角色/武器生成系统
// ==========================================

/**
 * HandlePlayerRequestSpawn - v31.1 Refactored - delegates to URoomSpawnSubsystem
 *
 * 【v52 P0】3 把武器一起转发 (主+副+近战)
 */
void ARoomGameMode::HandlePlayerRequestSpawn(AController* PlayerToSpawn, const FString& CharRowName, const FString& WeaponPrimaryRowName, const FString& WeaponSecondaryRowName, const FString& WeaponMeleeRowName)
{
	if (URoomSpawnSubsystem* SpawnSys = URoomSpawnSubsystem::Get(this))
	{
		SpawnSys->HandlePlayerRequestSpawn(PlayerToSpawn, CharRowName, WeaponPrimaryRowName, WeaponSecondaryRowName, WeaponMeleeRowName);
	}
}


/**
 * GetDefaultPawnClassForController_Implementation - v31.1 Refactored - delegates to URoomSpawnSubsystem
 */
UClass* ARoomGameMode::GetDefaultPawnClassForController_Implementation(AController* InController)
{
	if (URoomSpawnSubsystem* SpawnSys = URoomSpawnSubsystem::Get(this))
	{
		return SpawnSys->GetDefaultPawnClassForController(InController);
	}
	return nullptr;
}


/**
 * RestartPlayer - v31.1 Refactored - delegates to URoomSpawnSubsystem
 */
void ARoomGameMode::RestartPlayer(AController* NewPlayer)
{
	if (URoomSpawnSubsystem* SpawnSys = URoomSpawnSubsystem::Get(this))
	{
		SpawnSys->RestartPlayer(NewPlayer);
	}
}


/**
 * RequestRespawn - v31.1 Refactored - delegates to URoomSpawnSubsystem
 */
void ARoomGameMode::RequestRespawn(AController* DeadController, bool bImmediateRespawn)
{
	if (URoomSpawnSubsystem* SpawnSys = URoomSpawnSubsystem::Get(this))
	{
		SpawnSys->RequestRespawn(DeadController, bImmediateRespawn);
	}
}


// ==========================================
// 5. 比赛开始流程
// ==========================================

/**
 * RequestStartGame - v31.1 delegates to URoomLifecycleSubsystem
 */
void ARoomGameMode::RequestStartGame(AController* RequestingController)
{
	if (!IsValid(RequestingController))
	{
		return;
	}

	// 1. 身份鉴权
	AController* HostController = GetWorld()->GetFirstPlayerController();
	if (RequestingController != HostController)
	{
		UE_LOG(LogTemp, Warning, TEXT("[RoomGameMode] 拒绝开局请求: 该玩家不是房主!"));
		return;
	}

	// 2. 全员准备校验 (测试开关短路)
	if (!bSkipRoomPhaseForTesting)
	{
		bool bAllReady = false;
		if (URoomMembershipSubsystem* MemSys = URoomMembershipSubsystem::Get(this))
		{
			bAllReady = MemSys->CheckAllPlayersReady();
		}
		if (!bAllReady)
		{
			BroadcastSystemMessage(TEXT("无法开始游戏: 还有玩家未准备就绪!"));
			return;
		}
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("[RoomGameMode] 测试模式开启, 无视准备状态, 强制开局"));
	}

	// 3. 转发到 LifecycleSubsystem (倒计时后回调 SpawnAllPlayersIntoBattle)
	if (URoomLifecycleSubsystem* LifeSys = URoomLifecycleSubsystem::Get(this))
	{
		LifeSys->PerformGameStart(MatchStartDelay, MeleeMatchDurationSeconds,
			FSimpleDelegate::CreateUObject(this, &ARoomGameMode::SpawnAllPlayersIntoBattle));
	}
}


/**
 * PerformGameStart - v31.1 delegates to URoomLifecycleSubsystem
 */
void ARoomGameMode::PerformGameStart()
{
	if (URoomLifecycleSubsystem* LifeSys = URoomLifecycleSubsystem::Get(this))
	{
		LifeSys->PerformGameStart(MatchStartDelay, MeleeMatchDurationSeconds,
			FSimpleDelegate::CreateUObject(this, &ARoomGameMode::SpawnAllPlayersIntoBattle));
	}
}


/**
 * SpawnAllPlayersIntoBattle - v31.2 delegates to URoomSpawnSubsystem
 */
void ARoomGameMode::SpawnAllPlayersIntoBattle()
{
	if (URoomSpawnSubsystem* SpawnSys = URoomSpawnSubsystem::Get(this))
	{
		SpawnSys->SpawnAllPlayersIntoBattle();
	}
}


// ==========================================
// 6. 出生点扫描与分配
// ==========================================

/**
 * ScanAndCachePlayerStarts - v31.2 delegates to URoomSpawnSubsystem
 */
void ARoomGameMode::ScanAndCachePlayerStarts(bool bReScan)
{
	if (URoomSpawnSubsystem* SpawnSys = URoomSpawnSubsystem::Get(this))
	{
		SpawnSys->ScanAndCachePlayerStarts(bReScan);
	}
}


/**
 * ReleaseSpawnPoint - v31.2 delegates to URoomSpawnSubsystem
 */
void ARoomGameMode::ReleaseSpawnPoint(AActor* PlayerStart)
{
	if (URoomSpawnSubsystem* SpawnSys = URoomSpawnSubsystem::Get(this))
	{
		SpawnSys->ReleaseSpawnPoint(PlayerStart);
	}
}


/**
 * GetAvailableSpawnPointForFaction - v31.5 delegates to URoomSpawnSubsystem
 *
 * 历史: v25 之前是 RoomGameMode 直接维护 AttackSpawnPoints/DefenseSpawnPoints
 *       v31 大厂重构拆到 URoomSpawnSubsystem, 但本方法 .h 声明保留作为 BP 兼容入口
 *       v31.5 修复: 补上委派实现, 不再是孤儿声明
 */
/**
 * GetAvailableSpawnPointForFaction - v39 扩展 OccupancyOwner 参数 (零兜底)
 */
AActor* ARoomGameMode::GetAvailableSpawnPointForFaction(FGameplayTag PlayerFactionTag, bool bRemoveOccupied, AController* OccupancyOwner)
{
	if (URoomSpawnSubsystem* SpawnSys = URoomSpawnSubsystem::Get(this))
	{
		return SpawnSys->GetAvailableSpawnPointForFaction(PlayerFactionTag, bRemoveOccupied, OccupancyOwner);
	}

	// 【v39 零兜底】没有 SpawnSubsystem 显式报错, 不允许静默 return nullptr
	UE_LOG(LogTemp, Error,
		TEXT("[RoomGameMode] GetAvailableSpawnPointForFaction: URoomSpawnSubsystem 未找到. "
		     "请检查 GameMode 初始化 (InjectSubsystemConfigs 是否调用)."));
	return nullptr;
}


/**
 * ResetAllSpawnPointOccupancy - v31.2 delegates to URoomSpawnSubsystem
 */
void ARoomGameMode::ResetAllSpawnPointOccupancy()
{
	if (URoomSpawnSubsystem* SpawnSys = URoomSpawnSubsystem::Get(this))
	{
		SpawnSys->ResetAllSpawnPointOccupancy();
	}
}


/**
 * GetPlayerSpawnData - v31.1 Refactored - delegates to URoomSpawnSubsystem
 */
bool ARoomGameMode::GetPlayerSpawnData(uint32 ControllerUniqueID, FString& OutCharID, FString& OutWeaponID) const
{
	if (URoomSpawnSubsystem* SpawnSys = URoomSpawnSubsystem::Get(this))
	{
		return SpawnSys->GetPlayerSpawnData(ControllerUniqueID, OutCharID, OutWeaponID);
	}
	return false;
}


// ==========================================
// 7. 比赛计时器系统
// ==========================================

/**
 * StartMatchTimer - v31.2 delegates to URoomLifecycleSubsystem
 */
void ARoomGameMode::StartMatchTimer()
{
	if (URoomLifecycleSubsystem* LifeSys = URoomLifecycleSubsystem::Get(this))
	{
		LifeSys->StartMatchTimer();
	}
}


/**
 * OnMatchTimerTick - v31.2 delegates to URoomLifecycleSubsystem
 */
void ARoomGameMode::OnMatchTimerTick()
{
	if (URoomLifecycleSubsystem* LifeSys = URoomLifecycleSubsystem::Get(this))
	{
		LifeSys->OnMatchTimerTick();
	}
}


/**
 * HandleMatchTimeOut - v31.2 delegates to URoomLifecycleSubsystem
 */
void ARoomGameMode::HandleMatchTimeOut()
{
	if (URoomLifecycleSubsystem* LifeSys = URoomLifecycleSubsystem::Get(this))
	{
		LifeSys->HandleMatchTimeOut();
	}
}


/**
 * HandleZombieRoundEnd - v31.2 delegates to URoomLifecycleSubsystem
 */
void ARoomGameMode::HandleZombieRoundEnd()
{
	if (URoomLifecycleSubsystem* LifeSys = URoomLifecycleSubsystem::Get(this))
	{
		LifeSys->HandleZombieRoundEnd();
	}
}


/**
 * StartNextZombieRound - v31.2 delegates to URoomLifecycleSubsystem
 */
void ARoomGameMode::StartNextZombieRound()
{
	if (URoomLifecycleSubsystem* LifeSys = URoomLifecycleSubsystem::Get(this))
	{
		LifeSys->StartNextZombieRound();
	}
}


// ==========================================
// 【P0 架构升级】服务端房主变更流程
// ==========================================

/**
 * TransferHostTo - v31.1 Refactored - delegates to URoomMembershipSubsystem
 */
bool ARoomGameMode::TransferHostTo(const FString& NewHostPlayerName)
{
	if (URoomMembershipSubsystem* MemSys = URoomMembershipSubsystem::Get(this))
	{
		return MemSys->TransferHostTo(NewHostPlayerName);
	}
	return false;
}


// ==========================================
// 8. 数据驱动兜底 (2026-07-03 重构)
// ==========================================


