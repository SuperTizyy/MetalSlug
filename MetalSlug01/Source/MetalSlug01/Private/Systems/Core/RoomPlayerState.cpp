// 版权声明：在项目设置的描述页面填写您的版权信息。

// ==========================================
// 头文件包含区
// ==========================================
// 引入本类头文件
#include "Systems/Core/RoomPlayerState.h"
// 【极其关键】: 必须包含此头文件才能使用 DOREPLIFETIME 宏
#include "Net/UnrealNetwork.h"

// 引入房间 GameState（用于 AddTeamKill 队伍击杀统计）
#include "Systems/RoomGameState.h"
#include "Data/Faction/FactionTags.h" // 【2026.07.10 P0 重构】阵营集中定义
#include "Data/Enums/CombatEnums.h"   // 【v100 新增】EKillStreakType (连杀真理源派生)
#include "Data/Config/PlayerConfigAsset.h" // 【v100 新增】UPlayerConfigAsset (连杀超时秒数)
#include "Systems/RoomGameMode.h"     // 【v100 新增】ARoomGameMode (拿 PlayerConfigAsset)


// ==========================================
// 1. 构造函数
// ==========================================

/**
 * ARoomPlayerState 构造函数
 *
 * 目的: 初始化默认值、开启网络同步
 * 1. 阵营 = Faction.Defense（默认守方，由 GameMode::PostLogin 覆盖）, 准备状态 = false
 * 2. 计分板数据归零
 * 3. PlayerState 开启网络同步
 * 4. 战备选择初始化为 "Default"
 */
ARoomPlayerState::ARoomPlayerState()
{
	// 【2026.07.10 P0 重构】阵营默认 Faction.Defense, GameMode 启动时根据 ModeRulesByMode 覆盖
	CurrentFactionTag = FFactionTags::Defense();
	bIsReady = false;

	// 初始化计分板数据
	RoomScore = 0;
	RoomKills = 0;
	RoomDeaths = 0;
	RoomAssists = 0;

	// 确保 PlayerState 开启网络同步
	bReplicates = true;

	SelectedCharacterID = TEXT("Default");
	// 使用新的变量名，并初始化双武器
	SelectedWeaponID1 = TEXT("Default");
	SelectedWeaponID2 = TEXT("Default");
}


// ==========================================
// 2. 网络同步注册
// ==========================================

/**
 * GetLifetimeReplicatedProps
 *
 * 重写以注册需要网络同步的变量
 * 【核心规范】: 在这里注册变量，DOREPLIFETIME 会让引擎底层接管这些变量的网络同步
 */
void ARoomPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// ==========================================
	// 【2026.07.10 P0 重构】阵营与准备状态同步 — 用 FGameplayTag 替代 ERoomTeam
	// 根因: 旧版 CurrentTeam 是 ERoomTeam 枚举, 现统一用 FGameplayTag 表达阵营
	// ==========================================
	DOREPLIFETIME(ARoomPlayerState, CurrentFactionTag);
	DOREPLIFETIME(ARoomPlayerState, bIsReady);
	// 【2026.07.11 v29.6】玩家主动选阵营标志 (阻止 auto-balance 反复覆盖)
	DOREPLIFETIME(ARoomPlayerState, bHasExplicitlyChosenTeam);

	// 【核心规范】: 在这里注册变量。DOREPLIFETIME 会让引擎底层接管这些变量的网络同步
	DOREPLIFETIME(ARoomPlayerState, SelectedCharacterID);
	DOREPLIFETIME(ARoomPlayerState, SelectedWeaponID1);
	DOREPLIFETIME(ARoomPlayerState, SelectedWeaponID2); // 注册新变量的网络同步
	// 【v52 P0】第 3 把武器同步 (近战) — 必须 DOREPLIFETIME, 否则客户端永远拿不到
	DOREPLIFETIME(ARoomPlayerState, SelectedWeaponID3);
	// 【2026.07.26 v99 P0】母体状态同步 — 复活链真理源 (RequestRespawn 读它决定是否原地变母体)
	DOREPLIFETIME(ARoomPlayerState, bIsMother);
	// 【v99.1 大厂架构】母体复活位置真理源 — 死亡 Transform + 标志位
	DOREPLIFETIME(ARoomPlayerState, LastDeathTransform);
	DOREPLIFETIME(ARoomPlayerState, bHasLastDeathTransform);

	// 注册计分板数据的网络同步
	DOREPLIFETIME(ARoomPlayerState, RoomScore);
	DOREPLIFETIME(ARoomPlayerState, RoomKills);
	DOREPLIFETIME(ARoomPlayerState, RoomDeaths);
	// 【v100 大厂架构】连杀计数真理源 — Replicate 让所有客户端能本地播对应音
	DOREPLIFETIME(ARoomPlayerState, CurrentKillStreak);
	DOREPLIFETIME(ARoomPlayerState, RoomAssists);
}


// ==========================================
// 【2026.07.11 v29.6】玩家主动切队标记
//
// 唯一调用方: ARoomGameMode::ChangePlayerTeam 改阵营成功后
// 效果: bHasExplicitlyChosenTeam = true, auto-balance 永远不再覆盖玩家的选择
//
// 大厂原则:
//   - 玩家意图 (PC.RequestChangeTeam) 是阵营真理 — 一旦确认, 不可被 RoomGameMode 的
//     auto-balance 重新覆盖
//   - 服务器修改 → 自动 Replicate 到客户端 → 客户端 OnStateChanged 自动 refresh
// ==========================================
void ARoomPlayerState::Server_MarkTeamExplicitlyChosen()
{
	// 仅服务器可调用
	if (!HasAuthority())
	{
		UE_LOG(LogTemp, Error,
			TEXT("[RoomPlayerState] Server_MarkTeamExplicitlyChosen: 客户端调用, 拒绝 (HasAuthority=false)"));
		return;
	}

	// 幂等: 已标记过则不重复
	if (bHasExplicitlyChosenTeam)
	{
		return;
	}

	bHasExplicitlyChosenTeam = true;
	UE_LOG(LogTemp, Log,
		TEXT("[RoomPlayerState] Server_MarkTeamExplicitlyChosen: 玩家已显式选阵营 (CurrentFactionTag=%s), "
		     "auto-balance 将永远不再覆盖此玩家的阵营"),
		*CurrentFactionTag.ToString());
}


// ==========================================
// 3. 客户端 Rep Notifies
// ==========================================

/**
 * OnRep_FactionTag
 *
 * 【2026.07.10 重构】阵营 Tag 变化时的客户端回调, 替代原 OnRep_Team
 * 通知 UI 刷新 — OnStateChanged 委托不变, UI 层无需改
 */
void ARoomPlayerState::OnRep_FactionTag()
{
	OnStateChanged.Broadcast();
}


/**
 * OnRep_IsReady
 *
 * 准备状态发生变化，通知 UI 刷新
 */
void ARoomPlayerState::OnRep_IsReady()
{
	// 准备状态发生变化，通知 UI 刷新
	OnStateChanged.Broadcast();
}


/**
 * OnRep_ScoreboardData
 *
 * 计分板数据发生变化，通知 UI 刷新
 */
void ARoomPlayerState::OnRep_ScoreboardData()
{
	// 计分板数据发生变化，通知 UI 刷新
	OnScoreboardDataChanged.Broadcast();
}


// ==========================================
// 4. 计分板操作（仅服务器）
// ==========================================

/**
 * AddKillScore
 *
 * 服务器专用: 增加得分（+1 击杀 +20 分）
 * 同步更新 GameState 中的队伍击杀统计
 */
void ARoomPlayerState::AddKillScore()
{
	// 只有服务器有权限修改计分板数据
	if (HasAuthority())
	{
		RoomKills += 1;
		RoomScore += KillScoreValue;

		// 更新 GameState 中的队伍击杀统计
		if (UWorld* World = GetWorld())
		{
			if (ARoomGameState* RoomGS = World->GetGameState<ARoomGameState>())
			{
				UE_LOG(LogTemp, Log,
					TEXT("[RoomPlayerState] AddKillScore: Player=%s, CurrentFactionTag=%s, Before: AttackerKills=%d, DefenderKills=%d"),
					*GetPlayerName(), *CurrentFactionTag.ToString(),
					RoomGS->AttackerTotalKills, RoomGS->DefenderTotalKills);
				// 【2026.07.10 P0 重构】传递 FGameplayTag 给 GameState, 替代 ERoomTeam
				RoomGS->AddTeamKill(CurrentFactionTag);
				UE_LOG(LogTemp, Log, TEXT("[RoomPlayerState] AddKillScore: After: AttackerKills=%d, DefenderKills=%d"),
					RoomGS->AttackerTotalKills, RoomGS->DefenderTotalKills);
			}
			else
			{
				UE_LOG(LogTemp, Error, TEXT("[RoomPlayerState] AddKillScore: RoomGameState 为空!"));
			}
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("[RoomPlayerState] AddKillScore: World 为空!"));
		}

		// 通知所有客户端刷新
		OnRep_ScoreboardData();
	}
}


/**
 * AddAssistScore
 *
 * 服务器专用: 增加助攻得分（+1 助攻 +10 分）
 */
void ARoomPlayerState::AddAssistScore()
{
	// 只有服务器有权限修改计分板数据
	if (HasAuthority())
	{
		RoomAssists += 1;
		RoomScore += AssistScoreValue;

		// 通知所有客户端刷新
		OnRep_ScoreboardData();
	}
}


/**
 * AddDeath
 *
 * 服务器专用: 增加死亡次数
 */
void ARoomPlayerState::AddDeath()
{
	// 只有服务器有权限修改计分板数据
	if (HasAuthority())
	{
		RoomDeaths += 1;

		// 通知所有客户端刷新
		OnRep_ScoreboardData();
	}
}


/**
 * ResetScoreboardStats
 *
 * 服务器专用: 重置计分板数据（每回合开始时调用）
 */
void ARoomPlayerState::ResetScoreboardStats()
{
	// 只有服务器有权限重置计分板数据
	if (HasAuthority())
	{
		RoomScore = 0;
		RoomKills = 0;
		RoomDeaths = 0;
		RoomAssists = 0;

		// 【v100 大厂架构】连杀计数同步清零(避免回合切换后继承上一局的连杀)
		CurrentKillStreak = 0;
		LastKillWorldTimeSeconds = -1.f;

		// 通知所有客户端刷新
		OnRep_ScoreboardData();
	}
}


// ==========================================
// 4b. 【v100 大厂架构 — 连杀真理源】服务器累加/重置
// ==========================================

/**
 * 【v100 大厂架构 — 连杀算法入口】服务器专用: 玩家击杀时累加/重置连杀
 *
 * 业务流程:
 *   1. UCombatDeathComponent::PerformKillSettlement 中
 *   2. KillerPS->AddKillScore() 之前 调 KillerPS->ServerUpdateKillStreak(bIsAssist, bIsHeadshot)
 *   3. 内部按 LastKillWorldTimeSeconds + KillStreakDuration 决定:
 *      - 超时或首次击杀 → CurrentKillStreak = 0 后 +1
 *      - 未超时 → CurrentKillStreak += 1
 *   4. 计算 EKillStreakType 返回 (服务器传给 Multicast_NotifyKill RPC → 客户端播音)
 *
 * 大厂原则 — 与 KillStreakWidget 镜像:
 *   - bIsHeadshot=true 优先 → 返回 Headshot
 *   - 否则按 CurrentKillStreak 封顶分级(>= 5 → FiveKills,无 FivePlusKills)
 *   - bIsAssist=true → 助攻,不计入连杀,重置为 0 并返回 None
 *     (与 widget RecordKill 业务保持一致 — RecordKill 客户端不为助攻计数)
 *
 * @param bIsAssist    是否助攻(为 true → 重置连杀, 返回 None)
 * @param bIsHeadshot  是否爆头
 * @return             计算后的连杀类型 — 服务器传给 RPC 供客户端播音
 *
 * 零兜底:
 *   - 找不到 GameMode 拿不到 KillStreakDuration → Log Error + 用默认 10s (零兜底不强求,但要报警)
 *   - bIsAssist=true → 返回 None (语义:助攻不计入连杀,客户端收到 None 应拒绝播放连杀音)
 */
EKillStreakType ARoomPlayerState::ServerUpdateKillStreak(bool bIsAssist, bool bIsHeadshot)
{
	// 零兜底守卫: 任何字段写只能在服务器跑
	if (!HasAuthority())
	{
		UE_LOG(LogTemp, Error,
			TEXT("[RoomPlayerState][v100] ServerUpdateKillStreak: 必须在服务器调用! "
				 "Player=%s. 返回 None 让调用方感知错误."),
			*GetPlayerName());
		return EKillStreakType::None;
	}

	// 助攻不计入连杀 + 不播连杀音(惯例: 助攻只显示图标,不刷"二连杀")
	if (bIsAssist)
	{
		CurrentKillStreak = 0; // 重置
		UE_LOG(LogTemp, Log,
			TEXT("[RoomPlayerState][v100] ServerUpdateKillStreak: bIsAssist=true, 重置连杀. Player=%s"),
			*GetPlayerName());
		return EKillStreakType::None;
	}

	// 获取超时阈值 — 优先级: GameMode->PlayerConfigAsset.KillStreakDuration > 10s 默认(零兜底)
	float KillStreakDuration = 10.0f;
	if (UWorld* World = GetWorld())
	{
		if (ARoomGameMode* RoomGM = World->GetAuthGameMode<ARoomGameMode>())
		{
			if (RoomGM->PlayerConfigAsset.IsValid())
			{
				if (UPlayerConfigAsset* Config = RoomGM->PlayerConfigAsset.LoadSynchronous())
				{
					KillStreakDuration = Config->KillStreakDuration;
				}
				else
				{
					UE_LOG(LogTemp, Error,
						TEXT("[RoomPlayerState][v100] ServerUpdateKillStreak: PlayerConfigAsset 加载失败, 使用默认 10s. "
							 "Player=%s. 【修复】检查 DA_PlayerConfig 资产是否被删除."),
						*GetPlayerName());
				}
			}
			else
			{
				UE_LOG(LogTemp, Error,
					TEXT("[RoomPlayerState][v100] ServerUpdateKillStreak: PlayerConfigAsset 未配, 使用默认 10s. "
						 "Player=%s. 【修复】在 GM_RoomGameMode Class Defaults → Room|Config → PlayerConfigAsset 配 DA_PlayerConfig."),
					*GetPlayerName());
			}
		}
	}

	// 累加/重置判定
	const float Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.f;
	if (LastKillWorldTimeSeconds < 0.f || (Now - LastKillWorldTimeSeconds) > KillStreakDuration)
	{
		// 首次击杀 / 超时后第一次击杀 → 重置为 0(然后立即 +1)
		CurrentKillStreak = 0;
	}
	LastKillWorldTimeSeconds = Now;
	CurrentKillStreak += 1;

	// 计算 EKillStreakType(镜像 UKillStreakWidget::GetKillStreakType 逻辑)
	// 【业务规则 2026.07.26】"超过五杀再激活还是五杀图标和声音"
	//   - 不新增枚举值(无 FivePlusKills,大厂原则 - 不为业务不需要的概念建抽象)
	//   - 封顶语义: Kills >= 5 都归 FiveKills (图标 + 音效都按五杀播)
	//   - 这样玩家连杀 6/7/8 杀时, 每次都重新触发一次 FiveKills 图标 + 音效
	EKillStreakType ResultStreakType = EKillStreakType::None;
	if (bIsHeadshot)
	{
		ResultStreakType = EKillStreakType::Headshot; // 爆头优先
	}
	else if (CurrentKillStreak >= 5)
	{
		ResultStreakType = EKillStreakType::FiveKills; // 封顶: 5+ 杀都是五杀
	}
	else if (CurrentKillStreak >= 4)
	{
		ResultStreakType = EKillStreakType::FourKills;
	}
	else if (CurrentKillStreak >= 3)
	{
		ResultStreakType = EKillStreakType::ThreeKills;
	}
	else if (CurrentKillStreak >= 2)
	{
		ResultStreakType = EKillStreakType::TwoKills;
	}
	else if (CurrentKillStreak >= 1)
	{
		ResultStreakType = EKillStreakType::OneKill;
	}

	UE_LOG(LogTemp, Display,
		TEXT("[RoomPlayerState][v100] ServerUpdateKillStreak: Player=%s, StreakCount=%d, Result=%d, bIsHeadshot=%d"),
		*GetPlayerName(), CurrentKillStreak, static_cast<int32>(ResultStreakType), bIsHeadshot ? 1 : 0);

	return ResultStreakType;
}

/**
 * 【v100 大厂架构 — 连杀超时清理】服务器专用: 重置连杀(死亡时调)
 *
 * 业务流程:
 *   - UCombatDeathComponent::PerformKillSettlement 中
 *   - VictimPS->AddDeath() 之后 立即调 VictimPS->ServerResetKillStreak()
 *
 * 不复制 OnRep_KillStreak (客户端 widget 自己监听死亡流)
 */
void ARoomPlayerState::ServerResetKillStreak()
{
	if (!HasAuthority())
	{
		UE_LOG(LogTemp, Error,
			TEXT("[RoomPlayerState][v100] ServerResetKillStreak: 必须在服务器调用! Player=%s"),
			*GetPlayerName());
		return;
	}

	if (CurrentKillStreak > 0)
	{
		UE_LOG(LogTemp, Display,
			TEXT("[RoomPlayerState][v100] ServerResetKillStreak: Player=%s, PreviousStreak=%d, 重置为 0."),
			*GetPlayerName(), CurrentKillStreak);
		CurrentKillStreak = 0;
		LastKillWorldTimeSeconds = -1.f;
		// ReplicatedUsing 触发 OnRep_KillStreak → 客户端 HUD 收到新值
	}
}


/**
 * 【v100 OnRep_KillStreak — 客户端接收连杀数据】
 *
 * 当前 v100 阶段:
 *   - KillStreakWidget 仍是本地 widget 内部字段(单数据源分裂,后续 PR 跟进)
 *   - 此处仅作为日志镜像 — 验证 RPC 链路 + 数据流正确
 *   - TODO(后续 PR): KillStreakWidget 改读 ARoomPlayerState::CurrentKillStreak
 */
void ARoomPlayerState::OnRep_KillStreak()
{
	UE_LOG(LogTemp, Log,
		TEXT("[RoomPlayerState][v100] OnRep_KillStreak: Player=%s, CurrentKillStreak=%d"),
		*GetPlayerName(), CurrentKillStreak);
}


// ==========================================
// 5. 战备选择 Setter
// ==========================================

/**
 * SetPlayerLoadout
 *
 * 供服务端 PlayerController 调用的本地 Setter
 * 不再需要是 RPC，因为 RPC 在 Controller 里已经走过了
 * @param InCharID 角色 ID
 * @param InPrimaryID   主武器 ID (Slot 1)
 * @param InSecondaryID 副武器 ID (Slot 2)
 * @param InMeleeID     近战武器 ID (Slot 3)
 */
void ARoomPlayerState::SetPlayerLoadout(const FString& InCharID, const FString& InPrimaryID, const FString& InSecondaryID, const FString& InMeleeID)
{
	// 只有服务器有权限修改带有 Replicated 的变量
	if (HasAuthority())
	{
		SelectedCharacterID = InCharID;
		SelectedWeaponID1 = InPrimaryID;
		SelectedWeaponID2 = InSecondaryID;
		SelectedWeaponID3 = InMeleeID;
	}
}
