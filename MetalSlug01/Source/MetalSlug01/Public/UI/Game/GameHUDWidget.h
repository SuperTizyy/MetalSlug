#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "UI/Login/Data/StaticTable.h"
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

	// Widget 构造完毕并加入视口后调用，最适合做订阅绑定
	virtual void NativeConstruct() override;

	// ==========================================
	// 公开接口（可被其他类调用）
	// ==========================================

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
	
	// 更新剩余局数文本的接口
	UFUNCTION(BlueprintCallable, Category = "GameHUD")
	void UpdateRemainingRoundsText(int32 RemainingRounds);

	// 更新AC值
	UFUNCTION(BlueprintCallable, Category = "GameHUD")
	void UpdateACValue(int32 Value);

	// 更新ACE值
	UFUNCTION(BlueprintCallable, Category = "GameHUD")
	void UpdateACEValue(int32 Value);

	// 更新ACE值并根据排名设置文字颜色（白=队内第一，金=全场第一）
	UFUNCTION(BlueprintCallable, Category = "GameHUD")
	void UpdateACEWithRank(int32 Value, EACERankType RankType);

	// 更新角色图标
	UFUNCTION(BlueprintCallable, Category = "GameHUD")
	void UpdateCharacterIcon(UTexture2D* Icon);

	// 根据游戏模式切换 Text_RemainingRounds 的可见性
	UFUNCTION(BlueprintCallable, Category = "GameHUD")
	void OnMatchModeChangedForHUD(ERoomMatchMode NewMode);

	// 获取玩家状态Widget（供其他类安全访问）
	UFUNCTION(BlueprintCallable, Category = "GameHUD")
	UPlayerStatusWidget* GetWidget_PlayerStatus() { return Widget_PlayerStatus; }

protected:
	// 尝试绑定到 GameState（带重试逻辑，解决时序问题）
	void TryBindToGameState();

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
