// 版权声明：在项目设置的描述页面填写您的版权信息。

// ==========================================
// 头文件包含区
// ==========================================
#include "UI/Game/GameHUDWidget.h"
#include "Systems/RoomPlayerController.h"
#include "Systems/RoomGameState.h"
#include "Systems/RoomGameMode.h"
#include "UI/Login/Data/StaticTable.h"
#include "UI/Game/Widgets/PlayerStatusWidget.h"
#include "UI/Game/Widgets/WeaponPanelWidget.h"
#include "UI/Game/Widgets/MatchInfoWidget.h"
#include "UI/Game/Widgets/KillFeedWidget.h"
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
