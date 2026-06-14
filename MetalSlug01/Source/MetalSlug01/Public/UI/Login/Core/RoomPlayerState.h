// 版权声明：在项目设置的描述页面填写您的版权信息。

#pragma once

// ==========================================
// 头文件包含说明
// ==========================================
// UE 引擎核心最小化头文件
#include "CoreMinimal.h"

// 引入 UE 原生 APlayerState 类（基类）
#include "GameFramework/PlayerState.h"

// 引入房间相关枚举（ERoomTeam 等）
#include "UI/Login/Data/StaticTable.h"

// UE 自动生成的头文件
#include "RoomPlayerState.generated.h"


// ==========================================
// 1. 委托声明
// ==========================================

/**
 * 计分板数据变化时的动态多播委托
 * 用于 UI 监听计分板数据刷新
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnScoreboardDataChanged);

/**
 * 房间玩家状态变化时的动态多播委托
 * 用于 UI 监听队伍/准备状态变化
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnRoomPlayerStateChanged);


/**
 * @class ARoomPlayerState
 * @brief 房间玩家状态类
 *
 * 职责说明:
 * - 在 Server 和 Client 之间自动同步单个玩家的队伍、准备状态以及计分板数据
 * - 暴露"已选角色+武器"信息给服务器 GameMode 使用
 * - 提供计分板（击杀/死亡/助攻/得分）的同步和广播
 *
 * 架构理念:
 * 1. 网络同步: ReplicatedUsing 机制自动同步到所有客户端
 * 2. UI 解耦: 通过委托广播，UI 层只需监听事件即可刷新
 * 3. 服务器权威: 所有数据修改均在 HasAuthority() 校验下执行
 */
UCLASS()
class METALSLUG01_API ARoomPlayerState : public APlayerState
{
	GENERATED_BODY()

public:
	/**
	 * 构造函数
	 * 目的: 初始化默认值、开启网络同步
	 */
	ARoomPlayerState();

	// ==========================================
	// 角色/武器选择 Getter（供服务器 GameMode 使用）
	// ==========================================

	/**
	 * 获取当前选中的角色 ID
	 */
	FString GetSelectedCharacterID() const { return SelectedCharacterID; }

	/**
	 * 获取当前选中的 1 号位武器 ID
	 */
	FString GetSelectedWeapon1ID() const { return SelectedWeaponID1; }

	/**
	 * 获取当前选中的 2 号位武器 ID
	 */
	FString GetSelectedWeapon2ID() const { return SelectedWeaponID2; }

	// ==========================================
	// 角色/武器选择 Setter
	// ==========================================

	/**
	 * 一次性设置角色+双武器（Controller 专用）
	 * 改为本地 Setter（去掉此处的 Server RPC，统一由 Controller 转发）
	 * @param InCharID 角色 ID
	 * @param InWeapon1ID 1 号位武器 ID
	 * @param InWeapon2ID 2 号位武器 ID
	 */
	void SetPlayerLoadout(const FString& InCharID, const FString& InWeapon1ID, const FString& InWeapon2ID);

	// ==========================================
	// 网络同步注册
	// ==========================================

	/**
	 * 重写此函数，注册需要网络同步的变量
	 * 【核心规范】: 必须重写
	 */
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// ==========================================
	// 2. 核心同步数据 (Replicated Data)
	// ==========================================

	/**
	 * 当前所属队伍
	 * ReplicatedUsing: 当服务器修改后，客户端收到新值时会自动触发 OnRep_Team
	 */
	UPROPERTY(ReplicatedUsing = OnRep_Team, BlueprintReadOnly, Category = "Room|State")
	ERoomTeam CurrentTeam;

	/**
	 * 玩家准备状态
	 * ReplicatedUsing: 客户端收到新值时会自动触发 OnRep_IsReady
	 */
	UPROPERTY(ReplicatedUsing = OnRep_IsReady, BlueprintReadOnly, Category = "Room|State")
	bool bIsReady;

	// ==========================================
	// 3. 计分板数据 (Scoreboard Data)
	// ==========================================
public:
	/**
	 * 获取总得分
	 */
	int32 GetScore() const { return RoomScore; }

	/**
	 * 获取击杀数
	 */
	int32 GetKills() const { return RoomKills; }

	/**
	 * 获取死亡数
	 */
	int32 GetDeaths() const { return RoomDeaths; }

	/**
	 * 获取助攻数
	 */
	int32 GetAssists() const { return RoomAssists; }

	/**
	 * 服务器专用: 增加得分（+1 击杀 +20 分）
	 * 同步更新 GameState 中的队伍击杀统计
	 */
	UFUNCTION(BlueprintCallable, Category = "Scoreboard")
	void AddKillScore();

	/**
	 * 服务器专用: 增加助攻得分（+1 助攻 +10 分）
	 */
	UFUNCTION(BlueprintCallable, Category = "Scoreboard")
	void AddAssistScore();

	/**
	 * 服务器专用: 增加死亡次数
	 */
	UFUNCTION(BlueprintCallable, Category = "Scoreboard")
	void AddDeath();

	/**
	 * 服务器专用: 重置计分板数据（每回合开始时调用）
	 */
	UFUNCTION(BlueprintCallable, Category = "Scoreboard")
	void ResetScoreboardStats();

protected:
	/**
	 * 计分板数据复制通知回调
	 */
	UFUNCTION()
	void OnRep_ScoreboardData();

	/**
	 * 总得分（网络同步）
	 */
	UPROPERTY(ReplicatedUsing = OnRep_ScoreboardData, BlueprintReadOnly, Category = "Scoreboard")
	int32 RoomScore;

	/**
	 * 击杀数（网络同步）
	 */
	UPROPERTY(ReplicatedUsing = OnRep_ScoreboardData, BlueprintReadOnly, Category = "Scoreboard")
	int32 RoomKills;

	/**
	 * 死亡数（网络同步）
	 */
	UPROPERTY(ReplicatedUsing = OnRep_ScoreboardData, BlueprintReadOnly, Category = "Scoreboard")
	int32 RoomDeaths;

	/**
	 * 助攻数（网络同步）
	 */
	UPROPERTY(ReplicatedUsing = OnRep_ScoreboardData, BlueprintReadOnly, Category = "Scoreboard")
	int32 RoomAssists;

public:
	/**
	 * 计分板数据变化时的广播事件
	 * UI 监听此委托即可刷新
	 */
	UPROPERTY(BlueprintAssignable, Category = "Scoreboard|Events")
	FOnScoreboardDataChanged OnScoreboardDataChanged;

	// ==========================================
	// 4. 客户端数据刷新回调 (Rep Notifies)
	// ==========================================

	/**
	 * 队伍变化时的客户端回调
	 */
	UFUNCTION()
	void OnRep_Team();

	/**
	 * 准备状态变化时的客户端回调
	 */
	UFUNCTION()
	void OnRep_IsReady();

	// ==========================================
	// 5. UI 绑定接口
	// ==========================================
	/**
	 * 队伍/准备状态变化广播事件
	 * UI 只需监听此委托即可刷新，实现完美解耦
	 */
	UPROPERTY(BlueprintAssignable, Category = "Room|Events")
	FOnRoomPlayerStateChanged OnStateChanged;

protected:
	// ==========================================
	// 6. 战备选择数据 (Replicated)
	// ==========================================
	/**
	 * 选中的角色 ID（用于显示队友信息）
	 */
	UPROPERTY(Replicated)
	FString SelectedCharacterID;

	/**
	 * 选中的 1 号位武器 ID
	 */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Room|Loadout")
	FString SelectedWeaponID1;

	/**
	 * 选中的 2 号位武器 ID
	 */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Room|Loadout")
	FString SelectedWeaponID2;

private:
	// ==========================================
	// 7. 计分板常量
	// ==========================================
	/**
	 * 击杀得分（每次击杀 +20 分）
	 */
	static constexpr int32 KillScoreValue = 20;

	/**
	 * 助攻得分（每次助攻 +10 分）
	 */
	static constexpr int32 AssistScoreValue = 10;
};
