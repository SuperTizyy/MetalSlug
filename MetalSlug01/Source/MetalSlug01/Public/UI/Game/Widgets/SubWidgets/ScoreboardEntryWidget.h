#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "ScoreboardEntryWidget.generated.h"

class UTextBlock;

/**
 * 计分板条目子控件
 * 用于显示单个玩家的计分板信息
 * 包含：排名、玩家名称、得分、击杀/死亡/助攻数据
 */
UCLASS()
class METALSLUG01_API UScoreboardEntryWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// ==========================================
	// 供外部调用的公共接口
	// ==========================================

	// 设置玩家排名
	UFUNCTION(BlueprintCallable, Category = "ScoreboardEntry")
	void SetRank(int32 Rank);

	// 设置玩家名称
	UFUNCTION(BlueprintCallable, Category = "ScoreboardEntry")
	void SetPlayerName(const FString& InPlayerName);

	// 设置得分（击杀20分/助攻10分）
	UFUNCTION(BlueprintCallable, Category = "ScoreboardEntry")
	void SetScore(int32 InScore);

	// 设置击杀/死亡/助攻数据
	UFUNCTION(BlueprintCallable, Category = "ScoreboardEntry")
	void SetKDA(int32 Kills, int32 Deaths, int32 Assists);

	// 设置是否为当前玩家（用于高亮显示）
	UFUNCTION(BlueprintCallable, Category = "ScoreboardEntry")
	void SetIsCurrentPlayer(bool bIsCurrentPlayer);

	// 获取当前玩家名称
	UFUNCTION(BlueprintCallable, Category = "ScoreboardEntry")
	FString GetPlayerName() const;

protected:
	// ==========================================
	// UI 组件绑定区域
	// ==========================================

	// 排名文本
	UPROPERTY(meta = (BindWidget))
	UTextBlock* Text_Rank;

	// 玩家名称文本
	UPROPERTY(meta = (BindWidget))
	UTextBlock* Text_PlayerName;

	// 得分文本
	UPROPERTY(meta = (BindWidget))
	UTextBlock* Text_Score;

	// 击杀/死亡/助攻文本
	UPROPERTY(meta = (BindWidget))
	UTextBlock* Text_KDA;

private:
	// 分数常量
	static constexpr int32 KillScore = 20;       // 击杀得分
	static constexpr int32 AssistScore = 10;     // 助攻得分
};
