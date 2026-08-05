// Copyright Epic Games, Inc. All Rights Reserved.

#include "Systems/Lifecycle/RoomLifecycleSubsystem.h"
#include "Systems/Airdrop/RoomAirdropSubsystem.h"  // 【v117】空投子系统回调
#include "Systems/RoomGameMode.h"
#include "Systems/RoomGameState.h"
#include "Systems/RoomPlayerController.h"
#include "Systems/Spawn/RoomSpawnSubsystem.h"  // 【v201】RestartZombieRoundPlayers
#include "Systems/Mother/RoomMotherMutationSubsystem.h"
#include "Enums/CoreEnums.h"
// 【v134 大厂架构修复】FFactionTags::Offense/Defense 静态方法 (cpp 必须显式 include, 不依赖 transitive)
#include "Data/Faction/FactionTags.h"
#include "Data/Enums/RoomEnums.h" // EZombieRoundWinner 枚举
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
	//
	// 大厂原则 — v134 零下限修复:
	//   - 旧版 ClampMin=30 是反模式 (静默篡改策划配置), 已删除
	//   - 新版: ZombieMatchDurationSeconds <= 0 拒绝启动 + Log Error (强制修复 BP 配置)
	//   - 镜像 Melee 模式, 也加防御层 (尽管 Melee 仍 ClampMin=30, 防御性检查不亏)
	if (RoomGS->CurrentMatchMode == ERoomMatchMode::Melee)
	{
		if (MatchDurationSeconds <= 0.0f)
		{
			UE_LOG(LogTemp, Error,
				TEXT("[RoomLifecycle] StartMatchTimer: Melee MatchDurationSeconds=%.2f <= 0, 拒绝启动. "
				     "【修复】检查 GameMode.MeleeMatchDurationSeconds 配置 (ClampMin=30)."),
				MatchDurationSeconds);
			return;
		}
		RoomGS->MatchEndTime = GetWorld()->GetTimeSeconds() + MatchDurationSeconds;
		UE_LOG(LogTemp, Log,
			TEXT("[RoomLifecycle] StartMatchTimer: Melee 模式, MatchEndTime=%.2f (Now+%.2fs)"),
			RoomGS->MatchEndTime, MatchDurationSeconds);
	}
	else if (RoomGS->CurrentMatchMode == ERoomMatchMode::Zombie)
	{
		// 【v134 大厂原则 — 零下限修复】ZombieMatchDurationSeconds <= 0 拒绝启动
		//   旧版 ClampMin=30 是反模式 — 已删除, 现在策划可任意配置
		//   配错 <= 0 → 拒绝启动 + Log Error, 强制修复 BP 配置
		if (ZombieMatchDurationSeconds <= 0)
		{
			UE_LOG(LogTemp, Error,
				TEXT("[RoomLifecycle] StartMatchTimer: Zombie ZombieMatchDurationSeconds=%d <= 0, 拒绝启动. "
				     "【修复】在 GM_RoomGameMode BP Class Defaults → MetalSlug|Match → ZombieMatchDurationSeconds 配 > 0 的值. "
				     "【业务后果】倒计时永不开始, 本小局无胜负判定."),
				ZombieMatchDurationSeconds);
			return;
		}
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

	// 大厂原则 — 镜像 v92 模式守卫:
	//   - Melee 模式: 检测倒计时归零 → HandleMatchTimeOut (走 TriggerSettlement 旧路径)
	//   - Zombie 模式: 检测倒计时归零 → FinishZombieRound (新路径, 走 HasAliveHumanOnField 判定)
	//   - 【v134 P0 修复】Zombie 模式原本缺失 OnMatchTimerTick Zombie 分支, 倒计时永远不触发 — 已修复
	if (RoomGS->CurrentMatchMode == ERoomMatchMode::Melee)
	{
		if (GetWorld()->GetTimeSeconds() >= RoomGS->MatchEndTime)
		{
			GetWorld()->GetTimerManager().ClearTimer(MatchTimerHandle);
			HandleMatchTimeOut();
		}
	}
	else if (RoomGS->CurrentMatchMode == ERoomMatchMode::Zombie)
	{
		// 【v134 P0 修复】生物化模式倒计时到期 + 提前结束 (人验收者) 共用 Tick 路径
		//
		// 大厂原则 — 零重复架构:
		//   - 单个 Timer Tick 内同时检查"倒计时归零" + "人验收者"
		//   - 任一满足 → 调 FinishZombieRound 收尾
		//   - 两个检查独立, 不互斥 (理论上同帧同时为真 = 倒计时结束时人类刚死 → 互不影响)
		//
		// 大厂原则 — 幂等:
		//   - FinishZombieRound 内部会拒绝 RoundWinner 已写的情况
		//   - 同一 Tick 内两次调用只有第一次生效
		bool bNeedFinishRound = false;

		// 路径 A: 倒计时归零
		if (GetWorld()->GetTimeSeconds() >= RoomGS->MatchEndTime)
		{
			bNeedFinishRound = true;
			UE_LOG(LogTemp, Display,
				TEXT("[RoomLifecycle] OnMatchTimerTick: 生化模式倒计时归零 (剩余秒数 = %.2fs), 触发本小局结束."),
				FMath::Max(0.0f, RoomGS->MatchEndTime - GetWorld()->GetTimeSeconds()));
		}

		// 路径 B: 场上无存活人类 (提前结束)
		//   - 即使倒计时没到, 无人存活也立即结束本小局
		//   - 用户 2026.08.06 明确: "如没剩余人类了, 就算 Text_RoundCountdown 没倒计时结束也要提前结束此小局"
		//   - 【v134 v4 修复】HasAliveHumanOnField 返回 EHASResult:
		//     * HasAliveHuman / NoAliveHuman → 触发 finish
		//     * NoData (0 Pawn) → 跳过本 Tick (修复"一进游戏就播母体赢音效" bug)
		const EHASResult HASResult = RoomGS->HasAliveHumanOnField();
		if (HASResult == EHASResult::NoAliveHuman)
		{
			bNeedFinishRound = true;
			UE_LOG(LogTemp, Display,
				TEXT("[RoomLifecycle] OnMatchTimerTick: 生化模式场上无存活人类 (倒计时未结束), 触发本小局提前结束 → Mother 赢."));
		}
		else if (HASResult == EHASResult::NoData)
		{
			UE_LOG(LogTemp, Verbose,
				TEXT("[RoomLifecycle] OnMatchTimerTick: 生化模式场上 0 Pawn (Spawn 未完成或异常), 跳过本 Tick 结算. "
				     "【业务后果】不触发本小局结束, 等待下一 Tick 再次检查."));
		}
		// EHASResult::HasAliveHuman → 不触发 finish (正常游戏中)

		if (bNeedFinishRound)
		{
			GetWorld()->GetTimerManager().ClearTimer(MatchTimerHandle);
			HandleZombieRoundEnd();
		}
	}
	else
	{
		UE_LOG(LogTemp, Error,
			TEXT("[RoomLifecycle] OnMatchTimerTick: CurrentMatchMode=%d 未识别, 拒绝处理 Tick. "
			     "【修复】检查 GameMode CurrentMatchMode 是否被合法赋值."),
			static_cast<int32>(RoomGS->CurrentMatchMode));
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
		// 【v201.3 大厂架构修复】生化模式走 HandleZombieRoundEnd 统一入口
		//   - 旧版直接调 FinishZombieRound, 跳过了 StartNextZombieRound
		//   - 新版 HandleZombieRoundEnd 是"本小局结束"业务唯一入口
		//     内含 FinishZombieRound 统一结算 + StartNextZombieRound 开始下一小局
		//   - 调用方 (OnMatchTimerTick) 在调用前已 ClearTimer, 这里无需重复
		HandleZombieRoundEnd();
	}
}

/**
 * 【v134 大厂架构重构】FinishZombieRound — 生化小局结算统一入口
 *
 * 业务规则 (用户 2026.08.06 明确):
 *   - 倒计时结束 → FinishZombieRound 触发
 *   - 提前结束 (无存活人类) → FinishZombieRound 触发
 *   - 业务唯一入口: 写 RoundWinner + 累加胜局数 + 广播 OnEnterSettlement (复用现有 Multicast)
 *
 * 大厂原则 — 单一入口 (零重复架构):
 *   - FinishZombieRound 是"本小局结束"业务唯一入口
 *   - 调用方: HandleMatchTimeOut (倒计时结束) + OnMatchTimerTick (提前结束 — Tick 检测无存活人类)
 *   - 内部流程:
 *     1. 校验模式 (Zombie) + 幂等 (RoundWinner 已为 Human/Mother 时跳过)
 *     2. 调 GameState.HasAliveHumanOnField 判定胜负
 *     3. GameState.SetRoundWinner(Human 或 Mother)
 *     4. 累加胜局数 (DefenderWins++ / AttackerWins++)
 *     5. 调 GameState.MulticastEnterSettlement 广播进入结算 (复用现有 RPC, **零新建**)
 *
 * 大厂原则 — 零兜底:
 *   - 非生化模式 → Log Error + return false
 *   - World/GameState 为空 → Log Error + return false
 *   - 幂等 (RoundWinner 已写) → return true (不算错)
 *
 * 不破坏刀战模式:
 *   - 刀战永不调用本函数, 字段保持 None
 */
bool URoomLifecycleSubsystem::FinishZombieRound()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[RoomLifecycle] FinishZombieRound: World 为空, 拒绝结算."));
		return false;
	}

	ARoomGameState* RoomGS = World->GetGameState<ARoomGameState>();
	if (!RoomGS)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[RoomLifecycle] FinishZombieRound: GameState 为空, 拒绝结算. "
			     "【修复】检查 PerformGameStart 调用顺序 (GameState 必须已存在)."));
		return false;
	}

	// 大厂原则 — 模式守卫 (镜像 StartMotherMutationCountdown):
	//   - 本函数仅在生化模式调用, 刀战模式 Log Error + return
	if (RoomGS->CurrentMatchMode != ERoomMatchMode::Zombie)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[RoomLifecycle] FinishZombieRound: 当前模式=%d, 非生化模式, 拒绝结算. "
			     "【修复】FinishZombieRound 仅由 Zombie 模式 Tick / Timeout 触发."),
			static_cast<int32>(RoomGS->CurrentMatchMode));
		return false;
	}

	// 大厂原则 — 幂等防御 (镜像 MotherMutationHasFired):
	//   - RoundWinner 已为 Human/Mother → 不重复结算 (防止双发, 双发会重复累加胜局数)
	if (RoomGS->RoundWinner != EZombieRoundWinner::None)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[RoomLifecycle] FinishZombieRound: RoundWinner 已为 %d, 幂等跳过 (本小局已结算). "
			     "【根因】OnMatchTimerTick 同帧内路径 A + 路径 B 同时触发, 第二次调用被挡."),
			static_cast<int32>(RoomGS->RoundWinner));
		return true;
	}

	// 步骤 1: 胜负判定 (单一真理源 — GameState.HasAliveHumanOnField)
	//   - 【v134 v4 修复】返回 EHASResult 而非 bool, 修复 "一进游戏就播母体赢音效" bug
	//   - NoData (0 Pawn) → 显式拒绝结算 (业务上不可能, 必配置错或时序未到)
	const EHASResult HASResult = RoomGS->HasAliveHumanOnField();
	if (HASResult == EHASResult::NoData)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[RoomLifecycle] FinishZombieRound: HasAliveHumanOnField 返回 NoData (场上 0 Pawn), 拒绝结算. "
			     "【根因】业务上不可能 — FinishZombieRound 应当只在 Tick 中检测到 NoAliveHuman 时调用, 或倒计时归零时调用. "
			     "【可能时序】当前模式下场上还没 Spawn 完成 → 推迟到下一 Tick. "
			     "【修复】检查调用方 (OnMatchTimerTick / 倒计时归零 / 其他入口) 是否在 Spawn 完成前触发."));
		return false;
	}
	const EZombieRoundWinner NewWinner = (HASResult == EHASResult::HasAliveHuman)
		? EZombieRoundWinner::Human
		: EZombieRoundWinner::Mother;

	// 步骤 2: 写入 RoundWinner (Replicated → OnRep 推所有客户端)
	RoomGS->SetRoundWinner(NewWinner);

	// 步骤 3: 累加胜局数 (单一真理源 — GameState.AddRoundWinToFaction)
	//   - Human 赢 → Defense (人类) → DefenderWins++
	//   - Mother 赢 → Offense (母体) → AttackerWins++
	//   - 内部触发 OnRep_WinStats → 所有客户端 UI 刷新 (Text_AttackerCount / Text_DefenderCount 显示源)
	//   - **不调** MulticastEnterSettlement: 它会隐藏 MatchInfo, 违反"每小局显示得分"业务
	//   - **不调** MulticastShowFinalSettlement: 它是全局结算才用, 非小局结束
	//   - 现有 OnWinStatsUpdated 委托链路已存在 UI 订阅 (GameHUDWidget::UpdateTeamKillCountsText)
	//     → 累加后 UI 自动刷新 (镜像 MulticastRefreshKillCount 调用方式)
	const FGameplayTag WinnerFaction = (NewWinner == EZombieRoundWinner::Human)
		? FFactionTags::Defense()
		: FFactionTags::Offense();
	RoomGS->AddRoundWinToFaction(WinnerFaction);

	// 步骤 4: 广播音效 (新增 MulticastPlayZombieRoundSound — 由 GameHUDWidget 订阅)
	//   - 大厂原则 — 零重复架构: 不重用 OnEnterSettlement RPC (它会隐藏 UI)
	//   - 单独建一条 RPC 链路, 专用于"小局结束音效", 客户端 OnRep 查 GameMode 音效表 + 播放
	//   - 因为 UObject* 不能跨 RPC (UE 5.6), 仅传枚举 + FactionTag, 客户端查 USoundBase*
	RoomGS->MulticastPlayZombieRoundSound(NewWinner);
	//   - 已绑定: UGameHUDWidget::OnZombieRoundSoundReceived (Multicast RPC 触发 → 查 GameMode 音效 + 播放)

	// 【v201 大厂架构修复】只有最后一局才进入结算页面
	//   - 非最后一局: 短暂显示胜负文本 (3秒), 然后延迟开始下一小局
	//   - 最后一局: 进入全局结算页面
	if (RoomGS->CurrentRound <= 1)
	{
		// 最后一局: 广播进入结算页面
		RoomGS->MulticastEnterSettlement(RoomGS->AttackerTotalKills, RoomGS->DefenderTotalKills, NewWinner);
		RoomGS->ScheduleFinalSettlement(3.0f);
	}
	else
	{
		// 非最后一局: 短暂显示小局结果 (3秒后自动隐藏), 不进入结算页面
		RoomGS->MulticastShowZombieRoundBriefResult(NewWinner);

		// 【v201.4 大厂架构新增】短暂结果显示需要3秒，等待后开始下一小局
		//   - 先清理旧的Timer防残留
		//   - 3秒延迟确保玩家能看到小局结果提示
		GetWorld()->GetTimerManager().ClearTimer(RoundTransitionTimerHandle);
		GetWorld()->GetTimerManager().SetTimer(RoundTransitionTimerHandle, this,
			&URoomLifecycleSubsystem::OnRoundTransitionTimerExpired, 3.0f, false);
	}

	UE_LOG(LogTemp, Log,
		TEXT("[RoomLifecycle] FinishZombieRound: 生化本小局结算完成. RoundWinner=%d, AttackerWins=%d, DefenderWins=%d. %s"),
		static_cast<int32>(NewWinner), RoomGS->AttackerWins, RoomGS->DefenderWins,
		(RoomGS->CurrentRound <= 1) ? TEXT("最后一局, 进入全局结算.") : TEXT("还有下一局, 短暂显示结果后进入下一小局."));

	return true;
}

bool URoomLifecycleSubsystem::HandleZombieRoundEnd()
{
	ARoomGameState* RoomGS = GetWorld()->GetGameState<ARoomGameState>();
	if (!RoomGS)
	{
		return false;
	}

	// 【v134 大厂架构重构】HandleZombieRoundEnd 现在只判断"是否还有下一回合"
	//   - 胜负判定 + RoundWinner 写入 + 胜局数累加 + Multicast 全部走 FinishZombieRound 单一入口
	//   - 旧版 HandleZombieRoundEnd 自己算胜负 = 重复架构 — 已废弃
	//
	// 大厂原则 — 零重复架构:
	//   - 调用方 (RoomGameMode 委托 / 外部 BP 触发) → HandleZombieRoundEnd 入口
	//   - 内部: 先调 FinishZombieRound (统一结算) → 然后判断 CurrentRound <= 1 (决定下一局)
	//   - 单一真理源: FinishZombieRound 是"本小局结束"业务唯一入口
	const bool bHasMoreRounds = FinishZombieRound();

	if (RoomGS->CurrentRound <= 1)
	{
		// 最后一局: 清理资源和关闭母体变异倒计时
		UE_LOG(LogTemp, Log, TEXT("生化模式全部 %d 回合结束, 准备进入全局结算..."), RoomGS->TotalRounds);
		// 【v92 大厂架构】全部回合结束, 关闭母体变异倒计时
		ResetMotherMutationCountdown();
		// 全局结算由 GameMode 决定, 这里只清理 Round 状态
		return false; // 没有下一回合
	}

	// 【v201.4 大厂架构修复】非最后一局: 短暂结果显示已由 FinishZombieRound 末尾设置Timer
	//   - Timer 3秒后调用 OnRoundTransitionTimerExpired → StartNextZombieRound
	//   - 这里只关闭母体变异倒计时
	UE_LOG(LogTemp, Log, TEXT("第 %d/%d 回合结束, 短暂结果显示中..."),
		RoomGS->TotalRounds - RoomGS->CurrentRound, RoomGS->TotalRounds);
	ResetMotherMutationCountdown();

	return true; // 下一回合由Timer触发
}

bool URoomLifecycleSubsystem::StartNextZombieRound()
{
	ARoomGameState* RoomGS = GetWorld()->GetGameState<ARoomGameState>();
	if (!RoomGS || RoomGS->CurrentRound <= 1)
	{
		return false;
	}

	// 【v134 大厂架构修复】调 ResetRoundWinner 确保下一局开始时 RoundWinner = None
	//   - 幂等防御: RoundWinner = None → 本局未开, 避免 OnRep_RoundWinner 触发非法结算
	//   - 影像: 复制到客户端, UGameHUDWidget 订阅的 OnRoundWinnerUpdated 不会混乱
	RoomGS->ResetRoundWinner();

	RoomGS->CurrentRound--;
	UE_LOG(LogTemp, Log, TEXT("第 %d/%d 回合开始"), RoomGS->TotalRounds - RoomGS->CurrentRound + 1, RoomGS->TotalRounds);

	// 重新启动比赛计时器 (使用与首局相同的 ZombieMatchDurationSeconds, 重置 MatchEndTime)
	StartMatchTimer();

	// 【v201 大厂架构新增】小局结束后重新分配所有人类玩家到 HumanSurvivor 复活点
	//   - 必须在 StartMatchTimer 之后调用 (确保计时器重新开始)
	//   - 必须在 StartMotherMutationCountdown 之前调用 (确保新回合开始时玩家在新位置)
	if (URoomSpawnSubsystem* SpawnSys = URoomSpawnSubsystem::Get(this))
	{
		SpawnSys->RestartZombieRoundPlayers();
	}

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

		// 【生化模式】新一轮母体变异倒计时开始前，关闭上一轮空投状态。
		RoomGS->ResetAirdropCountdown();
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

	// 【生化模式】当前小局结束时关闭空投倒计时，下一小局母体变异结束后再解锁。
	RoomGS->ResetAirdropCountdown();
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

		// 【v117 大厂架构新增】同步清理空投降临倒计时 Timer
		//   - 模式切换 / 整场结束 → 必须清掉空投 Timer, 防止"幽灵降临"
		//   - 同 v93.1 防残留逻辑
		if (AirdropIntervalTimerHandle.IsValid())
		{
			World->GetTimerManager().ClearTimer(AirdropIntervalTimerHandle);
		}
	}
}


// ==========================================
// 【生化模式】空投降临倒计时 — 单一调度入口
// ==========================================
//
// 业务规则 (用户 2026.08.03 明确):
//   - 每小局开局先走母体变异倒计时 (玩家 + AI 互相无敌)
//   - 母体变异一结束, 立刻启动首轮空投倒计时
//   - 空投实际降临完毕后, 由空投系统回调 NotifyAirdropArrivalCompleted 重新倒计时
//   - 任何切换回合 / 模式切换 / GameMode 兜底, 都要把空投倒计时关掉
//
// 大厂原则 — 单一入口:
//   - 启动入口: StartAirdropCountdown (母体变异倒计时 SetTimer 回调结束时调用)
//   - 复用入口: NotifyAirdropArrivalCompleted (空投系统降临完毕时调用)
//   - 重置入口: 已被 ResetMotherMutationCountdown + StartMotherMutationCountdown 镜像调用
//
// 大厂原则 — 零兜底:
//   - 配错 AirdropIntervalSeconds <= 0 → 静默跳过 (用户决策 A: 业务可禁用)
//   - 非生化模式 → 跳过 (镜像母体变异倒计时的模式守卫)
//   - GameState 为空 → Log Error + return
//   - 重复启动: ClearTimer 旧的再 SetTimer 新的 (镜像 v92 MotherMutationTimerHandle)

/**
 * URoomLifecycleSubsystem::StartAirdropCountdown
 *
 * 服务器内部入口 — 由母体变异倒计时到期时调用, 启动首轮空投倒计时
 *
 * 大厂原则 — 镜像 StartMotherMutationCountdown:
 *   - 启动前先 Reset 上一轮空投状态 (避免 StartTime/Duration 残留)
 *   - 启动后 SetTimer 到期, 由空投系统回调 NotifyAirdropArrivalCompleted 重新倒计时
 *   - GameState 是数据源, Subsystem 是调度者, 不持有状态
 */
void URoomLifecycleSubsystem::StartAirdropCountdown()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[RoomLifecycle] StartAirdropCountdown: World 为空, 拒绝启动空投倒计时."));
		return;
	}

	ARoomGameState* RoomGS = World->GetGameState<ARoomGameState>();
	if (!RoomGS)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[RoomLifecycle] StartAirdropCountdown: GameState 为空, 拒绝启动空投倒计时."));
		return;
	}

	// 大厂原则 — 镜像 StartMotherMutationCountdown: 仅生化模式启动
	if (RoomGS->CurrentMatchMode != ERoomMatchMode::Zombie)
	{
		UE_LOG(LogTemp, Log,
			TEXT("[RoomLifecycle] StartAirdropCountdown: 当前模式=%d, 非生化模式, 跳过启动空投倒计时."),
			static_cast<int32>(RoomGS->CurrentMatchMode));
		return;
	}

	// 大厂原则 — 用户决策 A: 配错 ≤ 0 静默跳过
	if (AirdropIntervalSeconds <= 0.0f)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[RoomLifecycle] StartAirdropCountdown: AirdropIntervalSeconds=%.2f <= 0, "
			     "业务禁用空投倒计时, 跳过启动. (按用户决策 A 静默跳过)"),
			AirdropIntervalSeconds);
		return;
	}

	// 业务规则前置 — 母体变异倒计时未结束, 不允许启动空投倒计时
	//   - 这是用户 2026.08.03 明确的业务规则
	//   - 即使服务器内部误调, 也必须挡掉, 不允许"变异前就先有空投"
	if (RoomGS->GetMotherMutationRemainingSeconds() > 0)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[RoomLifecycle] StartAirdropCountdown: 母体变异倒计时仍剩余 %d 秒, 拒绝启动空投倒计时. "
			     "【业务规则】空投倒计时必须在母体变异结束后才能启动. "
			     "【修复】检查调用时机, 确认母体变异 SetTimer 已到期或已被 ResetMotherMutationCountdown."),
			RoomGS->GetMotherMutationRemainingSeconds());
		return;
	}

	// 启动 — 写入 GameState Replicated 字段, 引擎自动同步到所有客户端
	RoomGS->StartAirdropCountdown(AirdropIntervalSeconds);

	// 【v117 大厂架构新增】启动倒计时到期 Timer
	// 大厂原则 — 镜像 v93.1 MotherMutationTimerHandle:
	//   - Timer 到期 → OnAirdropIntervalExpired → AirdropSubsystem::SpawnAirdropAtAllPoints
	//   - 这是"空投降临"业务事件, 不是 UI 事件
	//   - 旧版 (上一轮 UI 接入) 漏接 Timer 回调, 倒计时永远不触发 Spawn — v117 修复
	//   - 重复启动防御: ClearTimer 旧的再 SetTimer 新的 (同 v93.1 模式)
	if (AirdropIntervalTimerHandle.IsValid())
	{
		World->GetTimerManager().ClearTimer(AirdropIntervalTimerHandle);
	}

	World->GetTimerManager().SetTimer(
		AirdropIntervalTimerHandle,
		this,
		&URoomLifecycleSubsystem::OnAirdropIntervalExpired,
		AirdropIntervalSeconds,
		false); // 一次性, 不循环

	UE_LOG(LogTemp, Log,
		TEXT("[RoomLifecycle] StartAirdropCountdown: 空投降临倒计时已启动, Duration=%.2fs"),
		AirdropIntervalSeconds);
}


// ==========================================
// 【v117 大厂架构新增】空投降临倒计时到期回调
// ==========================================

/**
 * URoomLifecycleSubsystem::OnAirdropIntervalExpired
 *
 * 业务规则 (用户 2026.08.03):
 *   - 空投降临倒计时到期 = 服务器业务事件, 触发空投降临
 *   - 调 AirdropSubsystem::SpawnAirdropAtAllPoints 销毁旧空投 + 生成新空投
 *   - 然后调 NotifyAirdropArrivalCompleted 启动下一轮倒计时 (循环往复)
 *
 * 大厂原则 — 单一入口:
 *   - 倒计时到期 = 唯一业务触发点 (客户端 UI 倒计时只是显示, 不触发业务)
 *   - 业务调用链: OnAirdropIntervalExpired → SpawnAirdropAtAllPoints → NotifyAirdropArrivalCompleted
 *   - 后者复用 StartAirdropCountdown 的全部校验 + 业务闸
 *
 * 大厂原则 — 镜像 v93.1 MotherMutationTimer 回调:
 *   - 单一职责: 只负责"倒计时到期 → 触发业务"
 *   - 不持有业务逻辑, 委托给 AirdropSubsystem
 *
 * 大厂原则 — 零兜底:
 *   - World 为空 → Log Error + return (Timer 回调时 World 不应为空, 防御用)
 *   - AirdropSubsystem 为空 → Log Error + return (没注入/没创建)
 */
void URoomLifecycleSubsystem::OnAirdropIntervalExpired()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[RoomLifecycle] OnAirdropIntervalExpired: World 为空, 拒绝触发空投降临. 【防御】Timer 回调时 World 不应为空."));
		return;
	}

	UE_LOG(LogTemp, Display,
		TEXT("[RoomLifecycle] OnAirdropIntervalExpired: 空投降临倒计时到期, 触发空投生成 + 清理旧空投."));

	// 大厂原则 — 单一入口: 委托 AirdropSubsystem 处理 "销毁旧 + 生成新"
	if (URoomAirdropSubsystem* AirdropSys = URoomAirdropSubsystem::Get(this))
	{
		const int32 SpawnedCount = AirdropSys->SpawnAirdropAtAllPoints();

		UE_LOG(LogTemp, Log,
			TEXT("[RoomLifecycle] OnAirdropIntervalExpired: 本轮共生成 %d 个空投, 现在启动下一轮倒计时."),
			SpawnedCount);
	}
	else
	{
		UE_LOG(LogTemp, Error,
			TEXT("[RoomLifecycle] OnAirdropIntervalExpired: URoomAirdropSubsystem 不可用, 拒绝 Spawn. "
			     "【修复】检查 ARoomGameMode::InjectSubsystemConfigs 或 WorldSubsystem 创建时机."));
		return;
	}

	// 大厂原则 — 立即启动下一轮倒计时: 业务上"本轮空投降临"后立刻开始"下一轮倒计时"
	//   - 复用 StartAirdropCountdown 的全部校验 (模式/配置/母体变异状态)
	//   - 防止"降临完后没下一轮, 玩家永远吃不到补给"
	NotifyAirdropArrivalCompleted();
}

/**
 * 【v201.4 大厂架构新增】小局短暂结果显示后延迟回调
 *
 * 业务规则: 非最后一局时, FinishZombieRound 显示3秒短暂提示
 *           3秒后本函数被Timer调用 → StartNextZombieRound 开始下一小局
 */
void URoomLifecycleSubsystem::OnRoundTransitionTimerExpired()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[RoomLifecycle] OnRoundTransitionTimerExpired: World 为空, 拒绝开始下一小局. 【防御】Timer 回调时 World 不应为空."));
		return;
	}

	ARoomGameState* RoomGS = World->GetGameState<ARoomGameState>();
	if (!RoomGS)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[RoomLifecycle] OnRoundTransitionTimerExpired: GameState 为空, 拒绝开始下一小局."));
		return;
	}

	// 额外安全检查: CurrentRound <= 1 说明已经是最后一局或已结束
	if (RoomGS->CurrentRound <= 1)
	{
		UE_LOG(LogTemp, Log,
			TEXT("[RoomLifecycle] OnRoundTransitionTimerExpired: CurrentRound=%d <= 1, 已是最后一局或已结束, 不再开始下一小局."),
			RoomGS->CurrentRound);
		return;
	}

	UE_LOG(LogTemp, Log,
		TEXT("[RoomLifecycle] OnRoundTransitionTimerExpired: 短暂结果显示完毕, 开始第 %d/%d 回合."),
		RoomGS->TotalRounds - RoomGS->CurrentRound + 1, RoomGS->TotalRounds);

	StartNextZombieRound();
}


/**
 * URoomLifecycleSubsystem::NotifyAirdropArrivalCompleted
 *
 * 服务器公开入口 — "确认当前空投已降临完毕", 启动下一轮空投倒计时
 *
 * 大厂原则 — 镜像 StartAirdropCountdown:
 *   - 业务上等同于"新一轮空投降临开始倒计时"
 *   - 复用 StartAirdropCountdown 的全部校验 (模式/配置/母体变异状态守卫)
 *   - 重复启动: StartAirdropCountdown 内部已 ClearTimer 防残留
 *
 * 大厂原则 — 入口:
 *   - 业务方调用: 空投系统确认本次空投降临完毕 (v117 暂未接入, 未来可由空投管理器调用)
 *   - 内部调用: OnAirdropIntervalExpired (倒计时到期时)
 */
void URoomLifecycleSubsystem::NotifyAirdropArrivalCompleted()
{
	UE_LOG(LogTemp, Log,
		TEXT("[RoomLifecycle] NotifyAirdropArrivalCompleted: 收到空投降临完毕事件, 准备重启下一轮空投倒计时."));

	// 大厂原则 — 单一入口: 复用 StartAirdropCountdown 的全部校验 + 业务闸
	//   - 模式守卫 / AirdropIntervalSeconds 守卫 / 母体变异状态守卫 全部继承
	StartAirdropCountdown();
}