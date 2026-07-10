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
	// 【v56.7 大厂架构修复】先 Spawn Pawn，再显示 HUD
	//
	// 根因: 旧实现先调 Client_TransitToMatchState (HUD 显示), 再等 3 秒 Spawn Pawn
	//        → 玩家看到"飞翔视角" 3 秒
	//
	// 修复: 把 HUD 显示移到 Spawn 完成之后
	//        时序: 倒计时结束 → Spawn Pawn → 显示 HUD
	//        这样玩家永远不会看到"无 Pawn 的 HUD"

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

	// 3. 【v56.7 修复】先不显示 HUD，等 Spawn 完成后在回调里显示
	//    客户端看到的是: 黑屏/房间UI → 3秒后 → Pawn Spawn → HUD 显示
	//    这样玩家永远不会看到"无 Pawn 的 HUD"

	// 4. 延迟生成角色
	// 【v48 大厂架构修复】MatchStartDelay 上限保护 (默认 3s, BP 配错不会卡死用户)
	//   根因: BP_GM_RoomGameMode.MatchStartDelay 之前被用户配成 60s → 玩家点开始后等 60s 才 Spawn
	//   → 用户 8-10s 就关闭 PIE → 永远看不到 AI Spawn
	//   修复: 这里钳到 [0, 5]s 区间, 超过 5s 视为配错, 自动用 3s
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

	// 【v56.7 修复】Spawn 回调里会显示 HUD
	GetWorld()->GetTimerManager().SetTimer(
		MatchStartTimerHandle,
		FTimerDelegate::CreateLambda([this]()
		{
			// 5. 【v56.7 修复】先 Spawn Pawn，再显示 HUD
			UE_LOG(LogTemp, Log, TEXT("[RoomLifecycle] 倒计时结束，开始 Spawn Pawn..."));

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

			// 6. 【v56.7 修复】Spawn 完成后，显示 HUD
			for (FConstPlayerControllerIterator It = GetWorld()->GetPlayerControllerIterator(); It; ++It)
			{
				if (ARoomPlayerController* PC = Cast<ARoomPlayerController>(It->Get()))
				{
					PC->Client_TransitToMatchState(EMatchState::Battleing);
				}
			}

			// 7. 幂等广播 OnBattleStarted (AI 订阅后激活 BT)
			if (!bBattleStartedBroadcasted)
			{
				bBattleStartedBroadcasted = true;
				if (ARoomGameMode* GM = Cast<ARoomGameMode>(GetWorld()->GetAuthGameMode()))
				{
					GM->OnBattleStarted.Broadcast();
				}
				UE_LOG(LogTemp, Log, TEXT("[RoomLifecycle] PerformGameStart: Spawn 完成，OnBattleStarted 已广播"));
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