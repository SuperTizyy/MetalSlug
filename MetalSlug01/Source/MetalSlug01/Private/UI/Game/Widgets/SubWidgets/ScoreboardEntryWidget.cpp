// 版权声明：在项目设置的描述页面填写您的版权信息。

// ==========================================
// 头文件包含区
// ==========================================
#include "UI/Game/Widgets/SubWidgets/ScoreboardEntryWidget.h"
#include "Components/TextBlock.h"


// ==========================================
// 1. 排名
// ==========================================

/**
 * UScoreboardEntryWidget::SetRank
 *
 * 1: 🥇 + 金色 / 2: 🥈 + 银色 / 3: 🥉 + 铜色 / 其他: 纯数字 + 白色
 * 注意: emoji 在某些 UE 版本中可能不显示, 颜色方案更可靠
 */
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


// ==========================================
// 2. 玩家名 / 得分
// ==========================================

/**
 * UScoreboardEntryWidget::SetPlayerName
 *
 * 设置玩家名称（默认白, 可由 SetIsCurrentPlayer 覆盖为青色）
 */
void UScoreboardEntryWidget::SetPlayerName(const FString& InPlayerName)
{
	if (Text_PlayerName)
	{
		Text_PlayerName->SetText(FText::FromString(InPlayerName));
	}
}


/**
 * UScoreboardEntryWidget::SetScore
 *
 * 设置得分
 * >= 100: MVP 金色 / 否则: 白色
 */
void UScoreboardEntryWidget::SetScore(int32 InScore)
{
	if (Text_Score)
	{
		Text_Score->SetText(FText::FromString(FString::Printf(TEXT("%d"), InScore)));

		// 如果得分较高，可以设置特殊颜色（比如 MVP 金色）
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


// ==========================================
// 3. KDA
// ==========================================

/**
 * UScoreboardEntryWidget::SetKDA
 *
 * 格式: "Kills / Deaths / Assists"
 * 颜色规则:
 *   K/D >= 2.0  绿色 (Carry 表现)
 *   K/D >= 1.0  白色 (持平)
 *   K/D <  1.0  红色 (需要努力)
 * 防御性: Deaths=0 时 KDRatio 直接用 Kills
 */
void UScoreboardEntryWidget::SetKDA(int32 Kills, int32 Deaths, int32 Assists)
{
	if (Text_KDA)
	{
		// 格式: 击杀 / 死亡 / 助攻
		Text_KDA->SetText(FText::FromString(FString::Printf(TEXT("%d / %d / %d"), Kills, Deaths, Assists)));

		// 根据 KDA 比值设置颜色
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


// ==========================================
// 4. 当前玩家高亮
// ==========================================

/**
 * UScoreboardEntryWidget::SetIsCurrentPlayer
 *
 * true: 玩家名青色高亮
 * false: 玩家名恢复白色
 */
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


/**
 * UScoreboardEntryWidget::GetPlayerName
 *
 * 从 Text_PlayerName 读回玩家名
 * 用途: 由 UScoreboardWidget 用于匹配/排序
 */
FString UScoreboardEntryWidget::GetPlayerName() const
{
	if (Text_PlayerName)
	{
		return Text_PlayerName->GetText().ToString();
	}
	return FString();
}
