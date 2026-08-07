// 版权声明：在项目设置的描述页面填写您的版权信息。

// ==========================================
// 头文件包含区
// ==========================================
// 引入本类头文件
#include "Systems/RoomGameState.h"

#include "Systems/RoomGameMode.h"  // 【v201.13】ARoomGameMode::TotalRounds

// 引入 Net/UnrealNetwork.h（DOREPLIFETIME 宏的来源）
#include "Net/UnrealNetwork.h"

// 【v210.3 大厂架构修复】TSoftObjectPtr<USoundBase> 需要完整类型用于 LoadSynchronous
#include "Sound/SoundBase.h"

// 【P0】OnRep_HostPlayerName 内部转发给 URoomService 事件总线
#include "Services/RoomService.h"
// 【v215 大厂架构新增】URoomStateService — 结算状态/队伍击杀变更时通知 View
#include "Services/RoomStateService.h"

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

// 【v217 大厂架构新增】服务器推 Client_ReceiveSettlementSnapshot RPC 需要 ARoomPlayerController 完整类型
#include "Systems/RoomPlayerController.h"

// 【v216 大厂架构新增】结算快照跨地图持久化
#include "Systems/Settlement/SettlementSnapshotSubsystem.h"
// 【v216 大厂架构新增】游戏流状态机 (RequestStateOnNextLoad)
#include "Systems/GameFlowSubsystem.h"
// 【v216 大厂架构新增】跨地图关卡加载
#include "Kismet/GameplayStatics.h"
// 【v216】ULocalPlayer (遍历本地玩家准备快照, 多端分屏场景)
// (LocalPlayer 通过 GameFramework/PlayerController 已传递引用, 也许不需要单独 include)


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

	// 【v209 大厂架构新增】结算状态复制
	DOREPLIFETIME(ARoomGameState, bInSettlement);

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

	// 【v210.4 大厂架构重构 — 删除 CachedZombieHumanWinSound/MotherWinSound 的 DOREPLIFETIME】
	//   旧 v210.2 / v210.3 已废弃, 改走 RPC FSoftObjectPath 跨网络传音效路径

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
 * OnRep_bInSettlement
 *
 * 【v209 大厂架构新增】结算状态复制回调
 *
 * 业务规则 (用户 2026.08.07 明确):
 *   - 整局游戏完全结束，进入结算页面时，锁定所有玩家和 AI 移动
 *   - 结算期间不能走动，但结算页面内容正常显示
 *
 * 大厂原则 — 零兜底:
 *   - 结算状态必须显式设置，不允许静默跳过
 *   - BaseCharacter::Move 读取此字段决定是否允许移动
 */
void ARoomGameState::OnRep_bInSettlement()
{
	UE_LOG(LogTemp, Log,
		TEXT("[RoomGameState] OnRep_bInSettlement: 结算状态同步! bInSettlement=%d"),
		bInSettlement ? 1 : 0);

	OnSettlementStateChanged.Broadcast(bInSettlement);

	// 【v215 大厂架构新增】通知 RoomStateService — 结算状态变更时 ScoreboardWidget 可能要冻结快照
	if (URoomStateService* StateSvc = URoomStateService::Get(this))
	{
		StateSvc->ForwardPlayerSnapshotsChanged();
	}
}

/**
 * SetSettlementState
 *
 * 【v209 大厂架构新增】服务器专用：设置结算状态
 *
 * 业务规则 (用户 2026.08.07 明确):
 *   - MulticastEnterSettlement 时调用，设置 bInSettlement=true
 *   - 返回大厅时调用，设置 bInSettlement=false
 *
 * 大厂原则 — 单一真理源:
 *   - 只在结算入口处设置，不允许其他入口修改
 */
/**
 * 【v212 大厂架构 P0 修复】SetSettlementState 必须显式 Broadcast
 *
 * 业务规则 (用户 2026.08.07 明确):
 *   - 服务器设结算状态后, RoomPlayerController 必须收到 OnSettlementStateChanged 回调
 *     → 才能切 InputMode = UIOnly + bShowMouseCursor = true (结算页面可点按钮)
 *
 * 大厂原则 - 单一真理源 + 0 兜底:
 *   - 服务器自己写 bInSettlement 后, 不会触发 OnRep (OnRep 只在 Client 收到 Replicated 数据时触发)
 *   - Listen Server (NetMode=ListenServer) 端的 PC 也是 Server, 因此也收不到自己的 OnRep
 *   - 解决: 服务器自己写字段后, 立刻 Broadcast 一次 (0 兜底, 不允许"等 Replicated 后 OnRep 触发")
 *   - Client 端: 通过 OnRep_bInSettlement 自动 Broadcast (与服务器对称)
 *   - 严禁: 把 Broadcast 放在 OnRep + 服务器路径, 然后等 Replicated — 这是时序兜底
 *
 * 调用方:
 *   - ARoomGameState::MulticastEnterSettlement_Implementation (服务器入口)
 *   - ARoomPlayerController::ExecuteLeaveRoom (服务器出口, SetSettlementState(false))
 */
void ARoomGameState::SetSettlementState(bool bInSettlementState)
{
	if (!HasAuthority())
	{
		UE_LOG(LogTemp, Error,
			TEXT("[RoomGameState] SetSettlementState: 客户端调用非法, HasAuthority=false."));
		return;
	}

	const bool bChanged = (bInSettlement != bInSettlementState);
	bInSettlement = bInSettlementState;

	UE_LOG(LogTemp, Log,
		TEXT("[RoomGameState] SetSettlementState: 设置结算状态为 %d (Changed=%d)"),
		bInSettlementState ? 1 : 0, bChanged ? 1 : 0);

	// 【v212 P0 修复】服务器写字段后, 立刻 Broadcast (0 兜底 - 不依赖 OnRep)
	//   - Listen Server / Standalone: 这里是唯一 Broadcast 时机
	//   - Client: 走 OnRep_bInSettlement 的 Broadcast, 与这里对称
	if (bChanged)
	{
		OnSettlementStateChanged.Broadcast(bInSettlement);
	}
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

	// 【v215 大厂架构新增】通知 RoomStateService 转发 — 队伍击杀变更 ScoreboardWidget 应刷新
	if (URoomStateService* StateSvc = URoomStateService::Get(this))
	{
		StateSvc->ForwardPlayerSnapshotsChanged();
	}
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

	// 【v215 大厂架构新增】通知 RoomStateService 转发 (服务器自身在 ListenServer 模式下也需要)
	if (URoomStateService* StateSvc = URoomStateService::Get(this))
	{
		StateSvc->ForwardPlayerSnapshotsChanged();
	}
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
	//   - 2026.08.08: 调回 3s
	ScheduleFinalSettlement(3.0f);
}


/**
 * BroadcastFinalSettlement
 *
 * 延迟 3 秒后触发的最终结算广播
 * 使用 NetMulticast 替代原 HasAuthority + Broadcast 方案
 * 解决 ListenServer 中纯客户端进程 HasAuthority() 返回 false 的问题
 *
 * 【v216.2 大厂架构】服务器侧冗余保险:
 *   - 服务器侧再次主动预约 SettlementPage (冗余保险)
 *   - 即使 MulticastEnterSettlement 因任何原因没在服务器执行, 这里仍能兜住
 *   - 跨端冗余 = 大厂铁律 — 意图不依赖单一入口
 */
void ARoomGameState::BroadcastFinalSettlement()
{
	// 【网络架构修复】: 使用 NetMulticast 替代原有的 HasAuthority + Broadcast 方案
	// 原问题: 在 Listen Server 中，纯客户端进程的 HasAuthority() 返回 false，导致 OnShowFinalSettlement 从未广播给房主以外的玩家
	// 解决方案: NetMulticast RPC 在服务器端调用时，引擎自动将函数调用复制到所有连接的客户端
	MulticastShowFinalSettlement(AttackerWins, DefenderWins);

	// 【v216.2 大厂架构重构 — OpenLevel 延迟 3 秒 + 服务器主动预约 SettlementPage】
	// OpenLevel 必须在 HasAuthority 下执行 (只有服务器能切图)
	// ScheduleFinalSettlement(3.0f) 定时器触发此函数时, 结算面板已显示了 3 秒, 玩家已看完
	// 切图让玩家进入 L_Login 的结算页面
	if (HasAuthority())
	{
		// ==========================================
		// 【v216.2 大厂架构修复】服务器主动再次确保 SettlementPage 预约生效
		// ==========================================
		// 为什么要在这里再调一次预约:
		//   - 服务器侧的预约是在 MulticastEnterSettlement (t=0) 设置的 (v216.2 已修复跨端预约)
		//   - 但跨端预约在 Multicast_Implementation 里是"任意端"执行, 严格来说服务器这边也应该显式执行
		//   - 这里作为防御性检查: 即使 MulticastEnterSettlement 因任何原因没在服务器上执行,
		//     BroadcastFinalSettlement 时仍然能保证服务器侧预约到 SettlementPage
		//   - 跨端冗余 = 大厂铁律 — 意图不依赖单一入口
		// ==========================================
		UGameInstance* GI = GetGameInstance();
		if (!GI)
		{
			UE_LOG(LogTemp, Error,
				TEXT("[RoomGameState] BroadcastFinalSettlement: GetGameInstance() 失败, 无法预约 SettlementPage, "
				     "切图后玩家会进错页面. "
				     "【v216.2 零兜底】修复: 检查 GameInstance 生命周期."));
		}
		else
		{
			UGameFlowSubsystem* FlowSubsystem = GI->GetSubsystem<UGameFlowSubsystem>();
			if (!FlowSubsystem)
			{
				UE_LOG(LogTemp, Error,
					TEXT("[RoomGameState] BroadcastFinalSettlement: GameFlowSubsystem 不可用, "
					     "无法预约 SettlementPage, 切图后玩家可能进错页面. "
					     "【v216.2 零兜底】修复: 检查 GameInstanceSubsystem 注册."));
			}
			else
			{
				// 服务器侧再次预约 — 冗余保险 (即使 MulticastEnterSettlement 漏执行, 这里也能兜住)
				FlowSubsystem->RequestStateOnNextLoad(EMatchState::SettlementPage);
				UE_LOG(LogTemp, Log,
					TEXT("[RoomGameState] 【v216.2】BroadcastFinalSettlement: 服务器侧再次预约 SettlementPage (冗余保险)."));
			}
		}

		UWorld* World = GetWorld();
		if (!World)
		{
			UE_LOG(LogTemp, Error,
				TEXT("[RoomGameState] BroadcastFinalSettlement: GetWorld() 失败, 无法 OpenLevel(L_Login). "
				     "【v216.1 零兜底】修复: 检查 GameState 生命周期."));
			return;
		}

		const FName TargetLevel = FName(TEXT("L_Login"));
		UE_LOG(LogTemp, Log,
			TEXT("[RoomGameState] 【v216.1】BroadcastFinalSettlement 服务器切图: OpenLevel(%s, ?offline). "
			     "玩家已在结算面板停留 3 秒, 现在切到 L_Login 显示完整结算."),
			*TargetLevel.ToString());
		UGameplayStatics::OpenLevel(World, TargetLevel, true, TEXT("?offline"));
	}
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
 *
 * 【v216 大厂架构重构】: 跨地图切图后, 此 RPC 3 秒后到达时旧 GS 可能已销毁
 *   - 旧 (v215): 3 秒后更新 UScoreboardWidget 内存冻结的胜负局数
 *   - 新 (v216): AttackerWins/DefenderWins 已在 MulticastEnterSettlement 时直接快照到 USettlementSnapshotSubsystem
 *     → 此 RPC 主要作用: 兼容旧调用方 (BroadcastFinalSettlement / ScheduleFinalSettlement 仍触发)
 *     → 副作用: 如果旧 GS 仍然存活 (切图未发生), 增量更新 Subsystem 快照 (幂等, 允许覆盖)
 *     → 0 兜底: GS 已销毁 → RPC 收不到, 不报错 (新地图上无 GS, RPC 自然失败)
 */
void ARoomGameState::MulticastShowFinalSettlement_Implementation(int32 InAttackerWins, int32 InDefenderWins)
{
	UE_LOG(LogTemp, Log, TEXT("[RoomGameState] MulticastShowFinalSettlement: 攻方胜%d局, 守方胜%d局"), InAttackerWins, InDefenderWins);

	// 广播最终结算事件，附带双方的总胜局数（所有客户端均会执行此行）
	OnShowFinalSettlement.Broadcast(InAttackerWins, InDefenderWins);

	// 【v216 大厂架构新增】幂等增量更新快照 (允许覆盖 MulticastEnterSettlement 时的初值)
	// 大厂原则 — 0 兜底:
	//   - Subsystem 不可用 → Log Warning (不影响 OnShowFinalSettlement 广播)
	//   - Subsystem 内已有快照 → UpdateSnapshotWins 覆盖
	//   - Subsystem 内无快照 (切图后旧 Subsystem 也销毁) → UpdateSnapshotWins Log Error + return
	if (USettlementSnapshotSubsystem* SnapshotSub = USettlementSnapshotSubsystem::Get(this))
	{
		SnapshotSub->UpdateSnapshotWins(InAttackerWins, InDefenderWins);
	}
}


/**
 * MulticastEnterSettlement_Implementation
 *
 * NetMulticast 实现: 广播进入结算事件
 * 所有客户端均会执行此函数，触发 OnEnterSettlement 委托
 * 让 UI 显示 Text_GameOver + 对应胜负文本
 *
 * 【v200.1 大厂架构重构】: 增加 RoundWinner 参数，用于显示"人类胜利"或"幽灵胜利"
 * 【v216 大厂架构重构】跨地图显示结算页 — 离开房间关卡, 跳到 L_Login 上独立显示
 *   - 旧 (v209-v215): 在房间关卡内嵌 settlement UI, 玩家不能离开房间
 *   - 新 (v216): 写快照 + RequestStateOnNextLoad(SettlementPage) (t=0 预约)
 *   - 【v216.1 重构】OpenLevel 移到 BroadcastFinalSettlement 定时器 (t=3s 切图)
 *     → 玩家有 3 秒在房间关卡看结算面板, 然后才切图到 L_Login
 *   - 【v216.2 关键修复】预约代码从 HasAuthority 块内提到 HasAuthority 块外
 *     → 旧 bug: 客户端 Multicast_Implementation 跑 HasAuthority=false → 跳过预约 → 客户端 PostLoadMapWithWorld 找不到预约 → 进错页面
 *     → 新架构: 所有端 (服务器 + 所有客户端) 都执行预约 → 每个端的 GameFlowSubsystem 都持有 SettlementPage → 都正确显示结算面板
 *   — 刀战复用 EZombieRoundWinner: Attacker胜=Mother, Defender胜=Human
 */
void ARoomGameState::MulticastEnterSettlement_Implementation(int32 InAttackerKills, int32 InDefenderKills, EZombieRoundWinner InRoundWinner)
{
	UE_LOG(LogTemp, Log, TEXT("[RoomGameState] MulticastEnterSettlement: 攻方=%d, 守方=%d, RoundWinner=%d"),
		InAttackerKills, InDefenderKills, static_cast<int32>(InRoundWinner));

	// 【v209 大厂架构新增】进入结算时锁定移动
	// 大厂原则 — 单一真理源: 服务器在进入结算时设置 bInSettlement=true
	if (HasAuthority())
	{
		SetSettlementState(true);
	}

	// 广播进入结算事件，让所有客户端显示 Text_GameOver + 对应胜负文本
	OnEnterSettlement.Broadcast(InAttackerKills, InDefenderKills, InRoundWinner);

	// ==========================================
	// 【v217 大厂架构重构】跨地图结算快照写入 + OpenLevel + RequestStateOnNextLoad
	// ==========================================
	// 大厂原则 — 单一真理源 (v217) — 快照只在服务器端构建:
	//   - 旧 (v216.x): MulticastEnterSettlement 在服务器 + 每个客户端都执行,各自从自己 URoomStateService 拉数据
	//     → 客户端 AIC.CachedFactionTag 是空 (非 Replicated) → 客户端 Snapshot 永远不含 AI
	//     → 用户报告: "客户端结算页面只显示玩家信息,不显示房间内AI信息"
	//   - 新 (v217): 客户端只走"广播 OnEnterSettlement + 跨端预约 SettlementPage"两条
	//     → 服务器走"拉数据 + 写本地 Snapshot + 推 Client_ReceiveSettlementSnapshot RPC 给每个客户端"
	//     → 客户端等待 RPC 到达 → 收到后 WriteSnapshot 到本地 Subsystem
	//     → AI 数据从服务器端推过来, 客户端从不"自拉" (避免 CachedFactionTag 非 Replicated 导致的客户端空数据)
	//
	// 大厂原则 — 切图 vs 写快照的时序:
	//   - 写快照 → 必须先 (因为 OpenLevel 后, 当前 GS/Service 立即销毁, 拉不到数据)
	//   - OpenLevel → 必须在写快照之后 (否则快照内容为空)
	//   - RequestStateOnNextLoad → 写快照之后 (新地图加载时 GameFlowSubsystem 才会切到 SettlementPage)
	//
	// 大厂原则 — 0 兜底:
	//   - 服务器拉 Service 失败 → Log Error + 仍写空 Snapshot (避免后续 widget 渲染崩溃)
	//   - 服务器推 RPC 失败 → Log Error + 客户端没有 SnapshotSubsystem,ConsumeSnapshot 失败 → 走结算页 fallback (UI 不显示)
	//   - 客户端 SnapshotSubsystem 不可用 → Log Error + return (RPC 传到但写不进本地)
	// ==========================================
	if (HasAuthority())
	{
		// 1) 构造 FFinalSettlementSnapshot — 服务端权威数据
		FFinalSettlementSnapshot Snapshot;
		Snapshot.MatchMode = CurrentMatchMode;
		Snapshot.AttackerKills = InAttackerKills;
		Snapshot.DefenderKills = InDefenderKills;
		Snapshot.RoundWinner = InRoundWinner;
		// 【v216 大厂架构重构】胜负局数在 MulticastEnterSettlement 时直接读 GS 当前值
		//   - 旧 (v215): 3 秒后 MulticastShowFinalSettlement 增量更新
		//   - 新 (v216): 立刻写入 (因为 OpenLevel 切图后旧 GS 销毁, RPC 不到新地图)
		//   - 大厂原则 — 单一真理源: AttackerWins/DefenderWins 是 GameState 权威值
		Snapshot.AttackerWins = AttackerWins;
		Snapshot.DefenderWins = DefenderWins;
		Snapshot.WriteTimestamp = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
		Snapshot.bIsValid = false;     // 填充完数据后再置 true

		// 2) 从 URoomStateService 拉取阵营快照 → 转 FFinalSettlementSnapshot.AttackerEntries/DefenderEntries
		// 大厂原则 — 0 兜底: Service 不可用 → Log Error + 仍写一个空 snapshot (避免后续 widget 渲染崩溃)
		bool bServiceValid = false;
		if (UGameInstance* GI = GetGameInstance())
		{
			if (URoomStateService* StateService = GI->GetSubsystem<URoomStateService>())
			{
				bServiceValid = true;

				// 攻方 (Offense) 真人 + AI 快照
				const TArray<FPlayerSnapshot> AttackerSnaps = StateService->GetFactionSnapshotsWithAI(FFactionTags::Offense());
				Snapshot.AttackerEntries.Reserve(AttackerSnaps.Num());
				for (const FPlayerSnapshot& PS : AttackerSnaps)
				{
					FFactionSnapshotEntry Entry;
					Entry.DisplayName = PS.PlayerName;
					Entry.bIsAI = PS.bIsAI;
					Entry.Kills = PS.Kills;
					Entry.Deaths = PS.Deaths;
					Entry.Assists = PS.Assists;
					Entry.Score = PS.Score;
					Entry.bIsAttacker = true;
					Entry.FactionTagName = PS.FactionTag.ToString();
					Snapshot.AttackerEntries.Add(Entry);
				}

				// 守方 (Defense) 真人 + AI 快照
				const TArray<FPlayerSnapshot> DefenderSnaps = StateService->GetFactionSnapshotsWithAI(FFactionTags::Defense());
				Snapshot.DefenderEntries.Reserve(DefenderSnaps.Num());
				for (const FPlayerSnapshot& PS : DefenderSnaps)
				{
					FFactionSnapshotEntry Entry;
					Entry.DisplayName = PS.PlayerName;
					Entry.bIsAI = PS.bIsAI;
					Entry.Kills = PS.Kills;
					Entry.Deaths = PS.Deaths;
					Entry.Assists = PS.Assists;
					Entry.Score = PS.Score;
					Entry.bIsAttacker = false;
					Entry.FactionTagName = PS.FactionTag.ToString();
					Snapshot.DefenderEntries.Add(Entry);
				}
			}
		}

		if (!bServiceValid)
		{
			UE_LOG(LogTemp, Error,
				TEXT("[RoomGameState] MulticastEnterSettlement: URoomStateService 不可用, 快照只含基础字段 (MatchMode/Kills/RoundWinner), 玩家/AI 列表为空. "
				     "【v217 零兜底】修复: 检查 GameInstance 是否正确初始化 URoomStateService."));
		}
		else
		{
			UE_LOG(LogTemp, Log,
				TEXT("[RoomGameState] 【v217】MulticastEnterSettlement: 服务端构建快照完成. AttackerEntries=%d, DefenderEntries=%d, "
				     "AttackerAI=%d, DefenderAI=%d."),
				Snapshot.AttackerEntries.Num(), Snapshot.DefenderEntries.Num(),
				Snapshot.AttackerEntries.FilterByPredicate([](const FFactionSnapshotEntry& E){ return E.bIsAI; }).Num(),
				Snapshot.DefenderEntries.FilterByPredicate([](const FFactionSnapshotEntry& E){ return E.bIsAI; }).Num());
		}

		// 3) 写快照到 Subsystem (跨地图持久 — 切图后 UScoreboardWidget 仍能拉取)
		Snapshot.bIsValid = true;
		if (USettlementSnapshotSubsystem* SnapshotSub = USettlementSnapshotSubsystem::Get(this))
		{
			SnapshotSub->WriteSnapshot(Snapshot);
		}
		else
		{
			UE_LOG(LogTemp, Error,
				TEXT("[RoomGameState] MulticastEnterSettlement: USettlementSnapshotSubsystem 不可用, 跨地图快照丢失. "
				     "【v217 零兜底】修复: 检查 GameInstanceSubsystem 是否注册成功."));
		}

		// 4) 推 Client_ReceiveSettlementSnapshot 给每个客户端
		// 大厂原则 — 跨端大对象推 RPC (UE 5.x 标准模式):
		//   - NetMulticast 不适合带大对象 (Snapshot 含 8+ 玩家 + AI)
		//   - Client RPC 一对一点对点, Reliable 保证送达
		//   - 0 兜底: 遍历 PlayerArray 失败 → Log Error (没客户端 = 不需要推)
		// 大厂原则 — ServerSelf:
		//   - 服务器自己也是 Client (Listen Server), 但本地的 Snapshot 已在步骤 3 写好
		//   - 不需要给 ServerSelf 推 RPC (ReceiverOwningPlayerController=null 调用不到自己)
		//   - 流程: Server 本地走步骤 3 路径 → Client 走 Client_ReceiveSettlementSnapshot 路径
		int32 NumPushed = 0;
		UWorld* World = GetWorld();
		if (!World)
		{
			UE_LOG(LogTemp, Error,
				TEXT("[RoomGameState] MulticastEnterSettlement: GetWorld() 失败, 无法遍历 PlayerController 推 Client RPC. "
				     "【v217 零兜底】修复: 检查 GameState 生命周期."));
		}
		else
		{
			for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
			{
				ARoomPlayerController* RC = Cast<ARoomPlayerController>(It->Get());
				if (!RC)
				{
					// 0 兜底: 这是 AI 控制器或 ALoginPlayerController → 跳过
					continue;
				}

				// 大厂原则 — ReceiverOwningPlayerController:
				//   - 这个 RPC 是 ARoomGameState 上的, 但 ReceiverOwningPlayerController 由 RC 决定
				//   - 0 兜底: RC 已是 Server (Listen Server 上 RC 是个"特殊 PC") → 跳过 RPC (Server 自己 Snapshot 已在步骤 3 写好)
				if (RC->IsLocalController())
				{
					// Server 本地 PC → 不需要 RPC
					continue;
				}

				// Client PC → 推 Client RPC
				RC->Client_ReceiveSettlementSnapshot(Snapshot);
				NumPushed++;
			}
		}

		UE_LOG(LogTemp, Log,
			TEXT("[RoomGameState] 【v217】MulticastEnterSettlement: 服务端推 Client_ReceiveSettlementSnapshot 给 %d 个客户端. "
			     "客户端 AIC.CachedFactionTag 修补信号: client 不会再自拉 Service."),
			NumPushed);
	}
	// else: 客户端 — 等待 Client_ReceiveSettlementSnapshot RPC 到达
	// 大厂原则 — 客户端不做 Snapshot 自拉:
	//   - 客户端的 URooStateService 拉不到 AI 数据 (AIC.CachedFactionTag 非 Replicated)
	//   - 客户端只走 Broadcast OnEnterSettlement + 跨端预约 SettlementPage (见下)
	//   - Snapshot 数据完全由 Server 推 Client_ReceiveSettlementSnapshot 提供


	// ==========================================
	// 【v216.2 大厂架构修复】跨端统一预约 SettlementPage 状态
	// ==========================================
	// 大厂原则 — 意图必须在所有端持久化:
	//   - 旧 (v216): RequestStateOnNextLoad 在 HasAuthority 块内 → 客户端 Multicast_Implementation 跑 HasAuthority=false → 跳过预约
	//     → 服务器 PostLoadMapWithWorld 消费预约 ✓ → 服务器切到 SettlementPage ✓ (房主正常显示)
	//     → 客户端 PostLoadMapWithWorld 看到 bHasPendingStateOnNextLoad=false → 走"同步当前状态"路径 → 显示战斗 HUD ❌
	//   - 新 (v216.2): 预约代码提到 HasAuthority 块外 → 所有端 (服务器 + 所有客户端) 都执行
	//     → 每个端的 GameFlowSubsystem 都持有 SettlementPage 预约 → 每个端 OpenLevel 后 (服务器主动 / 客户端被动) → 都消费预约 → 都切到 SettlementPage ✓
	//
	// 为什么 OpenLevel 仍要在 HasAuthority 块内 (下一段):
	//   - OpenLevel 只有 Authority 能调, 客户端是被动跟随服务器
	//   - 服务器 OpenLevel 会通过 NetDriver 关闭所有客户端, 客户端 NetDriver 关闭后自动跳同一张图 (?closed)
	//   - 客户端的 PostLoadMapWithWorld 也会触发, 消费客户端侧的预约 → 显示 SettlementPage
	// ==========================================
	{
		UGameInstance* GI = GetGameInstance();
		if (!GI)
		{
			UE_LOG(LogTemp, Error,
				TEXT("[RoomGameState] MulticastEnterSettlement: GetGameInstance() 失败, 无法预约 SettlementPage 状态, "
				     "切图后玩家会进错页面. "
				     "【v216.2 零兜底】修复: 检查 GameInstance 生命周期."));
		}
		else
		{
			UGameFlowSubsystem* FlowSubsystem = GI->GetSubsystem<UGameFlowSubsystem>();
			if (!FlowSubsystem)
			{
				UE_LOG(LogTemp, Error,
					TEXT("[RoomGameState] MulticastEnterSettlement: GameFlowSubsystem 不可用 (端=%s), "
					     "新地图加载后无法自动切到 SettlementPage 状态, 玩家可能进错页面. "
					     "【v216.2 零兜底】修复: 检查 GameInstanceSubsystem 注册."),
					HasAuthority() ? TEXT("Server") : TEXT("Client"));
			}
			else
			{
				// 所有端都预约 — 意图必须跨端持久化 (大厂原则: 业务意图放跨地图持久的 Subsystem)
				FlowSubsystem->RequestStateOnNextLoad(EMatchState::SettlementPage);
				UE_LOG(LogTemp, Log,
					TEXT("[RoomGameState] 【v216.2】MulticastEnterSettlement: 端=%s 预约 SettlementPage 状态 (t=0), "
					     "切图完成后会自动切到 SettlementPage → UI 显示结算面板."),
					HasAuthority() ? TEXT("Server") : TEXT("Client"));
			}
		}
	}

	// ==========================================
	// 【v216.1 大厂架构】服务器侧 OpenLevel 延迟到 3s 后 BroadcastFinalSettlement
	// ==========================================
	// 大厂原则 — 切图预约顺序:
	//   - RequestStateOnNextLoad(SettlementPage) → t=0 预约 (已在上面跨端执行 ✓)
	//   - OpenLevel(L_Login) → t=3s 触发 (由 BroadcastFinalSettlement 调用)
	//   - 时序: t=0 预约状态 → t=3s 切图 → 切图完成 → HandlePostLoadMapWithWorld → 自动切到 SettlementPage
	// ==========================================
	if (HasAuthority())
	{
		UE_LOG(LogTemp, Log,
			TEXT("[RoomGameState] 【v216.2】MulticastEnterSettlement: 服务器侧 OpenLevel 延迟到 3s 后 BroadcastFinalSettlement. "
			     "玩家有 3 秒看结算面板."));
	}
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
 * 【v210.4 大厂架构重构 — RPC 传 FSoftObjectPath 替代 GS 缓存】
 *
 * 业务规则 (用户 2026.08.06 明确):
 *   - 每小局结束, 服务器推 1 次 RPC 到所有客户端
 *   - 服务器从 GM 配置 (单一真理源) 拿 USoundBase*, 转 FSoftObjectPath
 *   - 客户端 Implementation 调 LoadSynchronous() 加载 Sound + 广播
 *
 * 大厂原则 — RPC 纯数据 (UE 5.6 UObject 指针跨网络限制):
 *   - 不传 USoundBase* (UE 5.6 GC 误删 + Replicated 裸指针不可靠)
 *   - 传 FSoftObjectPath (UE 5.6 标准 Replicate 资产路径方式, GC 安全, 跨网络稳定)
 *
 * 大厂原则 — 单一真理源:
 *   - 音效资产只在 GM 配 (策划唯一配置点), GS 不复制不缓存
 *   - v210.2 引入的 CachedZombieHumanWinSound + CacheZombieRoundSounds 已废弃
 *   - v210.3 引入的 TSoftObjectPtr Replicated 已废弃
 *
 * 大厂原则 — 零兜底:
 *   - SoundPath 为空 → Log Error, 强制修复 GM BP 配置
 *
 * 调用方:
 *   - URoomLifecycleSubsystem::FinishZombieRound (本小局结束唯一入口)
 */
void ARoomGameState::MulticastPlayZombieRoundSound_Implementation(EZombieRoundWinner InRoundWinner, const FSoftObjectPath& InSoundPath)
{
	UE_LOG(LogTemp, Log,
		TEXT("[RoomGameState] 【v210.4】MulticastPlayZombieRoundSound_Implementation: 客户端/服务器收到音效 RPC. "
		     "InRoundWinner=%d, InSoundPath=%s"),
		static_cast<int32>(InRoundWinner),
		*InSoundPath.ToString());

	// UE 5.6 行为: NetMulticast Reliable 在 Listen Server 上服务器自身也会触发 _Implementation
	//   - 服务器调 MulticastPlayZombieRoundSound → 所有连接客户端 + 服务器自身都跑 _Implementation
	//   - 因此不需要单独"服务器手动 Broadcast"
	PlayZombieRoundSoundFromPath_Implementation(InRoundWinner, InSoundPath);
}


/**
 * PlayZombieRoundSoundFromPath_Implementation (RPC 内部实现)
 *
 * 【v210.4 大厂架构重构】从 FSoftObjectPath 加载 USoundBase + 广播给 UI
 *
 * 大厂原则 — 零兜底:
 *   - SoundPath 为空 → Log Error, 不静默跳过
 *   - LoadSynchronous 失败 → Log Error
 *
 * 调用方:
 *   - MulticastPlayZombieRoundSound_Implementation (RPC 触发)
 */
void ARoomGameState::PlayZombieRoundSoundFromPath_Implementation(EZombieRoundWinner InRoundWinner, const FSoftObjectPath& InSoundPath)
{
	// 【v210.4 零兜底】SoundPath 必须有效
	if (InSoundPath.IsNull())
	{
		UE_LOG(LogTemp, Error,
			TEXT("[RoomGameState] 【v210.4】PlayZombieRoundSoundFromPath_Implementation: InSoundPath 为空. "
			     "【修复】检查 GM_RoomGameMode BP Class Defaults → MetalSlug|Match|ZombieRound → "
			     "ZombieHumanWinSound / ZombieMotherWinSound 字段. "
			     "【业务后果】本小局音效未播放, 业务不阻塞."));
		return;
	}

	// 【v210.4】同步加载音效资产
	USoundBase* LoadedSound = Cast<USoundBase>(InSoundPath.TryLoad());
	if (!LoadedSound)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[RoomGameState] 【v210.4】PlayZombieRoundSoundFromPath_Implementation: LoadSynchronous 失败. "
			     "InSoundPath=%s. 【修复】检查资产路径是否正确, 资产是否存在."),
			*InSoundPath.ToString());
		return;
	}

	UE_LOG(LogTemp, Log,
		TEXT("[RoomGameState] 【v210.4】PlayZombieRoundSoundFromPath_Implementation: 音效加载成功. "
		     "LoadedSound=%s, InRoundWinner=%d"),
		*LoadedSound->GetName(),
		static_cast<int32>(InRoundWinner));

	// 【v210.4】直接在本机播放音效 (服务器 + 所有客户端都执行)
	//   - UE 5.6 PlaySound2D 在无 PIE / 无 PlayerController 时也能跑 (有 World 即可)
	if (UWorld* World = GetWorld())
	{
		UGameplayStatics::PlaySound2D(World, LoadedSound);
	}

	// 【v210.4】同时广播给 UI (HUD 可以监听这个事件做额外效果, 比如震屏 / 全屏闪烁)
	HandleZombieRoundSoundReceived(InRoundWinner);
}


/**
 * HandleZombieRoundSoundReceived (RPC Implementation 内部调用)
 *
 * 职责拆分:
 *   - 接收 RoundWinner, Broadcast OnZombieRoundSoundReceived
 *   - UGameHUDWidget 订阅后可做额外 UI 反馈
 *
 * 大厂原则 — 单一职责:
 *   - 不在本函数内查 GameMode (避免耦合, 音效加载/播放已在 PlayZombieRoundSoundFromPath_Implementation 完成)
 */
void ARoomGameState::HandleZombieRoundSoundReceived(EZombieRoundWinner InRoundWinner)
{
	UE_LOG(LogTemp, Log,
		TEXT("[RoomGameState] HandleZombieRoundSoundReceived: Broadcast OnZombieRoundSoundReceived, InRoundWinner=%d"),
		static_cast<int32>(InRoundWinner));

	OnZombieRoundSoundReceived.Broadcast(InRoundWinner);
}


