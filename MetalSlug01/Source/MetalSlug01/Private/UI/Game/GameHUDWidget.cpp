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
#include "Components/Border.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"


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

	// 初始化结算覆盖板为隐藏状态
	if (Border_SettlementOverlay)
	{
		Border_SettlementOverlay->SetVisibility(ESlateVisibility::Collapsed);
	}

	// 初始化游戏结束文本为隐藏状态
	if (Text_GameOver)
	{
		Text_GameOver->SetVisibility(ESlateVisibility::Collapsed);
	}

	// 绑定返回大厅按钮
	if (Button_ReturnToLobby)
	{
		Button_ReturnToLobby->OnClicked.AddDynamic(this, &UGameHUDWidget::OnReturnToLobbyClicked);
		Button_ReturnToLobby->SetVisibility(ESlateVisibility::Collapsed);
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
 * 2. 当前回合数 -> UpdateRemainingRoundsText
 * 3. 队伍击杀数 -> UpdateTeamKillCountsText
 * 4. 进入结算 -> OnEnterSettlement
 * 5. 显示最终胜负 -> OnShowFinalSettlement
 * 6. 立即同步一次
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

			// 绑定事件: 当 GameState 的当前回合数变化时（生化模式），刷新 Text_RemainingRounds
			RoomGS->OnCurrentRoundUpdated.AddDynamic(this, &UGameHUDWidget::UpdateRemainingRoundsText);

			// 绑定事件: 当 GameState 的队伍击杀统计变化时，刷新 MatchInfoWidget 上的 Text_AttackerCount / Text_DefenderCount
			RoomGS->OnTeamKillCountUpdated.AddDynamic(this, &UGameHUDWidget::UpdateTeamKillCountsText);

			// 绑定事件: 进入结算状态（倒计时归零时触发）
			RoomGS->OnEnterSettlement.AddDynamic(this, &UGameHUDWidget::OnEnterSettlement);

			// 绑定事件: 显示最终胜负（延迟 3 秒后触发）
			RoomGS->OnShowFinalSettlement.AddDynamic(this, &UGameHUDWidget::OnShowFinalSettlement);

			// 初始化时先刷一次（剩余回合数）
			UpdateRemainingRoundsText(RoomGS->CurrentRound);

			// 初始化时同步当前已存在的队伍击杀数据
			UpdateTeamKillCountsText(RoomGS->AttackerTotalKills, RoomGS->DefenderTotalKills);

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

/** 更新剩余局数文本 */
void UGameHUDWidget::UpdateRemainingRoundsText(int32 RemainingRounds)
{
	RemainingRounds = FMath::Max(0, RemainingRounds);

	if (Widget_MatchInfo)
	{
		Widget_MatchInfo->UpdateRemainingRounds(RemainingRounds);
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

	UE_LOG(LogTemp, Log, TEXT("[GameHUDWidget] 刷新队伍击杀数: 攻方=%d, 守方=%d"), AttackerKills, DefenderKills);
}

/** 根据游戏模式切换 Text_RemainingRounds 的可见性 */
void UGameHUDWidget::OnMatchModeChangedForHUD(ERoomMatchMode NewMode)
{
	if (Widget_MatchInfo)
	{
		Widget_MatchInfo->SetVisibilityByMode(NewMode);
	}
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
	UE_LOG(LogTemp, Log, TEXT("[GameHUDWidget] ShowScoreboard: Widget_Scoreboard=%s"), *GetNameSafe(Widget_Scoreboard));
	if (Widget_Scoreboard)
	{
		Widget_Scoreboard->SetVisibility(ESlateVisibility::Visible);
		Widget_Scoreboard->RefreshScoreboard();
	}
}

/** 隐藏计分板 */
void UGameHUDWidget::HideScoreboard()
{
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
// 8. 结算系统
// ==========================================

/**
 * UGameHUDWidget::OnEnterSettlement
 *
 * 进入结算状态（倒计时归零时由 GameState 广播触发）
 * 1. 暂存当局击杀数
 * 2. 隐藏 MatchInfo
 * 3. 隐藏准星
 * 4. 显示游戏结束文本（3 秒后由 OnShowFinalSettlement 隐藏）
 * 5. 隐藏返回大厅按钮
 * 6. 隐藏计分板
 */
void UGameHUDWidget::OnEnterSettlement(int32 AttackerKills, int32 DefenderKills)
{
	UE_LOG(LogTemp, Log, TEXT("[GameHUD] OnEnterSettlement: 攻方=%d, 守方=%d"), AttackerKills, DefenderKills);

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

	// 显示游戏结束文本（3 秒后由 OnShowFinalSettlement 隐藏）
	if (Text_GameOver)
	{
		Text_GameOver->SetVisibility(ESlateVisibility::Visible);
	}

	// 隐藏返回大厅按钮（等最终结果展示后再显示）
	if (Button_ReturnToLobby)
	{
		Button_ReturnToLobby->SetVisibility(ESlateVisibility::Collapsed);
	}

	// 隐藏计分板（3 秒结算动画期间不显示，等 OnShowFinalSettlement 再展示）
	if (Widget_Scoreboard)
	{
		Widget_Scoreboard->SetVisibility(ESlateVisibility::Collapsed);
	}
}


/**
 * UGameHUDWidget::OnShowFinalSettlement
 *
 * 显示最终胜负结果（延迟 3 秒后由 GameState 广播触发）
 * 1. 隐藏游戏结束文本
 * 2. 显示结算覆盖板
 * 3. 显示计分板 + 当局击杀数 + 最终胜负
 * 4. 显示返回大厅按钮
 */
void UGameHUDWidget::OnShowFinalSettlement(int32 AttackerWins, int32 DefenderWills)
{
	UE_LOG(LogTemp, Log, TEXT("[GameHUD] OnShowFinalSettlement: 攻方胜%d局, 守方胜%d局"), AttackerWins, DefenderWills);

	// 隐藏游戏结束文本（3 秒显示时间已到）
	if (Text_GameOver)
	{
		Text_GameOver->SetVisibility(ESlateVisibility::Collapsed);
	}

	// 显示结算覆盖板（Border 覆盖整个屏幕）
	if (Border_SettlementOverlay)
	{
		Border_SettlementOverlay->SetVisibility(ESlateVisibility::Visible);
	}

	// 显示计分板 + 当局击杀数 + 最终胜负
	if (Widget_Scoreboard)
	{
		Widget_Scoreboard->SetVisibility(ESlateVisibility::Visible);
		Widget_Scoreboard->ShowRoundSettlement(LastAttackerKills, LastDefenderKills);
		Widget_Scoreboard->ShowFinalResult(AttackerWins, DefenderWills);
	}

	// 显示返回大厅按钮
	if (Button_ReturnToLobby)
	{
		Button_ReturnToLobby->SetVisibility(ESlateVisibility::Visible);
	}
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
 * UGameHUDWidget::OnReturnToLobbyClicked
 *
 * 返回大厅按钮点击
 * 流程: PC->LeaveRoom()
 */
void UGameHUDWidget::OnReturnToLobbyClicked()
{
	if (APlayerController* PC = GetOwningPlayer())
	{
		if (ARoomPlayerController* RoomPC = Cast<ARoomPlayerController>(PC))
		{
			RoomPC->LeaveRoom();
		}
	}
}


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

		// 【2026.07.14 新增】订阅无敌期状态变化 - 控制复活进度条显示/隐藏
		Events->OnInvincibilityChanged.AddDynamic(this, &UGameHUDWidget::OnInvincibilityChanged);

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

		// 2. AC 快照补发 (主动拉取, 避免 OnRep_ACValue 比订阅晚触发)
		{
			const int32 CachedAC = Events->GetCachedACValue();
			if (CachedAC >= 0 && Widget_PlayerStatus)
			{
				UE_LOG(LogTemp, Verbose, TEXT("[GameHUDWidget][Bind-Snapshot] 补发 AC=%d"), CachedAC);
				Widget_PlayerStatus->UpdateACValue(CachedAC);
			}
		}

		// 【v40.7 新增】无敌期状态快照补发
		// 根因: 玩家复活时 ActivateSpawnInvincibility 先于 HUD 订阅触发
		// → OnInvincibilityChanged(true) 事件丢失 → Show() 从未被调用 → 进度条不显示
		// 修复: HUD 订阅成功后主动检查当前 invincibility 状态，如已激活则立即 Show
		{
			if (UHealthComponent* HC = Character->ResolveHealthComponent())
			{
				if (HC->IsInvincible() && Widget_RespawnProgress)
				{
					UE_LOG(LogTemp, Log,
						TEXT("[GameHUDWidget][Bind-Snapshot] 检测到当前处于无敌期，强制显示复活进度条. Pawn=%s"),
						*Character->GetName());
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
		CachedCharacterEvents->OnInvincibilityChanged.RemoveDynamic(this, &UGameHUDWidget::OnInvincibilityChanged);

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
