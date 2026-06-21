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


// ==========================================
// 1. 构造函数
// ==========================================

/**
 * ARoomPlayerState 构造函数
 *
 * 目的: 初始化默认值、开启网络同步
 * 1. 队伍 = None，准备状态 = false
 * 2. 计分板数据归零
 * 3. PlayerState 开启网络同步
 * 4. 战备选择初始化为 "Default"
 */
ARoomPlayerState::ARoomPlayerState()
{
	// 默认初始化
	CurrentTeam = ERoomTeam::None;
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
	// 【2026-06-29 P0 修复】注册阵营与准备状态的复制
	// 根因: CurrentTeam 和 bIsReady 标了 ReplicatedUsing = OnRep_Team/OnRep_IsReady,
	//       但 DOREPLIFETIME 漏注册 → 客户端永远拿不到这两个字段的值 → 
	//       GetPlayersInTeam(Attack/Defense) 找不到任何玩家 → Box_AttackTeam/Box_DefenseTeam 始终为空
	//       OnRep_Team/OnRep_IsReady 永远不触发 → UI 不刷新
	// ==========================================
	DOREPLIFETIME(ARoomPlayerState, CurrentTeam);
	DOREPLIFETIME(ARoomPlayerState, bIsReady);

	// 【核心规范】: 在这里注册变量。DOREPLIFETIME 会让引擎底层接管这些变量的网络同步
	DOREPLIFETIME(ARoomPlayerState, SelectedCharacterID);
	DOREPLIFETIME(ARoomPlayerState, SelectedWeaponID1);
	DOREPLIFETIME(ARoomPlayerState, SelectedWeaponID2); // 注册新变量的网络同步

	// 注册计分板数据的网络同步
	DOREPLIFETIME(ARoomPlayerState, RoomScore);
	DOREPLIFETIME(ARoomPlayerState, RoomKills);
	DOREPLIFETIME(ARoomPlayerState, RoomDeaths);
	DOREPLIFETIME(ARoomPlayerState, RoomAssists);
}


// ==========================================
// 3. 客户端 Rep Notifies
// ==========================================

/**
 * OnRep_Team
 *
 * 只有客户端会在变量改变时自动执行这些 OnRep 函数
 * 队伍发生变化，通知 UI 刷新
 */
void ARoomPlayerState::OnRep_Team()
{
	// 队伍发生变化，通知 UI 刷新
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
				UE_LOG(LogTemp, Log, TEXT("[RoomPlayerState] AddKillScore: Player=%s, CurrentTeam=%d, Before: AttackerKills=%d, DefenderKills=%d"),
					*GetPlayerName(), (int32)CurrentTeam, RoomGS->AttackerTotalKills, RoomGS->DefenderTotalKills);
				RoomGS->AddTeamKill(CurrentTeam);
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
 * @param InWeapon1ID 1 号位武器 ID
 * @param InWeapon2ID 2 号位武器 ID
 */
void ARoomPlayerState::SetPlayerLoadout(const FString& InCharID, const FString& InWeapon1ID, const FString& InWeapon2ID)
{
	// 只有服务器有权限修改带有 Replicated 的变量
	if (HasAuthority())
	{
		SelectedCharacterID = InCharID;
		SelectedWeaponID1 = InWeapon1ID;
		SelectedWeaponID2 = InWeapon2ID;
	}
}
