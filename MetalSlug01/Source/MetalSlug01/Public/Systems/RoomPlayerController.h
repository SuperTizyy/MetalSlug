#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "RoomPlayerController.generated.h"

class URoomInsidePage;
class UGameHUDWidget;

/**
 * 房间内的专属玩家控制器，负责处理 UI 数据与服务器的 RPC 同步
 */
UCLASS()
class METALSLUG01_API ARoomPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	// 获取 GameHUDWidget 实例
	UGameHUDWidget* GetGameHUDWidget() const;

	// 获取玩家名称（供外部访问）
	FString GetMyPlayerName() const { return MyPlayerName; }

	// ==========================================
	// 1. 客户端 -> 服务器的 RPC (Client to Server)
	// ==========================================

	// 刚进入房间时，客户端呼叫服务器："报告老大，我进来了，这是我的名字！"
	UFUNCTION(Server, Reliable, WithValidation)
	void Server_SendPlayerInfo(const FString& InPlayerName);

	// 客户端上报："我准备/取消准备了！"
	UFUNCTION(Server, Reliable, WithValidation)
	void Server_ToggleReady(bool bIsReady);

	// 向服务器请求换队伍 (true=去攻方, false=去守方)
	UFUNCTION(Server, Reliable, WithValidation)
	void Server_RequestChangeTeam(bool bToAttackTeam);

	// UI 按钮调用的本地退出逻辑
	UFUNCTION(BlueprintCallable, Category = "RoomUI")
	void LeaveRoom();

	// 告诉服务器我要走了，赶紧把我名字删掉
	UFUNCTION(Server, Reliable, WithValidation)
	void Server_LeaveRoom();

	// 房主专属 RPC，告诉服务器"把这个人给我踢了！"
	UFUNCTION(Server, Reliable, WithValidation)
	void Server_KickPlayer(const FString& PlayerNameToKick);

	// 客户端请求服务器添加 AI
	UFUNCTION(Server, Reliable, WithValidation)
	void Server_AddAI(bool bToAttackTeam, const FString& CharacterName, int32 Count);

	// 聊天：客户端发给服务器
	UFUNCTION(Server, Reliable, WithValidation)
	void Server_SendChatMessage(const FString& Message);

	// 房主专属：向服务器请求开始游戏
	UFUNCTION(Server, Reliable)
	void Server_RequestStartGame();

	// 客户端选择装备后通知服务器保存
	UFUNCTION(Server, Reliable, WithValidation)
	void Server_SelectLoadout(const FString& CharacterRowName, const FString& Weapon1RowName, const FString& Weapon2RowName);

	// 客户端向服务器呼叫："我进地图了，别管UI了，直接给我发人发枪！"
	UFUNCTION(Server, Reliable)
	void Server_RequestSpawn();

	// ==========================================
	// 2. 服务器 -> 客户端的 RPC (Server to Client)
	// ==========================================

	// 服务器下达的终极指令："全军出击！"
	UFUNCTION(Client, Reliable)
	void Client_EnterBattleState();

	// 服务器强制命令客户端："房主解散了，立刻退房！"
	UFUNCTION(Client, Reliable)
	void Client_ForceLeaveRoom();

	// 服务器给被踢的倒霉蛋发的专属指令："你被踢了，快回大厅！"
	UFUNCTION(Client, Reliable)
	void Client_BeKicked();

	// 服务器群发给所有人聊天消息
	UFUNCTION(Client, Reliable)
	void Client_ReceiveChatMessage(const FString& SenderName, bool bIsHost, const FString& Message, bool bIsSystemMsg);

	// 服务器发回系统警告文本
	UFUNCTION(Client, Reliable)
	void Client_ReceiveSystemMessage(const FString& Message);

	// 接收来自服务器的指令，强制本地客户端切换游戏状态流程
	UFUNCTION(Client, Reliable, Category = "MetalSlug|Network")
	void Client_TransitToMatchState(EMatchState NewState);

	// ==========================================
	// 3. 玩家数据属性（供外部读取）
	// ==========================================
	UPROPERTY()
	FString MyPlayerName;

	// 本地记录玩家在 UI 上点击选择的配置
	UPROPERTY(BlueprintReadWrite, Category = "Room|Loadout")
	FString MySelectedCharacter;

	UPROPERTY(BlueprintReadWrite, Category = "Room|Loadout")
	FString MySelectedWeapon;

	// ==========================================
	// 4. 蓝图/UI 绑定
	// ==========================================
	// 暴露给蓝图，用于生成 WBP_RoomInside
	UPROPERTY(EditDefaultsOnly, Category = "UI Config")
	TSubclassOf<URoomInsidePage> RoomUIClass;

	// 保存生成出来的 UI 界面指针，方便后续刷新
	UPROPERTY()
	URoomInsidePage* RoomUIWidget;

protected:
	// 游戏开始时触发
	virtual void BeginPlay() override;

	// 监听全局状态变化的事件回调
	UFUNCTION()
	void OnFlowStateChanged(EMatchState NewState);

private:
	// 延迟发送玩家信息，避开网络抢跑期
	void DelayedSendPlayerInfo();

	// 真正执行断网和跳地图的底层逻辑
	void ExecuteLeaveRoom();

	// 定时器，让子弹飞一会儿
	FTimerHandle HostLeaveTimer;
};
