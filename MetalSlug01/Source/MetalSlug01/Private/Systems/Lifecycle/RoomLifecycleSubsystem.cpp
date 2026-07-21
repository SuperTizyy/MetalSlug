// Copyright Epic Games, Inc. All Rights Reserved.

#include "Systems/Lifecycle/RoomLifecycleSubsystem.h"
#include "Systems/RoomGameMode.h"
#include "Systems/RoomGameState.h"
#include "Systems/RoomPlayerController.h"
#include "Enums/CoreEnums.h"
#include "TimerManager.h"
#include "Engine/World.h"
#include "GameFramework/GameStateBase.h"

// ==========================================
// UWorldSubsystem 基础
// ==========================================

URoomLifecycleSubsystem* URoomLifecycleSubsystem::Get(const UObject* WorldContextObject)
{
	if (!WorldContextObject)
	{
		return nullptr;
	}
	if (UWorld* World = WorldContextObject->GetWorld())
	{
		return World->GetSubsystem<URoomLifecycleSubsystem>();
	}
	return nullptr;
}

bool URoomLifecycleSubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	// 大厂原则 - Server-only: 只在服务器端创建 (GameMode 只在 server 跑)
	// 客户端不创建 Lifecycle Subsystem, 比赛状态由 ARoomGameState 复制传递
	if (UWorld* World = Cast<UWorld>(Outer))
	{
		const ENetMode NetMode = World->GetNetMode();
		return NetMode != NM_Client;
	}
	return false;
}

// ==========================================
// 比赛状态机
// ==========================================

void URoomLifecycleSubsystem::PerformGameStart(float InMatchStartDelay, float InMatchDurationSeconds,
	FSimpleDelegate InOnSpawnAllPlayersDelegate)
{
	// 【v56.7 + v56.8 大厂架构修复】先 Spawn Pawn，再显示 HUD（单一调度入口）
	//
	// 根因链:
	//   旧实现 (v22-v56.6):
	//     Server_RequestStartGame → Client_EnterBattleState (立刻切 HUD)
	//                          → GM->PerformGameStart (启动 N 秒倒计时)
	//                          → N 秒后: SpawnAllPlayersIntoBattle (Spawn Pawn)
	//     结果: 玩家在 HUD 显示后等 N 秒才看到自己的 Pawn → "飞翔视角"
	//
	//   v56.7 部分修复: 把 Client_TransitToMatchState + OnBattleStarted 移到 Spawn 回调
	//     但还有一个独立的 Client_EnterBattleState 路径仍然立刻切 HUD → 修不完整
	//
	//   v56.8 完整修复:
	//     - 移除 Server_RequestStartGame 中的 Client_EnterBattleState 调用
	//     - HUD 切换、OnBattleStarted 广播、Spawn 全部归一到一个回调
	//     - 玩家时序: 房间UI → 倒计时 → Pawn Spawn + HUD 同时显示
	//     - 大厂原则: 单一调度入口, 同步时序, 零"提前触发"

	// 1. 更新房间状态
	if (ARoomGameMode* GM = Cast<ARoomGameMode>(GetWorld()->GetAuthGameMode()))
	{
		GM->CurrentRoomState = ERoomState::BattleInProgress;
	}

	// 2. 启动比赛计时器 (同步 MatchEndTime 到所有客户端)
	MatchStartDelay = InMatchStartDelay;
	MatchDurationSeconds = InMatchDurationSeconds;
	OnSpawnAllPlayersCallback = InOnSpawnAllPlayersDelegate;

	StartMatchTimer();

	// 3. 【v56.7】先不显示 HUD，等 Spawn 完成后再在回调里显示
	//    这样玩家永远不会看到"无 Pawn 的 HUD"

	// 4. 延迟 Spawn Pawn (匹配时间由 BP 配 MatchStartDelay, 默认 3s)
	// 【v48】上限保护: 钳到 [0, 5]s, BP 配错不会卡死
	constexpr float kMaxAllowedStartDelay = 5.0f;
	constexpr float kDefaultStartDelay = 3.0f;
	if (MatchStartDelay > kMaxAllowedStartDelay)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[RoomLifecycle] MatchStartDelay=%.1fs 超过上限 %.1fs (BP 配置错误), 自动改为 %.1fs"),
			MatchStartDelay, kMaxAllowedStartDelay, kDefaultStartDelay);
		MatchStartDelay = kDefaultStartDelay;
	}
	else if (MatchStartDelay < 0.0f)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[RoomLifecycle] MatchStartDelay=%.1fs 为负值, 自动改为 0s (立即 Spawn)"),
			MatchStartDelay);
		MatchStartDelay = 0.0f;
	}
	UE_LOG(LogTemp, Log, TEXT("[RoomLifecycle] 游戏将在 %.1f 秒后开始..."), MatchStartDelay);

	// 5. 【v56.7 + v56.8】唯一回调: 先 Spawn Pawn，再显示 HUD, 最后广播 OnBattleStarted
	GetWorld()->GetTimerManager().SetTimer(
		MatchStartTimerHandle,
		FTimerDelegate::CreateLambda([this]()
		{
			UE_LOG(LogTemp, Log, TEXT("[RoomLifecycle] 倒计时结束，开始 Spawn Pawn..."));

			// 5a. 触发 Pawn Spawn (玩家 + AI)
			if (OnSpawnAllPlayersCallback.IsBound())
			{
				OnSpawnAllPlayersCallback.Execute();
			}
			else
			{
				UE_LOG(LogTemp, Error,
					TEXT("[RoomLifecycle] 倒计时结束但 OnSpawnAllPlayersCallback 未绑定."
						 " GameMode 必须调用 PerformGameStart 时传入该委托."));
			}

			// 5b. 推送 HUD 切换到客户端 (【v56.8】这是 HUD 切换的唯一入口)
			for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
			{
				if (ARoomPlayerController* PC = Cast<ARoomPlayerController>(It->Get()))
				{
					PC->Client_TransitToMatchState(EMatchState::Battleing);
				}
			}

			// 5c. 广播 OnBattleStarted (AI BT 激活)
			if (!bBattleStartedBroadcasted)
			{
				bBattleStartedBroadcasted = true;
				if (ARoomGameMode* GM = Cast<ARoomGameMode>(GetWorld()->GetAuthGameMode()))
				{
					GM->OnBattleStarted.Broadcast();
				}
				UE_LOG(LogTemp, Log, TEXT("[RoomLifecycle] PerformGameStart: Spawn + HUD 完成, OnBattleStarted 已广播"));
			}
		}),
		MatchStartDelay,
		false);
}

void URoomLifecycleSubsystem::StartMatchTimer()
{
	ARoomGameState* RoomGS = GetWorld()->GetGameState<ARoomGameState>();
	if (!RoomGS)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[RoomLifecycle] StartMatchTimer: ARoomGameState 为空, 无法启动计时"));
		return;
	}

	if (RoomGS->CurrentMatchMode == ERoomMatchMode::Melee)
	{
		// 刀战模式: 设定绝对结束时间
		RoomGS->MatchEndTime = GetWorld()->GetTimeSeconds() + MatchDurationSeconds;
	}
	else if (RoomGS->CurrentMatchMode == ERoomMatchMode::Zombie)
	{
		// 生化模式: 初始化回合数
		RoomGS->CurrentRound = ZombieTotalRounds;
	}

	GetWorld()->GetTimerManager().SetTimer(MatchTimerHandle, this, &URoomLifecycleSubsystem::OnMatchTimerTick, 1.0f, true);
}

void URoomLifecycleSubsystem::OnMatchTimerTick()
{
	ARoomGameState* RoomGS = GetWorld()->GetGameState<ARoomGameState>();
	if (!RoomGS)
	{
		GetWorld()->GetTimerManager().ClearTimer(MatchTimerHandle);
		return;
	}

	if (RoomGS->CurrentMatchMode == ERoomMatchMode::Melee)
	{
		if (GetWorld()->GetTimeSeconds() >= RoomGS->MatchEndTime)
		{
			GetWorld()->GetTimerManager().ClearTimer(MatchTimerHandle);
			HandleMatchTimeOut();
		}
	}
}

void URoomLifecycleSubsystem::HandleMatchTimeOut()
{
	ARoomGameState* RoomGS = GetWorld()->GetGameState<ARoomGameState>();
	if (!RoomGS)
	{
		return;
	}

	GetWorld()->GetTimerManager().ClearTimer(MatchTimerHandle);

	if (RoomGS->CurrentMatchMode == ERoomMatchMode::Melee)
	{
		// 刀战模式: 全局结算 (由 GameMode 决定, Subsystem 只清理计时器)
		if (ARoomGameMode* GM = Cast<ARoomGameMode>(GetWorld()->GetAuthGameMode()))
		{
			// 委托回 GameMode 做具体业务 (例如广播胜利方)
			// 这里只清理资源
		}
	}
	else if (RoomGS->CurrentMatchMode == ERoomMatchMode::Zombie)
	{
		HandleZombieRoundEnd();
	}
}

bool URoomLifecycleSubsystem::HandleZombieRoundEnd()
{
	ARoomGameState* RoomGS = GetWorld()->GetGameState<ARoomGameState>();
	if (!RoomGS)
	{
		return false;
	}

	if (RoomGS->CurrentRound <= 1)
	{
		UE_LOG(LogTemp, Log, TEXT("生化模式全部 %d 回合结束, 准备进入全局结算..."), ZombieTotalRounds);
		// 全局结算由 GameMode 决定, 这里只清理 Round 状态
		return false; // 没有下一回合
	}

	UE_LOG(LogTemp, Log, TEXT("第 %d/%d 回合结束"), ZombieTotalRounds - RoomGS->CurrentRound, ZombieTotalRounds);
	return true; // 还有下一回合, 调用方应调 StartNextZombieRound
}

bool URoomLifecycleSubsystem::StartNextZombieRound()
{
	ARoomGameState* RoomGS = GetWorld()->GetGameState<ARoomGameState>();
	if (!RoomGS || RoomGS->CurrentRound <= 1)
	{
		return false;
	}

	RoomGS->CurrentRound--;
	UE_LOG(LogTemp, Log, TEXT("第 %d/%d 回合开始"), ZombieTotalRounds - RoomGS->CurrentRound + 1, ZombieTotalRounds);

	// 重新启动比赛计时器
	GetWorld()->GetTimerManager().SetTimer(MatchTimerHandle, this, &URoomLifecycleSubsystem::OnMatchTimerTick, 1.0f, true);
	return true;
}