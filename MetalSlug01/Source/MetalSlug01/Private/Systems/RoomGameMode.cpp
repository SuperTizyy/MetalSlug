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

#include "Pickups/AirdropPickup.h"

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
#include "Data/Config/WeaponSoundMapAsset.h"
#include "GameFramework/PlayerController.h"
#include "AIController.h"

// 【v31.5 大厂架构】四个 Room Subsystem (从 RoomGameMode 拆出的业务下沉层)
#include "Systems/Lifecycle/RoomLifecycleSubsystem.h"
#include "Systems/Membership/RoomMembershipSubsystem.h"
#include "Systems/Spawn/RoomSpawnSubsystem.h"
#include "Systems/Targeting/RoomTargetingSubsystem.h"
// 【v93.1 大厂架构新增】第五个 Subsystem — 母体变异业务权威
#include "Systems/Mother/RoomMotherMutationSubsystem.h"


// ==========================================
// 【v76 大厂架构 — 武器切换音效】访问器实现
// ==========================================
//
// 单一真理源 (与 v37 WeaponAttachmentDataTable 模式完全对称):
//   - GM->WeaponSoundMapAsset 字段 (TSoftObjectPtr, 编辑器配置时不立即加载)
//   - GetWeaponSoundMapAsset() 同步加载并返回
//   - 调用方 (WeaponAttachmentComponent) 拿到 nullptr 时拒绝播放
//
// 大厂原则 - 配置可发现性:
//   - 错误日志明确指出"BP_GM_RoomGameMode → Class Defaults → Room|Audio → Weapon Sound Map Asset"
//   - 缺资产 → 音效系统永远不工作 (UI/手感都不会暴露, 但每次切武器都 Log Error)
//     → 强制策划/程序配置 (零兜底)
//
// 为什么不缓存为强指针字段?
//   - TSoftObjectPtr 是 UE 异步加载标准模式 (Asset Registry + Streamable)
//   - 调用方在切武器时拉一次, 失败立即 Log
//   - 频繁路径上 (Tick/AnimNotify) 永不调用 — 仅在 Server_SwitchToWeaponSlot 触发
//
// @return 同步加载后的 UWeaponSoundMapAsset*, nullptr = 已 Log Error
// ==========================================
UWeaponSoundMapAsset* ARoomGameMode::GetWeaponSoundMapAsset() const
{
	if (WeaponSoundMapAsset.IsNull())
	{
		UE_LOG(LogTemp, Error,
			TEXT("[ARoomGameMode] GetWeaponSoundMapAsset: GM->WeaponSoundMapAsset 未配置. ")
			TEXT("【v76 大厂原则 — 零兜底】必须修复: ")
			TEXT("打开 BP_GM_RoomGameMode → Class Defaults → Room|Audio → Weapon Sound Map Asset ")
			TEXT("→ 创建/选择 DA_WeaponSoundMap.uasset 并赋给此字段. ")
			TEXT("未配 → 切武器时拒绝播放音效, 但会 Log Error 报告根因."));
		return nullptr;
	}

	// 同步加载 (TSoftObjectPtr → 强指针)
	UWeaponSoundMapAsset* Resolved = WeaponSoundMapAsset.LoadSynchronous();
	if (!Resolved)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[ARoomGameMode] GetWeaponSoundMapAsset: WeaponSoundMapAsset 资产路径无效或加载失败. ")
			TEXT("AssetPath=%s. 【v76 零兜底】必须检查 DA_WeaponSoundMap.uasset 是否存在."),
			*WeaponSoundMapAsset.ToString());
		return nullptr;
	}

	return Resolved;
}


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
 *
 *   注意: InitGame 时 GameState **尚未创建** (GameState 在 InitGameState 中创建)
 *         所以写入 GS->CurrentMatchMode 的逻辑在 InitGameState 中 (见下方 override)
 */
void ARoomGameMode::InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage)
{
	Super::InitGame(MapName, Options, ErrorMessage);
	InjectSubsystemConfigs();
}

/**
 * InitGameState - UE override, GameState 已创建后调用
 *
 *   【v93 大厂架构修复】URL ?Mode=数字 → GS->SetCurrentMatchMode
 *   关键时序: InitGameState 时, GameState 已被 AGameModeBase 创建, 是新房间地图的 GS (不是 L_Login 的旧 GS)
 *   解析 URL Options 中的 ?Mode= 参数, 写入 GS CurrentMatchMode
 *   - Melee = 1, Zombie = 2
 *   - None 或缺省 → Log Error + 拒绝写入 (零兜底)
 *   - 写入后立即 Broadcast OnMatchModeChanged (镜像 SetTotalRounds 模式)
 *
 * 大厂原则 (Single Source of Truth):
 *   - URL Options 里的 Mode 字段 (由 GameFlowSubsystem::HandleStateEntry InRoom 写入) 是房间模式真理源
 *   - GS->CurrentMatchMode 是运行时决策层真理源
 *   - InitGameState 是这两个真理源之间的唯一桥梁
 *
 * 测试模式路径:
 *   - BootToLogin skip-login 分支不调 OpenLevel, GameState 已存在, 直接 GS->SetCurrentMatchMode (旧路径)
 *   - 正式路径: 通过 URL ?Mode= 跨地图传递, InitGameState 解析
 *   - 两条路径都最终通过 SetCurrentMatchMode 公开 API 写入 (单一真理源)
 */
void ARoomGameMode::InitGameState()
{
	Super::InitGameState();

	// 【v93 大厂架构】解析 URL Options 中的 ?Mode= 参数, 写入 GameState->CurrentMatchMode
	//   单一真理源: URL Options 里的 Mode 字段 (由 GameFlowSubsystem::HandleStateEntry InRoom 写入)
	//   没有兜底: 缺省 / 非法 / None → Log Error + 拒绝写入
	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[RoomGameMode] InitGameState: World 为 null, 跳过 URL ?Mode= 解析."));
		return;
	}
	ARoomGameState* GS = World->GetGameState<ARoomGameState>();
	if (!GS)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[RoomGameMode] InitGameState: GameState 不是 ARoomGameState (或为 null). "
			     "GS->SetCurrentMatchMode 无法调用. "
			     "【修复】检查 World Settings → GameMode Override / Default GameMode."));
		return;
	}

	// 解析 URL Options (注意: UE 标准接口是 OptionsString 字段, 不是 GetGameModeURLOptions)
	//   来源: AGameModeBase 在 InitGame(MapName, Options, ...) 中接收 URL Options 并保存到 OptionsString
	//   InitGameState 在 InitGame 之后被引擎自动调用, 此时 OptionsString 已有值
	const FString& Options = OptionsString;
	const FString ModeStr = UGameplayStatics::ParseOption(Options, TEXT("Mode"));
	if (ModeStr.IsEmpty())
	{
		UE_LOG(LogTemp, Error,
			TEXT("[RoomGameMode] InitGameState: URL Options 缺少 'Mode' 参数 (Options='%s'). "
			     "GS->SetCurrentMatchMode 拒绝写入 None. "
			     "【修复】GameFlowSubsystem::HandleStateEntry InRoom 必须在 OpenLevel 时附加 ?Mode=数字. "
			     "【业务根因】LANRoomPage::OnConfirmCreateRoomClicked 必须先调 FlowSub->SetTargetRoomMode(ERoomMatchMode)."),
			*Options);
		return;
	}
	const int32 ModeInt = FCString::Atoi(*ModeStr);
	const ERoomMatchMode ParsedMode = static_cast<ERoomMatchMode>(ModeInt);

	// 显式校验 (拒绝 None / 非法值, 大厂原则零兜底)
	if (ParsedMode == ERoomMatchMode::None || ModeInt < 1 || ModeInt > 2)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[RoomGameMode] InitGameState: URL 'Mode=%s' (整数=%d) 不是合法 ERoomMatchMode. "
			     "合法值: 1=Melee (刀战模式), 2=Zombie (生化模式). "
			     "拒绝写入 GS->SetCurrentMatchMode."),
			*ModeStr, ModeInt);
		return;
	}

	// 单一真理源写入 (镜像 Skip-Login 路径)
	GS->SetCurrentMatchMode(ParsedMode);
	UE_LOG(LogTemp, Log,
		TEXT("[RoomGameMode] InitGameState: URL ?Mode=%s → GS->SetCurrentMatchMode(%s) 成功."),
		*ModeStr, ParsedMode == ERoomMatchMode::Melee ? TEXT("Melee") : TEXT("Zombie"));

	// ==========================================
	// 【v201.12 大厂架构修复】补注入 GS 字段 (TotalRounds / ZombieMatchDuration 等)
	// ==========================================
	//
	// 根因 (v201.11 之前):
	//   - InitGame 时 GS 尚未创建 → LifeSys->SetTotalRounds(GetGameState === null) → Log Error + 拒绝写入
	//   - 日志证据: "[RoomLifecycle] SetTotalRounds: GameState 为空, 拒绝注入." (4 次)
	//   - 结果: GS.TotalRounds 永远 = GameState.h 默认值 5, 策划在 GM_RoomGameMode 改 TotalRounds=10 → 无效
	//   - 用户反馈 (2026.08.06): "GM_RoomGameMode 的 TotalRounds 我设置了, 但是生化模式进游戏的总局数还是不按照 GM_RoomGameMode 的 TotalRounds 设置来"
	//
	// 修复:
	//   - InitGameState 末尾 (GS 已创建) → 显式调 GS->SetTotalRounds / SetZombieMatchDuration
	//   - 这是在 GS 创建后唯一安全的注入时机
	//
	// 大厂原则:
	//   - 单一真理源: GM.TotalRounds 是策划配置, GS.TotalRounds 是运行时, 数据单向流 (GM → GS)
	//   - 零兜底: < 1 已经 GameState 校验过 (ClampMin=1), 这里直接写入
	//   - 显式优于隐式: 命名明确指出"补注入" 而非"重新注入"
	if (URoomLifecycleSubsystem* LifeSys = URoomLifecycleSubsystem::Get(this))
	{
		LifeSys->SetTotalRounds(TotalRounds);
		LifeSys->SetZombieMatchDuration(ZombieMatchDurationSeconds);
		UE_LOG(LogTemp, Log,
			TEXT("[RoomGameMode] 【v201.12】InitGameState 补注入: GS 已创建, "
				 "SetTotalRounds=%d, SetZombieMatchDuration=%ds."),
			TotalRounds, ZombieMatchDurationSeconds);
	}
	else
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[RoomGameMode] InitGameState: URoomLifecycleSubsystem 不可用, 跳过 TotalRounds 补注入."));
	}

	// 【v210.4 大厂架构重构 — 删除 CacheZombieRoundSounds】
	//   旧 (v210.2 / v210.3): InitGameState 末尾调 GS->CacheZombieRoundSounds 把音效复制到 GS + Replicate
	//   根因 (v210.4): InitGameState 有 4 处 early-return 跳过这段代码, 实际 0% 调用成功
	//     即使注入成功, Replicated UObject* / TSoftObjectPtr 在 UE 5.6 中仍可能 GC 误删
	//   新方案: 唯一真理源 = GameMode->ZombieHumanWinSound (策划唯一配置点)
	//   音效通过 RPC FSoftObjectPath 跨网络传输, InitGameState 完全不参与
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
	// 【v93.1 重构】变量提升到函数顶部, 让下面 4 个 Subsystem 注入块共享 (避免作用域 bug)
	URoomSpawnSubsystem* SpawnSys = URoomSpawnSubsystem::Get(this);
	URoomLifecycleSubsystem* LifeSys = URoomLifecycleSubsystem::Get(this);

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

	// 4. 【v92 大厂架构新增】注入母体变异倒计时秒数到 LifecycleSubsystem
	// 单一真理源: GameMode.MotherMutationDurationSeconds → Subsystem.MotherMutationDurationSeconds
	// 流向: GameMode → Subsystem (单向, 只在 InitGame 时一次性注入)
	//
	// 【v92 大厂架构重构】注入 TotalRounds 到 LifecycleSubsystem (转发到 GameState):
	//   - GameMode.TotalRounds (策划配置) → Subsystem.SetTotalRounds → GameState.TotalRounds (Replicated)
	//   - UI 显示用 GameState.TotalRounds, 单一真理源
	//   - 取代旧的 ZombieTotalRounds (已删除)
	//
	// 【v92 大厂架构重构】注入 ZombieMatchDurationSeconds:
	//   - GameMode.ZombieMatchDurationSeconds (策划配置) → Subsystem.ZombieMatchDurationSeconds
	//   - StartMatchTimer (Zombie 分支) 用此值写入 GameState.MatchEndTime
	//   - 与 MeleeMatchDurationSeconds 对称 (两模式各自配置每局时长)
	if (LifeSys)
	{
		LifeSys->SetMotherMutationDuration(MotherMutationDurationSeconds);
		LifeSys->SetAirdropInterval(AirdropIntervalSeconds);
		LifeSys->SetTotalRounds(TotalRounds);
		LifeSys->SetZombieMatchDuration(ZombieMatchDurationSeconds);

		// 【v108 大厂架构新增】注入母体变异数量 + 目标选择策略
		// 数据流: GM → Subsystem (InitGame 一次性, 改配置需重启游戏 — 用户决策)
		// 读取方: URoomMotherMutationSubsystem::HandleCountdownExpired (SetTimer 回调, 此刻需要访问)
		LifeSys->SetMotherMutationCount(MotherMutationCount);
		LifeSys->SetMotherSelectionPolicy(MotherSelectionPolicy);

		UE_LOG(LogTemp, Log,
			TEXT("[RoomGameMode] 注入 LifecycleSubsystem: MotherMutationDuration=%.2fs, AirdropInterval=%.2fs, TotalRounds=%d, ZombieMatchDuration=%ds, MotherMutationCount=%d, MotherSelectionPolicy=%d"),
			MotherMutationDurationSeconds, AirdropIntervalSeconds, TotalRounds, ZombieMatchDurationSeconds,
			MotherMutationCount, static_cast<int32>(MotherSelectionPolicy));
	}
	else
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[RoomGameMode] InjectSubsystemConfigs: URoomLifecycleSubsystem 不可用 (World 未就绪), 跳过 LifecycleSubsystem 注入"));
	}

	// 【v93.1 大厂架构新增】注入 MotherMutationSubsystem — 母体变异业务权威调度
	// 大厂原则 — 对称设计: 与上述 4 个 Subsystem 注入流程一致
	//   - GameMode 找到 Subsystem (URoomMotherMutationSubsystem::Get(this))
	//   - 调 InitializeSubsystem() 注入依赖 (GameMode / Lifecycle / Spawn)
	//   - 大厂原则 — 显式依赖注入, 不允许 lazy 解析 (避免时序 bug)
	//
	// 为什么独立注入:
	//   - MotherMutationSubsystem 业务有 3 个依赖 (GameMode / Lifecycle / Spawn)
	//   - Lifecycle 和 Spawn 已经在上面获取过, 这里直接复用
	//   - 即使 Lifecycle / Spawn 为空, 也要 InitializeSubsystem 注入 (MotherMutationSubsystem 内部已防御)
	if (URoomMotherMutationSubsystem* MutationSys = URoomMotherMutationSubsystem::Get(this))
	{
		// 复用上面已经获取的 SpawnSys / LifeSys (可能为 nullptr, MotherMutationSubsystem 内部 Log Error + 容忍)
		MutationSys->InitializeSubsystem(this, LifeSys, SpawnSys);

		UE_LOG(LogTemp, Log,
			TEXT("[RoomGameMode] 注入 MotherMutationSubsystem: 依赖注入完成 (GameMode=%s Lifecycle=%s Spawn=%s)"),
			*GetNameSafe(this), *GetNameSafe(LifeSys), *GetNameSafe(SpawnSys));
	}
	else
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[RoomGameMode] InjectSubsystemConfigs: URoomMotherMutationSubsystem 不可用 (World 未就绪), 跳过 MotherMutationSubsystem 注入"));
	}
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


// ==========================================
// 【v134 大厂架构新增 → v210.4 已废弃】生化小局结算音效查表
// ==========================================
//
// 【v210.4 大厂架构重构 — 废弃 ResolveZombieRoundEndSound】
//   旧 (v134 - v210.3): GM 提供 ResolveZombieRoundEndSound(RoundWinner) 集中决策
//   根因: 客户端 World->GetAuthGameMode() 返回 nullptr, 此函数在客户端永远走 fallback 路径
//   修复: 音效配置真理源保留在 GM, 但查表/播放上移到 RPC 链路 (服务器查 GM → FSoftObjectPath → 客户端 LoadSynchronous)
//   调用方: 已无, v210.4 后 UI / Lifecycle 都不再调此函数
//   保留函数体避免遗留调用编译错误, 但永远返回 nullptr (零兜底, 不允许 fallback)
//
// 大厂原则 — 零兜底:
//   - RoundWinner == None → 返回 nullptr (胜负未定, 不允许基于未定播放)
//   - 音效资产为空 → 返回 nullptr (策划配置错, Log Error 告知)
//
// 不破坏刀战模式:
//   - GameHUDWidget 仅在 Bio 模式 + RoundWinner 已写时调本函数
//   - 刀战永远不调, 4 个字段刀战模式 0 影响
USoundBase* ARoomGameMode::ResolveZombieRoundEndSound(EZombieRoundWinner InRoundWinner) const
{
	// 【v210.4 大厂架构重构】此函数已废弃, 永远返回 nullptr, 强制走新路径 (RPC FSoftObjectPath)
	UE_LOG(LogTemp, Error,
		TEXT("[RoomGameMode] 【v210.4 废弃】ResolveZombieRoundEndSound: 已废弃, 客户端 GetAuthGameMode 返回 nullptr. "
		     "【修复】调用方改为 RoomGS->MulticastPlayZombieRoundSound(NewWinner, FSoftObjectPath(Sound)). "
		     "InRoundWinner=%d"),
		static_cast<int32>(InRoundWinner));
	return nullptr;

	// 【v210.4 注释保留原实现供参考, 不执行】
#if 0
	if (InRoundWinner == EZombieRoundWinner::None)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[RoomGameMode] ResolveZombieRoundEndSound: RoundWinner=None (尚未结算), 拒绝查表."));
		return nullptr;
	}

	USoundBase* SelectedSound = nullptr;
	const TCHAR* SlotName = nullptr;
	switch (InRoundWinner)
	{
	case EZombieRoundWinner::Human:
		SelectedSound = ZombieHumanWinSound;
		SlotName = TEXT("ZombieHumanWinSound");
		break;
	case EZombieRoundWinner::Mother:
		SelectedSound = ZombieMotherWinSound;
		SlotName = TEXT("ZombieMotherWinSound");
		break;
	default:
		UE_LOG(LogTemp, Error,
			TEXT("[RoomGameMode] ResolveZombieRoundEndSound: RoundWinner=%d 未识别."),
			static_cast<int32>(InRoundWinner));
		return nullptr;
	}
	return SelectedSound;
#endif
}


