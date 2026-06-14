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
 * - 排名 1-3 用金/银/铜色高亮
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
	 * 设置玩家排名
	 * 1: 金色 + 🥇 / 2: 银色 + 🥈 / 3: 铜色 + 🥉 / 其他: 白色 + 纯数字
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
	 * 格式: "Kills / Deaths / Assists"
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
	// 3. 常量
	// ==========================================

	/** 击杀得分 */
	static constexpr int32 KillScore = 20;

	/** 助攻得分 */
	static constexpr int32 AssistScore = 10;
};
