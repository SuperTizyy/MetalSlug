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

	// 注册计分板数据的网络同步
	DOREPLIFETIME(ARoomPlayerState, RoomScore);
	DOREPLIFETIME(ARoomPlayerState, RoomKills);
	DOREPLIFETIME(ARoomPlayerState, RoomDeaths);
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

		// 通知所有客户端刷新
		OnRep_ScoreboardData();
	}
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
