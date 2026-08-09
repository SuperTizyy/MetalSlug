// 版权声明：在项目设置的描述页面填写您的版权信息。

// ==========================================
// 头文件包含区
// ==========================================
#include "UI/Game/GameHUDWidget.h"
#include "Systems/RoomPlayerController.h"
#include "Systems/RoomGameState.h"
#include "Systems/RoomGameMode.h"
#include "Data/Enums/CombatEnums.h"
#include "Data/Tables/WeaponTableRow.h"
#include "Data/Config/PlayerConfigAsset.h"
#include "UI/Game/Widgets/PlayerStatusWidget.h"
#include "UI/Game/Widgets/WeaponPanelWidget.h"
#include "UI/Game/Widgets/MatchInfoWidget.h"
#include "UI/Game/Widgets/KillFeedWidget.h"
#include "Characters/BaseCharacter.h"
#include "Components/CharacterEvents.h"
#include "UI/Game/Widgets/ChatWidget.h"
#include "UI/Game/Widgets/KillStreakWidget.h"
#include "UI/Game/Widgets/ScoreboardWidget.h"
#include "UI/Game/Widgets/CrosshairWidget.h"
#include "UI/Game/Widgets/EscMenuWidget.h"
#include "UI/Game/Widgets/RespawnProgressWidget.h"
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/HealthComponent.h"
// 【v134 大厂架构新增】音效查表 (Zombie 模式小局结束音效, 全体客户端播同一音效)
#include "Data/Enums/RoomEnums.h"
#include "Sound/SoundBase.h"
#include "Kismet/GameplayStatics.h"
#include "GameFramework/PlayerState.h"


// ==========================================
// 1. 初始化
// ==========================================

/**
 * UGameHUDWidget::Initialize
 *
 * 基础初始化
 * 注意: 真正的数据表注入和事件订阅在 NativeConstruct 中完成
 */
bool UGameHUDWidget::Initialize()
{
	if (!Super::Initialize())
	{
		return false;
	}

	return true;
}


// ==========================================
// 2. Widget 构造完毕
// ==========================================

/**
 * UGameHUDWidget::NativeConstruct
 *
 * 1. 注入击杀图标数据表
 * 2. 注入连杀图标数据表
 * 3. 绑定 Widget_Chat 的 OnChatMessageReady 事件
 * 4. 初始化结算覆盖板为隐藏
 * 5. 绑定返回大厅按钮
 * 6. 调用 TryBindToGameState
 */
void UGameHUDWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// 初始化击杀图标数据表
	if (Widget_KillFeed && KillIconDataTable)
	{
		Widget_KillFeed->SetKillIconDataTable(KillIconDataTable);
	}

	// 初始化连杀图标数据表
	if (Widget_KillStreak)
	{
		if (KillStreakIconDataTable)
		{
			Widget_KillStreak->SetKillStreakIconDataTable(KillStreakIconDataTable);
			UE_LOG(LogTemp, Log, TEXT("[GameHUDWidget] KillStreakIconDataTable 已设置: %s"), *KillStreakIconDataTable->GetName());
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("[GameHUDWidget] KillStreakIconDataTable 未配置!"));
		}

		// v41 数据驱动: 从 PlayerConfigAsset 读取连杀系统配置
		// 大厂架构: 玩家配置统一在 DA_PlayerConfig 资产中管理
		if (ARoomGameMode* GM = Cast<ARoomGameMode>(GetWorld()->GetAuthGameMode()))
		{
			if (GM->PlayerConfigAsset)
			{
				Widget_KillStreak->SetKillStreakConfig(
					GM->PlayerConfigAsset->KillStreakDuration,
					GM->PlayerConfigAsset->KillStreakIconDisplayDuration
				);
				UE_LOG(LogTemp, Log,
					TEXT("[GameHUDWidget] KillStreakConfig 已注入: Duration=%.1f, IconDisplay=%.1f"),
					GM->PlayerConfigAsset->KillStreakDuration, GM->PlayerConfigAsset->KillStreakIconDisplayDuration);
			}
			else
			{
				// PlayerConfigAsset 未配置, 报错 (零兜底)
				UE_LOG(LogTemp, Error,
					TEXT("[GameHUDWidget] GM->PlayerConfigAsset 未配置!"
						" 请在 GM_RoomGameMode 蓝图中配置 PlayerConfigAsset = DA_PlayerConfig。"));
				Widget_KillStreak->SetKillStreakConfig(10.0f, 3.0f);
			}
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("[GameHUDWidget] 无法获取 RoomGameMode!"));
			Widget_KillStreak->SetKillStreakConfig(10.0f, 3.0f);
		}
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[GameHUDWidget] Widget_KillStreak 为空!"));
	}

	// 绑定聊天消息事件: 当 Widget_Chat 有消息时，发送到服务器
	if (Widget_Chat)
	{
		Widget_Chat->OnChatMessageReady.AddDynamic(this, &UGameHUDWidget::OnChatMessageReadyFromWidget);
	}

	// 【v216 大厂架构回滚】Text_GameOver 仍在 GameHUDWidget 显示 (战斗内瞬时反馈)
	// 大厂原则:
	//   - Border_SettlementOverlay / Button_ReturnToLobby 仍迁移到 UScoreboardWidget (跨地图)
	//   - Text_GameOver 留 GameHUDWidget: 战斗中显示 "人类胜利/幽灵胜利" 3 秒瞬时文本
	//     → 切图后 GameHUDWidget 销毁, Text_GameOver 也不可见, 不需要跨地图
	if (Text_GameOver)
	{
		// 默认隐藏, 战斗内进入结算时 (OnEnterSettlement) 才显示
		Text_GameOver->SetVisibility(ESlateVisibility::Collapsed);
	}

	// 尝试绑定 GameState，成功则立即刷新；否则定时器重试（最多 5 次，每次间隔 0.5 秒）
	UE_LOG(LogTemp, Log, TEXT("[GameHUDWidget] NativeConstruct: Widget_MatchInfo=%s, Widget_PlayerStatus=%s"),
		*GetNameSafe(Widget_MatchInfo), *GetNameSafe(Widget_PlayerStatus));

	TryBindToGameState();

	// 【2026-07-01 新增】订阅 CharacterEvents (依赖倒置核心)
	// 与 TryBindToGameState 并行执行: GameState 控制比赛状态, CharacterEvents 控制角色状态
	// 成功 → 立即刷新武器图标; 失败 → 延迟重试 (最多 10 次)
	TryBindToCharacterEvents();
}


/**
 * UGameHUDWidget::NativeDestruct
 *
 * Widget 从视口移除时调用
 * 【2026-07-01 新增】: 清理 CharacterEvents 订阅, 防止 Widget 销毁后回调残留
 */
void UGameHUDWidget::NativeDestruct()
{
	// 先清理 CharacterEvents 订阅 (避免 Widget 销毁后 CharacterEvents 回调仍被触发)
	UnbindFromCharacterEvents();

	// 清理重试定时器
	if (CharacterEventsRetryTimerHandle.IsValid() && GetWorld())
	{
		GetWorld()->GetTimerManager().ClearTimer(CharacterEventsRetryTimerHandle);
	}

	Super::NativeDestruct();
}


/**
 * UGameHUDWidget::NativeTick
 *
 * 【2026-07-01 P0 新增】架构级兜底
 *
 * 解决 10 次重试仍未订阅的死局:
 *   原架构: TryBindToCharacterEvents 重试 10 次 (共 5 秒) 后放弃 → 永久丢订阅
 *   真实场景: ListenServer 上, ListenServer 玩家的 Pawn 可能在第 11 秒才被 ServerTravel + Possess 进来
 *             此时头像订阅失败 → 玩家永远看不到自己头像
 *
 * 兜底策略:
 *   - 每秒 1 次检查 (节流)
 *   - 已订阅但 Pawn 变了 → Unbind + 重试
 *   - 未订阅 + 有 Pawn → 重置重试计数 + 重新订阅
 *   - 已订阅 + Pawn 还在 → 不做事
 *
 * 性能:
 *   - 每秒 1 次字符串比较 + 引用比较, 开销可忽略
 *   - 不订阅 Tick 不增加任何开销 (GameHUDWidget 在战斗场景才显示)
 */
void UGameHUDWidget::NativeTick(const FGeometry& MyGeometry, float InDeltaTime)
{
	Super::NativeTick(MyGeometry, InDeltaTime);
	TickFallbackCheck(InDeltaTime);

	// 【v40.8 大厂架构 P0】无敌期轮询兜底
	//   根因: OnInvincibilityChanged 事件可能因为以下原因丢失:
	//     - 客户端刚 Spawn 时, bIsInvincible 的"初始值" 同步可能不触发 OnRep
	//     - 客户端收到 true → 立刻收到 false, 中间间隔小于 HUD Bind 时间,事件被错过
	//     - 任何边缘 race 都会让 HUD 永远错过 Show()
	//   修复: HUD NativeTick 每 0.1s 主动从 HealthComponent 拉 GetInvincibilityRemainingSeconds()
	//        与 Widget_RespawnProgress->bIsShowing 状态对比, 缺则补 Show/ Hide
	//   镜像对称 (v40.8): HUD 同时订阅事件 + 主动拉数据, 兜底"事件丢失"
	//   零重复: 不影响 OnInvincibilityChanged 事件流, 仅作兜底
	TickInvincibilityWatchdog(InDeltaTime);
}


/**
 * UGameHUDWidget::TickFallbackCheck
 *
 * 每秒 1 次 (节流), 检查 CharacterEvents 订阅状态
 */
void UGameHUDWidget::TickFallbackCheck(float DeltaTime)
{
	CharacterEventsFallbackTimer += DeltaTime;
	if (CharacterEventsFallbackTimer < 1.0f)
	{
		return;
	}
	CharacterEventsFallbackTimer = 0.0f;

	APlayerController* PC = GetOwningPlayer();
	if (!PC)
	{
		return;
	}

	ABaseCharacter* Character = Cast<ABaseCharacter>(PC->GetPawn());

	// 场景 1: 已订阅, 但 Pawn 变了 (或 CharacterEvents 失效)
	if (CachedCharacterEvents)
	{
		if (!IsValid(CachedCharacterEvents) || CachedCharacterEvents->GetOwner() != Character)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[GameHUDWidget][TickFallback] 已订阅但 CharacterEvents 失效或 Pawn 变了, 重新订阅"));

			UnbindFromCharacterEvents();
			CharacterEventsRetryCount = 0;
			TryBindToCharacterEvents();
		}
		return;
	}

	// 场景 2: 未订阅, 但 Pawn 已就绪 → 重置重试计数并重新尝试
	if (Character && Character->ResolveCharacterEvents())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[GameHUDWidget][TickFallback] 未订阅但 Pawn 已就绪, 重置重试计数并重新订阅 (Pawn=%s)"),
			*Character->GetName());

		CharacterEventsRetryCount = 0;
		TryBindToCharacterEvents();
	}
}


/**
 * UGameHUDWidget::TickInvincibilityWatchdog
 *
 * 【v40.8 P0 大厂架构】无敌期轮询兜底 (HUD 不依赖单一事件流)
 *
 * 设计动机:
 *   UE 网络复制 bIsInvincible 字段的 OnRep 行为有以下边缘 case:
 *     1. 客户端刚 Spawn 时, bIsInvincible 初始值可能不触发 OnRep (业界共识)
 *     2. 客户端收到 true → 立即收到 false (间隔 < 客户端 Bind 时间), 事件错过
 *     3. 任何 race condition 都可能让 HUD 永远错过 Show()
 *
 *   这导致玩家复活时, 复活进度条/无敌闪烁等 HUD 效果不显示 (用户报告)
 *
 * 大厂原则 (事件 + 拉取 双轨制):
 *   - 事件流: OnInvincibilityChanged (HandleInvincibilityChanged) 实时触发
 *   - 拉取流: 本函数每 0.1s 主动从 HealthComponent 拉 GetInvincibilityRemainingSeconds()
 *   - 两者互补, 拉取流兜底事件流丢失
 *
 * 性能:
 *   - 0.1s 间隔 = 10Hz, 完全不影响性能 (单字段 Get 几乎 0 开销)
 *   - 只在 GetInvincibilityRemainingSeconds() > 0 时调用 Show(), 否则 Hide()
 *   - RespawnProgressWidget->bIsShowing 状态做去重, 同状态重复调不浪费
 *
 * 零兜底原则:
 *   - 不用 bool 字段 (IsInvincible) 判断, 用绝对时间字段 (InvincibilityExpiresAtWorldTime)
 *     派生剩余秒数, 避免 OnRep 初始值不触发的 UE 固有限制
 *   - 不写死默认 Duration, 一切从 HealthComponent::GetInvincibilityDuration() 派生
 */
void UGameHUDWidget::TickInvincibilityWatchdog(float DeltaTime)
{
	// 0.1s 节流 (10Hz) — 完全不影响性能
	InvincibilityWatchdogTimer += DeltaTime;
	if (InvincibilityWatchdogTimer < 0.1f)
	{
		return;
	}
	InvincibilityWatchdogTimer = 0.0f;

	// 必须有 RespawnProgress Widget
	if (!Widget_RespawnProgress)
	{
		return;
	}

	APlayerController* PC = GetOwningPlayer();
	if (!PC)
	{
		return;
	}

	ABaseCharacter* Character = Cast<ABaseCharacter>(PC->GetPawn());
	if (!Character)
	{
		return;
	}

	UHealthComponent* HC = Character->ResolveHealthComponent();
	if (!HC)
	{
		return;
	}

	// 真理源: 派生剩余秒数 (不依赖 bool 字段,避免 OnRep 初始值不触发问题)
	const float Remaining = HC->GetInvincibilityRemainingSeconds();

	if (Remaining > 0.0f)
	{
		// 应该显示 (但可能事件错过了 → Show() 内部用 bIsShowing 去重, 重复调无副作用)
		if (!Widget_RespawnProgress->IsShowingProgress())
		{
			UE_LOG(LogTemp, Verbose,
				TEXT("[GameHUDWidget][Watchdog][v40.8] 检测到仍在无敌期, 但 Widget 未显示 → 强制 Show. "
					 "Pawn=%s, Remaining=%.2fs"),
				*Character->GetName(), Remaining);
			Widget_RespawnProgress->Show();
		}
	}
	else
	{
		// 不在无敌期, 应隐藏 (但事件可能错过)
		if (Widget_RespawnProgress->IsShowingProgress())
		{
			UE_LOG(LogTemp, Verbose,
				TEXT("[GameHUDWidget][Watchdog][v40.8] 无敌期已结束, 但 Widget 仍显示 → 强制 Hide. "
					 "Pawn=%s"),
				*Character->GetName());
			Widget_RespawnProgress->Hide();
		}
	}
}


// ==========================================
// 3. 聊天消息转发
// ==========================================

/**
 * UGameHUDWidget::OnChatMessageReadyFromWidget
 *
 * 接收 Widget_Chat 的消息并通过 PC->Server_SendChatMessage 转发到服务器
 */
void UGameHUDWidget::OnChatMessageReadyFromWidget(const FString& PlayerName, const FString& Message)
{
	if (APlayerController* PC = GetOwningPlayer())
	{
		if (ARoomPlayerController* RoomPC = Cast<ARoomPlayerController>(PC))
		{
			RoomPC->Server_SendChatMessage(Message);
		}
	}
}


// ==========================================
// 4. GameState 绑定重试
// ==========================================

/**
 * UGameHUDWidget::TryBindToGameState
 *
 * 尝试绑定到 GameState
 * 1. 模式切换 -> OnMatchModeChangedForHUD
 * 2. 总局数同步 -> UpdateTotalRoundsText (【v92】替换 UpdateRemainingRoundsText, 单一真理源)
 * 3. 队伍击杀数 -> UpdateTeamKillCountsText
 * 4. 进入结算 -> OnEnterSettlement
 * 5. 显示最终胜负 -> OnShowFinalSettlement
 * 6. 母体变异倒计时 -> OnMotherMutationChanged
 * 7. 空投降临倒计时 -> OnAirdropCountdownChanged
 * 8. 立即同步一次
 * 失败: 0.5 秒重试，最多 5 次
 */
void UGameHUDWidget::TryBindToGameState()
{
	if (UWorld* World = GetWorld())
	{
		if (ARoomGameState* RoomGS = World->GetGameState<ARoomGameState>())
		{
			// 绑定事件: 当 GameState 的模式切换时，UI 决定各控件的显示/隐藏
			RoomGS->OnMatchModeChanged.AddDynamic(this, &UGameHUDWidget::OnMatchModeChangedForHUD);

			// 【v92 大厂架构重构】绑定事件: GameState.TotalRounds 同步时刷新 Text_RemainingRounds
			//   - 替换旧的 OnCurrentRoundUpdated 订阅 (CurrentRound 是内部计数, UI 不订阅)
			//   - 大厂原则: UI 订阅真理源 (TotalRounds), 不订阅内部计数 (CurrentRound)
			RoomGS->OnTotalRoundsUpdated.AddDynamic(this, &UGameHUDWidget::UpdateTotalRoundsText);

			// 绑定事件: 当 GameState 的队伍击杀统计变化时，刷新 MatchInfoWidget 上的 Text_AttackerCount / Text_DefenderCount
			RoomGS->OnTeamKillCountUpdated.AddDynamic(this, &UGameHUDWidget::UpdateTeamKillCountsText);

			// 【v134 大厂架构新增】绑定事件: 小局胜局数变化时, 刷新 Text_AttackerCount / Text_DefenderCount 显示
			//   - 单一真理源: RoomGS->OnWinStatsUpdated (GameState.AddRoundWinToFaction 触发)
			//   - 与 OnTeamKillCountUpdated 镜像, 但语义不同: 击杀数 vs 胜局数
			//   - 大厂原则 — 零重复架构: 复用 UpdateTeamKillCountsText (因为它就是把 int32 写到 TextBlock)
			//   - 不破坏刀战模式: 刀战使用旧 MulticastRefreshKillCount 链路, 该绑定无影响
			//   - 模式分支由 MatchInfoWidget::OnWinStatsChanged 内部按 CurrentMatchMode 决定显示
			RoomGS->OnWinStatsUpdated.AddDynamic(this, &UGameHUDWidget::UpdateTeamWinCountsText);

			// 【v134 大厂架构新增】绑定事件: 小局音效 RPC 收到时, 查 GameMode 音效 + 播放
			//   - 单一真理源: RoomGS->OnZombieRoundSoundReceived (MulticastPlayZombieRoundSound 触发)
			// 【v210.4 大厂架构重构】音效播放已上移到 RPC Implementation, 这里只做 UI 通知
			//   - 旧 (v134 - v210.3): 客户端本机查 GameMode 缓存的 USoundBase + PlaySound2D
			//   - 新 (v210.4): GS::PlayZombieRoundSoundFromPath_Implementation 已 PlaySound2D
			//   - HUD 端 OnZombieRoundSoundReceived 仅留扩展点 (震屏 / 全屏闪烁等)
			//   - 业务规则 (用户 2026.08.06 明确): 全体客户端播同一音效, 不按 ClientFactionTag 分发
			//     - 人类赢 → 全体播 ZombieHumanWinSound (RPC 已发 FSoftObjectPath)
			//     - 母体赢 → 全体播 ZombieMotherWinSound (RPC 已发 FSoftObjectPath)
			RoomGS->OnZombieRoundSoundReceived.AddDynamic(this, &UGameHUDWidget::OnZombieRoundSoundReceived);

			// 绑定事件: 进入结算状态（倒计时归零时触发）
			RoomGS->OnEnterSettlement.AddDynamic(this, &UGameHUDWidget::OnEnterSettlement);

			// 【v201 大厂架构新增】绑定事件: 短暂显示小局结果（不进入结算页面）
			RoomGS->OnZombieRoundBriefResult.AddDynamic(this, &UGameHUDWidget::OnShowZombieRoundBriefResult);

			// 【v218 大厂架构新增】绑定事件: 刀战本局赢家更新 → 显示 Text_GameOver
			//   - 镜像 OnZombieRoundBriefResult 订阅位置 (紧邻, 业务对称)
			//   - 触发路径: FinishMeleeMatch → SetMeleeWinner → OnRep_MeleeWinner → 本回调
			//   - 刀战模式唯一显示 Text_GameOver 的入口 (业务分离)
			//   - 生化模式永远不触发 (MeleeWinner=Default None)
			RoomGS->OnMeleeWinnerUpdated.AddDynamic(this, &UGameHUDWidget::OnMeleeWinnerUpdated);

			// 绑定事件: 显示最终胜负（延迟 3 秒后触发）
			RoomGS->OnShowFinalSettlement.AddDynamic(this, &UGameHUDWidget::OnShowFinalSettlement);

			// 【v92 大厂架构新增】绑定事件: 母体变异倒计时同步 (生化模式每局开局 8s 倒计时)
			// 单一真理源: GameState.OnRep_MotherMutationState 触发该委托
			RoomGS->OnMotherMutationChanged.AddDynamic(this, &UGameHUDWidget::OnMotherMutationChanged);

			// 【生化模式】空投降临倒计时同步 — 镜像母体变异倒计时的绑定路径
			//   - 单一真理源: GameState.OnRep_AirdropCountdownState 触发该委托
			//   - HUD 端只做事件路由, 不持有倒计时逻辑
			RoomGS->OnAirdropCountdownChanged.AddDynamic(this, &UGameHUDWidget::OnAirdropCountdownChanged);

			// 【v92 大厂架构重构】初始化时先刷一次 (总局数, 直接读 GameState.TotalRounds)
			UpdateTotalRoundsText(RoomGS->TotalRounds);

			// 初始化时同步当前已存在的队伍击杀数据
			UpdateTeamKillCountsText(RoomGS->AttackerTotalKills, RoomGS->DefenderTotalKills);

			// 【v92 大厂架构新增】初始化时同步当前母体变异倒计时状态
			// 大厂原则 — 镜像 Bind-Snapshot 补发: 防止 HUD 订阅晚于服务器 Broadcast 事件
			// 例如: 服务器已开始倒计时 → HUD 创建 → 直接读 GameState 当前字段 → 立即显示
			OnMotherMutationChanged(RoomGS->MotherMutationStartTime, RoomGS->MotherMutationDuration);

			// 【生化模式】空投倒计时同步: 镜像 Bind-Snapshot 补发, 防止 HUD 订阅晚于服务器 Broadcast 事件
			OnAirdropCountdownChanged(RoomGS->AirdropCountdownStartTime, RoomGS->AirdropCountdownDuration);

			UE_LOG(LogTemp, Log, TEXT("[GameHUDWidget] 成功绑定 GameState，AttackerKills=%d, DefenderKills=%d"),
				RoomGS->AttackerTotalKills, RoomGS->DefenderTotalKills);

			// 绑定成功，不再重试
			return;
		}
	}

	// GameState 还未生成，定时器重试（最多 5 次，每次间隔 0.5 秒）
	static int32 RetryCount = 0;
	if (RetryCount < 5)
	{
		RetryCount++;
		FTimerHandle DummyHandle;
		GetWorld()->GetTimerManager().SetTimer(DummyHandle, this, &UGameHUDWidget::TryBindToGameState, 0.5f, false);
	}
	else
	{
		RetryCount = 0;
		UE_LOG(LogTemp, Warning, TEXT("[GameHUDWidget] Failed to bind to GameState after 5 retries"));
	}
}


// ==========================================
// 5. 玩家状态接口（转发到 Widget_PlayerStatus）
// ==========================================

/** 刷新玩家血量 */
void UGameHUDWidget::UpdateHealth(float Current, float Max)
{
	if (Widget_PlayerStatus)
	{
		Widget_PlayerStatus->UpdateHealth(Current, Max);
	}
}

/** 刷新玩家能量 */
void UGameHUDWidget::UpdateEnergy(float Current, float Max)
{
	if (Widget_PlayerStatus)
	{
		Widget_PlayerStatus->UpdateEnergy(Current, Max);
	}
}

/** 刷新玩家血量文本 */
void UGameHUDWidget::UpdateHealthText(int32 Current, int32 Max)
{
	if (Widget_PlayerStatus)
	{
		Widget_PlayerStatus->UpdateHealthText(Current, Max);
	}
}

/** 刷新玩家能量文本 */
void UGameHUDWidget::UpdateEnergyText(int32 Current, int32 Max)
{
	if (Widget_PlayerStatus)
	{
		Widget_PlayerStatus->UpdateEnergyText(Current, Max);
	}
}

/** 玩家击杀事件 - 转发到 Widget_KillStreak */
void UGameHUDWidget::OnPlayerKill(bool bIsHeadshot)
{
	if (Widget_KillStreak)
	{
		UE_LOG(LogTemp, Log, TEXT("[GameHUDWidget] OnPlayerKill: 调用 RecordKill"));
		Widget_KillStreak->RecordKill(bIsHeadshot);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[GameHUDWidget] OnPlayerKill: Widget_KillStreak 为空!"));
	}

	UE_LOG(LogTemp, Log, TEXT("[GameHUDWidget] OnPlayerKill: bIsHeadshot=%d"), bIsHeadshot);
}

/**
 * 【v92 大厂架构重构】更新总局数文本 (替换 UpdateRemainingRoundsText)
 *
 * 大厂原则 — 单一真理源:
 *   - 数据源: GameState.TotalRounds (Replicated)
 *   - 接收 GameState.OnTotalRoundsUpdated 回调 (替换旧的 OnCurrentRoundUpdated 订阅)
 *   - 静态显示 "总局数：xx", 不倒数
 */
void UGameHUDWidget::UpdateTotalRoundsText(int32 TotalRounds)
{
	if (Widget_MatchInfo)
	{
		Widget_MatchInfo->UpdateTotalRounds(TotalRounds);
	}
	else
	{
		UE_LOG(LogTemp, Error,
			TEXT("[GameHUDWidget] UpdateTotalRoundsText: Widget_MatchInfo 为空, 无法刷新总局数!"));
	}
}

/** 更新队伍击杀统计文本 */
void UGameHUDWidget::UpdateTeamKillCountsText(int32 AttackerKills, int32 DefenderKills)
{
	if (!Widget_MatchInfo)
	{
		UE_LOG(LogTemp, Error, TEXT("[GameHUDWidget] UpdateTeamKillCountsText: Widget_MatchInfo 为空，无法刷新队伍击杀数!"));
		return;
	}

	Widget_MatchInfo->UpdateAttackerCount(AttackerKills);
	Widget_MatchInfo->UpdateDefenderCount(DefenderKills);

	// 【v202.0 大厂架构新增】队伍击杀统计变化时, 同时触发 ScoreboardWidget 刷新
	// 大厂原则 — 单一事件源: OnTeamKillCountUpdated 是队伍击杀的真理源广播
	//   - MatchInfo 收: 数字显示
	//   - Scoreboard 收: 排名/KDA 重排
	// 注意: 重复架构 vs Tick 拉取的取舍 — 用户明确要求走 RPC, 走事件最直接
	if (Widget_Scoreboard && Widget_Scoreboard->GetVisibility() == ESlateVisibility::Visible)
	{
		Widget_Scoreboard->RefreshScoreboard();
	}

	UE_LOG(LogTemp, Log, TEXT("[GameHUDWidget] 刷新队伍击杀数: 攻方=%d, 守方=%d"), AttackerKills, DefenderKills);
}


/**
 * UGameHUDWidget::UpdateTeamWinCountsText (v134 大厂架构新增)
 *
 * 业务规则 (用户 2026.08.06 明确):
 *   - 生化模式每小局结束时, Text_AttackerCount / Text_DefenderCount 显示 累加胜局数
 *   - 数据源: ARoomGameState::AttackerWins / DefenderWins (通过 OnWinStatsUpdated 推送)
 *
 * 大厂原则 — 镜像 UpdateTeamKillCountsText:
 *   - 参数 (AttackerWins, DefenderWins) 由 GameState.OnWinStatsUpdated 推送
 *   - 转发到 MatchInfoWidget 决定显示
 *   - 语义不同但调用路径一致 (零重复架构)
 *
 * 大厂原则 — 零兜底:
 *   - Widget_MatchInfo 为空 → Log Error + return (防御层)
 */
void UGameHUDWidget::UpdateTeamWinCountsText(int32 AttackerWins, int32 DefenderWins)
{
	if (!Widget_MatchInfo)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[GameHUDWidget] UpdateTeamWinCountsText: Widget_MatchInfo 为空, 无法刷新小局胜局数! "
			     "【修复】检查 BP 资产 WBP_GameHUD 是否有 Widget_MatchInfo (Type=UMatchInfoWidget)."));
		return;
	}

	Widget_MatchInfo->UpdateAttackerCount(AttackerWins);
	Widget_MatchInfo->UpdateDefenderCount(DefenderWins);

	UE_LOG(LogTemp, Log,
		TEXT("[GameHUDWidget] 刷新小局胜局数 (生化模式): AttackerWins=%d, DefenderWins=%d"),
		AttackerWins, DefenderWins);
}


/**
 * UGameHUDWidget::OnZombieRoundSoundReceived (Multicast RPC 回调, 仅做 UI 通知)
 *
 * 【v210.4 大厂架构重构 — RPC 内置播放, UI 端只做通知】
 *
 * 业务规则 (用户 2026.08.06 明确):
 *   - 每小局结束, 服务器 Multicast 推 RoundWinner + SoundPath 到所有客户端
 *   - 音效播放已在 RPC Implementation 内 (PlayZombieRoundSoundFromPath_Implementation) 完成
 *   - 本回调只做 UI 反馈 (震屏 / 全屏闪烁等可扩展点)
 *
 * 大厂原则 — 单一职责:
 *   - 本函数不再查 GameMode / GameState 音效 (音效已在 RPC 链路播放)
 *   - 仅 Broadcast 通知 HUD 其他组件 (后续可扩展视觉反馈)
 *
 * 大厂原则 — 零兜底:
 *   - World 为空 → Log Error + return
 *
 * 不破坏刀战模式:
 *   - 刀战永不调 MulticastPlayZombieRoundSound, 本回调永不触发
 */
void UGameHUDWidget::OnZombieRoundSoundReceived(EZombieRoundWinner InRoundWinner)
{
	UE_LOG(LogTemp, Log,
		TEXT("[GameHUDWidget] 【v210.4】OnZombieRoundSoundReceived: 收到小局音效 RPC (音效已在 RPC 链路播放). InRoundWinner=%d"),
		static_cast<int32>(InRoundWinner));

	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[GameHUDWidget] 【v210.4】OnZombieRoundSoundReceived: World 为空, 跳过 UI 通知."));
		return;
	}

	// 【v210.4】这里可以扩展 UI 反馈 (震屏 / 全屏闪烁), 当前只 Log 留扩展点
	//   - 音效本身在 GS::PlayZombieRoundSoundFromPath_Implementation 里已 PlaySound2D
	//   - 不在这里重复播放, 避免双声道叠加
}

/**
 * 【v218 大厂架构新增】OnMeleeWinnerUpdated — 刀战本局赢家更新回调
 *
 * 触发链路:
 *   - Text_RoundCountdown 倒计时归零 → URoomLifecycleSubsystem::HandleMatchTimeOut
 *   - → URoomLifecycleSubsystem::FinishMeleeMatch (新)
 *   - → ARoomGameState::SetMeleeWinner(NewWinner) → 写 Replicated 字段 + 服务器手动 Broadcast
 *   - → ARoomGameState::MulticastEnterSettlement (复用 RPC, 推击杀数)
 *   - → 客户端 OnRep_MeleeWinner 触发 Broadcast OnMeleeWinnerUpdated
 *   - → 本回调触发 (UI 层订阅 OnMeleeWinnerUpdated)
 *
 * 业务职责:
 *   - 显示 Text_GameOver: "攻方胜利" / "守方胜利" / "平局"
 *   - 镜像 OnEnterSettlement 的 Text_GameOver 处理, 但语义更清晰 (用 EMeleeWinner)
 *   - 3 秒后 ScheduleFinalSettlement 切图到 L_Login 结算页 (走 ScheduleFinalSettlement 路径)
 *
 * 大厂原则 — 单一真理源 + 镜像 OnZombieRoundSoundReceived:
 *   - 本回调只负责业务 (Text_GameOver 显示), 不读 GameState (数据已通过参数传入)
 *   - UI 显示委托 Text_GameOver (BindWidgetOptional, 旧 v200.2 已存在)
 *   - 不动 MulticastEnterSettlement 链路, 避免重复架构
 *
 * 不破坏生化模式:
 *   - 生化模式 MeleeWinner 永远 None, 本回调永不触发
 *   - 生化胜负文本走 OnEnterSettlement / OnShowZombieRoundBriefResult (业务分离)
 *
 * 不破坏旧 v200.2 刀战行为:
 *   - 旧版 Text_GameOver 在 OnEnterSettlement 里 Collapsed (Melee 模式)
 *   - 新版在 OnMeleeWinnerUpdated 里显示 (Melee 模式专属) — OnEnterSettlement 不再覆盖
 */
void UGameHUDWidget::OnMeleeWinnerUpdated(EMeleeWinner NewWinner)
{
	UE_LOG(LogTemp, Log,
		TEXT("[GameHUDWidget] 【v218】OnMeleeWinnerUpdated: 刀战本局赢家同步! MeleeWinner=%d"),
		static_cast<int32>(NewWinner));

	// 0 兜底 — None 拒绝显示 (防止 UI 显示空文本)
	if (NewWinner == EMeleeWinner::None)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[GameHUDWidget] OnMeleeWinnerUpdated: NewWinner=None 拒绝显示. "
			     "【修复】调用方必须传 Attacker/Defender/Draw, 不允许传 None."));
		return;
	}

	if (!Text_GameOver)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[GameHUDWidget] OnMeleeWinnerUpdated: Text_GameOver 未绑定, 无法显示刀战胜负. "
			     "【修复】检查 WBP_GameHUDWidget 是否包含同名 Text_GameOver 控件."));
		return;
	}

	// 大厂原则 — 单一映射表: 不在 UI 层做 if/else, 用 switch 显式映射所有枚举值
	FString WinnerText;
	switch (NewWinner)
	{
	case EMeleeWinner::Attacker:
		WinnerText = TEXT("攻方胜利");
		break;
	case EMeleeWinner::Defender:
		WinnerText = TEXT("守方胜利");
		break;
	case EMeleeWinner::Draw:
		WinnerText = TEXT("平局");
		break;
	default:
		// 0 兜底 — 这里实际走不到 (上面 None 已 return), 但保留作为防御
		UE_LOG(LogTemp, Error,
			TEXT("[GameHUDWidget] OnMeleeWinnerUpdated: 未识别的 MeleeWinner=%d. "
			     "【修复】检查 RoomEnums.h::EMeleeWinner 枚举, 是否新增了值但本函数未更新."),
			static_cast<int32>(NewWinner));
		return;
	}

	Text_GameOver->SetText(FText::FromString(WinnerText));
	Text_GameOver->SetVisibility(ESlateVisibility::HitTestInvisible);

	// 3 秒后 ScheduleFinalSettlement 触发 BroadcastFinalSettlement 切图, Text_GameOver 随 GameHUDWidget 销毁而消失
	// 不需要手动 SetTimer 隐藏, 镜像 OnShowZombieRoundBriefResult 的 SetTimer (这里不需要, 因为 3s 后切图)
}

/** 根据游戏模式切换 Text_RemainingRounds 的可见性 */
void UGameHUDWidget::OnMatchModeChangedForHUD(ERoomMatchMode NewMode)
{
	// 【v200.2 大厂架构新增】缓存当前模式，供 OnEnterSettlement 判断显示文本
	CachedMatchMode = NewMode;

	if (Widget_MatchInfo)
	{
		Widget_MatchInfo->SetVisibilityByMode(NewMode);
	}
}


/**
 * 【v92 大厂架构新增】母体变异倒计时同步回调 (转发壳)
 *
 * 单一真理源 — 由 GameState.OnRep_MotherMutationState 触发 OnMotherMutationChanged
 * 职责: 把事件转发给 Widget_MatchInfo, 由 Widget 内部决定显示/隐藏 + NativeTick 刷新数字
 *
 * 大厂原则 — 镜像 OnMatchModeChangedForHUD:
 *   - GameHUDWidget 不持有倒计时逻辑, 只做事件路由
 *   - Widget_MatchInfo 是唯一显示组件
 *
 * 大厂原则 — 零兜底:
 *   - Widget_MatchInfo 为空 → Log Error + return (强制修复 BP)
 */
void UGameHUDWidget::OnMotherMutationChanged(float StartTime, float Duration)
{
	if (!Widget_MatchInfo)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[GameHUDWidget] OnMotherMutationChanged: Widget_MatchInfo 为空, 无法转发母体变异倒计时!"
			     " 【修复】检查 WBP_GameHUDWidget 是否绑定了 Widget_MatchInfo (UMatchInfoWidget)."));
		return;
	}

	Widget_MatchInfo->UpdateMotherMutationCountdown(StartTime, Duration);

	UE_LOG(LogTemp, Log,
		TEXT("[GameHUDWidget] OnMotherMutationChanged: 转发母体变异倒计时到 MatchInfoWidget. StartTime=%.2f, Duration=%.2f"),
		StartTime, Duration);
}


/**
 * 【生化模式】空投降临倒计时同步回调 (转发壳)
 *
 * 单一真理源 — 由 GameState.OnRep_AirdropCountdownState 触发 OnAirdropCountdownChanged
 * 职责: 把事件转发给 Widget_MatchInfo, 由 Widget 内部决定显示/隐藏 + NativeTick 刷新数字
 *
 * 大厂原则 — 镜像 OnMotherMutationChanged:
 *   - GameHUDWidget 不持有倒计时逻辑, 只做事件路由
 *   - Widget_MatchInfo 是唯一显示组件, 同时也是"母体变异未结束则继续隐藏"的业务闸
 */
void UGameHUDWidget::OnAirdropCountdownChanged(float StartTime, float Duration)
{
	if (!Widget_MatchInfo)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[GameHUDWidget] OnAirdropCountdownChanged: Widget_MatchInfo 为空, 无法转发空投倒计时!"
			     " 【修复】检查 WBP_GameHUDWidget 是否绑定了 Widget_MatchInfo (UMatchInfoWidget)."));
		return;
	}

	Widget_MatchInfo->UpdateAirdropCountdown(StartTime, Duration);

	UE_LOG(LogTemp, Log,
		TEXT("[GameHUDWidget] OnAirdropCountdownChanged: 转发空投倒计时到 MatchInfoWidget. StartTime=%.2f, Duration=%.2f"),
		StartTime, Duration);
}

/** 更新 AC 值 */
void UGameHUDWidget::UpdateACValue(int32 Value)
{
	if (Widget_PlayerStatus)
	{
		Widget_PlayerStatus->UpdateACValue(Value);
	}
}

/** 更新 ACE 值 */
void UGameHUDWidget::UpdateACEValue(int32 Value)
{
	if (Widget_PlayerStatus)
	{
		Widget_PlayerStatus->UpdateACEValue(Value);
	}
}

/** 更新 ACE 值并根据排名设置文字颜色 */
void UGameHUDWidget::UpdateACEWithRank(int32 Value, EACERankType RankType)
{
	if (Widget_PlayerStatus)
	{
		Widget_PlayerStatus->SetACEValueWithRank(Value, RankType);
	}
}

/** 更新角色图标 */
void UGameHUDWidget::UpdateCharacterIcon(UTexture2D* Icon)
{
	if (Widget_PlayerStatus)
	{
		Widget_PlayerStatus->UpdateCharacterIcon(Icon);
	}
}


// ==========================================
// 6. 聊天消息（转发到 Widget_Chat）
// ==========================================

/** 接收服务器广播的聊天消息 */
void UGameHUDWidget::AddChatMessage(const FString& PlayerName, bool bIsHost, const FString& Message)
{
	if (Widget_Chat)
	{
		Widget_Chat->AddChatMessage(PlayerName, Message);
	}
}

/** 接收服务器广播的系统消息 */
void UGameHUDWidget::AddSystemMessage(const FString& Message)
{
	if (Widget_Chat)
	{
		Widget_Chat->AddSystemMessage(Message);
	}
}

/** 接收击杀消息 - 转发到 Widget_KillFeed */
void UGameHUDWidget::AddKillFeedMessage(const FString& KillerName, const FString& VictimName, EKillMethod KillMethod)
{
	if (Widget_KillFeed)
	{
		Widget_KillFeed->AddKillInfo(KillerName, VictimName, KillMethod);
	}
}

/** 激活聊天输入框 */
void UGameHUDWidget::ActivateChatInput()
{
	// 通过已绑定的 Widget_Chat 成员变量激活聊天输入框
	if (Widget_Chat && !Widget_Chat->IsInputFocused())
	{
		Widget_Chat->SetInputFocused(true);
	}
}


// ==========================================
// 7. 计分板 / 准星 / ESC 菜单
// ==========================================

/** 显示计分板 */
void UGameHUDWidget::ShowScoreboard()
{
	UE_LOG(LogTemp, Display,
		TEXT("[GameHUDWidget] 【v221.2】ShowScoreboard: Widget_Scoreboard=%s, 即将刷新 (Tab 按键/Hold 状态)."),
		*GetNameSafe(Widget_Scoreboard));
	if (Widget_Scoreboard)
	{
		Widget_Scoreboard->SetVisibility(ESlateVisibility::Visible);
		Widget_Scoreboard->RefreshScoreboard();
	}
}

/** 隐藏计分板 */
void UGameHUDWidget::HideScoreboard()
{
	UE_LOG(LogTemp, Display,
		TEXT("[GameHUDWidget] 【v221.2】HideScoreboard: Widget_Scoreboard=%s, Tab 按键释放."),
		*GetNameSafe(Widget_Scoreboard));
	if (Widget_Scoreboard)
	{
		Widget_Scoreboard->SetVisibility(ESlateVisibility::Hidden);
	}
}

/** 显示准星 */
void UGameHUDWidget::ShowCrosshair()
{
	if (Widget_Crosshair)
	{
		Widget_Crosshair->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
}

/** 隐藏准星 */
void UGameHUDWidget::HideCrosshair()
{
	if (Widget_Crosshair)
	{
		Widget_Crosshair->SetVisibility(ESlateVisibility::Collapsed);
	}
}

/** 显示 ESC 菜单 */
void UGameHUDWidget::ShowEscMenu()
{
	// 显示 ESC 菜单
	if (Widget_EscMenu)
	{
		UE_LOG(LogTemp, Log, TEXT("[GameHUD] ShowEscMenu 设置 Widget_EscMenu 为 Visible"));
		Widget_EscMenu->SetVisibility(ESlateVisibility::Visible);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[GameHUD] ShowEscMenu Widget_EscMenu 为空!"));
	}
}

/** 隐藏 ESC 菜单 */
void UGameHUDWidget::HideEscMenu()
{
	// 隐藏 ESC 菜单
	if (Widget_EscMenu)
	{
		UE_LOG(LogTemp, Log, TEXT("[GameHUD] HideEscMenu 设置 Widget_EscMenu 为 Hidden"));
		Widget_EscMenu->SetVisibility(ESlateVisibility::Hidden);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[GameHUD] HideEscMenu Widget_EscMenu 为空!"));
	}
}


// ==========================================
// v60.11 大厂架构 — Crosshair 世界射线服务
// ==========================================

/**
 * UGameHUDWidget::GetCrosshairWorldRay
 *
 * 大厂原则 — UI 层职责对等:
 *   - "准星 → 世界射线" 是 UI 层的专业职责
 *   - Strategy / Character 层只读结果, 不关心转换算法
 *   - 这是"武器射线检测"的**单一真理源**入口
 *
 * 流程 (严格按顺序, 任一失败立即返回 false):
 *   1. PlayerController 存在 (本地玩家专属)
 *   2. ViewportSize 获取成功 (>= 1x1)
 *   3. DeprojectScreenPositionToWorld 成功 (返回 true)
 *
 * 【v82+ 重大重构】不再依赖 Widget_Crosshair 的几何位置
 *   旧版 (v60-v81) 反模式: Widget_Crosshair->GetCenterScreenPosition() 拿 widget 几何中心
 *   - 根因: widget 的 Alignment / Padding 决定位置, 玩家放置不准确 → 射线方向 Z 偏 (-0.3 朝地)
 *   - 真实 Bug: 用户报告 "射线总是朝地打" — widget 中心坐标 ≠ 屏幕中心
 *   - 新版 (v82+): 用 ViewportSize / 2.0 = 真正的屏幕中心 (玩家设计的"准星")
 *   - Widget_Crosshair 只是装饰, 不参与几何计算 (单一真理源 = 屏幕中心)
 *
 * @note 调用方必须显式校验返回值, false 时拒绝射线检测 (零兜底)
 */
bool UGameHUDWidget::GetCrosshairWorldRay(FVector& OutWorldOrigin, FVector& OutWorldDirection) const
{
	OutWorldOrigin = FVector::ZeroVector;
	OutWorldDirection = FVector::ZeroVector;

	// (1) PlayerController — 准星屏幕坐标 → 世界射线需要 PC
	APlayerController* PC = GetOwningPlayer();
	if (!PC)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[GameHUDWidget::GetCrosshairWorldRay] PlayerController 为空 (HUD 不属于本地玩家). "
			     "【v82+ 零兜底】本函数仅本地玩家武器使用, AI/远端客户端禁止调用."));
		return false;
	}

	// (2) ViewportSize — 用屏幕中心 (ViewportSize / 2) 作为准星屏幕坐标
	//   旧版 (v60-v81): 用 Widget_Crosshair->GetCenterScreenPosition()
	//   - widget 几何位置 ≠ 玩家视觉准星中心 → Z 分量偏移 (-0.3 朝地)
	//   - 大厂原则: 真理源 = 玩家屏幕中心, 不是 widget 几何中心
	int32 ViewportSizeX = 0;
	int32 ViewportSizeY = 0;
	PC->GetViewportSize(ViewportSizeX, ViewportSizeY);
	if (ViewportSizeX <= 0 || ViewportSizeY <= 0)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[GameHUDWidget::GetCrosshairWorldRay] ViewportSize 无效. SizeX=%d SizeY=%d. "
			     "【v82+ 零兜底】原因排查: 1) Viewport 未初始化? 2) 关卡切换中? 3) 玩家无本地窗口?"),
			ViewportSizeX, ViewportSizeY);
		return false;
	}

	const FVector2D ScreenPos = FVector2D(
		ViewportSizeX * 0.5f,
		ViewportSizeY * 0.5f
	);

	// (3) Deproject 转换屏幕坐标 → 世界射线
	FVector WorldOrigin;
	FVector WorldDirection;
	const bool bDeprojectOK = PC->DeprojectScreenPositionToWorld(
		ScreenPos.X,
		ScreenPos.Y,
		WorldOrigin,
		WorldDirection
	);
	if (!bDeprojectOK)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[GameHUDWidget::GetCrosshairWorldRay] DeprojectScreenPositionToWorld 失败. "
			     "ScreenPos=%s Player=%s. "
			     "【v82+ 零兜底】原因排查: 1) Viewport 未初始化? 2) ScreenPos 越界 (越界时 UE 返回 false)."),
			*ScreenPos.ToString(),
			*PC->GetName());
		return false;
	}

	OutWorldOrigin = WorldOrigin;
	OutWorldDirection = WorldDirection;
	return true;
}


// ==========================================
// 8. 结算系统
// ==========================================

/**
 * UGameHUDWidget::OnEnterSettlement
 *
 * 进入结算状态（倒计时归零时由 GameState 广播触发）
 * 【v200.2 大厂架构重构】: 根据模式区分显示
 *   - 生化模式: RoundWinner=Human→"人类胜利", Mother→"幽灵胜利", None→"平局"
 *   - 刀战模式: 暂不显示胜负文本（用户尚未规定内容）
 *
 * 1. 暂存当局击杀数
 * 2. 隐藏 MatchInfo + 准星
 * 3. 根据模式显示对应胜负文本（仅生化模式, 在 Text_GameOver 里)
 * 4. 隐藏计分板（等 OnShowFinalSettlement 再展示, 跨地图后由 UScoreboardWidget 接管）
 */
void UGameHUDWidget::OnEnterSettlement(int32 AttackerKills, int32 DefenderKills, EZombieRoundWinner RoundWinner)
{
	UE_LOG(LogTemp, Log, TEXT("[GameHUD] OnEnterSettlement: 攻方=%d, 守方=%d, RoundWinner=%d, Mode=%d"),
		AttackerKills, DefenderKills, static_cast<int32>(RoundWinner), static_cast<int32>(CachedMatchMode));

	// 暂存当局击杀数，供 3 秒后 OnShowFinalSettlement 使用
	LastAttackerKills = AttackerKills;
	LastDefenderKills = DefenderKills;

	// 隐藏 MatchInfo（倒计时归零，不再需要显示）
	if (Widget_MatchInfo)
	{
		Widget_MatchInfo->SetVisibility(ESlateVisibility::Collapsed);
	}

	// 隐藏准星
	HideCrosshair();

	// 【v216 大厂架构回滚】Text_GameOver 仍在 GameHUDWidget 显示 (战斗内瞬时反馈)
	// 大厂原则 — 单一真理源 (按业务场景):
	//   - Text_GameOver (战斗 HUD): 战斗内显示"当局胜负"瞬时文本 (3 秒后切图)
	//   - UScoreboardWidget (跨地图): L_Login 上显示完整结算面板 (含 Border + Button + 玩家列表)
	//   - 两者职责不同: Text_GameOver 是"瞬时反馈", ScoreboardWidget 是"完整结算"
	// 战斗内进入结算:
	//   - MulticastEnterSettlement 触发 → OnEnterSettlement → Text_GameOver 显示"人类胜利/幽灵胜利"
	//   - 紧接着 OnShowZombieRoundBriefResult / OnShowFinalSettlement 可能进一步刷新文本
	//   - OpenLevel 切图时 GameHUDWidget 销毁, Text_GameOver 也消失 (符合预期: 战斗内只看到瞬时反馈)
	if (Text_GameOver)
	{
		// 生化模式: 显示"人类胜利" / "幽灵胜利" / "平局"
		if (CachedMatchMode == ERoomMatchMode::Zombie)
		{
			FString WinnerText;
			switch (RoundWinner)
			{
			case EZombieRoundWinner::Human:
				WinnerText = TEXT("人类胜利");
				break;
			case EZombieRoundWinner::Mother:
				WinnerText = TEXT("幽灵胜利");
				break;
			default:
				WinnerText = TEXT("平局");
				break;
			}
			Text_GameOver->SetText(FText::FromString(WinnerText));
			Text_GameOver->SetVisibility(ESlateVisibility::HitTestInvisible);
		}
		else if (CachedMatchMode == ERoomMatchMode::Melee)
		{
			// 【v218 大厂架构修复】刀战模式: 不在此处显示 Text_GameOver
			//
			// 大厂原则 — 单一入口 (零重复架构):
			//   - 刀战 Text_GameOver 显示由 OnMeleeWinnerUpdated 触发的 EMeleeWinner (Attacker/Defender/Draw)
			//   - OnEnterSettlement 的 RoundWinner 参数对刀战无意义 (走 EZombieRoundWinner::None 显式传)
			//   - 旧 (v200.2): 在此处 Collapsed Text_GameOver → 会和 OnMeleeWinnerUpdated 抢显示 (顺序竞争)
			//
			// 不设置 Visibility — 让 OnMeleeWinnerUpdated 自由控制:
			//   - 如果 MeleeWinner 复制比 MulticastEnterSettlement 先到 → OnMeleeWinnerUpdated 已显示
			//   - 如果 MeleeWinner 复制后到 → OnMeleeWinnerUpdated 接着显示
			//   - 如果 GameHUDWidget 初始化晚于 MeleeWinner 复制 → 需要额外的 RefreshCache 路径
			//
			// 大厂原则 — 零兜底: 不主动 Hide 也不主动 Show, 业务分离 (OnMeleeWinnerUpdated 负责显示)
			UE_LOG(LogTemp, Log,
				TEXT("[GameHUDWidget] 【v218】OnEnterSettlement (Melee): Text_GameOver 显示委托给 OnMeleeWinnerUpdated. "
				     "MeleeWinner 复制抵达后客户端 UI 自动刷新."));

			// 大厂原则 — 防御性兜底: 如果 MeleeWinner 已经在 GameState 复制抵达 (时序早于本回调),
			//   兜底主动触发一次显示 (避免 OnMeleeWinnerUpdated 因某些极端时序未触发)
			// 大厂原则 — 单一真理源: MeleeWinner 字段从 GameState 取 (服务器权威 Replicated 字段)
			ARoomGameState* RoomGS = GetWorld() ? GetWorld()->GetGameState<ARoomGameState>() : nullptr;
			if (RoomGS && RoomGS->MeleeWinner != EMeleeWinner::None)
			{
				UE_LOG(LogTemp, Log,
					TEXT("[GameHUDWidget] 【v218】OnEnterSettlement (Melee): 检测到 MeleeWinner=%d 已同步, 主动显示 Text_GameOver."),
					static_cast<int32>(RoomGS->MeleeWinner));
				OnMeleeWinnerUpdated(RoomGS->MeleeWinner);
			}
			else if (!RoomGS)
			{
				UE_LOG(LogTemp, Error,
					TEXT("[GameHUDWidget] 【v218】OnEnterSettlement (Melee): RoomGS 为空, 跳过 MeleeWinner 检查. "
					     "【修复】检查 GameHUDWidget 初始化时 GameState 是否已存在."));
			}
		}
		else
		{
			// 未来其他模式: 暂不显示
			Text_GameOver->SetVisibility(ESlateVisibility::Collapsed);
		}
	}
	else
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[GameHUDWidget] OnEnterSettlement: Text_GameOver 未绑定, 无法显示战斗内胜负瞬时反馈. "
			     "【v216 警告】请检查 WBP_GameHUDWidget 是否包含同名 Text_GameOver 控件."));
	}

	// 隐藏计分板（3 秒结算动画期间不显示，等 OnShowFinalSettlement 再展示）
	if (Widget_Scoreboard)
	{
		Widget_Scoreboard->SetVisibility(ESlateVisibility::Collapsed);
	}
}


/**
 * UGameHUDWidget::OnShowZombieRoundBriefResult
 *
 * 【v201 大厂架构新增】短暂显示小局结果
 *
 * 业务场景:
 *   - 每小局结束时短暂显示"人类胜利"或"母体胜利"
 *   - 显示 3 秒后自动隐藏
 *   - 不进入结算页面，继续下一小局
 *
 * 【v216 大厂架构回滚】Text_GameOver 操作留在 GameHUDWidget:
 *   - 大厂原则: 战斗内"小局胜负"瞬时反馈是战斗 HUD 职责, 归 GameHUDWidget
 *   - UScoreboardWidget (跨地图) 只在完整结算时 (MulticastEnterSettlement 路径) 显示文本
 */
void UGameHUDWidget::OnShowZombieRoundBriefResult(EZombieRoundWinner RoundWinner)
{
	UE_LOG(LogTemp, Log, TEXT("[GameHUD] 【v201】OnShowZombieRoundBriefResult: RoundWinner=%d"),
		static_cast<int32>(RoundWinner));

	// 【v216 大厂架构回滚】Text_GameOver 操作保留在 GameHUDWidget (战斗内瞬时反馈)
	if (Text_GameOver)
	{
		FString WinnerText;
		switch (RoundWinner)
		{
		case EZombieRoundWinner::Human:
			WinnerText = TEXT("人类胜利");
			break;
		case EZombieRoundWinner::Mother:
			WinnerText = TEXT("幽灵胜利");
			break;
		default:
			WinnerText = TEXT("平局");
			break;
		}
		Text_GameOver->SetText(FText::FromString(WinnerText));
		Text_GameOver->SetVisibility(ESlateVisibility::HitTestInvisible);

		// 3 秒后自动隐藏 (与 ScheduleFinalSettlement 3 秒同步 — 2026.08.08 调回 3s)
		if (UWorld* World = GetWorld())
		{
			FTimerHandle HideTimer;
			FTimerDelegate HideDelegate = FTimerDelegate::CreateWeakLambda(this, [this]()
			{
				if (Text_GameOver)
				{
					Text_GameOver->SetVisibility(ESlateVisibility::Collapsed);
				}
			});
			World->GetTimerManager().SetTimer(HideTimer, HideDelegate, 3.0f, false);
		}
	}
}


/**
 * UGameHUDWidget::OnShowFinalSettlement
 *
 * 显示最终胜负结果（延迟 3 秒后由 GameState 广播触发）
 *
 * 【v216 大厂架构重构】结算覆盖板 + 返回大厅按钮已迁移:
 *   - 旧:
 *     * 隐藏 Text_GameOver
 *     * 显示 Border_SettlementOverlay
 *     * 显示 Widget_Scoreboard + 调 ShowRoundSettlement + ShowFinalResult
 *     * 显示 Button_ReturnToLobby
 *   - 新 (v216 — 切图到 L_Login):
 *     * 服务器 MulticastShowFinalSettlement_Implementation 写 USettlementSnapshotSubsystem.UpdateSnapshotWins
 *     * UScoreboardWidget 在 L_Login 上 ApplySnapshot 时读快照 → 自己管 Border / Button 显示
 *     * GameHUDWidget 完全不感知结算覆盖板, 单一职责: 战斗 HUD
 *
 * 保留职责:
 *   - 隐藏 Text_GameOver (战斗内"瞬时反馈"结束, 即将切图)
 *   - 隐藏 MatchInfo / 准星 (战斗 HUD 控件, 不属于结算)
 *   - 隐藏 Widget_Scoreboard (切图后 GameHUDWidget 整个被销毁)
 *
 * @param AttackerWins 攻方胜局数 (来自 GameState 广播)
 * @param DefenderWins 守方胜局数 (来自 GameState 广播)
 *
 * 大厂原则:
 *   - 本函数不应该再直接操作任何结算控件 (Border/Button)
 *   - Text_GameOver 是战斗内瞬时反馈 (MulticastEnterSettlement 时显示, 3 秒后隐藏)
 *   - 应该做的是"让战斗 HUD 退出视野", 切图后的 L_Login 由 UScoreboardWidget 接管
 */
void UGameHUDWidget::OnShowFinalSettlement(int32 AttackerWins, int32 DefenderWills)
{
	UE_LOG(LogTemp, Log, TEXT("[GameHUD] 【v216】OnShowFinalSettlement: 攻方胜%d局, 守方胜%d局. "
		"本函数仅负责战斗 HUD 退出视野, 结算覆盖板已迁移到 UScoreboardWidget (跨地图持久)."),
		AttackerWins, DefenderWills);

	// 【v216 大厂架构删除】Border_SettlementOverlay / Button_ReturnToLobby 操作已迁移
	// 旧:
	//   - Text_GameOver->SetVisibility(Collapsed)
	//   - Border_SettlementOverlay->SetVisibility(Visible)
	//   - Widget_Scoreboard->ShowRoundSettlement + ShowFinalResult
	//   - Button_ReturnToLobby->SetVisibility(Visible)
	// 新:
	//   - UScoreboardWidget::ApplySnapshot 在 L_Login 上拉 USettlementSnapshotSubsystem 快照
	//   - ApplySnapshot 内部完成: Border 显示 + 文本刷新 + Button 显示 + 绑定 OnReturnToLobbyClicked
	//   - 本函数仅负责"战斗 HUD 退出视野"

	// 【保留】隐藏战斗 HUD (不依赖切图, 旧切图前逻辑保留)
	if (Widget_MatchInfo)
	{
		Widget_MatchInfo->SetVisibility(ESlateVisibility::Collapsed);
	}
	HideCrosshair();

	// 【v216 大厂架构回滚】Text_GameOver 隐藏:
	//   - OnEnterSettlement 显示"人类胜利/幽灵胜利"瞬时文本
	//   - OnShowFinalSettlement (3 秒后) 隐藏该文本 (即将切图到 L_Login)
	// 大厂原则: Text_GameOver 是战斗内"瞬时反馈", 切图后 GameHUDWidget 销毁, 不需要跨地图
	if (Text_GameOver)
	{
		Text_GameOver->SetVisibility(ESlateVisibility::Collapsed);
	}

	// 【v216 大厂架构删除】Widget_Scoreboard 操作:
	//   旧: 显示 Scoreboard + 调 ShowRoundSettlement(LastAttackerKills, LastDefenderKills) + ShowFinalResult(AttackerWins, DefenderWills)
	//   新: UScoreboardWidget 在 L_Login 上通过 ApplySnapshot 拉 USettlementSnapshotSubsystem 一次性拿数据
	//        GameHUDWidget 不再调 ShowRoundSettlement / ShowFinalResult (那两个函数 v217 也会重构成 ApplySnapshot 入口)
}


// ==========================================
// 9. 武器图标刷新（按武器 ID）
// ==========================================

/**
 * UGameHUDWidget::UpdateWeaponIconFromID
 *
 * 根据武器 ID 从 DT_WeaponInfo 刷新武器面板图标
 * 1. 校验 Widget_WeaponPanel 绑定
 * 2. 从 GameMode->WeaponDataTable 查找
 * 3. 调用 Widget_WeaponPanel->UpdateMeleeWeaponIcon
 */
void UGameHUDWidget::UpdateWeaponIconFromID(const FString& WeaponID)
{
	if (WeaponID.IsEmpty()) return;

	if (!Widget_WeaponPanel)
	{
		UE_LOG(LogTemp, Error, TEXT("[GameHUDWidget] UpdateWeaponIconFromID: Widget_WeaponPanel 未绑定! 请检查 WBP_GameHUDWidget 蓝图中是否正确拖入了 WeaponPanel 子控件"));
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("[GameHUDWidget] UpdateWeaponIconFromID: Widget_WeaponPanel=%s, WeaponID=%s"), *Widget_WeaponPanel->GetName(), *WeaponID);

	if (UWorld* World = GetWorld())
	{
		if (ARoomGameMode* GM = Cast<ARoomGameMode>(World->GetAuthGameMode()))
		{
			if (!GM->WeaponDataTable)
			{
				UE_LOG(LogTemp, Error, TEXT("[GameHUDWidget] UpdateWeaponIconFromID: WeaponDataTable 未配置"));
				return;
			}

			static const FString ContextString(TEXT("HUD_WeaponIconLookup"));
			FWeaponInfo* WeaponInfo = GM->WeaponDataTable->FindRow<FWeaponInfo>(FName(*WeaponID), ContextString);
			if (!WeaponInfo)
			{
				UE_LOG(LogTemp, Error, TEXT("[GameHUDWidget] UpdateWeaponIconFromID: DT_WeaponInfo 中找不到 WeaponID=%s"), *WeaponID);
				return;
			}
			if (!WeaponInfo->WeaponIcon)
			{
				UE_LOG(LogTemp, Error, TEXT("[GameHUDWidget] UpdateWeaponIconFromID: WeaponID=%s 的 WeaponIcon 字段为空，请在 DT_WeaponInfo 中为该行配置图标资源"), *WeaponID);
				return;
			}

			UE_LOG(LogTemp, Log, TEXT("[GameHUDWidget] UpdateWeaponIconFromID: 找到图标资源=%s, 准备刷新 Image_MeleeWeapon"), *WeaponInfo->WeaponIcon->GetName());
			Widget_WeaponPanel->UpdateMeleeWeaponIcon(WeaponInfo->WeaponIcon);
			UE_LOG(LogTemp, Log, TEXT("[GameHUDWidget] 刷新武器图标完成: %s"), *WeaponID);
		}
	}
}


// ==========================================
// 10. 返回大厅
// ==========================================

/**
 * 【v216 大厂架构删除】OnReturnToLobbyClicked 已迁移到 UScoreboardWidget::OnReturnToLobbyClicked
 *
 * 历史: 本函数调 PC->LeaveRoom() 让玩家退出房间关卡
 * v216: UScoreboardWidget::Button_ReturnToLobby 走 RPC 链路
 * v216.2 大厂架构重构: UScoreboardWidget 直接调 UGameFlowSubsystem::RequestStateOnNextLoad(MainLobby)
 *   - 去除 RPC 链路 (Server_SettlementReturnToLobby / Client_OpenLobbyFromSettlement 已全部删除)
 *   - 客户端本地操作, 不需要服务器往返
 *   - GameFlowSubsystem 在 L_Login 上切到 MainLobby 状态, UIViewService ShowPanel(MainLobbyPanel)
 *
 * 大厂原则 — 0 兜底:
 *   - 旧路径是"GameHUDWidget::OnReturnToLobbyClicked → PC->LeaveRoom → OpenLevel"
 *   - v216 路径是"RPC → 状态机", 但跨地图后 PC 类型变化导致 Cast 失败 (按钮无反应)
 *   - v216.2 路径是"客户端直接调 GameFlowSubsystem", 跨 PC 类型工作
 *   - 旧路径强行同步切图 (无视其他客户端), 违反 Room 生命周期管理
 *
 * 函数实现已删除 (本函数以前在 .cpp 第 1235 行附近, 现有代码已删除)
 */


// ==========================================
// 11. CharacterEvents 订阅 (2026-07-01 新增 - 依赖倒置核心)
// 替代 BaseCharacter 直接 Push GameHUDWidget 的旧模式
// ==========================================

/**
 * UGameHUDWidget::TryBindToCharacterEvents
 *
 * 尝试订阅 CharacterEvents 组件 (挂在 Pawn 上)
 * 流程: PC -> GetPawn -> Cast<UCharacterEvents> -> AddDynamic
 * 成功: 记录 CachedCharacterEvents 以便后续 Unbind
 * 失败: 0.5s 重试, 最多 10 次 (共 5 秒, 足够 Pawn 切换)
 *
 * 【2026-07-01 P0 修复】"事件 + 缓存"双轨制: 订阅成功后**主动拉取快照**
 * 设计动机:
 *   - BaseCharacter::PossessedBy 在 GameHUDWidget 创建之前调用 Client_RefreshCharacterIcon
 *   - 那次 Broadcast CharacterIconReady 事件时, 没有订阅者 → 头像永远丢
 *   - 现在 CharacterEvents 缓存最近一次头像, 订阅成功后立刻拉取快照, 主动调用 OnCharacterIconReady 模拟"补发"
 *   - 这样无论何时订阅, 都能拿到头像
 */
void UGameHUDWidget::TryBindToCharacterEvents()
{
	// 防御: 检查是否已绑定
	if (CachedCharacterEvents)
	{
		return;
	}

	// 通过 PlayerController 获取当前控制的角色
	APlayerController* PC = GetOwningPlayer();
	ABaseCharacter* Character = PC ? Cast<ABaseCharacter>(PC->GetPawn()) : nullptr;

	if (Character && Character->ResolveCharacterEvents())
	{
		UCharacterEvents* Events = Character->ResolveCharacterEvents();

		// 订阅 7 个角色状态事件
		Events->OnCharacterIconReady.AddDynamic(this,   &UGameHUDWidget::OnCharacterIconReady);
		Events->OnHealthChangedDelegate.AddDynamic(this, &UGameHUDWidget::OnHealthChanged);
		Events->OnEnergyChangedDelegate.AddDynamic(this, &UGameHUDWidget::OnEnergyChanged);
		Events->OnACValueChanged.AddDynamic(this,       &UGameHUDWidget::OnACValueChanged);
		Events->OnACEValueChanged.AddDynamic(this,     &UGameHUDWidget::OnACEValueChanged);
		Events->OnACEWithRankChanged.AddDynamic(this,  &UGameHUDWidget::OnACEWithRankChanged);
		Events->OnWeaponIconReady.AddDynamic(this,     &UGameHUDWidget::OnWeaponIconReady);
		Events->OnWeaponAmmoInfoReady.AddDynamic(this, &UGameHUDWidget::OnWeaponAmmoInfoReady);

		// 【2026.07.14 新增】订阅无敌期状态变化 - 控制复活进度条显示/隐藏
		Events->OnInvincibilityChanged.AddDynamic(this, &UGameHUDWidget::OnInvincibilityChanged);

		// 【v201.6 大厂架构新增】订阅移动锁定状态变化 - 控制复活进度条显示移动锁定倒计时
		Events->OnRespawnMovementLockedChanged.AddDynamic(this, &UGameHUDWidget::OnRespawnMovementLockedChanged);

		// 【v105 新增】订阅武器面板显隐状态变化 - 母体无武器时隐藏弹药 UI
		Events->OnWeaponPanelVisibilityChanged.AddUObject(this, &UGameHUDWidget::OnWeaponPanelVisibilityChanged);

		// 【v120 新增】订阅母体加速技能冷却状态 - 控制 CDProgress 材质参数
		Events->OnMotherSkillCooldownChanged.AddDynamic(this, &UGameHUDWidget::OnMotherSkillCooldownChanged);

		CachedCharacterEvents = Events;

		UE_LOG(LogTemp, Log,
			TEXT("[GameHUDWidget][Bind] 成功订阅 CharacterEvents, Pawn=%s, IsLocallyControlled=%d"),
			*Character->GetName(), Character->IsLocallyControlled() ? 1 : 0);

		// ==========================================
		// 【2026-07-01 P0 修复】"事件 + 缓存"双轨制 - 主动拉取快照补发
		// ==========================================
		// 1. 头像快照补发
		{
			FString CachedCharID;
			UTexture2D* CachedAvatar = nullptr;
			if (Events->GetCachedCharacterIcon(CachedCharID, CachedAvatar))
			{
				UE_LOG(LogTemp, Log,
					TEXT("[GameHUDWidget][Bind-Snapshot] 补发头像: CharID=%s, Avatar=%s"),
					*CachedCharID,
					CachedAvatar ? *CachedAvatar->GetName() : TEXT("nullptr (查表失败)"));
				OnCharacterIconReady(CachedCharID, CachedAvatar);
			}
			else
			{
				UE_LOG(LogTemp, Verbose,
					TEXT("[GameHUDWidget][Bind-Snapshot] 无头像缓存, 等待 CharacterEvents::OnCharacterIconReady 事件"));
			}
		}

		// 1.5. 【v40.2 P0 新增】武器图标快照补发 (镜像头像补发)
		//   根因: 旧版没补发武器图标 → 客户端武器 RPC 比 HUD 订阅早触发 → 武器图标永远丢失
		//   修复: 主动拉武器图标缓存, 调用 OnWeaponIconReady 模拟补发
		{
			FString CachedWeaponID;
			UTexture2D* CachedWeaponIcon = nullptr;
			if (Events->GetCachedWeaponIcon(CachedWeaponID, CachedWeaponIcon))
			{
				UE_LOG(LogTemp, Log,
					TEXT("[GameHUDWidget][Bind-Snapshot] 补发武器图标: WeaponID=%s, Icon=%s"),
					*CachedWeaponID,
					CachedWeaponIcon ? *CachedWeaponIcon->GetName() : TEXT("nullptr (查表失败)"));
				OnWeaponIconReady(CachedWeaponID, CachedWeaponIcon);
			}
			else
			{
				UE_LOG(LogTemp, Verbose,
					TEXT("[GameHUDWidget][Bind-Snapshot] 无武器图标缓存, 等待 CharacterEvents::OnWeaponIconReady 事件"));
			}
		}

		// 【v85.2 大厂架构新增】弹药快照补发 (HUD 绑定成功后立即显示弹夹数)
		// 根因: CharacterIconComponent::BeginPlay 执行时武器 WeaponFireComponent 可能未初始化
		//       → BroadcastWeaponAmmoInfo 读到空弹药 → HUD 显示为空
		//       → 用户需要切枪才能看到弹夹数
		// 解决方案: HUD 订阅成功后，从 CharacterEvents::OnWeaponAmmoInfoReady 缓存快照直接补发到 UI
		//
		// 【v105.2 大厂架构改造】母体路径强制 1/1
		// 业务规则 (用户 2026.07.27 反馈):
		//   - 母体没有武器, 弹药数据固定 1/1 (跟刀战模式一样显示弹药 UI)
		//   - 武器图标显示 DT_WeaponInfo.MT001
		//   - 不再隐藏 WeaponPanel
		{
			if (Character->bIsMother)
			{
				// 母体弹药固定 1/1 (母体攻击不消耗弹药)
				UE_LOG(LogTemp, Display,
					TEXT("[GameHUDWidget][Bind-Snapshot] 母体弹药固定 1/1 (Owner=%s)"),
					*Character->GetName());
				OnWeaponAmmoInfoReady(1, 1, 1);
			}
			else
			{
				// 检查缓存的弹药数据
				int32 CachedCurrentAmmo = -1;
				int32 CachedMagazineSize = -1;
				int32 CachedReserveAmmo = -1;
				bool bCachedIsMelee = false;
				if (Events->GetCachedWeaponAmmoInfo(CachedCurrentAmmo, CachedMagazineSize, CachedReserveAmmo, bCachedIsMelee))
				{
					UE_LOG(LogTemp, Log,
						TEXT("[GameHUDWidget][Bind-Snapshot] 补发弹药: %d/%d (MagSize=%d, Reserve=%d, Melee=%d)"),
						CachedCurrentAmmo, CachedMagazineSize, CachedMagazineSize, CachedReserveAmmo, bCachedIsMelee ? 1 : 0);
					OnWeaponAmmoInfoReady(CachedCurrentAmmo, CachedMagazineSize, CachedReserveAmmo);
				}
				else
				{
					// 如果 CharacterEvents 没有缓存，直接从武器组件读取
					if (ABaseWeapon* CurrentWeapon = Character->GetCurrentWeapon())
					{
						if (UWeaponFireComponent* FireComp = CurrentWeapon->FindComponentByClass<UWeaponFireComponent>())
						{
							const int32 CurrentAmmo = FireComp->GetCurrentAmmo();
							const int32 MagazineSize = FireComp->GetMagazineSize();
							const int32 ReserveAmmo = FireComp->GetReserveAmmo();
							const bool bIsMelee = (CurrentWeapon->GetMeshType() == EWeaponMeshType::Melee);

							UE_LOG(LogTemp, Log,
								TEXT("[GameHUDWidget][Bind-Snapshot] 从武器组件补发弹药: %d/%d (MagSize=%d, Reserve=%d, Melee=%d)"),
								CurrentAmmo, MagazineSize, MagazineSize, ReserveAmmo, bIsMelee ? 1 : 0);
							OnWeaponAmmoInfoReady(CurrentAmmo, MagazineSize, ReserveAmmo);
						}
					}
				}
			}
		}

		// 【v121 大厂架构新增】母体技能图标显隐快照补发
		// 根因: HUD 订阅成功时，CharacterIconComponent 可能还没有触发 OnMotherSkillCooldownChanged
		//        → 非母体的技能图标可能一直显示
		// 修复: HUD 订阅成功后主动设置技能图标显隐（非母体=隐藏，母体=显示）
		{
			const bool bIsMother = Character->bIsMother;
			UE_LOG(LogTemp, Display,
				TEXT("[GameHUDWidget][Bind-Snapshot] 母体技能图标初始化: bIsMother=%d"),
				bIsMother ? 1 : 0);
			if (Widget_PlayerStatus)
			{
				Widget_PlayerStatus->SetMotherSkillIconVisibility(bIsMother);
			}
		}

		// 2. AC 快照补发 (主动拉取, 避免 OnRep_ACValue 比订阅晚触发)
		{
			const int32 CachedAC = Events->GetCachedACValue();
			if (CachedAC >= 0 && Widget_PlayerStatus)
			{
				UE_LOG(LogTemp, Verbose, TEXT("[GameHUDWidget][Bind-Snapshot] 补发 AC=%d"), CachedAC);
				Widget_PlayerStatus->UpdateACValue(CachedAC);
			}
		}

		// 【v40.9 增强 v40.8】无敌期状态快照补发
		// 根因 (v40.7): 玩家复活时 ActivateSpawnInvincibility 先于 HUD 订阅触发
		//   → OnInvincibilityChanged(true) 事件丢失 → Show() 从未被调用 → 进度条不显示
		// 修复 (v40.7): HUD 订阅成功后主动检查当前 invincibility 状态，如已激活则立即 Show
		//
		// 二次根因 (v40.8): 旧版用 `HC->IsInvincible()` (bool 字段)
		//   - UE 网络复制: bIsInvincible 字段的"初始值" 在某些时序下不会触发 OnRep
		//     → 客户端字段可能停留在 false 即使无敌期还未结束
		//   - 或: 客户端先收到 true → OnRep true → Bind 之前就收到 false → 字段 false
		//     → Bind 时 HC->IsInvincible() == false → 漏补发
		// 二次修复 (v40.8): 改用 `GetInvincibilityRemainingSeconds() > 0` 判定
		//   - InvincibilityExpiresAtWorldTime 是 Replicated 绝对时间
		//   - 客户端读 World->GetTimeSeconds() 算剩余 → 即使 OnRep 初始值丢失,也能从 expires-at 字段反推
		//   - 这才是无敌期状态的"真理源" (大厂原则: 派生字段 > 衍生 bool 字段)
		//
		// 三次增强 (v40.9): GetInvincibilityRemainingSeconds 内部已用 GameState 时间 (服务器权威 + 自动网络延迟补偿)
		//   - 这是 Bind-Snapshot 兜底链路的最后一环
		//   - 即使 Replicated 字段有微小时序差异,剩余秒数仍准确
		{
			if (UHealthComponent* HC = Character->ResolveHealthComponent())
			{
				const float Remaining = HC->GetInvincibilityRemainingSeconds();
				if (Remaining > 0.0f && Widget_RespawnProgress)
				{
					UE_LOG(LogTemp, Log,
						TEXT("[GameHUDWidget][Bind-Snapshot][v40.8] 仍在无敌期, 强制显示复活进度条. "
							 "Pawn=%s, Remaining=%.2fs"),
						*Character->GetName(), Remaining);
					Widget_RespawnProgress->Show();
				}
			}
		}

		// 3. ACE + 排名 快照补发
		{
			int32 CachedACE = -1;
			EACERankType CachedRank = EACERankType::None;
			Events->GetCachedACEState(CachedACE, CachedRank);
			if (CachedACE >= 0 && Widget_PlayerStatus)
			{
				UE_LOG(LogTemp, Verbose, TEXT("[GameHUDWidget][Bind-Snapshot] 补发 ACE=%d, Rank=%d"),
					CachedACE, (int32)CachedRank);
				Widget_PlayerStatus->SetACEValueWithRank(CachedACE, CachedRank);
			}
		}

		// ==========================================
		// 4. HP / Energy 不缓存, 但仍主动拉一次 (实时性高, 但被订阅频率低, 拉一次保险)
		// [v40 P0 修复] 必须用 ResolveHealthComponent/ResolveEnergyComponent 而非裸字段
		// ==========================================
		if (Widget_PlayerStatus)
		{
			if (UHealthComponent* HC = Character->ResolveHealthComponent())
			{
				Widget_PlayerStatus->UpdateHealth(HC->GetCurrent(), HC->GetMax());
				Widget_PlayerStatus->UpdateHealthText(
					FMath::CeilToInt(HC->GetCurrent()),
					FMath::CeilToInt(HC->GetMax()));
			}
			else
			{
				UE_LOG(LogTemp, Error,
					TEXT("[GameHUDWidget][Bind-Snapshot] ResolveHealthComponent 失败. Pawn=%s"),
					*Character->GetName());
			}

			if (UEnergyComponent* EC = Character->ResolveEnergyComponent())
			{
				Widget_PlayerStatus->UpdateEnergy(EC->GetCurrent(), EC->GetMax());
				Widget_PlayerStatus->UpdateEnergyText(
					FMath::CeilToInt(EC->GetCurrent()),
					FMath::CeilToInt(EC->GetMax()));
			}
			else
			{
				UE_LOG(LogTemp, Error,
					TEXT("[GameHUDWidget][Bind-Snapshot] ResolveEnergyComponent 失败. Pawn=%s"),
					*Character->GetName());
			}
		}

		// 【v105.2 大厂架构改造】武器面板永久显示 — 弹药 UI 和武器图标都由各自 RPC 推送内容
		// 不再基于 bIsMother 隐藏 Widget_WeaponPanel
		//   - 弹药: Client_RefreshWeaponAmmo RPC + HUD 订阅时 1/1 补发
		//   - 武器图标: Client_RefreshWeaponIcon RPC (服务端查表推 MuTiWeapon)

		// 清理重试定时器
		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().ClearTimer(CharacterEventsRetryTimerHandle);
		}
		CharacterEventsRetryCount = 0;
		return;
	}

	// 绑定失败, 延迟重试
	CharacterEventsRetryCount++;
	if (CharacterEventsRetryCount < 10)
	{
		UE_LOG(LogTemp, Verbose,
			TEXT("[GameHUDWidget][Bind-Retry] CharacterEvents 未就绪, 重试中 (%d/10), Pawn=%s"),
			CharacterEventsRetryCount,
			Character ? *Character->GetName() : TEXT("nullptr"));

		if (UWorld* World = GetWorld())
		{
			World->GetTimerManager().SetTimer(
				CharacterEventsRetryTimerHandle, this,
				&UGameHUDWidget::TryBindToCharacterEvents, 0.5f, false);
		}
	}
	else
	{
		UE_LOG(LogTemp, Error,
			TEXT("[GameHUDWidget][Bind-Failed] CharacterEvents 绑定失败 (已达最大重试次数 10 次), 请检查 BaseCharacter 是否正确挂载 CharacterEvents 组件"));
		CharacterEventsRetryCount = 0;
	}
}


/**
 * UGameHUDWidget::UnbindFromCharacterEvents
 *
 * 取消订阅 CharacterEvents (在 Pawn 切换时调用)
 * 用途: 防止旧 Pawn 的 CharacterEvents 被销毁后, 回调仍被触发
 */
void UGameHUDWidget::UnbindFromCharacterEvents()
{
	if (CachedCharacterEvents)
	{
		CachedCharacterEvents->OnCharacterIconReady.RemoveDynamic(this,   &UGameHUDWidget::OnCharacterIconReady);
		CachedCharacterEvents->OnHealthChangedDelegate.RemoveDynamic(this, &UGameHUDWidget::OnHealthChanged);
		CachedCharacterEvents->OnEnergyChangedDelegate.RemoveDynamic(this, &UGameHUDWidget::OnEnergyChanged);
		CachedCharacterEvents->OnACValueChanged.RemoveDynamic(this,       &UGameHUDWidget::OnACValueChanged);
		CachedCharacterEvents->OnACEValueChanged.RemoveDynamic(this,     &UGameHUDWidget::OnACEValueChanged);
		CachedCharacterEvents->OnACEWithRankChanged.RemoveDynamic(this,  &UGameHUDWidget::OnACEWithRankChanged);
		CachedCharacterEvents->OnWeaponIconReady.RemoveDynamic(this,     &UGameHUDWidget::OnWeaponIconReady);
		CachedCharacterEvents->OnWeaponAmmoInfoReady.RemoveDynamic(this, &UGameHUDWidget::OnWeaponAmmoInfoReady);
		CachedCharacterEvents->OnInvincibilityChanged.RemoveDynamic(this, &UGameHUDWidget::OnInvincibilityChanged);

		// 【v201.6 大厂架构新增】取消订阅移动锁定状态变化
		CachedCharacterEvents->OnRespawnMovementLockedChanged.RemoveDynamic(this, &UGameHUDWidget::OnRespawnMovementLockedChanged);

		CachedCharacterEvents->OnWeaponPanelVisibilityChanged.RemoveAll(this);

		// 【v120 新增】取消订阅母体加速技能冷却
		CachedCharacterEvents->OnMotherSkillCooldownChanged.RemoveDynamic(this, &UGameHUDWidget::OnMotherSkillCooldownChanged);

		CachedCharacterEvents = nullptr;
		CharacterEventsRetryCount = 0;
	}
}


// ==========================================
// 7 个 CharacterEvents 回调实现
// ==========================================

/**
 * UGameHUDWidget::OnCharacterIconReady
 *
 * 角色头像加载完毕回调 (由 CharacterEvents 广播触发)
 * 服务器传递 UTexture2D* Avatar, 解决客户端查表失败的白板问题
 */
void UGameHUDWidget::OnCharacterIconReady(const FString& CharacterID, class UTexture2D* Icon)
{
	if (Widget_PlayerStatus)
	{
		Widget_PlayerStatus->UpdateCharacterIcon(Icon);
	}
}


/**
 * UGameHUDWidget::OnHealthChanged
 *
 * 血量变化回调 (由 CharacterEvents 广播触发)
 * 同时更新进度条和文本
 */
void UGameHUDWidget::OnHealthChanged(float Current, float Max)
{
	if (Widget_PlayerStatus)
	{
		Widget_PlayerStatus->UpdateHealth(Current, Max);
		Widget_PlayerStatus->UpdateHealthText(FMath::CeilToInt(Current), FMath::CeilToInt(Max));
	}
}


/**
 * UGameHUDWidget::OnEnergyChanged
 *
 * 能量变化回调 (由 CharacterEvents 广播触发)
 * 同时更新进度条和文本
 */
void UGameHUDWidget::OnEnergyChanged(float Current, float Max)
{
	if (Widget_PlayerStatus)
	{
		Widget_PlayerStatus->UpdateEnergy(Current, Max);
		Widget_PlayerStatus->UpdateEnergyText(FMath::CeilToInt(Current), FMath::CeilToInt(Max));
	}
}


/**
 * UGameHUDWidget::OnACValueChanged
 *
 * AC 防护服值变化回调
 */
void UGameHUDWidget::OnACValueChanged(int32 NewAC)
{
	if (Widget_PlayerStatus)
	{
		Widget_PlayerStatus->UpdateACValue(NewAC);
	}
}


/**
 * UGameHUDWidget::OnACEValueChanged
 *
 * ACE 击杀数变化回调 (无排名, 默认白色)
 */
void UGameHUDWidget::OnACEValueChanged(int32 NewACE)
{
	if (Widget_PlayerStatus)
	{
		Widget_PlayerStatus->UpdateACEValue(NewACE);
	}
}


/**
 * UGameHUDWidget::OnACEWithRankChanged
 *
 * ACE 击杀数 + 排名颜色变化回调
 */
void UGameHUDWidget::OnACEWithRankChanged(int32 NewACE, EACERankType RankType)
{
	if (Widget_PlayerStatus)
	{
		Widget_PlayerStatus->SetACEValueWithRank(NewACE, RankType);
	}
}


/**
 * UGameHUDWidget::OnWeaponIconReady
 *
 * 武器图标加载完毕回调
 * WeaponID 从 CharacterEvents 传入, 触发异步加载流程
 */
void UGameHUDWidget::OnWeaponIconReady(const FString& WeaponID, class UTexture2D* Icon)
{
	if (WeaponID.IsEmpty())
	{
		return;
	}

	// 如果服务器直接传递了 Icon, 直接使用
	if (Icon)
	{
		if (Widget_WeaponPanel)
		{
			Widget_WeaponPanel->UpdateMeleeWeaponIcon(Icon);
		}
		return;
	}

	// Icon 为空: 通过 WeaponID 从 DataTable 加载
	UpdateWeaponIconFromID(WeaponID);
}


/**
 * UGameHUDWidget::OnWeaponAmmoInfoReady
 *
 * 【v84 大厂架构新增】武器弹药数量加载完毕回调
 *
 * 触发时机:
 *   - 服务器 CharacterIconComponent::BroadcastWeaponAmmoInfo
 *   - 在武器图标刷新时同步广播 (RefreshWeaponIconOnHUD 末尾)
 *
 * 格式规则:
 *   - 近战武器 (MeshType=Melee): "1/1"
 *   - 枪械: "CurrentMag/MagazineSize + ReserveAmmo"
 *
 * @param CurrentMag   当前弹匣弹药
 * @param MagazineSize 弹匣容量
 * @param ReserveAmmo  备用弹药总数
 */
void UGameHUDWidget::OnWeaponAmmoInfoReady(int32 CurrentMag, int32 MagazineSize, int32 ReserveAmmo)
{
	if (!Widget_WeaponPanel)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[GameHUDWidget] OnWeaponAmmoInfoReady: Widget_WeaponPanel 未绑定! "
				 "请检查 WBP_GameHUDWidget 蓝图中是否正确拖入了 WeaponPanel 子控件"));
		return;
	}

	// 根据武器类型判断是否为近战武器
	// 弹药信息中 ReserveAmmo=0 且 MagazineSize=1 的是近战武器标志
	const bool bIsMelee = (MagazineSize == 1 && ReserveAmmo == 0);

	// 【v208.6 增强日志】★ 标记让用户一眼能看到弹药 RPC 是否收到
	UE_LOG(LogTemp, Display,
		TEXT("[GameHUDWidget] ★ OnWeaponAmmoInfoReady: CurrentMag=%d, MagazineSize=%d, ReserveAmmo=%d, bIsMelee=%d. "
		     "【v208.6】如果日志消失 = RPC 没收到，检查 Server_RefillAmmo 是否被调用."),
		CurrentMag, MagazineSize, ReserveAmmo, bIsMelee ? 1 : 0);

	Widget_WeaponPanel->UpdateWeaponAmmoText(CurrentMag, MagazineSize, ReserveAmmo, bIsMelee);
}


/**
 * UGameHUDWidget::OnWeaponPanelVisibilityChanged 【v105 新增】
 *
 * 武器面板显隐状态变化回调 — 用于母体等无武器角色隐藏武器弹药 UI
 *
 * 业务规则 (用户 2026.07.27 明确):
 *   - 母体没有武器, 不应该显示弹药 UI (Text_WeaponAmmo / Image_MeleeWeapon)
 *   - 变成母体时隐藏武器面板, 变回人类时显示武器面板
 *
 * @param bIsVisible true=显示武器面板, false=隐藏武器面板
 */
/**
 * UGameHUDWidget::OnWeaponPanelVisibilityChanged
 *
 * 触发场景: CharacterEvents::OnWeaponPanelVisibilityChanged 事件 (监听服/事件触发时的备用通道)
 *
 * 【v105 重构】主路径已改为 Client_RefreshCharacterIcon RPC 直接调 SetWeaponPanelVisible,
 *              本函数保留作为 CharacterEvents 事件总线的备用通道 (用于没有走 RPC 的边缘 case)
 */
void UGameHUDWidget::OnWeaponPanelVisibilityChanged(bool bIsVisible)
{
	// 【v105 转发】统一走 SetWeaponPanelVisible (消除重复代码)
	SetWeaponPanelVisible(bIsVisible);
}


void UGameHUDWidget::SetWeaponPanelVisible(bool bIsVisible)
{
	if (!Widget_WeaponPanel)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[GameHUDWidget] SetWeaponPanelVisible: Widget_WeaponPanel 未绑定! "
				 "请检查 WBP_GameHUDWidget 蓝图中是否正确拖入了 WeaponPanel 子控件"));
		return;
	}

	UE_LOG(LogTemp, Display,
		TEXT("[GameHUDWidget] SetWeaponPanelVisible: bIsVisible=%d (Widget_WeaponPanel->%s)"),
		bIsVisible ? 1 : 0, bIsVisible ? TEXT("显示") : TEXT("隐藏"));

	// 控制武器面板显隐
	Widget_WeaponPanel->SetVisibility(bIsVisible ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
}


// ==========================================
// 9. 无敌期进度条控制 (2026.07.14 重构)
// ==========================================

/**
 * UGameHUDWidget::OnInvincibilityChanged
 *
 * 无敌期状态变化回调 - 控制复活进度条的显示/隐藏
 *
 * 【2026.07.14 重构说明】
 * 旧架构: URespawnProgressWidget 自己订阅事件 + 控制自己的 Show/Hide
 *        但 Widget_RespawnProgress 的 Visibility 从未被控制 → 进度条不显示
 *
 * 新架构 (大厂单一订阅点原则):
 *   - UGameHUDWidget 是唯一订阅 OnInvincibilityChanged 的地方
 *   - UGameHUDWidget 直接控制 Widget_RespawnProgress 的 Visibility
 *   - URespawnProgressWidget 只负责进度条和倒计时文本的内容更新
 *
 * @param bIsNowInvincible true=进入无敌期(显示进度条), false=退出无敌期(隐藏进度条)
 */
void UGameHUDWidget::OnInvincibilityChanged(bool bIsNowInvincible)
{
	UE_LOG(LogTemp, Display,
		TEXT("[GameHUDWidget] OnInvincibilityChanged 被调用. bIsNowInvincible=%d"),
		bIsNowInvincible ? 1 : 0);

	if (!Widget_RespawnProgress)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[GameHUDWidget] OnInvincibilityChanged: Widget_RespawnProgress 未绑定! "
				"请在 WBP_GameHUDWidget 蓝图中正确拖入复活进度条控件。"));
		return;
	}

	if (bIsNowInvincible)
	{
		// 进入无敌期 - 显示进度条（Show() 会记录总时长并立即更新内容）
		Widget_RespawnProgress->Show();
		UE_LOG(LogTemp, Display,
			TEXT("[GameHUDWidget] OnInvincibilityChanged: 进入无敌期, 显示复活进度条。 Widget=%s"),
			*Widget_RespawnProgress->GetName());
	}
	else
	{
		// 退出无敌期 - 隐藏进度条
		Widget_RespawnProgress->Hide();
		UE_LOG(LogTemp, Display,
			TEXT("[GameHUDWidget] OnInvincibilityChanged: 退出无敌期, 隐藏复活进度条。 Widget=%s"),
			*Widget_RespawnProgress->GetName());
	}
}


/**
 * UGameHUDWidget::OnRespawnMovementLockedChanged 【v201.6 大厂架构新增】
 *
 * 移动锁定状态变化回调 - 控制复活进度条显示移动锁定倒计时
 *
 * 订阅: CharacterEvents.OnRespawnMovementLockedChanged
 * 触发: HealthComponent 移动锁定状态变化
 *
 * @param bIsLocked true=进入锁定(显示倒计时), false=退出锁定(隐藏倒计时)
 * @param Duration 锁定时长(秒)
 */
void UGameHUDWidget::OnRespawnMovementLockedChanged(bool bIsLocked, float Duration)
{
	UE_LOG(LogTemp, Display,
		TEXT("[GameHUDWidget] OnRespawnMovementLockedChanged: bIsLocked=%d Duration=%.2f"),
		bIsLocked ? 1 : 0, Duration);

	if (!Widget_RespawnProgress)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[GameHUDWidget] OnRespawnMovementLockedChanged: Widget_RespawnProgress 未绑定!"));
		return;
	}

	if (bIsLocked)
	{
		// 进入移动锁定 - 显示移动锁定倒计时
		Widget_RespawnProgress->ShowMovementLock(Duration);
		UE_LOG(LogTemp, Display,
			TEXT("[GameHUDWidget] OnRespawnMovementLockedChanged: 进入移动锁定, 显示移动锁定倒计时 Duration=%.2fs. Widget=%s"),
			Duration, *Widget_RespawnProgress->GetName());
	}
	else
	{
		// 退出移动锁定 - 隐藏移动锁定倒计时
		Widget_RespawnProgress->HideMovementLock();
		UE_LOG(LogTemp, Display,
			TEXT("[GameHUDWidget] OnRespawnMovementLockedChanged: 退出移动锁定, 隐藏移动锁定倒计时。 Widget=%s"),
			*Widget_RespawnProgress->GetName());
	}
}


void UGameHUDWidget::OnMotherSkillCooldownChanged(float CDProgress, bool bSkillActive)
{
	UE_LOG(LogTemp, Display,
		TEXT("[GameHUDWidget] OnMotherSkillCooldownChanged: CDProgress=%.2f bSkillActive=%d"),
		CDProgress, bSkillActive ? 1 : 0);

	// 获取 PlayerStatusWidget (持有 Image_MotherSkillIcon)
	if (!Widget_PlayerStatus)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[GameHUDWidget] OnMotherSkillCooldownChanged: Widget_PlayerStatus 未绑定!"));
		return;
	}

	// 【v121 大厂架构新增】从 CachedCharacterEvents 的 Owner 获取 bIsMother 状态
	// CachedCharacterEvents 挂在 Pawn 上, Owner 就是 Character
	bool bIsMother = false;
	if (CachedCharacterEvents && CachedCharacterEvents->GetOwner())
	{
		if (ABaseCharacter* Character = Cast<ABaseCharacter>(CachedCharacterEvents->GetOwner()))
		{
			bIsMother = Character->bIsMother;
		}
	}

	// 【v121 大厂架构】先设置显隐 (母体才显示)
	Widget_PlayerStatus->SetMotherSkillIconVisibility(bIsMother);

	// 如果是非母体, 不需要更新冷却进度 (技能图标已隐藏)
	if (!bIsMother)
	{
		return;
	}

	// 【v121.3 重构】冷却进度由 PlayerStatusWidget 内部自动计算
	Widget_PlayerStatus->UpdateMotherSkillCooldownProgress();
}

