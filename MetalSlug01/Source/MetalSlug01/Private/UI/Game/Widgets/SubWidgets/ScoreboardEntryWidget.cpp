#include "UI/Game/Widgets/SubWidgets/ScoreboardEntryWidget.h"
#include "Components/TextBlock.h"

void UScoreboardEntryWidget::SetRank(int32 Rank)
{
	if (Text_Rank)
	{
		// 根据排名设置不同的显示格式
		FString RankText;
		switch (Rank)
		{
		case 1:
			RankText = FString::Printf(TEXT("🥇 %d"), Rank);
			Text_Rank->SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.84f, 0.0f, 1.0f))); // 金色
			break;
		case 2:
			RankText = FString::Printf(TEXT("🥈 %d"), Rank);
			Text_Rank->SetColorAndOpacity(FSlateColor(FLinearColor(0.75f, 0.75f, 0.75f, 1.0f))); // 银色
			break;
		case 3:
			RankText = FString::Printf(TEXT("🥉 %d"), Rank);
			Text_Rank->SetColorAndOpacity(FSlateColor(FLinearColor(0.8f, 0.5f, 0.2f, 1.0f))); // 铜色
			break;
		default:
			RankText = FString::Printf(TEXT("%d"), Rank);
			Text_Rank->SetColorAndOpacity(FSlateColor(FLinearColor::White));
			break;
		}
		Text_Rank->SetText(FText::FromString(RankText));
	}
}

void UScoreboardEntryWidget::SetPlayerName(const FString& InPlayerName)
{
	if (Text_PlayerName)
	{
		Text_PlayerName->SetText(FText::FromString(InPlayerName));
	}
}

void UScoreboardEntryWidget::SetScore(int32 InScore)
{
	if (Text_Score)
	{
		Text_Score->SetText(FText::FromString(FString::Printf(TEXT("%d"), InScore)));
		
		// 如果得分较高，可以设置特殊颜色（比如MVP金色）
		if (InScore >= 100)
		{
			Text_Score->SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.84f, 0.0f, 1.0f)));
		}
		else
		{
			Text_Score->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		}
	}
}

void UScoreboardEntryWidget::SetKDA(int32 Kills, int32 Deaths, int32 Assists)
{
	if (Text_KDA)
	{
		// 格式：击杀 / 死亡 / 助攻
		Text_KDA->SetText(FText::FromString(FString::Printf(TEXT("%d / %d / %d"), Kills, Deaths, Assists)));
		
		// 根据KDA比值设置颜色
		float KDRatio = Deaths > 0 ? static_cast<float>(Kills) / Deaths : static_cast<float>(Kills);
		if (KDRatio >= 2.0f)
		{
			// K/D >= 2.0 绿色
			Text_KDA->SetColorAndOpacity(FSlateColor(FLinearColor(0.0f, 1.0f, 0.0f, 1.0f)));
		}
		else if (KDRatio >= 1.0f)
		{
			// K/D >= 1.0 白色
			Text_KDA->SetColorAndOpacity(FSlateColor(FLinearColor::White));
		}
		else
		{
			// K/D < 1.0 红色
			Text_KDA->SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.3f, 0.3f, 1.0f)));
		}
	}
}

void UScoreboardEntryWidget::SetIsCurrentPlayer(bool bIsCurrentPlayer)
{
	if (bIsCurrentPlayer && Text_PlayerName)
	{
		// 当前玩家名称高亮显示
		Text_PlayerName->SetColorAndOpacity(FSlateColor(FLinearColor(0.0f, 1.0f, 1.0f, 1.0f)));
	}
	else if (Text_PlayerName)
	{
		Text_PlayerName->SetColorAndOpacity(FSlateColor(FLinearColor::White));
	}
}

FString UScoreboardEntryWidget::GetPlayerName() const
{
	if (Text_PlayerName)
	{
		return Text_PlayerName->GetText().ToString();
	}
	return FString();
}
