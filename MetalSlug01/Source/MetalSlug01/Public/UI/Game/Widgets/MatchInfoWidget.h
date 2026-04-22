#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/Login/Data/StaticTable.h"
#include "MatchInfoWidget.generated.h"

class ARoomGameState;
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
	
	// 更新剩余局数
	UFUNCTION(BlueprintCallable, Category = "MatchInfo")
	void UpdateRemainingRounds(int32 Rounds);

	// 根据游戏模式设置 Text_RemainingRounds 的显示状态
	// 刀战模式（Melee）下隐藏，生化模式（Zombie）下显示
	UFUNCTION(BlueprintCallable, Category = "MatchInfo")
	void SetVisibilityByMode(ERoomMatchMode Mode);

	// 添加攻方人数图标
	UFUNCTION(BlueprintCallable, Category = "MatchInfo")
	void AddAttackerIcon(UTexture2D* Icon);

	// 添加守方人数图标
	UFUNCTION(BlueprintCallable, Category = "MatchInfo")
	void AddDefenderIcon(UTexture2D* Icon);

protected:
	virtual bool Initialize() override;
	
	// UE 标准做法：如果 UI 频繁更新状态，使用 NativeTick。如果需要优化，可改用内部 Timer
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

	// 响应双方击杀人数变化的回调
	UFUNCTION()
	void OnTeamKillCountChanged(int32 AttackerKills, int32 DefenderKills);

	// 【架构重构】：暴露设计参数供 UMG 编辑器（蓝图）配置，彻底解耦 C++ 表现
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MatchInfo|Style")
	int32 WarningTimeThreshold = 10;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MatchInfo|Style")
	FSlateColor NormalTimeColor = FSlateColor(FLinearColor::White);

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MatchInfo|Style")
	FSlateColor WarningTimeColor = FSlateColor(FLinearColor::Red);

private:
	// ==========================================
	// 对局赢局显示
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

	// 是否已成功绑定 GameState 的标志位（防止每帧重复尝试绑定或重复订阅事件）
	bool bIsBoundToGameState = false;

	// 缓存 GameState 引用以避免每帧执行 Cast 或 Get 操作，提升性能
	UPROPERTY(Transient)
	ARoomGameState* CachedGameState;

	// 用于记录上一秒的值，避免每帧都在重复更新 Text 和渲染（UI重绘开销很大）
	int32 LastRenderedSeconds = -1;
};