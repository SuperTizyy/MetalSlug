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

// 再次引入 Kismet 静态函数库（重复包含，无副作用，便于阅读）
#include "Kismet/GameplayStatics.h"

// 再次引入角色基类（同上）
#include "Characters/BaseCharacter.h"

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
#include "Data/AI/AIProfileAsset.h"
#include "Data/AI/AIBehaviorConfigSO.h"
#include "GameFramework/PlayerController.h"
#include "AIController.h"


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
// 2. 玩家管理
// ==========================================

/**
 * AddPlayerToRoom
 *
 * 处理新玩家加入房间
 * 1. 把第一个进入的玩家记录为房主（写入 GameState）
 * 2. 设置玩家名到 PlayerState
 * 3. 智能分配攻/守方（哪边人少进哪边）
 * 4. 广播系统提示
 */
void ARoomGameMode::AddPlayerToRoom(AController* RequestingController, const FString& PlayerName)
{
	if (ARoomGameState* GS = GetGameState<ARoomGameState>())
	{
		// 谁第一个进房间（房主建房时），谁的名字就刻在 GameState 上
		if (GS->HostPlayerName.IsEmpty())
		{
			GS->HostPlayerName = PlayerName;
		}
	}

	if (ARoomPlayerState* PS = RequestingController->GetPlayerState<ARoomPlayerState>())
	{
		// 引擎底层会自动同步名字
		PS->SetPlayerName(PlayerName);

		// 智能分配算法: 向 GameState 查询目前哪边人少？
		if (ARoomGameState* GS = GetGameState<ARoomGameState>())
		{
			int32 AttackCount = GS->GetPlayersInTeam(ERoomTeam::Attack).Num();
			int32 DefenseCount = GS->GetPlayersInTeam(ERoomTeam::Defense).Num();

			// 修改队伍，引擎会自动将这个改动广播给全服
			const ERoomTeam NewTeam = (AttackCount <= DefenseCount) ? ERoomTeam::Attack : ERoomTeam::Defense;
			PS->CurrentTeam = NewTeam;

			// ==========================================
			// 【2026-06-29 P0 修复】服务器端手动触发 OnRep
			// 同 ChangePlayerTeam 的根因: ReplicatedUsing 只在客户端触发,
			// 服务器自己改 CurrentTeam 必须手动 Broadcast OnStateChanged
			// 否则房主自己的 UI 在新玩家加入时不会立即显示该玩家
			// ==========================================
			PS->OnRep_Team();
		}
	}

	// 广播系统提示，告知所有人有新玩家加入
	BroadcastSystemMessage(FString::Printf(TEXT("玩家【%s】加入了房间"), *PlayerName));

	// 【P0 架构升级】通过 URoomService 事件总线广播玩家加入 (RoomInsidePage 订阅了 OnPlayerJoined, 无需 5s 兜底)
	URoomService::BroadcastPlayerJoined(this, PlayerName);
}


/**
 * ChangePlayerTeam
 *
 * 处理玩家主动请求换队伍
 * 服务器直接修改 PlayerState 的 CurrentTeam
 * 引擎会自动在下一个 Tick 将这个变量广播给所有客户端，并触发 OnRep_Team
 */
void ARoomGameMode::ChangePlayerTeam(AController* RequestingController, bool bToAttackTeam)
{
	// 不再按名字去找，直接获取发请求的那个人的 PlayerState
	if (ARoomPlayerState* PS = RequestingController->GetPlayerState<ARoomPlayerState>())
	{
		// 服务器直接修改它的值
		const ERoomTeam NewTeam = bToAttackTeam ? ERoomTeam::Attack : ERoomTeam::Defense;

		// 【2026-06-29 P0 修复】已经在该阵营则不需要重复触发
		if (PS->CurrentTeam == NewTeam)
		{
			return;
		}

		PS->CurrentTeam = NewTeam;

		// ==========================================
		// 【2026-06-29 P0 修复】服务器端手动触发 OnRep
		// 根因: ReplicatedUsing 只在**客户端**收到同步包后触发 OnRep_* 回调,
		//       服务器自己改了 CurrentTeam 不会触发, 但服务器端运行的 UI (房主/独立进程模式)
		//       必须收到 OnStateChanged 才能刷新自己看到的队伍切换
		// 修复: 服务器主动调用 OnRep_Team() 等价于 OnStateChanged.Broadcast()
		//       → 订阅了 ARoomPlayerState::OnStateChanged 的所有 UI 立即刷新
		//       → 客户端那边: 复制系统下发后, 客户端 OnRep_Team 自动触发, 同样刷新
		// ==========================================
		PS->OnRep_Team();
	}
}


/**
 * RemovePlayerFromRoom
 *
 * 处理玩家离开房间
 * 引擎会自动把 PlayerState 从 GameState 中销毁并移除，无需手动 Remove
 */
void ARoomGameMode::RemovePlayerFromRoom(AController* RequestingController)
{
	FString LeftPlayerName;
	if (ARoomPlayerState* PS = RequestingController->GetPlayerState<ARoomPlayerState>())
	{
		LeftPlayerName = PS->GetPlayerName();
		BroadcastSystemMessage(FString::Printf(TEXT("玩家【%s】退出了房间"), *LeftPlayerName));
	}

	// 【P0 架构升级】通过 URoomService 事件总线广播玩家离开 (RoomInsidePage 订阅了 OnPlayerLeft, 无需 5s 兜底)
	if (!LeftPlayerName.IsEmpty())
	{
		URoomService::BroadcastPlayerLeft(this, LeftPlayerName);
	}

	// 【P0 架构升级】自动房主转让: 如果离开的是房主, 自动把房主权限转交给下一个在线玩家
	// 触发路径: 房主主动退房 / 房主被踢 / 房主断线
	if (ARoomGameState* GS = GetGameState<ARoomGameState>())
	{
		if (!GS->HostPlayerName.IsEmpty() && GS->HostPlayerName.Equals(LeftPlayerName, ESearchCase::IgnoreCase))
		{
			UE_LOG(LogTemp, Log, TEXT("[RoomGameMode] 房主 [%s] 离房, 自动转交房主权限"), *LeftPlayerName);
			TransferHostTo(TEXT("")); // 空字符串 = 自动选下一个
		}
	}
}


/**
 * BroadcastChatMessage
 *
 * 广播玩家聊天
 * 1. 判断发送者是否是房主（在 ListenServer 架构下，FirstPlayerController 就是本机的房主）
 * 2. 给所有 PC 触发 Client_ReceiveChatMessage
 */
void ARoomGameMode::BroadcastChatMessage(const FString& SenderName, const FString& Message)
{
	// 1. 鉴定谁是房主？(服务器上排在第一个的本地玩家就是房主)
	FString HostName = TEXT("");
	if (ARoomPlayerController* HostPC = Cast<ARoomPlayerController>(GetWorld()->GetFirstPlayerController()))
	{
		HostName = HostPC->MyPlayerName;
	}

	// 对比一下，发消息的这个人是不是房主？
	bool bIsHost = (SenderName == HostName);

	// 2. 拿着大喇叭，给房间里所有的对讲机下达指令！
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (ARoomPlayerController* PC = Cast<ARoomPlayerController>(It->Get()))
		{
			// 触发所有人的 Client RPC
			PC->Client_ReceiveChatMessage(SenderName, bIsHost, Message, false);
		}
	}
}


/**
 * BroadcastSystemMessage
 *
 * 广播系统绿字提示（无发送人）
 */
void ARoomGameMode::BroadcastSystemMessage(const FString& Message)
{
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (ARoomPlayerController* PC = Cast<ARoomPlayerController>(It->Get()))
		{
			// 系统消息: 没有发送人，bIsHost为false，bIsSystemMsg 为 true
			PC->Client_ReceiveChatMessage(TEXT(""), false, Message, true);
		}
	}
}


/**
 * AddAIToRoom（兼容老 API — DEPRECATED）
 *
 * 设计: 仍然可用但内部转发给 AddAIByRequest.
 *       旧蓝图/老蓝图测试节点不会因此报错.
 *
 * Phase 2 推荐改用 AddAIByRequest(FAISpawnRequest).
 */
void ARoomGameMode::AddAIToRoom(bool bToAttackTeam, const FString& CharacterName, int32 Count)
{
	if (Count <= 0)
	{
		return;
	}

	FAISpawnRequest Request;
	Request.Mode = ERoomMatchMode::Melee;            // 老调用默认走 Melee (向后兼容)
	Request.Team = bToAttackTeam ? ERoomTeam::Attack : ERoomTeam::Defense;
	Request.CharacterRowName = FName(*CharacterName);
	Request.Count = Count;
	Request.ProfileTag = DefaultProfileTag;          // 兜底 Tag
	Request.bUseTeamSpawnPoint = true;

	const int32 Spawned = AddAIByRequest(Request);

	UE_LOG(LogTemp, Log, TEXT("[RoomGameMode][DEPRECATED] AddAIToRoom -> AddAIByRequest 已生成 %d/%d"), Spawned, Count);
}


/**
 * AddAIByRequest — Phase 2 模式化 Spawn 主入口
 *
 * 流程:
 *   1. TryResolveProfile  → Profile
 *   2. SpawnAIInternal     → 生成 Controller + Pawn + Possess + Profile 注入
 *   3. 计数 + 返回
 *
 * 注意: 即使 Profile 为 nullptr, SpawnAIInternal 仍可以兜底 (生成裸 Base + 不跑 BT),
 *       这样策划忘配 Profile 时不至于崩溃, 只是 Profile 默认值生效.
 */
int32 ARoomGameMode::AddAIByRequest(const FAISpawnRequest& Request)
{
	if (Request.Count <= 0)
	{
		return 0;
	}

	UAIProfileAsset* Profile = TryResolveProfile(Request.Mode, Request.ProfileTag);
	if (!Profile)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[RoomGameMode] AddAIByRequest: Mode=%d Tag=%s 未找到 Profile, 走兜底裸 AI"),
			(int32)Request.Mode, *Request.ProfileTag.ToString());
	}

	return SpawnAIInternal(Request, Profile);
}


/**
 * TryResolveProfile — 按 Mode + Tag 多级兜底查找
 *
 * 查询顺序:
 *   1. ProfilesByMode[Mode].Profiles[Tag]                  (精确命中)
 *   2. ProfilesByMode[Mode].Profiles[DefaultProfileTag]    (模式默认)
 *   3. ProfilesByMode[Melee].Profiles[DefaultProfileTag]   (跨模式兜底)
 *   4. nullptr                                            (让调用方报警/降级)
 */
UAIProfileAsset* ARoomGameMode::TryResolveProfile(ERoomMatchMode Mode, FGameplayTag ProfileTag) const
{
	// 1. 精确命中
	if (const FAIProfileRegistry* Registry = ProfilesByMode.Find(Mode))
	{
		if (const TSoftObjectPtr<UAIProfileAsset>* Soft = Registry->Profiles.Find(ProfileTag))
		{
			if (UAIProfileAsset* Loaded = Soft->LoadSynchronous())
			{
				return Loaded;
			}
		}
		// 模式存在但 Tag 没配 — 尝试模式默认 Tag
		if (DefaultProfileTag.IsValid())
		{
			if (auto* Soft2 = Registry->Profiles.Find(DefaultProfileTag))
			{
				if (UAIProfileAsset* Loaded = Soft2->LoadSynchronous())
				{
					return Loaded;
				}
			}
		}
	}

	// 2. 跨模式兜底 — 用 Melee 的默认
	if (Mode != ERoomMatchMode::Melee)
	{
		if (const FAIProfileRegistry* MeleeRegistry = ProfilesByMode.Find(ERoomMatchMode::Melee))
		{
			if (DefaultProfileTag.IsValid())
			{
				if (auto* Soft3 = MeleeRegistry->Profiles.Find(DefaultProfileTag))
				{
					if (UAIProfileAsset* Loaded = Soft3->LoadSynchronous())
					{
						return Loaded;
					}
				}
			}
		}
	}

	return nullptr;  // 找不到 — 让调用方决定怎么办
}


/**
 * GetModeRules — 简化查表
 *
 * 设计: 策划在 BP_RoomGameMode 配置 ModeRulesByMode, 这里一行拿到.
 *       找不到时返回默认值 (AttackFaction="Faction.Player", DefenseFaction="Faction.Zombie")
 *       这是为兼容老代码 (GameMode.cpp 里 384行 的 hardcode)
 */
bool ARoomGameMode::GetModeRules(ERoomMatchMode Mode, FAIModeRules& OutRules) const
{
	if (const FAIModeRules* Found = ModeRulesByMode.Find(Mode))
	{
		OutRules = *Found;
		return true;
	}

	// 兜底: 沿用老的硬编码 (Attack→Zombie/AI, Defense→Player)
	// 根因: Melee模式中"攻方"是玩家(活人)，"守方"是AI(Grunt)
	//       而阵营ID中 Player=0, Zombie=1, 所以 AttackTeam应该是Zombie, DefenseTeam应该是Player
	OutRules = FAIModeRules();
	OutRules.AttackTeamFaction = FGameplayTag::RequestGameplayTag(TEXT("Faction.Zombie"), false);
	OutRules.DefenseTeamFaction = FGameplayTag::RequestGameplayTag(TEXT("Faction.Player"), false);
	OutRules.bRoundBased = (Mode == ERoomMatchMode::Zombie);
	OutRules.TotalRounds = ZombieTotalRounds;
	return false;
}


/**
 * SpawnAIInternal — 实际生成动作 (Phase 2 模式化)
 *
 * 设计:
 *   - 完全接管 AddAIToRoom 老代码的 Spawn 链路
 *   - Profile -> Controller Class 由 Profile.ControllerClass 决定 (留空兜 DefaultControllerClass)
 *   - FactionTag 从 ModeRules 取 — 老代码 hardcode "Faction.Player/Zombie" 退役
 *   - Possess 后调 InitializeFromProfile, 走 Phase 1 链路 (感知+BT+阵营协议)
 *
 * @return 实际生成数
 */
int32 ARoomGameMode::SpawnAIInternal(const FAISpawnRequest& Request, UAIProfileAsset* Profile)
{
	// 【P0 2026.07.10 v23 大厂架构修复】入口诊断
	//
	// 历史 bug: 复活 lambda 调用 SpawnAIInternal(*RequestCopy, nullptr) 时没有任何 ERROR 日志
	//   → 看起来"复活流程正常", 但新 AIC 没有 Profile → 没有 BT → "静默失败"
	//   → 用户观察: "AI 死了没复活" (实际是复活了但没配置)
	//
	// 新架构 (大厂原则: 错误显式化 > 静默兜底):
	//   入口处立即打印 Profile 状态, 任何 nullptr 立即 ERROR
	//   让"Profile 是否到达 SpawnAIInternal"成为显式可观测的事实
	if (Profile)
	{
		UE_LOG(LogTemp, Log,
			TEXT("[SpawnAIInternal] 入口: Profile=%s FactionTag=%s CharacterRowName=%s WeaponID=%s Mode=%d Team=%d"),
			*Profile->GetName(),
			*Profile->FactionTag.ToString(),
			*Profile->CharacterRowName.ToString(),
			*Profile->WeaponID.ToString(),
			(int32)Request.Mode, (int32)Request.Team);
	}
	else
	{
		UE_LOG(LogTemp, Error,
			TEXT("[SpawnAIInternal] 入口: Profile 为空! "
				 "AI 不会获得 BehaviorTree / 感知配置 / 武器配置. "
				 "根因排查: 复活调用方 RequestRespawn 必须传非空 Profile, 见 BaseAIController::InitializeFromProfile. "
				 "Mode=%d Team=%d CharacterRowName=%s"),
			(int32)Request.Mode, (int32)Request.Team,
			*Request.CharacterRowName.ToString());
		// 不立即 return — 仍然允许生成"无 Profile 的空壳 AI", 由调用方看到 ERROR 日志后定位
		// 这是大厂原则"错误显式化": 让问题可见, 而非静默丢弃 spawn 请求
	}

	if (!Profile && !DefaultControllerClass)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[RoomGameMode] SpawnAIInternal: Profile 为空且 DefaultControllerClass 也为空, 无法生成 AI"));
		return 0;
	}

	// 1. 查 CharacterDataTable 拿 Pawn Class
	TSubclassOf<ABaseCharacter> AIPawnClass = nullptr;
	if (Request.CharacterRowName != NAME_None && CharacterDataTable)
	{
		static const FString Ctx(TEXT("SpawnAIInternal"));
		if (FCharacterInfo* Row = CharacterDataTable->FindRow<FCharacterInfo>(Request.CharacterRowName, Ctx))
		{
			if (!Row->CharacterBlueprint.IsNull())
			{
				AIPawnClass = Row->CharacterBlueprint.LoadSynchronous();
			}
		}
	}

	// Fallback: 没填 CharacterRowName 时, 尝试用 Profile.ProfileTag 的 DisplayName 反查
	// (设计: Profile.Tag 命名规范时, 等同于 Character DT 行名)
	if (!AIPawnClass && Profile && CharacterDataTable)
	{
		const FString DerivedRowName = Profile->ProfileTag.ToString();
		if (!DerivedRowName.IsEmpty())
		{
			static const FString Ctx2(TEXT("SpawnAIInternal.Fallback"));
			if (FCharacterInfo* Row = CharacterDataTable->FindRow<FCharacterInfo>(FName(*DerivedRowName), Ctx2))
			{
				if (!Row->CharacterBlueprint.IsNull())
				{
					AIPawnClass = Row->CharacterBlueprint.LoadSynchronous();
				}
			}
		}
	}

	if (!AIPawnClass)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[RoomGameMode] SpawnAIInternal: CharRowName='%s' Profile='%s' 都查不到 Pawn, 跳过"),
			*Request.CharacterRowName.ToString(), *GetNameSafe(Profile));
		return 0;
	}

	// 2. 决定 Controller Class (Profile > 默认)
	TSubclassOf<AAIController> ControllerClass = DefaultControllerClass;
	if (Profile && Profile->ControllerClass)
	{
		ControllerClass = Profile->ControllerClass;
	}
	if (!ControllerClass)
	{
		ControllerClass = ABaseAIController::StaticClass();
	}

	// 3. 决定 FactionTag (从 ModeRules 取 — 替代硬编码)
	FAIModeRules Rules;
	GetModeRules(Request.Mode, Rules);
	const FGameplayTag DesiredFaction =
		(Request.Team == ERoomTeam::Attack) ? Rules.AttackTeamFaction : Rules.DefenseTeamFaction;

	// 4. 循环 Spawn
	int32 SpawnedCount = 0;
	for (int32 i = 0; i < Request.Count; ++i)
	{
		const FString AIName = FString::Printf(
			TEXT("AI_%s_%d"),
			Profile ? *Profile->DisplayName.ToString() : TEXT("NPC"),
			AINextID++);

		// 4a. 出生点
		FVector SpawnLoc = FVector::ZeroVector;
		FRotator SpawnRot = FRotator::ZeroRotator;
		if (Request.bUseTeamSpawnPoint)
		{
			ScanAndCachePlayerStarts(false);
			AActor* SpawnPt = GetAvailableSpawnPointForTeam(Request.Team, true);
			if (SpawnPt)
			{
				SpawnLoc = SpawnPt->GetActorLocation();
				SpawnRot = SpawnPt->GetActorRotation();
			}
		}

		// 4b. Spawn Controller
		FActorSpawnParameters SP;
		SP.Owner = this;
		SP.Name = FName(*FString::Printf(TEXT("AIC_%s"), *AIName));

		AAIController* AIC = GetWorld()->SpawnActor<AAIController>(
			ControllerClass, SpawnLoc, SpawnRot, SP);

		if (!AIC)
		{
			UE_LOG(LogTemp, Error,
				TEXT("[RoomGameMode] SpawnAIController 失败 (Class=%s)"), *GetNameSafe(ControllerClass));
			continue;
		}

		// 4c. Spawn Pawn + Possess
		FActorSpawnParameters PawnSP;
		PawnSP.Owner = AIC;
		PawnSP.Instigator = nullptr;

		ABaseCharacter* AIPawn = GetWorld()->SpawnActor<ABaseCharacter>(
			AIPawnClass, SpawnLoc, SpawnRot, PawnSP);

		if (!AIPawn)
		{
			UE_LOG(LogTemp, Error, TEXT("[RoomGameMode] Spawn AIPawn '%s' 失败"), *AIPawnClass->GetName());
			AIC->Destroy();
			continue;
		}

		// 【P0 修复 2026.07.03 19:35】把 Profile 的武器/角色配置写入 Pawn 字段
		// PossessedBy 中会读这些字段 → 调 SpawnAndEquipWeapon → 武器到手
		// 注意: 必须在 Possess 之前写, 这样 PossessedBy 时已经能看到
		if (Profile)
		{
			const FString DesiredCharID =
				Profile->CharacterRowName.IsNone()
					? Request.CharacterRowName.ToString()
					: Profile->CharacterRowName.ToString();
			const FString DesiredWeaponID = Profile->WeaponID.IsNone() ? FString() : Profile->WeaponID.ToString();
			AIPawn->SetSpawnLoadout(DesiredCharID, DesiredWeaponID);
		}

		AIC->Possess(AIPawn);

		// 4e. 【Phase 1 核心】调 InitializeFromProfile 注入感知 + BT
		// 注意: InitializeFromProfile 内部会设置 FactionTag (来自 Profile.FactionTag)
		// 所以 Faction 设置必须在 InitializeFromProfile 之后, 让 ModeRules 阵营覆盖 Profile 阵营
		if (Profile)
		{
			if (ABaseAIController* BaseAIC = Cast<ABaseAIController>(AIC))
			{
				BaseAIC->InitializeFromProfile(Profile);

				// 【P0 2026.07.10 v23 大厂架构修复】复活成功验证
				//
				// 历史 bug: InitializeFromProfile(nullptr) 内部直接 return, 不抛错
				//   → 旧 AIC 没 BT → 新 AI "死了没复活" 静默失败
				//   → 即使 Profile 非空, InitializeFromProfile 内 LoadBehaviorConfigSync 返回 nullptr
				//     也会 return, RuntimeConfig 没 Config
				//
				// 新架构 (大厂原则: 错误显式化 > 静默兜底):
				//   Spawn 后立即验证 AIC 拿到了 Profile + RuntimeConfig 拿到了 Config
				//   任何环节为 nullptr 立即 ERROR, 让"AI 复活失败"成为显式可见的事实
				UAIProfileAsset* AICProfileAfter = BaseAIC->GetCurrentProfile();
				if (!AICProfileAfter)
				{
					UE_LOG(LogTemp, Error,
						TEXT("[SpawnAIInternal] ⚠ 复活验证失败: AIC '%s' InitializeFromProfile 后 CurrentProfile 仍为空! "
							 "根因排查: UAIProfileAsset 在 InitializeFromProfile 内被清空, 或 Profile 加载失败."),
						*BaseAIC->GetName());
				}
				else if (AICProfileAfter != Profile)
				{
					UE_LOG(LogTemp, Error,
						TEXT("[SpawnAIInternal] ⚠ 复活验证失败: AIC '%s' 拿到的 Profile 与传入的不一致! "
							 "Expected=%s, Got=%s. "
							 "根因排查: InitializeFromProfile 内被外部覆盖."),
						*BaseAIC->GetName(),
						*Profile->GetName(), *AICProfileAfter->GetName());
				}
				else
				{
					UE_LOG(LogTemp, Log,
						TEXT("[SpawnAIInternal] ✓ 复活验证通过: AIC '%s' 成功拿到 Profile=%s"),
						*BaseAIC->GetName(), *AICProfileAfter->GetName());
				}
			}
		}
		else
		{
			// 【P0 2026.07.10 v23】Profile 为空时, AIC 没有 BT / Config, 提前报错
			UE_LOG(LogTemp, Error,
				TEXT("[SpawnAIInternal] ⚠ AIC '%s' 因 Profile 为空, 不会获得 BT / RuntimeConfig — 这是一个 '僵尸 AI', 会立即站桩. "
					 "调用方必须保证 Profile 非空."),
				*AIC->GetName());
		}

		// 4d. Faction Tag (必须在 InitializeFromProfile 之后, 确保 ModeRules 阵营覆盖 Profile.FactionTag)
		if (DesiredFaction.IsValid())
		{
			AIPawn->SetGenericTeamId(ABaseCharacter::ResolveGenericTeamIdFromTag(DesiredFaction));
		}

		UE_LOG(LogTemp, Log,
			TEXT("[RoomGameMode] AI 生成: %s, Mode=%d, Team=%d, Faction=%s, Class=%s, Weapon='%s'"),
			*AIName, (int32)Request.Mode, (int32)Request.Team,
			*DesiredFaction.ToString(), *GetNameSafe(ControllerClass),
			*AIPawn->GetSpawnWeaponID());

		++SpawnedCount;
	}

	if (SpawnedCount > 0)
	{
		BroadcastSystemMessage(FString::Printf(TEXT("已添加 %d 名 AI 参战"), SpawnedCount));
	}

	return SpawnedCount;
}


/**
 * GetEffectiveHuntPolicy — 从 AI 拿它当前生效的 HuntPolicy
 *
 * 设计: 调用方不想暴露 ABaseAIController 类给 BP, GameMode 做转发.
 *       Profile 为空时给默认 NearestDistance (与 Phase 1 一致).
 */
FAIHuntPolicy ARoomGameMode::GetEffectiveHuntPolicy(ABaseCharacter* AI) const
{
	if (!AI)
	{
		return FAIHuntPolicy();
	}
	AController* Ctrl = AI->GetController();
	if (ABaseAIController* BaseAIC = Cast<ABaseAIController>(Ctrl))
	{
		if (UAIProfileAsset* Profile = BaseAIC->GetCurrentProfile())
		{
			return Profile->HuntPolicy;
		}
	}
	return FAIHuntPolicy();
}


/**
 * UpdatePlayerReadyState
 *
 * 切换玩家准备状态
 * 服务器直接修改 PlayerState 的 bIsReady，引擎自动同步
 *
 * 【2026-06-30 P0 修复】手动触发 OnRep_IsReady (与 SwitchPlayerTeam 同款)
 * 根因: ReplicatedUsing OnRep_IsReady 只在**远端客户端**收到同步包后触发。
 *       服务器自己（房主进程 / ListenServer 上的 server 自己）改了 bIsReady 后
 *       OnRep_IsReady 不会自动跑, 房主 UI 看不到玩家准备状态即时变化。
 * 之前症状: "玩家点了准备后, 房主端 widget 一直是未准备, 必须切换阵营才更新"
 * 修复: 修改 PS->bIsReady 后立即主动调用 OnRep_IsReady(), 强制 server 端触发 OnStateChanged
 *       → 订阅了 OnStateChanged 的 RoomInsidePage 立即 RefreshRoomUI
 *       → 远端 client 那边仍由复制系统自动触发 OnRep_IsReady, 不冲突
 */
void ARoomGameMode::UpdatePlayerReadyState(AController* RequestingController, bool bIsReady)
{
	if (ARoomPlayerState* PS = RequestingController->GetPlayerState<ARoomPlayerState>())
	{
		// 服务器直接修改它的值，引擎会自动同步给全网
		PS->bIsReady = bIsReady;

		// 房主端 (ListenServer 进程本地) 立即看到该玩家准备状态变化
		PS->OnRep_IsReady();
	}
}


// ==========================================
// 3. AI 目标分配系统
// ==========================================

/**
 * RequestTargetForAI — Phase 2 重构
 *
 * 设计:
 *   - 不再写死"按分数排序"
 *   - 走 ScoreCandidateForAI 对每个候选评分
 *   - 按 Policy.Strategy 选策略 (Nearest/Random/HighestScore/Mother-Weight)
 *   - 反扎堆均摊: 用 Policy.AntiHuddleWeight 调整分
 *
 * 候选筛选前置:
 *   - GetAllAliveEnemiesFor 已经过滤"异阵营 + 活着"
 *   - 这里再加一层 Policy.MaxChaseDistance / MinHealthThreshold 过滤
 *
 * 评分逻辑见 ScoreCandidateForAI — 这里按策略聚合
 */
ABaseCharacter* ARoomGameMode::RequestTargetForAI(ABaseCharacter* RequestingAI)
{
	if (!RequestingAI) return nullptr;

	// 0. 拿 Profile.HuntPolicy — 决定后面怎么选
	const FAIHuntPolicy Policy = GetEffectiveHuntPolicy(RequestingAI);

	// 1. 取候选 (同队伍 vs 异阵营已经过滤)
	TArray<ABaseCharacter*> AllEnemies = GetAllAliveEnemiesFor(RequestingAI);
	if (AllEnemies.Num() == 0) return nullptr;

	// 2. 距离预过滤（超过最大追击距离的直接剔除）
	AllEnemies.RemoveAll([this, &Policy, RequestingAI](ABaseCharacter* Enemy)
	{
		if (!Enemy) return true;
		const float Dist = FVector::Dist(Enemy->GetActorLocation(), RequestingAI->GetActorLocation());
		if (Dist > Policy.MaxChaseDistance) return true;
		return false;
	});

	if (AllEnemies.Num() == 0) return nullptr;

	// ============================================================
	// 3. 锁定优先策略（核心新逻辑）
	//
	// 需求规则：
	//   - 优先选"未被任何其他 AI 锁定"的目标
	//   - 已锁定目标只能分配给已锁定它的 AI（通过 ExcludeAI 排除）
	//   - 所有候选都被锁 → 选被锁人数最少的
	//   - 分数相同时取距离最近
	// ============================================================

	TArray<ABaseCharacter*> UnlockedEnemies;
	TArray<ABaseCharacter*> LockedEnemies;

	for (ABaseCharacter* Enemy : AllEnemies)
	{
		if (IsTargetLockedByOthers(Enemy, RequestingAI))
		{
			LockedEnemies.Add(Enemy);
		}
		else
		{
			UnlockedEnemies.Add(Enemy);
		}
	}

	// 优先从"未锁定"列表里选最佳（分数最高的）
	ABaseCharacter* BestTarget = nullptr;
	float BestScore = -1.f;

	// 候选范围：先 try unlocked，没有才用 locked
	TArray<ABaseCharacter*>& PrimaryPool = (UnlockedEnemies.Num() > 0) ? UnlockedEnemies : LockedEnemies;

	for (ABaseCharacter* Enemy : PrimaryPool)
	{
		const float Score = ScoreCandidateForAI(RequestingAI, Enemy, Policy);
		if (Score > BestScore)
		{
			BestScore = Score;
			BestTarget = Enemy;
		}
	}

	// 如果 primary pool 分数相同，用距离打破平局
	if (PrimaryPool.Num() > 1 && BestTarget)
	{
		float BestDist = FVector::Dist(BestTarget->GetActorLocation(), RequestingAI->GetActorLocation());
		for (ABaseCharacter* Enemy : PrimaryPool)
		{
			if (Enemy == BestTarget) continue;
			const float Score = ScoreCandidateForAI(RequestingAI, Enemy, Policy);
			if (FMath::Abs(Score - BestScore) < KINDA_SMALL_NUMBER)
			{
				const float Dist = FVector::Dist(Enemy->GetActorLocation(), RequestingAI->GetActorLocation());
				if (Dist < BestDist)
				{
					BestDist = Dist;
					BestTarget = Enemy;
				}
			}
		}
	}

	// 4. 写入猎人账本（覆盖旧目标）
	if (BestTarget)
	{
		AIHuntingMap.Add(RequestingAI, BestTarget);
	}

	return BestTarget;
}


/**
 * ScoreCandidateForAI — 对单个候选敌人评分 (0~1, 越大越适合)
 *
 * 综合得分 = Σ (权重 × 归一化维度) − 反扎堆惩罚
 *
 * 维度权重 (HuntPolicy 字段):
 *   - DistanceWeight        (距离越近 → 分越高)
 *   - ScoreWeight           (积分越高 → 分越高)
 *   - TimeRemainingWeight   (剩余时间越少 → 分越高, 母体策略)
 *   - AntiHuddleWeight      (被多个 AI 锁定 → 减分)
 *
 * 归一化方式: 距离归一到 [0, MaxChaseDistance]; 积分暂用 PS->GetScore() / 100.f 上限.
 *             剩余时间从 GameState 取 (TODO: GameState 还没暴露 TimeRemainingSec 字段).
 */
float ARoomGameMode::ScoreCandidateForAI(ABaseCharacter* RequestingAI,
	ABaseCharacter* Candidate, const FAIHuntPolicy& Policy) const
{
	if (!RequestingAI || !Candidate) return 0.f;

	// --- 维度归一值 (0~1) ---
	float DistanceScore = 0.f;
	if (Policy.DistanceWeight > 0.f && Policy.MaxChaseDistance > 0.f)
	{
		const float Dist = FVector::Dist(
			Candidate->GetActorLocation(),
			RequestingAI->GetActorLocation());
		DistanceScore = FMath::Clamp(1.f - (Dist / Policy.MaxChaseDistance), 0.f, 1.f);
	}

	float PlayerScoreScore = 0.f;
	if (Policy.ScoreWeight > 0.f)
	{
		// 设计: 玩家积分上限假设 100, 大于 1.0 直接封顶
		const float PS_Score = Candidate->GetPlayerState()
			? Candidate->GetPlayerState()->GetScore() : 0.f;
		PlayerScoreScore = FMath::Clamp(PS_Score / 100.f, 0.f, 1.f);
	}

	float TimeRemainingScore = 0.f;
	if (Policy.TimeRemainingWeight > 0.f)
	{
		// 母体策略专用: 用 GameState.MatchEndTime 反推
		// 没有 TimeRemainingSec 字段时退化为 0.5 (中性), 不影响排名
		if (ARoomGameState* GS = GetGameState<ARoomGameState>())
		{
			const float Secs = GS->GetMatchRemainingSeconds();
			const float MaxSecs = 600.f; // 兜底: 10分钟上限
			TimeRemainingScore = 1.f - FMath::Clamp(Secs / MaxSecs, 0.f, 1.f);
		}
	}

	// --- 反扎堆惩罚 ---
	// 用 IsTargetLockedByOthers: 排除自己锁的情况，只有被别人锁才扣分
	float AntiHuddle = 0.f;
	if (Policy.AntiHuddleWeight > 0.f)
	{
		// IsTargetLockedByOthers 排除 RequestingAI 自己，所以自己锁的不算被占用
		const bool bLockedByOthers = const_cast<ARoomGameMode*>(this)->
			IsTargetLockedByOthers(Candidate, RequestingAI);
		// 被锁 = 惩罚值; 未被锁 = 0
		AntiHuddle = bLockedByOthers ? Policy.AntiHuddleWeight : 0.f;
	}

	// --- 综合加权 ---
	const float Composite =
		Policy.DistanceWeight       * DistanceScore
		+ Policy.ScoreWeight        * PlayerScoreScore
		+ Policy.TimeRemainingWeight* TimeRemainingScore
		- Policy.AntiHuddleWeight   * AntiHuddle;

	return FMath::Clamp(Composite, 0.f, 1.f);
}


/**
 * GetAllAliveEnemiesFor
 *
 * 遍历全场, 找出对这个 AI 来说所有活着的敌人
 * 过滤规则:
 *   1. 不能是空指针
 *   2. 不能是正在请求的 AI 自己
 *   3. 目标必须是活着的
 *   4. 走 IGenericTeamAgentInterface::GetTeamAttitudeTowards (而非 GetTeamID)
 */
TArray<ABaseCharacter*> ARoomGameMode::GetAllAliveEnemiesFor(ABaseCharacter* RequestingAI)
{
	TArray<ABaseCharacter*> AliveEnemies;
	if (!RequestingAI) return AliveEnemies;

	TArray<AActor*> AllCharacters;
	UGameplayStatics::GetAllActorsOfClass(this, ABaseCharacter::StaticClass(), AllCharacters);

	for (AActor* Actor : AllCharacters)
	{
		ABaseCharacter* Char = Cast<ABaseCharacter>(Actor);

		// 过滤条件: 1)非空 2)不是自己 3)不是死亡的
		if (Char && Char != RequestingAI && !Char->GetIsDead())
		{
			// 【Phase 2】阵营过滤走 IGenericTeamAgentInterface 原生协议
			// 不再依赖 GetTeamID() 这个内部 uint8 (Pawn 子类暴露它, 但与项目无关)
			const ETeamAttitude::Type Attitude = RequestingAI->GetTeamAttitudeTowards(*Char);
			if (Attitude == ETeamAttitude::Hostile)
			{
				AliveEnemies.Add(Char);
			}
		}
	}

	return AliveEnemies;
}


/**
 * ReleaseTarget
 *
 * 释放目标记录
 * AI 死亡或想换目标时，从账本里把这个 AI 抹除
 */
void ARoomGameMode::ReleaseTarget(ABaseCharacter* RequestingAI)
{
	if (IsValid(RequestingAI) && AIHuntingMap.Contains(RequestingAI))
	{
		AIHuntingMap.Remove(RequestingAI);
	}
}


/**
 * GetAllAliveEnemiesFor
 *
 * 遍历全场，找出对这个 AI 来说所有活着的敌人
 * 过滤规则:
 *   1. 不能是空指针
 *   2. 不能是正在请求的 AI 自己
 *   3. 目标必须是活着的
 */
/**
 * GetAttackerCount
 *
 * 统计某个敌人正在被几个 AI 追杀
 * 遍历整个猎人账本，数一数 Value 出现次数
 */
int32 ARoomGameMode::GetAttackerCount(ABaseCharacter* TargetEnemy)
{
	int32 Count = 0;
	// 遍历整个猎人账本
	for (const auto& Pair : AIHuntingMap)
	{
		// 如果猎人还没死，并且他的目标正是我们要查的人
		if (IsValid(Pair.Key) && !Pair.Key->IsDead() && Pair.Value == TargetEnemy)
		{
			Count++;
		}
	}
	return Count;
}


/**
 * IsTargetLocked — 某个敌人是否已被锁定（任意 AI 锁定即算锁定）
 * 用于 BT/AI 侧查询：遇到一个敌人时判断"这个人还能不能抢"
 */
bool ARoomGameMode::IsTargetLocked(ABaseCharacter* TargetEnemy)
{
	if (!TargetEnemy) return false;
	// 猎人大于 0 即被锁定
	return GetAttackerCount(TargetEnemy) > 0;
}


/**
 * IsTargetLockedByOthers — 某个敌人是否被指定 AI 以外的人锁定
 * 用于评分系统：排除自己已锁定的情况
 */
bool ARoomGameMode::IsTargetLockedByOthers(ABaseCharacter* TargetEnemy, ABaseCharacter* ExcludeAI)
{
	if (!TargetEnemy) return false;
	int32 Count = 0;
	for (const auto& Pair : AIHuntingMap)
	{
		if (IsValid(Pair.Key) && !Pair.Key->IsDead() && Pair.Value == TargetEnemy && Pair.Key != ExcludeAI)
		{
			Count++;
		}
	}
	return Count > 0;
}


/**
 * CheckAllPlayersReady
 *
 * 检查所有玩家是否都已准备
 * 房主拥有特权（豁免准备状态校验）
 *
 * @return true: 全部准备或只有房主; false: 至少有一个非房主玩家未准备
 */
bool ARoomGameMode::CheckAllPlayersReady()
{
	ARoomGameState* GS = GetGameState<ARoomGameState>();
	if (!GS) return false;

	// 【防呆设计】: 如果房间里没有任何人（理论上不可能），直接拦截
	if (GS->PlayerArray.Num() == 0) return false;

	// 【底层溯源】: 在 Listen Server 架构下，服务器的 FirstPlayerController 必定是本机的房主
	APlayerController* HostPC = Cast<APlayerController>(GetWorld()->GetFirstPlayerController());

	for (APlayerState* GenericPS : GS->PlayerArray)
	{
		if (ARoomPlayerState* PS = Cast<ARoomPlayerState>(GenericPS))
		{
			// 【架构精进】: 通过比对 Controller 引用，精准鉴别当前遍历的 PlayerState 是不是房主的
			bool bIsHost = (PS->GetPlayerController() == HostPC);

			// 🌟 核心修复 1 & 2: 房主拥有特权，豁免准备状态校验
			// 因为房主自己主宰游戏什么时候开始，不需要点击准备
			if (bIsHost)
			{
				continue; // 直接跳过房主的校验，检查下一个人
			}

			// 对于非房主的普通玩家，严格校验其准备状态
			if (PS->CurrentTeam != ERoomTeam::None && !PS->bIsReady)
			{
				// 工业规范: 输出日志，方便后期在后台查出是哪个玩家卡住了进程
				UE_LOG(LogTemp, Warning, TEXT("[RoomGameMode] 拦截开局: 普通玩家 【%s】 尚未准备！"), *PS->GetPlayerName());
				return false; // 只要有一个人没准备，立刻阻断开局
			}
		}
	}

	// 走到这里意味着:
	// 情景 A: 房间里只有房主 1 个人，循环直接 continue 结束 -> 返回 true 允许开局
	// 情景 B: 房间里有多人，且除房主外的所有人都 bIsReady == true -> 返回 true 允许开局

	UE_LOG(LogTemp, Log, TEXT("[RoomGameMode] 准备状态全部校验通过，准许开局！"));
	return true;
}


// ==========================================
// 4. 角色/武器生成系统
// ==========================================

/**
 * HandlePlayerRequestSpawn
 *
 * 接收生成请求，记录数据，然后命令引擎开始原生生成流程
 * 1. 优先从已有缓存读取，再与入参合并（避免空字符串覆盖有效缓存）
 * 2. 兜底默认值: Warrior / Knife
 * 3. 写入 PlayerSpawnDataCache 和 PlayerState
 * 4. 查表获取角色类
 * 5. 根据队伍分配出生点
 * 6. 销毁旧 Pawn，SpawnActor 新角色，Possess
 */
void ARoomGameMode::HandlePlayerRequestSpawn(AController* PlayerToSpawn, const FString& CharRowName, const FString& WeaponRowName)
{
	if (!PlayerToSpawn) return;

	UE_LOG(LogTemp, Warning, TEXT("[Spawn] HandlePlayerRequestSpawn called. Char='%s', Weapon='%s'"),
		*CharRowName, *WeaponRowName);

	// 【核心修复】: 优先从已有缓存读取，再与入参合并
	// 避免空字符串覆盖已有的有效缓存数据
	FString FinalCharID = CharRowName;
	FString FinalWeaponID = WeaponRowName;

	if (FPlayerSpawnData* ExistingCache = PlayerSpawnDataCache.Find(PlayerToSpawn->GetUniqueID()))
	{
		if (FinalCharID.IsEmpty())
		{
			FinalCharID = ExistingCache->CharID;
		}
		if (FinalWeaponID.IsEmpty())
		{
			FinalWeaponID = ExistingCache->WeaponID;
		}
	}

	// 最终结果仍然为空，则用默认值兜底
	// 【2026-07-03 重构】: 不再硬编码 "Warrior" / "Knife", 改走数据驱动兜底 (ResolveFallbackCharacterRow / ResolveFallbackWeaponRow)
	if (FinalCharID.IsEmpty() || FinalCharID == TEXT("Default"))
	{
		FinalCharID = ResolveFallbackCharacterRow();
	}
	if (FinalWeaponID.IsEmpty())
	{
		FinalWeaponID = ResolveFallbackWeaponRow();
	}

	UE_LOG(LogTemp, Log, TEXT("[Spawn] Resolved spawn data: Char='%s', Weapon='%s'"), *FinalCharID, *FinalWeaponID);

	// 写入缓存（供复活时读取）
	FPlayerSpawnData SpawnData;
	SpawnData.CharID = FinalCharID;
	SpawnData.WeaponID = FinalWeaponID;
	PlayerSpawnDataCache.Add(PlayerToSpawn->GetUniqueID(), SpawnData);

	// 同时写入 PlayerState
	if (ARoomPlayerState* PS = PlayerToSpawn->GetPlayerState<ARoomPlayerState>())
	{
		PS->SetPlayerLoadout(FinalCharID, FinalWeaponID, PS->GetSelectedWeapon2ID());
	}

	// ==========================================
	// 【P0 v28 关键修复】如果玩家已经有正确的 Pawn，跳过生成
	// 
	// 问题根因:
	//   - SpawnAllPlayersIntoBattle 定时器可能重复触发
	//   - 每次触发都调用 HandlePlayerRequestSpawn
	//   - HandlePlayerRequestSpawn 销毁当前 Pawn，生成新 Pawn
	//   - 结果: 玩家 Pawn 不断被重建，玩家感觉"角色被替换了"
	//
	// 修复:
	//   - 先查表获取角色类
	//   - 检查当前 Pawn 是否已经是正确的类
	//   - 如果是，跳过销毁和重新生成
	//   - 只有 Pawn 类不匹配时才重新生成
	// ==========================================

	// Step 1: 查表获取角色类
	// 【P0 2026.07.10 v3 修复】用 FinalCharID 而不是 CharRowName
	// 根因: CharRowName 是入参，可能为空，但 FinalCharID 已经解析过（有缓存或默认值）
	TSubclassOf<ABaseCharacter> CharClassToSpawn = nullptr;
	if (!FinalCharID.IsEmpty() && FinalCharID != TEXT("Default") && CharacterDataTable)
	{
		static const FString CharCtx(TEXT("ManualSpawn"));
		if (FCharacterInfo* Info = CharacterDataTable->FindRow<FCharacterInfo>(FName(*FinalCharID), CharCtx))
		{
			if (!Info->CharacterBlueprint.IsNull())
			{
				CharClassToSpawn = Info->CharacterBlueprint.LoadSynchronous();
				UE_LOG(LogTemp, Warning, TEXT("[Spawn] ManualChar lookup '%s' -> %s"),
					*FinalCharID, *GetNameSafe(CharClassToSpawn));
			}
		}
	}
	if (!CharClassToSpawn) CharClassToSpawn = DefaultPawnClass;

	// 【P0 v28】检查是否需要重新生成 Pawn
	// 如果玩家已经有正确的 Pawn，直接跳过生成
	APawn* ExistingPawn = PlayerToSpawn->GetPawn();
	bool bNeedsRespawn = true;

	if (ExistingPawn)
	{
		// 检查当前 Pawn 是否是期望的类
		if (ExistingPawn->GetClass() == CharClassToSpawn)
		{
			// 当前 Pawn 已经是正确的类，跳过销毁和重新生成
			UE_LOG(LogTemp, Warning,
				TEXT("[Spawn] HandlePlayerRequestSpawn: Pawn=%s 已经是正确的类 '%s', 跳过重新生成"),
				*ExistingPawn->GetName(), *GetNameSafe(CharClassToSpawn));
			bNeedsRespawn = false;
		}
		else
		{
			// 当前 Pawn 类不匹配，需要销毁并重新生成
			UE_LOG(LogTemp, Warning,
				TEXT("[Spawn] HandlePlayerRequestSpawn: Pawn 类不匹配, 销毁旧 Pawn: %s (类=%s), 生成新 Pawn: %s"),
				*ExistingPawn->GetName(), *GetNameSafe(ExistingPawn->GetClass()), *GetNameSafe(CharClassToSpawn));
			ExistingPawn->Destroy();
			bNeedsRespawn = true;
		}
	}

	// ==========================================
	// 【新增】: 根据队伍分配出生点
	// ==========================================
	FVector SpawnLoc = FVector::ZeroVector;
	FRotator SpawnRot = FRotator::ZeroRotator;
	AActor* AssignedSpawnPoint = nullptr;

	// 获取玩家的队伍信息
	ERoomTeam PlayerTeam = ERoomTeam::Attack; // 默认攻方
	if (ARoomPlayerState* PS = PlayerToSpawn->GetPlayerState<ARoomPlayerState>())
	{
		PlayerTeam = PS->CurrentTeam;
	}

	// 尝试从队伍对应的出生点列表中分配
	AssignedSpawnPoint = GetAvailableSpawnPointForTeam(PlayerTeam, true);

	// 如果没有找到队伍对应的出生点，回退到引擎默认的 FindPlayerStart（兜底）
	if (!AssignedSpawnPoint)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Spawn] No team-specific spawn found, falling back to default FindPlayerStart"));
		AssignedSpawnPoint = FindPlayerStart(PlayerToSpawn, TEXT(""));
	}

	if (AssignedSpawnPoint)
	{
		SpawnLoc = AssignedSpawnPoint->GetActorLocation();
		SpawnRot = AssignedSpawnPoint->GetActorRotation();
		UE_LOG(LogTemp, Log, TEXT("[Spawn] Using spawn point: %s for team %d at %s"),
			*AssignedSpawnPoint->GetName(), (int32)PlayerTeam, *SpawnLoc.ToString());
	}
	else
	{
		// 兜底: 如果完全找不到出生点，使用默认位置（地图原点上方）
		SpawnLoc = FVector(0.0f, 0.0f, 200.0f);
		SpawnRot = FRotator::ZeroRotator;
		UE_LOG(LogTemp, Error, TEXT("[Spawn] WARNING: No spawn point found! Using default location at (0, 0, 200)"));
	}

	// Step 3: 手动 Spawn 角色（仅当需要重新生成时）
	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = PlayerToSpawn;
	SpawnParams.Instigator = PlayerToSpawn->GetPawn();

	ABaseCharacter* SpawnedChar = nullptr;

	if (bNeedsRespawn)
	{
		SpawnedChar = GetWorld()->SpawnActor<ABaseCharacter>(
			CharClassToSpawn, SpawnLoc, SpawnRot, SpawnParams);

		if (SpawnedChar)
		{
			UE_LOG(LogTemp, Warning, TEXT("[Spawn] ManualSpawn success: %s at %s"),
				*SpawnedChar->GetName(), *SpawnLoc.ToString());

			// 【Phase 1 重构】阵营设置: 走 IGenericTeamAgentInterface 而不是直接 TeamID
			// Melee模式: Attack=攻方(玩家)=Faction.Player, Defense=守方(AI)=Faction.Zombie
			// 注意: HandlePlayerRequestSpawn 只给玩家调用, 所以 PlayerTeam=Attack 对应 Faction.Player
			const FGameplayTag Faction =
				(PlayerTeam == ERoomTeam::Attack)
					? FGameplayTag::RequestGameplayTag(TEXT("Faction.Player"))
					: FGameplayTag::RequestGameplayTag(TEXT("Faction.Zombie"));
			SpawnedChar->SetGenericTeamId(
				ABaseCharacter::ResolveGenericTeamIdFromTag(Faction));

			// Step 4: Possess（会触发 PossessedBy -> SpawnAndEquipWeapon 自动装备武器）
			PlayerToSpawn->Possess(SpawnedChar);
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("[Spawn] ManualSpawn FAILED for Char='%s'"), *CharRowName);
		}
	}
	else
	{
		// 【P0 v29 关键修复】跳过重新生成时，仍然需要 Possess
		//
		// 问题根因:
		//   - 引擎生成 Pawn 后，Controller 可能没有正确 Possess
		//   - 武器装备依赖于 PossessedBy 事件
		//   - 如果不 Possess，武器就不会被装备
		//
		// 修复:
		//   - 检查当前 Pawn 是否有效
		//   - 如果有效，调用 Possess（即使已经 Possess，重复 Possess 也会触发 PossessedBy）
		//   - PossessedBy 中会检查武器是否已装备，不会重复生成武器
		ExistingPawn = PlayerToSpawn->GetPawn();
		if (ExistingPawn && Cast<ABaseCharacter>(ExistingPawn))
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[Spawn] HandlePlayerRequestSpawn: Pawn=%s 存在，调用 Possess 确保武器装备"),
				*ExistingPawn->GetName());
			PlayerToSpawn->Possess(ExistingPawn);
		}
		else
		{
			UE_LOG(LogTemp, Error,
				TEXT("[Spawn] HandlePlayerRequestSpawn: bNeedsRespawn=false 但 Pawn 不存在！需要重新生成"));
		}
	}
}


/**
 * GetDefaultPawnClassForController_Implementation
 *
 * 引擎底层在生成 Actor 前，会来问我们: "应该生成什么类？"
 *
 * 核心修复: 优先从 GameMode 本地缓存读取角色 ID（绕过 PlayerState 复制时序问题）
 */
UClass* ARoomGameMode::GetDefaultPawnClassForController_Implementation(AController* InController)
{
	if (!InController) return nullptr;

	// 【P0 v27 关键修复】对于 AI Controller，不要 fallback 到 DefaultPawnClass
	//
	// 根因:
	//   - AI Controller 调用此函数时，PlayerSpawnDataCache 没有数据（AI 没有 PlayerState）
	//   - 旧代码 fallback 到 DefaultPawnClass，而 DefaultPawnClass = BP_SWAT_C（玩家角色）
	//   - 结果: 引擎使用 BP_SWAT_C 生成 Pawn，AI Controller 控制了玩家角色！
	//
	// 修复:
	//   - 对于 AAIController，直接返回 nullptr，让引擎跳过默认 Pawn 生成
	//   - AI 的 Pawn 生成完全由我们的 RequestRespawn -> SpawnAIInternal 流程控制
	if (Cast<AAIController>(InController))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Spawn] GetDefaultPawnClass: AI Controller=%s, 返回 nullptr (AI Pawn 由 RequestRespawn 控制)"),
			*InController->GetName());
		return nullptr;
	}

	FString CharID = TEXT("(no cache)");
	// 【核心修复】: 优先从 GameMode 本地缓存读取角色 ID（绕过 PlayerState 复制时序问题）
	FPlayerSpawnData* CachedData = PlayerSpawnDataCache.Find(InController->GetUniqueID());
	if (CachedData && !CachedData->CharID.IsEmpty())
	{
		CharID = CachedData->CharID;
		UE_LOG(LogTemp, Warning, TEXT("[Spawn] GetDefaultPawnClass (from cache) ControllerID=%d, CharID='%s'"),
			InController->GetUniqueID(), *CharID);
	}
	else
	{
		// 兜底: 从 PlayerState 读
		if (ARoomPlayerState* PS = InController->GetPlayerState<ARoomPlayerState>())
		{
			CharID = PS->GetSelectedCharacterID();
		}
		UE_LOG(LogTemp, Warning, TEXT("[Spawn] GetDefaultPawnClass (from PS fallback) ControllerID=%d, CharID='%s'"),
			InController->GetUniqueID(), *CharID);
	}

	if (!CharID.IsEmpty() && CharID != TEXT("Default") && CharacterDataTable)
	{
		static const FString ContextString(TEXT("CharacterSpawnContext"));
		FCharacterInfo* Info = CharacterDataTable->FindRow<FCharacterInfo>(FName(*CharID), ContextString);

		UE_LOG(LogTemp, Warning, TEXT("[Spawn] FindRow('%s') -> Info=%s, Blueprint=%s"),
			*CharID,
			Info ? TEXT("FOUND") : TEXT("NULL"),
			(Info && !Info->CharacterBlueprint.IsNull()) ? TEXT("SET") : TEXT("NULL"));

		if (Info && !Info->CharacterBlueprint.IsNull())
		{
			return Info->CharacterBlueprint.LoadSynchronous();
		}
	}

	// 【P0 v27】玩家 Controller fallback 到 DefaultPawnClass
	// AI Controller 在前面已经返回 nullptr，所以不会走到这里
	UE_LOG(LogTemp, Warning, TEXT("[Spawn] Falling back to DefaultPawnClass=%s"),
		DefaultPawnClass ? *DefaultPawnClass->GetName() : TEXT("NULL"));
	return DefaultPawnClass;
}


/**
 * RestartPlayer
 *
 * 引擎底层 RestartPlayer 的钩子
 * 作用: 如果有缓存数据，使用自定义的生成逻辑（确保使用基于队伍的出生点）
 *
 * 【P0 2026.07.10 v27 关键修复】防止与 SpawnAllPlayersIntoBattle 冲突
 *
 * 问题:
 *   - 日志显示游戏开始时出现 "[Spawn] Falling back to DefaultPawnClass"
 *   - 这说明引擎自动调用了 RestartPlayer (生成了 Pawn)
 *   - 然后 SpawnAllPlayersIntoBattle 又调用了 HandlePlayerRequestSpawn (销毁旧 Pawn, 生成新 Pawn)
 *   - 这导致了额外的 Pawn 生成和销毁，可能造成混乱
 *
 * 修复:
 *   - 在 SpawnAllPlayersIntoBattle 期间，RestartPlayer 直接返回，不干扰
 *   - 因为 SpawnAllPlayersIntoBattle 已经正确处理了玩家生成
 */
void ARoomGameMode::RestartPlayer(AController* NewPlayer)
{
	if (!NewPlayer) return;

	// 【P0 v27】如果正在 SpawnAllPlayersIntoBattle 期间，不允许 RestartPlayer 干扰
	// SpawnAllPlayersIntoBattle 会在 bSpawnInProgress=true 时进行玩家生成
	// 这防止了引擎的自动 RestartPlayer 与我们手动调用 SpawnAllPlayersIntoBattle 冲突
	// 注意: 复活流程中 bSpawnInProgress 应该是 false, 所以复活不受影响
	if (bSpawnInProgress)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[Spawn] RestartPlayer 跳过: bSpawnInProgress=true, SpawnAllPlayersIntoBattle 正在执行中"));
		return;
	}

	// 从缓存中读取角色和武器数据
	FPlayerSpawnData* CachedData = PlayerSpawnDataCache.Find(NewPlayer->GetUniqueID());

	// 如果有缓存数据，使用自定义的生成逻辑（确保使用基于队伍的出生点）
	if (CachedData)
	{
		// 【2026-07-03 重构】: 兜底走数据驱动 (不再硬编码 "Warrior"/"Knife")
		FString CharID = CachedData->CharID.IsEmpty() ? ResolveFallbackCharacterRow() : CachedData->CharID;
		FString WeaponID = CachedData->WeaponID.IsEmpty() ? ResolveFallbackWeaponRow() : CachedData->WeaponID;

		UE_LOG(LogTemp, Warning, TEXT("[Spawn] RestartPlayer (cached): Char='%s', Weapon='%s'"), *CharID, *WeaponID);

		// 调用自定义生成函数，确保使用队伍对应的出生点
		HandlePlayerRequestSpawn(NewPlayer, CharID, WeaponID);
		return;
	}

	// 如果没有缓存数据（理论上不应该走到这里，但作为兜底）
	UE_LOG(LogTemp, Warning, TEXT("[Spawn] RestartPlayer: No cache found, falling back to Super"));
	Super::RestartPlayer(NewPlayer);
}


/**
 * RequestRespawn — 统一复活入口 (玩家 & AI 共用)
 *
 * 【大厂架构重构 2026.07.06】
 *
 * 设计原则:
 *   - 复活逻辑集中化, Die() 只负责死亡事件派发, 复活决策由 GameMode 统一处理
 *   - 玩家复活走 HandlePlayerRequestSpawn (复用开局生成逻辑, 保证角色/武器/出生点一致)
 *   - AI 复活走 AIProfile 驱动的新 Pawn 生成, 复用 SpawnAIInternal 流程
 *   - 支持延迟复活 (bImmediateRespawn=false 时走复活定时器, true 时立即复活)
 *
 * @param DeadController  死亡的 Controller (玩家 ARoomPlayerController 或 AI AAIController)
 * @param bImmediateRespawn  true=立即复活, false=使用复活延迟定时器
 */
void ARoomGameMode::RequestRespawn(AController* DeadController, bool bImmediateRespawn)
{
	if (!DeadController)
	{
		UE_LOG(LogTemp, Error, TEXT("[Respawn] RequestRespawn: DeadController is null!"));
		return;
	}

	UE_LOG(LogTemp, Warning,
		TEXT("[Respawn] RequestRespawn: Controller=%s, Type=%s, bImmediate=%d"),
		*DeadController->GetName(),
		*GetNameSafe(DeadController->GetClass()),
		bImmediateRespawn);

	// 根据 Controller 类型分发给不同复活路径
	if (Cast<ARoomPlayerController>(DeadController))
	{
		// ======================================
		// 路径 A: 玩家复活 (复用 HandlePlayerRequestSpawn)
		// ======================================
		FPlayerSpawnData* CachedData = PlayerSpawnDataCache.Find(DeadController->GetUniqueID());
		if (CachedData)
		{
			FString CharID   = CachedData->CharID.IsEmpty() ? ResolveFallbackCharacterRow() : CachedData->CharID;
			FString WeaponID = CachedData->WeaponID.IsEmpty() ? ResolveFallbackWeaponRow() : CachedData->WeaponID;

			if (bImmediateRespawn)
			{
				// 立即复活: 直接调生成逻辑
				UE_LOG(LogTemp, Warning, TEXT("[Respawn] Player immediate respawn: Char='%s', Weapon='%s'"), *CharID, *WeaponID);
				HandlePlayerRequestSpawn(DeadController, CharID, WeaponID);
			}
			else
			{
				// 延迟复活: 使用 TFunction + lambda 捕获变量
				// UE 5.6 SetTimer 优先接受 TFunction, 避免了 BindUObject 的 const-ref 参数匹配问题
				TWeakObjectPtr<AController> ControllerWeak = DeadController;
				const FString CharIDCopy = CharID;
				const FString WeaponIDCopy = WeaponID;
				FTimerHandle LocalRespawnHandle;
				GetWorld()->GetTimerManager().SetTimer(LocalRespawnHandle,
					[this, ControllerWeak, CharIDCopy, WeaponIDCopy]()
					{
						if (AController* C = ControllerWeak.Get())
						{
							HandlePlayerRequestSpawn(C, CharIDCopy, WeaponIDCopy);
						}
					},
					RespawnDelaySeconds, false);
				UE_LOG(LogTemp, Warning, TEXT("[Respawn] Player delayed respawn scheduled in %.1f seconds"), RespawnDelaySeconds);
			}
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("[Respawn] Player has no spawn cache, skipping respawn"));
		}
	}
	else if (Cast<AAIController>(DeadController))
	{
		// ======================================
		// 路径 B: AI 复活 (复用 SpawnAIInternal 流程)
		// ======================================
		if (ABaseAIController* BaseAIC = Cast<ABaseAIController>(DeadController))
		{
			UAIProfileAsset* Profile = BaseAIC->GetCurrentProfile();

			// 从 GameState 读取当前 MatchMode (GameMode 本身没有 CurrentMatchMode 成员)
			ERoomMatchMode MatchMode = ERoomMatchMode::Melee;
			if (ARoomGameState* GS = GetGameState<ARoomGameState>())
			{
				MatchMode = GS->CurrentMatchMode;
			}

			// 【P0 2026.07.10 v24 大厂架构修复】AI 复活 Team 派生
			ERoomTeam RespawnTeam = ERoomTeam::Defense;
			{
				const FGenericTeamId DeadTeamId = BaseAIC->GetGenericTeamId();
				const int32 DeadTeamInt = DeadTeamId.GetId();
				if (DeadTeamInt == 1) // Attack 阵营 TeamID=1
				{
					RespawnTeam = ERoomTeam::Attack;
				}
				else if (DeadTeamInt == 2) // Defense 阵营 TeamID=2
				{
					RespawnTeam = ERoomTeam::Defense;
				}
				else if (Profile && Profile->FactionTag.IsValid())
				{
					const FString TagStr = Profile->FactionTag.ToString();
					if (TagStr.Contains(TEXT("Offense")) || TagStr.Contains(TEXT("Attack")))
					{
						RespawnTeam = ERoomTeam::Attack;
					}
					else if (TagStr.Contains(TEXT("Defense")))
					{
						RespawnTeam = ERoomTeam::Defense;
					}
				}
			}

			// 构建 AI 复活请求
			FAISpawnRequest Request;
			Request.Mode  = MatchMode;
			Request.Team  = RespawnTeam;
			Request.Count = 1;

			// 【P0 2026.07.10 v24 关键修复】CharacterRowName 必须有值
			// 优先级: Profile->CharacterRowName > "Grunt" (AI 默认角色行名)
			Request.CharacterRowName = NAME_None;
			FString ProfileCharRowName = TEXT("(none)");
			if (Profile && !Profile->CharacterRowName.IsNone())
			{
				ProfileCharRowName = Profile->CharacterRowName.ToString();
				
				// 【P0 2026.07.10 v26 关键验证】防止 AI 使用玩家角色 ID
				// 
				// 根因: DA_AIProfile_MeleeGrunt.CharacterRowName 被错误配置为 'JS001' (玩家角色 ID)
				// 结果: AI 复活时查表得到 BP_SWAT (玩家 Blueprint), AIController 控制玩家角色
				//
				// 检测规则: JS 开头的 ID 通常是玩家角色, AI 应该使用非 JS 前缀的 ID
				// (如果你的 AI 角色 ID 也以 JS 开头, 请修改这里的检测逻辑)
				if (ProfileCharRowName.StartsWith(TEXT("JS")))
				{
					UE_LOG(LogTemp, Error,
						TEXT("[Respawn] AI 复活配置错误: Profile.CharacterRowName='%s' 看起来是玩家角色 ID!"
							 " AI 不应该使用玩家角色 Blueprint。"
							 " 请在 DA_AIProfile_MeleeGrunt 中将 CharacterRowName 改为 AI 角色 ID (如 'Grunt')。"
							 " 当前使用默认 'Grunt' 作为安全兜底, AI 会正常复活."),
						*ProfileCharRowName);
					Request.CharacterRowName = FName(TEXT("Grunt"));
				}
				else
				{
					Request.CharacterRowName = Profile->CharacterRowName;
				}
			}
			else
			{
				// 【P0 v24】当 Profile->CharacterRowName 为空时，使用默认 AI 角色行名
				Request.CharacterRowName = FName(TEXT("Grunt"));
				UE_LOG(LogTemp, Warning,
					TEXT("[Respawn] AI 复活: Profile->CharacterRowName 为空, 使用默认 'Grunt' 作为角色行名"));
			}

			// 【P0 2026.07.10 v24 关键修复】AIController 不销毁，只重新生成 Pawn
			// 
			// 用户明确要求: "AIController 要一直存在才对吧？"
			// 
			// 旧架构问题:
			//   - Die() 销毁 Pawn, RequestRespawn 销毁 Controller
			//   - 每次复活都创建新的 Controller, 导致 Controller 数量不断增加
			//
			// 新架构 (大厂标准):
			//   - Controller 一直存在, 只销毁并重新生成 Pawn
			//   - 新 Pawn 被同一个 Controller Possess
			//   - 这样 Controller 的 Profile/BT/状态全部保留
			//
			// 流程:
			//   1. Die() 销毁 Pawn (已完成)
			//   2. Controller 变成无 Pawn 状态
			//   3. RequestRespawn 只生成新 Pawn, Possess 给现有 Controller
			//   4. Controller 保持不变

			// 【P0 v24】只生成 Pawn, 复用现有 Controller, 不销毁 Controller
			UE_LOG(LogTemp, Warning,
				TEXT("[Respawn] AI 复活: Controller=%s 复用, 只生成新 Pawn"),
				*DeadController->GetName());

			// 查表获取角色类
			TSubclassOf<ABaseCharacter> AIPawnClass = nullptr;
			FString BlueprintPath = TEXT("(null)");
			if (CharacterDataTable)
			{
				static const FString Ctx(TEXT("AIRespawn"));
				if (FCharacterInfo* Row = CharacterDataTable->FindRow<FCharacterInfo>(Request.CharacterRowName, Ctx))
				{
					if (!Row->CharacterBlueprint.IsNull())
					{
						AIPawnClass = Row->CharacterBlueprint.LoadSynchronous();
						BlueprintPath = Row->CharacterBlueprint.GetLongPackageName();
						UE_LOG(LogTemp, Warning,
							TEXT("[Respawn] AI 复活查表: CharacterRowName='%s' -> Blueprint='%s' -> Class='%s'"),
							*Request.CharacterRowName.ToString(),
							*BlueprintPath,
							*GetNameSafe(AIPawnClass));
					}
					else
					{
						UE_LOG(LogTemp, Error,
							TEXT("[Respawn] AI 复活查表失败: Row.CharacterBlueprint 为空, CharacterRowName='%s'"),
							*Request.CharacterRowName.ToString());
					}
				}
				else
				{
					UE_LOG(LogTemp, Error,
						TEXT("[Respawn] AI 复活查表失败: CharacterDataTable 中没有 '%s' 这一行"),
						*Request.CharacterRowName.ToString());
				}
			}
			else
			{
				UE_LOG(LogTemp, Error,
					TEXT("[Respawn] AI 复活查表失败: CharacterDataTable 未设置"));
			}

			if (!AIPawnClass)
			{
				UE_LOG(LogTemp, Error,
					TEXT("[Respawn] AI 复活失败: 查不到 PawnClass, CharacterRowName='%s'"),
					*Request.CharacterRowName.ToString());
				return;
			}

			// 获取出生点
			FVector SpawnLoc = FVector::ZeroVector;
			FRotator SpawnRot = FRotator::ZeroRotator;
			ScanAndCachePlayerStarts(false);
			if (AActor* SpawnPt = GetAvailableSpawnPointForTeam(Request.Team, true))
			{
				SpawnLoc = SpawnPt->GetActorLocation();
				SpawnRot = SpawnPt->GetActorRotation();
			}

			// 生成新 Pawn
			FActorSpawnParameters PawnSP;
			PawnSP.Owner = DeadController;
			PawnSP.Instigator = nullptr;

			ABaseCharacter* NewPawn = GetWorld()->SpawnActor<ABaseCharacter>(
				AIPawnClass, SpawnLoc, SpawnRot, PawnSP);

			// 【P0 v27 修复】AI 复活失败时，必须返回错误，不能 fallback 到 DefaultPawnClass
			// 
			// 根因: 
			//   - 当 GetAvailableSpawnPointForTeam 返回 nullptr 时，SpawnLoc = 原点
			//   - 原点有碰撞，SpawnActor 失败
			//   - 旧代码 fallback 到 DefaultPawnClass（可能是 BP_SWAT_C，玩家角色）
			//   - 结果: AI Controller 控制了玩家角色！
			//
			// 修复:
			//   - SpawnActor 失败时，直接返回错误，不 fallback
			//   - 这样可以清楚地看到 AI 复活失败的日志
			//   - 同时添加出生点诊断日志
			if (!NewPawn)
			{
				UE_LOG(LogTemp, Error,
					TEXT("[Respawn] AI Pawn Spawn 失败: Class=%s, SpawnLoc=%s, SpawnRot=%s, 原因可能是出生点不可用或位置有碰撞"),
					*GetNameSafe(AIPawnClass),
					*SpawnLoc.ToString(),
					*SpawnRot.ToString());

				// 【P0 v27】添加出生点诊断
				UE_LOG(LogTemp, Warning, TEXT("[Respawn] 出生点诊断: Team=%d, AttackStarts=%d, DefenseStarts=%d"),
					Request.Team, AttackSpawnPoints.Num(), DefenseSpawnPoints.Num());
				if (AttackSpawnPoints.Num() > 0)
				{
					UE_LOG(LogTemp, Warning, TEXT("[Respawn] Attack 出生点列表:"));
					for (APlayerStart* Start : AttackSpawnPoints)
					{
						UE_LOG(LogTemp, Warning, TEXT("  - %s @ %s"),
							*Start->GetName(), *Start->GetActorLocation().ToString());
					}
				}

				return;
			}

			// 设置武器配置 (与 SpawnAIInternal 一致)
			if (Profile)
			{
				FString DesiredCharID = Profile->CharacterRowName.IsNone()
					? Request.CharacterRowName.ToString()
					: Profile->CharacterRowName.ToString();
				FString DesiredWeaponID = Profile->WeaponID.IsNone() ? FString() : Profile->WeaponID.ToString();
				NewPawn->SetSpawnLoadout(DesiredCharID, DesiredWeaponID);
			}

			// 【P0 2026.07.10 v25 关键修复】Possess 不触发 OnPossess
			// 
			// 根因分析:
			//   - OnPossess 只在引擎第一次将 Controller 附身到 Pawn 时调用
			//   - 当 Controller 已经存在时 (像 AI 复活这种场景), Possess 不会再次调用 OnPossess
			//   - 结果: SetupMeleeAI 不会被调用, AI 没有 Profile/BT/武器
			//
			// 修复:
			//   - Possess 之后显式调用 SetupMeleeAI
			//   - 这与关卡预放 AI 的 OnPossess 自举逻辑完全一致
			//   - Profile 强引用保证 SetupMeleeAI 不会拿到 nullptr
			DeadController->Possess(NewPawn);
			if (AMeleeAIController* MeleeAIC = Cast<AMeleeAIController>(DeadController))
			{
				MeleeAIC->SetupMeleeAI(Profile);
				UE_LOG(LogTemp, Warning,
					TEXT("[Respawn] AI 复活: SetupMeleeAI 已调用, Controller=%s, Pawn=%s, Profile=%s"),
					*DeadController->GetName(), *NewPawn->GetName(), *GetNameSafe(Profile));
			}
			else
			{
				UE_LOG(LogTemp, Warning,
					TEXT("[Respawn] AI 复活成功 (非 Melee AIC): Controller=%s, Pawn=%s"),
					*DeadController->GetName(), *NewPawn->GetName());
			}
		}
	}
}



// ==========================================
// 5. 比赛开始流程
// ==========================================

/**
 * RequestStartGame
 *
 * 接收并处理玩家请求开始游戏的指令 (仅服务器运行)
 * 步骤:
 *   1. 身份鉴权: 必须是房主
 *   2. 测试开关短路拦截
 *   3. 业务逻辑校验: 检查是否全员准备
 *   4. 校验通过 -> 调 PerformGameStart
 */
void ARoomGameMode::RequestStartGame(AController* RequestingController)
{
	// 工业级防呆: 任何涉及指针的操作必须先做安全校验
	if (!IsValid(RequestingController))
	{
		return;
	}

	// 1. 【身份鉴权】: 判断发起请求的人是否为房主
	// 在 Listen Server (局域网) 架构中，World 的 FirstPlayerController 就是本机的房主
	AController* HostController = GetWorld()->GetFirstPlayerController();
	if (RequestingController != HostController)
	{
		UE_LOG(LogTemp, Warning, TEXT("[RoomGameMode] 拒绝开局请求: 该玩家不是房主！"));
		return;
	}

	// 2. 【测试开关短路拦截】: 如果开启了无视大厅直接测试，强制开局
	if (bSkipRoomPhaseForTesting)
	{
		UE_LOG(LogTemp, Log, TEXT("[RoomGameMode] 测试模式开启，无视准备状态，强制开局！"));
		PerformGameStart();
		return;
	}

	// 3. 【业务逻辑校验】: 检查是否全员准备就绪
	if (!CheckAllPlayersReady())
	{
		// 校验失败: 向全房间广播系统红字/绿字提示
		BroadcastSystemMessage(TEXT("无法开始游戏: 还有玩家未准备就绪！"));
		return;
	}

	// 4. 校验全部通过，移交权限给核心执行器
	PerformGameStart();
}


/**
 * PerformGameStart
 *
 * 权威校验通过后，真正执行开局指令下发与状态流转
 * 1. 更新房间状态为 BattleInProgress
 * 2. 开启倒计时（同步 MatchEndTime 到所有客户端）
 * 3. 通知所有客户端切换 UI（隐藏房间UI，显示战斗HUD）
 * 4. 延迟 MatchStartDelay 秒后生成所有玩家
 */
void ARoomGameMode::PerformGameStart()
{
	// 1. 更新房间状态
	CurrentRoomState = ERoomState::BattleInProgress;

	// 2. 【核心更新】: 先开启倒计时，把 MatchEndTime 同步到所有客户端
	//    这样当 Client_TransitToMatchState 触发 HUD 显示时，值已经是正确的了
	StartMatchTimer();

	// 3. 下发 Client RPC 给所有客户端，让他们立刻切 UI（此时倒计时已经广播完毕）
	for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
	{
		if (ARoomPlayerController* PC = Cast<ARoomPlayerController>(It->Get()))
		{
			// 通知客户端立刻隐藏房间UI，打开准星血条HUD
			PC->Client_TransitToMatchState(EMatchState::Battleing);
		}
	}

	// 4. 延迟生成角色，给客户端 UI 切换留出时间
	BroadcastSystemMessage(FString::Printf(TEXT("游戏将在 %.1f 秒后开始..."), MatchStartDelay));

	GetWorld()->GetTimerManager().SetTimer(
		MatchStartTimerHandle,
		this,
		&ARoomGameMode::SpawnAllPlayersIntoBattle,
		MatchStartDelay,
		false // 只执行一次
	);

	// 【P0 架构升级 2026.07.06 17:00】广播战斗开始事件
	// 解决: AI 出生后立即 RunBehaviorTree, 玩家还在大厅就追玩家 (用户原话"还没点开始游戏 AI 就开始攻击人")
	// AI 控制器订阅 OnBattleStarted, 收到后才会激活 BT (RunBehaviorTree)
	// 幂等保护: 多次调用 PerformGameStart 不会重复广播 (例如调试重启)
	if (!bBattleStartedBroadcasted)
	{
		bBattleStartedBroadcasted = true;
		OnBattleStarted.Broadcast();
		UE_LOG(LogTemp, Log, TEXT("[RoomGameMode] PerformGameStart: OnBattleStarted 已广播 (AI 将激活 BT)"));
	}
}


/**
 * SpawnAllPlayersIntoBattle
 *
 * 倒计时结束后触发，负责遍历所有人并生成真实的 3D 角色
 * 1. 先扫描并缓存出生点
 * 2. 遍历 GameState.PlayerArray
 * 3. 为每个玩家调用 HandlePlayerRequestSpawn
 *
 * 【P0 2026.07.10 v27】bSpawnInProgress 标志用于防止 RestartPlayer 干扰
 */
void ARoomGameMode::SpawnAllPlayersIntoBattle()
{
	// 【P0 v27】设置标志，防止 RestartPlayer 干扰
	bSpawnInProgress = true;

	UE_LOG(LogTemp, Warning, TEXT("[Spawn] SpawnAllPlayersIntoBattle called!"));

	// 【新增】: 在生成玩家前，先扫描并缓存地图中的所有出生点
	ScanAndCachePlayerStarts(true);

	// 遍历当前的 GameState 中所有成功建立连接的 PlayerState
	if (ARoomGameState* GS = GetGameState<ARoomGameState>())
	{
		UE_LOG(LogTemp, Warning, TEXT("[Spawn] PlayerArray has %d players"), GS->PlayerArray.Num());
		for (APlayerState* GenericPS : GS->PlayerArray)
		{
			if (ARoomPlayerState* PS = Cast<ARoomPlayerState>(GenericPS))
			{
				UE_LOG(LogTemp, Warning, TEXT("[Spawn] PS='%s', Owner=%s, CharID='%s', WeaponID='%s'"),
					*PS->GetPlayerName(),
					*GetNameSafe(PS->GetOwner()),
					*PS->GetSelectedCharacterID(),
					*PS->GetSelectedWeapon1ID());
				// 获取这个 PlayerState 对应的 Controller
				if (AController* PlayerController = Cast<AController>(PS->GetOwner()))
				{
					// 【修复】: 全面使用 Getter 替换被删除的本地变量
					// 【2026-07-03 重构】: 兜底走数据驱动 (不再硬编码 "Warrior"/"Knife")
					FString FinalChar = PS->GetSelectedCharacterID().IsEmpty()
						? ResolveFallbackCharacterRow()
						: PS->GetSelectedCharacterID();
					FString FinalWeapon = PS->GetSelectedWeapon1ID().IsEmpty()
						? ResolveFallbackWeaponRow()
						: PS->GetSelectedWeapon1ID();

					// 调用您已经写好的 HandlePlayerRequestSpawn 进行生成
					HandlePlayerRequestSpawn(PlayerController, FinalChar, FinalWeapon);
				}
				else
				{
					UE_LOG(LogTemp, Warning, TEXT("[Spawn] WARNING: PS->GetOwner() is NULL for '%s'!"), *PS->GetPlayerName());
				}
			}
		}
	}

	BroadcastSystemMessage(TEXT("战斗开始！"));

	// 【P0 v27】清除标志，允许 RestartPlayer 正常工作
	bSpawnInProgress = false;
}


// ==========================================
// 6. 出生点扫描与分配
// ==========================================

/**
 * ScanAndCachePlayerStarts
 *
 * 在游戏开始时扫描地图中的所有 PlayerStart，按名称前缀分类存储
 * - "Attack" / "Attacker" 前缀 -> 攻方
 * - "Defense" / "Defender" 前缀 -> 守方
 * - 其他 -> 默认攻方（向后兼容）
 */
void ARoomGameMode::ScanAndCachePlayerStarts(bool bReScan)
{
	// 如果已经扫描过且不是强制重新扫描，则跳过
	if (bSpawnPointsScanned && !bReScan)
	{
		return;
	}

	// 清空旧的缓存数据
	AttackSpawnPoints.Empty();
	DefenseSpawnPoints.Empty();
	OccupiedSpawnPoints.Empty();

	// 获取当前地图中所有的 PlayerStart Actor
	TArray<AActor*> AllPlayerStarts;
	UGameplayStatics::GetAllActorsOfClass(this, APlayerStart::StaticClass(), AllPlayerStarts);

	UE_LOG(LogTemp, Warning, TEXT("[Spawn] Scanning %d PlayerStarts..."), AllPlayerStarts.Num());

	// 遍历所有 PlayerStart，按名称前缀分类
	for (AActor* Actor : AllPlayerStarts)
	{
		if (APlayerStart* PS = Cast<APlayerStart>(Actor))
		{
			FString StartName = PS->GetName();

			// 大小写不敏感的比较，兼容 "attack", "Attack", "ATTACK" 等写法
			if (StartName.Contains(TEXT("Attack"), ESearchCase::IgnoreCase) ||
				StartName.Contains(TEXT("Attacker"), ESearchCase::IgnoreCase))
			{
				AttackSpawnPoints.Add(PS);
				UE_LOG(LogTemp, Log, TEXT("[Spawn] Found Attack spawn point: %s"), *StartName);
			}
			else if (StartName.Contains(TEXT("Defense"), ESearchCase::IgnoreCase) ||
				StartName.Contains(TEXT("Defender"), ESearchCase::IgnoreCase))
			{
				DefenseSpawnPoints.Add(PS);
				UE_LOG(LogTemp, Log, TEXT("[Spawn] Found Defense spawn point: %s"), *StartName);
			}
			else
			{
				// 未分类的 PlayerStart，默认加入攻方（向后兼容）
				AttackSpawnPoints.Add(PS);
				UE_LOG(LogTemp, Warning, TEXT("[Spawn] PlayerStart '%s' has no team prefix, defaulting to Attack"), *StartName);
			}
		}
	}

	// 输出统计日志
	UE_LOG(LogTemp, Warning, TEXT("[Spawn] Spawn point scan complete: %d Attack, %d Defense"),
		AttackSpawnPoints.Num(), DefenseSpawnPoints.Num());

	// 如果没有找到任何出生点，输出警告
	if (AttackSpawnPoints.Num() == 0 && DefenseSpawnPoints.Num() == 0)
	{
		UE_LOG(LogTemp, Error, TEXT("[Spawn] WARNING: No valid PlayerStarts found! Make sure your map has PlayerStarts named with 'Attack' or 'Defense' prefix."));
	}

	bSpawnPointsScanned = true;
}


/**
 * GetAvailableSpawnPointForTeam
 *
 * 根据玩家所属队伍获取一个未被占用的出生点
 * 复活时使用:
 *   - 第一轮: 优先分配未被占用的点
 *   - 第二轮: 如果都用过了则随机分配（允许出生点复用）
 */
AActor* ARoomGameMode::GetAvailableSpawnPointForTeam(ERoomTeam PlayerTeam, bool bRemoveOccupied)
{
	// 根据队伍选择对应的出生点列表
	TArray<class APlayerStart*>* TeamSpawns = nullptr;

	if (PlayerTeam == ERoomTeam::Attack)
	{
		TeamSpawns = &AttackSpawnPoints;
	}
	else if (PlayerTeam == ERoomTeam::Defense)
	{
		TeamSpawns = &DefenseSpawnPoints;
	}

	// 如果队伍无效或没有对应的出生点列表，返回 nullptr
	if (!TeamSpawns || TeamSpawns->Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Spawn] No spawn points available for team %d"), (int32)PlayerTeam);
		return nullptr;
	}

	// 【核心逻辑】: 优先找一个未被占用的出生点
	for (APlayerStart* SpawnPoint : (*TeamSpawns))
	{
		if (SpawnPoint && !OccupiedSpawnPoints.Contains(SpawnPoint))
		{
			// 找到一个空闲的出生点
			if (bRemoveOccupied)
			{
				OccupiedSpawnPoints.Add(SpawnPoint);
				UE_LOG(LogTemp, Log, TEXT("[Spawn] Allocated spawn point: %s (Team=%d)"), *SpawnPoint->GetName(), (int32)PlayerTeam);
			}
			return SpawnPoint;
		}
	}

	// 第二轮: 如果所有出生点都被占用，随机选择一个（允许出生点复用，避免玩家无法复活）
	int32 RandomIndex = FMath::RandRange(0, TeamSpawns->Num() - 1);
	APlayerStart* SelectedSpawn = (*TeamSpawns)[RandomIndex];

	if (SelectedSpawn)
	{
		UE_LOG(LogTemp, Warning, TEXT("[Spawn] All spawn points occupied, reusing: %s (Team=%d)"), *SelectedSpawn->GetName(), (int32)PlayerTeam);

		// 即使复用，也需要更新占用记录（替换旧的占用者）
		if (bRemoveOccupied)
		{
			OccupiedSpawnPoints.Add(SelectedSpawn);
		}
	}

	return SelectedSpawn;
}


/**
 * ReleaseSpawnPoint
 *
 * 释放已占用的出生点
 * 玩家离开（断开连接或退出房间）时调用
 */
void ARoomGameMode::ReleaseSpawnPoint(AActor* PlayerStart)
{
	if (!PlayerStart)
	{
		return;
	}

	// 尝试将传入的 Actor 转换为 PlayerStart
	if (APlayerStart* PS = Cast<APlayerStart>(PlayerStart))
	{
		if (OccupiedSpawnPoints.Contains(PS))
		{
			OccupiedSpawnPoints.Remove(PS);
			UE_LOG(LogTemp, Log, TEXT("[Spawn] Released spawn point: %s"), *PS->GetName());
		}
	}
}


/**
 * ResetAllSpawnPointOccupancy
 *
 * 强制重置所有出生点的占用状态
 * 调用时机: 每回合/每局开始时
 */
void ARoomGameMode::ResetAllSpawnPointOccupancy()
{
	OccupiedSpawnPoints.Empty();
	UE_LOG(LogTemp, Warning, TEXT("[Spawn] All spawn point occupancy reset."));
}


/**
 * GetPlayerSpawnData
 *
 * 获取玩家生成数据缓存的接口（供 BaseCharacter 复活时使用）
 */
bool ARoomGameMode::GetPlayerSpawnData(uint32 ControllerUniqueID, FString& OutCharID, FString& OutWeaponID) const
{
	if (const FPlayerSpawnData* CachedData = PlayerSpawnDataCache.Find(ControllerUniqueID))
	{
		OutCharID = CachedData->CharID;
		OutWeaponID = CachedData->WeaponID;
		return true;
	}
	return false;
}


// ==========================================
// 7. 比赛计时器系统
// ==========================================

/**
 * StartMatchTimer
 *
 * 核心函数: 根据模式初始化并开启倒计时
 * 1. 通知 UI 当前模式（让 UI 决定显示回合数）
 * 2. 根据模式设置 MatchEndTime
 * 3. 重置双方击杀统计
 * 4. 启动 1 秒执行一次的循环定时器
 */
void ARoomGameMode::StartMatchTimer()
{
	ARoomGameState* RoomGS = GetGameState<ARoomGameState>();
	if (!RoomGS) return;

	// 先通知 UI 当前是什么模式，让 MatchInfoWidget 决定 Text_RemainingRounds 的显示/隐藏
	RoomGS->OnMatchModeChanged.Broadcast(RoomGS->CurrentMatchMode);

	// 根据当前模式设置初始时间（转换为秒）
	switch (RoomGS->CurrentMatchMode)
	{
	case ERoomMatchMode::Melee:
		// 设置绝对结束时间 = 当前世界时间 + 设定秒数（MeleeMatchDurationSeconds 由蓝图配置）
		RoomGS->MatchEndTime = GetWorld()->GetTimeSeconds() + MeleeMatchDurationSeconds;
		RoomGS->CurrentRound = 0; // 刀战只有一整局，不显示回合数
		break;
	case ERoomMatchMode::Zombie:
		// 设置绝对结束时间 = 当前世界时间 + 设定秒数
		RoomGS->MatchEndTime = GetWorld()->GetTimeSeconds() + (10 * 60);
		RoomGS->CurrentRound = ZombieTotalRounds; // 初始化为总回合数，每回合结束后递减
		break;
	default:
		RoomGS->MatchEndTime = 0;
		RoomGS->CurrentRound = 0;
		break;
	}

	RoomGS->OnCurrentRoundUpdated.Broadcast(RoomGS->CurrentRound);

	// 重置双方击杀统计
	RoomGS->ResetTeamKillStats();

	// 启动一个1秒执行一次的循环定时器
	GetWorldTimerManager().SetTimer(MatchTimerHandle, this, &ARoomGameMode::OnMatchTimerTick, 1.0f, true);
}


/**
 * OnMatchTimerTick
 *
 * 核心函数: 每秒触发一次，检查倒计时是否归零
 */
void ARoomGameMode::OnMatchTimerTick()
{
	ARoomGameState* RoomGS = GetGameState<ARoomGameState>();
	if (!RoomGS) return;

	// 检查是否倒计时结束
	if (RoomGS->GetMatchRemainingSeconds() <= 0)
	{
		// 停止计时器
		GetWorldTimerManager().ClearTimer(MatchTimerHandle);

		// 触发超时结算逻辑
		HandleMatchTimeOut();
	}
}


/**
 * HandleMatchTimeOut
 *
 * 核心函数: 处理时间耗尽的宏观逻辑
 * 刀战模式: 直接结束一整局游戏 -> 触发结算
 * 生化模式: 触发 HandleZombieRoundEnd
 */
void ARoomGameMode::HandleMatchTimeOut()
{
	ARoomGameState* RoomGS = GetGameState<ARoomGameState>();
	if (!RoomGS) return;

	// 停止计时器
	GetWorldTimerManager().ClearTimer(MatchTimerHandle);

	UE_LOG(LogTemp, Warning, TEXT("[RoomGameMode] Match Time Out! Mode: %d"), (int32)RoomGS->CurrentMatchMode);

	if (RoomGS->CurrentMatchMode == ERoomMatchMode::Melee)
	{
		// 刀战模式: 直接结束一整局游戏
		UE_LOG(LogTemp, Log, TEXT("刀战模式结束，准备进入全局结算..."));
		BroadcastSystemMessage(TEXT("刀战模式结束！"));
		// 调用结算系统: 判断胜负并广播给所有客户端
		RoomGS->TriggerSettlement();
	}
	else if (RoomGS->CurrentMatchMode == ERoomMatchMode::Zombie)
	{
		// 生化模式: 回合结束处理
		HandleZombieRoundEnd();
	}
}


/**
 * HandleZombieRoundEnd
 *
 * 生化模式回合结束处理
 * - 还有剩余回合: 广播"第 X/Y 回合结束"
 * - 所有回合结束: 触发全局结算
 */
void ARoomGameMode::HandleZombieRoundEnd()
{
	ARoomGameState* RoomGS = GetGameState<ARoomGameState>();
	if (!RoomGS) return;

	RoomGS->CurrentRound--;
	RoomGS->OnCurrentRoundUpdated.Broadcast(RoomGS->CurrentRound);

	if (RoomGS->CurrentRound <= 0)
	{
		// 所有回合结束，整局游戏结束
		UE_LOG(LogTemp, Log, TEXT("生化模式全部 %d 回合结束，准备进入全局结算..."), ZombieTotalRounds);
		BroadcastSystemMessage(FString::Printf(TEXT("生化模式结束！共 %d 回合！"), ZombieTotalRounds));
		// 调用结算系统: 判断最终胜负并广播给所有客户端
		RoomGS->TriggerSettlement();
	}
	else
	{
		// 还有剩余回合，等待 StartNextZombieRound 被调用
		BroadcastSystemMessage(FString::Printf(TEXT("第 %d/%d 回合结束！"), ZombieTotalRounds - RoomGS->CurrentRound, ZombieTotalRounds));
	}
}


/**
 * StartNextZombieRound
 *
 * 生化模式进入下一回合
 * 1. 重置每回合时间（10分钟）
 * 2. 重置双方击杀统计
 * 3. 重启定时器
 * 4. TODO: 重置玩家位置、复活等场景清理工作
 */
void ARoomGameMode::StartNextZombieRound()
{
	ARoomGameState* RoomGS = GetGameState<ARoomGameState>();
	if (!RoomGS || RoomGS->CurrentMatchMode != ERoomMatchMode::Zombie) return;

	if (RoomGS->CurrentRound <= 0)
	{
		// 已经全部结束，不再开始新回合
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("[RoomGameMode] Starting next zombie round. Round=%d"), RoomGS->CurrentRound);
	BroadcastSystemMessage(FString::Printf(TEXT("第 %d/%d 回合开始！"), ZombieTotalRounds - RoomGS->CurrentRound + 1, ZombieTotalRounds));

	// 重置每回合时间
	RoomGS->MatchEndTime = GetWorld()->GetTimeSeconds() + (10 * 60);

	// 重置双方击杀统计
	RoomGS->ResetTeamKillStats();

	// TODO: 重置玩家位置、复活等场景清理工作...

	// 重新启动定时器
	GetWorldTimerManager().SetTimer(MatchTimerHandle, this, &ARoomGameMode::OnMatchTimerTick, 1.0f, true);
}


// ==========================================
// 【P0 架构升级】服务端房主变更流程
// ==========================================

/**
 * TransferHostTo
 *
 * 服务端主动转交房主身份
 * 1. 解析新房主名 (空字符串 → 取第一个在线玩家)
 * 2. 校验新房主不是当前房主自己
 * 3. 修改 GameState->HostPlayerName (ReplicatedUsing 触发客户端 OnRep_HostPlayerName)
 * 4. 服务器本地主动广播 BroadcastHostChanged (因为服务端不走 OnRep)
 * 5. 系统提示"X 成为新房主"
 *
 * 调用时机:
 *  - 房主玩家离开房间后 (RemovePlayerFromRoom 末尾)
 *  - 房主主动转让 (未来 Server_RequestTransferHost)
 *  - 房主被强制踢出 (Server_KickPlayer 命中自己时)
 *
 * @param NewHostPlayerName 新房主名 (为空表示"自动选下一个")
 * @return 是否成功转交
 */
bool ARoomGameMode::TransferHostTo(const FString& NewHostPlayerName)
{
	ARoomGameState* GS = GetGameState<ARoomGameState>();
	if (!GS)
	{
		UE_LOG(LogTemp, Warning, TEXT("[RoomGameMode] TransferHostTo 失败: GameState 无效"));
		return false;
	}

	// ---- 1. 解析目标房主名 ----
	FString TargetHost = NewHostPlayerName;
	if (TargetHost.IsEmpty())
	{
		// 空字符串: 取第一个在线玩家 (按 PlayerController 顺序)
		for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
		{
			if (ARoomPlayerState* PS = It->Get()->GetPlayerState<ARoomPlayerState>())
			{
				TargetHost = PS->GetPlayerName();
				break;
			}
		}
	}

	if (TargetHost.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("[RoomGameMode] TransferHostTo 失败: 房间内已无玩家"));
		return false;
	}

	// ---- 2. 校验: 新房主不能等于当前房主 ----
	if (TargetHost.Equals(GS->HostPlayerName, ESearchCase::IgnoreCase))
	{
		UE_LOG(LogTemp, Log, TEXT("[RoomGameMode] TransferHostTo: 目标 %s 已是当前房主, 跳过"), *TargetHost);
		return true;
	}

	// ---- 3. 修改 GameState->HostPlayerName (触发客户端 OnRep) ----
	const FString OldHost = GS->HostPlayerName;
	GS->HostPlayerName = TargetHost;

	UE_LOG(LogTemp, Log, TEXT("[RoomGameMode] TransferHostTo: 房主变更 %s → %s"), *OldHost, *TargetHost);

	// ---- 4. 服务器本地主动广播 (服务端不走 OnRep, 必须手动广播) ----
	// 【P0 架构修正】走 URoomService::GetCurrentAccountName() 而非直读 PC->MyPlayerName
	bool bLocalPCIsHostNow = false;
	if (URoomService* RoomService = URoomService::Get(this))
	{
		const FString LocalAccountName = RoomService->GetCurrentAccountName();
		bLocalPCIsHostNow = !LocalAccountName.IsEmpty()
			&& LocalAccountName.Equals(TargetHost, ESearchCase::IgnoreCase);
	}
	URoomService::BroadcastHostChanged(this, bLocalPCIsHostNow);

	// ---- 5. 系统提示 ----
	BroadcastSystemMessage(FString::Printf(TEXT("玩家【%s】成为新房主"), *TargetHost));

	return true;
}


// ==========================================
// 8. 数据驱动兜底 (2026-07-03 重构)
// ==========================================

/**
 * ResolveFallbackWeaponRow
 *
 * 从 WeaponDataTable 解析一个有效的武器 RowName (3 级兜底)
 *
 * 设计动机:
 *   - 旧硬编码 TEXT("Knife") 与 DT_WeaponInfo 实际 RowName ("Knife01"/"WQ001") 对不上
 *   - 导致 bSkipLoginDirectToLobby 等测试路径下武器永远生成失败
 *
 * 兜底策略:
 *   1. WeaponDataTable 第一行 (数据驱动)
 *   2. MetalSlugGameDefaults::FallbackWeaponRowName (绝对底线, DT 未配置时)
 *   3. 全部失败时返回空字符串 + 打 Error 日志
 *
 * @return 武器 RowName (正常情况绝不为空)
 */
FString ARoomGameMode::ResolveFallbackWeaponRow() const
{
	// L1: 数据驱动 - 取 DT 第一行
	if (WeaponDataTable)
	{
		const TArray<FName> RowNames = WeaponDataTable->GetRowNames();
		if (RowNames.Num() > 0)
		{
			const FString FirstRow = RowNames[0].ToString();
			UE_LOG(LogTemp, Log,
				TEXT("[RoomGameMode] ResolveFallbackWeaponRow: 数据驱动命中, RowName='%s' (来自 WeaponDataTable 第一行)"),
				*FirstRow);
			return FirstRow;
		}
	}

	// L2: 全局兜底常量
	UE_LOG(LogTemp, Warning,
		TEXT("[RoomGameMode] ResolveFallbackWeaponRow: WeaponDataTable 为空或无行, 退到全局兜底 '%s'. 请检查 BP_RoomGameMode 的 WeaponDataTable 配置!"),
		*MetalSlugGameDefaults::FallbackWeaponRowName);
	return MetalSlugGameDefaults::FallbackWeaponRowName;
}


/**
 * ResolveFallbackCharacterRow
 *
 * 从 CharacterDataTable 解析一个有效的角色 RowName (3 级兜底)
 *
 * @see ResolveFallbackWeaponRow (同款策略)
 * @return 角色 RowName (正常情况绝不为空)
 */
FString ARoomGameMode::ResolveFallbackCharacterRow() const
{
	// L1: 数据驱动 - 取 DT 第一行
	if (CharacterDataTable)
	{
		const TArray<FName> RowNames = CharacterDataTable->GetRowNames();
		if (RowNames.Num() > 0)
		{
			const FString FirstRow = RowNames[0].ToString();
			UE_LOG(LogTemp, Log,
				TEXT("[RoomGameMode] ResolveFallbackCharacterRow: 数据驱动命中, RowName='%s' (来自 CharacterDataTable 第一行)"),
				*FirstRow);
			return FirstRow;
		}
	}

	// L2: 全局兜底常量
	UE_LOG(LogTemp, Warning,
		TEXT("[RoomGameMode] ResolveFallbackCharacterRow: CharacterDataTable 为空或无行, 退到全局兜底 '%s'. 请检查 BP_RoomGameMode 的 CharacterDataTable 配置!"),
		*MetalSlugGameDefaults::FallbackCharacterRowName);
	return MetalSlugGameDefaults::FallbackCharacterRowName;
}
