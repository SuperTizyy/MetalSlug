// 版权声明：在项目设置的描述页面填写您的版权信息。

// ==========================================
// 【v202.0 大厂架构重构 — ScoreboardWidget】
// ==========================================
//
// 用户反馈 (2026.08.07):
//   "WBP_ScoreboardWidget 需要显示 AI 信息, 走 RPC 链路, 不能有兜底"
//
// 修复前 (v22-v201.x) 重复架构清单:
//   1. 直接遍历 GS->PlayerArray → 永远不读 AI (用户报告)
//   2. OnPlayerScoreChanged 死代码 (从来没人订阅)
//   3. AddKillScore/AddAssistScore/AddDeath 3 处冗余服务器手动 Broadcast
//   4. 数据源分裂: AI 没有 PlayerState → 计数永远没累加
//   5. AIC 没有 bReplicates / 没有 Replicated 字段 → 客户端不可能读到 AI 计数
//
// 修复后 (v202.0) 单一真理源:
//   - View 数据源 = URoomStateService::GetFactionSnapshotsWithAI (CQRS 读取端)
//   - 真人字段: ARoomPlayerState::RoomKills/RoomDeaths/RoomAssists/RoomScore (已有 Replicated)
//   - AI 字段:   ABaseAIController::AIKills/AIDeaths/AIAssists/AIScore (本次新增 Replicated)
//   - RPC 链路: ReplicatedUsing → 引擎自动 Replicate → 客户端 OnRep → UI 主动 Refresh
//   - 同一 Snapshot 结构, 不同真理源 → 排序/排名/显示逻辑完全统一

#include "UI/Game/Widgets/ScoreboardWidget.h"
#include "UI/Game/Widgets/SubWidgets/ScoreboardEntryWidget.h"
#include "Components/VerticalBox.h"
#include "Components/TextBlock.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerController.h"
// 【v202.1 修复】补完整类型 — GetLocalPlayerName 用 APlayerState, ShowRoundSettlement/ShowFinalResult 用 ARoomGameState
#include "GameFramework/PlayerState.h"
#include "Systems/RoomGameState.h"
#include "Services/RoomStateService.h"
#include "Data/Faction/FactionTags.h"

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

	return true;
}

void UScoreboardWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// 初始化结算文本为隐藏状态（等待 ShowRoundSettlement / ShowFinalResult 时才显示）
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

	// 【v202.0 大厂架构】延迟刷新: 等待服务器数据同步完成
	// 大厂原则: 不再需要双 Refresh 路径 (旧版 0.5s timer 是为了 PS 同步延迟)
	//   现在走快照路径, URoomStateService 内部处理 GS/GM 不存在的边界 case
	FTimerHandle RefreshTimer;
	GetWorld()->GetTimerManager().SetTimer(RefreshTimer, this, &UScoreboardWidget::RefreshScoreboard, 0.5f, false);

	// 【v203.0 大厂架构新增】初始化模式缓存 + 阵营标题
	//   - CachedMatchMode 来自 GS->CurrentMatchMode (Replicated)
	//   - 模式缓存后, 后续 RefreshTeamTitles / ShowRoundSettlement / ShowFinalResult 直接读
	//   - 不允许在多处各自查 GS, 单一真理源 = CachedMatchMode
	RefreshCachedMatchMode();
	RefreshTeamTitles();
}

// ==========================================
// 【v202.0 大厂架构 — Tick 兜底轮询】
// ==========================================
// 大厂原则 — 事件 + 拉取 双轨制 (镜像 GameHUDWidget::TickInvincibilityWatchdog):
//   - 事件流: 用户主动 ShowScoreboard → RefreshScoreboard (及时)
//   - 拉取流: 0.5s 周期主动拉快照, 兜底事件流丢失 (AI 数据可能错过 OnRep)
//
// 性能影响:
//   - 只在 widget 可见时执行 (Visibility 检查)
//   - 0.5s 间隔 = 2Hz, 完全不影响性能 (单 URoomStateService::GetXxx 调用)
//
// 零重复架构:
//   - 不订阅 PlayerState / AIC 的 OnRep 委托 (那种方案会导致 N 个订阅点)
//   - 单点拉取: 快照已经聚合真人 + AI, 一次调用拿到全部
void UScoreboardWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);

	// 大厂原则 — 性能优化: 只在 widget 可见时刷新 (隐藏时不浪费 CPU)
	if (GetVisibility() == ESlateVisibility::Hidden ||
	    GetVisibility() == ESlateVisibility::Collapsed)
	{
		return;
	}

	// 节流: 0.5s 拉一次
	static float AccumulatedTime = 0.0f;
	AccumulatedTime += InDeltaTime;
	if (AccumulatedTime < 0.5f)
	{
		return;
	}
	AccumulatedTime = 0.0f;

	RefreshScoreboard();
}

// ==========================================
// 1. 公共接口
// ==========================================

void UScoreboardWidget::RefreshScoreboard()
{
	// 清空现有数据
	ClearScoreboard();

	// 从 RoomStateService 获取最新数据 (真人 + AI 单一真理源)
	RefreshFromRoomStateService();
}

/**
 * 【v202.0 大厂架构】从 URoomStateService 获取真人 + AI 数据并刷新 UI
 *
 * 数据流:
 *   URoomStateService::GetFactionSnapshotsWithAI(FactionTag)
 *     ↓ 真人 (PlayerArray) + AI 占位 (PendingAIQueue) + 战斗 AI (AIC Replicated 字段)
 *   单一 FPlayerSnapshot 列表 → UI 渲染
 *
 * 大厂原则:
 *   - View 不感知数据层 (CQRS 读取端隔离)
 *   - 真人 / AI 同一 Snapshot 结构 → 渲染逻辑统一
 */
void UScoreboardWidget::RefreshFromRoomStateService()
{
	URoomStateService* StateSvc = URoomStateService::Get(this);
	if (!StateSvc)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[ScoreboardWidget] RefreshFromRoomStateService: URoomStateService 不可用 (World 还未初始化?), 跳过刷新"));
		return;
	}

	// 一次性获取两个阵营的快照 (真人 + AI 全部)
	const TArray<FPlayerSnapshot> AttackerSnapshots = StateSvc->GetFactionSnapshotsWithAI(FFactionTags::Offense());
	const TArray<FPlayerSnapshot> DefenderSnapshots = StateSvc->GetFactionSnapshotsWithAI(FFactionTags::Defense());

	// 记录本地玩家名 (用于高亮)
	const FString LocalPlayerName = GetLocalPlayerName();

	UE_LOG(LogTemp, Verbose,
		TEXT("[ScoreboardWidget] RefreshFromRoomStateService: Attacker.Num=%d Defender.Num=%d LocalPlayer='%s'"),
		AttackerSnapshots.Num(), DefenderSnapshots.Num(), *LocalPlayerName);

	// 攻方 → VB_AttackerTeam
	for (const FPlayerSnapshot& Snap : AttackerSnapshots)
	{
		const bool bIsLocal = (Snap.PlayerName == LocalPlayerName);
		UpdateOrCreateEntryFromSnapshot(Snap, bIsLocal);
	}

	// 守方 → VB_DefenderTeam
	for (const FPlayerSnapshot& Snap : DefenderSnapshots)
	{
		const bool bIsLocal = (Snap.PlayerName == LocalPlayerName);
		UpdateOrCreateEntryFromSnapshot(Snap, bIsLocal);
	}

	// 排序并更新排名 (统一逻辑, 真人/AI 一视同仁)
	SortEntriesByScore(VB_AttackerTeam);
	SortEntriesByScore(VB_DefenderTeam);
	UpdateAllRanks(VB_AttackerTeam);
	UpdateAllRanks(VB_DefenderTeam);
}

/**
 * 【v202.0 大厂架构】根据 FPlayerSnapshot 更新或创建条目
 *
 * 大厂原则:
 *   - View 只读 POJO 数据, 不感知 ARoomPlayerState / ABaseAIController
 *   - 真人 (bIsAI=false) 和 AI (bIsAI=true) 共用同一 Snapshot, 渲染统一
 *
 * 阵营判断: Snap.FactionTag == FFactionTags::Offense() → 攻方
 *           Snap.FactionTag == FFactionTags::Defense() → 守方
 *           其他: 大厂原则 — 显式 Log Error (不静默兜底)
 */
void UScoreboardWidget::UpdateOrCreateEntryFromSnapshot(const FPlayerSnapshot& Snapshot, bool bIsLocalPlayer)
{
	if (Snapshot.PlayerName.IsEmpty())
	{
		UE_LOG(LogTemp, Error,
			TEXT("[ScoreboardWidget] UpdateOrCreateEntryFromSnapshot: Snapshot.PlayerName 为空 (bIsAI=%d). "
				 "【v202.0 修复】FPlayerSnapshot.PlayerName 必须是 DisplayName (AI) 或 PlayerName (真人), "
				 "不允许空字符串进入 UI 渲染."),
			Snapshot.bIsAI ? 1 : 0);
		return;
	}

	// 大厂原则 — 显式优于默认: 无效阵营 → Log Error + 跳过 (不允许静默归到某阵营)
	if (!FFactionTags::IsValidFaction(Snapshot.FactionTag))
	{
		UE_LOG(LogTemp, Error,
			TEXT("[ScoreboardWidget] UpdateOrCreateEntryFromSnapshot: Snapshot.PlayerName='%s' (bIsAI=%d) "
				 "的 FactionTag='%s' 不是 Offense/Defense 有效阵营, 跳过渲染."),
			*Snapshot.PlayerName, Snapshot.bIsAI ? 1 : 0, *Snapshot.FactionTag.ToString());
		return;
	}

	const bool bIsAttacker = (Snapshot.FactionTag == FFactionTags::Offense());

	// 获取对应的 VerticalBox
	UVerticalBox* TargetBox = bIsAttacker ? VB_AttackerTeam : VB_DefenderTeam;
	UVerticalBox* WrongBox = bIsAttacker ? VB_DefenderTeam : VB_AttackerTeam;
	if (!TargetBox)
	{
		return;
	}

	// 先在错误容器中查找并移除 (防止之前错分到对面容器)
	if (WrongBox)
	{
		for (int32 i = WrongBox->GetChildrenCount() - 1; i >= 0; i--)
		{
			UScoreboardEntryWidget* Entry = Cast<UScoreboardEntryWidget>(WrongBox->GetChildAt(i));
			if (Entry && Entry->GetPlayerName() == Snapshot.PlayerName)
			{
				Entry->RemoveFromParent();
				break;
			}
		}
	}

	// 在正确容器中查找是否已存在
	UScoreboardEntryWidget* ExistingEntry = nullptr;
	for (int32 i = 0; i < TargetBox->GetChildrenCount(); i++)
	{
		UScoreboardEntryWidget* Entry = Cast<UScoreboardEntryWidget>(TargetBox->GetChildAt(i));
		if (Entry && Entry->GetPlayerName() == Snapshot.PlayerName)
		{
			ExistingEntry = Entry;
			break;
		}
	}

	if (ExistingEntry)
	{
		// 更新现有条目
		ExistingEntry->SetScore(Snapshot.Score);
		ExistingEntry->SetKDA(Snapshot.Kills, Snapshot.Deaths, Snapshot.Assists);
		ExistingEntry->SetIsCurrentPlayer(bIsLocalPlayer);
	}
	else
	{
		// 创建新条目
		CreateEntryWidgetFromSnapshot(Snapshot, bIsAttacker);
	}
}

UScoreboardEntryWidget* UScoreboardWidget::CreateEntryWidgetFromSnapshot(const FPlayerSnapshot& Snapshot, bool bIsAttacker)
{
	if (Snapshot.PlayerName.IsEmpty())
	{
		UE_LOG(LogTemp, Error,
			TEXT("[ScoreboardWidget] CreateEntryWidgetFromSnapshot: Snapshot.PlayerName 为空, 拒绝创建. "
				 "【v202.0 修复】UPlayerLabelWidget 数据源单一 (FPlayerSnapshot), 不允许空名字渲染."));
		return nullptr;
	}

	// 实时校验阵营归属 (防止 AI/真人跨阵营误判)
	const bool bActualIsAttacker = (Snapshot.FactionTag == FFactionTags::Offense());
	if (bActualIsAttacker != bIsAttacker)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[ScoreboardWidget] CreateEntryWidgetFromSnapshot: Snapshot '%s' (bIsAI=%d) 阵营不一致, 参数=%d 实际=%d, 已修正"),
			*Snapshot.PlayerName, Snapshot.bIsAI ? 1 : 0, bIsAttacker, bActualIsAttacker);
		bIsAttacker = bActualIsAttacker;
	}

	UVerticalBox* TargetBox = bIsAttacker ? VB_AttackerTeam : VB_DefenderTeam;
	if (!TargetBox)
	{
		return nullptr;
	}

	if (!ScoreboardEntryWidgetClass)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[ScoreboardWidget] CreateEntryWidgetFromSnapshot: ScoreboardEntryWidgetClass 未配置！请在 WBP_ScoreboardWidget 蓝图中设置. "
				 "【v202.0 修复】不再静默 return, 显式 Log Error 强制修复 BP 配置."));
		return nullptr;
	}

	UScoreboardEntryWidget* EntryWidget = CreateWidget<UScoreboardEntryWidget>(this, ScoreboardEntryWidgetClass);
	if (!EntryWidget)
	{
		return nullptr;
	}

	// 【v208 大厂架构重构 — PlayerName 单一真理源】
	//   历史 (v202.0): 在此处拼接 "[AI] " 前缀 → 与 UpdateOrCreateEntryFromSnapshot 中比较 Entry->GetPlayerName() == Snapshot.PlayerName 不一致
	//     - Entry->GetPlayerName() = "[AI] AIC_AI_SWAT_AI_3" (有前缀)
	//     - Snapshot.PlayerName = "AIC_AI_SWAT_AI_3" (无前缀)
	//     - 比较失败 → 每次 Refresh 找不到现有 Entry → 反复创建新 Widget → AI entry 堆积
	//
	//   新 (v208): 拼接前缀的责任上移到 BuildAISnapshot / BuildAISnapshotFromController
	//     - Snap.PlayerName 永远带 "[AI] " 前缀 (数据源统一)
	//     - 本函数直接用 Snap.PlayerName, 不再拼接 (避免双前缀)
	//     - 比较 Entry->GetPlayerName() == Snapshot.PlayerName 永远命中
	//     - 大厂原则 — 单一真理源: "[AI] " 前缀只在 BuildAISnapshot* 一处拼
	//
	// 视觉差异仍由 bIsAI 字段驱动 (本函数不再做拼前缀, 真理源已就位)
	EntryWidget->SetPlayerName(Snapshot.PlayerName);
	EntryWidget->SetScore(Snapshot.Score);
	EntryWidget->SetKDA(Snapshot.Kills, Snapshot.Deaths, Snapshot.Assists);

	const FString LocalPlayerName = GetLocalPlayerName();
	// 【v208】注意: SetIsCurrentPlayer 仍直接比较 Snapshot.PlayerName vs LocalPlayerName
	//   - 真人: Snap.PlayerName = "TestUser_E7B5" = LocalPlayerName → bIsLocal=true
	//   - AI:   Snap.PlayerName = "[AI] AIC_AI_SWAT_AI_3" != LocalPlayerName → bIsLocal=false
	//   - 大厂原则 — 视觉差异 = 字段驱动 (bIsAI), Snapshot 已含完整信息
	EntryWidget->SetIsCurrentPlayer(Snapshot.PlayerName == LocalPlayerName);

	TargetBox->AddChild(EntryWidget);

	return EntryWidget;
}

void UScoreboardWidget::ClearScoreboard()
{
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
// 大厂原则 — 单一真理源:
//   - CachedMatchMode 是 ScoreboardWidget 内唯一表示"当前模式"的字段
//   - GS->CurrentMatchMode (Replicated) 是服务器真理源, 通过此函数单向同步到 CachedMatchMode
//   - 模式变化时才更新 (避免无变化时重复读 GS)
//
// 为什么不是 Tick 每帧读 GS:
//   - GS->CurrentMatchMode 一局不变 (开局 SetCurrentMatchMode 后不变), 没必每帧读
//   - 模式切换 = 玩家点"开始游戏"后进入新局, 此时调 ShowRoundSettlement 时同步读一次即可
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
		// 不空白 UI, 也不报错 (客户端首帧常见, NativeConstruct 时 GS 可能未到达)
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

/**
 * 【v203.0 大厂架构新增】根据当前模式刷新阵营标题 TextBlock
 *
 * 大厂原则:
 *   - 单一入口: 所有阵营标题文案走 namespace ScoreboardFactionNames, 不在 cpp 其他位置拼字符串
 *   - Text_AttackerTeamTitle / Text_DefenderTeamTitle 用 BindWidgetOptional (允许未绑, 不报错)
 *   - BindWidgetOptional: 旧 WBP 没这两个控件时编译不挂, 仅 Log Warning 提示
 */
void UScoreboardWidget::RefreshTeamTitles()
{
	if (Text_AttackerTeamTitle)
	{
		Text_AttackerTeamTitle->SetText(FText::FromString(ScoreboardFactionNames::GetAttackerTeamTitle(CachedMatchMode)));
	}
	else
	{
		// BindWidgetOptional: 不强制要求绑定, 旧 WBP 没这控件时不报错, 仅 Verbose 日志
		UE_LOG(LogTemp, Verbose,
			TEXT("[ScoreboardWidget] RefreshTeamTitles: Text_AttackerTeamTitle 未绑定 (WBP 旧版本?), 跳过刷新. "
			     "【v203.0 新增控件】如需显示阵营标题, 在 WBP_ScoreboardWidget 内加 Text_AttackerTeamTitle TextBlock."));
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

void UScoreboardWidget::SortEntriesByScore(UVerticalBox* VerticalBox)
{
	if (!VerticalBox)
	{
		return;
	}

	// 获取所有子控件
	TArray<UWidget*> Children = VerticalBox->GetAllChildren();
	if (Children.Num() == 0)
	{
		return;
	}

	// 收集条目数据 + 名字
	// 大厂原则 — 简化: 现在数据都来自 Snapshot, 不再二次查 GameState
	struct FEntrySortData
	{
		FString PlayerName;
		int32 Score;
		UScoreboardEntryWidget* Widget;
		bool bBelongsToThisBox;
	};

	URoomStateService* StateSvc = URoomStateService::Get(this);
	const FString LocalPlayerName = GetLocalPlayerName();

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
		Data.bBelongsToThisBox = false;

	// 从 RoomStateService 快照查最新数据 (单一真理源)
		if (StateSvc)
		{
			// 攻/守两个阵营都查一遍, 找到匹配的
			const TArray<FPlayerSnapshot> AttackerSnaps = StateSvc->GetFactionSnapshotsWithAI(FFactionTags::Offense());
			const TArray<FPlayerSnapshot> DefenderSnaps = StateSvc->GetFactionSnapshotsWithAI(FFactionTags::Defense());

			// 【v208 大厂架构重构 — PlayerName 单一真理源】
			//   历史 (v202.0): Entry->GetPlayerName() 带 "[AI] " 前缀, Snapshot.PlayerName 不带 → 需要剥离
			//   新 (v208): 双方都带 "[AI] " 前缀 (BuildAISnapshot* 已统一) → 直接比较
			//   大厂原则 — 单一真理源: 不在前缀转换上各算各的
			const FString& RawName = Data.PlayerName;

			// 线性搜索两个阵营 (数据量小, O(N) 完全够)
			for (const FPlayerSnapshot& Snap : AttackerSnaps)
			{
				if (Snap.PlayerName == RawName)
				{
					Data.Score = Snap.Score;
					Data.bBelongsToThisBox = (VerticalBox == VB_AttackerTeam);
					break;
				}
			}
			if (!Data.bBelongsToThisBox)
			{
				for (const FPlayerSnapshot& Snap : DefenderSnaps)
				{
					if (Snap.PlayerName == RawName)
					{
						Data.Score = Snap.Score;
						Data.bBelongsToThisBox = (VerticalBox == VB_DefenderTeam);
						break;
					}
				}
			}
		}

		EntryList.Add(Data);
	}

	// 清空当前容器
	VerticalBox->ClearChildren();

	// 按归属分类
	TArray<FEntrySortData> BelongsList;
	TArray<FEntrySortData> NotBelongsList;
	for (const FEntrySortData& Data : EntryList)
	{
		if (Data.bBelongsToThisBox)
		{
			BelongsList.Add(Data);
		}
		else
		{
			NotBelongsList.Add(Data);
		}
	}

	BelongsList.Sort([](const FEntrySortData& A, const FEntrySortData& B)
	{
		return A.Score > B.Score;
	});

	// 重新添加属于当前容器的条目
	for (const FEntrySortData& Data : BelongsList)
	{
		VerticalBox->AddChild(Data.Widget);
	}

	// 将不属于当前容器的条目移动到正确容器
	for (const FEntrySortData& Data : NotBelongsList)
	{
		UVerticalBox* CorrectBox = (VerticalBox == VB_AttackerTeam) ? VB_DefenderTeam : VB_AttackerTeam;
		if (CorrectBox && Data.Widget)
		{
			CorrectBox->AddChild(Data.Widget);
		}
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
	// 获取当前玩家控制器
	if (APlayerController* PC = UGameplayStatics::GetPlayerController(this, 0))
	{
		// 获取玩家状态
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

	// 【v203.0 大厂架构】模式可能在 ShowRoundSettlement 调用时已变化 (切局时模式不变, 但缓存要刷新)
	//   单一真理源: CachedMatchMode 必须与 GS->CurrentMatchMode 同步
	RefreshCachedMatchMode();
	RefreshTeamTitles();

	// 强制从 GameState 获取最新数据 (避免参数传递链路中数据丢失或同步延迟)
	// 【v203.0 大厂架构重构】根据模式读不同真理源:
	//   - 刀战: AttackerTotalKills / DefenderTotalKills (当局击杀数, 用户要求"攻方阵营击杀总数")
	//   - 生化: AttackerWins / DefenderWins (赢的小局数, 用户要求"母体阵营赢得对局数")
	//   - 大厂原则: 模式分支显式 (if/else), 不兜底 — 模式 = None 时 Log Error + 用刀战路径
	UWorld* World = GetWorld();
	ARoomGameState* RoomGS = World ? World->GetGameState<ARoomGameState>() : nullptr;

	int32 AttackerDisplayValue = AttackerKills; // 刀战用击杀数
	int32 DefenderDisplayValue = DefenderKills;

	if (RoomGS)
	{
		if (CachedMatchMode == ERoomMatchMode::Zombie)
		{
			// 生化模式: 显示"赢的小局次数" (用户业务规则 2026.08.07)
			AttackerDisplayValue = RoomGS->AttackerWins;
			DefenderDisplayValue = RoomGS->DefenderWins;
			UE_LOG(LogTemp, Log,
				TEXT("[ScoreboardWidget] ShowRoundSettlement[Zombie]: 从 GS 读取赢局数, 母体=%d, 人类=%d"),
				AttackerDisplayValue, DefenderDisplayValue);
		}
		else if (CachedMatchMode == ERoomMatchMode::Melee)
		{
			// 刀战模式: 显示"当局击杀总数"
			AttackerDisplayValue = RoomGS->AttackerTotalKills;
			DefenderDisplayValue = RoomGS->DefenderTotalKills;
			UE_LOG(LogTemp, Log,
				TEXT("[ScoreboardWidget] ShowRoundSettlement[Melee]: 从 GS 读取击杀数, 攻方=%d, 守方=%d"),
				AttackerDisplayValue, DefenderDisplayValue);
		}
		else // None / 异常
		{
			// 【v203.0 零兜底】模式未知 → Log Error + 用刀战路径 (确保 UI 始终显示数字, 不空白)
			//   这是大厂原则 - 不允许静默跳过, 也不允许 UI 空白
			//   默认走刀战 = 假定最常见模式, 至少给玩家看个数字
			UE_LOG(LogTemp, Error,
				TEXT("[ScoreboardWidget] ShowRoundSettlement: CachedMatchMode='None' 异常状态! "
				     "【v203.0 零兜底】默认走刀战路径 (AttackerTotalKills/DefenderTotalKills). "
				     "如需生化模式显示赢局数, 检查 GS->CurrentMatchMode 同步时序."));
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

	// 结算时强制刷新一遍玩家列表的队伍归属 (防止 CurrentFactionTag 同步延迟导致玩家被错分到对面容器)
	RefreshScoreboard();

	// 刷新当局击杀数 / 赢局数显示 (文案按模式分支)
	if (Text_Settlement_AttackerKills)
	{
		const FString Label = (CachedMatchMode == ERoomMatchMode::Zombie)
			? ScoreboardFactionNames::GetAttackerWinsLabel_Zombie(AttackerDisplayValue)
			: ScoreboardFactionNames::GetAttackerKillsLabel_Melee(AttackerDisplayValue);
		Text_Settlement_AttackerKills->SetText(FText::FromString(Label));
		Text_Settlement_AttackerKills->SetVisibility(ESlateVisibility::Visible);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[ScoreboardWidget] ShowRoundSettlement: Text_Settlement_AttackerKills 未绑定 (BindWidget 为 nullptr)! 请检查蓝图中是否正确绑定了该控件."));
	}

	if (Text_Settlement_DefenderKills)
	{
		const FString Label = (CachedMatchMode == ERoomMatchMode::Zombie)
			? ScoreboardFactionNames::GetDefenderWinsLabel_Zombie(DefenderDisplayValue)
			: ScoreboardFactionNames::GetDefenderKillsLabel_Melee(DefenderDisplayValue);
		Text_Settlement_DefenderKills->SetText(FText::FromString(Label));
		Text_Settlement_DefenderKills->SetVisibility(ESlateVisibility::Visible);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[ScoreboardWidget] ShowRoundSettlement: Text_Settlement_DefenderKills 未绑定 (BindWidget 为 nullptr)! 请检查蓝图中是否正确绑定了该控件."));
	}

	// 胜负文字暂时隐藏, 等最终结果广播后再显示
	if (Text_AttackerWinResult)
	{
		Text_AttackerWinResult->SetVisibility(ESlateVisibility::Collapsed);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[ScoreboardWidget] ShowRoundSettlement: Text_AttackerWinResult 未绑定!"));
	}

	if (Text_DefenderWinResult)
	{
		Text_DefenderWinResult->SetVisibility(ESlateVisibility::Collapsed);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[ScoreboardWidget] ShowRoundSettlement: Text_DefenderWinResult 未绑定!"));
	}

	// 强制显示计分板 (结算状态下按 Tab 隐藏后, 再按 Tab 应该能重新显示)
	SetVisibility(ESlateVisibility::Visible);

	UE_LOG(LogTemp, Log,
		TEXT("[ScoreboardWidget] ShowRoundSettlement: Mode=%d, 攻方=%d, 守方=%d"),
		static_cast<int32>(CachedMatchMode), AttackerDisplayValue, DefenderDisplayValue);
}

void UScoreboardWidget::ShowFinalResult(int32 AttackerWins, int32 DefenderWins)
{
	// 【v203.0 大厂架构】确保模式与阵营标题与最新同步
	RefreshCachedMatchMode();
	RefreshTeamTitles();

	// 从 GameState 获取最新胜局数 (避免参数传递链路中的同步问题)
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

	// 【v203.0 大厂架构重构】胜利文案按模式分支
	//   - 刀战: "攻方胜利" / "守方胜利" / "平局"
	//   - 生化: "母体阵营胜利" / "人类阵营胜利" / "平局"
	//   - 颜色逻辑不变: 攻方红 / 守方蓝 / 平局白
	//   - 单一真理源: 阵营名走 namespace ScoreboardFactionNames

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
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[ScoreboardWidget] ShowFinalResult: Text_AttackerWinResult 未绑定!"));
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
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[ScoreboardWidget] ShowFinalResult: Text_DefenderWinResult 未绑定!"));
	}

	UE_LOG(LogTemp, Log,
		TEXT("[ScoreboardWidget] ShowFinalResult: Mode=%d, 攻方/母体胜%d局, 守方/人类胜%d局"),
		static_cast<int32>(CachedMatchMode), AttackerWins, DefenderWins);
}

void UScoreboardWidget::HideSettlementOverlay()
{
	bIsInSettlementState = false;

	// 隐藏所有结算控件
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
}
