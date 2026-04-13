#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "GameHUDWidget.generated.h"

class UWidgetSwitcher;
class UPlayerStatusWidget;
class UWeaponPanelWidget;
class UMatchInfoWidget;
class UKillFeedWidget;
class UChatWidget;
class UKillStreakWidget;

/**
 * 游戏主HUD组件
 * 负责显示玩家状态、武器信息、比赛信息、击杀信息和聊天
 */
UCLASS()
class METALSLUG01_API UGameHUDWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// 初始化UI绑定
	virtual bool Initialize() override;

	// 公开接口：刷新玩家血量
	UFUNCTION(BlueprintCallable, Category = "GameHUD")
	void UpdateHealth(float Current, float Max);

	// 公开接口：刷新玩家血量文本
	UFUNCTION(BlueprintCallable, Category = "GameHUD")
	void UpdateHealthText(int32 Current, int32 Max);

	// 公开接口：刷新玩家能量
	UFUNCTION(BlueprintCallable, Category = "GameHUD")
	void UpdateEnergy(float Current, float Max);

	// 公开接口：刷新玩家能量文本
	UFUNCTION(BlueprintCallable, Category = "GameHUD")
	void UpdateEnergyText(int32 Current, int32 Max);

	// 公开接口：更新连杀数
	UFUNCTION(BlueprintCallable, Category = "GameHUD")
	void UpdateKillStreak(int32 Kills);

	// 公开接口：显示爆头击杀图标
	UFUNCTION(BlueprintCallable, Category = "GameHUD")
	void ShowHeadshotIcon();

	// 公开接口：显示普通击杀图标
	UFUNCTION(BlueprintCallable, Category = "GameHUD")
	void ShowKillIcon();

protected:
	// ==========================================
	// UI组件绑定
	// ==========================================

	// 玩家状态区域（血条、能量条、图标、技能等）
	UPROPERTY(meta = (BindWidget))
	UPlayerStatusWidget* Widget_PlayerStatus;

	// 武器面板区域
	UPROPERTY(meta = (BindWidget))
	UWeaponPanelWidget* Widget_WeaponPanel;

	// 比赛信息区域（人数、倒计时、局数）
	UPROPERTY(meta = (BindWidget))
	UMatchInfoWidget* Widget_MatchInfo;

	// 击杀信息区域
	UPROPERTY(meta = (BindWidget))
	UKillFeedWidget* Widget_KillFeed;

	// 聊天区域
	UPROPERTY(meta = (BindWidget))
	UChatWidget* Widget_Chat;

	// 连杀显示区域
	UPROPERTY(meta = (BindWidget))
	UKillStreakWidget* Widget_KillStreak;
};