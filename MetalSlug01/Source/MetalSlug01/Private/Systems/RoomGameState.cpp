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

	// 【v92 大厂架构重构】TotalRounds Replicated (替换 ZombieTotalRounds)
	DOREPLIFETIME(ARoomGameState, TotalRounds);

	DOREPLIFETIME(ARoomGameState, AttackerTotalKills);
	DOREPLIFETIME(ARoomGameState, DefenderTotalKills);
	DOREPLIFETIME(ARoomGameState, AttackerWins);
	DOREPLIFETIME(ARoomGameState, DefenderWins);

	// 【v46 新增】AI 占位队列复制
	DOREPLIFETIME(ARoomGameState, ReplicatedPendingAIQueue);

	// 【v92 新增】母体变异倒计时复制
	DOREPLIFETIME(ARoomGameState, MotherMutationStartTime);
	DOREPLIFETIME(ARoomGameState, MotherMutationDuration);

	// 【生化模式】空投倒计时复制
	DOREPLIFETIME(ARoomGameState, AirdropCountdownStartTime);
	DOREPLIFETIME(ARoomGameState, AirdropCountdownDuration);

	// 【v93.1 新增】母体变异触发标志 + 次数复制 (防重入层 2 + 业务统计)
	// 大厂原则 — 镜像 v27 FactionTag: 没有 DOREPLIFETIME = 客户端永远是默认值 → 防重入失效
	DOREPLIFETIME(ARoomGameState, MotherMutationHasFired);
	DOREPLIFETIME(ARoomGameState, MotherMutationCount);
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
// 【v92 大厂架构重构】总局数同步 (替换 ZombieTotalRounds)
// ==========================================

/**
 * OnRep_TotalRounds
 *
 * 客户端接到 TotalRounds 同步时的回调
 * 触发 OnTotalRoundsUpdated 委托, UI 立即刷新 "总局数：xx" 显示
 *
 * 大厂原则 — 与 OnRep_CurrentRound 对称:
 *   - CurrentRound: 内部计数器, UI 不订阅, 由 LifecycleSubsystem 内部用
 *   - TotalRounds: UI 真理源, UI 订阅 OnTotalRoundsUpdated
 *   - 两个字段职责完全分离 (内部 vs UI)
 */
void ARoomGameState::OnRep_TotalRounds()
{
	UE_LOG(LogTemp, Log,
		TEXT("[RoomGameState] OnRep_TotalRounds: 总局数同步! TotalRounds=%d"),
		TotalRounds);

	OnTotalRoundsUpdated.Broadcast(TotalRounds);
}


/**
 * SetTotalRounds (服务器专用)
 *
 * 大厂原则 — 显式优于隐式 (零兜底):
 *   - InTotalRounds < 1 → Log Error + return (强制修复 GameMode 配置)
 *   - 服务器不写"非权威默认值" (如 5), 让 Bug 立即暴露
 *   - 服务器写入字段后立即手动 Broadcast (自身 OnRep 不触发)
 */
void ARoomGameState::SetTotalRounds(int32 InTotalRounds)
{
	if (!HasAuthority())
	{
		UE_LOG(LogTemp, Error,
			TEXT("[RoomGameState] SetTotalRounds: 客户端调用非法, HasAuthority=false."));
		return;
	}

	if (InTotalRounds < 1)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[RoomGameState] SetTotalRounds: InTotalRounds=%d < 1, 拒绝写入. "
			     "【修复】检查 GameMode.TotalRounds 字段配置 (ClampMin=1)."),
			InTotalRounds);
		return;
	}

	TotalRounds = InTotalRounds;

	// 服务器自身不会触发 OnRep, 手动广播 (镜像 OnRep_HostPlayerName)
	OnTotalRoundsUpdated.Broadcast(TotalRounds);

	UE_LOG(LogTemp, Log,
		TEXT("[RoomGameState] SetTotalRounds: 总局数已设置! TotalRounds=%d"),
		TotalRounds);
}


// ==========================================
// 【v93 大厂架构新增】房间模式同步 (修复 OnRep 永远不 Broadcast 的 Bug)
// ==========================================
//
// 根因:
//   - GameState.CurrentMatchMode 在 v53 是 Replicated, 但没有 OnRep_CurrentMatchMode
//   - GameFlowSubsystem.cpp:435 直接 GS->CurrentMatchMode = RoomMode (绕开 OnRep 路径)
//   - OnMatchModeChanged 委托存在但永远不 Broadcast
//   - UI 订阅者 (URoomInsidePage) 永远收不到"模式变了"通知
//
// 修复 (v93 大厂架构):
//   - ReplicatedUsing = OnRep_CurrentMatchMode → 客户端 OnRep 自动 Broadcast
//   - 服务器写入走 SetCurrentMatchMode 公开 API → 服务器手动 Broadcast (镜像 SetTotalRounds)
//   - GameFlowSubsystem 改用 GS->SetCurrentMatchMode(RoomMode) (单一入口, 显式优于隐式)
// ==========================================

/**
 * OnRep_CurrentMatchMode
 *
 * 客户端接到 CurrentMatchMode 同步时的回调
 * 触发 OnMatchModeChanged 委托, UI 立即响应 (URoomInsidePage 切换 Melee/Zombie 容器显隐)
 *
 * 大厂原则 — 与 OnRep_TotalRounds 对称:
 *   - OnRep_TotalRounds: 服务器写入 → 客户端 OnRep → Broadcast OnTotalRoundsUpdated
 *   - OnRep_CurrentMatchMode: 服务器写入 → 客户端 OnRep → Broadcast OnMatchModeChanged
 *   - 两条路径完全对称 (UE 引擎 OnRep 机制保证跨网络同步触发)
 */
void ARoomGameState::OnRep_CurrentMatchMode()
{
	UE_LOG(LogTemp, Log,
		TEXT("[RoomGameState] OnRep_CurrentMatchMode: 房间模式同步! CurrentMatchMode=%d"),
		static_cast<int32>(CurrentMatchMode));

	OnMatchModeChanged.Broadcast(CurrentMatchMode);
}


/**
 * SetCurrentMatchMode (服务器专用)
 *
 * 大厂原则 — 显式优于隐式 (零兜底):
 *   - 不允许: 外部代码直接 GS->CurrentMatchMode = NewMode (绕过 OnRep 路径, 不触发 Broadcast)
 *   - 必须: 走 SetCurrentMatchMode 公开 API (镜像 SetTotalRounds 模式)
 *   - NewMode == None → Log Error + 拒绝写入 (强制修复调用方)
 *   - 服务器写入字段后立即手动 Broadcast (自身 OnRep 不触发)
 *
 * 调用方:
 *   - UGameFlowSubsystem::EnterSkipToHostMode (测试模式写入 RoomMode)
 *
 * @param NewMode 新模式 (必须为 Melee 或 Zombie, 不允许 None)
 */
void ARoomGameState::SetCurrentMatchMode(ERoomMatchMode NewMode)
{
	if (!HasAuthority())
	{
		UE_LOG(LogTemp, Error,
			TEXT("[RoomGameState] SetCurrentMatchMode: 客户端调用非法, HasAuthority=false. "
			     "【修复】SetCurrentMatchMode 必须由服务器调用 (例如 GameFlowSubsystem)."));
		return;
	}

	if (NewMode == ERoomMatchMode::None)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[RoomGameState] SetCurrentMatchMode: NewMode=None 拒绝写入. "
			     "【修复】调用方必须传入合法模式 (Melee/Zombie). "
			     "【背景】None 表示未配置模式, 不允许写入 GameState."));
		return;
	}

	CurrentMatchMode = NewMode;

	// 服务器自身不会触发 OnRep, 手动广播 (镜像 OnRep_TotalRounds)
	OnMatchModeChanged.Broadcast(CurrentMatchMode);

	UE_LOG(LogTemp, Log,
		TEXT("[RoomGameState] SetCurrentMatchMode: 房间模式已设置! CurrentMatchMode=%d"),
		static_cast<int32>(CurrentMatchMode));
}


// ==========================================
// 【v92 大厂架构新增】生化模式母体变异倒计时
// ==========================================

/**
 * OnRep_MotherMutationState
 *
 * 客户端接到 MotherMutationStartTime/Duration 同步时的回调
 * 触发 OnMotherMutationChanged 委托, Widget 收到事件后用 GetServerWorldTimeSeconds() 计算剩余秒数
 *
 * 大厂原则 — 镜像 OnRep_CurrentRound:
 *   - 不在 OnRep 内强制刷 UI (零兜底, 业务方自行决定)
 *   - 只负责 Broadcast, UI 自己用 DirtyFlag + NativeTick 渲染
 */
void ARoomGameState::OnRep_MotherMutationState()
{
	UE_LOG(LogTemp, Log,
		TEXT("[RoomGameState] OnRep_MotherMutationState: 母体变异倒计时同步! StartTime=%.2f, Duration=%.2f"),
		MotherMutationStartTime, MotherMutationDuration);

	OnMotherMutationChanged.Broadcast(MotherMutationStartTime, MotherMutationDuration);
}


/**
 * GetMotherMutationRemainingSeconds
 *
 * 计算母体变异倒计时剩余秒数
 * 使用 GetServerWorldTimeSeconds() 自适应网络延迟
 *
 * 大厂原则 — 镜像 GetMatchRemainingSeconds:
 *   - 服务器未启动倒计时 (Duration <= 0) → 返回 0
 *   - 倒计时已结束 → 返回 0 (不允许负数)
 *
 * @return 剩余秒数（最小为 0）
 */
int32 ARoomGameState::GetMotherMutationRemainingSeconds() const
{
	// 服务器未启动倒计时, 返回 0
	if (MotherMutationDuration <= 0.0f || MotherMutationStartTime <= 0.0f)
	{
		return 0;
	}

	// 使用内置的获取服务器预估世界时间的方法, 自动消除客户端与服务器端的时间差
	const float CurrentServerTime = GetServerWorldTimeSeconds();

	// 计算倒计时结束时间戳 + 剩余秒数 (钳制到 0 以上)
	const float EndTime = MotherMutationStartTime + MotherMutationDuration;
	const int32 RemainingSeconds = FMath::Max(0, FMath::RoundToInt(EndTime - CurrentServerTime));
	return RemainingSeconds;
}


/**
 * StartMotherMutationCountdown (服务器专用)
 *
 * 启动母体变异倒计时
 * 写入 MotherMutationStartTime/Duration, 引擎自动 Replicate 到所有客户端
 * OnRep_MotherMutationState 会在客户端触发 OnMotherMutationChanged 广播
 *
 * 大厂原则 — 零兜底:
 *   - Duration <= 0 → Log Error + return (强制业务方传有效值)
 *   - 服务器只写权威字段, 客户端本地计算剩余秒数
 *
 * @param Duration 倒计时总秒数 (必须 > 0)
 */
void ARoomGameState::StartMotherMutationCountdown(float Duration)
{
	// 仅在服务器端执行
	if (!HasAuthority())
	{
		UE_LOG(LogTemp, Error,
			TEXT("[RoomGameState] StartMotherMutationCountdown: 客户端调用非法, HasAuthority=false. 仅服务器可启动倒计时."));
		return;
	}

	// 大厂原则 — 零兜底: Duration 必须 > 0
	if (Duration <= 0.0f)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[RoomGameState] StartMotherMutationCountdown: Duration=%.2f 必须 > 0, 拒绝启动. "
			     "【修复】检查 LifecycleSubsystem 注入的 MotherMutationDurationSeconds 配置."),
			Duration);
		return;
	}

	// 写入 Replicated 字段 (引擎自动复制)
	MotherMutationStartTime = GetServerWorldTimeSeconds();
	MotherMutationDuration = Duration;

	// 服务器自身不会触发 OnRep, 手动广播 (镜像 OnRep_HostPlayerName 等其他字段)
	OnMotherMutationChanged.Broadcast(MotherMutationStartTime, MotherMutationDuration);

	UE_LOG(LogTemp, Log,
		TEXT("[RoomGameState] StartMotherMutationCountdown: 母体变异倒计时已启动! StartTime=%.2f, Duration=%.2fs"),
		MotherMutationStartTime, MotherMutationDuration);
}


/**
 * ResetMotherMutationCountdown (服务器专用)
 *
 * 重置母体变异倒计时 (关闭倒计时)
 * 写入 StartTime/Duration = 0, 客户端 GetMotherMutationRemainingSeconds() 自动返回 0
 *
 * 用途: 倒计时到期后, 服务器主动关闭, 客户端 Widget 收到事件后隐藏 TextBlock
 */
void ARoomGameState::ResetMotherMutationCountdown()
{
	// 仅在服务器端执行
	if (!HasAuthority())
	{
		return;
	}

	MotherMutationStartTime = 0.0f;
	MotherMutationDuration = 0.0f;

	// 服务器自身不会触发 OnRep, 手动广播
	OnMotherMutationChanged.Broadcast(0.0f, 0.0f);

	UE_LOG(LogTemp, Log, TEXT("[RoomGameState] ResetMotherMutationCountdown: 母体变异倒计时已重置."));
}


// ==========================================
// 【v93.1 大厂架构新增】母体变异触发标志 — 防重入
// ==========================================

/**
 * MarkMotherMutationFired — 服务器专用
 *
 * 大厂原则 — 显式优于隐式:
 *   - 仅服务器可调用 (HasAuthority 校验)
 *   - 写入 Replicated 字段 → 自动同步到客户端
 *   - 业务约束: 一局比赛只能调用一次 (URoomMotherMutationSubsystem 内已防重入, 这里再加一道防御)
 *
 * 调用方:
 *   - URoomMotherMutationSubsystem::HandleCountdownExpired (倒计时到期, 已选好母体)
 */
void ARoomGameState::MarkMotherMutationFired()
{
	// 大厂原则 — 服务器权威校验
	if (!HasAuthority())
	{
		UE_LOG(LogTemp, Error,
			TEXT("[RoomGameState] MarkMotherMutationFired: 客户端调用非法, HasAuthority=false. 仅服务器可标记."));
		return;
	}

	if (MotherMutationHasFired)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[RoomGameState] MarkMotherMutationFired: MotherMutationHasFired 已为 true, 重复调用. "
			     "【根因】URoomMotherMutationSubsystem 防重入失效, 检查 bMotherMutationFired_Local 字段."));
		return;
	}

	MotherMutationHasFired = true;
	MotherMutationCount++;

	UE_LOG(LogTemp, Display,
		TEXT("[RoomGameState] MarkMotherMutationFired: 已标记母体变异触发 (Count=%d)"),
		MotherMutationCount);
}


/**
 * ResetMotherMutationHasFired — 服务器专用 (新回合 / 模式切换)
 *
 * 大厂原则 — 显式优于隐式:
 *   - 仅服务器可调用 (HasAuthority 校验)
 *   - MotherMutationCount 不清 (业务统计字段, 应保留历史)
 *
 * 调用方:
 *   - URoomLifecycleSubsystem::StartMotherMutationCountdown (新局开始)
 *   - URoomLifecycleSubsystem::HandleZombieRoundEnd (本局结束)
 */
void ARoomGameState::ResetMotherMutationHasFired()
{
	if (!HasAuthority())
	{
		UE_LOG(LogTemp, Error,
			TEXT("[RoomGameState] ResetMotherMutationHasFired: 客户端调用非法, HasAuthority=false."));
		return;
	}

	MotherMutationHasFired = false;

	UE_LOG(LogTemp, Log,
		TEXT("[RoomGameState] ResetMotherMutationHasFired: 防重入标志已重置 (MotherMutationCount=%d 保留)"),
		MotherMutationCount);
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


// ==========================================
// 【生化模式】空投降临倒计时
// ==========================================

/**
 * OnRep_AirdropCountdownState
 *
 * 客户端接到 AirdropCountdownStartTime/Duration 同步时的回调
 * 触发 OnAirdropCountdownChanged 委托, Widget 收到事件后用 GetServerWorldTimeSeconds() 计算剩余秒数
 *
 * 大厂原则 — 镜像 OnRep_MotherMutationState:
 *   - 不在 OnRep 内强制刷 UI (零兜底, 业务方自行决定)
 *   - 只负责 Broadcast, UI 自己用 DirtyFlag + NativeTick 渲染
 */
void ARoomGameState::OnRep_AirdropCountdownState()
{
	UE_LOG(LogTemp, Log,
		TEXT("[RoomGameState] OnRep_AirdropCountdownState: 空投倒计时同步! StartTime=%.2f, Duration=%.2f"),
		AirdropCountdownStartTime, AirdropCountdownDuration);

	OnAirdropCountdownChanged.Broadcast(AirdropCountdownStartTime, AirdropCountdownDuration);
}


/**
 * GetAirdropRemainingSeconds
 *
 * 计算空投倒计时剩余秒数
 * 使用 GetServerWorldTimeSeconds() 自适应网络延迟
 *
 * 大厂原则 — 镜像 GetMotherMutationRemainingSeconds:
 *   - 服务器未启动倒计时 (Duration <= 0) → 返回 0
 *   - 倒计时已结束 → 返回 0 (不允许负数)
 *
 * @return 剩余秒数（最小为 0）
 */
int32 ARoomGameState::GetAirdropRemainingSeconds() const
{
	// 服务器未启动倒计时, 返回 0
	if (AirdropCountdownDuration <= 0.0f || AirdropCountdownStartTime <= 0.0f)
	{
		return 0;
	}

	// 使用内置的获取服务器预估世界时间的方法, 自动消除客户端与服务器端的时间差
	const float CurrentServerTime = GetServerWorldTimeSeconds();

	// 计算倒计时结束时间戳 + 剩余秒数 (钳制到 0 以上)
	const float EndTime = AirdropCountdownStartTime + AirdropCountdownDuration;
	const int32 RemainingSeconds = FMath::Max(0, FMath::RoundToInt(EndTime - CurrentServerTime));
	return RemainingSeconds;
}


/**
 * StartAirdropCountdown (服务器专用)
 *
 * 启动空投倒计时
 * 写入 AirdropCountdownStartTime/Duration, 引擎自动 Replicate 到所有客户端
 * OnRep_AirdropCountdownState 会在客户端触发 OnAirdropCountdownChanged 广播
 *
 * 大厂原则 — 镜像 StartMotherMutationCountdown:
 *   - Duration <= 0 → Log Error + return (强制业务方传有效值)
 *   - 服务器只写权威字段, 客户端本地计算剩余秒数
 *
 * @param Duration 倒计时总秒数 (必须 > 0)
 */
void ARoomGameState::StartAirdropCountdown(float Duration)
{
	// 仅在服务器端执行
	if (!HasAuthority())
	{
		UE_LOG(LogTemp, Error,
			TEXT("[RoomGameState] StartAirdropCountdown: 客户端调用非法, HasAuthority=false. 仅服务器可启动空投倒计时."));
		return;
	}

	// 大厂原则 — 零兜底: Duration 必须 > 0
	if (Duration <= 0.0f)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[RoomGameState] StartAirdropCountdown: Duration=%.2f 必须 > 0, 拒绝启动. "
			     "【修复】检查 LifecycleSubsystem 注入的 AirdropIntervalSeconds 配置."),
			Duration);
		return;
	}

	// 写入 Replicated 字段 (引擎自动复制)
	AirdropCountdownStartTime = GetServerWorldTimeSeconds();
	AirdropCountdownDuration = Duration;

	// 服务器自身不会触发 OnRep, 手动广播 (镜像 StartMotherMutationCountdown)
	OnAirdropCountdownChanged.Broadcast(AirdropCountdownStartTime, AirdropCountdownDuration);

	UE_LOG(LogTemp, Log,
		TEXT("[RoomGameState] StartAirdropCountdown: 空投降临倒计时已启动! StartTime=%.2f, Duration=%.2fs"),
		AirdropCountdownStartTime, AirdropCountdownDuration);
}


/**
 * ResetAirdropCountdown (服务器专用)
 *
 * 重置空投倒计时 (关闭倒计时)
 * 写入 StartTime/Duration = 0, 客户端 GetAirdropRemainingSeconds() 自动返回 0
 *
 * 用途: 新小局开始前 / 当前小局结束时 / 模式切换时调用, 强制关闭倒计时
 */
void ARoomGameState::ResetAirdropCountdown()
{
	// 仅在服务器端执行
	if (!HasAuthority())
	{
		return;
	}

	AirdropCountdownStartTime = 0.0f;
	AirdropCountdownDuration = 0.0f;

	// 服务器自身不会触发 OnRep, 手动广播
	OnAirdropCountdownChanged.Broadcast(0.0f, 0.0f);

	UE_LOG(LogTemp, Log, TEXT("[RoomGameState] ResetAirdropCountdown: 空投倒计时已关闭."));
}
