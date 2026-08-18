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
/**
 * @brief 同步加载并返回武器音效映射数据资产 (DA_WeaponSoundMap)
 *
 * 真理源: GM->WeaponSoundMapAsset (TSoftObjectPtr)
 * 大厂原则 — 单一真理源: 调用方拿 nullptr 时拒绝播放, 不允许 fallback
 *
 * @return UWeaponSoundMapAsset* 同步加载后的数据资产指针
 * @return nullptr = 配置缺失或加载失败, 已 Log Error 报告根因
 *
 * @note v76 大厂架构重构 — 零兜底
 * @note 频繁路径 (Tick/AnimNotify) 永不调用, 仅 Server_SwitchToWeaponSlot 触发
 * @note 缺资产时强制 Log Error 引导修复 BP_GM_RoomGameMode → Room|Audio → Weapon Sound Map Asset
 */
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
 * @brief ARoomGameMode 构造函数 — 配置默认的玩家类、控制器类、HUD 类
 *
 * 目的: 配置默认的玩家类、控制器类、HUD 类等
 * 时机: 在游戏进入战斗地图、GameMode 被实例化时由引擎自动调用
 *
 * @param ObjectInitializer UE 对象初始化器 (UE 内部使用)
 *
 * @note bUseSeamlessTravel=false — 局域网测试期避免 Spawn 时序错乱
 * @note bSkipRoomPhaseForTesting=true — 开发期默认跳过大厅, 直进战斗
 * @note PlayerStateClass/PlayerControllerClass/HUDClass 强制覆盖, 防止 BP 错配
 * @note ABaseAIController::SelfTestArrivalDecision — 启动期单元自检 v9 大厂架构
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
 * @brief UE override, World 已就绪后第一次调用 — 把 GameMode 配置注入 Subsystem 的最早安全时机
 *
 * 数据流: GameMode → Subsystem (单向, 只在 InitGame 时一次性注入)
 * 真理源分层: 编辑器面板 (GameMode UPROPERTY) → 运行时副本 (Subsystem 内部副本, Fast 访问)
 *
 * @param MapName UE 引擎传入的当前地图名
 * @param Options UE 引擎传入的 URL Options 字符串 (含 ?Mode= 等)
 * @param ErrorMessage out 参数, 若初始化失败可写入错误信息
 *
 * @note InitGame 时 GameState 尚未创建, 所以写入 GS->CurrentMatchMode 的逻辑在 InitGameState 中
 * @note v31.3 大厂架构 — 委派所有配置注入到 InjectSubsystemConfigs()
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
 * @brief UE override, 属性构造后调用 — 编辑器实例化时的兜底注入入口
 *
 * 保险措施: 编辑器实例化时也注入, 用于 Subsystem 在 CDO 阶段被访问的场景
 * InitGame 是主入口, 这里只是兜底
 *
 * @note PostInitProperties 可能在 World 不存在时调用, 内部 Get() 会失败
 * @note InitGame 是主入口, 这里只是兜底
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
 * @brief 把 GameMode 配置 (DT / AI Profile / ModeRules) 注入 Subsystem
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
 *
 * @note v31.3 大厂架构 — 数据单向流, InitGame 一次性注入
 * @note v92 扩展注入 Lifecycle 配置 (母体变异 / 总局数 / 时长)
 * @note v93.1 新增注入 MotherMutationSubsystem (母体变异业务权威调度)
 * @note v108 扩展注入母体变异数量 + 目标选择策略
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
 * @brief [大厂委派层] 把玩家加入房间 — 真理源在 URoomMembershipSubsystem
 *
 * v31.5 重构: PlayerStateClass 从 this->PlayerStateClass 传递 (GameMode 唯一真理源),
 * 不在 Subsystem 内部访问"不存在的字段"
 *
 * @param RequestingController 要加入房间的 Controller 指针
 * @param PlayerName 玩家显示名
 *
 * @note 大厂委派层 — GameMode 不持有成员, 真理源下沉到 Subsystem
 * @note v29.2 三层防御: InitPlayerState override + AddPlayerToRoom PS 二次机会 + EnterSkipToHostMode 调完整路径
 */
void ARoomGameMode::AddPlayerToRoom(AController* RequestingController, const FString& PlayerName)
{
	if (URoomMembershipSubsystem* MemSys = URoomMembershipSubsystem::Get(this))
	{
		MemSys->AddPlayerToRoom(RequestingController, PlayerName, PlayerStateClass);
	}
}


/**
 * @brief [大厂委派层] 切换玩家阵营 — 真理源在 URoomMembershipSubsystem
 *
 * @param RequestingController 发起阵营切换的 Controller
 * @param bToAttackTeam true=切到攻方, false=切到守方
 *
 * @note 大厂委派层 — v31.1 重构
 * @note v27 同时同步 Pawn.FactionTag (避免换阵营失效)
 */
void ARoomGameMode::ChangePlayerTeam(AController* RequestingController, bool bToAttackTeam)
{
	if (URoomMembershipSubsystem* MemSys = URoomMembershipSubsystem::Get(this))
	{
		MemSys->ChangePlayerTeam(RequestingController, bToAttackTeam);
	}
}


/**
 * @brief [大厂委派层] 从房间移除玩家 — 真理源在 URoomMembershipSubsystem
 *
 * @param RequestingController 要移除的玩家 Controller
 *
 * @note 大厂委派层 — v31.1 重构
 * @note v28 按阶段分支 (PendingAI / AIController / 真人) 处理 Server_KickPlayer
 */
void ARoomGameMode::RemovePlayerFromRoom(AController* RequestingController)
{
	if (URoomMembershipSubsystem* MemSys = URoomMembershipSubsystem::Get(this))
	{
		MemSys->RemovePlayerFromRoom(RequestingController);
	}
}


/**
 * @brief [大厂委派层] 全房间广播聊天消息 — 真理源在 URoomMembershipSubsystem
 *
 * @param SenderName 发送者名字
 * @param Message 聊天内容
 *
 * @note 大厂委派层 — v31.1 重构
 */
void ARoomGameMode::BroadcastChatMessage(const FString& SenderName, const FString& Message)
{
	if (URoomMembershipSubsystem* MemSys = URoomMembershipSubsystem::Get(this))
	{
		MemSys->BroadcastChatMessage(SenderName, Message);
	}
}


/**
 * @brief [大厂委派层] 全房间广播系统消息 — 真理源在 URoomMembershipSubsystem
 *
 * @param Message 系统消息内容
 *
 * @note 大厂委派层 — v31.1 重构
 */
void ARoomGameMode::BroadcastSystemMessage(const FString& Message)
{
	if (URoomMembershipSubsystem* MemSys = URoomMembershipSubsystem::Get(this))
	{
		MemSys->BroadcastSystemMessage(Message);
	}
}


/**
 * @brief [大厂委派层] 查询指定模式的 AI 配置规则 — 真理源在 URoomSpawnSubsystem
 *
 * @param Mode 房间模式 (Melee/Zombie)
 * @param OutRules out 参数, 返回该模式下的 AI 配置规则 (AIControllerClass + BehaviorTree)
 * @return true=找到, false=SpawnSubsystem 不可用或规则不存在
 *
 * @note 大厂委派层 — v31.2 重构
 * @note 规则集由 GameMode 在 InjectSubsystemConfigs 时注入, 运行时真理源 = Subsystem
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
 * @brief [大厂委派层] AI 实际生成 — 真理源在 URoomSpawnSubsystem
 *
 * 【v54 大厂架构重构】参数从 Profile 改 Config (UAIBehaviorConfigSO)
 *
 * @param Request AI Spawn 请求 (阵营 / CharacterRowName / SequenceID 等)
 * @param Config AI 行为配置资产 (DataAsset, 含 AttackRange/Hyst 等)
 * @param OptionalExistingController 可选, 复用现有 Controller (用于复用机制, v24 大厂架构)
 * @return int32 Spawn 成功的 AI 数量
 *
 * @note 大厂委派层 — v31.2 重构
 * @note v36 单一真理源: 武器 SpawnWeaponID 来源 = Profile.WeaponID (拒绝 NAME_None 静默写入)
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
 * @brief [大厂委派层] 更新玩家准备状态 — 真理源在 URoomMembershipSubsystem
 *
 * @param RequestingController 发起准备的 Controller
 * @param bIsReady true=已准备, false=取消准备
 *
 * @note 大厂委派层 — v31.2 重构
 */
void ARoomGameMode::UpdatePlayerReadyState(AController* RequestingController, bool bIsReady)
{
	if (URoomMembershipSubsystem* MemSys = URoomMembershipSubsystem::Get(this))
	{
		MemSys->UpdatePlayerReadyState(RequestingController, bIsReady);
	}
}

/**
 * @brief [大厂委派层] AI 入队等待战斗 Spawn — 真理源在 URoomSpawnSubsystem
 *
 * 大厅阶段: AI 不生成, 只在 GameMode 维护 "待生成清单"
 *
 * @param Request AI Spawn 请求 (阵营 / CharacterRowName / ProfileTag 等)
 * @return int32 成功入队的数量
 *
 * @note 大厂委派层 — v31.2 重构
 * @note v28 大厅入队 + 战斗 Spawn 架构: 大厅不生成, 战斗开局才 Spawn
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
 * @brief [大厂委派层] 查询指定阵营的待生成 AI 列表 — 真理源在 URoomSpawnSubsystem
 *
 * @param FactionTag 阵营 Tag (Offense/Defense)
 * @return TArray<FPendingAIEntry> 待生成 AI 列表
 *
 * @note 大厂委派层 — v31.2 重构
 * @note UI (URoomInsidePage) 直接读此接口用于大厅占位渲染
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
 * @brief [大厂委派层] 消费所有待生成 AI 一次性 Spawn — 真理源在 URoomSpawnSubsystem
 *
 * 战斗开局时由 SpawnAllPlayersIntoBattle 调用, 清空队列
 *
 * @return TArray<FAISpawnRequest> 已消费的 Spawn 请求列表
 *
 * @note 大厂委派层 — v31.2 重构
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
 * @brief [大厂委派层] 从 PendingAIEntry 构建 Spawn 请求 — 真理源在 URoomSpawnSubsystem
 *
 * 字段转换集中一处, 避免散落各处的字段映射
 *
 * @param Entry 待生成 AI 条目
 * @return FAISpawnRequest 转换后的 Spawn 请求
 *
 * @note 大厂委派层 — v31.2 重构
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
 * @brief [大厂委派层] 判断名字是否为待生成 AI — 真理源在 URoomSpawnSubsystem
 *
 * 用于 Server_KickPlayer 阶段分支判定 (PendingAI vs AIController vs 真人)
 *
 * @param DisplayName 玩家显示名
 * @return true=是 PendingAI, false=不是或 Subsystem 不可用
 *
 * @note 大厂委派层 — v31.2 重构
 * @note v28 阶段分支判定 — 禁止用 StartsWith 等字符串模糊判定
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
 * @brief [大厂委派层] 按名字移除 PendingAI — 真理源在 URoomSpawnSubsystem
 *
 * @param DisplayName 玩家显示名
 * @return true=成功移除, false=未找到或 Subsystem 不可用
 *
 * @note 大厂委派层 — v31.2 重构
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
 * @brief [大厂委派层] 查询所有待生成 AI — 真理源在 URoomSpawnSubsystem
 *
 * 历史: v28-v49 直接返回 GameMode 自身 PendingAIQueue 字段
 * v50: GameMode 不再持有 PendingAIQueue 字段, 委派给 Subsystem
 *
 * @return TArray<FPendingAIEntry> 所有待生成 AI 列表 (跨阵营)
 *
 * @note 大厂委派层 — v50 重构
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
 * @brief [大厂委派层] AI 请求目标敌人 — 真理源在 URoomTargetingSubsystem
 *
 * @param RequestingAI 发起请求的 AI 角色
 * @return ABaseCharacter* 选中的目标敌人, nullptr=无目标或 Subsystem 不可用
 *
 * @note 大厂委派层 — v31.2 重构
 * @note 内部走 GetAllAliveEnemiesFor → RequestTargetForAI 二阶段路径
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
 * @brief [大厂委派层] 获取 AI 的狩猎策略 — 真理源在 URoomTargetingSubsystem
 *
 * 狩猎策略包含目标选择权重配置 (距玩家 / 敌人数 / 阵营优先等)
 *
 * @param AI AI 角色
 * @return FAIHuntPolicy AI 的狩猎策略 (Subsystem 不可用时返回默认构造)
 *
 * @note 大厂委派层 — v31.2 重构
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
 * @brief [大厂委派层] 评估候选敌人得分 — 真理源在 URoomTargetingSubsystem (3 参数签名)
 *
 * 大厂原则 (v31.5):
 *   - 候选敌人必须 const (Subsystem 内部不修改 Candidate 状态, 仅读)
 *   - 头文件声明同步改为 const ABaseCharacter*, 与 cpp 委派签名一致
 *
 * @param RequestingAI 评分主体 AI
 * @param Candidate 候选敌人 (const, 仅读)
 * @param HuntPolicy 狩猎策略
 * @return float 评分 (数值越大越优, 0=Subsystem 不可用)
 *
 * @note 大厂委派层 — v31.2 重构
 * @note 委派时 const_cast 解包, 因 Subsystem 接口要求非 const (内部仍按 const 使用)
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
 * @brief [大厂委派层] 查询所有活着的敌人 — 真理源在 URoomTargetingSubsystem
 *
 * @param RequestingAI 视角 AI
 * @return TArray<ABaseCharacter*> 所有活着且敌对的角色列表
 *
 * @note 大厂委派层 — v31.2 重构
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
 * @brief [大厂委派层] AI 释放目标 — 真理源在 URoomTargetingSubsystem
 *
 * AI 死亡 / 状态切换时调用, 清空锁定 + 释放候选池引用
 *
 * @param RequestingAI 释放目标的 AI
 *
 * @note 大厂委派层 — v31.2 重构
 */
void ARoomGameMode::ReleaseTarget(ABaseCharacter* RequestingAI)
{
	if (URoomTargetingSubsystem* TargetingSys = URoomTargetingSubsystem::Get(this))
	{
		TargetingSys->ReleaseTarget(RequestingAI);
	}
}


/**
 * @brief [大厂委派层] 查询锁定目标的 AI 数量 — 真理源在 URoomTargetingSubsystem
 *
 * @param TargetEnemy 被锁定的目标
 * @return int32 锁定此目标的 AI 数量 (0=Subsystem 不可用)
 *
 * @note 大厂委派层 — v31.2 重构
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
 * @brief [大厂委派层] 判断目标是否被任意 AI 锁定 — 真理源在 URoomTargetingSubsystem
 *
 * @param TargetEnemy 被查询的目标
 * @return true=已被锁定, false=未被锁定或 Subsystem 不可用
 *
 * @note 大厂委派层 — v31.2 重构
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
 * @brief [大厂委派层] 判断目标是否被其他 AI 锁定 (排除指定 AI) — 真理源在 URoomTargetingSubsystem
 *
 * @param TargetEnemy 被查询的目标
 * @param ExcludeAI 排除的 AI (一般是查询发起者自己)
 * @return true=被其他 AI 锁定, false=未被其他锁定或 Subsystem 不可用
 *
 * @note 大厂委派层 — v31.2 重构
 * @note 用于狩猎策略判定: "目标被别 AI 抢了, 我是否要抢"
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
 * @brief [大厂委派层] 检查全员是否已准备 — 真理源在 URoomMembershipSubsystem
 *
 * @return true=全员已准备, false=有人未准备或 Subsystem 不可用
 *
 * @note 大厂委派层 — v31.2 重构
 * @note RequestStartGame 会用此判定, 测试模式 (bSkipRoomPhaseForTesting=true) 会短路此检查
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
 * @brief [大厂委派层] 处理玩家 Spawn 请求 (3 把武器) — 真理源在 URoomSpawnSubsystem
 *
 * 【v52 P0】3 把武器一起转发 (主+副+近战)
 *
 * @param PlayerToSpawn 要 Spawn 的玩家 Controller
 * @param CharRowName 角色 RowName (CharacterDataTable 查询)
 * @param WeaponPrimaryRowName 主武器 RowName
 * @param WeaponSecondaryRowName 副武器 RowName
 * @param WeaponMeleeRowName 近战武器 RowName
 *
 * @note 大厂委派层 — v31.1 重构
 * @note v36 零兜底: CharID/WeaponID 空 → Log Error + 拒绝 Spawn
 */
void ARoomGameMode::HandlePlayerRequestSpawn(AController* PlayerToSpawn, const FString& CharRowName, const FString& WeaponPrimaryRowName, const FString& WeaponSecondaryRowName, const FString& WeaponMeleeRowName)
{
	if (URoomSpawnSubsystem* SpawnSys = URoomSpawnSubsystem::Get(this))
	{
		SpawnSys->HandlePlayerRequestSpawn(PlayerToSpawn, CharRowName, WeaponPrimaryRowName, WeaponSecondaryRowName, WeaponMeleeRowName);
	}
}


/**
 * @brief [大厂委派层] 查询 Controller 的默认 Pawn 类 — 真理源在 URoomSpawnSubsystem
 *
 * UE override, 由引擎在 Spawn Pawn 前调用
 *
 * @param InController 要查询的 Controller
 * @return UClass* Pawn Class (nullptr=Subsystem 不可用)
 *
 * @note 大厂委派层 — v31.1 重构
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
 * @brief [大厂委派层] 重生玩家 — 真理源在 URoomSpawnSubsystem
 *
 * UE override, 引擎在玩家死亡后调用
 *
 * @param NewPlayer 要重生的 Controller
 *
 * @note 大厂委派层 — v31.1 重构
 */
void ARoomGameMode::RestartPlayer(AController* NewPlayer)
{
	if (URoomSpawnSubsystem* SpawnSys = URoomSpawnSubsystem::Get(this))
	{
		SpawnSys->RestartPlayer(NewPlayer);
	}
}


/**
 * @brief [大厂委派层] 玩家/AI 复活请求 — 真理源在 URoomSpawnSubsystem
 *
 * @param DeadController 死亡的 Controller
 * @param bImmediateRespawn true=立即复活, false=等复活延迟 (RespawnDelaySeconds)
 *
 * @note 大厂委派层 — v31.1 重构
 * @note v26 单一真理源: AI 复活链路走 CachedFactionTag, 不走 ModeRules 兜底
 * @note v30 复活无敌期激活入口: 复活后自动触发 ActivateSpawnInvincibility
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
 * @brief 房主请求开始游戏 — RPC 入口, 含身份鉴权 + 全员准备校验 + 委派 LifecycleSubsystem
 *
 * 三段流程:
 *   1. 身份鉴权: 只有 FirstPlayerController (房主) 可发起
 *   2. 全员准备校验 (测试模式短路)
 *   3. 转发到 LifecycleSubsystem, 倒计时后回调 SpawnAllPlayersIntoBattle
 *
 * @param RequestingController 发起开局请求的 Controller
 *
 * @note 不是纯委派 — 含鉴权和测试模式短路业务逻辑 (生命周期调度层)
 * @note v31.1 重构 — 委派 LifecycleSubsystem 执行真正的倒计时
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
 * @brief [大厂委派层] 执行游戏开始流程 — 真理源在 URoomLifecycleSubsystem
 *
 * 与 RequestStartGame 不同: 无鉴权, 直接触发倒计时
 * 用于测试模式或外部直接触发场景
 *
 * @note 大厂委派层 — v31.1 重构
 * @note 通过 FSimpleDelegate 注册回调 (倒计时结束后 SpawnAllPlayersIntoBattle)
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
 * @brief [大厂委派层] 战斗开局 Spawn 所有玩家/AI — 真理源在 URoomSpawnSubsystem
 *
 * LifecycleSubsystem 倒计时结束后的回调入口
 * 遍历 GS->PlayerArray + ConsumePendingAIQueue → SpawnAIInternal/HandlePlayerRequestSpawn
 *
 * @note 大厂委派层 — v31.2 重构
 * @note v28 大厅入队 + 战斗 Spawn: 战斗开局才 Spawn AI (大厅不生成)
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
 * @brief [大厂委派层] 扫描并缓存所有 PlayerStart — 真理源在 URoomSpawnSubsystem
 *
 * 按阵营前缀 (Attack_/Defense_) 分类, 用于 Spawn 时的出生点分配
 *
 * @param bReScan true=强制重新扫描 (忽略缓存), false=用现有缓存 (有则不重扫)
 *
 * @note 大厂委派层 — v31.2 重构
 * @note v27 修复: 无前缀 PlayerStart → Log Error + 拒绝静默归 Attack
 */
void ARoomGameMode::ScanAndCachePlayerStarts(bool bReScan)
{
	if (URoomSpawnSubsystem* SpawnSys = URoomSpawnSubsystem::Get(this))
	{
		SpawnSys->ScanAndCachePlayerStarts(bReScan);
	}
}


// ==========================================
// 【v242 大厂架构重构】粗粒度接口 DEPRECATED — 引导走 ReleaseSpawnPointByController 精准释放
// ==========================================
//
// 编译警告根因 (v242 修复后):
//   旧实现 (v242) 直接调 SpawnSys->ReleaseSpawnPoint(PlayerStart) → 触发下层 UE_DEPRECATED 警告
//   编译警告本身就是 UE 5.6 C4996 报错, "请改用新版 API"
//
// 新实现 (v246) — 转发壳内部走 v246 新增的语义化 API:
//   - ReleaseSpawnPointBySpawnPoint: 反查 OccupiedSpawnByController, 释放所有映射此点的 Controller
//   - 不再调下层废弃方法 (ReleaseSpawnPoint), 编译警告消失
//   - 不暴露 OccupiedSpawnByController 字段给 RoomGameMode (大厂封装原则)
//
// 大厂原则 - 暴露行为, 不暴露数据结构:
//   - RoomGameMode 不需要知道 OccupiedSpawnByController 是 TMap 还是 TSet
//   - URoomSpawnSubsystem 提供语义化 API (ReleaseSpawnPointByController / ReleaseSpawnPointBySpawnPoint)
//   - 后续重构 TMap → TArray 时, RoomGameMode 不需要任何改动
//
// 大厂原则 - 单一释放入口:
//   - ReleaseSpawnPoint(AActor*) 是粗粒度,多玩家同帧死亡会误清空
//   - ReleaseSpawnPointByController(AController*) 是精准释放,大厂首选
//   - 本接口仅保留用于兼容旧 BP 调用方,新代码必须走细粒度
//
/**
 * @brief [大厂委派层] DEPRECATED — 粗粒度释放指定出生点 — 真理源在 URoomSpawnSubsystem
 *
 * 旧 API 已废弃, 新代码必须改用 ReleaseSpawnPointByController(AController*) 精准释放
 *
 * @param PlayerStart 要释放的出生点 (粗粒度 — 会释放所有映射到此点的 Controller)
 *
 * @note v242 DEPRECATED — 编译警告引导改用 ReleaseSpawnPointByController
 * @note v246 转发壳内部走 ReleaseSpawnPointBySpawnPoint 语义化 API (不调下层废弃方法)
 */
UE_DEPRECATED(5.6, "【v242 零兜底】ReleaseSpawnPoint(AActor*) 是粗粒度接口, 请改用 ReleaseSpawnPointByController(AController*) 精准释放, 避免多玩家同帧死亡误清空.")
void ARoomGameMode::ReleaseSpawnPoint(AActor* PlayerStart)
{
	URoomSpawnSubsystem* SpawnSys = URoomSpawnSubsystem::Get(this);
	if (!SpawnSys)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[RoomGameMode] ReleaseSpawnPoint: URoomSpawnSubsystem 未找到. "
			     "请检查 GameMode 初始化 (InjectSubsystemConfigs 是否调用)."));
		return;
	}

	// 【v246 大厂架构重构】走语义化 API, 不再调下层废弃方法
	//   - ReleaseSpawnPointBySpawnPoint 内部反查 OccupiedSpawnByController, 释放所有映射此点的 Controller
	//   - 内部逐个调 ReleaseSpawnPointByController, 双表一致性由其内部维护
	//   - 不暴露 OccupiedSpawnByController 字段给外部 (大厂封装)
	const int32 ReleasedCount = SpawnSys->ReleaseSpawnPointBySpawnPoint(PlayerStart);

	UE_LOG(LogTemp, Display,
		TEXT("[RoomGameMode] ReleaseSpawnPoint: 释放完成 — 共释放 %d 个 Controller. "
		     "【v246 零兜底】新代码请改用 ReleaseSpawnPointByController 按 Controller 精准释放, 避免反查."),
		ReleasedCount);
}


/**
 * @brief [大厂委派层] 查询可用的阵营出生点 — 真理源在 URoomSpawnSubsystem
 *
 * 历史: v25 之前是 RoomGameMode 直接维护 AttackSpawnPoints/DefenseSpawnPoints
 *       v31 大厂重构拆到 URoomSpawnSubsystem, 但本方法 .h 声明保留作为 BP 兼容入口
 *       v31.5 修复: 补上委派实现, 不再是孤儿声明
 *
 * @param PlayerFactionTag 阵营 Tag (Offense/Defense)
 * @param bRemoveOccupied true=占用此点 (Spawn 时调用), false=仅查询 (测试时调用)
 * @param OccupancyOwner 占用者 Controller (用于记录 Controller → PlayerStart 映射, v39 新增)
 * @return AActor* 出生点 (nullptr=无可用或 Subsystem 不可用, 已 Log Error)
 *
 * @note 大厂委派层 — v31.5 重构
 * @note v39 零兜底: Subsystem 不可用时显式 Log Error, 不静默 return nullptr
 * @note v39 OccupancyOwner 用于追踪占用关系, 实现精准释放 (避免多玩家同帧死亡误清空)
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


// ==========================================
// 【v242 大厂架构重构】粗粒度接口 DEPRECATED — 引导走 ReleaseSpawnPointByController 精准释放
// ==========================================
//
// 编译警告根因 (v242 修复后):
//   旧实现 (v242) 直接调 SpawnSys->ResetAllSpawnPointOccupancy() → 触发下层 UE_DEPRECATED 警告
//   编译警告本身就是 UE 5.6 C4996 报错, "请改用新版 API"
//
// 新实现 (v246) — 转发壳内部走 v246 新增的语义化 API:
//   - ReleaseAllSpawnPointOccupancy: 释放所有占用,内部逐个调 ReleaseSpawnPointByController
//   - 不再调下层废弃方法 (ResetAllSpawnPointOccupancy), 编译警告消失
//   - 不暴露 OccupiedSpawnByController 字段给 RoomGameMode (大厂封装原则)
//
// 大厂原则 - 暴露行为, 不暴露数据结构:
//   - RoomGameMode 不需要知道 OccupiedSpawnByController 是 TMap 还是 TSet
//   - URoomSpawnSubsystem 提供语义化 API (ReleaseSpawnPointByController / ReleaseAllSpawnPointOccupancy)
//   - 后续重构 TMap → TArray 时, RoomGameMode 不需要任何改动
//
// 大厂原则 - 单一释放入口:
//   - ResetAllSpawnPointOccupancy 是粗粒度,会误清空其他玩家占用
//   - ReleaseSpawnPointByController 是精准释放,大厂首选
//   - 本接口仅保留用于兼容旧 BP 调用方,新代码必须走细粒度
//
/**
 * @brief [大厂委派层] DEPRECATED — 重置所有出生点占用 — 真理源在 URoomSpawnSubsystem
 *
 * 旧 API 已废弃, 新代码必须改用 ReleaseSpawnPointByController(AController*) 精准释放
 *
 * @note v242 DEPRECATED — 编译警告引导改用 ReleaseSpawnPointByController
 * @note v246 转发壳内部走 ReleaseAllSpawnPointOccupancy 语义化 API (不调下层废弃方法)
 */
UE_DEPRECATED(5.6, "【v242 零兜底】ResetAllSpawnPointOccupancy 是粗粒度接口, 请改用 ReleaseSpawnPointByController 精细化释放, 避免多玩家同帧死亡误清空.")
void ARoomGameMode::ResetAllSpawnPointOccupancy()
{
	URoomSpawnSubsystem* SpawnSys = URoomSpawnSubsystem::Get(this);
	if (!SpawnSys)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[RoomGameMode] ResetAllSpawnPointOccupancy: URoomSpawnSubsystem 未找到. "
			     "请检查 GameMode 初始化 (InjectSubsystemConfigs 是否调用)."));
		return;
	}

	// 【v246 大厂架构重构】走语义化 API, 不再调下层废弃方法
	//   - ReleaseAllSpawnPointOccupancy 内部逐个调 ReleaseSpawnPointByController
	//   - 双表一致性由 ReleaseSpawnPointByController 内部维护
	//   - 不暴露 OccupiedSpawnByController 字段给外部 (大厂封装)
	const int32 ReleasedCount = SpawnSys->ReleaseAllSpawnPointOccupancy();

	UE_LOG(LogTemp, Display,
		TEXT("[RoomGameMode] ResetAllSpawnPointOccupancy: 释放完成 — 共释放 %d 个 Controller. "
		     "【v246 零兜底】新代码应使用 ReleaseSpawnPointByController 按 Controller 精准释放, 避免误清空."),
		ReleasedCount);
}


/**
 * @brief [大厂委派层] 查询玩家的 CharID/WeaponID 缓存 — 真理源在 URoomSpawnSubsystem
 *
 * 用于临时调试 / 兼容旧接口; 当前主链路已走 Pawn.SpawnWeaponID (Replicated, 单一真理源 v36)
 *
 * @param ControllerUniqueID Controller UniqueID (网络唯一标识)
 * @param OutCharID out 参数, 返回角色 ID
 * @param OutWeaponID out 参数, 返回武器 ID
 * @return true=查询成功, false=未找到或 Subsystem 不可用
 *
 * @note 大厂委派层 — v31.1 重构
 * @note v36 起此接口为"兼容缓存", 主链路不再依赖 (真理源 = Pawn 字段)
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
 * @brief [大厂委派层] 启动比赛计时器 — 真理源在 URoomLifecycleSubsystem
 *
 * 倒计时参数来自 ZombieMatchDurationSeconds / MeleeMatchDurationSeconds (按模式选择)
 * 到期回调 OnMatchTimerTick → HandleMatchTimeOut
 *
 * @note 大厂委派层 — v31.2 重构
 */
void ARoomGameMode::StartMatchTimer()
{
	if (URoomLifecycleSubsystem* LifeSys = URoomLifecycleSubsystem::Get(this))
	{
		LifeSys->StartMatchTimer();
	}
}


/**
 * @brief [大厂委派层] 比赛计时器 Tick 回调 — 真理源在 URoomLifecycleSubsystem
 *
 * 由 UE Timer 系统按 0.1s 频率调用, 更新 GS->MatchRemainingSeconds / 触发提前结束判定
 *
 * @note 大厂委派层 — v31.2 重构
 */
void ARoomGameMode::OnMatchTimerTick()
{
	if (URoomLifecycleSubsystem* LifeSys = URoomLifecycleSubsystem::Get(this))
	{
		LifeSys->OnMatchTimerTick();
	}
}


/**
 * @brief [大厂委派层] 处理比赛超时 — 真理源在 URoomLifecycleSubsystem
 *
 * 计时器归零时触发, 根据当前模式进入结算流程 (Melee/Zombie 分支)
 *
 * @note 大厂委派层 — v31.2 重构
 */
void ARoomGameMode::HandleMatchTimeOut()
{
	if (URoomLifecycleSubsystem* LifeSys = URoomLifecycleSubsystem::Get(this))
	{
		LifeSys->HandleMatchTimeOut();
	}
}


/**
 * @brief [大厂委派层] 处理生化小局结束 — 真理源在 URoomLifecycleSubsystem
 *
 * 生化模式特有: 母体被击杀或人类全灭时触发
 *
 * @note 大厂委派层 — v31.2 重构
 * @note 进入结算 UI, 准备下一局 / 切换模式
 */
void ARoomGameMode::HandleZombieRoundEnd()
{
	if (URoomLifecycleSubsystem* LifeSys = URoomLifecycleSubsystem::Get(this))
	{
		LifeSys->HandleZombieRoundEnd();
	}
}


/**
 * @brief [大厂委派层] 开启下一局生化小局 — 真理源在 URoomLifecycleSubsystem
 *
 * @note 大厂委派层 — v31.2 重构
 * @note 走 Reset + 重新占位 + 重置母体的完整链路
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
 * @brief [大厂委派层] 服务端房主权限转移 — 真理源在 URoomMembershipSubsystem
 *
 * @param NewHostPlayerName 新房主玩家名
 * @return true=转移成功, false=未找到玩家或 Subsystem 不可用
 *
 * @note 大厂委派层 — v31.1 重构
 * @note 触发 GS->HostPlayerName 更新 + Broadcast OnHostChanged 事件
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
/**
 * @brief [v210.4 大厂架构重构] 生化小局结算音效查表 — 已废弃, 永远返回 nullptr
 *
 * 【v210.4 大厂架构重构 — 废弃 ResolveZombieRoundEndSound】
 *   旧 (v134 - v210.3): GM 提供 ResolveZombieRoundEndSound(RoundWinner) 集中决策
 *   根因: 客户端 World->GetAuthGameMode() 返回 nullptr, 此函数在客户端永远走 fallback 路径
 *   修复: 音效配置真理源保留在 GM, 但查表/播放上移到 RPC 链路 (服务器查 GM → FSoftObjectPath → 客户端 LoadSynchronous)
 *   调用方: 已无, v210.4 后 UI / Lifecycle 都不再调此函数
 *   保留函数体避免遗留调用编译错误, 但永远返回 nullptr (零兜底, 不允许 fallback)
 *
 * 大厂原则 — 零兜底:
 *   - RoundWinner == None → 返回 nullptr (胜负未定, 不允许基于未定播放)
 *   - 音效资产为空 → 返回 nullptr (策划配置错, Log Error 告知)
 *
 * 不破坏刀战模式:
 *   - GameHUDWidget 仅在 Bio 模式 + RoundWinner 已写时调本函数
 *   - 刀战永远不调, 4 个字段刀战模式 0 影响
 *
 * @param InRoundWinner 小局胜负方 (Human/Mother)
 * @return USoundBase* 永远返回 nullptr (函数已废弃, 已 Log Error 引导修复)
 *
 * @note v210.4 永远返回 nullptr, 强制调用方走新 RPC 链路
 * @note 新路径: RoomGS->MulticastPlayZombieRoundSound(NewWinner, FSoftObjectPath(Sound))
 */
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


