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
	// 配置入口 (由 GameMode 在构造/初始化时注入)
	// ==========================================

	/**
	 * @brief 注入生化模式总回合数 (来自 GameMode.ZombieTotalRounds)
	 */
	void SetZombieTotalRounds(int32 InRounds) { ZombieTotalRounds = InRounds; }

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
	 * 是否已经广播过 OnBattleStarted (幂等保护)
	 */
	bool bBattleStartedBroadcasted = false;

	/**
	 * 生化模式总回合数 (由 GameMode 注入)
	 */
	int32 ZombieTotalRounds = 5;

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