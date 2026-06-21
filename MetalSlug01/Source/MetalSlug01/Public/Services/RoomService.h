// 版权声明：在项目设置的描述页面填写您的版权信息。

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
 * │ RequestAddAI     │ PC->Server_AddAI    │ GM->AddAIToRoom     │
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

	UFUNCTION(BlueprintCallable, Category = "RoomService")
	void RequestReady(bool bIsReady);

	UFUNCTION(BlueprintCallable, Category = "RoomService")
	void RequestSendChatMessage(const FString& Message);

	UFUNCTION(BlueprintCallable, Category = "RoomService")
	void RequestAddAI(bool bToAttackTeam, const FString& CharacterName, int32 Count);

	UFUNCTION(BlueprintCallable, Category = "RoomService")
	void RequestSelectLoadout(const FString& CharacterRowName, const FString& Weapon1RowName, const FString& Weapon2RowName);

	UFUNCTION(BlueprintCallable, Category = "RoomService")
	void RequestStartGame();

	UFUNCTION(BlueprintCallable, Category = "RoomService")
	void RequestLeaveRoom();

	// ==========================================
	// 身份同步（被 LANRoomPresenter 调用）
	// ==========================================

	/** 通知 RoomService 本实例是 Host（由 Presenter 在 OnCreateRoomComplete 时调用） */
	void NotifyBecameHost();

	/** 通知 RoomService 本实例是 Client */
	void NotifyBecameClient();

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

private:
	UPROPERTY(Transient)
	bool bIsHost = false;
};