// 版权声明：在项目设置的描述页面填写您的版权信息。

// ========================================================================
// RoomService.h — 房间业务服务头文件
// ========================================================================
//
// 文件功能总览:
//   - 声明 3 个事件委托(FOnRoomHostChanged / FOnRoomPlayerJoined / FOnRoomPlayerLeft)
//   - 声明 URoomService 类(继承 UGameInstanceSubsystem),为 View 层提供
//     房间业务(切队/准备/聊天/添加 AI/选 Loadout/开局/离房)统一门面
//
// 大厂架构角色 — L2 Service Layer (Transport Abstraction):
//   - 业务层(RoomInsidePage)只调 RoomService, 零感知 RPC/GameMode
//   - RoomService 内部自动判断: 联机模式走 RPC, 独立进程模式直接调 GameMode
//   - 大厂对应:Riot 客户端的 Transport Layer / Epic Lyra 的 PlayerCommands
//
// 透明传输策略:
//   ┌─────────────────┬─────────────────────┬─────────────────────┐
//   │ 调用             │ 联机(Client/Server) │ 独立进程(Host=Client)│
//   ├─────────────────┼─────────────────────┼─────────────────────┤
//   │ RequestChangeTeam│ PC->Server_*        │ GM->ChangePlayerTeam│
//   │ RequestReady     │ PC->Server_Toggle   │ GM->UpdateReady     │
//   │ RequestChat      │ PC->Server_SendChat │ GM->BroadcastChat   │
//   │ RequestAddAI     │ PC->Server_QueueAI  │ GM->QueueAIForBattleSpawn │
//   │ RequestLoadout   │ PC->Server_Select   │ PS->SetPlayerLoadout│
//   │ RequestStartGame │ PC->Server_Start    │ GM->RequestStartGame│
//   │ RequestLeaveRoom │ SM->DestroySession  │ SM->DestroySession  │
//   └─────────────────┴─────────────────────┴─────────────────────┘
// ========================================================================

#pragma once

// ==========================================
// 头文件包含说明
// ==========================================
#include "CoreMinimal.h"
#include "Subsystems/GameInstanceSubsystem.h"
#include "RoomService.generated.h"

class APlayerController;

/**
 * 【P0 架构升级】事件总线委托声明（必须在 UCLASS 外, 否则 UHT 不识别）
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnRoomHostChanged, bool, bIsHostNow);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnRoomPlayerJoined, const FString&, PlayerName);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnRoomPlayerLeft, const FString&, PlayerName);

/**
 * @class URoomService
 * @brief 房间业务服务（统一 RPC 路由层，独立进程 100% 兼容）
 *
 * 【大厂标准架构：L2 Service Layer - Transport Abstraction】
 * - 业务层（RoomInsidePage）只调 RoomService，零感知 RPC/GameMode
 * - RoomService 内部自动判断：联机模式用 RPC，独立进程模式直接调 GameMode
 * - 业务代码可读性 100% 提升
 *
 * 透明传输策略:
 * ┌─────────────────┬─────────────────────┬─────────────────────┐
 * │ 调用             │ 联机（Client/Server）│ 独立进程（Host=Client）│
 * ├─────────────────┼─────────────────────┼─────────────────────┤
 * │ RequestChangeTeam│ PC->Server_*        │ GM->ChangePlayerTeam│
 * │ RequestReady     │ PC->Server_Toggle   │ GM->UpdateReady     │
 * │ RequestChat      │ PC->Server_SendChat │ GM->BroadcastChat   │
 * │ RequestAddAI     │ PC->Server_QueueAI  │ GM->QueueAIForBattleSpawn │
 *                                                 (v28: 只入队, 战斗 Spawn) │
 * │ RequestLoadout   │ PC->Server_Select   │ PS->SetPlayerLoadout│
 * │ RequestStartGame │ PC->Server_Start    │ GM->RequestStartGame│
 * │ RequestLeaveRoom │ SM->DestroySession  │ SM->DestroySession  │
 * └─────────────────┴─────────────────────┴─────────────────────┘
 *
 * 【大厂对应】 Riot 客户端的 Transport Layer / Epic Lyra 的 PlayerCommands
 */
UCLASS()
class METALSLUG01_API URoomService : public UGameInstanceSubsystem
{
	GENERATED_BODY()

public:
	// ==========================================
	// 静态访问器（业务层唯一调用入口）
	// ==========================================

	UFUNCTION(BlueprintCallable, Category = "RoomService", meta = (WorldContext = "WorldContextObject"))
	static URoomService* Get(const UObject* WorldContextObject);

	// ==========================================
	// 玩家状态业务（已使用）
	// ==========================================

	UFUNCTION(BlueprintCallable, Category = "RoomService")
	void RequestChangeTeam(bool bToAttackTeam);

	/**
	 * @brief 玩家切换准备状态 (联机走 Server RPC, 独立进程直接调 GM)
	 *
	 * 调用方: 房内 UI 准备按钮
	 * 联机模式: 通过 PC->Server_TogglePlayerReady RPC 走真实网络
	 * 独立进程: 直接 GM->UpdateReady(PC, bIsReady) (Host=Client 同进程, 无需 RPC)
	 */
	UFUNCTION(BlueprintCallable, Category = "RoomService")
	void RequestReady(bool bIsReady);

	/**
	 * @brief 发送房间聊天消息 (联机走 Server RPC, 独立进程直接调 GM)
	 *
	 * 调用方: 房内聊天输入框
	 * 消息经透明传输层发到服务器, 由 GM->BroadcastChat 广播给所有玩家
	 */
	UFUNCTION(BlueprintCallable, Category = "RoomService")
	void RequestSendChatMessage(const FString& Message);

	/**
	 * @brief 房主添加 AI 占位 (v28: 大厅只入队, 战斗 Spawn 才生成 Pawn)
	 *
	 * 调用方: 房主 UI "添加 AI [攻方/守方]" 按钮
	 * 联机模式: PC->Server_QueueAIForBattleSpawn RPC
	 * 独立进程: 直接 GM->QueueAIForBattleSpawn(...)
	 * AI Pawn 不立刻生成, 仅维护在 PendingAIQueue 等候战斗开局 Spawn
	 */
	UFUNCTION(BlueprintCallable, Category = "RoomService")
	void RequestAddAI(bool bToAttackTeam, const FString& CharacterRowName, const FString& WeaponRowName, int32 Count);

	/**
	 * @brief 玩家选择大厅 Loadout (4 把武器: 主/副/副/近战)
	 *
	 * 调用方: 大厅选武器 UI 确认按钮
	 * 联机模式: PC->Server_SelectLoadout RPC
	 * 独立进程: 直接 PS->SetPlayerLoadout(...)
	 * 仅玩家 UI 触发, 不影响已 Spawn 的 Pawn (战斗开始 Spawn 链才读这些字段)
	 */
	UFUNCTION(BlueprintCallable, Category = "RoomService")
	void RequestSelectLoadout(const FString& CharacterRowName, const FString& WeaponPrimaryRowName, const FString& WeaponSecondaryRowName, const FString& WeaponMeleeRowName);

	/**
	 * @brief 房主请求开始游戏 (联机走 Server RPC, 独立进程直接调 GM)
	 *
	 * 调用方: 房主 UI "开始游戏" 按钮 (仅房主可见/可点)
	 * 联机模式: PC->Server_RequestStartGame RPC
	 * 独立进程: 直接 GM->RequestStartGame()
	 * 触发后进入 MatchCountdown 倒计时, 倒计时结束调 PerformGameStart
	 */
	UFUNCTION(BlueprintCallable, Category = "RoomService")
	void RequestStartGame();

	/**
	 * @brief 玩家离开房间 (联机走 Server RPC, 独立进程直接调 GM)
	 *
	 * 调用方: 房内 UI "离开房间" 按钮
	 * 联机模式: PC->Server_RequestLeaveRoom RPC
	 * 独立进程: 直接 GM->RemovePlayerFromRoom(PC)
	 * 房主离房 = 销毁整个 Session
	 */
	UFUNCTION(BlueprintCallable, Category = "RoomService")
	void RequestLeaveRoom();

	// ==========================================
	// 身份同步（被 LANRoomPresenter 调用）
	// ==========================================

	/** 通知 RoomService 本实例是 Host（由 Presenter 在 OnCreateRoomComplete 时调用） */
	void NotifyBecameHost();

	/** 通知 RoomService 本实例是 Client */
	void NotifyBecameClient();

	/**
	 * 【大厂 P0 修复 2026.07.03】进入"测试房主"模式
	 *
	 * 业务场景:
	 *   - 开发者通过 Project Settings 勾选"跳过登录自动当房主"
	 *   - GameFlow 启动时检测到勾选 → 调本接口
	 *   - 本机立即被识别为独立进程房主, 无需走 SessionManager 创房成功回调
	 *
	 * 与 NotifyBecameHost 的差异:
	 *   - NotifyBecameHost: 依赖 LANRoomPresenter 创房成功, 异步, 走真实 Session
	 *   - EnterSkipToHostMode: 同步, 纯本地状态, 不创 Session, 不走 RPC
	 *
	 * 幂等保护:
	 *   - 重复调用安全, 仅在状态变化时广播事件
	 *
	 * 副作用:
	 *   - bIsHost = true
	 *   - 广播 OnHostChanged(true) → RoomInsidePage 立即刷新房主按钮
	 *   - 广播 OnPlayerJoined(LocalAccountName) → 本机玩家标签显示
	 *   - 同步 GameState->HostPlayerName = 本机账号 (若有 Authority)
	 */
	UFUNCTION(BlueprintCallable, Category = "RoomService")
	void EnterSkipToHostMode();

	/** 当前是否是 Host */
	bool IsHost() const { return bIsHost; }

	/** 获取当前登录的账号名（从 AccountSubsystem 拿） */
	FString GetCurrentAccountName() const;

	// ==========================================
	// 【P0 架构升级】事件总线 - 业务变更广播（替代 UI 轮询）
	// 大厂做法: View 订阅, Service 在数据变化时主动广播
	// ==========================================

	/** Host 身份变化（房主转让 / 客户端变为房主） */
	UPROPERTY(BlueprintAssignable, Category = "RoomService")
	FOnRoomHostChanged OnHostChanged;

	/** 玩家加入房间（房主调用 GM->AddPlayerToRoom 后由 GM 末尾广播） */
	UPROPERTY(BlueprintAssignable, Category = "RoomService")
	FOnRoomPlayerJoined OnPlayerJoined;

	/** 玩家离开房间 */
	UPROPERTY(BlueprintAssignable, Category = "RoomService")
	FOnRoomPlayerLeft OnPlayerLeft;

	// ==========================================
	// 【P0 架构升级】静态广播器（供 GM/Presenter 直接调用, 避免在非 Subsystem 类中 Get）
	// ==========================================

	/** 静态广播入口: 不需要 UObject* 即可触发, 内部自动定位 GameInstance 上的 RoomService */
	UFUNCTION(BlueprintCallable, Category = "RoomService", meta = (WorldContext = "WorldContextObject"))
	static void BroadcastHostChanged(const UObject* WorldContextObject, bool bIsHostNow);

	UFUNCTION(BlueprintCallable, Category = "RoomService", meta = (WorldContext = "WorldContextObject"))
	static void BroadcastPlayerJoined(const UObject* WorldContextObject, const FString& PlayerName);

	UFUNCTION(BlueprintCallable, Category = "RoomService", meta = (WorldContext = "WorldContextObject"))
	static void BroadcastPlayerLeft(const UObject* WorldContextObject, const FString& PlayerName);

private:
	// ==========================================
	// 内部路由
	// ==========================================

	class ARoomPlayerController* GetEffectiveRoomPC() const;
	class ARoomGameMode* GetRoomGameMode() const;
	class ARoomPlayerState* GetEffectivePlayerState() const;
	class APlayerController* GetEffectivePC() const;

	// 【v51 大厂重构 — 已删除】ResolveCharacterInfoRowName 函数已被完全删除
	//   - 旧 (v49): UI 选 CharacterName → 调本函数反查 RowName → SpawnAIInternal 又反查 PawnClass
	//   - 新 (v51): RequestAddAI 一次性反查全部字段 (RowName + AIPawnClass + WeaponID)
	//   - 反查路径只有一条, 完全消除反查分散

private:
	UPROPERTY(Transient)
	bool bIsHost = false;
};