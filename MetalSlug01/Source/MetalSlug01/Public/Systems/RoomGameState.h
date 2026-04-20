#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameStateBase.h"
#include "UI/Login/Data/StaticTable.h"
#include "RoomGameState.generated.h"

/**
 * 房间全局状态类
 * 引擎会自动将所有连入房间的 PlayerState 存放在原生的 PlayerArray 数组中。
 * 这里未来可以放置房间的全局数据，如“当前对局阶段(等待、开战、结算)”、“总比分”等。
 */
// 声明倒计时改变的委托，UI可以订阅它以避免Tick轮询
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMatchTimeUpdated, int32, RemainingSeconds);

// 声明当前回合数改变的委托（生化模式每回合结束后递减）
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnCurrentRoundUpdated, int32, CurrentRound);

// 声明模式切换的委托（UI据此隐藏/显示 Text_RemainingRounds）
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMatchModeChanged, ERoomMatchMode, NewMode);

UCLASS()
class METALSLUG01_API ARoomGameState : public AGameStateBase
{
	GENERATED_BODY()

public:
	ARoomGameState();
	
	// 必须重写此函数以注册需要网络同步的变量
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;
	
	// ==========================================
	// 比赛模式与时间同步
	// ==========================================

	// 当前房间的游戏模式
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Room|Match")
	ERoomMatchMode CurrentMatchMode = ERoomMatchMode::Melee;

	// 【架构重构】：只同步比赛结束的绝对时间戳，避免每秒网络通信的极大开销
	// 当服务器决定开始倒计时，设置此变量为：GetServerWorldTimeSeconds() + 倒计时总时长
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Room|Match")
	float MatchEndTime = 0.0f;
	
	// 提供一个接口供 UI 查询剩余时间（秒），在客户端调用时能自适应网络延迟计算
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Room|Match")
	int32 GetMatchRemainingSeconds() const;

	// 剩余局数（生化模式：剩余回合数，刀战模式：隐藏此字段）
	UPROPERTY(ReplicatedUsing = OnRep_CurrentRound, BlueprintReadOnly, Category = "Room|Match")
	int32 CurrentRound = 0;

	// 客户端接到回合数同步时的回调
	UFUNCTION()
	void OnRep_CurrentRound();

	// UI 监听的委托
	UPROPERTY(BlueprintAssignable, Category = "Room|Match")
	FOnCurrentRoundUpdated OnCurrentRoundUpdated;

	// UI 监听的委托：模式切换时 UI 需隐藏/显示 Text_RemainingRounds
	UPROPERTY(BlueprintAssignable, Category = "Room|Match")
	FOnMatchModeChanged OnMatchModeChanged;

	// 提供一个极其方便的辅助函数：获取特定队伍的所有玩家
	// 因为数据分散在每个人自己的 PlayerState 里了，所以我们需要遍历查询
	UFUNCTION(BlueprintCallable, Category = "Room|Query")
	TArray<class ARoomPlayerState*> GetPlayersInTeam(ERoomTeam TargetTeam) const;
	
	// 记录当前房间的房主名称，全服同步！
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Room|Global")
	FString HostPlayerName;
};