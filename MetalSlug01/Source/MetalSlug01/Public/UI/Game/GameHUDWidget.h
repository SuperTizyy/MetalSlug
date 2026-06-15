// 版权声明：在项目设置的描述页面填写您的版权信息。

#pragma once

// ==========================================
// 头文件包含说明
// ==========================================
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
// 引入数据表类型（EACERankType、EKillMethod 等）
#include "Data/Enums/CombatEnums.h"
// 引入 ERoomMatchMode 等房间枚举
#include "Data/Enums/RoomEnums.h"
#include "GameHUDWidget.generated.h"

// 前向声明所有用到的子控件
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
 * @class UGameHUDWidget
 * @brief 游戏主 HUD 组件（战斗中的根 Widget）
 *
 * 职责说明:
 * - 聚合所有战斗子 Widget：玩家状态 / 武器面板 / 比赛信息 / 击杀信息 / 聊天
 * - 接受 Controller / Character / GameState 的事件并广播到子 Widget
 * - 控制结算覆盖板（GameOver 文本、返回大厅按钮）
 *
 * 架构理念:
 * 1. 委托驱动: 大量使用 `AddDynamic` 订阅 GameState 事件
 * 2. 重试绑定: GameState 还未生成时定时器重试 5 次
 * 3. 容器化: 所有子 Widget 都是 BindWidgetOptional，强耦合由蓝图控制
 * 4. 单一入口: 所有外部调用 (Controller、Character) 通过本类接口进入
 */
UCLASS()
class METALSLUG01_API UGameHUDWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// ==========================================
	// 1. 生命周期
	// ==========================================

	/**
	 * 初始化 UI 绑定
	 * 时机: 第一次创建 Widget 时
	 */
	virtual bool Initialize() override;

	/**
	 * Widget 构造完毕并加入视口后调用
	 * 用途: 数据表注入、订阅事件、初始化隐藏覆盖板
	 * 注意: 这里是最适合做订阅绑定的地方
	 */
	virtual void NativeConstruct() override;

	// ==========================================
	// 2. 公开接口（被其他类调用）
	// ==========================================

	/**
	 * 刷新玩家血量
	 * @param Current 当前血量
	 * @param Max 最大血量
	 */
	UFUNCTION(BlueprintCallable, Category = "GameHUD")
	void UpdateHealth(float Current, float Max);

	/**
	 * 刷新玩家血量文本（整数显示）
	 * @param Current 当前血量（int）
	 * @param Max 最大血量（int）
	 */
	UFUNCTION(BlueprintCallable, Category = "GameHUD")
	void UpdateHealthText(int32 Current, int32 Max);

	/**
	 * 刷新玩家能量
	 * @param Current 当前能量
	 * @param Max 最大能量
	 */
	UFUNCTION(BlueprintCallable, Category = "GameHUD")
	void UpdateEnergy(float Current, float Max);

	/**
	 * 刷新玩家能量文本（整数显示）
	 */
	UFUNCTION(BlueprintCallable, Category = "GameHUD")
	void UpdateEnergyText(int32 Current, int32 Max);

	/**
	 * 处理玩家击杀事件（简化：内部调用 RecordKill 管理连杀数和计时）
	 * @param bIsHeadshot 是否为爆头
	 */
	UFUNCTION(BlueprintCallable, Category = "GameHUD")
	void OnPlayerKill(bool bIsHeadshot);

	/**
	 * 更新剩余局数文本的接口
	 */
	UFUNCTION(BlueprintCallable, Category = "GameHUD")
	void UpdateRemainingRoundsText(int32 RemainingRounds);

	/**
	 * 更新队伍击杀统计文本
	 * @param AttackerKills 攻方击杀数
	 * @param DefenderKills 守方击杀数
	 */
	UFUNCTION(BlueprintCallable, Category = "GameHUD")
	void UpdateTeamKillCountsText(int32 AttackerKills, int32 DefenderKills);

	/**
	 * 更新 AC 值（Assists / 助攻）
	 */
	UFUNCTION(BlueprintCallable, Category = "GameHUD")
	void UpdateACValue(int32 Value);

	/**
	 * 更新 ACE 值（杀人数）
	 */
	UFUNCTION(BlueprintCallable, Category = "GameHUD")
	void UpdateACEValue(int32 Value);

	/**
	 * 更新 ACE 值并根据排名设置文字颜色
	 * @param Value ACE 数值
	 * @param RankType 排名类型（None / White / Gold）
	 */
	UFUNCTION(BlueprintCallable, Category = "GameHUD")
	void UpdateACEWithRank(int32 Value, EACERankType RankType);

	/**
	 * 更新角色图标
	 * @param Icon 角色头像贴图
	 */
	UFUNCTION(BlueprintCallable, Category = "GameHUD")
	void UpdateCharacterIcon(UTexture2D* Icon);

	/**
	 * 根据游戏模式切换 Text_RemainingRounds 的可见性
	 * 刀战模式隐藏，生化模式显示
	 */
	UFUNCTION(BlueprintCallable, Category = "GameHUD")
	void OnMatchModeChangedForHUD(ERoomMatchMode NewMode);

	/**
	 * 获取玩家状态 Widget（供其他类安全访问）
	 */
	UFUNCTION(BlueprintCallable, Category = "GameHUD")
	UPlayerStatusWidget* GetWidget_PlayerStatus() { return Widget_PlayerStatus; }

	/**
	 * 获取武器面板 Widget
	 */
	UFUNCTION(BlueprintCallable, Category = "GameHUD")
	UWeaponPanelWidget* GetWidget_WeaponPanel() { return Widget_WeaponPanel; }

	/**
	 * 获取计分板 Widget（供 Controller 调用）
	 */
	UFUNCTION(BlueprintCallable, Category = "GameHUD")
	UScoreboardWidget* GetWidget_Scoreboard() { return Widget_Scoreboard; }

	/**
	 * 显示计分板（Tab 按下时调用）
	 */
	UFUNCTION(BlueprintCallable, Category = "GameHUD")
	void ShowScoreboard();

	/**
	 * 隐藏计分板（Tab 抬起时调用）
	 */
	UFUNCTION(BlueprintCallable, Category = "GameHUD")
	void HideScoreboard();

	/**
	 * 显示准星
	 */
	UFUNCTION(BlueprintCallable, Category = "GameHUD")
	void ShowCrosshair();

	/**
	 * 隐藏准星
	 */
	UFUNCTION(BlueprintCallable, Category = "GameHUD")
	void HideCrosshair();

	/**
	 * 显示 ESC 菜单
	 */
	UFUNCTION(BlueprintCallable, Category = "GameHUD")
	void ShowEscMenu();

	/**
	 * 隐藏 ESC 菜单
	 */
	UFUNCTION(BlueprintCallable, Category = "GameHUD")
	void HideEscMenu();

	/**
	 * 获取 ESC 菜单 Widget（供 Controller 调用）
	 */
	UFUNCTION(BlueprintCallable, Category = "GameHUD")
	UEscMenuWidget* GetWidget_EscMenu() { return Widget_EscMenu; }

	/**
	 * 获取聊天 Widget（供外部访问）
	 */
	UFUNCTION(BlueprintCallable, Category = "GameHUD")
	UChatWidget* GetWidget_Chat() { return Widget_Chat; }

	/**
	 * 接收服务器广播的聊天消息（由 Controller 调用）
	 */
	UFUNCTION(BlueprintCallable, Category = "GameHUD")
	void AddChatMessage(const FString& PlayerName, bool bIsHost, const FString& Message);

	/**
	 * 接收服务器广播的系统消息（由 Controller 调用）
	 */
	UFUNCTION(BlueprintCallable, Category = "GameHUD")
	void AddSystemMessage(const FString& Message);

	/**
	 * 接收服务器广播的击杀消息（由 BaseCharacter 调用）
	 */
	UFUNCTION(BlueprintCallable, Category = "GameHUD")
	void AddKillFeedMessage(const FString& KillerName, const FString& VictimName, EKillMethod KillMethod);

	/**
	 * 根据武器 ID 从 DT_WeaponInfo 刷新武器面板图标
	 * @param WeaponID 武器行名
	 */
	UFUNCTION(BlueprintCallable, Category = "GameHUD")
	void UpdateWeaponIconFromID(const FString& WeaponID);

	// ==========================================
	// 3. 结算系统接口
	// ==========================================

	/**
	 * 显示当局比分面板（由 GameState 广播触发，倒计时归零时调用）
	 * 1. 暂存当局击杀数
	 * 2. 隐藏 MatchInfo / 准星 / 计分板
	 * 3. 显示游戏结束文本
	 */
	UFUNCTION(BlueprintCallable, Category = "GameHUD|Settlement")
	void OnEnterSettlement(int32 AttackerKills, int32 DefenderKills);

	/**
	 * 显示最终胜负结果（由 GameState 广播触发，延迟 3 秒后调用）
	 * 1. 隐藏游戏结束文本
	 * 2. 显示结算覆盖板
	 * 3. 显示计分板 + 当局击杀数 + 最终胜负
	 * 4. 显示返回大厅按钮
	 */
	UFUNCTION(BlueprintCallable, Category = "GameHUD|Settlement")
	void OnShowFinalSettlement(int32 AttackerWins, int32 DefenderWins);

	/**
	 * 激活聊天输入框
	 * 用途: 由 Controller 按键触发
	 */
	UFUNCTION(BlueprintCallable, Category = "Chat")
	void ActivateChatInput();

protected:
	// ==========================================
	// 4. 内部辅助
	// ==========================================

	/**
	 * 尝试绑定到 GameState（带重试逻辑，解决时序问题）
	 * 最多 5 次，每次 0.5 秒
	 */
	void TryBindToGameState();

	/**
	 * 接收 Widget_Chat 的消息并转发到服务器
	 */
	UFUNCTION()
	void OnChatMessageReadyFromWidget(const FString& PlayerName, const FString& Message);

	// ==========================================
	// 5. UI 组件绑定
	// ==========================================

	/** 玩家状态区域（血条、能量条、图标、技能等） */
	UPROPERTY(meta = (BindWidget))
	UPlayerStatusWidget* Widget_PlayerStatus;

	/** 武器面板区域 */
	UPROPERTY(meta = (BindWidget))
	UWeaponPanelWidget* Widget_WeaponPanel;

	/** 比赛信息区域（人数、倒计时、局数） */
	UPROPERTY(meta = (BindWidget))
	UMatchInfoWidget* Widget_MatchInfo;

	/** 击杀信息区域 */
	UPROPERTY(meta = (BindWidget))
	UKillFeedWidget* Widget_KillFeed;

	/** 聊天区域 */
	UPROPERTY(meta = (BindWidget))
	UChatWidget* Widget_Chat;

	/** 连杀显示区域 */
	UPROPERTY(meta = (BindWidget))
	UKillStreakWidget* Widget_KillStreak;

	/** 计分板区域（屏幕中央矩形显示） */
	UPROPERTY(meta = (BindWidget))
	UScoreboardWidget* Widget_Scoreboard;

	/** 准星（覆盖在最上层） */
	UPROPERTY(meta = (BindWidget))
	UCrosshairWidget* Widget_Crosshair;

	/** ESC 菜单面板（ESC 键呼出） */
	UPROPERTY(meta = (BindWidget))
	UEscMenuWidget* Widget_EscMenu;

	/**
	 * 击杀图标数据表引用（用于击杀信息显示）
	 * 关联: DT_KillIconInfo
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameHUD")
	class UDataTable* KillIconDataTable;

	/**
	 * 连杀图标数据表引用（用于连杀图标显示）
	 * 关联: DT_KillStreakIconInfo
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "GameHUD")
	class UDataTable* KillStreakIconDataTable;

	// ==========================================
	// 6. 结算覆盖板
	// ==========================================

	/** 结算覆盖板 Border（由 GameHUD 统一控制显示/隐藏） */
	UPROPERTY(meta = (BindWidget))
	UBorder* Border_SettlementOverlay;

	/** 游戏结束文本（倒计时结束时显示） */
	UPROPERTY(meta = (BindWidget))
	UTextBlock* Text_GameOver;

	/** 返回大厅按钮（最终结果展示后可见） */
	UPROPERTY(meta = (BindWidget))
	UButton* Button_ReturnToLobby;

	/**
	 * 返回大厅按钮点击回调
	 * 流程: PC->LeaveRoom()
	 */
	UFUNCTION()
	void OnReturnToLobbyClicked();

private:
	/**
	 * 暂存当局击杀数（由 OnEnterSettlement 传入，在 OnShowFinalSettlement 中使用）
	 */
	int32 LastAttackerKills = 0;
	int32 LastDefenderKills = 0;
};
