// 版权声明：在项目设置的描述页面填写您的版权信息。

#pragma once

// ==========================================
// 头文件包含说明
// ==========================================
// UE 引擎核心最小化头文件
#include "CoreMinimal.h"

// 引入 UE 原生 APlayerState 类（基类）
#include "GameFramework/PlayerState.h"

// 【v229.x 大厂架构】KDA 公式单一真理源 (业务层 + UI 层共用)
#include "Utils/KdaScoring.h"

// 引入房间相关枚举（ERoomState/ERoomMatchMode — ERoomTeam 已于 2026.07.10 删除）
#include "Data/Enums/RoomEnums.h"
#include "Data/Enums/CombatEnums.h"  // 【v100 新增】EKillStreakType 连杀类型
#include "GameplayTagContainer.h" // 【2026.07.10 P0 重构】FGameplayTag 阵营

// UE 自动生成的头文件
#include "RoomPlayerState.generated.h"

/**
 * @file RoomPlayerState.h
 * @brief 房间玩家状态类 (ARoomPlayerState) — UE 5.6 玩家网络权威数据集中地
 *
 * 大厂架构角色 — 单一真理源 (Single Source of Truth):
 *   - 阵营 (CurrentFactionTag FGameplayTag 替代 ERoomTeam)
 *   - 准备状态 + 是否主动选过阵营
 *   - 母体状态 (bIsMother) — 母体复活链真理源
 *   - 战备选择 (CharID / 3 个武器槽位) — Spawn 阶段读取
 *   - 计分板 (Score/Kills/Deaths/Assists) + 连杀计数 (CurrentKillStreak)
 *
 * 与其他组件的关系:
 *   - 上游: ARoomGameMode (PostLogin 改阵营 / AddPlayerToRoom)
 *   - 下游: ABaseCharacter Pawn (持引用, Pawn 死亡时仍存活)
 *   - 配套: URoomSpawnSubsystem (复活链读 PS 字段)
 *
 * v229.x 重构:
 *   - KDA 公式委托 FKdaScoring 单一真理源 (业务层 = UI 层)
 *   - 旧 KillScoreValue / AssistScoreValue 标记 deprecated alias
 */


// ==========================================
// 1. 委托声明
// ==========================================

/**
 * 计分板数据变化时的动态多播委托
 * 用于 UI 监听计分板数据刷新
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnScoreboardDataChanged);

/**
 * 房间玩家状态变化时的动态多播委托
 * 用于 UI 监听队伍/准备状态变化
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnRoomPlayerStateChanged);


/**
 * @class ARoomPlayerState
 * @brief 房间玩家状态类
 *
 * 职责说明:
 * - 在 Server 和 Client 之间自动同步单个玩家的队伍、准备状态以及计分板数据
 * - 暴露"已选角色+武器"信息给服务器 GameMode 使用
 * - 提供计分板（击杀/死亡/助攻/得分）的同步和广播
 *
 * 架构理念:
 * 1. 网络同步: ReplicatedUsing 机制自动同步到所有客户端
 * 2. UI 解耦: 通过委托广播，UI 层只需监听事件即可刷新
 * 3. 服务器权威: 所有数据修改均在 HasAuthority() 校验下执行
 */
UCLASS()
class METALSLUG01_API ARoomPlayerState : public APlayerState
{
	GENERATED_BODY()

public:
	/**
	 * 构造函数
	 * 目的: 初始化默认值、开启网络同步
	 */
	ARoomPlayerState();

	// ==========================================
	// 角色/武器选择 Getter（供服务器 GameMode 使用）
	// ==========================================

	/**
	 * 获取当前选中的角色 ID
	 */
	FString GetSelectedCharacterID() const { return SelectedCharacterID; }

	/**
	 * 获取当前选中的 1 号位武器 ID
	 */
	FString GetSelectedWeapon1ID() const { return SelectedWeaponID1; }

	/**
	 * 获取当前选中的 2 号位武器 ID
	 */
	FString GetSelectedWeapon2ID() const { return SelectedWeaponID2; }

	/**
	 * 【2026.07.18 P0】获取当前选中的 3 号位武器 ID (近战武器)
	 *
	 * v52 槽位扩展: 大厅 3 把武器 (主+副+近战) 对应 SelectedWeaponID1/2/3
	 * 战斗 Spawn 时统一读出 3 把应用到玩家
	 */
	FString GetSelectedWeapon3ID() const { return SelectedWeaponID3; }

	// ==========================================
	// 角色/武器选择 Setter
	// ==========================================

	/**
	 * 一次性设置角色+3 把武器（Controller 专用）
	 * 改为本地 Setter（去掉此处的 Server RPC，统一由 Controller 转发）
	 *
	 * 【v52 P0 扩展】参数从 2 把改为 3 把 (主/副/近战)
	 *
	 * 【v210 P0 防御性写入 (大厂架构 — 零覆盖)】
	 *   - W1/W2/W3 传入空串 + 字段当前已有非空值 → **保留旧值不覆盖** (不主动清空玩家已选)
	 *   - W1/W2/W3 传入空串 + 字段当前为空       → 写入空串 (玩家未选, Spawn 阶段走 v209 业务兜底)
	 *   - 传入非空                             → 写入新值 (玩家主动选择, 正常覆盖)
	 *   - CharID (4 个字段里唯一**永远无脑覆盖**的, 因为存档也没有"角色已选"概念)
	 *
	 * 设计动机 (用户 2026.08.09 BUG):
	 *   "客户端自己选择了近战武器, 但是进游戏无法切到近战武器"
	 *   根因: DelayedSendPlayerInfo 调 Server_SelectLoadout(Char, W1, W2, "")
	 *         SetPlayerLoadout 无脑覆盖 → PS.SelectedWeaponID3 = "" → Spawn 走 v209 兜底 DT 第 2 行
	 *         玩家切近战看到"不认识的刀" → "无法切到近战武器"
	 *   修复后: 防御性写入, W3 空串不覆盖, 玩家大厅选过的近战武器"重连不丢"
	 *
	 * 调用方契约:
	 *   - "主动同步" (玩家换武器) → 传完整 4 个非空字段 (RoomService 路径, 由 SyncLoadoutToServer 触发)
	 *   - "同步存档 + 保留运行时已选" (DelayedSendPlayerInfo / Init 阶段) → 传空串给存档没有的字段
	 *   - "显式清空" → 调用 ClearPlayerLoadout (如需新增, 见 v210 架构说明)
	 *
	 * @param InCharID     角色 ID
	 * @param InPrimaryID  主武器 ID (Slot 1) — 空串保留旧值
	 * @param InSecondaryID 副武器 ID (Slot 2) — 空串保留旧值
	 * @param InMeleeID    近战武器 ID (Slot 3) — 空串保留旧值 (关键! 防止 Q8 决策的存档漏洞)
	 */
	void SetPlayerLoadout(const FString& InCharID, const FString& InPrimaryID, const FString& InSecondaryID, const FString& InMeleeID);

	// ==========================================
	// 网络同步注册
	// ==========================================

	/**
	 * 重写此函数，注册需要网络同步的变量
	 * 【核心规范】: 必须重写
	 */
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// ==========================================
	// 2. 核心同步数据 (Replicated Data)
	// ==========================================

	/**
	 * 【2026.07.10 P0 重构】玩家当前所属阵营 (FGameplayTag)
	 *
	 * 设计 (大厂原则 - 单一真理源):
	 *   - 取代 ERoomTeam (None/Attack/Defense) — 全部阵营表达统一用 FGameplayTag
	 *   - 有效值: Faction.Offense (攻方) / Faction.Defense (守方)
	 *   - 由 ARoomGameMode::ModeRulesByMode 决定每种模式哪个 Tag 属于攻/守方
	 *   - 初始值: Faction.Defense (玩家默认守方, 平衡设计; 模式启动后由 GameMode 重写)
	 *
	 * 迁移 (旧 BP 引用 ERoomTeam 字段会编译失败 → 显式更新 BP 即可):
	 *   - ERoomTeam::None     → 空 Tag (新流程中不存在, GameMode 必须显式设)
	 *   - ERoomTeam::Attack   → Faction.Offense
	 *   - ERoomTeam::Defense  → Faction.Defense
	 */
	UPROPERTY(ReplicatedUsing = OnRep_FactionTag, BlueprintReadOnly, Category = "Room|State")
	FGameplayTag CurrentFactionTag;

	/**
	 * 玩家准备状态
	 * ReplicatedUsing: 客户端收到新值时会自动触发 OnRep_IsReady
	 */
	UPROPERTY(ReplicatedUsing = OnRep_IsReady, BlueprintReadOnly, Category = "Room|State")
	bool bIsReady;

	/**
	 * 【2026.07.11 v29.6 大厂原则】玩家是否已显式选择阵营
	 *
	 * 设计动机:
	 *   - AddPlayerToRoom 内置 auto-balance 会按"哪边人少"自动分配阵营
	 *   - 但 auto-balance 多次跑 (EnterSkipToHostMode 一次 + DelayedSendPlayerInfo 一次),
	 *     会**反复覆盖**玩家已经在 UI 选过的阵营
	 *   - 玩家点 Btn_JoinDefenseTeam 切队之后, 又被 auto-balance 改回 Offense → 用户感知"切队没生效"
	 *
	 * 大厂原则 (单一真理源 + 玩家意图不可覆盖):
	 *   - 玩家主动切队 (ChangePlayerTeam 成功改阵营) → bHasExplicitlyChosenTeam = true, 并 Replicate
	 *   - auto-balance 只在 bHasExplicitlyChosenTeam == false 时跑
	 *   - 玩家从未主动切过 → 默认 Defense (PS 构造函数已设), auto-balance 可优化初值
	 *   - 玩家主动切过 → auto-balance **不再动**, 玩家意图是真神
	 *
	 * 注: 不可 Reset; 跨 session 不持久化 (P0 这版先 Reload-only, 未来如需 reset 重连场景, 再加 reset API)
	 */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Room|State")
	bool bHasExplicitlyChosenTeam = false;

	/**
	 * 【2026.07.11 v29.6】服务器专用: 标记玩家已显式选择阵营
	 *
	 * 调用方: ARoomGameMode::ChangePlayerTeam (收到玩家主动切队请求, 改阵营成功后)
	 * 大厂原则: 一旦玩家主动切队, auto-balance 永远不再覆盖
	 */
	void Server_MarkTeamExplicitlyChosen();

	/**
	 * 【2026.07.26 v99 P0】玩家是否已被变异为母体 (复活链真理源)
	 *
	 * 业务核心 (用户 2026.07.26 明确):
	 *   - 生化模式: 母体死后复活, 必须原地复活成母体, 不能复活成人类
	 *
	 * 根因 (Session1.log line 1161-1218):
	 *   - 旧版 RequestRespawn 直接读 PlayerSpawnDataCache.CharID (永远是"JS001" 人类)
	 *   - 即使死前 Pawn 是 BP_MuTi, 复活时一律走 HandlePlayerRequestSpawn → 复活成 BP_SWAT_C 人类
	 *
	 * 大厂原则 — 单一真理源 + Replicated:
	 *   - 写点: URoomMotherMutationSubsystem::MutateCharacterToMother 末尾设 true
	 *   - 写点: URoomSpawnSubsystem::MutatePawnToMother → 业务层调 MutateCharacterToMother
	 *   - 读点: URoomSpawnSubsystem::RequestRespawn 读 PS->bIsMother, true 走母体复活流程
	 *   - 读点: 同局内任何复活链入口 (玩家 / AI)
	 *
	 * 网络同步:
	 *   - Replicated (无 OnRep — UI 不需要监听, 复活流程是服务器内部决策)
	 *   - 客户端不需要实时感知 (它只关心视觉 OnRep_bIsMother 由 Pawn 提供)
	 *
	 * 跨进程同步说明:
	 *   - 服务器: 写 true 后, PS 字段 Replicated 自动同步客户端
	 *   - 客户端: 仅用于可观测性 (Log/调试), 不参与复活决策 (决策在服务器跑)
	 *
	 * 不破坏刀战模式:
	 *   - 刀战模式从不调 MutateCharacterToMother → bIsMother 永远是 false
	 *   - RequestRespawn 读 false 走老路径 → 刀战逻辑零影响
	 */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Room|State")
	bool bIsMother = false;

	/**
	 * 【v99.1 大厂架构 — 母体复活位置真理源】玩家 Pawn 上次死亡的世界 Transform
	 *
	 * 写入时机:
	 *   - UCombatDeathComponent::ExecuteDeathLocal 头部 (HasAuthority) — 死亡瞬间缓存当前 Pawn Transform
	 *   - 复活前由 URoomSpawnSubsystem::MutatePawnToMother 读取, 读取后由调用方负责清空
	 *
	 * 读取时机:
	 *   - URoomSpawnSubsystem::MutatePawnToMother 复活链 (OldPawn 已销毁时, 拿 PS 上次的死亡位置)
	 *
	 * 大厂原则 — 单一真理源 (零兜底):
	 *   - 旧版复活链没有死亡 Transform → 用了 ZeroVector → SpawnActor(0,0,0) 失败
	 *   - v99.1: 死亡时主动缓存到 PlayerState, 复活链精确还原
	 *   - bHasLastDeathTransform 标志位 — 防止"未死亡过"被误读为 (0,0,0)
	 *
	 * 跨模式安全:
	 *   - 刀战模式走出生点路径, 不读本字段(永远 bHasLastDeathTransform=false → 不会误用)
	 *   - 本字段不影响 BP/UI 显示(纯服务器内部决策用)
	 */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Room|State")
	FTransform LastDeathTransform;

	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Room|State")
	bool bHasLastDeathTransform = false;

	// ==========================================
	// 3. 计分板数据 (Scoreboard Data)
	// ==========================================
public:
	/**
	 * 获取总得分
	 */
	int32 GetScore() const { return RoomScore; }

	/**
	 * 获取击杀数
	 */
	int32 GetKills() const { return RoomKills; }

	/**
	 * 获取死亡数
	 */
	int32 GetDeaths() const { return RoomDeaths; }

	/**
	 * 获取助攻数
	 */
	int32 GetAssists() const { return RoomAssists; }

	/**
	 * 服务器专用: 增加得分（+1 击杀 +20 分）
	 * 同步更新 GameState 中的队伍击杀统计
	 */
	UFUNCTION(BlueprintCallable, Category = "Scoreboard")
	void AddKillScore();

	/**
	 * 服务器专用: 增加助攻得分（+1 助攻 +10 分）
	 */
	UFUNCTION(BlueprintCallable, Category = "Scoreboard")
	void AddAssistScore();

	/**
	 * 服务器专用: 增加死亡次数
	 */
	UFUNCTION(BlueprintCallable, Category = "Scoreboard")
	void AddDeath();

	/**
	 * 服务器专用: 重置计分板数据（每回合开始时调用）
	 */
	UFUNCTION(BlueprintCallable, Category = "Scoreboard")
	void ResetScoreboardStats();

protected:
	/**
	 * 计分板数据复制通知回调
	 */
	UFUNCTION()
	void OnRep_ScoreboardData();

	/**
	 * 总得分（网络同步）
	 */
	UPROPERTY(ReplicatedUsing = OnRep_ScoreboardData, BlueprintReadOnly, Category = "Scoreboard")
	int32 RoomScore;

	/**
	 * 击杀数（网络同步）
	 */
	UPROPERTY(ReplicatedUsing = OnRep_ScoreboardData, BlueprintReadOnly, Category = "Scoreboard")
	int32 RoomKills;

	/**
	 * 死亡数（网络同步）
	 */
	UPROPERTY(ReplicatedUsing = OnRep_ScoreboardData, BlueprintReadOnly, Category = "Scoreboard")
	int32 RoomDeaths;

	/**
	 * 助攻数（网络同步）
	 */
	UPROPERTY(ReplicatedUsing = OnRep_ScoreboardData, BlueprintReadOnly, Category = "Scoreboard")
	int32 RoomAssists;

	// ==========================================
	// 4. 【v100 大厂架构 — 连杀真理源】连杀追踪 (服务端权威)
	// ==========================================
public:
	/**
	 * 【v100 大厂架构 — 连杀计数真理源】当前连杀数 (服务端权威)
	 *
	 * 历史痛点 (v22-v99):
	 *   - 旧版连杀计数只存在于 UKillStreakWidget::CurrentKillStreak (HUD 客户端 widget)
	 *   - 该字段不在服务端 → 音效/Multicast_NotifyKill RPC 等服务器逻辑无法感知连杀数
	 *   - 真正的真理源分裂:HUD 显示是 widget 自己算,但游戏逻辑(音效/分级)无数据源
	 *
	 * 新版 (v100):
	 *   - 真理源迁移到 PlayerState (服务端权威)
	 *   - Replicated (所有客户端可读 — HUD widget 可订阅 OnRep 显示)
	 *   - 服务器通过 ServerUpdateKillStreak(bIsAssist, bIsHeadshot) 维护
	 *
	 * 大厂原则 — 单一真理源:
	 *   - 服务端算 StreakType → 跨 RPC 边界 → 所有客户端一致播放音效
	 *   - 客户端 HUD widget: 改读此字段(后续 PR 跟进)
	 *
	 * 不破坏刀战模式:
	 *   - 刀战模式同样调用 ServerUpdateKillStreak(无模式判断)
	 *   - 字段依然有效 — 连杀系统在所有模式通用
	 *
	 * 零兜底:
	 *   - < 0 视作 0(防御初始值) — 大厂原则: 不允许"特殊值"标志位
	 *   - 超时不重置(超时判定在 ServerUpdateKillStreak 内部按 WorldTime 比较)
	 */
	UPROPERTY(ReplicatedUsing = OnRep_KillStreak, BlueprintReadOnly, Category = "Scoreboard|Streak")
	int32 CurrentKillStreak = 0;

	/**
	 * 【v100 大厂架构 — 连杀超时判定】上次击杀时间戳 (服务端专用, 仅服务器)
	 *
	 * 设计:
	 *   - Server-only 字段 — 不 Replicate(客户端 HUD widget 改读 CurrentKillStreak 即可)
	 *   - 服务器调 ServerUpdateKillStreak 比较 LastKillWorldTimeSeconds 与 Now - KillStreakDuration
	 *   - 超时 → 重置 CurrentKillStreak = 0; 否则累加
	 *
	 * NOT Replicated 原因:
	 *   - 客户端不需要时间戳(超时逻辑全部由服务器算)
	 *   - 减少带宽(数据驱动 RPC 边界)
	 *   - 真理源全在服务端 → 客户端只能看到"最终结果" CurrentKillStreak
	 */
	UPROPERTY()
	float LastKillWorldTimeSeconds = -1.f;

	/**
	 * 【v100 大厂架构 — 连杀计算入口】服务器专用: 玩家击杀时累加/重置连杀
	 *
	 * 入口: UCombatDeathComponent::PerformKillSettlement 中
	 *       AddKillScore 之前调一次, 让本 PlayerState 更新连杀计数
	 *
	 * 大厂原则 — 单一真理源:
	 *   - 服务器累加 → CurrentKillStreak 字段(Replicated)
	 *   - 计算 EKillStreakType → 通过返回值传出 → 上层传给 Multicast_NotifyKill RPC
	 *
	 * @param bIsAssist  是否助攻(true = 助攻,不计入连杀)
	 * @param bIsHeadshot  是否爆头(影响 StreakType 计算)
	 * @return            累加/重置后的 EKillStreakType (服务器传给 RPC,客户端用此值决定播哪个音)
	 *
	 * 零兜底:
	 *   - 不在 PlayerState 无效时返回 None(必须让调用方感知错误)
	 *   - 实际调用方必走 GetRoomPlayerState() 守卫
	 */
	UFUNCTION(BlueprintCallable, Category = "Scoreboard|Streak")
	EKillStreakType ServerUpdateKillStreak(bool bIsAssist, bool bIsHeadshot);

	/**
	 * 【v100 大厂架构 — 连杀超时清理】服务器专用: 重置连杀(死亡时调)
	 *
	 * 入口: UCombatDeathComponent::PerformKillSettlement 中
	 *       AddDeath 时同步调(死亡清零)
	 *
	 * 不复制到客户端(客户端 widget 自己监听死亡流 — 后续 HUD 改造步骤)
	 */
	UFUNCTION(BlueprintCallable, Category = "Scoreboard|Streak")
	void ServerResetKillStreak();

protected:
	/**
	 * 【v100 OnRep】连杀数复制到客户端 (HUD widget 改读此处)
	 *
	 * 当前 v100 阶段:KillStreakWidget 暂不改 widget 内部字段读 PS(后续 PR)
	 * 当前只读 + Log 镜像 (便于调试和验证 RPC 链路)
	 */
	UFUNCTION()
	void OnRep_KillStreak();

public:
	/**
	 * 计分板数据变化时的广播事件
	 * UI 监听此委托即可刷新
	 */
	UPROPERTY(BlueprintAssignable, Category = "Scoreboard|Events")
	FOnScoreboardDataChanged OnScoreboardDataChanged;

	// ==========================================
	// 4. 客户端数据刷新回调 (Rep Notifies)
	// ==========================================

	/**
	 * 【2026.07.10 重构】FactionTag 变化时的客户端回调
	 * 替代原 OnRep_Team, OnRep 函数名变更仅为清晰区分 — UI 监听 OnStateChanged 即可
	 */
	UFUNCTION()
	void OnRep_FactionTag();

	/**
	 * 准备状态变化时的客户端回调
	 */
	UFUNCTION()
	void OnRep_IsReady();

	// ==========================================
	// 5. UI 绑定接口
	// ==========================================
	/**
	 * 队伍/准备状态变化广播事件
	 * UI 只需监听此委托即可刷新，实现完美解耦
	 */
	UPROPERTY(BlueprintAssignable, Category = "Room|Events")
	FOnRoomPlayerStateChanged OnStateChanged;

protected:
	// ==========================================
	// 6. 战备选择数据 (Replicated)
	// ==========================================
	/**
	 * 选中的角色 ID（用于显示队友信息）
	 */
	UPROPERTY(Replicated)
	FString SelectedCharacterID;

	/**
	 * 【v52】选中的 1 号位武器 ID = 主武器 (Primary)
	 * 真理源 = 大厅 UI BP1 的 SelectedWeaponID1
	 */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Room|Loadout")
	FString SelectedWeaponID1;

	/**
	 * 【v52】选中的 2 号位武器 ID = 副武器 (Secondary)
	 * 真理源 = 大厅 UI 的 TempSelectedWeaponsByType[Secondary] (运行期, 不存档)
	 */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Room|Loadout")
	FString SelectedWeaponID2;

	/**
	 * 【2026.07.18 P0】选中的 3 号位武器 ID (近战武器)
	 *
	 * 背景:
	 *   旧 v23-v51: 大厅只有 2 把武器槽位 (SelectedWeaponID1/2)
	 *   v52: 扩展为 3 槽位 (主/副/近战), 支持生化模式运行时切换
	 *
	 * 大厂原则 (单一真理源):
	 *   - 3 个槽位都从大厅写入, 战斗 Spawn 时统一读出
	 *   - RoomInsidePage 改造后, 每个背包都装 3 把武器, 玩家切背包时整个 Loadout 替换
	 *
	 * 零兜底 (v51 同原则):
	 *   - 空字符串表示"未选", 拒绝用 NAME_None 当标志位
	 *   - 大厅 UI 必须保证至少 1 把 (主或近战) 有值, 否则不允许开局
	 *
	 * 网络同步: Replicated (跟随 SelectedWeaponID1/2 自动同步)
	 */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Room|Loadout")
	FString SelectedWeaponID3;

private:
	// ==========================================
	// 7. 【v229.x 大厂架构重构】计分常量 — 委托 FKdaScoring 单一真理源
	// ==========================================
	//
	// 大厂原则 — 单一真理源:
	//   - 旧 (v22-v228): 本类硬编码 KillScoreValue=20 / AssistScoreValue=10
	//     - 与 UI 层规则 (10/5) 不一致 → 大厂反模式
	//     - 死亡不做操作 → 业务层缺失死亡扣分逻辑
	//   - 新 (v229.x): 委托 FKdaScoring::KillScore / FKdaScoring::AssistScore
	//     - 业务层 = UI 层 = 同一公式 (10/5/-1)
	//     - 死亡 -1 分由 AddDeath 统一处理 (走 FKdaScoring::ComputeStep)
	//
	// 保留同名常量作为 deprecated alias:
	//   - 旧代码引用 KillScoreValue/AssistScoreValue 时仍能编译
	//   - 但已弃用 (v229.x 起), 业务新增代码必须直接用 FKdaScoring
	[[deprecated("v229.x: 使用 FKdaScoring::KillScore / FKdaScoring::AssistScore")]]
	static constexpr int32 KillScoreValue = FKdaScoring::KillScore;

	[[deprecated("v229.x: 使用 FKdaScoring::AssistScore")]]
	static constexpr int32 AssistScoreValue = FKdaScoring::AssistScore;
};
