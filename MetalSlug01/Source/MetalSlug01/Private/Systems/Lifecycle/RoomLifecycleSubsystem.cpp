// Copyright Epic Games, Inc. All Rights Reserved.

#include "Systems/Lifecycle/RoomLifecycleSubsystem.h"
#include "Systems/RoomGameMode.h"
#include "Systems/RoomGameState.h"
#include "Systems/RoomPlayerController.h"
#include "Systems/Mother/RoomMotherMutationSubsystem.h"
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

	// 【v92 大厂架构】初始化生化模式回合数 (内部计数用, UI 不订阅)
	//   - 大厂原则 — 职责分离:
	//     CurrentRound: 仅作 Subsystem 内部计数器 (StartNextZombieRound: CurrentRound--)
	//     TotalRounds: UI 显示用 (已由 GameMode.InjectSubsystemConfigs 注入到 GameState)
	//   - 初始化位置: 这里 (PerformGameStart 入口) 而非 StartMatchTimer
	//   - 原因: StartMatchTimer 会被每回合 StartNextZombieRound 重复调用, 不应重复初始化
	if (ARoomGameState* RoomGS = GetWorld()->GetGameState<ARoomGameState>())
	{
		if (RoomGS->CurrentMatchMode == ERoomMatchMode::Zombie && RoomGS->TotalRounds >= 1)
		{
			RoomGS->CurrentRound = RoomGS->TotalRounds;
			UE_LOG(LogTemp, Log, TEXT("[RoomLifecycle] PerformGameStart: 生化模式回合数初始化 CurrentRound=%d"),
				RoomGS->CurrentRound);
		}
	}

	StartMatchTimer();

	// 3. 【v56.7】先不显示 HUD，等 Spawn 完成后再在回调里显示
	//    这样玩家永远不会看到"无 Pawn 的 HUD"

	// 4. 延迟 Spawn Pawn (匹配时间由 BP 配 MatchStartDelay, 默认 3s)
	//
	// 【v92 大厂架构】删除旧的 Clamp 兜底 (MatchStartDelay > 5 自动改 3 / 负数改 0):
	//   - 旧版是"静默篡改 BP 配置"的反模式, 让配置错不可见
	//   - 新版按用户决策 s3_no_clamp_trust: 完全不检查, 信任 GameMode 配置
	//   - 若 BP 配错 (如 999s 卡死), 用户会立即看到卡死 → 主动修复 BP
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

		// 5d. 【v92 大厂架构】生化模式首局启动母体变异倒计时
		//   - 集中调度入口: 仅在 PerformGameStart 末尾启动一次
		//   - 后续回合由 StartNextZombieRound 末尾启动
		//   - 业务规则: 每局开始人类重置, 互相无敌, 倒计时结束变异
		//   - 大厂原则 — 单一职责: StartMotherMutationCountdown 内部已检查模式 + Duration, 不重复判断
		StartMotherMutationCountdown();
	}),
		MatchStartDelay,
		false);
}

/**
 * SetTotalRounds — 转发到 GameState.SetTotalRounds (单一真理源)
 *
 * 大厂原则 — 转发壳模式:
 *   - Subsystem 内部不再持有 TotalRounds 副本 (消除重复架构)
 *   - GameMode → GameState → UI 一条路径, 不绕道 Subsystem
 *   - Subsystem 内部需要时直接读 GameState.TotalRounds
 */
void URoomLifecycleSubsystem::SetTotalRounds(int32 InRounds)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[RoomLifecycle] SetTotalRounds: World 为空, 拒绝注入."));
		return;
	}

	ARoomGameState* RoomGS = World->GetGameState<ARoomGameState>();
	if (!RoomGS)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[RoomLifecycle] SetTotalRounds: GameState 为空, 拒绝注入."));
		return;
	}

	RoomGS->SetTotalRounds(InRounds);
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

	// 大厂原则 — 严格模式分支 (零兜底):
	//   - Melee 模式: 用 MeleeMatchDurationSeconds 写入 MatchEndTime
	//   - Zombie 模式: 用 ZombieMatchDurationSeconds 写入 MatchEndTime
	//   - 其他模式 (None 等): 显式 Error + return, 不静默跳过
	//   - 不允许: "Zombie 模式 MatchEndTime 保持 0 让 Widget 隐式不显示" 的兜底
	if (RoomGS->CurrentMatchMode == ERoomMatchMode::Melee)
	{
		RoomGS->MatchEndTime = GetWorld()->GetTimeSeconds() + MatchDurationSeconds;
		UE_LOG(LogTemp, Log,
			TEXT("[RoomLifecycle] StartMatchTimer: Melee 模式, MatchEndTime=%.2f (Now+%.2fs)"),
			RoomGS->MatchEndTime, MatchDurationSeconds);
	}
	else if (RoomGS->CurrentMatchMode == ERoomMatchMode::Zombie)
	{
		RoomGS->MatchEndTime = GetWorld()->GetTimeSeconds() + ZombieMatchDurationSeconds;
		UE_LOG(LogTemp, Log,
			TEXT("[RoomLifecycle] StartMatchTimer: Zombie 模式, MatchEndTime=%.2f (Now+%ds)"),
			RoomGS->MatchEndTime, ZombieMatchDurationSeconds);
	}
	else
	{
		UE_LOG(LogTemp, Error,
			TEXT("[RoomLifecycle] StartMatchTimer: CurrentMatchMode=%d 未识别, 拒绝设置 MatchEndTime. "
			     "【修复】检查 GameMode CurrentMatchMode 是否被合法赋值."),
			static_cast<int32>(RoomGS->CurrentMatchMode));
		return;
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
		UE_LOG(LogTemp, Log, TEXT("生化模式全部 %d 回合结束, 准备进入全局结算..."), RoomGS->TotalRounds);
		// 【v92 大厂架构】全部回合结束, 关闭母体变异倒计时
		ResetMotherMutationCountdown();
		// 全局结算由 GameMode 决定, 这里只清理 Round 状态
		return false; // 没有下一回合
	}

	UE_LOG(LogTemp, Log, TEXT("第 %d/%d 回合结束"), RoomGS->TotalRounds - RoomGS->CurrentRound, RoomGS->TotalRounds);
	// 【v92 大厂架构】本局结束, 关闭倒计时 (下一局由 StartNextZombieRound 重新启动)
	ResetMotherMutationCountdown();
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
	UE_LOG(LogTemp, Log, TEXT("第 %d/%d 回合开始"), RoomGS->TotalRounds - RoomGS->CurrentRound + 1, RoomGS->TotalRounds);

	// 重新启动比赛计时器 (使用与首局相同的 ZombieMatchDurationSeconds, 重置 MatchEndTime)
	StartMatchTimer();

	// 【v108 大厂架构】新回合开始, 重启母体变异倒计时
	// 重置母体账本 (跨回合累积 Bug 修复: AliveMotherCount=120)
	if (URoomMotherMutationSubsystem* MutationSys = URoomMotherMutationSubsystem::Get(this))
	{
		MutationSys->ResetForNewRound();
	}

	// 【v92 大厂架构】新回合开始, 重启母体变异倒计时 (玩家/AI 重置为人类, 重新走 8s 变异倒计时)
	StartMotherMutationCountdown();

	return true;
}


// ==========================================
// 【v92 大厂架构新增】母体变异倒计时调度
// ==========================================

/**
 * StartMotherMutationCountdown
 *
 * 启动母体变异倒计时 (服务器内部调用)
 * 大厂原则 — 单一入口:
 *   - 写入 GameState 的 Replicated 字段, 引擎自动同步到所有客户端
 *   - GameState 内部触发 OnMotherMutationChanged 广播, UI 订阅后显示倒计时
 *
 * 大厂原则 — 零兜底 (用户决策 A: 配错 ≤ 0 静默跳过):
 *   - MotherMutationDurationSeconds <= 0 → Log Warning + 跳过 (业务可禁用)
 *   - GameState 为空 → Log Error + return
 */
void URoomLifecycleSubsystem::StartMotherMutationCountdown()
{
	// 大厂原则 — Lifecycle 仅在服务器运行, 此处不重复 HasAuthority 检查
	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[RoomLifecycle] StartMotherMutationCountdown: World 为空, 拒绝启动."));
		return;
	}

	ARoomGameState* RoomGS = World->GetGameState<ARoomGameState>();
	if (!RoomGS)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[RoomLifecycle] StartMotherMutationCountdown: GameState 为空, 拒绝启动. "
			     "【修复】检查 PerformGameStart 调用顺序 (GameState 必须已存在)."));
		return;
	}

	// 大厂原则 — 用户决策 A: 配错 ≤ 0 静默跳过启动
	if (MotherMutationDurationSeconds <= 0.0f)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[RoomLifecycle] StartMotherMutationCountdown: MotherMutationDurationSeconds=%.2f <= 0, "
			     "业务禁用母体变异倒计时, 跳过启动. (按用户决策 A 静默跳过)"),
			MotherMutationDurationSeconds);
		return;
	}

	// 仅生化模式启动 (刀战模式不需要)
	if (RoomGS->CurrentMatchMode != ERoomMatchMode::Zombie)
	{
		UE_LOG(LogTemp, Log,
			TEXT("[RoomLifecycle] StartMotherMutationCountdown: 当前模式=%d, 非生化模式, 跳过启动."),
			static_cast<int32>(RoomGS->CurrentMatchMode));
		return;
	}

	// 委托给 GameState 写入 Replicated 字段
	RoomGS->StartMotherMutationCountdown(MotherMutationDurationSeconds);

	// 【v93.1 大厂架构】重置防重入标志 (新局开始时)
	RoomGS->ResetMotherMutationHasFired();

	// 【v93.1 大厂架构】SetTimer 到期触发母体变异业务
	// 大厂原则 — 倒计时到期 = 服务器端业务事件 (不是 UI 事件)
	//   - UI 倒计时显示: GameState.OnRep_MotherMutationState 客户端被动渲染
	//   - 业务触发: 服务器 SetTimer 到期 → MotherMutationSubsystem::HandleCountdownExpired
	//   - 镜像 v30 复活无敌期: 业务事件用 SetTimer, UI 用 Replicate 字段
	//
	// 大厂原则 — 重复启动清理:
	//   - ClearTimer 旧的 (防御 LifecycleSubsystem 重复启动 SetTimer 残留)
	//   - 再 SetTimer 新的 (镜像 MatchTimerHandle 模式)
	if (URoomMotherMutationSubsystem* MutationSys = URoomMotherMutationSubsystem::Get(this))
	{
		if (MotherMutationTimerHandle.IsValid())
		{
			World->GetTimerManager().ClearTimer(MotherMutationTimerHandle);
		}

		TWeakObjectPtr<URoomMotherMutationSubsystem> WeakMutation(MutationSys);
		World->GetTimerManager().SetTimer(
			MotherMutationTimerHandle,
			FTimerDelegate::CreateLambda([WeakMutation]()
			{
				if (URoomMotherMutationSubsystem* Sys = WeakMutation.Get())
				{
					Sys->HandleCountdownExpired();
				}
				else
				{
					UE_LOG(LogTemp, Error,
						TEXT("[RoomLifecycle] MotherMutationTimer 回调: MotherMutationSubsystem 已销毁 (World 卸载?). 母体变异业务未触发."));
				}
			}),
			MotherMutationDurationSeconds,
			false); // 一次性, 不循环
	}
	else
	{
		UE_LOG(LogTemp, Error,
			TEXT("[RoomLifecycle] StartMotherMutationCountdown: URoomMotherMutationSubsystem 不可用, SetTimer 跳过. "
			     "【修复】检查 ARoomGameMode::InjectSubsystemConfigs 是否调用. "
			     "【业务后果】UI 倒计时显示正常, 但倒计时到期后不会触发母体变异."));
	}

	UE_LOG(LogTemp, Log,
		TEXT("[RoomLifecycle] StartMotherMutationCountdown: 母体变异倒计时已启动, Duration=%.2fs"),
		MotherMutationDurationSeconds);
}


/**
 * ResetMotherMutationCountdown
 *
 * 重置母体变异倒计时 (关闭倒计时)
 * 大厂原则 — 显式失败链:
 *   - GameState 为空 → Log Error + return (不静默跳过)
 */
void URoomLifecycleSubsystem::ResetMotherMutationCountdown()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[RoomLifecycle] ResetMotherMutationCountdown: World 为空, 拒绝重置."));
		return;
	}

	ARoomGameState* RoomGS = World->GetGameState<ARoomGameState>();
	if (!RoomGS)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[RoomLifecycle] ResetMotherMutationCountdown: GameState 为空, 拒绝重置."));
		return;
	}

	RoomGS->ResetMotherMutationCountdown();

	// 【v93.1 大厂架构】ClearTimer 防残留 (镜像 MatchTimerHandle)
	// 大厂原则 — 防 Timer 残留:
	//   - 如果 LifecycleSubsystem 正在重启 / 模式切换, 旧的 SetTimer 必须 Clear
	//   - 否则旧 Timer 到期会触发"幽灵母体变异" (GameState 防重入会挡, 但浪费一次 Log Error)
	if (World)
	{
		if (MotherMutationTimerHandle.IsValid())
		{
			World->GetTimerManager().ClearTimer(MotherMutationTimerHandle);
		}
	}
}