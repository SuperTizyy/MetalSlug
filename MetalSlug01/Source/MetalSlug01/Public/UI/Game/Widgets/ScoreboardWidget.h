// 版权声明：在项目设置的描述页面填写您的版权信息。

#pragma once

// ==========================================
// ScoreboardWidget 头文件 — 战斗排名计分板
// ==========================================
//
// 文件作用:
//   1. 声明 UScoreboardWidget — Tab 键打开的 Scoreboard 计分板 + 结算面板容器
//   2. 显示攻/守方所有玩家 + 排名 + 击杀数 + 死亡数 + 助攻数
//   3. 显示结算页: Border_SettlementOverlay + 返回大厅按钮
//
// 设计理念 (大厂原则 - 5 个核心原则):
//   1. 单一真理源: Score 由 FKdaScoring::Compute(Kills, Assists, Deaths) 单一公式计算
//   2. 单入口: RefreshScoreboard / RefreshRanksInContainer 统一所有排名更新
//   3. 稳定排序: int64 复合 key 编码 + OriginalIndex tiebreaker
//   4. 严格唯一排名: rank = i+1, 1,2,3,4... 即便同分也不重号
//   5. 冻结快照: bIsFrozen 标志位让 Widget 与 Service 解耦 (结算页跨地图持久)
//
// 关键历史重构:
//   v215: 增量更新替代 Clear+重建 → 修复闪烁
//   v216: Border + Button 从 GameHUDWidget 迁移过来 (跨地图用)
//   v217: 冻结后跳过 Tick 兜底刷新 → 修复排名"反复跳变"
//   v229.x v2: 单一真理源 Score 公式 + 严格唯一排名 (用户业务规则)
//   v229.x v2.1: 倒序 RemoveChild + 顺序 AddChild 修复排序顺序错乱
//   v223.0: AI 数据源走 ReplicatedBattleAIEntries (Server-Authoritative)
//
// 大厂对应:
//   - Lyra: UCommonActivatableWidget + 自定义排名 widget
//   - Fortnite: 通用计分板
// ==========================================

// ==========================================
// 头文件包含说明
// ==========================================
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Data/Enums/RoomEnums.h" // 引入 ERoomMatchMode
// 【v202.0 大厂架构】不再 include RoomGameState / RoomPlayerState (走快照路径)
// 【v223.0 大厂架构】Include FFactionSnapshotEntry 的完整定义 (Compile Error 修复: ConvertBattleAIEntryToSnapshot 的参数类型)
//   - 完整定义带来 FGameplayTag / FFactionSnapshotEntry 所有字段, 头文件编译完全自给
//   - 不依赖 .cpp include 顺序 (UE 编译依赖传递性低, 必须每编译单元独立 include)
#include "Systems/Settlement/SettlementSnapshotSubsystem.h" // 【v223.0】FFactionSnapshotEntry 完整定义
#include "GameplayTagContainer.h" // 【v223.0】FGameplayTag 完整定义 (ServerValidateFactionConsistency 参数)
#include "ScoreboardWidget.generated.h"

// 前向声明
class UVerticalBox;
class UScoreboardEntryWidget;
// 【v202.0 大厂架构】删除 ARoomPlayerState 直接引用 (走 FPlayerSnapshot 快照路径, View 不感知 PlayerState)
// class ARoomPlayerState;
class UTextBlock;
class URoomStateService;
struct FPlayerSnapshot;
// 【v216 大厂架构新增】结算覆盖板 Border + 返回大厅按钮 (从 GameHUDWidget 迁移)
class UBorder;
class UButton;
// 【v216 大厂架构新增】结算快照结构体 (来自 USettlementSnapshotSubsystem)
// 仅前向声明: ApplySnapshot 接 const FFinalSettlementSnapshot& 引用,完整定义在 .cpp include
struct FFinalSettlementSnapshot;

// 【v215 大厂架构新增】URoomStateService OnPlayerSnapshotsChanged 事件回调声明
// (因为是 Dynamic delegate, 需要 UFUNCTION)


/**
 * @class UScoreboardWidget
 * @brief 刀战/生化 模式计分板控件 (通用)
 *
 * 使用方式:
 * - 对战中按 Tab 显示计分排行（VB_AttackerTeam / VB_DefenderTeam）
 * - 结算状态时保持显示，并在其基础上叠加显示
 * - 由 GameHUDWidget::ShowScoreboard 触发 RefreshScoreboard
 *
 * 显示内容:
 * - Text_AttackerTeamTitle / Text_DefenderTeamTitle: 阵营标题 (刀战"攻方/守方", 生化"母体阵营/人类阵营")
 * - VB_AttackerTeam / VB_DefenderTeam: 真人 + AI 共显示
 * - Text_Settlement_AttackerKills: 刀战"攻方击杀总数:xxx" / 生化"母体阵营赢得对局数:xxx"
 * - Text_Settlement_DefenderKills: 刀战"守方击杀总数:xxx" / 生化"人类阵营赢得对局数:xxx"
 * - Text_AttackerWinResult: 刀战"攻方胜利" / 生化"母体阵营胜利"
 * - Text_DefenderWinResult: 刀战"守方胜利" / 生化"人类阵营胜利"
 *
 * 注意: 返回大厅按钮已迁移至 GameHUDWidget 统一管理
 *
 * 【v202.0 大厂架构 — 数据源切换】
 *   历史 (v22-v201.x): 直接遍历 GS->PlayerArray → 只显示真人, AI 永远不显示
 *   新 (v202.0):
 *     - 数据源 = URoomStateService::GetFactionSnapshotsWithAI() (单一真理源)
 *     - 真人: 来自 ARoomPlayerState (已有 Replicated RoomKills/RoomDeaths/RoomAssists/RoomScore)
 *     - AI:   来自 ABaseAIController (v202.0 新增 Replicated AIKills/AIDeaths/AIAssists/AIScore)
 *     - 走完整 RPC 链路 (ReplicatedUsing → OnRep → Broadcast → UI 订阅刷新)
 *
 * 【v203.0 大厂架构 — 模式分支阵营文案】
 *   历史 (v22-v202.x): UI 文案写死"攻方/守方", 生化模式玩家看不懂
 *   新 (v203.0):
 *     - 阵营名映射走 namespace ScoreboardFactionNames (CPP 局部, 单一真理源)
 *     - 模式 = ERoomMatchMode::Melee  → 攻方/守方 (刀战)
 *     - 模式 = ERoomMatchMode::Zombie → 母体阵营/人类阵营 (生化)
 *     - CachedMatchMode 字段缓存当前模式, 避免每次读 GS
 *     - 阵营名与玩家数据 (FPlayerSnapshot.FactionTag) 完全解耦 — UI 文案不污染数据层
 *
 * 架构理念:
 * 1. 单一数据源: URoomStateService (CQRS 读取端, View 不感知 PlayerState/AIController)
 * 2. 真人/AI 双轨制: 同一种 Snapshot 结构, 不同数据源 (大厂原则)
 * 3. RPC 链路: 服务器修改 Replicated 字段 → 引擎自动 Replicate → 客户端 OnRep → UI 刷新
 * 4. 排名计算: RefreshRanksInContainer (按击杀数稳定排序 + 标准竞赛排名 1/2/2/4)
 * 5. 模式分支: 单一函数 namespace ScoreboardFactionNames::GetXxxLabel 集中所有 UI 文案
 */
UCLASS()
class METALSLUG01_API UScoreboardWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// ==========================================
	// 1. 公共接口（供外部调用）
	// ==========================================

	/**
	 * 【v202.0 大厂架构】刷新所有队伍的玩家+AI 排行信息
	 * 数据源: URoomStateService::GetFactionSnapshotsWithAI (统一真人+AI)
	 *
	 * 【v215 大厂架构重构 — 增量更新 (零破坏性清理)】
	 *   - 历史 (v22-v213): 内部调 ClearScoreboard() 把所有 entry 删除重建 → 闪烁
	 *   - 新 (v215):
	 *     * 调 UpdateOrCreateEntryFromSnapshot 增量更新已有 entry
	 *     * 新玩家自动添加, 已离开玩家自动删除 (基于 FrozenSnapshots / 现拉快照对比)
	 *   - 大厂原则 — 0 兜底: 不再 ClearChildren, 永远保留已有 entry (除非玩家真的离开)
	 *
	 * 【v229.x v2 大厂架构重构 — 排名权威化 v2 (按得分排 + 严格唯一)】
	 *   - 末尾调 RefreshRanksInContainer 一次, 代替旧 SortEntriesByScore + UpdateAllRanks 两次调用
	 *   - 排序字段: Entry->GetScore() 走 FKdaScoring::Compute (业务规则 2026.08.16: 击杀+10/助攻+5/死亡-1)
	 *   - 排名规则: 严格唯一 (rank = i+1, 永远递增, 即便同分也不重号 — 用户业务规则 2026.08.16 明确)
	 *   - 同分时: 按 OriginalIndex 升序 = "先插入在前" (稳定排序已保证)
	 *   - 稳定排序: 同 Score 玩家按原容器位置 (int64 复合 key 编码)
	 */
	UFUNCTION(BlueprintCallable, Category = "Scoreboard")
	void RefreshScoreboard();

	/**
	 * 清空所有玩家数据
	 */
	UFUNCTION(BlueprintCallable, Category = "Scoreboard")
	void ClearScoreboard();

	// ==========================================
	// 2. 结算阶段接口
	// ==========================================

	/**
	 * 进入结算状态
	 * 时机: 由 GameHUD 在倒计时归零时调用
	 * @param AttackerKills 当局攻方击杀数
	 * @param DefenderKills 当局守方击杀数
	 */
	UFUNCTION(BlueprintCallable, Category = "Scoreboard|Settlement")
	void ShowRoundSettlement(int32 AttackerKills, int32 DefenderKills);

	/**
	 * 显示最终胜负结果
	 * 时机: 由 GameHUD 延迟 3 秒后调用
	 * @param AttackerWins 攻方胜局数
	 * @param DefenderWins 守方胜局数
	 *
	 * 【v215 大厂架构新增 — 冻结快照】
	 *   进入结算 → FreezeSnapshot() 一次性从 URoomStateService 拉所有玩家/AI 数据
	 *   冻结后所有刷新走 FrozenSnapshots (与房间连接解耦)
	 *   玩家中途退出/AI 死亡不影响结算页面
	 *   0 兜底 — 必须依赖 URoomStateService 拿数据, 拿不到就 Log Error + return, 不静默
	 */
	UFUNCTION(BlueprintCallable, Category = "Scoreboard|Settlement")
	void ShowFinalResult(int32 AttackerWins, int32 DefenderWins);

	/**
	 * 隐藏结算控件，返回纯计分板状态
	 */
	UFUNCTION(BlueprintCallable, Category = "Scoreboard|Settlement")
	void HideSettlementOverlay();

	// ==========================================
	// 【v216 大厂架构新增】3.5 跨地图结算快照入口
	// ==========================================

	/**
	 * 【v216 大厂架构新增】从 USettlementSnapshotSubsystem 拉快照并应用到本 Widget
	 *
	 * 单一入口 — 替代旧的 ShowRoundSettlement + ShowFinalResult 双调用
	 *
	 * 大厂架构 (v216 — 切图到 L_Login):
	 *   - 玩家进入结算 → 服务器 MulticastEnterSettlement_Implementation 写快照 + OpenLevel(L_Login)
	 *   - L_Login 上 GameFlowSubsystem 检测 EMatchState::SettlementPage → UIViewService ShowPanel(SettlementPanel)
	 *   - UIViewService 创建 UScoreboardWidget → 调 ApplySnapshot()
	 *   - ApplySnapshot 从 USettlementSnapshotSubsystem::ConsumeSnapshot 拉快照 → FreezeSnapshot 到本地
	 *   - 显示 Border + 文本 + Button (BindWidget 已迁移到本 Widget)
	 *
	 * 0 兜底:
	 *   - ConsumeSnapshot 失败 → Log Error + return, 不静默
	 *   - InSnapshot.bIsValid=false → Log Error + return, 不静默创建空 UI
	 *
	 * @param InSnapshot 完整结算快照 (从 USettlementSnapshotSubsystem 拉)
	 */
	UFUNCTION(BlueprintCallable, Category = "Scoreboard|Settlement")
	void ApplySnapshot(const FFinalSettlementSnapshot& InSnapshot);

protected:
	// ==========================================
	// 3. 生命周期
	// ==========================================

	/**
	 * 初始化
	 */
	virtual bool Initialize() override;

	/**
	 * Widget 构造完毕并加入视口后调用
	 *
	 * 【v215 大厂架构重构 — 事件订阅】
	 *   - 历史 (v22-v213): 只靠 NativeTick 0.5s 拉取 (高 CPU 开销 + 闪烁)
	 *   - 新 (v215):
	 *     * 订阅 URoomStateService::OnPlayerSnapshotsChanged (Dynamic delegate)
	 *     * 收到事件就 RefreshScoreboard 增量更新
	 *     * 拉取流 (Tick) 降级为弱兜底 (5s 周期, 检测漏拉)
	 *   - 大厂原则 — 事件优先: 主要靠事件流, 拉取流只兜底事件丢失
	 */
	virtual void NativeConstruct() override;

	/**
	 * Widget 销毁时清理订阅
	 *
	 * 【v215 大厂架构新增 — 防内存泄漏】
	 *   必须从 URoomStateService 解绑, 否则 Widget 被销毁后事件还能触发到, 引起野指针
	 */
	virtual void NativeDestruct() override;

	/**
	 * 【v215 大厂架构重构 — 事件驱动优先, 拉取弱兜底】
	 * 大厂原则:
	 *   - 主要刷新靠 OnPlayerSnapshotsChanged 事件触发 RefreshScoreboard
	 *   - Tick 只用于兜底 (5s 周期), 检测事件流丢失
	 *   - 删除 0.5s 高频轮询 (历史版本罪魁祸首, 触发闪烁)
	 */
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	// ==========================================
	// 4. UI 组件绑定
	// ==========================================

	/**
	 * 攻方玩家列表容器
	 */
	UPROPERTY(meta = (BindWidget))
	UVerticalBox* VB_AttackerTeam;

	/**
	 * 守方玩家列表容器
	 */
	UPROPERTY(meta = (BindWidget))
	UVerticalBox* VB_DefenderTeam;

	/**
	 * 【v203.0 大厂架构新增】攻方阵营标题
	 * - 刀战模式: "攻方"
	 * - 生化模式: "母体阵营"
	 */
	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* Text_AttackerTeamTitle;

	/**
	 * 【v203.0 大厂架构新增】守方阵营标题
	 * - 刀战模式: "守方"
	 * - 生化模式: "人类阵营"
	 */
	UPROPERTY(meta = (BindWidgetOptional))
	UTextBlock* Text_DefenderTeamTitle;

	/**
	 * 结算覆盖层 - 当局攻方击杀数
	 */
	UPROPERTY(meta = (BindWidget))
	UTextBlock* Text_Settlement_AttackerKills;

	/**
	 * 结算覆盖层 - 当局守方击杀数
	 */
	UPROPERTY(meta = (BindWidget))
	UTextBlock* Text_Settlement_DefenderKills;

	/**
	 * 结算覆盖层 - 攻方胜利/平局文字
	 */
	UPROPERTY(meta = (BindWidget))
	UTextBlock* Text_AttackerWinResult;

	/**
	 * 结算覆盖层 - 守方胜利/平局文字
	 */
	UPROPERTY(meta = (BindWidget))
	UTextBlock* Text_DefenderWinResult;

	// ==========================================
	// 【v216 大厂架构新增】结算覆盖板 + 返回大厅按钮 (从 GameHUDWidget 迁移)
	// ==========================================
	//
	// 大厂原则 — 单一真理源:
	//   - 历史 (v22-v215.x): Border/Button 属于 UGameHUDWidget, 结算控件分散在两个 Widget
	//   - 新 (v216): 结算覆盖板 + 返回大厅按钮完全归 UScoreboardWidget 所有
	//     * Border_SettlementOverlay 覆盖整个屏幕 (在 L_Login 上也覆盖)
	//     * Button_ReturnToLobby 点击 → 客户端直接调 TransitToState(MainLobby) (v216.3)
	//
	// 用户操作 (BP 蓝图侧, 2026.08.08 已完成):
	//   - WBP_ScoreboardWidget 加了 Border_SettlementOverlay (Border 子控件)
	//   - WBP_ScoreboardWidget 加了 Button_ReturnToLobby (Button 子控件)
	//   - WBP_GameHUDWidget 删了这两个控件
	//
	// C++ 侧对应: 这两个 BindWidget 字段 + OnReturnToLobbyClicked UFUNCTION
	// ==========================================

	/** 结算覆盖板 Border (从 GameHUDWidget 迁移, 跨地图持久) */
	UPROPERTY(meta = (BindWidget))
	UBorder* Border_SettlementOverlay;

	/** 返回大厅按钮 (从 GameHUDWidget 迁移, 跨地图持久) */
	UPROPERTY(meta = (BindWidget))
	UButton* Button_ReturnToLobby;

	/**
	 * 【v216 大厂架构新增】返回大厅按钮点击回调 (从 GameHUDWidget 迁移)
	 *
	 * 【v216.3 大厂架构再修正】客户端直接调 TransitToState, 不用 RequestStateOnNextLoad
	 *   - v216 历史: 走 RPC 链路 ScoreboardWidget → ARoomPlayerController::Server_SettlementReturnToLobby
	 *     → 服务器状态校验 → Client_OpenLobbyFromSettlement → RequestStateOnNextLoad(MainLobby)
	 *   - v216.2 修正: 客户端直接调 RequestStateOnNextLoad(MainLobby) (去掉 RPC)
	 *   - v216.2 BUG (Session1.txt 12.19.04 验证): 按钮"点了有响应", 但状态没切!
	 *     * 玩家已在 L_Login 上, 没有 OpenLevel → PostLoadMapWithWorld 不会触发
	 *     * RequestStateOnNextLoad 预约的状态永远不会被消费
	 *     * 玩家卡在 SettlementPage 状态
	 *   - v216.3 真正修复: 改用 TransitToState(MainLobby) (立即切状态 + 广播, 不需要 OpenLevel)
	 *     * GameFlowSubsystem::HandleStateEntry(MainLobby) case: 不跳转地图 (单地图常驻模式)
	 *     * 只广播 OnStateChanged → UIViewService 自动 ShowPanel(LANRoomPage)
	 *     * 0 网络往返, 0 地图跳转, 跨 PC 类型工作
	 *
	 * 大厂原则 — RequestStateOnNextLoad vs TransitToState 语义区分:
	 *   - RequestStateOnNextLoad: "预约到下一张地图" — 配合 OpenLevel 使用, 在 PostLoadMapWithWorld 消费
	 *   - TransitToState: "立即切状态 + 广播" — 单地图常驻模式专用, 已在目标地图上时使用
	 *   - 玩家在 L_Login 上从 SettlementPage 切到 MainLobby → 已在目标地图 → TransitToState
	 *
	 * 大厂原则 — 0 兜底:
	 *   - OwningPlayer 为空 → Log Error + return
	 *   - GameInstance 拿不到 → Log Error + return
	 *   - GameFlowSubsystem 拿不到 → Log Error + return
	 *   - 当前状态不是 SettlementPage → Log Error + return (理论上不会发生)
	 *
	 * 旧路径 (v216, 已删除):
	 *   - OnClicked → ARoomPlayerController::Server_SettlementReturnToLobby RPC
	 *   - 服务器调 Client_OpenLobbyFromSettlement RPC
	 *   - 客户端 RequestStateOnNextLoad(EMatchState::MainLobby)
	 *   - 两个 RPC 已全部删除 (RoomPlayerController.h), 0 网络往返
	 */
	UFUNCTION()
	void OnReturnToLobbyClicked();

	/**
	 * 标记当前是否为结算状态
	 */
	bool bIsInSettlementState = false;

	/**
	 * 【v203.0 大厂架构新增】缓存当前游戏模式
	 *   - 来源: NativeConstruct 时读 GS->CurrentMatchMode (Replicated)
	 *   - 后续 ShowRoundSettlement / ShowFinalResult / RefreshTeamTitles 直接读缓存, 不重复查 GS
	 *   - 大厂原则: 避免每帧读 GS, 减少开销 + 保证模式稳定不变 (Replicated 字段一局不变)
	 *
	 * 默认值 Melee: 假设最常见模式, 如果 GS 没就绪 (客户端首帧) 至少显示刀战文案, 不空白
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Scoreboard|State")
	ERoomMatchMode CachedMatchMode = ERoomMatchMode::Melee;

	// ==========================================
	// 【v215 大厂架构新增】冻结快照字段
	//   大厂原则: 结算页面 = 一次性快照, 后续变化不影响显示
	// ==========================================

	/**
	 * 【v215 大厂架构新增】是否已冻结
	 *   - false: 正常事件驱动刷新 (从 URoomStateService 实时拉)
	 *   - true:  只读 FrozenSnapshots (冻结后房间连接已断, 不再更新)
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Scoreboard|State")
	bool bIsFrozen = false;

	/**
	 * 【v215 大厂架构新增】冻结时的数据快照
	 *   - 一次性拉 URoomStateService → 永久保留
	 *   - 刷新走 FrozenSnapshots, 与 URoomStateService 完全解耦
	 *   - 0 兜底: 冻结后所有读路径只查这里, 不再回 URoomStateService
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Scoreboard|State")
	TArray<FPlayerSnapshot> FrozenSnapshots;

	/**
	 * 【v215 大厂架构新增】冻结时的队伍击杀数
	 *   - 同 FrozenSnapshots 一次性冻结
	 *   - 冻结后 ShowRoundSettlement / ShowFinalResult 也只读这个缓存
	 */
	int32 FrozenAttackerKills = 0;
	int32 FrozenDefenderKills = 0;
	int32 FrozenAttackerWins = 0;
	int32 FrozenDefenderWins = 0;

	// ==========================================
	// 5. 子 Widget 配置
	// ==========================================

	/**
	 * 计分板条目 Widget 类
	 * 用途: 在蓝图中配置 WBP_ScoreboardEntryWidget
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Scoreboard|Config")
	TSubclassOf<UScoreboardEntryWidget> ScoreboardEntryWidgetClass;

private:
	// ==========================================
	// 6. 私有辅助
	// ==========================================

	/**
	 * 【v203.0 大厂架构新增】根据当前模式刷新阵营标题 TextBlock
	 *   - 刀战: 攻方 / 守方
	 *   - 生化: 母体阵营 / 人类阵营
	 *
	 * 【v229.x 修复】调用时机扩展:
	 *   - NativeConstruct (初始化,首次显示需要)
	 *   - ShowRoundSettlement / ShowFinalResult (结算时同步刷新)
	 *   - ApplySnapshot (跨地图场景)
	 *   - **RefreshScoreboard 入口检测 CachedMatchMode 变化** (Tab 打开时模式已切场景)
	 *
	 * 大厂原则: 单一入口, 不允许在多处各自拼文案
	 * 大厂原则 — 集中调度: RefreshScoreboard 入口检测 = 运行时模式变化的唯一响应点
	 */
	void RefreshTeamTitles();

	/**
	 * 【v203.0 大厂架构新增】从 GS 读取当前模式并更新 CachedMatchMode
	 *   - 真理源: GS->CurrentMatchMode (Replicated)
	 *   - 模式未变化时不做任何事 (避免 no-op 重复读)
	 */
	void RefreshCachedMatchMode();

	/**
	 * 【v202.0 大厂架构】从 URoomStateService 获取所有玩家+AI 数据并刷新 UI
	 * 单一真理源: URoomStateService::GetFactionSnapshotsWithAI (替代旧直接遍历 GS->PlayerArray)
	 */
	void RefreshFromRoomStateService();

	/**
	 * 【v202.0 大厂架构】根据 FPlayerSnapshot 更新或创建条目
	 * 大厂原则: View 只读 POJO 数据, 不感知 ARoomPlayerState / ABaseAIController
	 *
	 * @param Snapshot 玩家快照 (bIsAI=false 真人, bIsAI=true AI)
	 */
	void UpdateOrCreateEntryFromSnapshot(const FPlayerSnapshot& Snapshot, bool bIsLocalPlayer);

	/**
	 * 创建单个快照对应的条目 Widget
	 * @param Snapshot 玩家快照
	 * @param bIsAttacker 是否攻方 (决定添加到哪个容器)
	 * @return 创建的 UScoreboardEntryWidget
	 */
	UScoreboardEntryWidget* CreateEntryWidgetFromSnapshot(const FPlayerSnapshot& Snapshot, bool bIsAttacker);

	/**
	 * 【v229.x 大厂架构重构】增量删除已退出的玩家 entry
	 *
	 * 大厂原则:
	 *   - RefreshScoreboard 末尾调此函数, 对比 ActiveSnapshots 清理已退玩家
	 *   - 不缓存 entry 指针 (Widget 可能被外部清空), 每次动态遍历
	 *   - O(N) 一次遍历, 用 TSet/FName 做集合判断 (替代原 N² ContainsByPredicate)
	 *
	 * @param VerticalBox 容器 (VB_AttackerTeam 或 VB_DefenderTeam)
	 * @param ActiveSnapshots 当前活跃快照列表 (来自 GetActiveSnapshots)
	 */
	void RemoveStaleEntries(UVerticalBox* VerticalBox, const TArray<FPlayerSnapshot>& ActiveSnapshots);

	/**
	 * 【v229.x v2 大厂架构重构】单阵营内排名刷新 (排序 + 排名赋值) — 替代旧 SortEntriesByScore + UpdateAllRanks 拆分
	 *
	 * 历史 (v22-v228) 反模式:
	 *   - 拆成 2 个函数 SortEntriesByScore + UpdateAllRanks,调用方必须两次调用
	 *   - Sort 阶段重读 ActiveSnapshots 找 Score (N², 重复架构, 因为 UpdateOrCreateEntryFromSnapshot 已经 SetKDA)
	 *   - TArray::Sort 用 introsort (不稳定), 同分玩家顺序跳变
	 *   - UpdateAllRanks 按位置 1,2,3 累加, 同分玩家挤占名次 (1,2,3 全是 100 分,排第 4 的 80 分玩家显示 "4")
	 *
	 * 业务规则 (用户 2026.08.16 明确):
	 *   - 排名按"得分"排序: 击杀 +10 / 助攻 +5 / 死亡 -1 (走 FKdaScoring::Compute)
	 *   - 排名严格唯一 (1, 2, 3, 4 即便同分 — 用户规则: "排名不能重复数字")
	 *   - 同分时: 先插入在前 (按 OriginalIndex 升序, 稳定排序已保证)
	 *   - 排名只显示数字 (无 emoji, 无特殊字符)
	 *
	 * 新 (v229.x v2) 大厂架构:
	 *   - 单一入口: 排序 + 排名赋值 一气呵成, RefreshScoreboard 末尾一处调用即可
	 *   - 排序字段 = 得分 (Score = Kills*10 + Assists*5 - Deaths, 走 FKdaScoring::Compute)
	 *   - 稳定排序: 同分玩家按"原容器位置"作为 tiebreaker (大厂原则 — 视觉稳定,不抖)
	 *   - 严格唯一排名 (Strict Unique Ranking):
	 *     例: scores = [10, 8, 8, 5] → ranks = [1, 2, 3, 4] (用户业务规则: 永远不重号)
	 *     而不是 [1, 2, 2, 4] (旧 FIFA/LOL 标准竞赛排名 — 用户明确否决)
	 *   - 单一真理源: Entry Widget 自己持 CachedKills/Deaths/Assists (SetKDA 已写),
	 *     本函数直接读 Entry->GetScore() (FKdaScoring 现算), 不回查 ActiveSnapshots (消除重复架构)
	 *
	 * 算法 O(N log N):
	 *   1. 收集 (Position, Score, Widget) 三元组
	 *   2. stable sort: ① Score 降序 ② Position 升序 (tied 时入位在前者优先)
	 *   3. 增量重排 (RemoveChild + InsertChild, 不重建 Widget)
	 *   4. 严格唯一排名赋值 (rank = i + 1, 永远递增, 同分玩家也拿不同 rank)
	 *
	 * @param VerticalBox 容器 (VB_AttackerTeam 或 VB_DefenderTeam)
	 */
	void RefreshRanksInContainer(UVerticalBox* VerticalBox);

	/**
	 * 获取本地玩家名 (用于高亮)
	 * @return 当前玩家的 PlayerName
	 */
	FString GetLocalPlayerName() const;

	// ==========================================
	// 【v215 大厂架构新增】事件订阅与冻结
	// ==========================================

	/**
	 * 【v215 大厂架构新增】URoomStateService OnPlayerSnapshotsChanged 事件回调
	 *   - Dynamic Multicast Delegate 必须 UFUNCTION
	 *   - 触发 RefreshScoreboard 增量更新
	 *   - 必须配对 NativeDestruct 中 RemoveDynamic
	 */
	UFUNCTION()
	void HandlePlayerSnapshotsChanged();

	/**
	 * 【v215 大厂架构新增】订阅 / 解绑 URoomStateService 事件
	 *   - SubscribeScoreboardEvents 在 NativeConstruct 调一次
	 *   - UnsubscribeScoreboardEvents 在 NativeDestruct 调一次
	 *   - 必须配对, 否则 Widget 销毁后野指针
	 */
	void SubscribeScoreboardEvents();
	void UnsubscribeScoreboardEvents();

	/**
	 * 【v215 大厂架构新增】冻结数据快照
	 *   - 一次性从 URoomStateService 拉所有玩家/AI 数据到 FrozenSnapshots
	 *   - 同时冻结 AttackerKills/DefenderKills/AttackerWins/DefenderWins
	 *   - 标记 bIsFrozen = true, 之后所有刷新只读 FrozenSnapshots
	 *   - 0 兜底: URoomStateService 拿不到 → Log Error + return, 不冻结
	 *
	 * 时机: 由 ShowFinalResult 在结算开始时调用一次
	 */
	void FreezeSnapshot();

	/**
	 * 【v215 大厂架构新增】Tick 弱兜底累积时间
	 *   - 大厂原则: 事件流优先, Tick 只兜底
	 *   - 5s 周期检查一次是否需要 RefreshScoreboard
	 */
	float TickFallbackAccumulator = 0.0f;

	/**
	 * 【v215 大厂架构新增】快照 ID 生成
	 *   - SnapshotId = PlayerName + bIsAI 拼接, 唯一标识一个玩家/AI
	 *   - 用作 entry 索引, 增量更新时定位已有 entry
	 */
	static FString MakeSnapshotId(const FPlayerSnapshot& Snapshot);

	/**
	 * 【v215 大厂架构新增】从容器中按 SnapshotId 查找 entry
	 *   - 大厂原则: 不缓存 entry 指针 (Widget 可能被外部清空)
	 *   - 每次通过 SnapshotId 动态查找, 避免野指针
	 */
	UScoreboardEntryWidget* FindEntryById(UVerticalBox* VerticalBox, const FString& SnapshotId) const;

	/**
	 * 【v229.x v2.1 大厂架构新增】单一入口: 把 FPlayerSnapshot 字段灌入 Entry Widget
	 *   - 替代旧 (v22-v229.x v2): UpdateOrCreateEntryFromSnapshot 和 CreateEntryWidgetFromSnapshot
	 *     各自手动 SetPlayerName / SetScore / SetKDA / SetIsCurrentPlayer (DRY 违反)
	 *   - 新增字段 (例如未来 SetAvatar) 只需要在这里加一次, 2 个创建/更新路径自动同步
	 *   - 0 兜底: EntryWidget == nullptr → return (不抛、不 Log, 由调用方处理)
	 */
	void ApplySnapshotToEntry(UScoreboardEntryWidget* EntryWidget,
	                          const FPlayerSnapshot& Snapshot,
	                          bool bIsLocalPlayer) const;

	/**
	 * 【v215 大厂架构新增】从容器中删除指定 SnapshotId 的 entry
	 *   - 用于玩家中途退出时清理
	 *   - 0 兜底: 找不到 entry 静默忽略 (已经删过了)
	 */
	void RemoveEntryById(UVerticalBox* VerticalBox, const FString& SnapshotId);

	/**
	 * 【v215 大厂架构新增】获取当前应使用的数据源
	 *   - 冻结后返回 FrozenSnapshots (const 引用)
	 *   - 未冻结时实时拉 URoomStateService(真人) + RoomGameState(AI),并缓存到 CachedLiveSnapshots
	 *   - 因为要写缓存 (CachedLiveSnapshots), 此函数不能是 const
	 *   - 0 兜底: URoomStateService 拿不到 → Log Error + 返回空数组
	 *
	 * 【v223.0 大厂架构重构】AI 数据源切换:
	 *   - 老路径: URoomStateService::GetFactionSnapshotsWithAI → TActorIterator<ABaseAIController>
	 *   - 新路径: RoomGameState->GetBattleAIEntries(FFactionSnapshotEntry Replicated 列表)
	 *   - 真人玩家: 仍走 URoomStateService (PlayerArray 复制稳定)
	 *   - 单点转换: ConvertBattleAIEntryToSnapshot 把 FFactionSnapshotEntry 转 FPlayerSnapshot
	 *
	 * 单一入口, 调用方不感知冻结状态
	 */
	TArray<FPlayerSnapshot>& GetActiveSnapshots();

	/**
	 * 【v223.0 大厂架构新增】FFactionSnapshotEntry → FPlayerSnapshot 转换
	 *
	 * 单一真理源 (镜面 Settlement v217 路径):
	 *   - FFactionSnapshotEntry 是 Server-Authoritative 写入的 Replicated 数据
	 *   - FPlayerSnapshot 是 UI 内部使用的数据
	 *   - 转换函数集中在此, 避免分散拼凑
	 *
	 * 0 兜底:
	 *   - Entry.DisplayName 为空 → 返回空 Snapshot (RefreshScoreboard 会 Log Error 跳过)
	 *   - FactionTagName 解析失败 → 显式 Log Error + 用 Offense 默认 (不静默)
	 *
	 * @param Entry 来自 RoomGS->GetBattleAIEntries 的 Replicated Entry
	 * @return FPlayerSnapshot (bIsAI=true, 含 PlayerName 前缀)
	 */
	FPlayerSnapshot ConvertBattleAIEntryToSnapshot(const FFactionSnapshotEntry& Entry);

	/**
	 * 【v215 大厂架构新增】缓存当前快照 (可变缓存, 用于增量删除已退玩家)
	 *   - 未冻结时缓存, RefreshScoreboard 通过它比对删除
	 *   - 冻结后由 FrozenSnapshots 取代
	 */
	UPROPERTY()
	TArray<FPlayerSnapshot> CachedLiveSnapshots;
};
