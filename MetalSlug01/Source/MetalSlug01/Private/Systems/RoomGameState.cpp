// 版权声明：在项目设置的描述页面填写您的版权信息。

// ==========================================
// 头文件包含区
// ==========================================
// 引入本类头文件
#include "Systems/RoomGameState.h"

#include "Systems/RoomGameMode.h"  // 【v201.13】ARoomGameMode::TotalRounds

// 引入 Net/UnrealNetwork.h（DOREPLIFETIME 宏的来源）
#include "Net/UnrealNetwork.h"

// 【P0】OnRep_HostPlayerName 内部转发给 URoomService 事件总线
#include "Services/RoomService.h"

// 引入房间 PlayerState
#include "Systems/Core/RoomPlayerState.h"

// 引入角色基类（用于 GetAC/GetIsDead 等）
#include "Characters/BaseCharacter.h"
#include "Data/Faction/FactionTags.h" // 【2026.07.10 P0 重构】阵营集中定义

// 【v134 大厂架构新增】HasAliveHumanOnField 需要枚举 AI Controller
#include "Systems/BaseAIController.h"
// 【v134】TActorIterator (枚举 AI Controller 上的 Pawn)
#include "EngineUtils.h"
// 【v134】APlayerController (遍历本地玩家)
#include "GameFramework/PlayerController.h"


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

	// 【v210.2 大厂架构重构】小局结束音效资产缓存复制 (解决客户端无 AuthGameMode 问题)
	DOREPLIFETIME(ARoomGameState, CachedZombieHumanWinSound);
	DOREPLIFETIME(ARoomGameState, CachedZombieMotherWinSound);

	// 【v93.1 新增】母体变异触发标志 + 次数复制 (防重入层 2 + 业务统计)
	// 大厂原则 — 镜像 v27 FactionTag: 没有 DOREPLIFETIME = 客户端永远是默认值 → 防重入失效
	DOREPLIFETIME(ARoomGameState, MotherMutationHasFired);
	DOREPLIFETIME(ARoomGameState, MotherMutationCount);

	// 【v134 大厂架构新增】小局赢家复制 (RoundWinner)
	// 大厂原则 — 镜像 AttackerWins / DefenderWins: Replicated → OnRep 触发客户端 UI / 音效查表
	DOREPLIFETIME(ARoomGameState, RoundWinner);
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

	// ==========================================
	// 【v201.13 大厂架构新增】设置模式时自动补注入 GM 的 TotalRounds / ZombieMatchDuration
	// ==========================================
	//
	// 根因 (v201.12 之前):
	//   - 测试模式 (skip-login) 不走 InitGameState → InitGameState 末尾的补注入不生效
	//   - GameFlowSubsystem::BootToLogin 测试分支直接调 GS->SetCurrentMatchMode(Mode)
	//   - 结果: GS.CurrentMatchMode=Zombie, 但 GS.TotalRounds 还是默认值 5
	//   - 用户反馈 (2026.08.06): "TotalRounds 我设置了, 生化模式进游戏的总局数还是不按照设置来"
	//
	// 修复:
	//   - 任何路径调 SetCurrentMatchMode → GS 内部自动从 GM 读 TotalRounds 并写入
	//   - 涵盖: 正式路径 InitGameState (兜底)、测试路径 GameFlowSubsystem (主用)
	//
	// 大厂原则 — 单一真理源:
	//   - GM.TotalRounds 是策划配置真理源
	//   - GS.TotalRounds 是运行时真理源
	//   - SetCurrentMatchMode 是 GS 公开 API 的唯一入口, 在此注入最合适
	if (CurrentMatchMode == ERoomMatchMode::Zombie)
	{
		if (UWorld* World = GetWorld())
		{
			if (ARoomGameMode* GM = World->GetAuthGameMode<ARoomGameMode>())
			{
				const int32 GMTotalRounds = GM->TotalRounds;
				if (GMTotalRounds >= 1 && TotalRounds != GMTotalRounds)
				{
					UE_LOG(LogTemp, Log,
						TEXT("[RoomGameState] 【v201.13】SetCurrentMatchMode(Zombie) 联动注入 TotalRounds: GM.TotalRounds=%d → GS.TotalRounds=%d. "
							 "(测试模式跳过 InitGameState, 这里兜底)"),
						TotalRounds, GMTotalRounds);
					SetTotalRounds(GMTotalRounds);
				}
			}
		}
	}
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


/**
 * AddRoundWinToFaction (服务器专用)
 *
 * 大厂原则 — 镜像 SetRoundWinner:
 *   - 仅服务器可调用 (HasAuthority 校验)
 *   - 客户端调用 → Log Error + return
 *   - 写入字段后 UE 引擎自动 Replicate 触发 OnRep_WinStats → 推所有客户端
 *   - 业务上无"服务器手动 Broadcast" 的需要 (与 SetRoundWinner 不同), UE 5.6 引擎保证 Replicate
 *
 * 大厂原则 — 零兜底:
 *   - FactionTag 非 Offense/Defense → Log Error + 拒绝累加
 *   - HasAuthority=false → Log Error + return
 *
 * 调用方 (单一入口):
 *   - URoomLifecycleSubsystem::FinishZombieRound
 *
 * @param WinnerFactionTag 胜出方阵营 (Faction.Offense = 母体赢, Faction.Defense = 人类赢)
 */
void ARoomGameState::AddRoundWinToFaction(FGameplayTag WinnerFactionTag)
{
	if (!HasAuthority())
	{
		UE_LOG(LogTemp, Error,
			TEXT("[RoomGameState] AddRoundWinToFaction: 客户端调用非法, HasAuthority=false. 仅服务器可累加胜局数."));
		return;
	}

	if (WinnerFactionTag == FFactionTags::Offense())
	{
		AttackerWins++;
		// 服务器自身不会触发 OnRep_WinStats, 手动 Broadcast 以同步房主本地 UI
		OnWinStatsUpdated.Broadcast(AttackerWins, DefenderWins);
		UE_LOG(LogTemp, Log,
			TEXT("[RoomGameState] AddRoundWinToFaction: 母体赢 (Offense), AttackerWins++ → %d. 客户端 OnRep_WinStats 即将触发."),
			AttackerWins);
	}
	else if (WinnerFactionTag == FFactionTags::Defense())
	{
		DefenderWins++;
		// 服务器自身不会触发 OnRep_WinStats, 手动 Broadcast 以同步房主本地 UI
		OnWinStatsUpdated.Broadcast(AttackerWins, DefenderWins);
		UE_LOG(LogTemp, Log,
			TEXT("[RoomGameState] AddRoundWinToFaction: 人类赢 (Defense), DefenderWins++ → %d. 客户端 OnRep_WinStats 即将触发."),
			DefenderWins);
	}
	else
	{
		UE_LOG(LogTemp, Error,
			TEXT("[RoomGameState] AddRoundWinToFaction: WinnerFactionTag='%s' 非 Offense/Defense, 拒绝累加. "
			     "【修复】调用方必须传 Faction.Offense (母体赢) 或 Faction.Defense (人类赢). "
			     "【业务后果】本小局胜局数未累加, UI 显示会不一致."),
			*WinnerFactionTag.ToString());
	}
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

	// 【v200.1 大厂架构新增】先构建 RoundWinner，后面广播时用
	EZombieRoundWinner LocalRoundWinner = EZombieRoundWinner::None;

	UE_LOG(LogTemp, Log, TEXT("[RoomGameState] TriggerSettlement: 倒计时结束，开始结算！AttackerKills=%d, DefenderKills=%d"),
		AttackerTotalKills, DefenderTotalKills);

	// 步骤 1: 判断当局胜负，累加胜局数
	if (AttackerTotalKills > DefenderTotalKills)
	{
		AttackerWins++;
		LocalRoundWinner = EZombieRoundWinner::Mother; // 攻方胜=幽灵赢（刀战语义复用）
		UE_LOG(LogTemp, Log, TEXT("[RoomGameState] 攻方获胜！AttackerWins=%d"), AttackerWins);
	}
	else if (DefenderTotalKills > AttackerTotalKills)
	{
		DefenderWins++;
		LocalRoundWinner = EZombieRoundWinner::Human; // 守方胜=人类赢（刀战语义复用）
		UE_LOG(LogTemp, Log, TEXT("[RoomGameState] 守方获胜！DefenderWins=%d"), DefenderWins);
	}
	else
	{
		LocalRoundWinner = EZombieRoundWinner::None; // 平局
		UE_LOG(LogTemp, Log, TEXT("[RoomGameState] 当局平局，双方均不得分。"));
	}

	// 步骤 2: 立刻广播"进入结算状态"事件，让所有客户端显示比分面板 + 对应胜负文本
	// 【网络架构修复】: 使用 NetMulticast 替代原有的直接 Broadcast
	// 原因: 纯客户端进程的 OnEnterSettlement.Broadcast() 不会触发，导致 Text_GameOver 不显示
	// 【v200.1 新增】: 增加 RoundWinner 参数，用于显示"人类胜利"或"幽灵胜利"文本
	MulticastEnterSettlement(AttackerTotalKills, DefenderTotalKills, LocalRoundWinner);

	// 【v200.2 大厂架构重构】步骤 3: 延迟3秒后广播"显示最终结果"事件
	//   - 复用 ScheduleFinalSettlement 方法（与生化 FinishZombieRound 路径一致）
	ScheduleFinalSettlement(3.0f);
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
 * ScheduleFinalSettlement
 *
 * 【v200.2 大厂架构新增】设置结算定时器（供 LifecycleSubsystem 调用）
 * 统一管理 SettlementTimerHandle，避免外部直接访问 private 字段
 */
void ARoomGameState::ScheduleFinalSettlement(float DelaySeconds)
{
	if (UWorld* MyWorld = GetWorld())
	{
		MyWorld->GetTimerManager().ClearTimer(SettlementTimerHandle);
		MyWorld->GetTimerManager().SetTimer(SettlementTimerHandle, this, &ARoomGameState::BroadcastFinalSettlement, DelaySeconds, false);
	}
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
 * 让 UI 显示 Text_GameOver + 对应胜负文本
 *
 * 【v200.1 大厂架构重构】: 增加 RoundWinner 参数，用于显示"人类胜利"或"幽灵胜利"
 * — 刀战复用 EZombieRoundWinner: Attacker胜=Mother, Defender胜=Human
 */
void ARoomGameState::MulticastEnterSettlement_Implementation(int32 InAttackerKills, int32 InDefenderKills, EZombieRoundWinner InRoundWinner)
{
	UE_LOG(LogTemp, Log, TEXT("[RoomGameState] MulticastEnterSettlement: 攻方=%d, 守方=%d, RoundWinner=%d"),
		InAttackerKills, InDefenderKills, static_cast<int32>(InRoundWinner));

	// 广播进入结算事件，让所有客户端显示 Text_GameOver + 对应胜负文本
	OnEnterSettlement.Broadcast(InAttackerKills, InDefenderKills, InRoundWinner);
}


/**
 * MulticastShowZombieRoundBriefResult_Implementation
 *
 * 【v201 大厂架构新增】短暂显示小局结果
 */
void ARoomGameState::MulticastShowZombieRoundBriefResult_Implementation(EZombieRoundWinner InRoundWinner)
{
	UE_LOG(LogTemp, Log, TEXT("[RoomGameState] 【v201】MulticastShowZombieRoundBriefResult: RoundWinner=%d"),
		static_cast<int32>(InRoundWinner));

	// 广播短暂显示结果事件，UI 层订阅后显示胜负文本
	OnZombieRoundBriefResult.Broadcast(InRoundWinner);
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


// ==========================================
// 【v134 大厂架构新增】生化小局赢家 (RoundWinner) — 单一真理源
// ==========================================
//
// 业务规则 (用户 2026.08.06):
//   - 服务器 URoomLifecycleSubsystem::FinishZombieRound 在每小局结束时写入 RoundWinner
//   - 客户端 OnRep_RoundWinner → Broadcast OnRoundWinnerUpdated
//   - UGameHUDWidget 缓存 + OnEnterSettlement 触发时查 GameMode 音效 + 播放
//
// 大厂原则 — 镜像 SetTotalRounds / SetCurrentMatchMode:
//   - 服务器写入字段后手动 Broadcast (自身 OnRep 不触发)
//   - 客户端 OnRep 自动 Broadcast (UE 引擎机制保证)
//
// 大厂原则 — 零兜底:
//   - InWinner == None → 拒绝写入 (强制调用方传 Human/Mother, 防止"还没结算就清空")
//   - 客户端调用 → Log Error + return
//
// 不破坏刀战模式:
//   - 刀战永不调用本函数, 字段保持 None, UI 不订阅

/**
 * OnRep_RoundWinner (客户端)
 *
 * 客户端接到 RoundWinner 同步时的回调
 * 触发 OnRoundWinnerUpdated 委托, UI 据此缓存字段 + 准备查音效
 *
 * 大厂原则 — 镜像 OnRep_WinStats:
 *   - 不在 OnRep 内强制播放音效 (避免双发)
 *   - 只负责 Broadcast, 音效播放由 GameHUDWidget 在 OnEnterSettlement 触发时统一调度
 */
void ARoomGameState::OnRep_RoundWinner()
{
	UE_LOG(LogTemp, Log,
		TEXT("[RoomGameState] OnRep_RoundWinner: 小局赢家同步! RoundWinner=%d"),
		static_cast<int32>(RoundWinner));

	OnRoundWinnerUpdated.Broadcast(RoundWinner);
}


/**
 * SetRoundWinner (服务器专用)
 *
 * 大厂原则 — 显式优于隐式 (镜像 SetTotalRounds / SetCurrentMatchMode):
 *   - 仅服务器可调用 (HasAuthority 校验)
 *   - InWinner == None → Log Error + 拒绝写入 (强制修复调用方)
 *   - 服务器写入字段后立即手动 Broadcast (自身 OnRep 不触发)
 *
 * 调用方:
 *   - URoomLifecycleSubsystem::FinishZombieRound (本小局结束唯一入口)
 *
 * @param InWinner 小局赢家 (必须为 Human 或 Mother, 不允许 None)
 */
void ARoomGameState::SetRoundWinner(EZombieRoundWinner InWinner)
{
	if (!HasAuthority())
	{
		UE_LOG(LogTemp, Error,
			TEXT("[RoomGameState] SetRoundWinner: 客户端调用非法, HasAuthority=false. 仅服务器可设置小局赢家."));
		return;
	}

	if (InWinner == EZombieRoundWinner::None)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[RoomGameState] SetRoundWinner: InWinner=None 拒绝写入. "
			     "【修复】调用方必须传 Human/Mother, 不允许传 None. "
			     "【业务后果】如需重置请改调 ResetRoundWinner 入口."));
		return;
	}

	RoundWinner = InWinner;

	// 服务器自身不会触发 OnRep, 手动广播 (镜像 OnRep_HostPlayerName 等其他字段)
	OnRoundWinnerUpdated.Broadcast(RoundWinner);

	UE_LOG(LogTemp, Log,
		TEXT("[RoomGameState] SetRoundWinner: 小局赢家已设置! RoundWinner=%d"),
		static_cast<int32>(RoundWinner));
}


/**
 * ResetRoundWinner (服务器专用)
 *
 * 大厂原则 — 镜像 ResetMotherMutationHasFired / ResetAirdropCountdown:
 *   - 仅服务器可调用 (HasAuthority 校验)
 *   - 调用方: URoomLifecycleSubsystem::StartNextZombieRound / StartMotherMutationCountdown
 *   - 新小局开始时清零, 让 UI / 音效缓存不再受旧胜负干扰
 *
 * 与 SetRoundWinner(None) 的区别:
 *   - SetRoundWinner(None) 强制拒绝 (零兜底 — 显式优于隐式)
 *   - ResetRoundWinner 是专门的"清零入口", 调用方语义明确
 */
void ARoomGameState::ResetRoundWinner()
{
	if (!HasAuthority())
	{
		UE_LOG(LogTemp, Error,
			TEXT("[RoomGameState] ResetRoundWinner: 客户端调用非法, HasAuthority=false."));
		return;
	}

	RoundWinner = EZombieRoundWinner::None;

	// 服务器自身不会触发 OnRep, 手动广播
	OnRoundWinnerUpdated.Broadcast(RoundWinner);

	UE_LOG(LogTemp, Log,
		TEXT("[RoomGameState] ResetRoundWinner: 小局赢家已重置为 None (新小局开始)."));
}


/**
 * HasAliveHumanOnField (服务器查询) — v134 v4 大厂重构 (返回 EHASResult 替代 bool)
 *
 * 业务规则:
 *   - 遍历所有 Controller (PlayerController + AIController), 检查它们 Possess 的 Pawn:
 *     - FactionTag == Faction.Defense (人类阵营)
 *     - IsDead() == false (活着)
 *   - 任一满足 → 返回 EHASResult::HasAliveHuman (有人类活着)
 *   - 没找到 Defense 活着的 Pawn, 但场上还有其他 Pawn (Offense 或 配置错) → 返回 EHASResult::NoAliveHuman (无人存活)
 *   - 场上 0 个 Pawn → 返回 EHASResult::NoData (业务上不可能, 配置错或时序未到)
 *
 * 大厂原则 — 单一真理源:
 *   - 唯一"人验收者"判定入口, URoomLifecycleSubsystem 调本函数决定 FinishZombieRound(Human/Mother)
 *
 * 大厂原则 — 零兜底 (v134 v4 修复 — "一进游戏就播母体赢音效" bug):
 *   - 旧版 (bool) 兜底: 0 存活 Pawn → return false → LifecycleSubsystem 解释为 "Mother 赢" → 错误播母体赢音效
 *   - 新版 (enum) 显式: 0 存活 Pawn → 返回 NoData → LifecycleSubsystem 必须跳过本 Tick 结算
 *   - 触发链修复: 1s 间隔 MatchTimerTick 在 MatchStartDelay (3s) Spawn 完成前触发
 *     → HasAliveHumanOnField 看到 0 Pawn → 返回 false → FinishZombieRound → Mother 赢音效播放
 *     → 现在返回 NoData → LifecycleSubsystem 跳过本 Tick → 不再错误触发
 *
 * 不破坏刀战模式:
 *   - 刀战模式永不调本函数, 但本函数本身对所有模式都可调 (返回 NoData 也无害)
 */
EHASResult ARoomGameState::HasAliveHumanOnField() const
{
	const UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[RoomGameState] HasAliveHumanOnField: World 为空, 拒绝判定. "
			     "【修复】本函数仅应在服务器 GameMode/Tick 调用, 检查调用方."));
		return EHASResult::NoData;
	}

	int32 AliveHumanCount = 0;
	int32 AliveMotherCount = 0;
	int32 AliveOtherCount = 0;

	// 路径 A: 遍历 PlayerArray (真人玩家) — 真理源 PlayerArray
	for (APlayerState* PS : PlayerArray)
	{
		ARoomPlayerState* RoomPS = Cast<ARoomPlayerState>(PS);
		if (!RoomPS)
		{
			continue;
		}

		APawn* Pawn = RoomPS->GetPawn();
		if (!Pawn)
		{
			// 玩家死亡时 Pawn 已被 Destroy, 跳过 (死亡流程已清理)
			continue;
		}

		ABaseCharacter* BaseChar = Cast<ABaseCharacter>(Pawn);
		if (!BaseChar || BaseChar->IsDead())
		{
			continue;
		}

		const FGameplayTag PawnFactionTag = BaseChar->GetFactionTag();
		if (!PawnFactionTag.IsValid())
		{
			UE_LOG(LogTemp, Error,
				TEXT("[RoomGameState] HasAliveHumanOnField: 玩家 Pawn='%s' FactionTag 为空, 跳过 (不计入). "
				     "【根因】SyncFactionTagFromController 没正确同步. 检查 PossessedBy 链路."),
				*Pawn->GetName());
			continue;
		}

		if (PawnFactionTag == FFactionTags::Defense())
		{
			AliveHumanCount++;
		}
		else if (PawnFactionTag == FFactionTags::Offense())
		{
			AliveMotherCount++;
		}
		else
		{
			UE_LOG(LogTemp, Error,
				TEXT("[RoomGameState] HasAliveHumanOnField: 玩家 Pawn='%s' FactionTag='%s' 既非 Defense 也非 Offense, 跳过. "
				     "【修复】Pawn.FactionTag 必须是 Offense/Defense 之一."),
				*Pawn->GetName(), *PawnFactionTag.ToString());
			AliveOtherCount++;
		}
	}

	// 路径 B: 遍历 AI Controller (关卡预放 AI + 大厅入队 AI)
	//   - 镜像 v40.3 SpawnSubsystem 的 AI 处理, 用 TActorIterator
	for (TActorIterator<ABaseAIController> It(const_cast<UWorld*>(World)); It; ++It)
	{
		ABaseAIController* AIC = *It;
		if (!AIC)
		{
			continue;
		}

		APawn* AIPawn = AIC->GetPawn();
		if (!AIPawn)
		{
			continue;
		}

		ABaseCharacter* BaseChar = Cast<ABaseCharacter>(AIPawn);
		if (!BaseChar || BaseChar->IsDead())
		{
			continue;
		}

		const FGameplayTag PawnFactionTag = BaseChar->GetFactionTag();
		if (!PawnFactionTag.IsValid())
		{
			UE_LOG(LogTemp, Error,
				TEXT("[RoomGameState] HasAliveHumanOnField: AI Pawn='%s' FactionTag 为空, 跳过 (不计入). "
				     "【根因】Cach[ed]FactionTag 写入链路丢失. 检查 v26/v40.3 的 CachedFactionTag 时序."),
				*AIPawn->GetName());
			continue;
		}

		if (PawnFactionTag == FFactionTags::Defense())
		{
			AliveHumanCount++;
		}
		else if (PawnFactionTag == FFactionTags::Offense())
		{
			AliveMotherCount++;
		}
		else
		{
			UE_LOG(LogTemp, Error,
				TEXT("[RoomGameState] HasAliveHumanOnField: AI Pawn='%s' FactionTag='%s' 既非 Defense 也非 Offense, 跳过. "
				     "【修复】Pawn.FactionTag 必须是 Offense/Defense 之一."),
				*AIPawn->GetName(), *PawnFactionTag.ToString());
			AliveOtherCount++;
		}
	}

	// 大厂原则 — 零兜底 (v134 v4 修复):
	//   - 没找到任何 Pawn (AliveHuman + AliveMother + AliveOther == 0) → 返回 NoData
	//   - 业务上不可能 (开局一定有玩家或 AI, 配置错或时序竞速)
	//   - 不允许静默返回 false (被 LifecycleSubsystem 错误解释为 "Mother 赢" → 错误播母体赢音效)
	//   - 调用方收到 NoData 必须跳过本 Tick 结算, 修复 "一进游戏就播母体赢音效" bug
	if (AliveHumanCount == 0 && AliveMotherCount == 0 && AliveOtherCount == 0)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[RoomGameState] HasAliveHumanOnField: 场上没有任何 存活 Pawn (无玩家 + 无 AI) → 返回 NoData (跳过本 Tick). "
			     "【根因】业务上不可能 — 开局至少会有 Player / 关卡预放 AI. "
			     "【可能时序】MatchTimerTick 间隔 1s, 在 MatchStartDelay (默认 3s) Spawn 完成前触发. "
			     "【修复】检查 Spawn 链路是否异常崩溃, 或调用方是否在错误时机调用本函数."));
		return EHASResult::NoData;
	}

	// 业务判定: 有人类活着 → HasAliveHuman; 否则 NoAliveHuman
	const EHASResult Result = (AliveHumanCount > 0) ? EHASResult::HasAliveHuman : EHASResult::NoAliveHuman;

	UE_LOG(LogTemp, Display,
		TEXT("[RoomGameState] HasAliveHumanOnField: 存活统计 Human=%d Mother=%d Other=%d → 返回 %s"),
		AliveHumanCount, AliveMotherCount, AliveOtherCount,
		Result == EHASResult::HasAliveHuman ? TEXT("HasAliveHuman (人类赢)") : TEXT("NoAliveHuman (母体赢)"));

	return Result;
}


/**
 * MulticastPlayZombieRoundSound (NetMulticast, 服务器 → 所有客户端)
 *
 * 业务规则 (用户 2026.08.06 明确):
 *   - 每小局结束, 服务器推 1 次 RPC 到所有客户端
 *   - 客户端 Implementation 回调 UGameHUDWidget 查 GameMode 配置的 USoundBase + 播放
 *
 * 大厂原则 — RPC 纯数据:
 *   - 仅传 RoundWinner (枚举), 不传 USoundBase* (UE 5.6 UObject 跨网络限制)
 *   - 客户端 Implementation 内部查 GameMode 配置
 *
 * 大厂原则 — 镜像 MulticastEnterSettlement:
 *   - 服务器自身不会触发 _Implementation, 手动 Broadcast 以同步房主本地 UI
 *
 * 大厂原则 — 零兜底:
 *   - _Implementation 内 USoundBase 为空 → Log Error + 跳过 (不让音效缺失卡住业务)
 *   - 客户端不查 GameMode → Log Error, 强制修复 BP 资产配置
 *
 * 调用方:
 *   - URoomLifecycleSubsystem::FinishZombieRound (本小局结束唯一入口)
 */
void ARoomGameState::MulticastPlayZombieRoundSound_Implementation(EZombieRoundWinner InRoundWinner)
{
	UE_LOG(LogTemp, Log,
		TEXT("[RoomGameState] MulticastPlayZombieRoundSound_Implementation: 客户端/服务器收到音效 RPC, InRoundWinner=%d"),
		static_cast<int32>(InRoundWinner));

	// UE 5.6 行为: NetMulticast Reliable 在 Listen Server 上服务器自身也会触发 _Implementation
	//   - 服务器调 MulticastPlayZombieRoundSound → 所有连接客户端 + 服务器自身都跑 _Implementation
	//   - 因此不需要单独"服务器手动 Broadcast",与 MulticastEnterSettlement 镜像
	HandleZombieRoundSoundReceived(InRoundWinner);
}


/**
 * HandleZombieRoundSoundReceived (服务器调用 + 客户端 _Implementation 调用)
 *
 * 职责拆分:
 *   - 接收 RoundWinner, Broadcast OnZombieRoundSoundReceived
 *   - UGameHUDWidget 订阅后, 查 GameMode USoundBase + 播放
 *
 * 大厂原则 — 单一职责:
 *   - 不在本函数内查 GameMode (避免耦合, GameMode 查表在 UGameHUDWidget 完成)
 *   - 仅 Broadcast, 音效查表/播放由 UI 层负责
 */
USoundBase* ARoomGameState::GetZombieRoundEndSound(EZombieRoundWinner InRoundWinner) const
{
	switch (InRoundWinner)
	{
	case EZombieRoundWinner::Human:
		return CachedZombieHumanWinSound;
	case EZombieRoundWinner::Mother:
		return CachedZombieMotherWinSound;
	default:
		UE_LOG(LogTemp, Error,
			TEXT("[RoomGameState] GetZombieRoundEndSound: RoundWinner=%d 无效, 返回 nullptr."),
			static_cast<int32>(InRoundWinner));
		return nullptr;
	}
}

void ARoomGameState::CacheZombieRoundSounds(USoundBase* InHumanSound, USoundBase* InMotherSound)
{
	if (!HasAuthority())
	{
		UE_LOG(LogTemp, Error,
			TEXT("[RoomGameState] CacheZombieRoundSounds: 客户端调用非法, HasAuthority=false. 仅服务器可缓存音效."));
		return;
	}

	CachedZombieHumanWinSound = InHumanSound;
	CachedZombieMotherWinSound = InMotherSound;

	UE_LOG(LogTemp, Log,
		TEXT("[RoomGameState] CacheZombieRoundSounds: 已缓存音效资产. HumanSound=%s, MotherSound=%s."),
		InHumanSound ? *InHumanSound->GetName() : TEXT("nullptr"),
		InMotherSound ? *InMotherSound->GetName() : TEXT("nullptr"));
}

void ARoomGameState::HandleZombieRoundSoundReceived(EZombieRoundWinner InRoundWinner)
{
	UE_LOG(LogTemp, Log,
		TEXT("[RoomGameState] HandleZombieRoundSoundReceived: Broadcast OnZombieRoundSoundReceived, InRoundWinner=%d"),
		static_cast<int32>(InRoundWinner));

	OnZombieRoundSoundReceived.Broadcast(InRoundWinner);
}
