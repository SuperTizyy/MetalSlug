#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "MatchInfoWidget.generated.h"

class UTextBlock;
class UHorizontalBox;
class UImage;

/**
 * 比赛信息组件
 * 负责显示攻方人数、守方人数、单局倒计时、剩余局数、双方人数图标
 */
UCLASS()
class METALSLUG01_API UMatchInfoWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// 更新攻方人数
	UFUNCTION(BlueprintCallable, Category = "MatchInfo")
	void UpdateAttackerCount(int32 Count);

	// 更新守方人数
	UFUNCTION(BlueprintCallable, Category = "MatchInfo")
	void UpdateDefenderCount(int32 Count);

	// 更新单局倒计时
	UFUNCTION(BlueprintCallable, Category = "MatchInfo")
	void UpdateRoundCountdown(int32 Seconds);

	// 更新剩余局数
	UFUNCTION(BlueprintCallable, Category = "MatchInfo")
	void UpdateRemainingRounds(int32 Rounds);

	// 添加攻方人数图标
	UFUNCTION(BlueprintCallable, Category = "MatchInfo")
	void AddAttackerIcon(UTexture2D* Icon);

	// 添加守方人数图标
	UFUNCTION(BlueprintCallable, Category = "MatchInfo")
	void AddDefenderIcon(UTexture2D* Icon);

protected:
	virtual bool Initialize() override;

private:
	// ==========================================
	// 人数显示
	// ==========================================
	UPROPERTY(meta = (BindWidget))
	UTextBlock* Text_AttackerCount;

	UPROPERTY(meta = (BindWidget))
	UTextBlock* Text_DefenderCount;

	// ==========================================
	// 人数图标容器
	// ==========================================
	UPROPERTY(meta = (BindWidget))
	UHorizontalBox* HB_AttackerIcons;

	UPROPERTY(meta = (BindWidget))
	UHorizontalBox* HB_DefenderIcons;

	// ==========================================
	// 倒计时和局数
	// ==========================================
	UPROPERTY(meta = (BindWidget))
	UTextBlock* Text_RoundCountdown;

	UPROPERTY(meta = (BindWidget))
	UTextBlock* Text_RemainingRounds;

	// 最大图标显示数量（超出后不再添加）
	int32 MaxIconDisplayCount = 10;
};