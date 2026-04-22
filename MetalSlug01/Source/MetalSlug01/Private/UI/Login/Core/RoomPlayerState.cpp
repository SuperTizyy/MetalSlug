#include "UI/Login/Core/RoomPlayerState.h"
// 【极其关键】：必须包含此头文件才能使用 DOREPLIFETIME 宏
#include "Net/UnrealNetwork.h"
#include "Systems/RoomGameState.h"

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
	//使用新的变量名，并初始化双武器
	SelectedWeaponID1 = TEXT("Default");
	SelectedWeaponID2 = TEXT("Default");
}

void ARoomPlayerState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// 【核心规范】：在这里注册变量。DOREPLIFETIME 会让引擎底层接管这些变量的网络同步
	DOREPLIFETIME(ARoomPlayerState, SelectedCharacterID);
	DOREPLIFETIME(ARoomPlayerState, SelectedWeaponID1);
	DOREPLIFETIME(ARoomPlayerState, SelectedWeaponID2); // 注册新变量的网络同步

	// 注册计分板数据的网络同步
	DOREPLIFETIME(ARoomPlayerState, RoomScore);
	DOREPLIFETIME(ARoomPlayerState, RoomKills);
	DOREPLIFETIME(ARoomPlayerState, RoomDeaths);
	DOREPLIFETIME(ARoomPlayerState, RoomAssists);
}

// 只有客户端会在变量改变时自动执行这些 OnRep 函数
void ARoomPlayerState::OnRep_Team()
{
	// 队伍发生变化，通知 UI 刷新
	OnStateChanged.Broadcast();
}

void ARoomPlayerState::OnRep_IsReady()
{
	// 准备状态发生变化，通知 UI 刷新
	OnStateChanged.Broadcast();
}

void ARoomPlayerState::OnRep_ScoreboardData()
{
	// 计分板数据发生变化，通知 UI 刷新
	OnScoreboardDataChanged.Broadcast();
}

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
				UE_LOG(LogTemp, Error, TEXT("[RoomPlayerState] AddKillScore: RoomGameState 为空！"));
			}
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("[RoomPlayerState] AddKillScore: World 为空！"));
		}

		// 通知所有客户端刷新
		OnRep_ScoreboardData();
	}
}

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

// 供服务端 PlayerController 调用的本地 Setter，不再需要是 RPC，因为 RPC 在 Controller 里已经走过了
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