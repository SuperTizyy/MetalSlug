// 版权声明：在项目设置的描述页面填写您的版权信息。

#pragma once

// ==========================================
// 头文件包含说明
// ==========================================
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ScoreboardEntryWidget.generated.h"

// 前向声明
class UTextBlock;


/**
 * @class UScoreboardEntryWidget
 * @brief 计分板条目子控件
 *
 * 职责说明:
 * - 用于显示单个玩家的计分板信息
 * - 包含: 排名、玩家名称、得分、击杀/死亡/助攻数据
 * - K/D >= 2 绿色 / >= 1 白色 / < 1 红色
 * - 当前玩家用青色高亮
 *
 * 架构理念:
 * 1. 纯展示: 没有事件, 只有 Set/Get 接口
 * 2. 数据驱动: 排名/分数颜色都由数值决定
 * 3. 模块化: 由 UScoreboardWidget 动态创建和管理
 * 4. 复用: 适用于 ATTACK / DEFEND 双方
 */
UCLASS()
class METALSLUG01_API UScoreboardEntryWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// ==========================================
	// 1. 供外部调用的公共接口
	// ==========================================

	/**
	 * 设置玩家排名 — 仅显示数字 (用户业务规则 2026.08.16)
	 *
	 * 大厂原则 — 0 兜底:
	 *   - 旧 (v22-v228): 排名 1/2/3 拼接 emoji + 改颜色 (金/银/铜)
	 *     → "🥇1" "🥈2" "🥉3" 不符合 "只显示数字" 用户规则
	 *     → emoji 在某些 UE 版本渲染异常 (大厂反模式: 跨平台渲染假设)
	 *   - 新 (v229.x): 严格数字 "%d", 不拼接任何字符
	 *     → 用户规则 2026.08.16 明确: "排名显示就数字就行, 不要特殊字符"
	 *
	 * 大厂原则 — 颜色分层:
	 *   - 排名 1/2/3 仍保留颜色提示 (金/银/铜) — 但不影响 "只显示数字" 规则
	 *   - 颜色 = 视觉强化, 不是文本 — 保留不违反用户规则
	 */
	UFUNCTION(BlueprintCallable, Category = "ScoreboardEntry")
	void SetRank(int32 Rank);

	/**
	 * 设置玩家名称
	 */
	UFUNCTION(BlueprintCallable, Category = "ScoreboardEntry")
	void SetPlayerName(const FString& InPlayerName);

	/**
	 * 设置得分（>= 100 时 MVP 金色）
	 */
	UFUNCTION(BlueprintCallable, Category = "ScoreboardEntry")
	void SetScore(int32 InScore);

	/**
	 * 设置击杀/死亡/助攻数据
	 * 格式: "击杀 / 死亡 / 助攻"
	 * 颜色: K/D >= 2 绿 / >= 1 白 / < 1 红
	 */
	UFUNCTION(BlueprintCallable, Category = "ScoreboardEntry")
	void SetKDA(int32 Kills, int32 Deaths, int32 Assists);

	/**
	 * 设置是否为当前玩家（用于高亮显示）
	 * true: 青色 / false: 白色
	 */
	UFUNCTION(BlueprintCallable, Category = "ScoreboardEntry")
	void SetIsCurrentPlayer(bool bIsCurrentPlayer);

	/**
	 * 【v229.x 大厂架构】获取当前条目得分 — Scoreboard 排序排名唯一读源
	 *
	 * 大厂原则 — 单一真理源 + 公式唯一:
	 *   - 用户业务规则 (2026.08.16 明确):
	 *     "排名按照得分: 击杀 +10 / 助攻 +5 / 死亡 -1"
	 *   - 公式: Score = Kills*10 + Assists*5 - Deaths
	 *   - 本接口返回 FKdaScoring::Compute(CachedKills, CachedAssists, CachedDeaths)
	 *
	 * 大厂原则 — 不缓存:
	 *   - 旧 (v22-v228): CachedScore = Kills*20 + Assists*10 (缓存住), 业务修改后 UI 不同步
	 *     → 大厂反模式: 缓存 + 业务写入是两条独立路径, 不同步永远可能发生
	 *   - 新 (v229.x): 不缓存 CachedScore, 每次现算
	 *     → SetKDA 写入 K/D/A 缓存 (3 个字段), GetScore 走公式 = 真理源唯一
	 *     → 即使外部写入 K/D/A 之一, GetScore 立即反映, 不会有"数据漂移"
	 *
	 * 大厂原则 — 0 兜底:
	 *   - CachedKills/Deaths/Assists 默认 0 → GetScore 默认 = 0
	 *   - 不允许 "如果 CachedKills == 0 就用默认值", 因为这是业务规则 (0 击杀合法)
	 */
	UFUNCTION(BlueprintCallable, Category = "ScoreboardEntry")
	int32 GetScore() const;

	/**
	 * 获取当前玩家名称
	 */
	UFUNCTION(BlueprintCallable, Category = "ScoreboardEntry")
	FString GetPlayerName() const;

protected:
	// ==========================================
	// 2. UI 组件
	// ==========================================

	/** 排名文本 */
	UPROPERTY(meta = (BindWidget))
	UTextBlock* Text_Rank;

	/** 玩家名称文本 */
	UPROPERTY(meta = (BindWidget))
	UTextBlock* Text_PlayerName;

	/** 得分文本 */
	UPROPERTY(meta = (BindWidget))
	UTextBlock* Text_Score;

	/** 击杀/死亡/助攻文本 */
	UPROPERTY(meta = (BindWidget))
	UTextBlock* Text_KDA;

private:
	// ==========================================
	// 3. 【v229.x 大厂架构】运行时缓存 — K/D/A 三个字段 (公式输入)
	// ==========================================
	//
	// 大厂原则 — 单一真理源:
	//   - Entry 自己是 Kills / Deaths / Assists 的真理源 (SetKDA 写入, GetScore 现算)
	//   - ScoreboardWidget 排序时直接 Entry->GetScore(), 不回查 FPlayerSnapshot
	//   - 替代旧 (v22-v228) SortEntriesByScore 内 N² 的 ActiveSnapshots 线性查表
	//
	// 大厂原则 — 不缓存派生数据:
	//   - CachedScore 不存在, 因为 Score 是 K/D/A 派生
	//   - 派生数据 = 现算, 不缓存 (否则会出现"字段更新但派生值未更新"的飘移)

	/** 当前条目击杀数缓存 (SetKDA 写入, GetScore 现算) */
	UPROPERTY(VisibleAnywhere, Category = "ScoreboardEntry|Cache")
	int32 CachedKills = 0;

	/** 当前条目死亡数缓存 (SetKDA 写入, GetScore 现算) */
	UPROPERTY(VisibleAnywhere, Category = "ScoreboardEntry|Cache")
	int32 CachedDeaths = 0;

	/** 当前条目助攻数缓存 (SetKDA 写入, GetScore 现算) */
	UPROPERTY(VisibleAnywhere, Category = "ScoreboardEntry|Cache")
	int32 CachedAssists = 0;
};