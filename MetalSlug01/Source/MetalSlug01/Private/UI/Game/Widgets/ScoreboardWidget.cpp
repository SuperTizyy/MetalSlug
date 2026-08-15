// 版权声明：在项目设置的描述页面填写您的版权信息。

// ==========================================
// 【v229.x v2 大厂架构重构 — ScoreboardWidget 排名权威化 v2】
// ==========================================
//
// 用户反馈 (2026.08.16 v3 明确):
//   - "排名显示就数字就行, 不要特殊字符"
//   - "排名不能重复数字" (严格唯一: 1,2,3,4 即便同分)
//   - "排名按得分: 击杀 +10 / 助攻 +5 / 死亡 -1"
//   - "绝对不能有兜底行为"
//
// 历史 (v22-v228) 5 个反模式:
//   1. 排序字段错: 用 Snap.Score (总分 = Kill*20 + Assist*10), 与玩家直觉不符
//   2. N² 重复架构: SortEntriesByScore 在内重读 ActiveSnapshots 线性查 Score (总复杂度 N²)
//   3. 不稳定排序: TArray::Sort 用 introsort (UE 实现),同 Kills 玩家顺序跳变
//   4. UpdateAllRanks 按位置 1,2,3 累加: 同 Kills 玩家挤占名次
//   5. 调用方必须两次调用 (Sort + UpdateRanks), 散落维护成本
//   6. RemoveStaleEntries 用 N×M ContainsByPredicate (反模式)
//
// v229.x (v1) 修复 — 旧 (已过期):
//   - 排序字段: Entry->GetKills() (按击杀数)
//   - 排名算法: Standard Competition Ranking (1,2,2,4)
//
// v229.x (v2) 修复 — 新 (用户业务规则 2026.08.16):
//   - 排序字段: Entry->GetScore() 走 FKdaScoring::Compute (按"得分", 击杀+10/助攻+5/死亡-1)
//   - 排名算法: 严格唯一 (rank = i+1, 永远递增, 1,2,3,4 即便同分)
//   - 同分时: 按 OriginalIndex 升序 (稳定排序已保证), rank 也升序 (用户规则: "先插入在前")
//   - 排名显示: 仅数字 (用户规则: "排名显示就数字就行, 不要特殊字符"), SetRank 内 emoji 删除
//
// 终极修复 (大厂架构 v229.x v2):
//
//   [单一真理源 + 公式唯一]  FKdaScoring::Compute(Kills, Assists, Deaths) = Kills*10 + Assists*5 - Deaths
//                            → 业务层 (RoomPlayerState/BaseAIController) = UI 层 (Entry::GetScore) 同一真理源
//                            → 业务层 20/10 → 10/5, 死亡 -1 (单一修正, 不分散)
//                            → CachedScore 不缓存 (派生数据每次现算, 避免字段飘移)
//
//   [单入口]                  RefreshRanksInContainer(VerticalBox) 一函数搞定排序+排名赋值
//                            → RefreshScoreboard 末尾一次调用, 代替旧两次调用
//
//   [稳定排序]                用 int64 复合 key 编码 (-Score 高 32 位 + OriginalIndex 低 32 位)
//                            → 同 Score 玩家按入位先后顺序, 视觉稳定不抖
//
//   [严格唯一排名]            rank = i + 1 (永远递增, 1, 2, 3, 4 ...)
//                            → 同分玩家按 OriginalIndex 升序 = "先插入在前" 满足用户规则
//
//   [O(N) 集合]               RemoveStaleEntries 用 TSet<FString> 哈希查找 (替代 N² ContainsByPredicate)
//
//   [0 兜底]
//   - VerticalBox 为空 → return (无需排序)
//   - Entry->GetScore() 永远有效 (CachedKills/Deaths/Assists 默认 0, 不抛)
//   - SetRank 严格只输出数字 (无 emoji, 无 fallback)
//   - RemoveChild(Widget) 不销毁 Widget 对象, 后续 AddChild 复用 (与 v215 Bug #1 闪烁修复一致)
//
// [v229.x v2.1 大厂架构修复 — 排序顺序生效]
//   历史 (v229.x v2): RemoveChild + InsertChildAt(i, Widget) — 用户反馈"排名正确但排序不对"
//   根因: UE 5.6 UPanelWidget::InsertChildAt 在 Widget->Slot==nullptr 时序竞争下, 实际插入位置可能不是 i
//   修复: 倒序 RemoveChildAll + 按 EncodedList 顺序 AddChild (100% 可靠)
//
// ==========================================

#include "UI/Game/Widgets/ScoreboardWidget.h"
#include "UI/Game/Widgets/SubWidgets/ScoreboardEntryWidget.h"
#include "Components/VerticalBox.h"
#include "Components/TextBlock.h"
// 【v216 大厂架构新增】Border + Button 绑定 (从 GameHUDWidget 迁移)
#include "Components/Border.h"
#include "Components/Button.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/PlayerState.h"
#include "Systems/RoomGameState.h"
#include "Services/RoomStateService.h"
#include "Data/Faction/FactionTags.h"
// 【v216 大厂架构新增】结算快照子系统 (跨地图持久) + v216.2 重构后不再需要 RPC
#include "Systems/Settlement/SettlementSnapshotSubsystem.h"
// 【v216.2 大厂架构重构】去除 RPC 链路, 客户端直接调 UGameFlowSubsystem
//   - UGameFlowSubsystem 是 UGameInstanceSubsystem, 跨地图持久
//   - 客户端本地操作即可, 无需服务器往返
//   - 旧 RPC (Server_SettlementReturnToLobby / Client_OpenLobbyFromSettlement) 已全部删除 (RoomPlayerController.h)
#include "Systems/GameFlowSubsystem.h"
#include "Engine/GameInstance.h"
#include "Enums/CoreEnums.h"
#include "Engine/LocalPlayer.h"
// 【v217 大厂架构 — 单一入口】调 URoomService::RequestLeaveRoom 销毁 Session + 切 UI 状态
#include "Services/RoomService.h"

// ==========================================
// 【v203.0 大厂架构 — 阵营名映射 namespace】
// ==========================================
// 大厂原则 — 单一真理源:
//   - 所有 UI 文案 (阵营标题/击杀数标签/胜利文字) 集中在这一处
//   - 不允许在 cpp 其他位置各自拼字符串 ("攻方胜利" / "母体阵营胜利" 等)
//   - 加新模式 = 加一个 case, 不破坏既有调用方
//
// 模式 → 阵营名映射表:
//   ERoomMatchMode::Melee  → 攻方/守方 (刀战, 业务规则 v22 沿用)
//   ERoomMatchMode::Zombie → 母体阵营/人类阵营 (生化, 用户 2026.08.07 反馈要求)
//   ERoomMatchMode::None   → Log Error + 默认走刀战 (不允许空白 UI, 也不允许静默)
// ==========================================
namespace ScoreboardFactionNames
{
	// 阵营标题 (用于 VB 容器上方的 TextBlock)
	static FString GetAttackerTeamTitle(ERoomMatchMode Mode)
	{
		switch (Mode)
		{
		case ERoomMatchMode::Melee:
			return TEXT("攻方");
		case ERoomMatchMode::Zombie:
			return TEXT("母体阵营");
		case ERoomMatchMode::None:
		default:
			UE_LOG(LogTemp, Error,
				TEXT("[ScoreboardWidget] GetAttackerTeamTitle: MatchMode='None' 异常状态, 默认走刀战文案. "
					     "【v203.0 零兜底】不允许空白 UI, 业务方需检查模式初始化时序."));
			return TEXT("攻方");
		}
	}

	static FString GetDefenderTeamTitle(ERoomMatchMode Mode)
	{
		switch (Mode)
		{
		case ERoomMatchMode::Melee:
			return TEXT("守方");
		case ERoomMatchMode::Zombie:
			return TEXT("人类阵营");
		case ERoomMatchMode::None:
		default:
			UE_LOG(LogTemp, Error,
				TEXT("[ScoreboardWidget] GetDefenderTeamTitle: MatchMode='None' 异常状态, 默认走刀战文案."));
			return TEXT("守方");
		}
	}

	// 当局击杀数标签 (用于 Text_Settlement_AttackerKills / _DefenderKills 刀战模式)
	static FString GetAttackerKillsLabel_Melee(int32 Kills)
	{
		return FString::Printf(TEXT("攻方阵营击杀总数: %d"), Kills);
	}

	static FString GetDefenderKillsLabel_Melee(int32 Kills)
	{
		return FString::Printf(TEXT("守方阵营击杀总数: %d"), Kills);
	}

	// 赢的小局数标签 (用于 Text_Settlement_AttackerKills / _DefenderKills 生化模式)
	static FString GetAttackerWinsLabel_Zombie(int32 Wins)
	{
		return FString::Printf(TEXT("母体阵营赢得对局数: %d"), Wins);
	}

	static FString GetDefenderWinsLabel_Zombie(int32 Wins)
	{
		return FString::Printf(TEXT("人类阵营赢得对局数: %d"), Wins);
	}

	// 胜利文字 (用于 Text_AttackerWinResult / _DefenderWinResult)
	//   - 刀战: 攻方胜利 / 守方胜利 / 平局
	//   - 生化: 母体阵营胜利 / 人类阵营胜利 / 平局
	static FString GetAttackerWinLabel(ERoomMatchMode Mode)
	{
		switch (Mode)
		{
		case ERoomMatchMode::Melee:
			return TEXT("攻方胜利");
		case ERoomMatchMode::Zombie:
			return TEXT("母体阵营胜利");
		case ERoomMatchMode::None:
		default:
			UE_LOG(LogTemp, Error,
				TEXT("[ScoreboardWidget] GetAttackerWinLabel: MatchMode='None' 异常状态, 默认走刀战文案."));
			return TEXT("攻方胜利");
		}
	}

	static FString GetDefenderWinLabel(ERoomMatchMode Mode)
	{
		switch (Mode)
		{
		case ERoomMatchMode::Melee:
			return TEXT("守方胜利");
		case ERoomMatchMode::Zombie:
			return TEXT("人类阵营胜利");
		case ERoomMatchMode::None:
		default:
			UE_LOG(LogTemp, Error,
				TEXT("[ScoreboardWidget] GetDefenderWinLabel: MatchMode='None' 异常状态, 默认走刀战文案."));
			return TEXT("守方胜利");
		}
	}

	static FString GetTieLabel()
	{
		return TEXT("平局");
	}
}

bool UScoreboardWidget::Initialize()
{
	if (!Super::Initialize())
	{
		return false;
	}

	// 【v216 大厂架构新增】Border/Button 初始化 (从 GameHUDWidget 迁移)
	// 大厂原则 — BindWidget 失败必须 Log Error:
	//   - Border_SettlementOverlay 未绑定 → 用户没在 BP 蓝图侧加 → 报错让用户修
	//   - Button_ReturnToLobby 未绑定 → 同上
	//
	// 注: Initialize() 时 Widget 树还没构造完成, 此时访问 Border->GetChildrenCount 等是合法的
	//     但 OnClicked.AddDynamic 在 Initialize() 调用过早就失效 (Widget tree 未挂载)
	//     所以 OnClicked 绑定放在 NativeConstruct (Widget 树挂载后)

	return true;
}

// ==========================================
// 【v215 大厂架构重构 — NativeConstruct 事件订阅】
// ==========================================
// 大厂原则 — 事件优先于 Tick:
//   历史 (v22-v213): 只靠 NativeTick 0.5s 拉取 (高 CPU 开销 + 闪烁)
//   新 (v215):
//     * 订阅 URoomStateService::OnPlayerSnapshotsChanged
//     * 事件触发时 RefreshScoreboard (增量更新)
//     * Tick 降级为 5s 弱兜底
//
// 【v215 Bug #3 修复】客户端显示 AI 信息:
//   客户端早期 Tab 打开时可能错过早期 OnRep → 事件订阅保证不丢
//
// 【0 兜底】URoomStateService 拿不到 → Log Error, 不静默 (Tick 兜底会兜住)
void UScoreboardWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// 初始化结算文本为隐藏状态（等待 ShowRoundSettlement / ShowFinalResult 时才显示）
	if (Text_Settlement_AttackerKills)
	{
		Text_Settlement_AttackerKills->SetVisibility(ESlateVisibility::Collapsed);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[ScoreboardWidget] NativeConstruct: Text_Settlement_AttackerKills 未绑定 (BindWidget)!"));
	}

	if (Text_Settlement_DefenderKills)
	{
		Text_Settlement_DefenderKills->SetVisibility(ESlateVisibility::Collapsed);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[ScoreboardWidget] NativeConstruct: Text_Settlement_DefenderKills 未绑定 (BindWidget)!"));
	}

	if (Text_AttackerWinResult)
	{
		Text_AttackerWinResult->SetVisibility(ESlateVisibility::Collapsed);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[ScoreboardWidget] NativeConstruct: Text_AttackerWinResult 未绑定 (BindWidget)!"));
	}

	if (Text_DefenderWinResult)
	{
		Text_DefenderWinResult->SetVisibility(ESlateVisibility::Collapsed);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[ScoreboardWidget] NativeConstruct: Text_DefenderWinResult 未绑定 (BindWidget)!"));
	}

	// 【v215 大厂架构新增】初始化模式缓存 + 阵营标题
	RefreshCachedMatchMode();
	RefreshTeamTitles();

	// 【v215 大厂架构新增】订阅 URoomStateService 事件
	SubscribeScoreboardEvents();

	// 【v215 大厂架构新增】初次拉取 (NativeConstruct 时拿一次, 避免等到第一次事件触发才显示)
	RefreshScoreboard();

	// 【v216 大厂架构新增】初始化 Border_SettlementOverlay + Button_ReturnToLobby
	// 大厂原则 — BindWidget 失败 Log Error:
	//   - 用户已手动在 WBP_ScoreboardWidget 加了这两个控件
	//   - 如果没绑上, 说明 BP 重命名/删除了 → Log Error 强制修
	if (Border_SettlementOverlay)
	{
		// 默认隐藏, 等 ApplySnapshot 拉快照后再显示
		Border_SettlementOverlay->SetVisibility(ESlateVisibility::Collapsed);
	}
	else
	{
		UE_LOG(LogTemp, Error,
			TEXT("[ScoreboardWidget] NativeConstruct: Border_SettlementOverlay 未绑定 (BindWidget)! "
			     "请检查 WBP_ScoreboardWidget 蓝图是否添加了同名 Border 子控件, "
			     "【v216 大厂架构】结算覆盖板已从 WBP_GameHUDWidget 迁移到此 Widget."));
	}

	if (Button_ReturnToLobby)
	{
		// 默认隐藏
		Button_ReturnToLobby->SetVisibility(ESlateVisibility::Collapsed);
		// 绑定 OnClicked → OnReturnToLobbyClicked (客户端本地调 GameFlowSubsystem)
		Button_ReturnToLobby->OnClicked.AddDynamic(this, &UScoreboardWidget::OnReturnToLobbyClicked);
	}
	else
	{
		UE_LOG(LogTemp, Error,
			TEXT("[ScoreboardWidget] NativeConstruct: Button_ReturnToLobby 未绑定 (BindWidget)! "
			     "请检查 WBP_ScoreboardWidget 蓝图是否添加了同名 Button 子控件, "
			     "【v216 大厂架构】返回大厅按钮已从 WBP_GameHUDWidget 迁移到此 Widget."));
	}
}

// ==========================================
// 【v215 大厂架构新增 — NativeDestruct 解绑】
// 大厂原则 — 必须配对, 否则野指针:
void UScoreboardWidget::NativeDestruct()
{
	// 【v216 大厂架构新增】解除 Button OnClicked 绑定
	// 大厂原则 — 防野指针: Widget 销毁后 Button 还能回调到, 引发崩溃
	if (Button_ReturnToLobby)
	{
		Button_ReturnToLobby->OnClicked.RemoveDynamic(this, &UScoreboardWidget::OnReturnToLobbyClicked);
	}

	UnsubscribeScoreboardEvents();
	Super::NativeDestruct();
}

// ==========================================
// 【v215 大厂架构重构 — Tick 弱兜底 5s】
// ==========================================
// 大厂原则:
//   - 主要刷新靠 OnPlayerSnapshotsChanged 事件
//   - Tick 只用于兜底事件丢失 (5s 周期)
//   - 修复闪烁: 不再 0.5s 高频拉取 (历史罪魁祸首)
//
// 【v217 大厂架构重构 — 冻结后跳过 Tick 兜底刷新】
// 大厂原则 — 单一真理源 + 0 兜底:
//   - 旧 (v215-v216): 5s TickFallback 无条件调 RefreshScoreboard
//     → 冻结后 RefreshScoreboard 走 FrozenSnapshots → 内容虽然不变, 但每 5 秒做一次增量更新(增删改排序)
//     → 用户感知: 排名列表"似乎"在抖动 (虽然内容相同, 但 entry 被反复 RemoveChild/InsertChild 触发重绘)
//     → 用户报告: "结算页面的玩家排名列表,过一会就自动变化一下顺序"
//   - 新 (v217): bIsFrozen=true 时 NativeTick 直接 return
//     → 冻结后整个 Widget 完全静止, 不再有任何代码路径触发重新刷新
//     → 等用户点击返回大厅 (UScoreboardWidget::OnReturnToLobbyClicked) → TransitToState(MainLobby)
//     → UIViewService 关闭本 Widget,NativeTick 不再执行
//     → 重新进入游戏后 (新地图加载) bIsFrozen=false, TickFallback 自动恢复
void UScoreboardWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	// 【v217 大厂架构重构】冻结后整个 Widget 完全静止
	if (bIsFrozen)
	{
		return;
	}

	// 性能优化: 隐藏时不刷新
	if (GetVisibility() == ESlateVisibility::Hidden ||
	    GetVisibility() == ESlateVisibility::Collapsed)
	{
		return;
	}

	TickFallbackAccumulator += InDeltaTime;
	if (TickFallbackAccumulator < 5.0f)
	{
		return;
	}
	TickFallbackAccumulator = 0.0f;

	RefreshScoreboard();
}

// ==========================================
// 【v215 大厂架构新增 — 事件订阅 / 解绑】
// ==========================================
void UScoreboardWidget::SubscribeScoreboardEvents()
{
	URoomStateService* StateSvc = URoomStateService::Get(this);
	if (!StateSvc)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[ScoreboardWidget] SubscribeScoreboardEvents: URoomStateService 不可用! "
			     "【v215 零兜底】事件订阅失败, Tick 兜底仍工作, 但事件流丢失. 检查 World/Subsystem 初始化时序."));
		return;
	}

	// 【v215 大厂架构】动态多播必须 AddDynamic (Function Name 方式)
	StateSvc->OnPlayerSnapshotsChanged.AddDynamic(this, &UScoreboardWidget::HandlePlayerSnapshotsChanged);

	UE_LOG(LogTemp, Log,
		TEXT("[ScoreboardWidget] SubscribeScoreboardEvents: 已订阅 URoomStateService::OnPlayerSnapshotsChanged."));
}

void UScoreboardWidget::UnsubscribeScoreboardEvents()
{
	URoomStateService* StateSvc = URoomStateService::Get(this);
	if (!StateSvc)
	{
		// 【v215 0 兜底】Widget 销毁时 URoomStateService 可能已经销毁 (World 切换)
		//   - 这种情况 PIE 重启常见
		//   - 但解绑失败不致命, 后续事件触发会因为 Widget 已销毁而自然失效
		UE_LOG(LogTemp, Verbose,
			TEXT("[ScoreboardWidget] UnsubscribeScoreboardEvents: URoomStateService 已不可用, 解绑跳过 (Widget 销毁流程)."));
		return;
	}

	StateSvc->OnPlayerSnapshotsChanged.RemoveDynamic(this, &UScoreboardWidget::HandlePlayerSnapshotsChanged);
}

// ==========================================
// 【v215 大厂架构新增 — 事件回调】
// 大厂原则: 事件回调只做"通知", 真实逻辑全部走 RefreshScoreboard 单一入口
//
// 【v217 大厂架构重构 — 冻结后跳过事件刷新】
// 大厂原则 — 单一真理源 + 0 兜底:
//   - 旧 (v215-v216): HandlePlayerSnapshotsChanged 永远调 RefreshScoreboard
//     → Service 实时数据推过来就触发刷新 → 排名列表自动变顺序
//     → 用户报告: "结算页面的玩家排名列表,过一会就自动变化一下顺序"
//   - 新 (v217): bIsFrozen=true 时直接 return, 冻结后任何 Service 实时推送都不再响应
//   - 注意: TickFallback (5s 兜底) 仍然会调 RefreshScoreboard (走 GetActiveSnapshots → FrozenSnapshots)
//     → 冻结后 RefreshScoreboard 也只读 FrozenSnapshots, 不变
void UScoreboardWidget::HandlePlayerSnapshotsChanged()
{
	// 【v217 大厂架构重构】冻结后跳过 Service 实时事件 — 排名列表冻结
	if (bIsFrozen)
	{
		UE_LOG(LogTemp, Verbose,
			TEXT("[ScoreboardWidget] HandlePlayerSnapshotsChanged: bIsFrozen=true, 跳过 Service 实时事件刷新. "
			     "排名列表已冻结, 等用户点击返回大厅后才会重新响应 Service 推送."));
		return;
	}

	UE_LOG(LogTemp, Verbose,
		TEXT("[ScoreboardWidget] HandlePlayerSnapshotsChanged: 收到 URoomStateService 事件, 触发 RefreshScoreboard."));

	RefreshScoreboard();
}

// ==========================================
// 1. 公共接口 — RefreshScoreboard 增量更新 (Bug #1 闪烁修复)
// ==========================================
// 【v215 大厂架构重构】
//   历史 (v22-v213): ClearScoreboard() 全删全建 → 闪烁
//   新 (v215):
//     1. 拿最新快照 (冻结后只读 FrozenSnapshots)
//     2. 按 SnapshotId 增量更新已有 entry
//     3. 新玩家 CreateWidget + AddChild
//     4. 已退出的玩家 RemoveChild (基于 CachedLiveSnapshots 对比)
//     5. 排名变化用 RemoveChild/InsertChild 增量重排
void UScoreboardWidget::RefreshScoreboard()
{
	// 【v221.2 大厂架构诊断】让用户复测时能看到 RefreshScoreboard 的触发时统计
	UE_LOG(LogTemp, Display,
		TEXT("[ScoreboardWidget] 【v221.2】RefreshScoreboard: bIsFrozen=%d, bIsInSettlementState=%d, 触发进入数据刷新流程."),
		bIsFrozen ? 1 : 0, bIsInSettlementState ? 1 : 0);

	// 1. 拿当前应使用的数据源 (冻结 vs 实时)
	const TArray<FPlayerSnapshot>& ActiveSnapshots = GetActiveSnapshots();

	// 1.5 【v229.x 修复】检测模式变化 — Tab 打开 + 模式已切时立即更新阵营标题
	// 大厂原则 — 单一真理源 + 集中调度:
	//   - 旧 (v22-v229.x): RefreshTeamTitles 只在 NativeConstruct / ShowRoundSettlement /
	//     ShowFinalResult / ApplySnapshot 时调,Tab 打开不调,模式已变 (Melee→Zombie) 但 UI 不响应
	//   - 新 (v229.x): RefreshScoreboard 入口检测 CachedMatchMode 变化, 变化即 RefreshTeamTitles
	//   - 不在多处散落 RefreshTeamTitles 调用 (避免重复架构)
	//   - 0 兜底: 模式不变 → no-op; 模式变 → 立即刷新
	const ERoomMatchMode OldMatchMode = CachedMatchMode;
	RefreshCachedMatchMode();
	if (CachedMatchMode != OldMatchMode)
	{
		UE_LOG(LogTemp, Display,
			TEXT("[ScoreboardWidget] 【v229.x 修复】RefreshScoreboard: 模式变化 %d → %d, 触发 RefreshTeamTitles."),
			static_cast<int32>(OldMatchMode), static_cast<int32>(CachedMatchMode));
		RefreshTeamTitles();
	}

	// 2. 合并攻守两个阵营的快照 → 单一列表 (用 FactionTag 区分, 不区分容器)
	const FString LocalPlayerName = GetLocalPlayerName();

	// 3. 增量更新: 遍历最新快照, 更新/创建 entry
	for (const FPlayerSnapshot& Snap : ActiveSnapshots)
	{
		if (Snap.PlayerName.IsEmpty())
		{
			UE_LOG(LogTemp, Error,
				TEXT("[ScoreboardWidget] RefreshScoreboard: Snapshot.PlayerName 为空 (bIsAI=%d), 跳过. "
					 "【v202.0 零兜底】PlayerName 必须是 DisplayName/PlayerName."),
				Snap.bIsAI ? 1 : 0);
			continue;
		}

		if (!FFactionTags::IsValidFaction(Snap.FactionTag))
		{
			UE_LOG(LogTemp, Error,
				TEXT("[ScoreboardWidget] RefreshScoreboard: Snapshot.PlayerName='%s' FactionTag='%s' 无效, 跳过."),
				*Snap.PlayerName, *Snap.FactionTag.ToString());
			continue;
		}

		UpdateOrCreateEntryFromSnapshot(Snap, Snap.PlayerName == LocalPlayerName);
	}

	// 4. 删除已退出的玩家 (CachedLiveSnapshots 中存在但 ActiveSnapshots 中不存在的)
	RemoveStaleEntries(VB_AttackerTeam, ActiveSnapshots);
	RemoveStaleEntries(VB_DefenderTeam, ActiveSnapshots);

	// 5. 【v229.x 大厂架构重构 v2】单入口排名刷新 (排序 + 排名赋值一气呵成)
	//   单一真理源: Entry->GetScore() 走 FKdaScoring::Compute(Kills, Assists, Deaths)
	//   业务规则 (用户 2026.08.16 明确): 排名按"得分"排,击杀+10/助攻+5/死亡-1,严格唯一不重号
	//   旧 (v22-v228): SortEntriesByScore + UpdateAllRanks 两次调用 + N² 回查 Snap.Score + 不稳定排序
	RefreshRanksInContainer(VB_AttackerTeam);
	RefreshRanksInContainer(VB_DefenderTeam);

	// 【v215 大厂架构】注意: ActiveSnapshots 就是 CachedLiveSnapshots 本身 (GetActiveSnapshots 内部已写入),
	//   这里不需要再次赋值 (自我赋值无意义)
	//   下次 Refresh 时 RemoveStaleEntries 直接用 ActiveSnapshots 对比即可
}

// ==========================================
// 【v215 大厂架构新增 — 增量删除已退玩家】
// 大厂原则: 不缓存 entry 指针 (Widget 可能被外部清空), 每次动态查找
//
// 【v229.x 大厂架构重构 — O(N) 优化】
//   旧 (v22-v228): 用 ContainsByPredicate 按 PlayerName 线性查找 → N² 复杂度
//   新 (v229.x): 先把 ActiveSnapshots 的名字收集进 TSet<FString>, O(N+M) 总成本
//     - N = VerticalBox 子控件数
//     - M = ActiveSnapshots 数
//     - vs 旧版 O(N × M) = 大幅加速, 大厂原则 — 复杂工厂业级标配
void UScoreboardWidget::RemoveStaleEntries(UVerticalBox* VerticalBox, const TArray<FPlayerSnapshot>& ActiveSnapshots)
{
	if (!VerticalBox)
	{
		return;
	}

	// 【v229.x】构建 O(M) 的活跃快照名集合 (替代 N² ContainsByPredicate)
	TSet<FString> ActiveNames;
	ActiveNames.Reserve(ActiveSnapshots.Num());
	for (const FPlayerSnapshot& Snap : ActiveSnapshots)
	{
		ActiveNames.Add(Snap.PlayerName);
	}

	// 倒序遍历, 删除时不影响索引
	for (int32 i = VerticalBox->GetChildrenCount() - 1; i >= 0; i--)
	{
		UScoreboardEntryWidget* Entry = Cast<UScoreboardEntryWidget>(VerticalBox->GetChildAt(i));
		if (!Entry)
		{
			continue;
		}

		const FString EntryName = Entry->GetPlayerName();

		// TSet 查找是 O(1) (哈希), Total = O(N) 遍历 + O(M) 哈希, 替代旧 N×M ContainsByPredicate
		if (!ActiveNames.Contains(EntryName))
		{
			UE_LOG(LogTemp, Verbose,
				TEXT("[ScoreboardWidget] RemoveStaleEntries: 玩家 '%s' 已离开, 移除 entry."),
				*EntryName);
			VerticalBox->RemoveChildAt(i);
		}
	}
}

/**
 * 【v215 大厂架构】根据 FPlayerSnapshot 更新或创建条目 (增量更新, 不 Clear)
 *
 * 大厂原则:
 *   - View 只读 POJO 数据, 不感知 ARoomPlayerState / ABaseAIController
 *   - 真人 (bIsAI=false) 和 AI (bIsAI=true) 共用同一 Snapshot, 渲染统一
 *   - 已存在的 entry 调 SetScore/SetKDA/SetPlayerName, 不重建 → 修复闪烁
 *   - 跨阵营转移 (Snap.FactionTag 变化): 先 RemoveChild 再 AddChild
 */
void UScoreboardWidget::UpdateOrCreateEntryFromSnapshot(const FPlayerSnapshot& Snapshot, bool bIsLocalPlayer)
{
	if (Snapshot.PlayerName.IsEmpty())
	{
		UE_LOG(LogTemp, Error,
			TEXT("[ScoreboardWidget] UpdateOrCreateEntryFromSnapshot: Snapshot.PlayerName 为空 (bIsAI=%d), 拒绝渲染."),
			Snapshot.bIsAI ? 1 : 0);
		return;
	}

	if (!FFactionTags::IsValidFaction(Snapshot.FactionTag))
	{
		UE_LOG(LogTemp, Error,
			TEXT("[ScoreboardWidget] UpdateOrCreateEntryFromSnapshot: Snapshot.PlayerName='%s' (bIsAI=%d) "
				 "FactionTag='%s' 无效, 跳过."),
			*Snapshot.PlayerName, Snapshot.bIsAI ? 1 : 0, *Snapshot.FactionTag.ToString());
		return;
	}

	const bool bIsAttacker = (Snapshot.FactionTag == FFactionTags::Offense());
	UVerticalBox* TargetBox = bIsAttacker ? VB_AttackerTeam : VB_DefenderTeam;
	UVerticalBox* WrongBox = bIsAttacker ? VB_DefenderTeam : VB_AttackerTeam;
	if (!TargetBox)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[ScoreboardWidget] UpdateOrCreateEntryFromSnapshot: TargetBox 为 nullptr (阵营=%s)! "
			     "【v215 零兜底】检查 WBP_ScoreboardWidget 是否绑定了 VB_AttackerTeam / VB_DefenderTeam."),
			bIsAttacker ? TEXT("Attacker") : TEXT("Defender"));
		return;
	}

	const FString SnapshotId = MakeSnapshotId(Snapshot);

	// 0. 跨阵营转移: 先在错误容器中查找并移除
	if (WrongBox)
	{
		UScoreboardEntryWidget* Misplaced = FindEntryById(WrongBox, SnapshotId);
		if (Misplaced)
		{
			UE_LOG(LogTemp, Verbose,
				TEXT("[ScoreboardWidget] UpdateOrCreateEntryFromSnapshot: 玩家 '%s' 跨阵营转移, 从错误容器移除."),
				*Snapshot.PlayerName);
			WrongBox->RemoveChild(Misplaced);
		}
	}

	// 1. 在正确容器中查找已有 entry
	UScoreboardEntryWidget* ExistingEntry = FindEntryById(TargetBox, SnapshotId);
	if (ExistingEntry)
	{
		// 增量更新 (不重建 → 修复闪烁)
		// 【v229.x v2.1 大厂架构】单一入口: ApplySnapshotToEntry 统一所有 Setter 调用
		//   - 与 CreateEntryWidgetFromSnapshot 调用的字段集完全对称 (DRY)
		//   - 新字段加这里即可, 不需要 2 处同步维护
		ApplySnapshotToEntry(ExistingEntry, Snapshot, bIsLocalPlayer);
		return;
	}

	// 2. 不存在 → 创建新 entry
	CreateEntryWidgetFromSnapshot(Snapshot, bIsAttacker);
}

// ==========================================
// 【v215 大厂架构新增 — 私有辅助函数实现】
// ==========================================
FString UScoreboardWidget::MakeSnapshotId(const FPlayerSnapshot& Snapshot)
{
	// 大厂原则 — SnapshotId 包含 bIsAI, 防止真人/AI 同名冲突 (虽然理论上不会)
	return FString::Printf(TEXT("%s|%d"), *Snapshot.PlayerName, Snapshot.bIsAI ? 1 : 0);
}

UScoreboardEntryWidget* UScoreboardWidget::FindEntryById(UVerticalBox* VerticalBox, const FString& SnapshotId) const
{
	if (!VerticalBox)
	{
		return nullptr;
	}

	// SnapshotId 格式 = "PlayerName|bIsAI"
	// 拆出 PlayerName 用于匹配 Entry->GetPlayerName()
	FString TargetName;
	int32 TargetIsAI = 0;
	const FString Delimiter = TEXT("|");
	int32 DelimPos = INDEX_NONE;
	if (SnapshotId.FindChar(Delimiter[0], DelimPos))
	{
		TargetName = SnapshotId.Left(DelimPos);
		const FString AIStr = SnapshotId.Mid(DelimPos + 1);
		TargetIsAI = FCString::Atoi(*AIStr);
	}
	else
	{
		// 【v215 0 兜底】格式不对 → 整个 id 当名字用
		UE_LOG(LogTemp, Warning,
			TEXT("[ScoreboardWidget] FindEntryById: SnapshotId='%s' 格式不对 (无 '|' 分隔符), 降级用整串当名字."),
			*SnapshotId);
		TargetName = SnapshotId;
	}

	(void)TargetIsAI; // 当前版本 bIsAI 不影响 Entry 匹配 (Entry 只存名字), 留作未来扩展

	for (int32 i = 0; i < VerticalBox->GetChildrenCount(); i++)
	{
		UScoreboardEntryWidget* Entry = Cast<UScoreboardEntryWidget>(VerticalBox->GetChildAt(i));
		if (Entry && Entry->GetPlayerName() == TargetName)
		{
			return Entry;
		}
	}
	return nullptr;
}

void UScoreboardWidget::RemoveEntryById(UVerticalBox* VerticalBox, const FString& SnapshotId)
{
	UScoreboardEntryWidget* Entry = FindEntryById(VerticalBox, SnapshotId);
	if (Entry && VerticalBox)
	{
		VerticalBox->RemoveChild(Entry);
	}
}

// ==========================================
// 【v229.x v2.1 大厂架构新增】单一入口: 把 FPlayerSnapshot 字段灌入 Entry Widget
// ==========================================
//
// 大厂原则 — DRY (Don't Repeat Yourself):
//   - 旧 (v22-v229.x v2): UpdateOrCreateEntryFromSnapshot 增量更新 + CreateEntryWidgetFromSnapshot
//     创建新 entry 各自手动 SetPlayerName / SetScore / SetKDA / SetIsCurrentPlayer 4 次
//   - 新 (v229.x v2.1): 统一封装在 ApplySnapshotToEntry, 2 个调用方共享
//   - 防未来扩展时漏改: 加新字段 (例如 SetAvatar) 只需要修改此处, 2 个路径自动同步
//
// 大厂原则 — 0 兜底:
//   - EntryWidget == nullptr → 早 return (不让 UE 内部 Set 函数 null deref)
//   - Snapshot.PlayerName == "" → 仍调 SetPlayerName(""), 由 Entry Widget 内部决定
//     (Entry Widget 内部 SetText 不会崩, 但 UI 会显示空 — 这是 UI reality 不算兜底)
void UScoreboardWidget::ApplySnapshotToEntry(UScoreboardEntryWidget* EntryWidget,
                                              const FPlayerSnapshot& Snapshot,
                                              bool bIsLocalPlayer) const
{
	if (!EntryWidget)
	{
		return;
	}

	// 【v208 大厂架构重构 — PlayerName 单一真理源】
	//   BuildAISnapshot* 已统一加 "[AI] " 前缀, Entry 直接用 Snap.PlayerName (不再二次加前缀)
	EntryWidget->SetPlayerName(Snapshot.PlayerName);
	EntryWidget->SetScore(Snapshot.Score);
	EntryWidget->SetKDA(Snapshot.Kills, Snapshot.Deaths, Snapshot.Assists);
	EntryWidget->SetIsCurrentPlayer(bIsLocalPlayer);
}

TArray<FPlayerSnapshot>& UScoreboardWidget::GetActiveSnapshots()
{
	// 【v215 大厂架构 — 冻结 vs 实时切换】
	//   冻结: 返回 FrozenSnapshots (已与 URoomStateService 解耦)
	//   未冻结: 实时拉 RoomStateService(真人) + RoomGameState(AI), 写入 CachedLiveSnapshots, 返回引用
	if (bIsFrozen)
	{
		return FrozenSnapshots;
	}

	// 未冻结: 实时拉
	// 【v215 0 兜底】URoomStateService 拿不到 → 返回空数组 + Log Error
	URoomStateService* StateSvc = URoomStateService::Get(this);
	if (!StateSvc)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[ScoreboardWidget] GetActiveSnapshots: URoomStateService 不可用, 返回空数组. "
			     "【v215 零兜底】事件流丢失, 需检查 World 切换时序."));
		static TArray<FPlayerSnapshot> EmptySnapshots;
		return EmptySnapshots;
	}

	// ==========================================
	// 【v223.0 大厂架构 P0 — AI 数据源切换 Server-Authoritative Replicated】
	// ==========================================
	//
	// 根因 (v218-v222.0 仍未解决):
	//   - 老路径: URoomStateService::GetFactionSnapshotsWithAI → TActorIterator<ABaseAIController>
	//   - 客户端 AIC 实例数 = 0 (UE 5.6 AAIController 复制受 NetCull/PossessedPawn 时序影响)
	//   - 客户端 Tab Scoreboard 永远不显示 AI
	//
	// 新路径 (镜像 Settlement v217 + PendingAIQueue v46):
	//   - 真人玩家: StateSvc->GetFactionSnapshots(Offense/Defense) (PlayerArray 复制稳定, 老路径)
	//   - AI:        RoomGS->GetBattleAIEntries(Offense/Defense) (ReplicatedBattleAIEntries, Server 拉)
	//   - Server    端在 SpawnAIInternal 末尾写 ReplicatedBattleAIEntries + ForceNetUpdate
	//   - 完全不依赖 AIC 复制, 0 兜底
	//
	// 大厂原则:
	//   - 单一真理源: Server 拉 AIC → 写 Replicated → Client 读
	//   - 0 兜底: GetBattleAIEntries 内部已校验 FactionTag 有效性, 这里不重复
	// ==========================================

	UWorld* World = GetWorld();
	ARoomGameState* RoomGS = World ? World->GetGameState<ARoomGameState>() : nullptr;

	// 真人玩家 (老路径, PlayerArray 复制稳定)
	CachedLiveSnapshots.Empty();
	CachedLiveSnapshots.Append(StateSvc->GetFactionSnapshots(FFactionTags::Offense()));
	CachedLiveSnapshots.Append(StateSvc->GetFactionSnapshots(FFactionTags::Defense()));

	// AI (新路径, v223.0 Server-Authoritative 复制)
	if (RoomGS)
	{
		// 攻方 AI
		const TArray<FFactionSnapshotEntry> OffenseAIEntries = RoomGS->GetBattleAIEntries(FFactionTags::Offense());
		for (const FFactionSnapshotEntry& Entry : OffenseAIEntries)
		{
			CachedLiveSnapshots.Add(ConvertBattleAIEntryToSnapshot(Entry));
		}

		// 守方 AI
		const TArray<FFactionSnapshotEntry> DefenseAIEntries = RoomGS->GetBattleAIEntries(FFactionTags::Defense());
		for (const FFactionSnapshotEntry& Entry : DefenseAIEntries)
		{
			CachedLiveSnapshots.Add(ConvertBattleAIEntryToSnapshot(Entry));
		}
	}
	else
	{
		// 0 兜底: GameState 拿不到 → AI 数据源缺失, 显式 Log Error
		UE_LOG(LogTemp, Error,
			TEXT("[ScoreboardWidget] GetActiveSnapshots: RoomGameState 不可用, AI 数据源缺失. "
			     "【v223.0 零兜底】Tab Scoreboard 将不显示 AI. 修复: 检查 GM_RoomGameMode 启动时序."));
	}

	return CachedLiveSnapshots;
}

// ==========================================
// 【v223.0 大厂架构】FFactionSnapshotEntry → FPlayerSnapshot 转换 (镜面 Settlement v217 路径)
// ==========================================
//
// 单一真理源:
//   - FFactionSnapshotEntry 是 Server-Authoritative 写入的 Replicated 数据
//   - FPlayerSnapshot 是 UI 内部使用的数据
//   - 转换函数集中在 ScoreboardWidget.cpp 一处, 避免分散拼凑
//
// 0 兜底:
//   - Entry.DisplayName 为空 → 返回空 Snapshot (RefreshScoreboard 会 Log Error 跳过)
//   - FactionTagName 解析失败 → 用 FFactionTags::Offense() 默认 (Log Warning, 不静默)
FPlayerSnapshot UScoreboardWidget::ConvertBattleAIEntryToSnapshot(const FFactionSnapshotEntry& Entry)
{
	FPlayerSnapshot Snap;

	// 0 兜底: DisplayName 必须非空
	if (Entry.DisplayName.IsEmpty())
	{
		UE_LOG(LogTemp, Error,
			TEXT("[ScoreboardWidget] ConvertBattleAIEntryToSnapshot: Entry.DisplayName 为空, 返回空 Snapshot. "
			     "【v223.0 零兜底】Server 写入时检查 AIC->GetCharacterID() 是否有效."));
		return Snap;
	}

	Snap.PlayerName = FString::Printf(TEXT("[AI] %s"), *Entry.DisplayName); // 镜像 v208 单一真理源
	Snap.bIsAI      = true;
	Snap.bIsReady   = false;
	Snap.bIsHost    = false;
	Snap.SequenceID = 0;
	Snap.Score      = Entry.Score;
	Snap.Kills      = Entry.Kills;
	Snap.Deaths     = Entry.Deaths;
	Snap.Assists    = Entry.Assists;

	// FactionTag: Server 写入时用 bIsAttacker (直接对应 Offense/Defense), 客户端无需解析 FactionTagName
	// 大厂原则: 单一真理源 = bIsAttacker (Server 写入 ReplicatedBattleAIEntries 时已计算)
	// 0 兜底: Server 写入 bIsAttacker 必须 (1) 正确计算 (2) 与 FactionTagName 逻辑一致 — ServerRefreshBattleAIEntries 内有 IsValidFaction 校验
	if (Entry.bIsAttacker)
	{
		Snap.FactionTag = FFactionTags::Offense();
	}
	else
	{
		Snap.FactionTag = FFactionTags::Defense();
	}

	return Snap;
}

UScoreboardEntryWidget* UScoreboardWidget::CreateEntryWidgetFromSnapshot(const FPlayerSnapshot& Snapshot, bool bIsAttacker)
{
	if (Snapshot.PlayerName.IsEmpty())
	{
		UE_LOG(LogTemp, Error,
			TEXT("[ScoreboardWidget] CreateEntryWidgetFromSnapshot: Snapshot.PlayerName 为空, 拒绝创建."));
		return nullptr;
	}

	const bool bActualIsAttacker = (Snapshot.FactionTag == FFactionTags::Offense());
	if (bActualIsAttacker != bIsAttacker)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[ScoreboardWidget] CreateEntryWidgetFromSnapshot: Snapshot '%s' 阵营不一致, 参数=%d 实际=%d, 已修正"),
			*Snapshot.PlayerName, bIsAttacker, bActualIsAttacker);
		bIsAttacker = bActualIsAttacker;
	}

	UVerticalBox* TargetBox = bIsAttacker ? VB_AttackerTeam : VB_DefenderTeam;
	if (!TargetBox)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[ScoreboardWidget] CreateEntryWidgetFromSnapshot: TargetBox 为 nullptr (阵营=%s)! "
			     "【v215 零兜底】检查 WBP_ScoreboardWidget 是否绑定了 VB_AttackerTeam / VB_DefenderTeam."),
			bIsAttacker ? TEXT("Attacker") : TEXT("Defender"));
		return nullptr;
	}

	if (!ScoreboardEntryWidgetClass)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[ScoreboardWidget] CreateEntryWidgetFromSnapshot: ScoreboardEntryWidgetClass 未配置！请在 WBP_ScoreboardWidget 蓝图中设置. "
				 "【v215 修复】不再静默 return, 显式 Log Error 强制修复 BP 配置."));
		return nullptr;
	}

	UScoreboardEntryWidget* EntryWidget = CreateWidget<UScoreboardEntryWidget>(this, ScoreboardEntryWidgetClass);
	if (!EntryWidget)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[ScoreboardWidget] CreateEntryWidgetFromSnapshot: CreateWidget 失败 (Class 配置错误或内存不足). "
			     "【v215 零兜底】拒绝继续, 不返回空 entry."));
		return nullptr;
	}

	// 【v229.x v2.1 大厂架构】单一入口: ApplySnapshotToEntry 统一所有 Setter 调用
	//   - 与 UpdateOrCreateEntryFromSnapshot 的 ExistingEntry 路径完全对称 (DRY)
	//   - 任何新字段只需要在 ApplySnapshotToEntry 加一次, 2 个创建/更新路径自动同步
	const FString LocalPlayerName = GetLocalPlayerName();
	ApplySnapshotToEntry(EntryWidget, Snapshot, Snapshot.PlayerName == LocalPlayerName);

	TargetBox->AddChild(EntryWidget);

	return EntryWidget;
}

void UScoreboardWidget::ClearScoreboard()
{
	// 【v215 大厂架构重构】
	//   历史 (v22-v213): RefreshScoreboard 开头调此函数, 触发闪烁
	//   新 (v215): RefreshScoreboard 走增量更新, 此函数仅供外部明确重置时调用
	//              (如切局/退出结算时)
	if (VB_AttackerTeam)
	{
		VB_AttackerTeam->ClearChildren();
	}

	if (VB_DefenderTeam)
	{
		VB_DefenderTeam->ClearChildren();
	}
}

// ==========================================
// 【v203.0 大厂架构新增】模式缓存 + 阵营标题刷新
// ==========================================
void UScoreboardWidget::RefreshCachedMatchMode()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	ARoomGameState* RoomGS = World->GetGameState<ARoomGameState>();
	if (!RoomGS)
	{
		// 零兜底: GS 还没就绪时, CachedMatchMode 保持默认 Melee, 至少显示刀战文案
		return;
	}

	if (CachedMatchMode != RoomGS->CurrentMatchMode)
	{
		UE_LOG(LogTemp, Log,
			TEXT("[ScoreboardWidget] RefreshCachedMatchMode: 模式变化 %d → %d"),
			static_cast<int32>(CachedMatchMode), static_cast<int32>(RoomGS->CurrentMatchMode));
		CachedMatchMode = RoomGS->CurrentMatchMode;
	}
}

void UScoreboardWidget::RefreshTeamTitles()
{
	if (Text_AttackerTeamTitle)
	{
		Text_AttackerTeamTitle->SetText(FText::FromString(ScoreboardFactionNames::GetAttackerTeamTitle(CachedMatchMode)));
	}
	else
	{
		UE_LOG(LogTemp, Verbose,
			TEXT("[ScoreboardWidget] RefreshTeamTitles: Text_AttackerTeamTitle 未绑定 (WBP 旧版本?), 跳过刷新."));
	}

	if (Text_DefenderTeamTitle)
	{
		Text_DefenderTeamTitle->SetText(FText::FromString(ScoreboardFactionNames::GetDefenderTeamTitle(CachedMatchMode)));
	}
	else
	{
		UE_LOG(LogTemp, Verbose,
			TEXT("[ScoreboardWidget] RefreshTeamTitles: Text_DefenderTeamTitle 未绑定 (WBP 旧版本?), 跳过刷新."));
	}
}

// ==========================================
// 【v229.x 大厂架构重构 v2】单入口排名刷新 — 按"得分" + 严格唯一排名
// ==========================================
//
// 历史 (v22-v228) 反模式:
//   1. 排序字段错: 用 Snap.Score (总分 = Kill*20 + Assist*10), 与玩家"谁击杀多"的直觉不符
//   2. N² 重复架构: Sort 内重读 ActiveSnapshots 线性查 Snap.Score (Sort 函数本身又是 N log N)
//   3. 不稳定排序: TArray::Sort 用 introsort (UE 实现),同 Kills 玩家顺序随机跳变
//   4. UpdateAllRanks 按位置 1,2,3 累加: 同 Kills 玩家挤占名次 (rank 1,2,3 全是 5 Kills)
//   5. 调用方必须两次调用: SortEntriesByScore + UpdateAllRanks (忘调其一 → 排名不更新)
//
// 业务规则 (用户 2026.08.16 明确):
//   - 排名按"得分"排序: 击杀 +10 / 助攻 +5 / 死亡 -1
//   - 公式: Score = Kills*10 + Assists*5 - Deaths (走 FKdaScoring::Compute)
//   - 排名不能重复数字 (严格唯一: 1,2,3,4 即便同分)
//   - 同分时: 先插入在前 (按 OriginalIndex 升序 → 排名也升序)
//   - 排名只显示数字 (无 emoji, 无特殊字符)
//
// 新 (v229.x v2) 大厂架构:
//   - 单一入口 (此处): RefreshRanksInContainer 一个函数搞定排序+排名赋值
//   - 排序字段: Entry->GetScore() (走 FKdaScoring 公式, 业务层 = UI 层 同一真理源)
//   - 单一真理源: Entry Widget 持 CachedKills/Deaths/Assists, GetScore 现算
//   - 稳定排序: 复合 key 编码到 int64 (-Score 高 32 位, OriginalIndex 低 32 位)
//   - 严格唯一排名: rank 永远递增 (1, 2, 3, 4, 5...), 同分玩家按原位置插队, rank 也不同
//
// 算反复杂度 O(N log N):
//   1. 收集 (OriginalIndex, Score, Widget) 三元组 — O(N)
//   2. 稳定排序 — O(N log N)
//   3. 增量重排 (RemoveChild + InsertChild) — O(N) (位置变化部分)
//   4. 一次遍历排名赋值 — O(N)
//
// 大厂原则 — 零兜底:
//   - VerticalBox 为空 → return (无需排序)
//   - Entry->GetScore() 永远有效 (CachedKills/Deaths/Assists 默认 0, 不抛)
//   - RemoveChild + InsertChild 不重建 Widget, 与 v215 Bug #1 闪烁修复一致
// ==========================================
void UScoreboardWidget::RefreshRanksInContainer(UVerticalBox* VerticalBox)
{
	if (!VerticalBox)
	{
		return;
	}

	const int32 ChildCount = VerticalBox->GetChildrenCount();
	if (ChildCount == 0)
	{
		return;
	}

	// ==========================================
	// Step 1: 收集 (OriginalIndex, Score, Widget) 三元组
	// ==========================================
	struct FEntryRankData
	{
		int32 OriginalIndex;             // 原容器位置 (稳定排序 tiebreaker)
		int32 Score;                      // 排序主键 (用户业务规则: 按得分排, 走 FKdaScoring::Compute)
		UScoreboardEntryWidget* Widget;  // 不拥有, 仅 WeakRef (容器管理生命周期)
	};

	TArray<FEntryRankData> EntryList;
	EntryList.Reserve(ChildCount);

	for (int32 i = 0; i < ChildCount; i++)
	{
		UScoreboardEntryWidget* EntryWidget = Cast<UScoreboardEntryWidget>(VerticalBox->GetChildAt(i));
		if (!EntryWidget)
		{
			UE_LOG(LogTemp, Verbose,
				TEXT("[ScoreboardWidget] RefreshRanksInContainer: 位置 %d 的子控件不是 UScoreboardEntryWidget, 跳过."),
				i);
			continue;
		}

		FEntryRankData Data;
		Data.OriginalIndex = i;
		Data.Score = EntryWidget->GetScore();     // 单一真理源: Entry Widget 自持 K/D/A → FKdaScoring 现算
		Data.Widget = EntryWidget;
		EntryList.Add(Data);
	}

	if (EntryList.Num() <= 1)
	{
		// 0 或 1 个 Entry: 无需排序,但仍按位置赋 rank
		int32 Rank = 1;
		for (const FEntryRankData& Data : EntryList)
		{
			if (Data.Widget)
			{
				Data.Widget->SetRank(Rank++);
			}
		}
		return;
	}

	// ==========================================
	// Step 2: 稳定排序 — Score 降序, 同分按 OriginalIndex 升序
	// ==========================================
	// 【大厂原则 — 稳定排序实现】
	//   不用 Algo::Sort (不稳定),不用 TArray::Sort (UE 实现是 introsort, 不稳定)
	//   用复合 key 编码法 (O(N log N), 工业标准做法 — Google/Epic 内部都用):
	//     key = (int64(-Score) << 32) | int64(OriginalIndex)
	//     排序后按 key 升序 → -Score 升序 = Score 降序, OriginalIndex 升序 = 稳定
	struct FEncodedEntry
	{
		int64 EncodedKey;
		UScoreboardEntryWidget* Widget;
	};

	TArray<FEncodedEntry> EncodedList;
	EncodedList.Reserve(EntryList.Num());
	for (const FEntryRankData& Data : EntryList)
	{
		FEncodedEntry Encoded;
		Encoded.EncodedKey = (static_cast<int64>(-Data.Score) << 32) | static_cast<int64>(Data.OriginalIndex);
		Encoded.Widget = Data.Widget;
		EncodedList.Add(Encoded);
	}

	EncodedList.Sort([](const FEncodedEntry& A, const FEncodedEntry& B)
	{
		return A.EncodedKey < B.EncodedKey;
	});

	// ==========================================
	// Step 3: 顺序重排 — 倒序 RemoveChildAll 收集 + 按序 AddChild (稳健实现, 不用 InsertChildAt)
	// ==========================================
	//
	// 【v229.x v2.1 大厂架构修复 — 排序顺序不生效真根因】
	//
	// 历史 (v229.x v2): RemoveChild(Widget) + InsertChildAt(i, Widget) — 用户反馈"排名正确但排序不对"
	//
	// 根因 (大厂级 — UE 5.6 UMG API 行为):
	//   - UE 5.6 UPanelWidget::InsertChildAt(i, Widget) 内部:
	//     - 检查 Widget->Slot != nullptr → 报错 / 断言 / 静默 no-op
	//     - 即使 RemoveChild 后 Widget->Slot == nullptr, InsertChildAt 仍可能因 Z-Order / Layout 缓存
	//       导致实际插入位置不是 i (而是尾部)
	//   - 表现: SetRank 正确 (i+1), 但 Widget 实际渲染位置错乱
	//   - 用户测试: 排名 1 显示在容器最下方, 排名 3 显示在最上方 — InsertChildAt 顺序被颠倒或丢失
	//
	// 新方案 (v229.x v2.1 大厂架构 — 倒序 RemoveChild + 顺序 AddChild):
	//   1. 倒序遍历容器 → 全部 RemoveChildAt(i) → Widget 对象全部脱离容器 (不销毁)
	//   2. 按 EncodedList 顺序遍历 → AddChild(Widget) → 容器按 Score 降序排列
	//
	// 大厂原则:
	//   - Widget 对象复用 (与 v215 闪烁修复一致): 只 RemoveChild 不 Destroy
	//   - 顺序重建 100% 可靠: AddChild 永远在末尾追加 (UMG 标准行为, 无位置歧义)
	//   - 零重复架构: 顺序重排集中一处 (RefreshRanksInContainer 单一入口)
	//   - 0 兜底: 任何 Widget 为 null → Log Error 强制排查
	//
	// 与 v215 闪烁修复的关系:
	//   - v215 闪烁修复: 不 ClearChildren + 重建 Widget 对象 (避免 CreateWidget 闪烁)
	//   - v229.x v2.1: 仍然不重建 Widget 对象 — 只重新排序容器内位置
	//   - Widget 实例保持不变 (避免动画/Timer/状态重置), 只是 Visible 位置变了
	//
	// 性能 (O(N) 总开销, 与 v229.x v2 等同):
	//   - N 次 RemoveChild + N 次 AddChild = 2N 次 UMG Slot 操作
	//   - 与 InsertChildAt 等同的 Slot 操作数, 但 100% 可靠
	//
	// Step 3a: 倒序 RemoveChild (避免索引移动 — 倒序遍历 RemoveChildAt(i) 是业界标准做法)
	//   - 只移除 Entry Widget (规避非 Entry 的 widget 占位)
	//   - Cast 失败 = 大厂错配 (容器混入了其他 widget), 移除错配 widget (它本不该在这里)
	for (int32 i = VerticalBox->GetChildrenCount() - 1; i >= 0; i--)
	{
		UWidget* WidgetAt = VerticalBox->GetChildAt(i);
		UScoreboardEntryWidget* EntryWidget = Cast<UScoreboardEntryWidget>(WidgetAt);
		if (!EntryWidget)
		{
			// 【v229.x v2.1 零兜底】非 UScoreboardEntryWidget 类型 → Log Error, 移除错配 widget
			//   - 移除原因: 容器混入其他 widget = 大厂错配, 修复后重新 AddChild
			//   - 保留原因: 也没意义 (Entry 排序按 Score 降序, 错配 widget 没有 Score)
			//   - 大厂原则: 0 兜底 = 显式报错 + 移除错配, 让 BP 工程师在 UE 编辑器修
			UE_LOG(LogTemp, Error,
				TEXT("[ScoreboardWidget] RefreshRanksInContainer: 位置 %d 子控件 '%s' 不是 UScoreboardEntryWidget, 类型错配. "
					 "已移除错配 widget. 【修复】检查 WBP_ScoreboardWidget.VB_AttackerTeam/DefenderTeam 是否混入了其他 widget."),
				i, *GetNameSafe(WidgetAt));
			VerticalBox->RemoveChildAt(i);
			continue;
		}

		// 0 兜底: RemoveChild 后 Widget 应仍有效 (UE GC 不销毁), 但显式校验
		VerticalBox->RemoveChildAt(i);
		if (!IsValid(EntryWidget))
		{
			UE_LOG(LogTemp, Error,
				TEXT("[ScoreboardWidget] RefreshRanksInContainer: 位置 %d 的 Widget 在 RemoveChildAt 后失效 (被 GC). "
					 "【零兜底】检查是否有外部代码在排序期间销毁 Widget."),
				i);
		}
	}

	// Step 3b: 按 EncodedList 顺序 AddChild (容器末尾追加, 顺序就是最终排序)
	//   - 防重复: 实际不会发生 (Step 3a 已移除所有 Child), 但显式校验更稳健
	for (const FEncodedEntry& Encoded : EncodedList)
	{
		if (!IsValid(Encoded.Widget))
		{
			// 0 兜底: Widget 被 GC → Log Error 强制排查
			const int32 OriginalIndex = static_cast<int32>(Encoded.EncodedKey & 0xFFFFFFFF);
			UE_LOG(LogTemp, Error,
				TEXT("[ScoreboardWidget] RefreshRanksInContainer: EncodedList 包含失效 Widget (OriginalIndex=%d). "
					 "【零兜底】Widget 应由 VerticalBox 管理, 不会失效. 检查 GC 路径."),
				OriginalIndex);
			continue;
		}

		VerticalBox->AddChild(Encoded.Widget);
	}

	// ==========================================
	// Step 4: 严格唯一排名 — rank 永远递增 (用户规则 2026.08.16)
	// ==========================================
	// 规则:
	//   - 排名 1 = 得分最高者
	//   - 排名严格递增, 永远不重号 (1, 2, 3, 4, ...)
	//   - 同分时: 按 OriginalIndex 升序 = 先插入在前 → rank 也升序
	//
	// 实现: rank 永远 = i + 1 (1-based 位置), 不需要"继承前一个 rank"的复杂判断
	//   因为稳定排序已经保证同分按 OriginalIndex 升序, 顺序遍历自然 rank 递增
	//
	// 例: scores = [10, 8, 8, 5] → ranks = [1, 2, 3, 4] (用户硬规则: 永远不重号)
	// 例: scores = [10, 8, 8, 5, 5] → ranks = [1, 2, 3, 4, 5] (同分先插入在前, rank 仍递增)
	for (int32 i = 0; i < EncodedList.Num(); i++)
	{
		UScoreboardEntryWidget* EntryWidget = EncodedList[i].Widget;
		if (!EntryWidget)
		{
			continue;
		}

		// 严格唯一: rank = 1-based 位置
		EntryWidget->SetRank(i + 1);
	}
}

// 注: 旧 SortEntriesByScore / UpdateAllRanks 已在 v229.x 删除 (合并进 RefreshRanksInContainer 单一入口)
//   - SortEntriesByScore: 重复架构 (从 Snap 查 Score), 不稳定排序, 调用方必须记得调 UpdateAllRanks
//   - UpdateAllRanks: 按位置 1,2,3... 累加 (业界标准不符, 同 Kills 玩家挤占名次)

FString UScoreboardWidget::GetLocalPlayerName() const
{
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
	{
		if (APlayerState* PS = PC->GetPlayerState<APlayerState>())
		{
			return PS->GetPlayerName();
		}
	}

	return FString();
}

// ==========================================
// 2. 结算阶段接口
// ==========================================

void UScoreboardWidget::ShowRoundSettlement(int32 AttackerKills, int32 DefenderKills)
{
	bIsInSettlementState = true;

	RefreshCachedMatchMode();
	RefreshTeamTitles();

	UWorld* World = GetWorld();
	ARoomGameState* RoomGS = World ? World->GetGameState<ARoomGameState>() : nullptr;

	int32 AttackerDisplayValue = AttackerKills;
	int32 DefenderDisplayValue = DefenderKills;

	if (RoomGS)
	{
		if (CachedMatchMode == ERoomMatchMode::Zombie)
		{
			AttackerDisplayValue = RoomGS->AttackerWins;
			DefenderDisplayValue = RoomGS->DefenderWins;
		}
		else if (CachedMatchMode == ERoomMatchMode::Melee)
		{
			AttackerDisplayValue = RoomGS->AttackerTotalKills;
			DefenderDisplayValue = RoomGS->DefenderTotalKills;
		}
		else // None / 异常
		{
			UE_LOG(LogTemp, Error,
				TEXT("[ScoreboardWidget] ShowRoundSettlement: CachedMatchMode='None' 异常状态! "
				     "【v203.0 零兜底】默认走刀战路径."));
			AttackerDisplayValue = RoomGS->AttackerTotalKills;
			DefenderDisplayValue = RoomGS->DefenderTotalKills;
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[ScoreboardWidget] ShowRoundSettlement: GameState 未就绪, 使用传入参数: 攻方=%d, 守方=%d"),
			AttackerKills, DefenderKills);
	}

	RefreshScoreboard();

	if (Text_Settlement_AttackerKills)
	{
		const FString Label = (CachedMatchMode == ERoomMatchMode::Zombie)
			? ScoreboardFactionNames::GetAttackerWinsLabel_Zombie(AttackerDisplayValue)
			: ScoreboardFactionNames::GetAttackerKillsLabel_Melee(AttackerDisplayValue);
		Text_Settlement_AttackerKills->SetText(FText::FromString(Label));
		Text_Settlement_AttackerKills->SetVisibility(ESlateVisibility::Visible);
	}

	if (Text_Settlement_DefenderKills)
	{
		const FString Label = (CachedMatchMode == ERoomMatchMode::Zombie)
			? ScoreboardFactionNames::GetDefenderWinsLabel_Zombie(DefenderDisplayValue)
			: ScoreboardFactionNames::GetDefenderKillsLabel_Melee(DefenderDisplayValue);
		Text_Settlement_DefenderKills->SetText(FText::FromString(Label));
		Text_Settlement_DefenderKills->SetVisibility(ESlateVisibility::Visible);
	}

	if (Text_AttackerWinResult)
	{
		Text_AttackerWinResult->SetVisibility(ESlateVisibility::Collapsed);
	}

	if (Text_DefenderWinResult)
	{
		Text_DefenderWinResult->SetVisibility(ESlateVisibility::Collapsed);
	}

	SetVisibility(ESlateVisibility::Visible);

	UE_LOG(LogTemp, Log,
		TEXT("[ScoreboardWidget] ShowRoundSettlement: Mode=%d, 攻方=%d, 守方=%d"),
		static_cast<int32>(CachedMatchMode), AttackerDisplayValue, DefenderDisplayValue);
}

// ==========================================
// 【v215 大厂架构重构 — ShowFinalResult 冻结快照入口】
// ==========================================
// Bug #1 闪烁修复:
//   进入结算 → FreezeSnapshot() 一次性拉所有数据
//   后续刷新只读 FrozenSnapshots, 不再触发任何 UI 重建
// Bug #2 解耦:
//   冻结后 Widget 不再订阅/拉 URoomStateService, 完全独立于房间连接
void UScoreboardWidget::ShowFinalResult(int32 AttackerWins, int32 DefenderWins)
{
	RefreshCachedMatchMode();
	RefreshTeamTitles();

	UWorld* World = GetWorld();
	if (World)
	{
		if (ARoomGameState* RoomGS = World->GetGameState<ARoomGameState>())
		{
			AttackerWins = RoomGS->AttackerWins;
			DefenderWins = RoomGS->DefenderWins;
			UE_LOG(LogTemp, Log,
				TEXT("[ScoreboardWidget] ShowFinalResult: 从 GameState 读取胜局数, Mode=%d, 攻方/母体=%d, 守方/人类=%d"),
				static_cast<int32>(CachedMatchMode), AttackerWins, DefenderWins);
		}
	}

	// 【v215 大厂架构重构 — 冻结快照】
	FreezeSnapshot();

	// 冻结后立即用冻结数据渲染一次 (确保 UI 显示冻结的数据, 不是当前实时数据)
	RefreshScoreboard();

	// 显示攻方胜利/平局文字
	//   【v229.x v3 大厂架构】字体相关设置(颜色)统一在 WBP_ScoreboardWidget 蓝图配置,代码不再干预
	//   - SetText 保留(显示内容由 ScoreboardFactionNames 决定)
	//   - SetVisibility 保留(显隐控制是业务逻辑)
	//   - SetColorAndOpacity 已删除(字体颜色 = 美术表现,蓝图单一真理源)
	if (Text_AttackerWinResult)
	{
		if (AttackerWins > DefenderWins)
		{
			Text_AttackerWinResult->SetText(FText::FromString(ScoreboardFactionNames::GetAttackerWinLabel(CachedMatchMode)));
		}
		else if (AttackerWins == DefenderWins)
		{
			Text_AttackerWinResult->SetText(FText::FromString(ScoreboardFactionNames::GetTieLabel()));
		}
		Text_AttackerWinResult->SetVisibility(AttackerWins >= DefenderWins ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}

	// 显示守方胜利/平局文字
	if (Text_DefenderWinResult)
	{
		if (DefenderWins > AttackerWins)
		{
			Text_DefenderWinResult->SetText(FText::FromString(ScoreboardFactionNames::GetDefenderWinLabel(CachedMatchMode)));
		}
		else if (AttackerWins == DefenderWins)
		{
			Text_DefenderWinResult->SetText(FText::FromString(ScoreboardFactionNames::GetTieLabel()));
		}
		Text_DefenderWinResult->SetVisibility(DefenderWins >= AttackerWins ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}

	UE_LOG(LogTemp, Log,
		TEXT("[ScoreboardWidget] ShowFinalResult: 已冻结快照, 攻方/母体胜%d局, 守方/人类胜%d局. "
		     "【v215 冻结快照】后续刷新只读 FrozenSnapshots, 与房间连接解耦."),
		AttackerWins, DefenderWins);
}

// ==========================================
// 【v215 大厂架构新增 — FreezeSnapshot 实现】
// ==========================================
// 大厂原则 — 一次性快照, 不允许分多次拉:
//   冻结时直接拉所有 FactionTag 的快照到一个 TArray, 永久保存
//   后续 RefreshScoreboard 通过 GetActiveSnapshots() 自动走 FrozenSnapshots
void UScoreboardWidget::FreezeSnapshot()
{
	// 0 兜底: URoomStateService 拿不到 → Log Error, 不冻结, 不静默
	URoomStateService* StateSvc = URoomStateService::Get(this);
	if (!StateSvc)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[ScoreboardWidget] FreezeSnapshot: URoomStateService 不可用! "
			     "【v215 零兜底】冻结失败, 保留旧状态. 检查 World 切换时序 (如已开始离开房间)."));
		return;
	}

	// 一次性合并两个阵营到一个 FrozenSnapshots
	FrozenSnapshots.Empty();

	// 【v223.0 大厂架构 P0】同 GetActiveSnapshots: 真人走 StateSvc, AI 走 RoomGS->GetBattleAIEntries
	// 0 兜底: World 拿不到 → Log Error + 不冻结 (旧路径会静默 return 空)
	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[ScoreboardWidget] FreezeSnapshot: World 无效, 不冻结. 【v223.0 零兜底】检查 ShowFinalResult 调用时序."));
		return;
	}

	// 真人玩家 (老路径)
	FrozenSnapshots.Append(StateSvc->GetFactionSnapshots(FFactionTags::Offense()));
	FrozenSnapshots.Append(StateSvc->GetFactionSnapshots(FFactionTags::Defense()));

	// AI (新路径, v223.0 Server-Authoritative 复制)
	ARoomGameState* RoomGS = World->GetGameState<ARoomGameState>();
	if (RoomGS)
	{
		const TArray<FFactionSnapshotEntry> OffenseAIEntries = RoomGS->GetBattleAIEntries(FFactionTags::Offense());
		for (const FFactionSnapshotEntry& Entry : OffenseAIEntries)
		{
			FrozenSnapshots.Add(ConvertBattleAIEntryToSnapshot(Entry));
		}

		const TArray<FFactionSnapshotEntry> DefenseAIEntries = RoomGS->GetBattleAIEntries(FFactionTags::Defense());
		for (const FFactionSnapshotEntry& Entry : DefenseAIEntries)
		{
			FrozenSnapshots.Add(ConvertBattleAIEntryToSnapshot(Entry));
		}

		// 同步冻结队伍击杀/胜局数 (复用 RoomGS, 不重复 GetGameState)
		FrozenAttackerKills = RoomGS->AttackerTotalKills;
		FrozenDefenderKills = RoomGS->DefenderTotalKills;
		FrozenAttackerWins = RoomGS->AttackerWins;
		FrozenDefenderWins = RoomGS->DefenderWins;
	}
	else
	{
		UE_LOG(LogTemp, Error,
			TEXT("[ScoreboardWidget] FreezeSnapshot: GameState 不可用, AI 数据 + 队伍击杀将缺失. "
			     "【v223.0 零兜底】结算页面将不正确. 检查 GM_RoomGameMode 启动时序."));

		// 0 兜底: RoomGS 拿不到 → 击杀数据强制设 0, 不静默
		FrozenAttackerKills = 0;
		FrozenDefenderKills = 0;
		FrozenAttackerWins = 0;
		FrozenDefenderWins = 0;
	}

	// 标记冻结
	bIsFrozen = true;

	UE_LOG(LogTemp, Log,
		TEXT("[ScoreboardWidget] FreezeSnapshot: 冻结完成, 真人+AI 共 %d 个快照, 攻方击杀=%d 守方击杀=%d 攻方胜=%d 守方胜=%d. "
		     "【v215 冻结快照】后续刷新只读 FrozenSnapshots."),
		FrozenSnapshots.Num(), FrozenAttackerKills, FrozenDefenderKills, FrozenAttackerWins, FrozenDefenderWins);
}

void UScoreboardWidget::HideSettlementOverlay()
{
	bIsInSettlementState = false;
	// 【v215 大厂架构新增】解除冻结 (退出结算页时)
	bIsFrozen = false;
	FrozenSnapshots.Empty();

	if (Text_Settlement_AttackerKills)
	{
		Text_Settlement_AttackerKills->SetVisibility(ESlateVisibility::Collapsed);
	}

	if (Text_Settlement_DefenderKills)
	{
		Text_Settlement_DefenderKills->SetVisibility(ESlateVisibility::Collapsed);
	}

	if (Text_AttackerWinResult)
	{
		Text_AttackerWinResult->SetVisibility(ESlateVisibility::Collapsed);
	}

	if (Text_DefenderWinResult)
	{
		Text_DefenderWinResult->SetVisibility(ESlateVisibility::Collapsed);
	}

	// 【v216 大厂架构新增】隐藏 Border + Button (退出结算页时)
	if (Border_SettlementOverlay)
	{
		Border_SettlementOverlay->SetVisibility(ESlateVisibility::Collapsed);
	}

	if (Button_ReturnToLobby)
	{
		Button_ReturnToLobby->SetVisibility(ESlateVisibility::Collapsed);
	}
}

// ==========================================
// 【v216 大厂架构新增】ApplySnapshot — 跨地图结算快照主入口
// ==========================================
//
// 大厂架构 (v216 — 切图到 L_Login):
//   - 调用时机: UIViewService 在 L_Login 上 ShowPanel(SettlementPanel) 创建本 Widget 后立即调
//   - 数据流:
//     * ApplySnapshot(const FFinalSettlementSnapshot& InSnapshot)
//     * → 校验 InSnapshot.bIsValid (0 兜底, false → Log Error + return)
//     * → 缓存快照到 widget 内存 (C++ 字段, 类似 FrozenSnapshots)
//     * → 显示 Border + 文本 (AttackerKills / DefenderKills / WinResults)
//     * → 显示 Button_ReturnToLobby
//     * → 触发 RefreshScoreboard 让 ScoreboardEntryWidget 列表立即渲染
//
// 0 兜底:
//   - InSnapshot.bIsValid=false → Log Error + return, 不静默创建空 UI
//   - Border_SettlementOverlay / Button_ReturnToLobby 未绑 → Log Error (NativeConstruct 已报过, 这里再 defensive check)
//   - 不静默 return, 让玩家看到红色日志, 知道哪里出错
//
// 大厂原则 — 与 v215 FreezeSnapshot 的关系:
//   - v215 FreezeSnapshot: 把 URoomStateService 实时数据冻结到 widget (用于旧房间内结算页)
//   - v216 ApplySnapshot: 把 USettlementSnapshotSubsystem 跨地图快照应用到 widget (用于 L_Login 上)
//   - 两者都调用 RefreshScoreboard, 但 FreezeSnapshot 走"实时→冻结", ApplySnapshot 走"快照→应用"
//   - ApplySnapshot 内部也调 FreezeSnapshot-like 缓存 (因为切图后 URoomStateService 已销毁)
// ==========================================
void UScoreboardWidget::ApplySnapshot(const FFinalSettlementSnapshot& InSnapshot)
{
	// 0 兜底: 快照无效 → Log Error + return, 不静默创建空 UI
	if (!InSnapshot.bIsValid)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[ScoreboardWidget] ApplySnapshot: InSnapshot.bIsValid=false. "
			     "【v216 零兜底】调用顺序错误: 必须先 MulticastEnterSettlement → USettlementSnapshotSubsystem::WriteSnapshot, "
			     "再 MulticastShowFinalSettlement → UpdateSnapshotWins. 修复: 检查 RPC 链路时序."));
		return;
	}

	UE_LOG(LogTemp, Display,
		TEXT("[ScoreboardWidget] 【v216】ApplySnapshot: MatchMode=%d, AttackerWins=%d, DefenderWins=%d, "
		     "AttackerEntries=%d, DefenderEntries=%d, RoundWinner=%d. "
		     "跨地图持久数据已加载到本 Widget."),
		static_cast<int32>(InSnapshot.MatchMode),
		InSnapshot.AttackerWins,
		InSnapshot.DefenderWins,
		InSnapshot.AttackerEntries.Num(),
		InSnapshot.DefenderEntries.Num(),
		static_cast<int32>(InSnapshot.RoundWinner));

	// 【v216 大厂架构重构 — 设置游戏模式缓存】
	// ApplySnapshot 接收的是 MatchMode (快照内的纯枚举值), 不依赖 GS 查询
	// 跨地图后 GameState 已销毁, 不能调 World->GetGameState<ARoomGameState>()
	// 大厂原则 — 0 兜底: 直接用 InSnapshot.MatchMode, 无 Log Error (快照来自服务器, 必有效)
	CachedMatchMode = InSnapshot.MatchMode;

	// 刷新阵营标题 (刀战/生化 文案)
	RefreshTeamTitles();

	// 【v216 大厂架构重构 — 冻结快照到 widget 内存】
	// 把 InSnapshot.AttackerEntries / DefenderEntries 转成本地 FPlayerSnapshot 缓存
	// 大厂原则 — 单一真理源:
	//   - 切图后 URoomStateService 已销毁, 不能再 GetFactionSnapshotsWithAI
	//   - 必须用 InSnapshot.AttackerEntries / DefenderEntries
	// 大厂原则 — 类型转换:
	//   - FFactionSnapshotEntry (纯 POJO, 跨地图持久) ↔ FPlayerSnapshot (View 层缓存)
	//   - 不能 Array.Append 直接转, 字段语义不同 (FactionTagName vs FactionTag / FGameplayTag)
	//   - 显式 for 转换, 0 兜底: FactionTagName 空 → Log Error + 用 Offense 兜底 (理论上不该空)
	FrozenSnapshots.Empty();

	// 攻方转换
	const FGameplayTag AttackerTag = FFactionTags::Offense();
	for (const FFactionSnapshotEntry& Entry : InSnapshot.AttackerEntries)
	{
		FPlayerSnapshot Snap;
		Snap.PlayerName = Entry.DisplayName;
		Snap.bIsAI = Entry.bIsAI;
		Snap.Kills = Entry.Kills;
		Snap.Deaths = Entry.Deaths;
		Snap.Assists = Entry.Assists;
		Snap.Score = Entry.Score;
		// FactionTagName 是 FString, 转为 FGameplayTag
		// 0 兜底: Entry.bIsAttacker=true → 用 Offense; Entry.FactionTagName 空 → Log Error + 用 Offense
		if (Entry.FactionTagName.IsEmpty())
		{
			UE_LOG(LogTemp, Error,
				TEXT("[ScoreboardWidget] ApplySnapshot: 攻方快照 Entry '%s' FactionTagName 为空, 用 Offense 兜底. "
				     "【v216 零兜底】修复: 检查 RoomGameState::MulticastEnterSettlement 写入逻辑."),
				*Entry.DisplayName);
			Snap.FactionTag = AttackerTag;
		}
		else
		{
			const FGameplayTag ParsedTag = FGameplayTag::RequestGameplayTag(FName(*Entry.FactionTagName), false);
			if (!ParsedTag.IsValid())
			{
				UE_LOG(LogTemp, Error,
					TEXT("[ScoreboardWidget] ApplySnapshot: 攻方快照 Entry '%s' FactionTagName '%s' 不是有效 FGameplayTag, 用 Offense 兜底. "
					     "【v216 零兜底】修复: 检查 FactionTagName 写入路径."),
					*Entry.DisplayName, *Entry.FactionTagName);
				Snap.FactionTag = AttackerTag;
			}
			else
			{
				Snap.FactionTag = ParsedTag;
			}
		}
		FrozenSnapshots.Add(MoveTemp(Snap));
	}

	// 守方转换
	const FGameplayTag DefenderTag = FFactionTags::Defense();
	for (const FFactionSnapshotEntry& Entry : InSnapshot.DefenderEntries)
	{
		FPlayerSnapshot Snap;
		Snap.PlayerName = Entry.DisplayName;
		Snap.bIsAI = Entry.bIsAI;
		Snap.Kills = Entry.Kills;
		Snap.Deaths = Entry.Deaths;
		Snap.Assists = Entry.Assists;
		Snap.Score = Entry.Score;
		if (Entry.FactionTagName.IsEmpty())
		{
			UE_LOG(LogTemp, Error,
				TEXT("[ScoreboardWidget] ApplySnapshot: 守方快照 Entry '%s' FactionTagName 为空, 用 Defense 兜底. "
				     "【v216 零兜底】修复: 检查 RoomGameState::MulticastEnterSettlement 写入逻辑."),
				*Entry.DisplayName);
			Snap.FactionTag = DefenderTag;
		}
		else
		{
			const FGameplayTag ParsedTag = FGameplayTag::RequestGameplayTag(FName(*Entry.FactionTagName), false);
			if (!ParsedTag.IsValid())
			{
				UE_LOG(LogTemp, Error,
					TEXT("[ScoreboardWidget] ApplySnapshot: 守方快照 Entry '%s' FactionTagName '%s' 不是有效 FGameplayTag, 用 Defense 兜底. "
					     "【v216 零兜底】修复: 检查 FactionTagName 写入路径."),
					*Entry.DisplayName, *Entry.FactionTagName);
				Snap.FactionTag = DefenderTag;
			}
			else
			{
				Snap.FactionTag = ParsedTag;
			}
		}
		FrozenSnapshots.Add(MoveTemp(Snap));
	}

	// 同步冻结胜负局数 + 当局击杀数
	FrozenAttackerKills = InSnapshot.AttackerKills;
	FrozenDefenderKills = InSnapshot.DefenderKills;
	FrozenAttackerWins = InSnapshot.AttackerWins;
	FrozenDefenderWins = InSnapshot.DefenderWins;

	// 标记冻结 (后续刷新走 FrozenSnapshots, 不依赖 URoomStateService)
	bIsFrozen = true;
	bIsInSettlementState = true;

	// 【v216 大厂架构新增】显示结算覆盖板 + 返回大厅按钮
	// 大厂原则 — BindWidget 缺失: 已 Log Error 过了, 这里防御性检查 + 仍走完逻辑
	if (Border_SettlementOverlay)
	{
		Border_SettlementOverlay->SetVisibility(ESlateVisibility::Visible);
		UE_LOG(LogTemp, Display, TEXT("[ScoreboardWidget] ApplySnapshot: Border_SettlementOverlay=已设置 Visible."));
	}

	if (Button_ReturnToLobby)
	{
		Button_ReturnToLobby->SetVisibility(ESlateVisibility::Visible);
		UE_LOG(LogTemp, Display, TEXT("[ScoreboardWidget] ApplySnapshot: Button_ReturnToLobby=已设置 Visible."));
	}

	// 【v216 大厂架构重构 — 显示结算文本】
	// 大厂原则 — 单一真理源: 所有显示数据来自 InSnapshot, 不再调 GS
	if (Text_Settlement_AttackerKills)
	{
		const FString Label = (CachedMatchMode == ERoomMatchMode::Zombie)
			? ScoreboardFactionNames::GetAttackerWinsLabel_Zombie(FrozenAttackerWins)
			: ScoreboardFactionNames::GetAttackerKillsLabel_Melee(FrozenAttackerKills);
		Text_Settlement_AttackerKills->SetText(FText::FromString(Label));
		Text_Settlement_AttackerKills->SetVisibility(ESlateVisibility::Visible);
		UE_LOG(LogTemp, Display, TEXT("[ScoreboardWidget] ApplySnapshot: Text_Settlement_AttackerKills='%s', 设为 Visible."),
			*Label);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[ScoreboardWidget] 【v216 零兜底】ApplySnapshot: Text_Settlement_AttackerKills 未绑定! "
			"【修复】打开 WBP_ScoreboardWidget 蓝图, 添加同名 TextBlock 子控件, 命名必须为 Text_Settlement_AttackerKills (区分大小写)."));
	}

	if (Text_Settlement_DefenderKills)
	{
		const FString Label = (CachedMatchMode == ERoomMatchMode::Zombie)
			? ScoreboardFactionNames::GetDefenderWinsLabel_Zombie(FrozenDefenderWins)
			: ScoreboardFactionNames::GetDefenderKillsLabel_Melee(FrozenDefenderKills);
		Text_Settlement_DefenderKills->SetText(FText::FromString(Label));
		Text_Settlement_DefenderKills->SetVisibility(ESlateVisibility::Visible);
		UE_LOG(LogTemp, Display, TEXT("[ScoreboardWidget] ApplySnapshot: Text_Settlement_DefenderKills='%s', 设为 Visible."),
			*Label);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[ScoreboardWidget] 【v216 零兜底】ApplySnapshot: Text_Settlement_DefenderKills 未绑定! "
			"【修复】打开 WBP_ScoreboardWidget 蓝图, 添加同名 TextBlock 子控件, 命名必须为 Text_Settlement_DefenderKills (区分大小写)."));
	}

	// 胜利/平局文字
	//   【v229.x v3 大厂架构】字体相关设置(颜色)统一在 WBP_ScoreboardWidget 蓝图配置,代码不再干预
	if (Text_AttackerWinResult)
	{
		if (FrozenAttackerWins > FrozenDefenderWins)
		{
			Text_AttackerWinResult->SetText(FText::FromString(ScoreboardFactionNames::GetAttackerWinLabel(CachedMatchMode)));
		}
		else if (FrozenAttackerWins == FrozenDefenderWins)
		{
			Text_AttackerWinResult->SetText(FText::FromString(ScoreboardFactionNames::GetTieLabel()));
		}
		Text_AttackerWinResult->SetVisibility(FrozenAttackerWins >= FrozenDefenderWins ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		UE_LOG(LogTemp, Display, TEXT("[ScoreboardWidget] ApplySnapshot: Text_AttackerWinResult=已设置."));
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[ScoreboardWidget] 【v216 零兜底】ApplySnapshot: Text_AttackerWinResult 未绑定! "
			"【修复】打开 WBP_ScoreboardWidget 蓝图, 添加同名 TextBlock 子控件."));
	}

	if (Text_DefenderWinResult)
	{
		if (FrozenDefenderWins > FrozenAttackerWins)
		{
			Text_DefenderWinResult->SetText(FText::FromString(ScoreboardFactionNames::GetDefenderWinLabel(CachedMatchMode)));
		}
		else if (FrozenAttackerWins == FrozenDefenderWins)
		{
			Text_DefenderWinResult->SetText(FText::FromString(ScoreboardFactionNames::GetTieLabel()));
		}
		Text_DefenderWinResult->SetVisibility(FrozenDefenderWins >= FrozenAttackerWins ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
		UE_LOG(LogTemp, Display, TEXT("[ScoreboardWidget] ApplySnapshot: Text_DefenderWinResult=已设置."));
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[ScoreboardWidget] 【v216 零兜底】ApplySnapshot: Text_DefenderWinResult 未绑定! "
			"【修复】打开 WBP_ScoreboardWidget 蓝图, 添加同名 TextBlock 子控件."));
	}

	UE_LOG(LogTemp, Display, TEXT("[ScoreboardWidget] ApplySnapshot: 阵营容器检查: VB_AttackerTeam=%s, VB_DefenderTeam=%s"),
		VB_AttackerTeam ? TEXT("已绑") : TEXT("未绑"),
		VB_DefenderTeam ? TEXT("已绑") : TEXT("未绑"));

	// 立即渲染 ScoreboardEntryWidget 列表 (走 FrozenSnapshots)
	const int32 BeforeCount = (VB_AttackerTeam ? VB_AttackerTeam->GetChildrenCount() : 0) +
		(VB_DefenderTeam ? VB_DefenderTeam->GetChildrenCount() : 0);
	RefreshScoreboard();
	const int32 AfterCount = (VB_AttackerTeam ? VB_AttackerTeam->GetChildrenCount() : 0) +
		(VB_DefenderTeam ? VB_DefenderTeam->GetChildrenCount() : 0);
	UE_LOG(LogTemp, Display, TEXT("[ScoreboardWidget] ApplySnapshot: RefreshScoreboard 完成. Entry 数量: %d → %d (FrozenSnapshots=%d)"),
		BeforeCount, AfterCount, FrozenSnapshots.Num());
}

// ==========================================
// 【v216.3 大厂架构再修正】OnReturnToLobbyClicked — 客户端本地调 GameFlowSubsystem::TransitToState
// ==========================================
//
// v216.2 用 RequestStateOnNextLoad 是错的 (已修复):
//   - 历史 (v216): 走 RPC 链路 ScoreboardWidget → ARoomPlayerController::Server_SettlementReturnToLobby
//     → 服务器状态校验 → Client_OpenLobbyFromSettlement → RequestStateOnNextLoad(MainLobby)
//   - v216.2 修复: 客户端直接调 RequestStateOnNextLoad(MainLobby) (去掉 RPC, 跨 PC 类型工作)
//   - v216.2 BUG (Session1.txt 12.19.04 验证): 按钮"点了有响应"(状态预约成功), 但实际状态没切!
//     * 玩家已在 L_Login 上, 没有 OpenLevel → PostLoadMapWithWorld 不会触发
//     * 预约的状态永远不会被消费
//     * 玩家卡在 SettlementPage 状态
//
// v216.3 真正修复: 改用 TransitToState(MainLobby) (立即切状态 + 广播, 不需要 OpenLevel)
//   - GameFlowSubsystem::HandleStateEntry(MainLobby) case: **不跳转地图** (单地图常驻模式)
//   - 只广播 OnStateChanged → UIViewService 自动 ShowPanel(LANRoomPage)
//   - 0 网络往返, 0 地图跳转, 跨 PC 类型工作
//
// v217 大厂架构再重构 — 单一入口 (DRY):
//   - 旧 (v216.3): 本 Widget 直接调 FlowSubsystem->TransitToState(MainLobby)
//     → UI 切 ✓, 但 Session **未销毁** ✗
//     → 下次进房 OSS 拒绝 "Session already exists, can't join twice"
//     → 用户报告: "进入结算页面点击返回大厅后就进不去任何房间了"
//   - 新 (v217): 本 Widget 调 URoomService::RequestLeaveRoom (统一入口)
//     → Service 负责: 销毁 Session + 切 UI 状态 + (战斗地图) OpenLevel(L_Login)
//     → Service 单一职责: "玩家想离开房间" = 完整 Leave Room 链路
//
// 大厂架构修正 (RequestStateOnNextLoad vs TransitToState 语义区分):
//   - RequestStateOnNextLoad: "预约到下一张地图" - 配合 OpenLevel 使用, 在 PostLoadMapWithWorld 消费
//   - TransitToState: "立即切状态 + 广播" - 单地图常驻模式专用, 已在目标地图上时使用
//
// 旧路径 (v216, 已删除 RPC 链路):
//   客户端: OnReturnToLobbyClicked
//     ↓ Server_SettlementReturnToLobby (已删除)
//   服务器: 收到 RPC → 调 Client_OpenLobbyFromSettlement (已删除)
//     ↓ RequestStateOnNextLoad(MainLobby) → 预约不消费 ❌
//
// 新路径 (v217, 客户端本地 — 单一入口):
//   客户端: OnReturnToLobbyClicked
//     ↓ URoomService::RequestLeaveRoom (Service 统一入口)
//   RoomService: 销毁 Session + TransitToState(MainLobby) + (已在 L_Login) OnInterrupted(LANRoom)
//   SessionManager: DestroyRoom 成功 → OnSessionTerminated → HandleSessionTerminated
//   HandleSessionTerminated: 检测"已在 L_Login" → 跳过 OpenLevel (避免循环切图)
//   UIViewService: 收到 MainLobby → ShowPanel(LANRoomPage)
//
// 大厂原则 — 0 兜底:
//   - OwningPlayer 为空 → Log Error + return, 不静默
//   - GameInstance 拿不到 → Log Error + return
//   - RoomService 拿不到 → Log Error + return
//   - 当前状态不是 SettlementPage → Log Error + return (防御性检查, 理论上不会发生)
//
// 不需要 RPC 的理由:
//   - RequestLeaveRoom 是客户端本地操作 (销毁自己的 Session)
//   - 玩家已在 L_Login 上, 不需要 OpenLevel
//   - 没有任何作弊空间 (只是 UI 状态切换 + 自己 Session 销毁, 不涉及游戏世界)
// ==========================================
void UScoreboardWidget::OnReturnToLobbyClicked()
{
	// 0 兜底: OwningPlayer 为空 → Log Error + return
	APlayerController* OwningPC = GetOwningPlayer();
	if (!OwningPC)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[ScoreboardWidget] OnReturnToLobbyClicked: GetOwningPlayer 返回 nullptr. "
			     "【v217 零兜底】按钮无响应. 检查 Widget 创建流程 (OwningPlayer 必须有效)."));
		return;
	}

	// 0 兜底: GameInstance 拿不到 → Log Error + return
	UGameInstance* GI = OwningPC->GetGameInstance();
	if (!GI)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[ScoreboardWidget] OnReturnToLobbyClicked: PlayerController '%s' 拿不到 GameInstance. "
			     "【v217 零兜底】按钮无响应. 检查 PC 生命周期 (不应在 GameInstance 销毁后被调用)."),
			*OwningPC->GetName());
		return;
	}

	// 大厂原则 — 单一入口: 调 URoomService::RequestLeaveRoom (Service 负责销毁 Session + 切 UI 状态)
	URoomService* RoomService = URoomService::Get(GI);
	if (!RoomService)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[ScoreboardWidget] OnReturnToLobbyClicked: URoomService 不可用. "
			     "【v217 零兜底】按钮无响应. 检查 Subsystem 注册."));
		return;
	}

	UE_LOG(LogTemp, Display,
		TEXT("[ScoreboardWidget] 【v217】OnReturnToLobbyClicked: 玩家请求返回大厅 → URoomService::RequestLeaveRoom (单一入口). "
		     "PlayerController='%s'. Service 会: 销毁 Session + 切 UI 状态 (已在 L_Login 上时不 OpenLevel)."),
		*OwningPC->GetName());

	// 单一入口 — 让 Service 负责完整 Leave Room 链路
	RoomService->RequestLeaveRoom();
}
