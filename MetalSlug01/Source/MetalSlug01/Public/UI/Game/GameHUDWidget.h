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
class UScoreboardWidget;
class UCrosshairWidget;
class UEscMenuWidget;
class UBorder;
class UButton;
class UTextBlock;

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

	// 更新队伍击杀统计文本的接口（Text_AttackerCount / Text_DefenderCount）
	UFUNCTION(BlueprintCallable, Category = "GameHUD")
	void UpdateTeamKillCountsText(int32 AttackerKills, int32 DefenderKills);

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

	// 获取计分板Widget（供Controller调用）
	UFUNCTION(BlueprintCallable, Category = "GameHUD")
	UScoreboardWidget* GetWidget_Scoreboard() { return Widget_Scoreboard; }

	// 显示计分板（Tab按下时调用）
	UFUNCTION(BlueprintCallable, Category = "GameHUD")
	void ShowScoreboard();

	// 隐藏计分板（Tab抬起时调用）
	UFUNCTION(BlueprintCallable, Category = "GameHUD")
	void HideScoreboard();

	// 显示准星
	UFUNCTION(BlueprintCallable, Category = "GameHUD")
	void ShowCrosshair();

	// 隐藏准星
	UFUNCTION(BlueprintCallable, Category = "GameHUD")
	void HideCrosshair();

	// 显示ESC菜单
	UFUNCTION(BlueprintCallable, Category = "GameHUD")
	void ShowEscMenu();

	// 隐藏ESC菜单
	UFUNCTION(BlueprintCallable, Category = "GameHUD")
	void HideEscMenu();

	// 获取ESC菜单Widget（供Controller调用）
	UFUNCTION(BlueprintCallable, Category = "GameHUD")
	UEscMenuWidget* GetWidget_EscMenu() { return Widget_EscMenu; }

	// 获取聊天Widget（供外部访问）
	UFUNCTION(BlueprintCallable, Category = "GameHUD")
	UChatWidget* GetWidget_Chat() { return Widget_Chat; }

	// 接收服务器广播的聊天消息（由 Controller 调用）
	UFUNCTION(BlueprintCallable, Category = "GameHUD")
	void AddChatMessage(const FString& PlayerName, bool bIsHost, const FString& Message);

	// 接收服务器广播的系统消息（由 Controller 调用）
	UFUNCTION(BlueprintCallable, Category = "GameHUD")
	void AddSystemMessage(const FString& Message);
	
	// 接收服务器广播的击杀消息（由 BaseCharacter 调用）
	UFUNCTION(BlueprintCallable, Category = "GameHUD")
	void AddKillFeedMessage(const FString& KillerName, const FString& VictimName, EKillMethod KillMethod);

	// ==========================================
	// 结算系统接口
	// ==========================================

	// 显示当局比分面板（由 GameState 广播触发）
	UFUNCTION(BlueprintCallable, Category = "GameHUD|Settlement")
	void OnEnterSettlement(int32 AttackerKills, int32 DefenderKills);

	// 显示最终胜负结果（由 GameState 广播触发，延迟3秒）
	UFUNCTION(BlueprintCallable, Category = "GameHUD|Settlement")
	void OnShowFinalSettlement(int32 AttackerWins, int32 DefenderWins);

	UFUNCTION(BlueprintCallable, Category = "Chat")
	void ActivateChatInput();

protected:
	// 尝试绑定到 GameState（带重试逻辑，解决时序问题）
	void TryBindToGameState();

	// 接收 Widget_Chat 的消息并转发到服务器
	UFUNCTION()
	void OnChatMessageReadyFromWidget(const FString& PlayerName, const FString& Message);

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

	// 计分板区域（屏幕中央矩形显示）
	UPROPERTY(meta = (BindWidget))
	UScoreboardWidget* Widget_Scoreboard;

	// 准星（覆盖在最上层）
	UPROPERTY(meta = (BindWidget))
	UCrosshairWidget* Widget_Crosshair;

	// ESC菜单面板（ESC键呼出）
	UPROPERTY(meta = (BindWidget))
	UEscMenuWidget* Widget_EscMenu;

	// 击杀图标数据表引用
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameHUD")
	class UDataTable* KillIconDataTable;

	// ==========================================
	// 结算覆盖板（由 GameHUD 统一控制显示/隐藏）
	// ==========================================
	UPROPERTY(meta = (BindWidget))
	UBorder* Border_SettlementOverlay;

	// 结算覆盖层 - 游戏结束文本（倒计时结束时显示）
	UPROPERTY(meta = (BindWidget))
	UTextBlock* Text_GameOver;

	// 结算覆盖层 - 返回大厅按钮（最终结果展示后可见）
	UPROPERTY(meta = (BindWidget))
	UButton* Button_ReturnToLobby;

	// 返回大厅按钮点击回调
	UFUNCTION()
	void OnReturnToLobbyClicked();
};
