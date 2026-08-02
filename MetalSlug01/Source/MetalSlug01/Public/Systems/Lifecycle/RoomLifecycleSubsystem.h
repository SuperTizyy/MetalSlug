// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

// ==========================================
// URoomLifecycleSubsystem — 比赛状态机子系统
//
// 【2026.07.11 v31 大厂架构重构】从 RoomGameMode 拆出
//
// 设计原则:
//   - 单一职责: 比赛生命周期 (Lobby → BattleInProgress → Round 流转)
//   - 不持有 Pawn/Player: 只协调, 不生成 (生成归 URoomSpawnSubsystem)
//   - 不管玩家/AI 入队: 只管状态机和计时器 (入队归 URoomMembershipSubsystem)
//
// 职责清单:
//   - PerformGameStart: 房间状态转 BattleInProgress + 开局倒计时 + 广播 OnBattleStarted
//   - StartMatchTimer / OnMatchTimerTick / HandleMatchTimeOut: 比赛计时
//   - StartNextZombieRound / HandleZombieRoundEnd: 生化模式回合流转
//   - 持有 bBattleStartedBroadcasted 幂等标志
//   - 持有 MatchStartTimerHandle / MatchTimerHandle
//
// 大厂原则 - 单一真理源:
//   - 比赛时长 (MeleeMatchDurationSeconds) 配置在 GameMode, 启动时 GameMode 注入 Subsystem
//   - 当前 RoomState 真理在 ARoomGameState (Replicated)
//   - 计时器句柄在这里, 但状态字段在 GameState
//
// 访问入口:
//   URoomLifecycleSubsystem* Lifecycle = URoomLifecycleSubsystem::Get(this);
// ==========================================

// UE 引擎基础
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"

// Room 共享枚举 (ERoomState / ERoomMatchMode)
#include "Data/Enums/RoomEnums.h"

// v108 — EMotherSelectionPolicy (母体变异目标选择策略)
// 业务可配 (GameMode.MotherSelectionPolicy → Subsystem.CachedMotherSelectionPolicy)
#include "Systems/AI/AIBehaviorTypes.h"

// 自动生成的反射头 — 必须放在所有 #include 之后, forward declaration 之前
// 大厂注意 (UE 5.6 UHT 严格模式): .generated.h 必须用裸文件名, 不能带目录前缀
//   UHT Parser 中: includeNameString 跟 GeneratedHeaderFileName 做字面 OrdinalIgnoreCase 比对
//   "Systems/Lifecycle/RoomLifecycleSubsystem.generated.h" 永远 != "RoomLifecycleSubsystem.generated.h"
#include "RoomLifecycleSubsystem.generated.h"

// ==========================================
// 前向声明 — 避免在本头中 include 完整定义
// ==========================================
class ARoomGameMode;
class ARoomPlayerController;
class ARoomGameState;

UCLASS()
class METALSLUG01_API URoomLifecycleSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	// ==========================================
	// 生命周期
	// ==========================================

	/**
	 * 标准 UE Subsystem 访问入口 (大厂原则)
	 * @param WorldContextObject 任何能拿到 World 的 UObject (通常传 this)
	 * @return 当前 World 的 LifecycleSubsystem 实例 (server-only, 客户端拿不到)
	 */
	static URoomLifecycleSubsystem* Get(const UObject* WorldContextObject);

	// UWorldSubsystem 接口 (server-only, 客户端不创建)
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

	// ==========================================
	// 比赛状态机 API (供 RoomGameMode 调用)
	// ==========================================

	/**
	 * @brief 执行开局指令 (权威服务器, C++ 调用 — 不暴露 BP)
	 *
	 * 流程:
	 *   1. 更新 CurrentRoomState = BattleInProgress
	 *   2. 开启比赛倒计时 (同步 MatchEndTime 到所有客户端)
	 *   3. 通知所有客户端切换 UI (Client_TransitToMatchState)
	 *   4. 延迟 MatchStartDelay 秒后触发 OnSpawnAllPlayersCallback (让 GameMode 调用 URoomSpawnSubsystem::SpawnAllPlayers)
	 *   5. 幂等广播 OnBattleStarted (AI 订阅后激活 BT)
	 *
	 * 大厂原则 - 单一入口: PerformGameStart 是开局的唯一权威方法
	 * 大厂原则 - 反射友好: 不暴露 FSimpleDelegate 给 UHT (DECLARE_DELEGATE 不被 UHT 识别),
	 *                  本方法走 C++ 调用路径, BP 入口保持在 GameMode 层
	 *
	 * @param InMatchStartDelay 开局延迟秒数 (来自 GameMode.MatchStartDelay)
	 * @param InMatchDurationSeconds 比赛时长秒数 (Melee 模式; Zombie 模式用 TotalRounds)
	 * @param OnSpawnAllPlayersDelegate 倒计时结束后触发 (让 GameMode 调用 URoomSpawnSubsystem::SpawnAllPlayers)
	 */
	void PerformGameStart(float InMatchStartDelay, float InMatchDurationSeconds,
		FSimpleDelegate OnSpawnAllPlayersDelegate);

	/**
	 * @brief 比赛时间耗尽 (由本 Subsystem 内部定时器调用)
	 * 刀战模式: 触发全局结算 (GameMode 决定)
	 * 生化模式: 进入 HandleZombieRoundEnd
	 */
	void HandleMatchTimeOut();

	/**
	 * @brief 启动比赛计时器 (1Hz tick, 持续到 MatchEndTime)
	 *
	 * 大厂原则 (v31.5):
	 *   - 启动点由 RoomGameMode::PerformGameStart 走 SpawnAllPlayers 后调用
	 *   - 启动前 GameMode 已通过 SetMatchEndTime 写入 GameState.MatchEndTime
	 *   - 启动后, 每秒 OnMatchTimerTick 检查剩余时间, 0 时自动 HandleMatchTimeOut
	 *
	 * 失败模式 (大厂显式化):
	 *   - GameState 为空 → Log Error + return (不静默启动无效 Timer)
	 *   - MatchEndTime <= 0 → Log Error + return
	 */
	void StartMatchTimer();

	/**
	 * @brief 比赛计时器每秒 tick (public 供 RoomGameMode::OnMatchTimerTick 委派调用)
	 *
	 * 大厂原则 (v31.5):
	 *   - GameMode 通过 URoomLifecycleSubsystem 间接代理
	 *   - 内部逻辑: 读 GameState.MatchRemainingSeconds, <=0 时 ClearTimer + HandleMatchTimeOut
	 */
	void OnMatchTimerTick();

	/**
	 * @brief 生化模式进入下一回合
	 * @return 是否还有下一回合 (false = 全部回合结束)
	 */
	UFUNCTION(BlueprintCallable, Category = "Room|Lifecycle")
	bool StartNextZombieRound();

	/**
	 * @brief 生化模式当前回合结束
	 * @return 是否还有下一回合
	 */
	UFUNCTION(BlueprintCallable, Category = "Room|Lifecycle")
	bool HandleZombieRoundEnd();

	// ==========================================
	// 【v92 大厂架构新增】生化模式母体变异倒计时调度
	// ==========================================
	//
	// 业务规则:
	//   生化模式每局开局, 玩家/AI 都是人类, 互相无敌
	//   8 秒倒计时结束才会"变异"为母体 (Phase 3 母体死亡广播)
	//
	// 大厂原则 — 集中调度:
	//   - 启动入口: PerformGameStart (首局) + StartNextZombieRound (后续局)
	//   - 重置入口: HandleZombieRoundEnd (本局结束) + ResetMotherMutationCountdown (兜底)
	//   - GameState 是数据源, Subsystem 是调度者, 不持有状态
	//
	// 反射友好 (UE 5.6 UHT):
	//   - 不暴露 FSimpleDelegate 给 UHT, 用普通 C++ 方法

	/**
	 * @brief 启动母体变异倒计时 (服务器内部调用)
	 *
	 * 大厂原则 — 镜像 MatchEndTime 写入流程:
	 *   - 调用 GameState->StartMotherMutationCountdown(Duration)
	 *   - GameState 写入 Replicated 字段, 客户端 OnRep 自动触发
	 *   - Widget 收到 OnMotherMutationChanged 后显示倒计时
	 *
	 * @param Duration 倒计时总秒数 (必须 > 0, 由 GameMode.MotherMutationDurationSeconds 注入)
	 */
	void StartMotherMutationCountdown();

	/**
	 * @brief 重置母体变异倒计时 (服务器内部调用, 关闭倒计时)
	 * 调用时机: 本局结束 / 切换模式 / GameMode 兜底重置
	 */
	void ResetMotherMutationCountdown();

	// ==========================================
	// 配置入口 (由 GameMode 在构造/初始化时注入)
	// ==========================================

	/**
	 * @brief 注入总局数 (来自 GameMode.TotalRounds)
	 *
	 * 大厂原则 — 单一真理源:
	 *   - GameMode.TotalRounds → GameState.TotalRounds (Replicated) → UI 订阅
	 *   - Subsystem 内部不再持有 ZombieTotalRounds 副本 (消除重复架构)
	 *   - Subsystem 在 HandleZombieRoundEnd / StartNextZombieRound 内部读 GameState.TotalRounds
	 */
	void SetTotalRounds(int32 InRounds);

	/**
	 * @brief 注入生化模式每局时长 (来自 GameMode.ZombieMatchDurationSeconds)
	 *
	 * 大厂原则 — 对称设计:
	 *   - MeleeMatchDurationSeconds 走 PerformGameStart 直接传 (GameMode → Subsystem)
	 *   - ZombieMatchDurationSeconds 走 SetZombieMatchDuration 注入 (GameMode → Subsystem)
	 *   - 两条路径都是 GameMode → Subsystem 单向注入
	 *
	 * @param InSeconds 生化模式每局时长 (秒, 必须 >= 30)
	 */
	void SetZombieMatchDuration(int32 InSeconds) { ZombieMatchDurationSeconds = InSeconds; }

	/**
	 * @brief 注入母体变异倒计时秒数 (来自 GameMode.MotherMutationDurationSeconds)
	 * @param InSeconds 倒计时秒数 (必须 > 0, <= 0 时按零兜底: 静默跳过启动)
	 */
	void SetMotherMutationDuration(float InSeconds) { MotherMutationDurationSeconds = InSeconds; }

	// 【生化模式】空投倒计时配置 (服务器权威, 由 GameMode 注入)
	void SetAirdropInterval(float InSeconds) { AirdropIntervalSeconds = InSeconds; }

	// 【生化模式】母体变异倒计时结束后启动首轮空投倒计时
	void StartAirdropCountdown();

	// 【生化模式】空投系统确认本次空投已降临完毕后调用，重新启动下一轮倒计时
	UFUNCTION(BlueprintCallable, Category = "Room|Lifecycle|Airdrop")
	void NotifyAirdropArrivalCompleted();

	/**
	 * 【v117 大厂架构新增】空投倒计时到期回调 (服务器内部)
	 *
	 * 业务规则 (用户 2026.08.03):
	 *   - AirdropIntervalTimer 到期时调此函数
	 *   - 调 AirdropSubsystem::SpawnAirdropAtAllPoints (生成新空投 + 清理旧空投)
	 *   - 然后调 NotifyAirdropArrivalCompleted (启动下一轮倒计时, 等待下次降临)
	 *
	 * 大厂原则 — 单一入口:
	 *   - 倒计时"到期"= 服务器业务事件 (不是 UI 事件)
	 *   - UI 倒计时显示: GameState.OnRep_AirdropCountdownState 客户端被动渲染
	 *   - 业务触发: 服务器 SetTimer 到期 → 本函数 → AirdropSubsystem.SpawnAirdropAtAllPoints
	 *
	 * 大厂原则 — 镜像 v93.1 MotherMutationTimerHandle:
	 *   - StartAirdropCountdown 末尾 SetTimer(Duration) → 到期调本函数
	 *   - ResetAirdropCountdown 内部 ClearTimer 防残留
	 *   - 重复启动: ClearTimer 旧的再 SetTimer 新的
	 */
	void OnAirdropIntervalExpired();
	void SetMotherMutationCount(int32 InCount)
	{
		CachedMotherMutationCount = FMath::Max(1, InCount);
	}

	/**
	 * 【v108 大厂架构新增】注入母体变异目标选择策略
	 * @param InPolicy 策略 (来自 GameMode.MotherSelectionPolicy)
	 * @note 注入时机: InitGame 一次性, 改配置需重启游戏 (用户决策)
	 */
	void SetMotherSelectionPolicy(EMotherSelectionPolicy InPolicy)
	{
		CachedMotherSelectionPolicy = InPolicy;
	}

	/**
	 * 【v108 大厂架构新增】读取母体变异数量 (供 MotherMutationSubsystem 在 SetTimer 回调中读取)
	 * @return GameMode 注入的母体数量 (默认 1)
	 */
	int32 GetCachedMotherMutationCount() const { return CachedMotherMutationCount; }

	/**
	 * 【v108 大厂架构新增】读取母体变异目标选择策略
	 * @return GameMode 注入的策略枚举 (默认 Random)
	 */
	EMotherSelectionPolicy GetCachedMotherSelectionPolicy() const { return CachedMotherSelectionPolicy; }

	// ==========================================
	// 查询接口
	// ==========================================

	UFUNCTION(BlueprintPure, Category = "Room|Lifecycle")
	bool IsBattleStarted() const { return bBattleStartedBroadcasted; }

protected:
	// ==========================================
	// 内部状态
	// ==========================================

	/**
	 * 开局倒计时的定时器句柄 (MatchStartDelay 秒后触发 SpawnAllPlayers)
	 */
	FTimerHandle MatchStartTimerHandle;

/**
 * 比赛计时器句柄 (1Hz tick, 检查时间是否耗尽)
 */
	FTimerHandle MatchTimerHandle;

	/**
	 * 【v93.1 大厂架构新增】母体变异倒计时到期定时器句柄
	 *
	 * 大厂原则 — 镜像 MatchTimerHandle:
	 *   - StartMotherMutationCountdown 末尾 SetTimer(Duration) → 到期调 MotherMutationSubsystem::HandleCountdownExpired
	 *   - ResetMotherMutationCountdown 内部 ClearTimer 防残留
	 *   - 重复启动: ClearTimer 旧的再 SetTimer 新的 (避免 SetTimer 累加)
	 *
	 * 防重入机制:
	 *   - 本地层 1: URoomMotherMutationSubsystem::bMotherMutationFired_Local
	 *   - 分布式层 2: ARoomGameState::MotherMutationHasFired (Replicated)
	 *   - 客户端层 3: LifecycleSubsystem 不在客户端运行 (服务器权威)
	 */
	FTimerHandle MotherMutationTimerHandle;

	/**
	 * 【v117 大厂架构新增】空投降临倒计时到期定时器句柄
	 *
	 * 大厂原则 — 镜像 MotherMutationTimerHandle:
	 *   - StartAirdropCountdown 末尾 SetTimer(AirdropIntervalSeconds) → 到期调 OnAirdropIntervalExpired
	 *   - OnAirdropIntervalExpired 调 AirdropSubsystem::SpawnAirdropAtAllPoints + NotifyAirdropArrivalCompleted
	 *   - ResetMotherMutationCountdown 内部一并 ClearTimer 防残留
	 *   - 重复启动: ClearTimer 旧的再 SetTimer 新的
	 */
	FTimerHandle AirdropIntervalTimerHandle;

	/**
	 * 是否已经广播过 OnBattleStarted (幂等保护)
	 */
	bool bBattleStartedBroadcasted = false;

	/**
	 * 【v92 大厂架构重构】生化模式每局时长 (由 GameMode 注入, 默认 120s)
	 *
	 * 替代旧的 ZombieTotalRounds (重复字段已删除):
	 *   - 旧版: ZombieTotalRounds (回合数, Subsystem 内部用) + TotalRounds (UI 用) — 重复
	 *   - 新版: ZombieMatchDurationSeconds (时长) + TotalRounds (UI 总局数) — 各司其职
	 *
	 * StartMatchTimer (Zombie 分支) 用此值 + Now 写入 GameState.MatchEndTime
	 * StartNextZombieRound 内部读 GameState.MatchEndTime 重置
	 */
	int32 ZombieMatchDurationSeconds = 120;

	/**
	 * 母体变异倒计时秒数 (由 GameMode 注入, 默认 8s)
	 * 大厂原则 — 业务可配: GameMode 暴露字段给策划调整
	 * <= 0 表示禁用 (匹配逻辑里不启动倒计时)
	 */
	float MotherMutationDurationSeconds = 8.0f;

	/**
	 * 空投倒计时总时长 (秒), 由 GameMode 注入, 默认 120 秒。
	 * 只有母体变异倒计时结束后才会写入 GameState。
	 */
	float AirdropIntervalSeconds = 120.0f;

	/**
	 * 【v108 大厂架构新增】母体变异数量 (由 GameMode.MotherMutationCount 注入)
	 *
	 * 业务规则: 倒计时结束后生成多少个母体
	 * - 默认 1 (跟现状一致, 最小风险)
	 * - ClampMin=1, ClampMax=10 由 GM 暴露字段控制
	 *
	 * 数据流: GameMode.MotherMutationCount → Subsystem (InitGame 一次性注入, 改配置需重启游戏)
	 * 读取方: URoomMotherMutationSubsystem::HandleCountdownExpired (SetTimer 回调, 此刻需要访问)
	 */
	int32 CachedMotherMutationCount = 1;

	/**
	 * 【v108 大厂架构新增】母体变异目标选择策略 (由 GameMode.MotherSelectionPolicy 注入)
	 *
	 * 业务规则: 倒计时结束后按什么策略选目标
	 * - Random / AIOnly / PlayerOnly (见 EMotherSelectionPolicy)
	 *
	 * 数据流: GameMode.MotherSelectionPolicy → Subsystem (InitGame 一次性注入)
	 * 读取方: URoomMotherMutationSubsystem::HandleCountdownExpired
	 */
	EMotherSelectionPolicy CachedMotherSelectionPolicy = EMotherSelectionPolicy::Random;

	/**
	 * 开局延迟秒数 (由 GameMode 注入, 启动 PerformGameStart 时写入)
	 */
	float MatchStartDelay = 3.0f;

	/**
	 * 比赛时长秒数 (刀战模式; 生化模式忽略)
	 */
	float MatchDurationSeconds = 600.0f;

	/**
	 * SpawnAllPlayers 委托 (倒计时结束后触发, 让 GameMode 调 Spawn)
	 */
	FSimpleDelegate OnSpawnAllPlayersCallback;

private:
	// ==========================================
	// 内部方法 (UFUNCTION 必须 public/protected, 私有用 .h 不行)
	// ==========================================
};