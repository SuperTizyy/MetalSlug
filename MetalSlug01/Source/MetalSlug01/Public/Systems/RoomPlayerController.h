// 版权声明：在项目设置的描述页面填写您的版权信息。

#pragma once

// ==========================================
// 头文件包含说明
// ==========================================
// UE 引擎核心最小化头文件
#include "CoreMinimal.h"

// 引入 UE 原生 APlayerController 类（基类）
#include "GameFramework/PlayerController.h"

// UE 自动生成的头文件
#include "RoomPlayerController.generated.h"

// ==========================================
// 前置声明（加快编译速度，避免循环包含）
// ==========================================
class UInputAction;          // Enhanced Input 输入动作
class UInputMappingContext;  // Enhanced Input 映射上下文
class URoomInsidePage;       // 房间内 UI 页面
class UGameHUDWidget;        // 战斗 HUD 容器
class ARoomPlayerState;      // 房间玩家状态

/**
 * @class ARoomPlayerController
 * @brief 房间内的专属玩家控制器
 *
 * 职责说明:
 * - 处理 UI 数据与服务器的 RPC 同步（攻守方选择/准备/开始游戏/聊天/踢人/退房等）
 * - Enhanced Input 绑定（Tab 计分板/ESC 菜单/T 聊天）
 * - 死亡后的复活定时器
 * - 监听 GameFlowSubsystem 状态变化，自动切换 UI（房间 UI / 战斗 HUD）
 *
 * 关键设计:
 * 1. 复活定时器放在 Controller 上而非 Character，避免死亡被销毁
 * 2. UI 不在客户端直接调用 Server RPC，而是通过 PlayerController 中转
 * 3. 使用 Enhanced Input 系统，提供更好的输入控制粒度
 * 4. ESC 菜单状态用 bool 标志位 bIsEscMenuOpen 维护，避免依赖蓝图可见性
 */
UCLASS()
class METALSLUG01_API ARoomPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	// ==========================================
	// 生命周期
	// ==========================================

	/**
	 * UE 原生生命周期: 在 Actor 首次初始化时调用
	 * 用途: 订阅 GameFlowSubsystem 状态变化、初始化 ESC 菜单标志位
	 */
	virtual void BeginPlay() override;

	/**
	 * UE 原生生命周期: 设置输入组件
	 * 用途: 绑定 Enhanced Input 动作（Tab/ESC/T 聊天）
	 */
	virtual void SetupInputComponent() override;

	// ==========================================
	// Enhanced Input 系统配置
	// ==========================================

	/**
	 * 聊天快捷键输入动作（T 键）
	 * 在 BP_RoomPlayerController 蓝图中配置
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Input")
	UInputAction* IA_ToggleChat;

	/**
	 * 计分板快捷键输入动作（Tab 键）
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Input")
	UInputAction* IA_ToggleScoreboard;

	/**
	 * ESC 菜单快捷键输入动作
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Input")
	UInputAction* IA_ToggleEscMenu;

	// ==========================================
	// UI 引用
	// ==========================================

	/**
	 * 房间内 UI 页面蓝图类（攻守方选择/准备/开始游戏界面）
	 * 目的: OnFlowStateChanged(InRoom) 中动态 CreateWidget 创建
	 */
	UPROPERTY(EditDefaultsOnly, Category = "UI")
	TSubclassOf<URoomInsidePage> RoomUIClass;

	/**
	 * 当前激活的房间内 UI 实例
	 * 用途: 切换显示/隐藏、转发聊天消息
	 */
	UPROPERTY(Transient)
	URoomInsidePage* RoomUIWidget = nullptr;

	// ==========================================
	// 状态监听
	// ==========================================

	/**
	 * ESC 菜单的开关状态标志
	 * 作用: 用作 ESC 菜单是否打开的唯一可信真相源，规避蓝图可见性配置不一致的坑
	 */
	UPROPERTY(Transient)
	bool bIsEscMenuOpen = false;

	/**
	 * 房主解散房间时的延迟退出定时器
	 */
	FTimerHandle HostLeaveTimer;

	/**
	 * 计分板 Widget 实例缓存
	 */
	UPROPERTY(Transient)
	class UScoreboardWidget* ScoreboardWidgetInstance = nullptr;

	// ==========================================
	// Server RPC（客户端 → 服务器）
	// ==========================================

	/**
	 * 玩家向服务器发送自己的玩家名
	 * @param InPlayerName 玩家展示名（账号名）
	 * 服务器端: 写入 MyPlayerName + 覆盖底层 PlayerState 名字 + 通知 GameMode.AddPlayerToRoom
	 */
	UFUNCTION(Server, Reliable, WithValidation)
	void Server_SendPlayerInfo(const FString& InPlayerName);

	/**
	 * 玩家向服务器发送选中的角色和武器（用于加载存档偏好）
	 * @param CharacterRowName 角色 DataTable 行名
	 * @param Weapon1RowName 武器1 DataTable 行名
	 * @param Weapon2RowName 武器2 DataTable 行名
	 */
	UFUNCTION(Server, Reliable, WithValidation)
	void Server_SelectLoadout(const FString& CharacterRowName, const FString& Weapon1RowName, const FString& Weapon2RowName);

	/**
	 * 玩家请求切换队伍（攻/守）
	 * @param bToAttackTeam true=攻方，false=守方
	 */
	UFUNCTION(Server, Reliable, WithValidation)
	void Server_RequestChangeTeam(bool bToAttackTeam);

	/**
	 * 玩家请求切换准备状态
	 * @param bIsReady 是否准备
	 */
	UFUNCTION(Server, Reliable, WithValidation)
	void Server_ToggleReady(bool bIsReady);

	/**
	 * 玩家请求开始游戏（房主专属）
	 * 服务器端: 校验所有玩家已准备 -> 通知每个 Client 切换状态 -> HandlePlayerRequestSpawn
	 */
	UFUNCTION(Server, Reliable)
	void Server_RequestStartGame();

	/**
	 * 玩家请求离开房间（普通玩家）
	 */
	UFUNCTION(Server, Reliable, WithValidation)
	void Server_LeaveRoom();

	/**
	 * 房主踢人请求
	 * @param PlayerNameToKick 被踢玩家名（[AI] 前缀的为 AI）
	 */
	UFUNCTION(Server, Reliable, WithValidation)
	void Server_KickPlayer(const FString& PlayerNameToKick);

	/**
	 * 房主请求添加 AI
	 * @param bToAttackTeam AI 加入攻/守
	 * @param CharacterName AI 角色名
	 * @param Count AI 数量
	 */
	UFUNCTION(Server, Reliable, WithValidation)
	void Server_AddAI(bool bToAttackTeam, const FString& CharacterName, int32 Count);

	/**
	 * 玩家发送聊天消息
	 * @param Message 聊天内容
	 */
	UFUNCTION(Server, Reliable, WithValidation)
	void Server_SendChatMessage(const FString& Message);

	/**
	 * 玩家请求服务器生成自己的 3D 角色（用于测试/复活）
	 */
	UFUNCTION(Server, Reliable)
	void Server_RequestSpawn();

	// ==========================================
	// Client RPC（服务器 → 客户端）
	// ==========================================

	/**
	 * 服务器通知客户端"进入战斗状态"
	 * 客户端: 初始化倒计时 + 通知 GameFlowSubsystem 切换到 Battleing
	 */
	UFUNCTION(Client, Reliable)
	void Client_EnterBattleState();

	/**
	 * 服务器命令某个玩家离开房间（被房主踢出或房主解散）
	 * 客户端: 屏幕提示 + 执行 ExecuteLeaveRoom
	 */
	UFUNCTION(Client, Reliable)
	void Client_ForceLeaveRoom();

	/**
	 * 服务器通知某个玩家"你被踢了"
	 */
	UFUNCTION(Client, Reliable)
	void Client_BeKicked();

	/**
	 * 服务器广播聊天消息给某个客户端
	 * @param SenderName 发送者
	 * @param bIsHost 是否是房主
	 * @param Message 消息内容
	 * @param bIsSystemMsg 是否是系统消息
	 */
	UFUNCTION(Client, Reliable)
	void Client_ReceiveChatMessage(const FString& SenderName, bool bIsHost, const FString& Message, bool bIsSystemMsg);

	/**
	 * 服务器广播系统提示给某个客户端
	 * @param Message 提示内容
	 */
	UFUNCTION(Client, Reliable)
	void Client_ReceiveSystemMessage(const FString& Message);

	/**
	 * 服务器命令某个客户端切换全局状态
	 * @param NewState 目标状态
	 */
	UFUNCTION(Client, Reliable)
	void Client_TransitToMatchState(EMatchState NewState);

	// ==========================================
	// 退房系统
	// ==========================================

	/**
	 * 玩家点击"离开房间"按钮时调用
	 * 房主: 解散 Session + 遣散其他人 + 0.5秒后自己也走
	 * 普通玩家: 通知服务器 + 自己也走
	 */
	UFUNCTION(BlueprintCallable, Category = "Room|Leave")
	void LeaveRoom();

	/**
	 * 真正执行断网和跳地图的底层逻辑
	 * 步骤: DestroySession -> 0.5s 延时 -> GameFlowSubsystem.TransitToState(MainLobby)
	 */
	void ExecuteLeaveRoom();

	// ==========================================
	// Enhanced Input 回调
	// ==========================================

	/**
	 * T 键按下: 根据当前状态路由到正确的 UI 聊天输入
	 */
	void OnToggleChatAction();

	/**
	 * Tab 键按下: 显示计分板
	 */
	void OnScoreboardPressed();

	/**
	 * Tab 键松开: 隐藏计分板
	 */
	void OnScoreboardReleased();

	/**
	 * ESC 键按下: 切换 ESC 菜单显示/隐藏
	 */
	void OnEscPressed();

	// ==========================================
	// ESC 菜单
	// ==========================================

	/**
	 * 显示 ESC 菜单（暂停游戏 + UIOnly 输入 + 鼠标显示）
	 */
	void ShowEscMenu();

	/**
	 * 隐藏 ESC 菜单（恢复游戏 + GameOnly 输入 + 鼠标隐藏）
	 */
	void HideEscMenu();

	// ==========================================
	// 状态监听
	// ==========================================

	/**
	 * GameFlowSubsystem 状态变化回调
	 * @param NewState 新状态
	 * InRoom: 显示 RoomUI（鼠标UIOnly）
	 * Battleing: 隐藏 RoomUI（鼠标GameOnly）
	 * 其他: 销毁所有 UI，恢复游戏状态
	 */
	UFUNCTION()
	void OnFlowStateChanged(EMatchState NewState);

	// ==========================================
	// 复活系统
	// ==========================================

	/**
	 * 服务器端: 启动玩家复活倒计时
	 * @param InDelaySeconds 复活等待时间（秒）
	 */
	void StartRespawnTimer(float InDelaySeconds);

	/**
	 * 复活定时器到期回调
	 * 作用: 调 Server_RequestSpawn 重生角色
	 */
	UFUNCTION()
	void OnPlayerRespawnTimerFinished();

	/**
	 * 复活倒计时定时器句柄
	 */
	FTimerHandle RespawnTimerHandle;

	// ==========================================
	// 计分板管理
	// ==========================================

	/**
	 * 服务器端: 重置所有玩家的计分板数据
	 * 触发时机: 进入战斗状态时
	 */
	UFUNCTION()
	void ResetAllPlayerScoreboardStats();

	// ==========================================
	// 辅助接口
	// ==========================================

	/**
	 * 获取当前玩家激活的 HUD Widget
	 * 用途: 路由聊天/系统消息到战斗 HUD
	 */
	UGameHUDWidget* GetGameHUDWidget() const;

	/**
	 * 玩家名缓存（由 Server_SendPlayerInfo 设置）
	 */
	UPROPERTY(Transient)
	FString MyPlayerName;

	/**
	 * 玩家进入房间后延迟发送自身信息（解决网络未稳固问题）
	 */
	UFUNCTION()
	void DelayedSendPlayerInfo();
};
