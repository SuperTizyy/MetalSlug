#include "UI/Game/GameHUDWidget.h"
#include "Systems/RoomPlayerController.h"

#include "Systems/RoomGameState.h"
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

bool UGameHUDWidget::Initialize()
{
	if (!Super::Initialize())
	{
		return false;
	}

	return true;
}

void UGameHUDWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// 初始化击杀图标数据表
	if (Widget_KillFeed && KillIconDataTable)
	{
		Widget_KillFeed->SetKillIconDataTable(KillIconDataTable);
	}

	// 绑定聊天消息事件：当 Widget_Chat 有消息时，发送到服务器
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

	// 尝试绑定 GameState，成功则立即刷新；否则定时器重试（最多5次，每次间隔0.5秒）
	UE_LOG(LogTemp, Log, TEXT("[GameHUDWidget] NativeConstruct: Widget_MatchInfo=%s, Widget_PlayerStatus=%s"),
		*GetNameSafe(Widget_MatchInfo), *GetNameSafe(Widget_PlayerStatus));

	TryBindToGameState();
}

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

void UGameHUDWidget::TryBindToGameState()
{
	if (UWorld* World = GetWorld())
	{
		if (ARoomGameState* RoomGS = World->GetGameState<ARoomGameState>())
		{
			// 绑定事件：当 GameState 的模式切换时，UI 决定各控件的显示/隐藏
			RoomGS->OnMatchModeChanged.AddDynamic(this, &UGameHUDWidget::OnMatchModeChangedForHUD);

			// 绑定事件：当 GameState 的当前回合数变化时（生化模式），刷新 Text_RemainingRounds
			RoomGS->OnCurrentRoundUpdated.AddDynamic(this, &UGameHUDWidget::UpdateRemainingRoundsText);

			// 绑定事件：当 GameState 的队伍击杀统计变化时，刷新 MatchInfoWidget 上的 Text_AttackerCount / Text_DefenderCount
			RoomGS->OnTeamKillCountUpdated.AddDynamic(this, &UGameHUDWidget::UpdateTeamKillCountsText);

			// 绑定事件：进入结算状态（倒计时归零时触发）
			RoomGS->OnEnterSettlement.AddDynamic(this, &UGameHUDWidget::OnEnterSettlement);

			// 绑定事件：显示最终胜负（延迟3秒后触发）
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

	// GameState 还未生成，定时器重试（最多5次，每次间隔0.5秒）
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

void UGameHUDWidget::UpdateHealth(float Current, float Max)
{
	if (Widget_PlayerStatus)
	{
		Widget_PlayerStatus->UpdateHealth(Current, Max);
	}
}

void UGameHUDWidget::UpdateEnergy(float Current, float Max)
{
	if (Widget_PlayerStatus)
	{
		Widget_PlayerStatus->UpdateEnergy(Current, Max);
	}
}

void UGameHUDWidget::UpdateHealthText(int32 Current, int32 Max)
{
	if (Widget_PlayerStatus)
	{
		Widget_PlayerStatus->UpdateHealthText(Current, Max);
	}
}

void UGameHUDWidget::UpdateEnergyText(int32 Current, int32 Max)
{
	if (Widget_PlayerStatus)
	{
		Widget_PlayerStatus->UpdateEnergyText(Current, Max);
	}
}

void UGameHUDWidget::UpdateKillStreak(int32 Kills)
{
	if (Widget_KillStreak)
	{
		Widget_KillStreak->UpdateKillCount(Kills);
	}
}

void UGameHUDWidget::ShowHeadshotIcon()
{
	if (Widget_KillStreak)
	{
		Widget_KillStreak->ShowIcon(ECKillIconType::Headshot);
	}
}

void UGameHUDWidget::ShowKillIcon()
{
	if (Widget_KillStreak)
	{
		Widget_KillStreak->ShowIcon(ECKillIconType::NormalKill);
	}
}

void UGameHUDWidget::UpdateRemainingRoundsText(int32 RemainingRounds)
{
	RemainingRounds = FMath::Max(0, RemainingRounds);

	if (Widget_MatchInfo)
	{
		Widget_MatchInfo->UpdateRemainingRounds(RemainingRounds);
	}
}

void UGameHUDWidget::UpdateTeamKillCountsText(int32 AttackerKills, int32 DefenderKills)
{
	if (!Widget_MatchInfo)
	{
		UE_LOG(LogTemp, Error, TEXT("[GameHUDWidget] UpdateTeamKillCountsText: Widget_MatchInfo 为空，无法刷新队伍击杀数！"));
		return;
	}

	Widget_MatchInfo->UpdateAttackerCount(AttackerKills);
	Widget_MatchInfo->UpdateDefenderCount(DefenderKills);

	UE_LOG(LogTemp, Log, TEXT("[GameHUDWidget] 刷新队伍击杀数: 攻方=%d, 守方=%d"), AttackerKills, DefenderKills);
}

void UGameHUDWidget::OnMatchModeChangedForHUD(ERoomMatchMode NewMode)
{
	if (Widget_MatchInfo)
	{
		Widget_MatchInfo->SetVisibilityByMode(NewMode);
	}
}

void UGameHUDWidget::UpdateACValue(int32 Value)
{
	if (Widget_PlayerStatus)
	{
		Widget_PlayerStatus->UpdateACValue(Value);
	}
}

void UGameHUDWidget::UpdateACEValue(int32 Value)
{
	if (Widget_PlayerStatus)
	{
		Widget_PlayerStatus->UpdateACEValue(Value);
	}
}

void UGameHUDWidget::UpdateACEWithRank(int32 Value, EACERankType RankType)
{
	if (Widget_PlayerStatus)
	{
		Widget_PlayerStatus->SetACEValueWithRank(Value, RankType);
	}
}

void UGameHUDWidget::UpdateCharacterIcon(UTexture2D* Icon)
{
	if (Widget_PlayerStatus)
	{
		Widget_PlayerStatus->UpdateCharacterIcon(Icon);
	}
}

void UGameHUDWidget::AddChatMessage(const FString& PlayerName, bool bIsHost, const FString& Message)
{
	if (Widget_Chat)
	{
		Widget_Chat->AddChatMessage(PlayerName, Message);
	}
}

void UGameHUDWidget::AddSystemMessage(const FString& Message)
{
	if (Widget_Chat)
	{
		Widget_Chat->AddSystemMessage(Message);
	}
}

void UGameHUDWidget::AddKillFeedMessage(const FString& KillerName, const FString& VictimName, EKillMethod KillMethod)
{
	if (Widget_KillFeed)
	{
		Widget_KillFeed->AddKillInfo(KillerName, VictimName, KillMethod);
	}
}

void UGameHUDWidget::ActivateChatInput()
{
	// 通过已绑定的 Widget_Chat 成员变量激活聊天输入框
	if (Widget_Chat && !Widget_Chat->IsInputFocused())
	{
		Widget_Chat->SetInputFocused(true);
	}
}

void UGameHUDWidget::ShowScoreboard()
{
	if (Widget_Scoreboard)
	{
		Widget_Scoreboard->SetVisibility(ESlateVisibility::Visible);
		Widget_Scoreboard->RefreshScoreboard();
	}
}

void UGameHUDWidget::HideScoreboard()
{
	if (Widget_Scoreboard)
	{
		Widget_Scoreboard->SetVisibility(ESlateVisibility::Hidden);
	}
}

void UGameHUDWidget::ShowCrosshair()
{
	if (Widget_Crosshair)
	{
		Widget_Crosshair->SetVisibility(ESlateVisibility::HitTestInvisible);
	}
}

void UGameHUDWidget::HideCrosshair()
{
	if (Widget_Crosshair)
	{
		Widget_Crosshair->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void UGameHUDWidget::ShowEscMenu()
{
	// 显示ESC菜单
	if (Widget_EscMenu)
	{
		UE_LOG(LogTemp, Log, TEXT("[GameHUD] ShowEscMenu 设置 Widget_EscMenu 为 Visible"));
		Widget_EscMenu->SetVisibility(ESlateVisibility::Visible);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[GameHUD] ShowEscMenu Widget_EscMenu 为空！"));
	}
}

void UGameHUDWidget::HideEscMenu()
{
	// 隐藏ESC菜单
	if (Widget_EscMenu)
	{
		UE_LOG(LogTemp, Log, TEXT("[GameHUD] HideEscMenu 设置 Widget_EscMenu 为 Hidden"));
		Widget_EscMenu->SetVisibility(ESlateVisibility::Hidden);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[GameHUD] HideEscMenu Widget_EscMenu 为空！"));
	}
}

void UGameHUDWidget::OnEnterSettlement(int32 AttackerKills, int32 DefenderKills)
{
	UE_LOG(LogTemp, Log, TEXT("[GameHUD] OnEnterSettlement: 攻方=%d, 守方=%d"), AttackerKills, DefenderKills);

	// 隐藏 MatchInfo（倒计时归零，不再需要显示）
	if (Widget_MatchInfo)
	{
		Widget_MatchInfo->SetVisibility(ESlateVisibility::Collapsed);
	}

	// 强制显示计分板并切换到结算状态
	if (Widget_Scoreboard)
	{
		Widget_Scoreboard->SetVisibility(ESlateVisibility::Visible);
		Widget_Scoreboard->ShowRoundSettlement(AttackerKills, DefenderKills);
	}

	// 隐藏准星
	HideCrosshair();

	// 显示游戏结束文本
	if (Text_GameOver)
	{
		Text_GameOver->SetVisibility(ESlateVisibility::Visible);
	}

	// 隐藏返回大厅按钮（等最终结果展示后再显示）
	if (Button_ReturnToLobby)
	{
		Button_ReturnToLobby->SetVisibility(ESlateVisibility::Collapsed);
	}
}

void UGameHUDWidget::OnShowFinalSettlement(int32 AttackerWins, int32 DefenderWins)
{
	UE_LOG(LogTemp, Log, TEXT("[GameHUD] OnShowFinalSettlement: 攻方胜%d局, 守方胜%d局"), AttackerWins, DefenderWins);

	// 显示结算覆盖板（Border 覆盖整个屏幕）
	if (Border_SettlementOverlay)
	{
		Border_SettlementOverlay->SetVisibility(ESlateVisibility::Visible);
	}

	// 显示最终胜负结果（更新计分板上的胜负文字）
	if (Widget_Scoreboard)
	{
		Widget_Scoreboard->ShowFinalResult(AttackerWins, DefenderWins);
	}

	// 显示返回大厅按钮
	if (Button_ReturnToLobby)
	{
		Button_ReturnToLobby->SetVisibility(ESlateVisibility::Visible);
	}
}

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
