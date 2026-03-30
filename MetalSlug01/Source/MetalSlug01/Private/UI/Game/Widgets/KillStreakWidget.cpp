#include "UI/Game/Widgets/KillStreakWidget.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "TimerManager.h"

bool UKillStreakWidget::Initialize()
{
	if (!Super::Initialize())
	{
		return false;
	}

	// 初始化隐藏所有图标
	HideAllIcons();

	// 初始化连杀数为0
	if (Text_KillCount)
	{
		Text_KillCount->SetText(FText::AsNumber(0));
		Text_KillCount->SetVisibility(ESlateVisibility::Hidden);
	}

	return true;
}

void UKillStreakWidget::UpdateKillCount(int32 Kills)
{
	if (Text_KillCount)
	{
		Text_KillCount->SetText(FText::AsNumber(Kills));

		// 根据连杀数显示/隐藏
		if (Kills > 1)
		{
			Text_KillCount->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
			UpdateKillStreakIcon();
		}
		else
		{
			Text_KillCount->SetVisibility(ESlateVisibility::Hidden);
		}
	}
}

void UKillStreakWidget::ShowIcon(ECKillIconType IconType)
{
	// 清除之前的定时器
	GetWorld()->GetTimerManager().ClearTimer(IconDisplayTimer);

	// 隐藏所有图标
	HideAllIcons();

	// 根据类型显示对应图标
	switch (IconType)
	{
	case ECKillIconType::Headshot:
		if (Image_Headshot)
		{
			Image_Headshot->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		}
		break;

	case ECKillIconType::NormalKill:
		if (Image_Kill)
		{
			Image_Kill->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		}
		break;

	case ECKillIconType::MultiKill:
	case ECKillIconType::DoubleKill:
	case ECKillIconType::TripleKill:
	case ECKillIconType::MegaKill:
	case ECKillIconType::UltraKill:
	case ECKillIconType::MonsterKill:
		if (Image_MultiKill)
		{
			Image_MultiKill->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		}
		break;

	default:
		break;
	}

	// 设置自动隐藏定时器
	GetWorld()->GetTimerManager().SetTimer(
		IconDisplayTimer,
		this,
		&UKillStreakWidget::HideCurrentIcon,
		IconDisplayDuration,
		false
	);
}

void UKillStreakWidget::HideAllIcons()
{
	if (Image_Headshot)
	{
		Image_Headshot->SetVisibility(ESlateVisibility::Hidden);
	}

	if (Image_Kill)
	{
		Image_Kill->SetVisibility(ESlateVisibility::Hidden);
	}

	if (Image_MultiKill)
	{
		Image_MultiKill->SetVisibility(ESlateVisibility::Hidden);
	}
}

void UKillStreakWidget::HideCurrentIcon()
{
	HideAllIcons();
}

void UKillStreakWidget::UpdateKillStreakIcon()
{
	if (!Text_KillCount)
	{
		return;
	}

	// 从文本中获取当前连杀数
	int32 KillCount = FCString::Atoi(*Text_KillCount->GetText().ToString());

	// 根据连杀数显示对应图标
	if (KillCount >= 5)
	{
		ShowIcon(ECKillIconType::MonsterKill);
	}
	else if (KillCount >= 4)
	{
		ShowIcon(ECKillIconType::UltraKill);
	}
	else if (KillCount >= 3)
	{
		ShowIcon(ECKillIconType::MegaKill);
	}
	else if (KillCount >= 2)
	{
		ShowIcon(ECKillIconType::DoubleKill);
	}
}