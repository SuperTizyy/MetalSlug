// 版权声明：在项目设置的描述页面填写您的版权信息。

#pragma once

// ==========================================
// 头文件包含说明
// ==========================================
// UE 引擎核心最小化头文件
#include "CoreMinimal.h"

// 引入 UE 原生 AGameStateBase 类（基类）
#include "GameFramework/GameStateBase.h"

// 引入房间相关枚举（ERoomState/ERoomMatchMode — ERoomTeam 已于 2026.07.10 删除）
// 改造: 改为精确子表头, 不再被其他无关表污染 (原 StaticTable.h 432 行)
#include "Data/Enums/RoomEnums.h"
#include "Systems/AI/AIBehaviorTypes.h"  // 【v46 新增】FPendingAIEntry
#include "GameplayTagContainer.h" // 【2026.07.10 P0 重构】FGameplayTag 阵营

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
 * @brief 大厅阶段 AI 占位队列变化委托（UI 订阅此事件刷新 Box_AttackTeam/Box_DefenseTeam）
 * @param None
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnPendingAIQueueChanged);

/**
 * @brief 当前回合数改变委托（生化模式每回合结束后递减）
 * @param CurrentRound 当前回合数
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCurrentRoundUpdated, int32, CurrentRound);

/**
 * 【v92 大厂架构重构】总局数改变委托 (替换 ZombieTotalRounds, 单一真理源)
 *
 * 大厂原则 — 单一真理源:
 *   - 服务器在 InitGame 阶段通过 RoomGameMode 注入 TotalRounds
 *   - GameState.TotalRounds Replicated, 客户端 UI 订阅显示
 *   - UI 显示格式: "总局数：xx"
 *
 * 为什么从 ZombieTotalRounds 改到 TotalRounds:
 *   - 旧版 ZombieTotalRounds 与 TotalRounds 是 2 个字段并存 (重复架构)
 *   - 新版 TotalRounds 统一切换模式时复用 (生化模式用, 刀战模式隐藏)
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnTotalRoundsUpdated, int32, TotalRounds);

/**
 * 【v92 大厂架构新增】生化模式母体变异倒计时改变委托
 *
 * 单一真理源 — 客户端 UI 订阅此事件:
 *   - 服务器写入 MotherMutationStartTime/Duration → 自动 Replicated
 *   - OnRep_MotherMutationState 触发 OnMotherMutationChanged.Broadcast(StartTime, Duration)
 *   - Widget 收到事件后用 GetServerWorldTimeSeconds() 计算剩余秒数
 *
 * 大厂原则:
 *   - 不传剩余秒数（避免每秒广播一次倒计时数字 — 大带宽浪费）
 *   - 客户端本地基于权威时间戳计算（与 MatchEndTime 同模式）
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnMotherMutationChanged, float, StartTime, float, Duration);

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
	 * ReplicatedUsing: 服务器写入后自动同步到所有客户端, OnRep_CurrentMatchMode 自动 Broadcast OnMatchModeChanged
	 *
	 * 【v93 大厂架构修复】OnRep 委托:
	 *   - 旧版 (v53-v92): 只有 Replicated, 无 OnRep, 客户端 UI 订阅 OnMatchModeChanged 永远收不到
	 *   - 旧根因: GameFlowSubsystem.cpp:435 只 GS->CurrentMatchMode = RoomMode (没调 Broadcast)
	 *   - 修复: ReplicatedUsing = OnRep_CurrentMatchMode → OnRep 自动 Broadcast (UE 标准机制)
	 *   - 服务器写入走 SetCurrentMatchMode 公开 API, 内部手动 Broadcast (镜像 SetTotalRounds)
	 *
	 * 大厂原则 — 显式优于隐式:
	 *   - 不允许: 直接赋值 GS->CurrentMatchMode = NewMode (绕过 OnRep)
	 *   - 必须: 调 GS->SetCurrentMatchMode(NewMode) 单一入口
	 */
	UPROPERTY(ReplicatedUsing = OnRep_CurrentMatchMode, BlueprintReadOnly, Category = "Room|Match")
	ERoomMatchMode CurrentMatchMode = ERoomMatchMode::Melee;

	/**
	 * 【v93 新增】客户端接到房间模式同步时的回调
	 *
	 * 用途: 触发 OnMatchModeChanged 委托, UI 立即响应 (切换 Melee/Zombie 容器显隐)
	 *
	 * 大厂原则 — 镜像 OnRep_TotalRounds:
	 *   - OnRep_TotalRounds: 服务器写入 → 客户端 OnRep → Broadcast OnTotalRoundsUpdated
	 *   - OnRep_CurrentMatchMode: 服务器写入 → 客户端 OnRep → Broadcast OnMatchModeChanged
	 *   - 两条路径完全对称 (UE 引擎 OnRep 机制保证跨网络同步触发)
	 */
	UFUNCTION()
	void OnRep_CurrentMatchMode();

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
	 *
	 * 大厂原则 — 内部计数 vs UI 显示:
	 *   - CurrentRound 仅作 Subsystem 内部计数器使用 (StartNextZombieRound: CurrentRound--)
	 *   - UI 显示用 TotalRounds (Replicated 单一真理源), 不显示"倒数过程"
	 *   - 避免: UI 显示与内部计数耦合, 客户端不应该看到 "5/5 → 4/5" 这种倒数动画
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
	 * 【v92 大厂架构重构】总局数 (替换 ZombieTotalRounds)
	 *
	 * 大厂原则 — 单一真理源 (大厂架构必修):
	 *   - 策划在 GameMode Class Defaults.TotalRounds 配置 (1~20 钳制)
	 *   - 服务器 InitGame 阶段调用 SetTotalRounds 写入 (单一入口)
	 *   - 引擎自动 Replicate 到所有客户端
	 *   - UI 显示格式: "总局数：xx" (由 UMatchInfoWidget::UpdateTotalRounds 渲染)
	 *
	 * 为什么不用 ZombieTotalRounds (已删除):
	 *   - 旧版 ZombieTotalRounds 与 TotalRounds 是 2 个字段并存 (重复架构)
	 *   - 旧版 ZombieTotalRounds 仅在 Subsystem 内部使用, UI 看不见 (无效数据)
	 *   - 新版 TotalRounds 唯一字段, Subsystem 与 UI 都从 GameState 读
	 *
	 * 调用方:
	 *   - ARoomGameMode::InjectSubsystemConfigs → SetTotalRounds (写入字段)
	 *   - URoomLifecycleSubsystem::HandleZombieRoundEnd / StartNextZombieRound (内部读, 用于日志)
	 */
	UPROPERTY(ReplicatedUsing = OnRep_TotalRounds, BlueprintReadOnly, Category = "Room|Match")
	int32 TotalRounds = 5;

	/**
	 * 客户端接到总局数同步时的回调
	 * 用途: 触发 OnTotalRoundsUpdated 委托, UI 立即更新显示
	 */
	UFUNCTION()
	void OnRep_TotalRounds();

	/**
	 * UI 监听的委托 (总局数变化时触发)
	 * 大厂原则 — 单一入口: GameState.TotalRounds 变化时, UI 立即刷新显示
	 */
	UPROPERTY(BlueprintAssignable, Category = "Room|Match")
	FOnTotalRoundsUpdated OnTotalRoundsUpdated;

	/**
	 * 【服务器专用】设置总局数 (GameMode 在 InitGame 时调用)
	 *
	 * 大厂原则 — 显式优于隐式:
	 *   - 不允许"TotalRounds <= 0 时静默设默认值" (强制修复 GameMode 配置)
	 *   - 不允许"TotalRounds < 1 时静默 Clamp 到 5" (GameMode 字段已 Clamp, 不会传非法值)
	 *
	 * @param InTotalRounds 总回合数 (必须 >= 1, 由 GameMode 钳制)
	 */
	UFUNCTION(BlueprintCallable, Category = "Room|Match")
	void SetTotalRounds(int32 InTotalRounds);

	// ==========================================
	// 【v92 大厂架构新增】生化模式母体变异倒计时
	// ==========================================
	//
	// 设计动机:
	//   生化模式每局开局时, 玩家/AI 都是人类, 互相无敌, 8 秒倒计时结束才会"变异"
	//   服务器端通过这两个字段同步倒计时状态, 客户端 UI 用本地时间计算秒数
	//
	// 大厂原则 — 与 MatchEndTime 镜像:
	//   - 服务器只同步"开始时间戳 + 持续秒数", 不每秒广播倒计时数字
	//   - 客户端用 GetServerWorldTimeSeconds() 计算, 自动补偿网络延迟
	//   - 单一真理源: 数据在 GameState, Widget 只是镜像
	//
	// 调度入口:
	//   - URoomLifecycleSubsystem::StartMotherMutationCountdown() (服务器)
	//   - URoomLifecycleSubsystem::ResetMotherMutationCountdown() (服务器)

	/**
	 * 母体变异开始时间戳 (服务器权威世界时间)
	 * Replicated: 服务器写入后自动同步到所有客户端
	 * 客户端用 GetServerWorldTimeSeconds() 计算剩余秒数
	 */
	UPROPERTY(ReplicatedUsing = OnRep_MotherMutationState, BlueprintReadOnly, Category = "Room|Match|Mother")
	float MotherMutationStartTime = 0.0f;

	/**
	 * 母体变异倒计时总秒数 (业务可配, 默认 8s)
	 * Replicated: 服务器写入后自动同步到所有客户端
	 */
	UPROPERTY(ReplicatedUsing = OnRep_MotherMutationState, BlueprintReadOnly, Category = "Room|Match|Mother")
	float MotherMutationDuration = 0.0f;

	// ==========================================
	// 【v93.1 大厂架构新增】母体变异触发状态 — 防止 SetTimer 重复触发
	// ==========================================
	//
	// 大厂原则 — 分布式防重入:
	//   - 母体变异倒计时结束后, 客户端/服务器都会收到"倒计时 = 0"
	//   - 但只有服务器应触发"选母体 + 变异"业务 (这是权威业务)
	//   - 防止: 同一局 LifecycleSubsystem 重复调 StartMotherMutationCountdown → SetTimer 残留
	//          → HandleCountdownExpired 被重复触发 → 同一局多次变异
	//   - 真理源: 服务器 MarkMotherMutationFired() → Replicate → 客户端读到 true
	//   - 分布式防御层 2 (URoomMotherMutationSubsystem 内 bMotherMutationFired_Local 是层 1)
	//
	// 业务约束:
	//   - 一次比赛只允许触发一次母体变异 (生化模式每局只有 1 个母体)
	//   - Reset 入口: HandleZombieRoundEnd (新回合) / 模式切换
	//   - 不在 OnRep 处理, 因为只是状态字段 (客户端不需要做额外业务)

	/**
	 * 母体变异是否已触发 — 防重入标志 (Replicated)
	 */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Room|Match|Mother")
	bool MotherMutationHasFired = false;

	/**
	 * 母体变异已触发次数 — 业务统计字段 (Replicated)
	 *
	 * 大厂原则 — 单一真理源:
	 *   - 服务器 URoomMotherMutationSubsystem::MutateCharacterToMother 成功时 ++
	 *   - 客户端通过 GetMotherMutationCount() 查询
	 *   - 业务用途: UI 显示 / 比赛结算
	 */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Room|Match|Mother")
	int32 MotherMutationCount = 0;

	/**
	 * 服务器专用: 标记母体变异已触发 + 次数 +1
	 *
	 * 大厂原则 — 显式优于隐式:
	 *   - 服务器写入 Replicated 字段 → 自动同步到客户端
	 *   - 强制调用方是服务器 (HasAuthority), 客户端调用 = 错误
	 *   - 不允许: 外部代码直接 GS->MotherMutationHasFired = true (绕过 +1 统计)
	 *
	 * 调用方:
	 *   - URoomMotherMutationSubsystem::HandleCountdownExpired 末尾
	 */
	UFUNCTION(BlueprintCallable, Category = "Room|Match|Mother")
	void MarkMotherMutationFired();

	/**
	 * 服务器专用: 重置母体变异触发标志 (新回合 / 模式切换)
	 *
	 * 大厂原则 — 显式优于隐式:
	 *   - 服务器写入 Replicated 字段 → 自动同步到客户端
	 *   - 调用方: URoomLifecycleSubsystem::StartMotherMutationCountdown (新局开始时清零)
	 *           + HandleZombieRoundEnd (本局结束)
	 *
	 * 注意: 不清 MotherMutationCount (业务统计字段, 应保留历史)
	 */
	UFUNCTION(BlueprintCallable, Category = "Room|Match|Mother")
	void ResetMotherMutationHasFired();

	/**
	 * 母体变异倒计时同步回调 (客户端)
	 * 触发 OnMotherMutationChanged 委托
	 */
	UFUNCTION()
	void OnRep_MotherMutationState();

	/**
	 * UI 监听的委托: 母体变异倒计时启动/重置时触发
	 */
	UPROPERTY(BlueprintAssignable, Category = "Room|Match|Mother")
	FOnMotherMutationChanged OnMotherMutationChanged;

	/**
	 * 提供一个接口供 UI 查询母体变异剩余时间（秒）
	 * 客户端调用时自动补偿网络延迟
	 *
	 * 大厂原则 — 镜像 GetMatchRemainingSeconds():
	 *   - 如果服务器未启动倒计时, 返回 0
	 *   - 如果倒计时已结束, 返回 0 (不允许负数)
	 *
	 * @return 剩余秒数（最小为 0, 未启动时返回 0）
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Room|Match|Mother")
	int32 GetMotherMutationRemainingSeconds() const;

	/**
	 * 【服务器专用】启动母体变异倒计时
	 * @param Duration 倒计时总秒数
	 * 服务器写入 MotherMutationStartTime/Duration, Replicate 推送到客户端
	 */
	UFUNCTION(BlueprintCallable, Category = "Room|Match|Mother")
	void StartMotherMutationCountdown(float Duration);

	/**
	 * 【服务器专用】重置母体变异倒计时 (关闭)
	 * 服务器写入 StartTime/Duration = 0, 客户端 GetMotherMutationRemainingSeconds() 自动返回 0
	 */
	UFUNCTION(BlueprintCallable, Category = "Room|Match|Mother")
	void ResetMotherMutationCountdown();

	/**
	 * UI 监听的委托: 模式切换时 UI 需隐藏/显示 Text_RemainingRounds
	 */
	UPROPERTY(BlueprintAssignable, Category = "Room|Match")
	FOnMatchModeChanged OnMatchModeChanged;

	/**
	 * 提供一个极其方便的辅助函数: 获取特定阵营的所有玩家 (FGameplayTag 版)
	 *
	 * 【2026.07.10 P0 重构】替代 GetPlayersInTeam(ERoomTeam), 阵营用 FGameplayTag 表达
	 *   - Tag == FFactionTags::Offense()  → 攻方所有玩家
	 *   - Tag == FFactionTags::Defense()  → 守方所有玩家
	 *   - 其它 Tag: 显式返回空数组 (无兜底)
	 *
	 * 因为数据分散在每个人自己的 PlayerState 里了，所以我们需要遍历查询
	 * @param TargetFactionTag 目标阵营 (必须为 Offense/Defense 之一)
	 * @return 该阵营的所有玩家 PlayerState 列表
	 */
	UFUNCTION(BlueprintCallable, Category = "Room|Query")
	TArray<class ARoomPlayerState*> GetPlayersInFaction(FGameplayTag TargetFactionTag) const;

	/**
	 * 查询指定阵营中 AC 最高的玩家的 PlayerState（忽略死亡或无 pawn 的玩家）
	 * @param TargetFactionTag 目标阵营 (必须为 Offense/Defense 之一)
	 * @return AC 最高的 PlayerState（找不到返回 nullptr）
	 */
	UFUNCTION(BlueprintCallable, Category = "Room|Query")
	class ARoomPlayerState* GetFactionTopACPlayer(FGameplayTag TargetFactionTag) const;

	/**
	 * 查询全场所有玩家中 AC 最高的玩家的 PlayerState（忽略死亡或无 pawn 的玩家）
	 * @return 全场 AC 最高的 PlayerState（找不到返回 nullptr）
	 */
	UFUNCTION(BlueprintCallable, Category = "Room|Query")
	class ARoomPlayerState* GetOverallTopACPlayer() const;

	/**
	 * 【v93 新增】服务器专用: 设置当前房间模式
	 *
	 * 大厂原则 — 显式优于隐式 (零兜底):
	 *   - 不允许: 外部代码直接 GS->CurrentMatchMode = NewMode (绕过 OnRep)
	 *   - 必须: 走 SetCurrentMatchMode 公开 API
	 *   - 原因: ReplicatedUsing = OnRep_CurrentMatchMode, 直接赋值不会触发 Broadcast
	 *   - 服务器写入字段后立即手动 Broadcast (镜像 SetTotalRounds)
	 *
	 * 调用方:
	 *   - UGameFlowSubsystem::EnterSkipToHostMode (测试模式写入)
	 *   - 其它未来入口 (例如大厅创房时根据房间类型写入)
	 *
	 * @param NewMode 新模式 (None/Unknown 拒绝写入 — 强制修复调用方)
	 */
	UFUNCTION(BlueprintCallable, Category = "Room|Match")
	void SetCurrentMatchMode(ERoomMatchMode NewMode);

	/**
	 * 记录当前房间的房主名称，全服同步！
	 * 用途: 房主专属标识 / UI 房主皇冠图标显示
	 * 【P0 升级】ReplicatedUsing: 服务端修改后, 客户端 OnRep_HostPlayerName 自动触发广播
	 */
	UPROPERTY(ReplicatedUsing = OnRep_HostPlayerName, BlueprintReadOnly, Category = "Room|Global")
	FString HostPlayerName;

	// ==========================================
	// 【v46 大厂架构修复】AI 占位队列复制 (客户端 UI 需要读取)
	// ==========================================
	//
	// 根因:
	//   ARoomGameMode::PendingAIQueue 不是 Replicated
	//   → 客户端 CheckForNewPlayers 中 GM->GetAllPendingAI() 返回空
	//   → ExpectedTotalCount 不含 AI 数量
	//   → RefreshRoomUI 路径 B (AI 占位) 永远为空
	//   → Box_AttackTeam/Box_DefenseTeam 不显示 AI 标签
	//
	// 修复:
	//   1. ARoomGameState 新增 ReplicatedPendingAIQueue (Replicated 复制到客户端)
	//   2. ARoomGameMode::QueueAIForBattleSpawn 成功后同步到 GameState
	//   3. ARoomGameMode::ConsumePendingAIForBattleSpawn 成功后清空 GameState
	//   4. URoomInsidePage::CheckForNewPlayers 改读 GameState.ReplicatedPendingAIQueue (而非 GM.PendingAIQueue)
	//   5. 添加 OnRep_ReplicatedPendingAIQueue 回调触发 UI 刷新

	/**
	 * AI 占位队列 (Replicated)
	 * 大厅阶段 AI 入队后复制到所有客户端, 用于 UI 渲染 Box_AttackTeam/Box_DefenseTeam
	 */
	UPROPERTY(ReplicatedUsing = OnRep_ReplicatedPendingAIQueue, BlueprintReadOnly, Category = "Room|AI")
	TArray<struct FPendingAIEntry> ReplicatedPendingAIQueue;

	/**
	 * ReplicatedPendingAIQueue 复制回调 (客户端)
	 * 触发 OnPendingAIQueueChanged 广播, 让 UI 订阅者刷新显示
	 */
	UFUNCTION()
	void OnRep_ReplicatedPendingAIQueue();

	/**
	 * AI 占位队列变化事件 (客户端 UI 订阅刷新 Box)
	 */
	UPROPERTY(BlueprintAssignable, Category = "Room|AI")
	FOnPendingAIQueueChanged OnPendingAIQueueChanged;

	/**
	 * 查询指定阵营的 AI 占位数量 (客户端 UI 用)
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Room|AI")
	int32 GetPendingAICountInFaction(FGameplayTag FactionTag) const;

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
	 * 服务器专用: 增加指定阵营的击杀数
	 *
	 * 【2026.07.10 P0 重构】传 FGameplayTag 替代 ERoomTeam
	 *   - Tag == FFactionTags::Offense()  → 攻方 +1
	 *   - Tag == FFactionTags::Defense()  → 守方 +1
	 *   - 其它 Tag: 显式报错, 不增任何字段 (无兜底)
	 */
	UFUNCTION(BlueprintCallable, Category = "Room|Match")
	void AddTeamKill(FGameplayTag FactionTag);

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

	// ==========================================
	// 【P0 架构升级】房主变更事件回调
	// ==========================================

	/**
	 * HostPlayerName 复制回调
	 * 时机: 服务器修改 HostPlayerName 后自动同步到所有客户端
	 * 职责: 转发给 URoomService.BroadcastHostChanged 让 UI 订阅者收到通知
	 */
	UFUNCTION()
	void OnRep_HostPlayerName();
};
