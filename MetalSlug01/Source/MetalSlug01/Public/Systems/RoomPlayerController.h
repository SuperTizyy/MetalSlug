#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerController.h"
#include "RoomPlayerController.generated.h"

class URoomInsidePage;

/**
 * 房间内的专属玩家控制器，负责处理 UI 数据与服务器的 RPC 同步
 */
UCLASS()
class METALSLUG01_API ARoomPlayerController : public APlayerController
{
	GENERATED_BODY()

public:
	
	// 客户端上报：“我准备/取消准备了！”
	UFUNCTION(Server, Reliable, WithValidation)
	void Server_ToggleReady(bool bIsReady);
	
	// ==========================================
	// 【新增】AI 管理相关 RPC
	// ==========================================

	// 客户端请求服务器添加 AI
	UFUNCTION(Server, Reliable, WithValidation)
	void Server_AddAI(bool bToRedTeam, const FString& CharacterName, int32 Count);
	
	// 【新增】：记住这个对讲机代表的玩家名字（服务器端需要用它）
	UPROPERTY()
	FString MyPlayerName;
	
	// 游戏开始时触发
	virtual void BeginPlay() override;
	
	// ==========================================
	// 1. 客户端 -> 服务器的 RPC (Client to Server)
	// ==========================================
	// 刚进入房间时，客户端呼叫服务器：“报告老大，我进来了，这是我的名字！”
	// Reliable 表示必定送达，WithValidation 是 UE 网络安全的防作弊校验规范
	UFUNCTION(Server, Reliable, WithValidation)
	void Server_SendPlayerInfo(const FString& InPlayerName);
	
	// 服务器下达的终极指令：“全军出击！”
	UFUNCTION(Client, Reliable)
	void Client_EnterBattleState();

	// ==========================================
	// 2. 服务器 -> 客户端的 RPC (Server to Client)
	// ==========================================

	// 延迟发送玩家信息，避开网络抢跑期
	void DelayedSendPlayerInfo();
	
	// 【新增】：向服务器请求换队伍 (true=去红队, false=去蓝队)
	UFUNCTION(Server, Reliable, WithValidation)
	void Server_RequestChangeTeam(bool bToRedTeam);
	
	// 【新增】：UI 按钮调用的本地退出逻辑
	UFUNCTION(BlueprintCallable, Category = "RoomUI")
	void LeaveRoom();

	// 【新增】：告诉服务器我要走了，赶紧把我名字删掉
	UFUNCTION(Server, Reliable, WithValidation)
	void Server_LeaveRoom();
	
	// 【新增】：服务器强制命令客户端：“房主解散了，立刻退房！”
	UFUNCTION(Client, Reliable)
	void Client_ForceLeaveRoom();
	
	// ==========================================
	// 【新增】：房主专属 RPC，告诉服务器“把这个人给我踢了！”
	// ==========================================
	UFUNCTION(Server, Reliable, WithValidation)
	void Server_KickPlayer(const FString& PlayerNameToKick);

	// ==========================================
	// 【新增】：服务器给被踢的倒霉蛋发的专属指令：“你被踢了，快回大厅！”
	// ==========================================
	UFUNCTION(Client, Reliable)
	void Client_BeKicked();
	
	// ==========================================
	// 聊天系统 RPC
	// ==========================================
	
	// 客户端发给服务器：“老大，我发了一句话！”
	UFUNCTION(Server, Reliable, WithValidation)
	void Server_SendChatMessage(const FString& Message);

	// 服务器群发给所有人：“这是刚刚有人说的原话，你们赶紧显示出来！”
	UFUNCTION(Client, Reliable)
	void Client_ReceiveChatMessage(const FString& SenderName, bool bIsHost, const FString& Message, bool bIsSystemMsg);

	// 房主专属：向服务器请求开始游戏
	UFUNCTION(Server, Reliable)
	void Server_RequestStartGame();

	// 接收服务器打回的系统警告文本
	UFUNCTION(Client, Reliable)
	void Client_ReceiveSystemMessage(const FString& Message);
	
	// ==========================================
	// 【新增】：战斗与生成系统 RPC
	// ==========================================
	
	// 客户端向服务器呼叫：“我进地图了，别管UI了，直接给我发人发枪！”
	UFUNCTION(Server, Reliable)
	void Server_RequestSpawn();
	
	// ==========================================
	// 装备与选角系统
	// ==========================================

	// 本地记录玩家在 UI 上点击选择的配置
	UPROPERTY(BlueprintReadWrite, Category = "Room|Loadout")
	FString MySelectedCharacter;

	UPROPERTY(BlueprintReadWrite, Category = "Room|Loadout")
	FString MySelectedWeapon;

	// 客户端 UI 选择完毕后，通知服务器保存这个决定
	UFUNCTION(Server, Reliable, WithValidation)
	void Server_SelectLoadout(const FString& CharacterRowName, const FString& Weapon1RowName, const FString& Weapon2RowName);
	
	/**
	 * @brief 接收来自服务器的指令，强制本地客户端切换游戏状态流程 (Client RPC)
	 * @param NewState 目标状态 (如 EMatchState::Battleing)
	 */
	UFUNCTION(Client, Reliable, Category = "MetalSlug|Network")
	void Client_TransitToMatchState(EMatchState NewState);
	
private:
	// 【新增】：真正执行断网和跳地图的底层逻辑
	void ExecuteLeaveRoom();

	// 【新增】：定时器，让子弹飞一会儿
	FTimerHandle HostLeaveTimer;
	
protected:
	// 监听全局状态变化的事件回调
	UFUNCTION()
	void OnFlowStateChanged(EMatchState NewState);
	
	// 暴露给蓝图，用于选择你刚做好的 WBP_RoomInside
	UPROPERTY(EditDefaultsOnly, Category = "UI Config")
	TSubclassOf<URoomInsidePage> RoomUIClass;

	// 保存生成出来的 UI 界面指针，方便后续刷新
	UPROPERTY()
	URoomInsidePage* RoomUIWidget;

};