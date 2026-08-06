// 版权声明：在项目设置的描述页面填写您的版权信息。

#pragma once

// ==========================================
// 头文件包含说明
// ==========================================
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "Data/Enums/RoomEnums.h" // 引入 ERoomMatchMode
// 【v202.0 大厂架构】不再 include RoomGameState / RoomPlayerState (走快照路径)
#include "ScoreboardWidget.generated.h"

// 前向声明
class UVerticalBox;
class UScoreboardEntryWidget;
// 【v202.0 大厂架构】删除 ARoomPlayerState 直接引用 (走 FPlayerSnapshot 快照路径, View 不感知 PlayerState)
// class ARoomPlayerState;
class UTextBlock;
class URoomStateService;
struct FPlayerSnapshot;


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
 * 4. 排名计算: SortEntriesByScore + UpdateAllRanks (与真人统一)
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
	 */
	UFUNCTION(BlueprintCallable, Category = "Scoreboard|Settlement")
	void ShowFinalResult(int32 AttackerWins, int32 DefenderWins);

	/**
	 * 隐藏结算控件，返回纯计分板状态
	 */
	UFUNCTION(BlueprintCallable, Category = "Scoreboard|Settlement")
	void HideSettlementOverlay();

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
	 */
	virtual void NativeConstruct() override;

	/**
	 * 【v202.0 大厂架构】Tick 周期 — 兜底轮询刷新
	 *
	 * 大厂原则 — 事件 + 拉取 双轨制:
	 *   - 事件流: GameHUDWidget::OnTeamKillCountUpdated 等会触发 ShowScoreboard → RefreshScoreboard
	 *   - 拉取流: 本 Tick 0.5s 周期主动拉 URoomStateService 快照 — 兜底事件流丢失
	 *
	 * 必要性 (用户反馈):
	 *   用户报告 ScoreboardWidget 不显示 AI — 即使刷新链路补全, 客户端 widget OnRep 触发
	 *   时机不一定对齐 (Tab 打开后才订阅事件, 已错过)
	 *   拉取流每秒 1 次, 主动同步保证 widget 永远显示最新
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
	 * 调用时机: NativeConstruct (初始化) + ShowRoundSettlement / ShowFinalResult (结算时同步刷新)
	 * 大厂原则: 单一入口, 不允许在多处各自拼文案
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
	 * 对指定 VerticalBox 中的条目按得分排序
	 */
	void SortEntriesByScore(UVerticalBox* VerticalBox);

	/**
	 * 更新所有条目的排名显示
	 */
	void UpdateAllRanks(UVerticalBox* VerticalBox);

	/**
	 * 获取本地玩家名 (用于高亮)
	 * @return 当前玩家的 PlayerName
	 */
	FString GetLocalPlayerName() const;
};
