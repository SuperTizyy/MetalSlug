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
#include "Components/CharacterEvents.h"
#include "UI/Game/Widgets/RespawnProgressWidget.h"
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

	/**
	 * Widget 从视口移除时调用
	 * 用途: 清理 CharacterEvents 订阅, 防止 Widget 销毁后回调残留
	 */
	virtual void NativeDestruct() override;

	/**
	 * 【2026-07-01 P0 新增】Tick 兜底:
	 *   若 TryBindToCharacterEvents 重试 10 次仍失败 (例如服务器 Possess 慢, 或 Pawn 后续才生成),
	 *   原架构会永久丢失订阅 → 头像永远不显示。
	 *   NativeTick 每帧检查: 若已订阅但 Pawn 已变, 重新订阅; 若未订阅且 Pawn 可用, 重置重试并重新尝试。
	 *   频率: 每秒 1 次 (节流), 避免每帧 Tick 的性能浪费。
	 */
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;

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
	 * 【v92 大厂架构重构】更新总局数文本 (替换 UpdateRemainingRoundsText)
	 *
	 * 大厂原则 — 单一真理源:
	 *   - 数据源: GameState.TotalRounds (Replicated, 由 GameMode.TotalRounds 注入)
	 *   - 调用方: GameState.OnTotalRoundsUpdated 委托回调
	 *   - 不接受"剩余局数"参数 (那是内部计数 CurrentRound, 不应该传到 UI)
	 *
	 * @param TotalRounds 总回合数 (来自 GameState.TotalRounds)
	 */
	UFUNCTION(BlueprintCallable, Category = "GameHUD")
	void UpdateTotalRoundsText(int32 TotalRounds);

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
	 * 【v92 大厂架构新增】母体变异倒计时同步回调
	 *
	 * 单一真理源 — 由 GameState.OnMotherMutationChanged OnRep 触发
	 * 职责: 转发给 Widget_MatchInfo, 由 Widget 内部决定显示/隐藏 + NativeTick 刷新数字
	 *
	 * 大厂原则 — 转发壳模式 (镜像 OnMatchModeChangedForHUD):
	 *   - GameHUDWidget 不持有倒计时逻辑, 只做事件路由
	 *   - Widget_MatchInfo 是唯一显示组件
	 *
	 * @param StartTime 服务器写入的开始时间戳 (<= 0 表示关闭)
	 * @param Duration 倒计时总秒数 (<= 0 表示关闭)
	 */
	UFUNCTION(BlueprintCallable, Category = "GameHUD")
	void OnMotherMutationChanged(float StartTime, float Duration);

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
	 * 【v105 大厂架构】直接控制武器面板显隐
	 *
	 * 根因: 旧版走 CharacterEvents::OnWeaponPanelVisibilityChanged 事件总线,
	 *       但 HUD 端订阅时序不对时丢失事件,导致弹药 UI 在母体时仍显示
	 * 修复: CharacterIconComponent::Client_RefreshCharacterIcon RPC 携带 bIsMother 参数,
	 *       客户端收到后直接调本函数控制 Widget_WeaponPanel Visibility
	 *
	 * @param bIsVisible true=显示武器面板, false=隐藏武器面板
	 */
	UFUNCTION(BlueprintCallable, Category = "GameHUD")
	void SetWeaponPanelVisible(bool bIsVisible);

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

	// ==========================================
	// v60.11 大厂架构 — Crosshair 服务
	// ==========================================

	/**
	 * GetWidget_Crosshair — 暴露 Crosshair Widget 引用 (BlueprintCallable)
	 *
	 * 大厂原则 — 公开 API 而非穿透 protected 字段:
	 *   - Strategy 层 (URangedLineStrategy) 需要读 Crosshair 屏幕坐标算射线
	 *   - 不能穿透 protected 字段, 必须提供公开 getter
	 *
	 * @return CrosshairWidget 指针 (永远 non-null, BindWidget 保证存在; 若 BP 没绑则编译期失败)
	 */
	UFUNCTION(BlueprintCallable, Category = "GameHUD|Crosshair")
	UCrosshairWidget* GetWidget_Crosshair() const { return Widget_Crosshair; }

	/**
	 * GetCrosshairWorldRay — 把准星屏幕坐标转换成"过准星的世界射线"
	 *
	 * 大厂原则 — UI 层职责对等 (v60.11 新增):
	 *   - "准星 → 世界射线" 是 UI 层的专业职责, 不应由 Strategy 算
	 *   - Strategy 只读"射线起点 + 方向", 不关心屏幕坐标怎么转
	 *   - 转换需要 PlayerController (Strategy 不该穿透 PC)
	 *
	 * 流程:
	 *   1. 拿准星 Widget 的中心屏幕坐标 (左上角原点, 像素)
	 *   2. 拿 PlayerController → DeprojectScreenPositionToWorld
	 *   3. 输出 WorldOrigin (相机平面上的点) + WorldDirection (相机射线方向)
	 *
	 * @param OutWorldOrigin  输出: 相机平面上的过准星点 (cm)
	 * @param OutWorldDirection 输出: 相机射线方向 (单位向量)
	 * @return true=成功, false=配置错 (Crosshair 未渲染 / PC 为空 / Deproject 失败)
	 *
	 * @note 调用方必须显式校验返回值, false 时拒绝后续逻辑 (零兜底)
	 */
	UFUNCTION(BlueprintCallable, Category = "GameHUD|Crosshair")
	bool GetCrosshairWorldRay(FVector& OutWorldOrigin, FVector& OutWorldDirection) const;

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

	/** 复活进度条（显示无敌时间进度）【2026.07.14 新增】 */
	UPROPERTY(meta = (BindWidget))
	URespawnProgressWidget* Widget_RespawnProgress;

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

	// ==========================================
	// 7. CharacterEvents 订阅 (2026-07-01 新增 - 依赖倒置核心)
	// ==========================================

	/**
	 * 【2026-07-01 新增】尝试订阅 CharacterEvents 组件
	 * 流程: NativeConstruct → TryBindToGameState → TryBindToCharacterEvents
	 *       通过 PlayerController -> Pawn -> CharacterEvents 链获取组件
	 * 失败: 延迟重试 (TimerHandle: CharacterEventsRetryTimerHandle, 最多 10 次)
	 */
	void TryBindToCharacterEvents();

	/** 清理 CharacterEvents 订阅 (在 Character 切换 Pawn 时调用) */
	void UnbindFromCharacterEvents();

	/** 头像加载完毕回调 */
	UFUNCTION()
	void OnCharacterIconReady(const FString& CharacterID, class UTexture2D* Icon);

	/** 血量变化回调 */
	UFUNCTION()
	void OnHealthChanged(float Current, float Max);

	/** 能量变化回调 */
	UFUNCTION()
	void OnEnergyChanged(float Current, float Max);

	/** AC 值变化回调 */
	UFUNCTION()
	void OnACValueChanged(int32 NewAC);

	/** ACE 值变化回调 (无排名) */
	UFUNCTION()
	void OnACEValueChanged(int32 NewACE);

	/** ACE 值 + 排名颜色变化回调 */
	UFUNCTION()
	void OnACEWithRankChanged(int32 NewACE, EACERankType RankType);

	/** 武器图标加载完毕回调 */
	UFUNCTION()
	void OnWeaponIconReady(const FString& WeaponID, class UTexture2D* Icon);

	/** 武器弹药数量加载完毕回调 【v84 大厂架构新增】 */
	UFUNCTION()
	void OnWeaponAmmoInfoReady(int32 CurrentMag, int32 MagazineSize, int32 ReserveAmmo);

	/** 武器面板显隐状态变化回调 【v105 新增 - 母体无武器时隐藏弹药 UI】 */
	void OnWeaponPanelVisibilityChanged(bool bIsVisible);

	// ==========================================
	// 8. 无敌期进度条控制 (2026.07.14 重构)
	// ==========================================

	/**
	 * 进入无敌期回调 - 显示复活进度条
	 * @param bIsNowInvincible true=进入无敌期
	 */
	UFUNCTION()
	void OnInvincibilityChanged(bool bIsNowInvincible);

private:
	/**
	 * 暂存当局击杀数（由 OnEnterSettlement 传入，在 OnShowFinalSettlement 中使用）
	 */
	int32 LastAttackerKills = 0;
	int32 LastDefenderKills = 0;

	// ==========================================
	// CharacterEvents 订阅状态 (2026-07-01 新增)
	// ==========================================

	/** 缓存当前订阅的 CharacterEvents 组件 (用于取消订阅) */
	UPROPERTY()
	UCharacterEvents* CachedCharacterEvents;

	/** CharacterEvents 延迟绑定定时器句柄 */
	UPROPERTY()
	FTimerHandle CharacterEventsRetryTimerHandle;

	/** CharacterEvents 重试计数器 (最大 10 次, 防止无限重试) */
	int32 CharacterEventsRetryCount = 0;

	/**
	 * 【2026-07-01 P0 新增】NativeTick 兜底计时器:
	 *   每秒检查一次 CharacterEvents 订阅状态, 解决 10 次重试后仍未订阅的"死局"场景
	 *   (例如 Pawn 在第 11 秒才被 Possess 进来, 此时重试已结束 → 永远丢订阅)
	 */
	float CharacterEventsFallbackTimer = 0.0f;

	/**
	 * 【2026-07-01 P0 新增】兜底检查: 每秒 1 次
	 *   - 已订阅但 Pawn 已变 → 退订重订
	 *   - 未订阅且有 Pawn → 重置重试并重新尝试
	 */
	void TickFallbackCheck(float DeltaTime);

	/**
	 * 【v40.8 P0 新增】无敌期轮询兜底: 每 0.1s 一次
	 *   - 主动从 HealthComponent 拉 GetInvincibilityRemainingSeconds()
	 *   - 与 Widget_RespawnProgress 显示状态对比, 缺则补 Show/Hide
	 *   - 解决 OnRep 初始值不触发 / 事件丢失 的边缘 case
	 *   - 大厂原则: HUD 不能依赖单一事件流, 必须有兜底机制
	 */
	float InvincibilityWatchdogTimer = 0.0f;
	void TickInvincibilityWatchdog(float DeltaTime);
};
