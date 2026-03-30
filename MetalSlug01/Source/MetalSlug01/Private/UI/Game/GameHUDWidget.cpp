#include "UI/Game/GameHUDWidget.h"
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