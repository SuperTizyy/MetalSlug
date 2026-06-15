// 版权声明：在项目设置的描述页面填写您的版权信息。

#pragma once

// ==========================================
// 头文件包含说明
// ==========================================
// UE 引擎核心最小化头文件
#include "CoreMinimal.h"

// 引入 UE 原生 AGameStateBase 类（基类）
#include "GameFramework/GameStateBase.h"

// 引入房间相关枚举（ERoomTeam/ERoomMatchMode 等）
// 改造: 改为精确子表头, 不再被其他无关表污染 (原 StaticTable.h 432 行)
#include "Data/Enums/RoomEnums.h"

// UE 自动生成的头文件
#include "RoomGameState.generated.h"

// ==========================================
// 动态多播委托声明（事件驱动机制）
// ==========================================

/**
 * @brief 倒计时改变委托（UI 订阅此事件避免 Tick 轮询）
 * @param RemainingSeconds 剩余秒数
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMatchTimeUpdated, int32, RemainingSeconds);

/**
 * @brief 当前回合数改变委托（生化模式每回合结束后递减）
 * @param CurrentRound 当前回合数
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCurrentRoundUpdated, int32, CurrentRound);

/**
 * @brief 模式切换委托（UI 据此隐藏/显示 Text_RemainingRounds）
 * @param NewMode 新模式
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMatchModeChanged, ERoomMatchMode, NewMode);

/**
 * @brief 双方击杀人数变化委托
 * @param AttackerKills 攻方击杀数
 * @param DefenderKills 守方击杀数
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnTeamKillCountUpdated, int32, AttackerKills, int32, DefenderKills);

/**
 * @brief 进入结算状态委托（倒计时归零时触发，3秒延迟后显示最终结果）
 * @param AttackerKills 攻方当局击杀数
 * @param DefenderKills 守方当局击杀数
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnEnterSettlementDelegate, int32, AttackerKills, int32, DefenderKills);

/**
 * @brief 显示最终结算委托（3秒延迟后触发，显示哪方获胜及总比分）
 * @param AttackerWins 攻方总胜场
 * @param DefenderWins 守方总胜场
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnShowFinalSettlementDelegate, int32, AttackerWins, int32, DefenderWins);

/**
 * @class ARoomGameState
 * @brief 房间全局状态类
 *
 * 引擎会自动将所有连入房间的 PlayerState 存放在原生的 PlayerArray 数组中。
 * 这里放置房间的全局数据，如"当前对局阶段(等待、开战、结算)"、"总比分"等。
 *
 * 网络架构:
 * 1. 服务器权威: 所有的数据修改都通过 HasAuthority() 校验
 * 2. Replicated + OnRep_: 客户端自动同步 + 主动回调
 * 3. NetMulticast: 弥补 OnRep_ 在 ListenServer 不触发的缺陷
 */
UCLASS()
class METALSLUG01_API ARoomGameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	/**
	 * 构造函数: 在 GameState 被实例化时调用
	 * 目的: 注册网络同步属性
	 */
	ARoomGameState();

	/**
	 * 必须重写此函数以注册需要网络同步的变量
	 * 目的: 让 UE 知道哪些 UPROPERTY 需要在客户端间复制
	 */
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// ==========================================
	// 比赛模式与时间同步
	// ==========================================

	/**
	 * 当前房间的游戏模式（刀战/生化）
	 * Replicated: 服务器设置后自动同步到所有客户端
	 */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Room|Match")
	ERoomMatchMode CurrentMatchMode = ERoomMatchMode::Melee;

	/**
	 * 【架构重构】: 只同步比赛结束的绝对时间戳，避免每秒网络通信的极大开销
	 * 当服务器决定开始倒计时，设置此变量为: GetServerWorldTimeSeconds() + 倒计时总时长
	 * 客户端通过 GetMatchRemainingSeconds() 自行换算剩余时间
	 */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Room|Match")
	float MatchEndTime = 0.0f;

	/**
	 * 提供一个接口供 UI 查询剩余时间（秒）
	 * 在客户端调用时能自适应网络延迟计算
	 * @return 剩余秒数（最小为0）
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Room|Match")
	int32 GetMatchRemainingSeconds() const;

	/**
	 * 剩余局数（生化模式: 剩余回合数，刀战模式: 隐藏此字段）
	 * ReplicatedUsing: 同步时会自动调用 OnRep_CurrentRound
	 */
	UPROPERTY(ReplicatedUsing = OnRep_CurrentRound, BlueprintReadOnly, Category = "Room|Match")
	int32 CurrentRound = 0;

	/**
	 * 客户端接到回合数同步时的回调
	 * 用途: 触发 OnCurrentRoundUpdated 委托
	 */
	UFUNCTION()
	void OnRep_CurrentRound();

	/**
	 * UI 监听的委托（回合数变化）
	 */
	UPROPERTY(BlueprintAssignable, Category = "Room|Match")
	FOnCurrentRoundUpdated OnCurrentRoundUpdated;

	/**
	 * UI 监听的委托: 模式切换时 UI 需隐藏/显示 Text_RemainingRounds
	 */
	UPROPERTY(BlueprintAssignable, Category = "Room|Match")
	FOnMatchModeChanged OnMatchModeChanged;

	/**
	 * 提供一个极其方便的辅助函数: 获取特定队伍的所有玩家
	 * 因为数据分散在每个人自己的 PlayerState 里了，所以我们需要遍历查询
	 * @param TargetTeam 目标队伍
	 * @return 该队伍的所有玩家 PlayerState 列表
	 */
	UFUNCTION(BlueprintCallable, Category = "Room|Query")
	TArray<class ARoomPlayerState*> GetPlayersInTeam(ERoomTeam TargetTeam) const;

	/**
	 * 查询指定队伍中 AC 最高的玩家的 PlayerState（忽略死亡或无 pawn 的玩家）
	 * @param TargetTeam 目标队伍
	 * @return AC 最高的 PlayerState（找不到返回 nullptr）
	 */
	UFUNCTION(BlueprintCallable, Category = "Room|Query")
	class ARoomPlayerState* GetTeamTopACPlayer(ERoomTeam TargetTeam) const;

	/**
	 * 查询全场所有玩家中 AC 最高的玩家的 PlayerState（忽略死亡或无 pawn 的玩家）
	 * @return 全场 AC 最高的 PlayerState（找不到返回 nullptr）
	 */
	UFUNCTION(BlueprintCallable, Category = "Room|Query")
	class ARoomPlayerState* GetOverallTopACPlayer() const;

	/**
	 * 记录当前房间的房主名称，全服同步！
	 * 用途: 房主专属标识 / UI 房主皇冠图标显示
	 */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Room|Global")
	FString HostPlayerName;

	// ==========================================
	// 双方击杀统计
	// ==========================================

	/**
	 * 攻方总击杀人数
	 * ReplicatedUsing: 同步时自动调用 OnRep_TeamKillCount
	 */
	UPROPERTY(ReplicatedUsing = OnRep_TeamKillCount, BlueprintReadOnly, Category = "Room|Match")
	int32 AttackerTotalKills = 0;

	/**
	 * 守方总击杀人数
	 */
	UPROPERTY(ReplicatedUsing = OnRep_TeamKillCount, BlueprintReadOnly, Category = "Room|Match")
	int32 DefenderTotalKills = 0;

	/**
	 * 击杀统计变化时的回调（客户端）
	 * 用途: 触发 OnTeamKillCountUpdated 委托
	 */
	UFUNCTION()
	void OnRep_TeamKillCount();

	/**
	 * 击杀统计变化时的广播事件
	 */
	UPROPERTY(BlueprintAssignable, Category = "Room|Match")
	FOnTeamKillCountUpdated OnTeamKillCountUpdated;

	/**
	 * 服务器专用: 增加指定队伍的击杀数
	 * @param Team 要增加击杀数的队伍
	 */
	UFUNCTION(BlueprintCallable, Category = "Room|Match")
	void AddTeamKill(ERoomTeam Team);

	/**
	 * 【网络架构修复】: 强制广播击杀数给所有客户端
	 * 原因: OnRep_TeamKillCount 在 Listen Server 本地不会触发（仅触发于远程客户端）
	 * NetMulticast 确保包括房主在内的所有客户端都能收到刷新通知
	 */
	UFUNCTION(NetMulticast, Unreliable)
	void MulticastRefreshKillCount(int32 AttackerKills, int32 DefenderKills);

	/**
	 * 服务器专用: 重置双方击杀统计（每回合开始时调用）
	 */
	UFUNCTION(BlueprintCallable, Category = "Room|Match")
	void ResetTeamKillStats();

	// ==========================================
	// 结算系统
	// ==========================================

	/**
	 * 攻方胜利局数
	 */
	UPROPERTY(ReplicatedUsing = OnRep_WinStats, BlueprintReadOnly, Category = "Room|Settlement")
	int32 AttackerWins = 0;

	/**
	 * 守方胜利局数
	 */
	UPROPERTY(ReplicatedUsing = OnRep_WinStats, BlueprintReadOnly, Category = "Room|Settlement")
	int32 DefenderWins = 0;

	/**
	 * 客户端接到胜负统计同步时的回调
	 */
	UFUNCTION()
	void OnRep_WinStats();

	/**
	 * 胜负统计变化时的广播事件（用于 UI 刷新胜负数显示）
	 */
	UPROPERTY(BlueprintAssignable, Category = "Room|Settlement")
	FOnTeamKillCountUpdated OnWinStatsUpdated;

	/**
	 * 进入结算状态的广播事件（触发 UI 显示比分面板，3秒后显示最终结果）
	 */
	UPROPERTY(BlueprintAssignable, Category = "Room|Settlement")
	FOnEnterSettlementDelegate OnEnterSettlement;

	/**
	 * 显示最终结算的广播事件（3秒延迟后触发，显示哪方获胜）
	 */
	UPROPERTY(BlueprintAssignable, Category = "Room|Settlement")
	FOnShowFinalSettlementDelegate OnShowFinalSettlement;

	/**
	 * 服务器专用: 执行当局结算（由 RoomGameMode 在倒计时归零时调用）
	 * 内部自动完成: 判断胜负 -> 累加胜局数 -> 广播进入结算 -> 延迟3秒广播最终结果
	 */
	UFUNCTION(BlueprintCallable, Category = "Room|Settlement")
	void TriggerSettlement();

private:
	/**
	 * 延迟3秒后广播最终结算（供内部 Timer 调用）
	 */
	UFUNCTION()
	void BroadcastFinalSettlement();

	/**
	 * 【网络架构修复】: NetMulticast 确保包括房主在内的所有客户端都能收到最终结算广播
	 * 替代方案: BroadcastFinalSettlement 中的 HasAuthority 检查导致纯客户端进程直接 return
	 */
	UFUNCTION(NetMulticast, Reliable)
	void MulticastShowFinalSettlement(int32 InAttackerWins, int32 InDefenderWins);

	/**
	 * 【网络架构修复】: NetMulticast 确保所有客户端都能收到进入结算通知（显示 Text_GameOver）
	 */
	UFUNCTION(NetMulticast, Reliable)
	void MulticastEnterSettlement(int32 InAttackerKills, int32 InDefenderKills);

	/**
	 * 结算定时器句柄（持久化，避免局部变量在延迟期间失效）
	 */
	FTimerHandle SettlementTimerHandle;
};
