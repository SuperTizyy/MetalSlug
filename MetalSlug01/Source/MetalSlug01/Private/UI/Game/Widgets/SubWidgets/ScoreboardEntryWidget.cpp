// 版权声明：在项目设置的描述页面填写您的版权信息。

// ==========================================
// ScoreboardEntryWidget 实现 — 计分板条目子控件
// ==========================================
//
// 文件作用:
//   1. 实现 UScoreboardEntryWidget — 单条计分板信息的展示
//   2. 排名/玩家名/得分/KDA/当前玩家高亮
//   3. KDA 公式单一真理源: FKdaScoring::Compute
//
// 大厂原则 (v229.x 大厂重构):
//   - 排名显示: 严格只显示数字, 无 emoji (用户业务规则 2026.08.16)
//   - K/D/A: 缓存到字段供 GetScore 现算, 不缓存 CachedScore (避免飘移)
//   - 颜色分层: K/D ≥ 2 绿 / ≥ 1 白 / < 1 红 (视觉强化, 不影响文本)
//   - 0 兜底: TextBlock 未绑 → 静默跳过 (BindWidget 可选)
//
// KDA 公式 (业务层 = UI 层 同一真理源):
//   Score = Kills*10 + Assists*5 - Deaths
//   旧 (v22-v228) 反模式: 缓存 CachedScore = Kills*20 + Assists*10 (字段飘移 + 业务公式不一致)
//   新 (v229.x): 每次 GetScore 现算
// ==========================================
#include "UI/Game/Widgets/SubWidgets/ScoreboardEntryWidget.h"
#include "Components/TextBlock.h"

// 【v229.x 大厂架构】KDA 公式单一真理源 — 业务层 / UI 层共用
#include "Utils/KdaScoring.h"


// ==========================================
// 1. 排名
// ==========================================

/**
 * UScoreboardEntryWidget::SetRank
 *
 * 【v229.x 大厂架构】仅显示数字 (用户业务规则 2026.08.16)
 *   - 1: 数字 + 金色 / 2: 数字 + 银色 / 3: 数字 + 铜色 / 其他: 数字 + 白色
 *   - 严格只显示数字, 无任何 emoji / 符号 / 前缀
 *
 * 大厂原则 — 0 兜底:
 *   - 旧 (v22-v228): 拼接 emoji ("🥇 1" "🥈 2" "🥉 3"), 跨 UE 版本渲染不可控
 *   - 新 (v229.x): 严格只输出 %d, 颜色按排名分层, 但不影响文本
 *   - 用户规则 2026.08.16: "排名显示就数字就行, 不要特殊字符"
 */
void UScoreboardEntryWidget::SetRank(int32 Rank)
{
	if (Text_Rank)
	{
		// 【v229.x】严格只显示数字, 无任何 emoji / 字符
		Text_Rank->SetText(FText::AsNumber(Rank));

		// 颜色分层 (视觉强化, 不影响文本 — 不违反"只显示数字"规则)
		switch (Rank)
		{
		case 1:
			Text_Rank->SetColorAndOpacity(FSlateColor(FLinearColor(1.0f, 0.84f, 0.0f, 1.0f))); // 金色
			break;
		case 2:
			Text_Rank->SetColorAndOpacity(FSlateColor(FLinearColor(0.75f, 0.75f, 0.75f, 1.0f))); // 银色
			break;
		case 3:
			Text_Rank->SetColorAndOpacity(FSlateColor(FLinearColor(0.8f, 0.5f, 0.2f, 1.0f))); // 铜色
			break;
		default:
			Text_Rank->SetColorAndOpacity(FSlateColor(FLinearColor::White));
			break;
		}
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
 *
 * 大厂原则 — 业务约束:
 *   - 母体击杀/死亡直接走 ARoomPlayerState::RoomKills/Deaths (单一真理源, 不开新字段)
 *   - PerformKillSettlement 内按 Killer/Victim 身份分发到母体/非母体的 RoomKills/RoomDeaths
 *   - Text_KDA 只需要展示现有 K/D/A 三列, 不污染业务字段
 *
 * 大厂原则 — 缓存同步 (v229.x):
 *   - 同步写 CachedKills / CachedDeaths / CachedAssists — ScoreboardWidget 排序读源
 *   - 不缓存 CachedScore — 派生数据每次 GetScore() 现算 (单一真理源)
 *   - 替代旧 (v22-v228) 排序时回查 ActiveSnapshots 的 N² 重复架构
 */
void UScoreboardEntryWidget::SetKDA(int32 Kills, int32 Deaths, int32 Assists)
{
	// 【v229.x】单一真理源: 写缓存 (排序排名读源), 与 TextBlock 双写同步
	CachedKills   = Kills;
	CachedDeaths  = Deaths;
	CachedAssists = Assists;

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


/**
 * UScoreboardEntryWidget::GetScore
 *
 * 【v229.x 大厂架构】得分公式单一真理源 + 现算 (不缓存)
 *
 * 业务规则 (用户 2026.08.16 明确):
 *   - 击杀 +10 / 助攻 +5 / 死亡 -1
 *   - 公式: Score = Kills*10 + Assists*5 - Deaths
 *   - 走 FKdaScoring::Compute (业务层 = UI 层 = 同一公式)
 *
 * 大厂原则 — 单一真理源:
 *   - 旧 (v22-v228): CachedScore = Kills*20 + Assists*10 缓存
 *     → 字段飘移风险: Kills 改但 CachedScore 未更新
 *     → 业务层 20/10 与 UI 排名公式不一致 (大厂反模式)
 *   - 新 (v229.x): 每次现算
 *     → CachedKills/Deaths/Assists 写入, GetScore 走 FKdaScoring
 *     → 业务层 (10/5/-1) = UI 层 (10/5/-1) = 同一公式
 *
 * 0 兜底:
 *   - CachedKills/Deaths/Assists 默认 0 → Score = 0
 *   - 不允许"如果 Kills==0 用默认值",因为 0 击杀是合法业务状态
 */
int32 UScoreboardEntryWidget::GetScore() const
{
	return FKdaScoring::Compute(CachedKills, CachedAssists, CachedDeaths);
}
