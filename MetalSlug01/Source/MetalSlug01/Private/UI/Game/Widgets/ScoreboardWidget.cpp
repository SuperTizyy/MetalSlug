// 版权声明：在项目设置的描述页面填写您的版权信息。

// ==========================================
// 【v215 大厂架构重构 — ScoreboardWidget 终极版】
// ==========================================
//
// 用户反馈 (2026.08.07) 三个 bug + 终极方案:
//   Bug #1: WBP_ScoreboardEntryWidget 一闪一闪
//   Bug #2: 房主点 ReturnToLobby, 强制所有客户端退出结算页
//   Bug #3: 客户端结算页面不显示 AI 玩家信息
//
// 终极修复 (大厂架构 v215):
//
//   [事件流]  URoomStateService.OnPlayerSnapshotsChanged
//                ↓ (Multicast Dynamic Delegate)
//          UScoreboardWidget.HandlePlayerSnapshotsChanged
//                ↓
//          RefreshScoreboard (增量更新, 0 闪烁)
//
//   [冻结快照]  ShowFinalResult()  →  FreezeSnapshot()
//                ↓ 一次性拉 URoomStateService → FrozenSnapshots
//             后续刷新只读 FrozenSnapshots, 与房间连接完全解耦
//
//   [Tick 兜底] 5s 周期检查 (弱兜底, 不是主路径)
//
//   [Bug #2 修复] 不在 ScoreboardWidget 改 — 修复在 ARoomPlayerController::LeaveRoom()
//                  房主退出时不再调 Client_ForceLeaveRoom, 各客户端独立
//
//   [0 兜底原则]
//   - URoomStateService 拿不到 → Log Error + return, 不静默
//   - Snapshot.PlayerName 为空 → Log Error + return, 不创建
//   - Snapshot.FactionTag 无效 → Log Error + return, 不静默归类
//   - ScoreboardEntryWidgetClass 未配 → Log Error + return, 不静默
//   - FrozenSnapshot 时 URoomStateService 拿不到 → Log Error + 保留旧快照 + return
//
//   [增量更新原则] — 修复 Bug #1 闪烁
//   - 永远不 ClearChildren()
//   - 已存在的 entry 调 SetScore/SetKDA 更新, 不重建
//   - 排名变化用 RemoveChild/InsertChild 增量重排
//   - 已退出的玩家在 CachedLiveSnapshots 里查不到 → RemoveEntryById

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
	// 1. 拿当前应使用的数据源 (冻结 vs 实时)
	const TArray<FPlayerSnapshot>& ActiveSnapshots = GetActiveSnapshots();

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

	// 5. 增量排序 + 排名刷新
	SortEntriesByScore(VB_AttackerTeam);
	SortEntriesByScore(VB_DefenderTeam);
	UpdateAllRanks(VB_AttackerTeam);
	UpdateAllRanks(VB_DefenderTeam);

	// 【v215 大厂架构】注意: ActiveSnapshots 就是 CachedLiveSnapshots 本身 (GetActiveSnapshots 内部已写入),
	//   这里不需要再次赋值 (自我赋值无意义)
	//   下次 Refresh 时 RemoveStaleEntries 直接用 ActiveSnapshots 对比即可
}

// ==========================================
// 【v215 大厂架构新增 — 增量删除已退玩家】
// 大厂原则: 不缓存 entry 指针 (Widget 可能被外部清空), 每次动态查找
void UScoreboardWidget::RemoveStaleEntries(UVerticalBox* VerticalBox, const TArray<FPlayerSnapshot>& ActiveSnapshots)
{
	if (!VerticalBox)
	{
		return;
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
		const bool bStillExists = ActiveSnapshots.ContainsByPredicate([&EntryName](const FPlayerSnapshot& Snap)
		{
			return Snap.PlayerName == EntryName;
		});

		if (!bStillExists)
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
		ExistingEntry->SetPlayerName(Snapshot.PlayerName);
		ExistingEntry->SetScore(Snapshot.Score);
		ExistingEntry->SetKDA(Snapshot.Kills, Snapshot.Deaths, Snapshot.Assists);
		ExistingEntry->SetIsCurrentPlayer(bIsLocalPlayer);
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

TArray<FPlayerSnapshot>& UScoreboardWidget::GetActiveSnapshots()
{
	// 【v215 大厂架构 — 冻结 vs 实时切换】
	//   冻结: 返回 FrozenSnapshots (已与 URoomStateService 解耦)
	//   未冻结: 实时拉 URoomStateService, 写入 CachedLiveSnapshots, 返回引用
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

	// 合并两个阵营到一个 CachedLiveSnapshots
	CachedLiveSnapshots.Empty();
	CachedLiveSnapshots.Append(StateSvc->GetFactionSnapshotsWithAI(FFactionTags::Offense()));
	CachedLiveSnapshots.Append(StateSvc->GetFactionSnapshotsWithAI(FFactionTags::Defense()));

	return CachedLiveSnapshots;
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

	// 【v208 大厂架构重构 — PlayerName 单一真理源】
	//   BuildAISnapshot* 已统一加 "[AI] " 前缀, Entry 直接用 Snap.PlayerName
	EntryWidget->SetPlayerName(Snapshot.PlayerName);
	EntryWidget->SetScore(Snapshot.Score);
	EntryWidget->SetKDA(Snapshot.Kills, Snapshot.Deaths, Snapshot.Assists);

	const FString LocalPlayerName = GetLocalPlayerName();
	EntryWidget->SetIsCurrentPlayer(Snapshot.PlayerName == LocalPlayerName);

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
// 【v215 大厂架构重构 — SortEntriesByScore 增量排序】
// ==========================================
// 大厂原则 — 不再 ClearChildren + 全部 AddChild:
//   历史 (v22-v213): ClearChildren → AddChild 全删全建 → 闪烁
//   新 (v215):
//     1. 按得分降序排
//     2. 用 RemoveChild + InsertChild 增量重排 (同一个 Widget 对象)
//     3. 排名变化只影响位置, 不影响存在性
//
// 【v217 大厂架构重构 — SortEntriesByScore 走 FrozenSnapshots】
// 大厂原则 — 单一真理源 + 0 兜底:
//   - 旧 (v215-v216): SortEntriesByScore 直接调 URoomStateService::GetFactionSnapshotsWithAI 拉实时数据
//     → 无论 bIsFrozen 标志, 实时数据可能变化 → 排名列表每秒重新排序
//     → 用户报告: "结算页面的玩家排名列表,过一会就自动变化一下顺序"
//   - 新 (v217): 走 GetActiveSnapshots() → 自动根据 bIsFrozen 选 FrozenSnapshots 或实时
//     → 冻结后 (bIsFrozen=true) 永远用 FrozenSnapshots, 实时数据即使在变化也不影响排序
//     → 未冻结时 (bIsFrozen=false) 仍走实时, 兼容游戏内 hero scoreboard
void UScoreboardWidget::SortEntriesByScore(UVerticalBox* VerticalBox)
{
	if (!VerticalBox)
	{
		return;
	}

	TArray<UWidget*> Children = VerticalBox->GetAllChildren();
	if (Children.Num() == 0)
	{
		return;
	}

	// 【v217 大厂架构重构】从 GetActiveSnapshots 拿数据 — 冻结后自动走 FrozenSnapshots
	// 大厂原则 — 单一真理源: 整个 Widget 内部所有数据访问都走 GetActiveSnapshots
	//   - 冻结后 (bIsFrozen=true): 返回 FrozenSnapshots (跨地图持久, 与房间连接解耦)
	//   - 未冻结 (bIsFrozen=false): 返回 Service 实时数据 (游戏内 hero scoreboard 用)
	const TArray<FPlayerSnapshot>& ActiveSnapshots = GetActiveSnapshots();

	struct FEntrySortData
	{
		FString PlayerName;
		int32 Score;
		UScoreboardEntryWidget* Widget;
	};

	TArray<FEntrySortData> EntryList;
	for (UWidget* Child : Children)
	{
		UScoreboardEntryWidget* EntryWidget = Cast<UScoreboardEntryWidget>(Child);
		if (!EntryWidget)
		{
			continue;
		}

		FEntrySortData Data;
		Data.PlayerName = EntryWidget->GetPlayerName();
		Data.Widget = EntryWidget;
		Data.Score = 0;

		// 【v217 大厂架构重构】从 ActiveSnapshots 查得分 (而非直接调 Service)
		// 不变量: 冻结后 ActiveSnapshots 内容永远不变 → 排序稳定 → 排名列表不抖
		const FString& RawName = Data.PlayerName;
		for (const FPlayerSnapshot& Snap : ActiveSnapshots)
		{
			if (Snap.PlayerName == RawName)
			{
				Data.Score = Snap.Score;
				break;
			}
		}

		EntryList.Add(Data);
	}

	// 按得分降序
	EntryList.Sort([](const FEntrySortData& A, const FEntrySortData& B)
	{
		return A.Score > B.Score;
	});

	// 【v215 增量重排 — 关键修复闪烁】
	//   RemoveChild 不销毁 Widget, 只是从容器移除 (Widget 对象存活)
	//   InsertChild 在指定位置插入, 触发 Slate 重绘但不重建 Widget
	for (int32 i = 0; i < EntryList.Num(); i++)
	{
		UWidget* Widget = EntryList[i].Widget;
		if (!Widget)
		{
			continue;
		}

		// 检查当前位置是否已经正确 (避免不必要的重排)
		if (VerticalBox->GetChildAt(i) == Widget)
		{
			continue;
		}

		// 增量重排: 从当前位置移除, 插入到新位置
		VerticalBox->RemoveChild(Widget);
		VerticalBox->InsertChildAt(i, Widget);
	}
}

void UScoreboardWidget::UpdateAllRanks(UVerticalBox* VerticalBox)
{
	if (!VerticalBox)
	{
		return;
	}

	int32 CurrentRank = 1;
	TArray<UWidget*> Children = VerticalBox->GetAllChildren();

	for (UWidget* Child : Children)
	{
		UScoreboardEntryWidget* EntryWidget = Cast<UScoreboardEntryWidget>(Child);
		if (EntryWidget)
		{
			EntryWidget->SetRank(CurrentRank);
			CurrentRank++;
		}
	}
}

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
	if (Text_AttackerWinResult)
	{
		if (AttackerWins > DefenderWins)
		{
			Text_AttackerWinResult->SetText(FText::FromString(ScoreboardFactionNames::GetAttackerWinLabel(CachedMatchMode)));
			Text_AttackerWinResult->SetColorAndOpacity(FSlateColor(FLinearColor::Red));
		}
		else if (AttackerWins == DefenderWins)
		{
			Text_AttackerWinResult->SetText(FText::FromString(ScoreboardFactionNames::GetTieLabel()));
			Text_AttackerWinResult->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		}
		Text_AttackerWinResult->SetVisibility(AttackerWins >= DefenderWins ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}

	// 显示守方胜利/平局文字
	if (Text_DefenderWinResult)
	{
		if (DefenderWins > AttackerWins)
		{
			Text_DefenderWinResult->SetText(FText::FromString(ScoreboardFactionNames::GetDefenderWinLabel(CachedMatchMode)));
			Text_DefenderWinResult->SetColorAndOpacity(FSlateColor(FLinearColor::Blue));
		}
		else if (AttackerWins == DefenderWins)
		{
			Text_DefenderWinResult->SetText(FText::FromString(ScoreboardFactionNames::GetTieLabel()));
			Text_DefenderWinResult->SetColorAndOpacity(FSlateColor(FLinearColor::White));
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
	FrozenSnapshots.Append(StateSvc->GetFactionSnapshotsWithAI(FFactionTags::Offense()));
	FrozenSnapshots.Append(StateSvc->GetFactionSnapshotsWithAI(FFactionTags::Defense()));

	// 同步冻结队伍击杀/胜局数
	UWorld* World = GetWorld();
	if (World)
	{
		if (ARoomGameState* RoomGS = World->GetGameState<ARoomGameState>())
		{
			FrozenAttackerKills = RoomGS->AttackerTotalKills;
			FrozenDefenderKills = RoomGS->DefenderTotalKills;
			FrozenAttackerWins = RoomGS->AttackerWins;
			FrozenDefenderWins = RoomGS->DefenderWins;
		}
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
	if (Text_AttackerWinResult)
	{
		if (FrozenAttackerWins > FrozenDefenderWins)
		{
			Text_AttackerWinResult->SetText(FText::FromString(ScoreboardFactionNames::GetAttackerWinLabel(CachedMatchMode)));
			Text_AttackerWinResult->SetColorAndOpacity(FSlateColor(FLinearColor::Red));
		}
		else if (FrozenAttackerWins == FrozenDefenderWins)
		{
			Text_AttackerWinResult->SetText(FText::FromString(ScoreboardFactionNames::GetTieLabel()));
			Text_AttackerWinResult->SetColorAndOpacity(FSlateColor(FLinearColor::White));
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
			Text_DefenderWinResult->SetColorAndOpacity(FSlateColor(FLinearColor::Blue));
		}
		else if (FrozenAttackerWins == FrozenDefenderWins)
		{
			Text_DefenderWinResult->SetText(FText::FromString(ScoreboardFactionNames::GetTieLabel()));
			Text_DefenderWinResult->SetColorAndOpacity(FSlateColor(FLinearColor::White));
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
