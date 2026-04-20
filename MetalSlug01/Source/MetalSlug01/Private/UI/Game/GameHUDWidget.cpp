#include "UI/Game/GameHUDWidget.h"

#include "Systems/RoomGameState.h"
#include "UI/Game/Widgets/PlayerStatusWidget.h"
#include "UI/Game/Widgets/WeaponPanelWidget.h"
#include "UI/Game/Widgets/MatchInfoWidget.h"
#include "UI/Game/Widgets/KillFeedWidget.h"
#include "UI/Game/Widgets/ChatWidget.h"
#include "UI/Game/Widgets/KillStreakWidget.h"

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

	// 尝试绑定 GameState，成功则立即刷新；否则定时器重试（最多5次，每次间隔0.5秒）
	TryBindToGameState();
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

			// 初始化时先刷一次
			UpdateRemainingRoundsText(RoomGS->CurrentRound);

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
