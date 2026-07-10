// 版权声明：在项目设置的描述页面填写您的版权信息。

// ==========================================
// 头文件包含区
// ==========================================
// 引入本类头文件
#include "Systems/RoomGameState.h"

// 引入 Net/UnrealNetwork.h（DOREPLIFETIME 宏的来源）
#include "Net/UnrealNetwork.h"

// 【P0】OnRep_HostPlayerName 内部转发给 URoomService 事件总线
#include "Services/RoomService.h"

// 引入房间 PlayerState
#include "Systems/Core/RoomPlayerState.h"

// 引入角色基类（用于 GetAC/GetIsDead 等）
#include "Characters/BaseCharacter.h"
#include "Data/Faction/FactionTags.h" // 【2026.07.10 P0 重构】阵营集中定义


// ==========================================
// 1. 构造函数
// ==========================================

/**
 * ARoomGameState 构造函数
 *
 * 目的: 配置 GameState 的基本属性
 * 关键: 开启 bReplicates 确保 GameState 本身在网络中同步
 */
ARoomGameState::ARoomGameState()
{
	// 确保 GameState 本身开启同步
	bReplicates = true;
}


// ==========================================
// 2. 阵营查询接口
// ==========================================

/**
 * GetPlayersInFaction (2026.07.10 P0 重构: 替代 GetPlayersInTeam(ERoomTeam))
 *
 * 获取指定阵营的所有玩家 PlayerState
 * 直接利用引擎底层的 PlayerArray，永远不会出现名单不一致的问题
 *
 * @param TargetFactionTag 目标阵营 (Offense/Defense)
 * @return 该阵营的 PlayerState 列表 (Tag 非有效阵营时返回空数组, 显式报错)
 */
TArray<ARoomPlayerState*> ARoomGameState::GetPlayersInFaction(FGameplayTag TargetFactionTag) const
{
	TArray<ARoomPlayerState*> FactionMembers;

	// 【P0 2026.07.10】大厂原则: 无效阵营不静默兜底, 显式报错
	if (!FFactionTags::IsValidFaction(TargetFactionTag))
	{
		UE_LOG(LogTemp, Error,
			TEXT("[RoomGameState] GetPlayersInFaction: 无效阵营 Tag='%s', 返回空数组"),
			*TargetFactionTag.ToString());
		return FactionMembers;
	}

	// 直接利用引擎底层的 PlayerArray, 不会有名单不一致问题
	for (APlayerState* PS : PlayerArray)
	{
		if (ARoomPlayerState* RoomPS = Cast<ARoomPlayerState>(PS))
		{
			if (RoomPS->CurrentFactionTag == TargetFactionTag)
			{
				FactionMembers.Add(RoomPS);
			}
		}
	}

	return FactionMembers;
}


/**
 * GetFactionTopACPlayer (2026.07.10 P0 重构: 替代 GetTeamTopACPlayer(ERoomTeam))
 *
 * 查询指定阵营中 AC 最高的玩家的 PlayerState
 * 忽略死亡或无 pawn 的玩家
 *
 * @param TargetFactionTag 目标阵营 (Offense/Defense)
 * @return AC 最高的 PlayerState（找不到返回 nullptr）
 */
ARoomPlayerState* ARoomGameState::GetFactionTopACPlayer(FGameplayTag TargetFactionTag) const
{
	if (!FFactionTags::IsValidFaction(TargetFactionTag))
	{
		UE_LOG(LogTemp, Error,
			TEXT("[RoomGameState] GetFactionTopACPlayer: 无效阵营 Tag='%s'"),
			*TargetFactionTag.ToString());
		return nullptr;
	}

	ARoomPlayerState* TopPlayer = nullptr;
	int32 TopAC = -1;

	for (APlayerState* PS : PlayerArray)
	{
		if (ARoomPlayerState* RoomPS = Cast<ARoomPlayerState>(PS))
		{
			// 阵营不匹配，跳过
			if (RoomPS->CurrentFactionTag != TargetFactionTag)
			{
				continue;
			}

			ABaseCharacter* Char = Cast<ABaseCharacter>(RoomPS->GetPawn());
			if (!Char || Char->GetIsDead())
			{
				// 死亡或无 Pawn，跳过
				continue;
			}

			// 找出 AC 最高的
			if (Char->GetAC() > TopAC)
			{
				TopAC = Char->GetAC();
				TopPlayer = RoomPS;
			}
		}
	}

	return TopPlayer;
}


/**
 * GetOverallTopACPlayer
 *
 * 查询全场所有玩家中 AC 最高的玩家的 PlayerState
 * 忽略死亡或无 pawn 的玩家
 *
 * @return 全场 AC 最高的 PlayerState（找不到返回 nullptr）
 */
ARoomPlayerState* ARoomGameState::GetOverallTopACPlayer() const
{
	ARoomPlayerState* TopPlayer = nullptr;
	int32 TopAC = -1;

	for (APlayerState* PS : PlayerArray)
	{
		if (ARoomPlayerState* RoomPS = Cast<ARoomPlayerState>(PS))
		{
			ABaseCharacter* Char = Cast<ABaseCharacter>(RoomPS->GetPawn());
			if (!Char || Char->GetIsDead())
			{
				continue;
			}

			if (Char->GetAC() > TopAC)
			{
				TopAC = Char->GetAC();
				TopPlayer = RoomPS;
			}
		}
	}

	return TopPlayer;
}


// ==========================================
// 3. 网络同步注册
// ==========================================

/**
 * GetLifetimeReplicatedProps
 *
 * 注册需要网络同步的 UPROPERTY
 * 这些变量在服务器端修改后会自动同步到所有客户端
 */
void ARoomGameState::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// 注册同步变量
	DOREPLIFETIME(ARoomGameState, CurrentMatchMode);
	DOREPLIFETIME(ARoomGameState, MatchEndTime);
	DOREPLIFETIME(ARoomGameState, HostPlayerName);
	DOREPLIFETIME(ARoomGameState, CurrentRound);
	DOREPLIFETIME(ARoomGameState, AttackerTotalKills);
	DOREPLIFETIME(ARoomGameState, DefenderTotalKills);
	DOREPLIFETIME(ARoomGameState, AttackerWins);
	DOREPLIFETIME(ARoomGameState, DefenderWins);

	// 【v46 新增】AI 占位队列复制
	DOREPLIFETIME(ARoomGameState, ReplicatedPendingAIQueue);
}


// ==========================================
// 4. 客户端属性同步回调
// ==========================================

/**
 * OnRep_CurrentRound
 *
 * 客户端接到 CurrentRound 同步时的回调
 * 触发 OnCurrentRoundUpdated 委托，UI 据此刷新回合数显示
 */
void ARoomGameState::OnRep_CurrentRound()
{
	OnCurrentRoundUpdated.Broadcast(CurrentRound);
}


// ==========================================
// 5. 时间计算接口
// ==========================================

/**
 * GetMatchRemainingSeconds
 *
 * 计算剩余比赛时间（秒）
 * 使用 GetServerWorldTimeSeconds() 自适应网络延迟
 *
 * @return 剩余秒数（最小为 0）
 */
int32 ARoomGameState::GetMatchRemainingSeconds() const
{
	// 如果尚未设置有效时间戳，直接返回 0
	if (MatchEndTime <= 0.0f)
	{
		return 0;
	}

	// 使用内置的获取服务器预估世界时间的方法，自动消除客户端与服务器端的时间差
	float CurrentServerTime = GetServerWorldTimeSeconds();

	// 计算剩余秒数并钳制到0以上，避免出现负数倒计时
	int32 RemainingSeconds = FMath::Max(0, FMath::RoundToInt(MatchEndTime - CurrentServerTime));
	return RemainingSeconds;
}


// ==========================================
// 6. 击杀统计
// ==========================================

/**
 * OnRep_TeamKillCount
 *
 * 客户端接到 AttackerTotalKills / DefenderTotalKills 同步时的回调
 * 触发 OnTeamKillCountUpdated 委托
 */
void ARoomGameState::OnRep_TeamKillCount()
{
	FString NetModeStr;
	switch (GetNetMode())
	{
	case NM_Standalone: NetModeStr = TEXT("Standalone"); break;
	case NM_ListenServer: NetModeStr = TEXT("ListenServer"); break;
	case NM_DedicatedServer: NetModeStr = TEXT("DedicatedServer"); break;
	case NM_Client: NetModeStr = TEXT("Client"); break;
	default: NetModeStr = TEXT("Unknown"); break;
	}
	UE_LOG(LogTemp, Log, TEXT("[RoomGameState] OnRep_TeamKillCount 触发！NetMode=%s, AttackerTotalKills=%d, DefenderTotalKills=%d"),
		*NetModeStr, AttackerTotalKills, DefenderTotalKills);
	OnTeamKillCountUpdated.Broadcast(AttackerTotalKills, DefenderTotalKills);
}


/**
 * AddTeamKill
 *
 * 服务器专用: 增加指定阵营的击杀数 (2026.07.10 P0 重构 — FGameplayTag 替代 ERoomTeam)
 * 同时通过 MulticastRefreshKillCount 强制广播给所有客户端（包括 ListenServer 主机自身）
 *
 * @param FactionTag 目标阵营 (Offense/Defense, 其它 Tag 显式报错)
 */
void ARoomGameState::AddTeamKill(FGameplayTag FactionTag)
{
	// 仅在服务器端执行
	if (!HasAuthority())
	{
		return;
	}

	// 【P0 2026.07.10】大厂原则: 无效阵营显式报错, 不增任何字段
	if (!FFactionTags::IsValidFaction(FactionTag))
	{
		UE_LOG(LogTemp, Error,
			TEXT("[RoomGameState] AddTeamKill: 无效阵营 Tag='%s', 击杀数不变"),
			*FactionTag.ToString());
		return;
	}

	UE_LOG(LogTemp, Log,
		TEXT("[RoomGameState] AddTeamKill 被调用！FactionTag=%s, Before: AttackerTotalKills=%d, DefenderTotalKills=%d"),
		*FactionTag.ToString(), AttackerTotalKills, DefenderTotalKills);

	// 【P0 2026.07.10】单一真理源: Offense = 攻方, Defense = 守方
	if (FactionTag == FFactionTags::Offense())
	{
		AttackerTotalKills++;
	}
	else if (FactionTag == FFactionTags::Defense())
	{
		DefenderTotalKills++;
	}
	// 其它有效阵营理论上不存在, 但 IsValidFaction 已检查, 这里无需 else

	UE_LOG(LogTemp, Log,
		TEXT("[RoomGameState] AddTeamKill 执行完毕！After: AttackerTotalKills=%d, DefenderTotalKills=%d"),
		AttackerTotalKills, DefenderTotalKills);

	// 【核心修复】: 强制广播给所有客户端（包括 Listen Server 主机自身）
	// 原因: OnRep_TeamKillCount 在 Listen Server 本地不会触发，导致房主 UI 永远不更新
	// NetMulticast 从服务器向所有连接的客户端广播，确保每个客户端都能收到击杀数刷新通知
	MulticastRefreshKillCount(AttackerTotalKills, DefenderTotalKills);
}


/**
 * MulticastRefreshKillCount_Implementation
 *
 * NetMulticast 实现: 在所有客户端（包括 ListenServer 主机）触发
 * 广播 OnTeamKillCountUpdated 委托
 */
void ARoomGameState::MulticastRefreshKillCount_Implementation(int32 AttackerKills, int32 DefenderKills)
{
	UE_LOG(LogTemp, Log, TEXT("[RoomGameState] MulticastRefreshKillCount 被调用！AttackerKills=%d, DefenderKills=%d"), AttackerKills, DefenderKills);
	OnTeamKillCountUpdated.Broadcast(AttackerKills, DefenderKills);
}


/**
 * ResetTeamKillStats
 *
 * 服务器专用: 重置双方击杀统计
 * 调用时机: 每回合/每局开始时
 */
void ARoomGameState::ResetTeamKillStats()
{
	// 仅在服务器端执行
	if (!HasAuthority())
	{
		return;
	}

	AttackerTotalKills = 0;
	DefenderTotalKills = 0;
}


// ==========================================
// 7. 胜负统计
// ==========================================

/**
 * OnRep_WinStats
 *
 * 客户端接到 AttackerWins / DefenderWins 同步时的回调
 * 触发 OnWinStatsUpdated 委托
 */
void ARoomGameState::OnRep_WinStats()
{
	UE_LOG(LogTemp, Log, TEXT("[RoomGameState] OnRep_WinStats 触发！AttackerWins=%d, DefenderWins=%d"),
		AttackerWins, DefenderWins);
	OnWinStatsUpdated.Broadcast(AttackerWins, DefenderWins);
}


// ==========================================
// 8. 结算系统
// ==========================================

/**
 * TriggerSettlement
 *
 * 服务器专用: 执行当局结算（由 RoomGameMode 在倒计时归零时调用）
 * 1. 判断当局胜负，累加胜局数
 * 2. 立刻广播"进入结算状态"事件（NetMulticast 修复纯客户端进程不触发的问题）
 * 3. 延迟 3 秒广播"显示最终结果"事件
 */
void ARoomGameState::TriggerSettlement()
{
	// 仅在服务器端执行
	if (!HasAuthority())
	{
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("[RoomGameState] TriggerSettlement: 倒计时结束，开始结算！AttackerKills=%d, DefenderKills=%d"),
		AttackerTotalKills, DefenderTotalKills);

	// 步骤 1: 判断当局胜负，累加胜局数
	if (AttackerTotalKills > DefenderTotalKills)
	{
		AttackerWins++;
		UE_LOG(LogTemp, Log, TEXT("[RoomGameState] 攻方获胜！AttackerWins=%d"), AttackerWins);
	}
	else if (DefenderTotalKills > AttackerTotalKills)
	{
		DefenderWins++;
		UE_LOG(LogTemp, Log, TEXT("[RoomGameState] 守方获胜！DefenderWins=%d"), DefenderWins);
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("[RoomGameState] 当局平局，双方均不得分。"));
	}

	// 步骤 2: 立刻广播"进入结算状态"事件，让所有客户端显示比分面板
	// 【网络架构修复】: 使用 NetMulticast 替代原有的直接 Broadcast
	// 原因: 纯客户端进程的 OnEnterSettlement.Broadcast() 不会触发，导致 Text_GameOver 不显示
	MulticastEnterSettlement(AttackerTotalKills, DefenderTotalKills);

	// 步骤 3: 通过 World Timer 延迟3秒，然后广播"显示最终结果"事件
	UWorld* World = GetWorld();
	if (World)
	{
		// 先清除可能存在的旧定时器（防止重复触发）
		World->GetTimerManager().ClearTimer(SettlementTimerHandle);
		World->GetTimerManager().SetTimer(SettlementTimerHandle, this, &ARoomGameState::BroadcastFinalSettlement, 3.0f, false);
	}
}


/**
 * BroadcastFinalSettlement
 *
 * 延迟 3 秒后触发的最终结算广播
 * 使用 NetMulticast 替代原 HasAuthority + Broadcast 方案
 * 解决 ListenServer 中纯客户端进程 HasAuthority() 返回 false 的问题
 */
void ARoomGameState::BroadcastFinalSettlement()
{
	// 【网络架构修复】: 使用 NetMulticast 替代原有的 HasAuthority + Broadcast 方案
	// 原问题: 在 Listen Server 中，纯客户端进程的 HasAuthority() 返回 false，导致 OnShowFinalSettlement 从未广播给房主以外的玩家
	// 解决方案: NetMulticast RPC 在服务器端调用时，引擎自动将函数调用复制到所有连接的客户端
	MulticastShowFinalSettlement(AttackerWins, DefenderWins);
}


/**
 * MulticastShowFinalSettlement_Implementation
 *
 * NetMulticast 实现: 广播最终结算事件
 * 所有客户端均会执行此函数，触发 OnShowFinalSettlement 委托
 */
void ARoomGameState::MulticastShowFinalSettlement_Implementation(int32 InAttackerWins, int32 InDefenderWins)
{
	UE_LOG(LogTemp, Log, TEXT("[RoomGameState] MulticastShowFinalSettlement: 攻方胜%d局, 守方胜%d局"), InAttackerWins, InDefenderWins);

	// 广播最终结算事件，附带双方的总胜局数（所有客户端均会执行此行）
	OnShowFinalSettlement.Broadcast(InAttackerWins, InDefenderWins);
}


/**
 * MulticastEnterSettlement_Implementation
 *
 * NetMulticast 实现: 广播进入结算事件
 * 所有客户端均会执行此函数，触发 OnEnterSettlement 委托
 * 让 UI 显示 Text_GameOver
 */
void ARoomGameState::MulticastEnterSettlement_Implementation(int32 InAttackerKills, int32 InDefenderKills)
{
	UE_LOG(LogTemp, Log, TEXT("[RoomGameState] MulticastEnterSettlement: 攻方=%d, 守方=%d"), InAttackerKills, InDefenderKills);

	// 广播进入结算事件，让所有客户端显示 Text_GameOver
	OnEnterSettlement.Broadcast(InAttackerKills, InDefenderKills);
}


// ==========================================
// 【P0 架构升级】房主变更 OnRep 回调
// ==========================================

/**
 * OnRep_HostPlayerName
 *
 * 服务端修改 HostPlayerName 后自动同步到所有客户端
 * 职责: 把变更转发给 URoomService.BroadcastHostChanged, 让 UI 订阅者收到"我是不是房主"通知
 *
 * 注意:
 *  - 服务器本身不会触发 OnRep, 所以服务端调用 TransferHostTo 时也要手动广播
 *  - 这里只处理客户端 OnRep 路径
 *
 * 实现规范:
 *  - 走 URoomService::GetCurrentAccountName() 而非直接读 PC->MyPlayerName
 *  - 避免 #include RoomPlayerController.h 头文件依赖, 也符合"业务层不直读 UPROPERTY"大厂规范
 */
void ARoomGameState::OnRep_HostPlayerName()
{
	UE_LOG(LogTemp, Log, TEXT("[RoomGameState] OnRep_HostPlayerName: 新房主 = %s"), *HostPlayerName);

	// 【P0】转发给 URoomService 事件总线 (客户端路径)
	if (HasAuthority())
	{
		// 服务端不走 OnRep, 但若手动调用也兼容 (防重入)
		return;
	}

	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// 【P0 架构修正】走 URoomService 门面, 不直读 PC->MyPlayerName
	// GetCurrentAccountName() 内部已经处理了 PC/LocalPlayer 的兼容读取, 是统一访问入口
	FString LocalAccountName;
	if (URoomService* RoomService = URoomService::Get(this))
	{
		LocalAccountName = RoomService->GetCurrentAccountName();
	}

	const bool bIsHostNow = !LocalAccountName.IsEmpty()
		&& LocalAccountName.Equals(HostPlayerName, ESearchCase::IgnoreCase);

	URoomService::BroadcastHostChanged(World, bIsHostNow);
}


// ==========================================
// 【v46 新增】AI 占位队列复制回调
// ==========================================

/**
 * OnRep_ReplicatedPendingAIQueue
 *
 * 客户端收到 ReplicatedPendingAIQueue 同步时的回调
 * 触发 OnPendingAIQueueChanged 广播, 让 URoomInsidePage 刷新 UI
 */
void ARoomGameState::OnRep_ReplicatedPendingAIQueue()
{
	UE_LOG(LogTemp, Log,
		TEXT("[RoomGameState] OnRep_ReplicatedPendingAIQueue: 触发 AI 队列刷新! Count=%d"),
		ReplicatedPendingAIQueue.Num());

	OnPendingAIQueueChanged.Broadcast();
}


/**
 * GetPendingAICountInFaction
 *
 * 查询指定阵营的 AI 占位数量 (客户端 UI 用)
 */
int32 ARoomGameState::GetPendingAICountInFaction(FGameplayTag FactionTag) const
{
	if (!FFactionTags::IsValidFaction(FactionTag))
	{
		return 0;
	}

	return ReplicatedPendingAIQueue.FilterByPredicate([FactionTag](const FPendingAIEntry& Entry)
	{
		return Entry.FactionTag == FactionTag;
	}).Num();
}
