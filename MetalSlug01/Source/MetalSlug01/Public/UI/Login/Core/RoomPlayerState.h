#pragma once

#include "CoreMinimal.h"
#include "GameFramework/PlayerState.h"
#include "RoomPlayerState.generated.h"

// 【规范】：使用强类型枚举定义队伍，比 bool 或 int 更具可读性和扩展性
UENUM(BlueprintType)
enum class ERoomTeam : uint8
{
	None UMETA(DisplayName = "未分配"),
	Red  UMETA(DisplayName = "红队"),
	Blue UMETA(DisplayName = "蓝队")
};

// 声明一个动态多播委托，用于通知 UI 刷新
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnRoomPlayerStateChanged);

/**
 * 房间玩家状态类
 * 负责在 Server 和 Client 之间自动同步单个玩家的队伍、准备状态等
 */
UCLASS()
class METALSLUG01_API ARoomPlayerState : public APlayerState
{
	GENERATED_BODY()

public:
	ARoomPlayerState();

	// 【核心规范】：必须重写此函数，注册需要网络同步的变量
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// ==========================================
	// 核心同步数据 (Replicated Data)
	// ==========================================

	// 当前所属队伍
	// ReplicatedUsing 意味着：当服务器修改这个值后，客户端收到新值时会自动触发 OnRep_Team 函数
	UPROPERTY(ReplicatedUsing = OnRep_Team, BlueprintReadOnly, Category = "Room|State")
	ERoomTeam CurrentTeam;

	// 玩家准备状态
	UPROPERTY(ReplicatedUsing = OnRep_IsReady, BlueprintReadOnly, Category = "Room|State")
	bool bIsReady;

	// ==========================================
	// 客户端数据刷新回调 (Rep Notifies)
	// ==========================================
	
	UFUNCTION()
	void OnRep_Team();

	UFUNCTION()
	void OnRep_IsReady();

	// ==========================================
	// UI 绑定接口
	// ==========================================
	// 当数据发生变化时，广播此事件，UI 层只需监听这个事件即可刷新，实现完美解耦
	UPROPERTY(BlueprintAssignable, Category = "Room|Events")
	FOnRoomPlayerStateChanged OnStateChanged;
	
	// 装备与选角配置
	// 用于持久化记录该玩家选择的英雄和武器
	
	UPROPERTY(BlueprintReadWrite, Category = "Room|Loadout")
	FString SelectedCharacterRowName;

	UPROPERTY(BlueprintReadWrite, Category = "Room|Loadout")
	FString SelectedWeaponRowName;
};