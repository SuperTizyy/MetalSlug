#include "UI/Game/Widgets/MatchInfoWidget.h"
#include "Components/TextBlock.h"
#include "Components/HorizontalBox.h"
#include "Components/Image.h"

bool UMatchInfoWidget::Initialize()
{
	if (!Super::Initialize())
	{
		return false;
	}

	// 初始化默认值
	if (Text_AttackerCount)
	{
		Text_AttackerCount->SetText(FText::AsNumber(0));
	}

	if (Text_DefenderCount)
	{
		Text_DefenderCount->SetText(FText::AsNumber(0));
	}

	if (Text_RoundCountdown)
	{
		Text_RoundCountdown->SetText(FText::AsNumber(0));
	}

	if (Text_RemainingRounds)
	{
		Text_RemainingRounds->SetText(FText::AsNumber(0));
	}

	return true;
}

void UMatchInfoWidget::UpdateAttackerCount(int32 Count)
{
	if (Text_AttackerCount)
	{
		Text_AttackerCount->SetText(FText::AsNumber(Count));
	}
}

void UMatchInfoWidget::UpdateDefenderCount(int32 Count)
{
	if (Text_DefenderCount)
	{
		Text_DefenderCount->SetText(FText::AsNumber(Count));
	}
}

void UMatchInfoWidget::UpdateRoundCountdown(int32 Seconds)
{
	if (Text_RoundCountdown)
	{
		// 格式化为 MM:SS
		int32 Minutes = Seconds / 60;
		int32 Secs = Seconds % 60;
		FString TimeString = FString::Printf(TEXT("%02d:%02d"), Minutes, Secs);
		Text_RoundCountdown->SetText(FText::FromString(TimeString));

		// 低于10秒显示红色警告
		if (Seconds <= 10)
		{
			Text_RoundCountdown->SetColorAndOpacity(FLinearColor::Red);
		}
		else
		{
			Text_RoundCountdown->SetColorAndOpacity(FLinearColor::White);
		}
	}
}

void UMatchInfoWidget::UpdateRemainingRounds(int32 Rounds)
{
	if (Text_RemainingRounds)
	{
		Text_RemainingRounds->SetText(FText::Format(
			NSLOCTEXT("MatchInfo", "RemainingRoundsFormat", "Rounds: {0}"),
			FText::AsNumber(Rounds)
		));
	}
}

void UMatchInfoWidget::AddAttackerIcon(UTexture2D* Icon)
{
	if (!HB_AttackerIcons || !Icon)
	{
		return;
	}

	// 检查是否超过最大显示数量
	if (HB_AttackerIcons->GetChildrenCount() >= MaxIconDisplayCount)
	{
		return;
	}

	// 创建图标控件
	UImage* NewIcon = NewObject<UImage>(HB_AttackerIcons);
	if (NewIcon)
	{
		NewIcon->SetBrushFromTexture(Icon);
		NewIcon->SetDesiredSizeOverride(FVector2D(20.0f, 20.0f));
		NewIcon->SetBrushSize(FVector2D(20.0f, 20.0f));
		HB_AttackerIcons->AddChild(NewIcon);
	}
}

void UMatchInfoWidget::AddDefenderIcon(UTexture2D* Icon)
{
	if (!HB_DefenderIcons || !Icon)
	{
		return;
	}

	// 检查是否超过最大显示数量
	if (HB_DefenderIcons->GetChildrenCount() >= MaxIconDisplayCount)
	{
		return;
	}

	// 创建图标控件
	UImage* NewIcon = NewObject<UImage>(HB_DefenderIcons);
	if (NewIcon)
	{
		NewIcon->SetBrushFromTexture(Icon);
		NewIcon->SetDesiredSizeOverride(FVector2D(20.0f, 20.0f));
		NewIcon->SetBrushSize(FVector2D(20.0f, 20.0f));
		HB_DefenderIcons->AddChild(NewIcon);
	}
}