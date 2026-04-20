#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "UI/Login/Data/StaticTable.h"
#include "RoomGameMode.generated.h"

class ABaseCharacter;
class ABaseWeapon;

/**
 * 房间大厅的专属 GameMode（只在服务器/房主端运行）
 * 负责管理权威的攻守方名单，并广播给所有人
 */
UCLASS()
class METALSLUG01_API ARoomGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	// 构造函数
	ARoomGameMode(const FObjectInitializer& ObjectInitializer);
	

	// 处理新玩家加入的逻辑
	void AddPlayerToRoom(AController* RequestingController, const FString& PlayerName);
	
	// 处理玩家主动请求换队伍
	void ChangePlayerTeam(AController* RequestingController, bool bToAttackTeam);
	
	// 处理玩家离开房间
	void RemovePlayerFromRoom(AController* RequestingController);
	
	// 广播玩家聊天
	void BroadcastChatMessage(const FString& SenderName, const FString& Message);
	// 广播系统绿字提示
	void BroadcastSystemMessage(const FString& Message);
	
	// 【新增】：添加 AI 玩家
	void AddAIToRoom(bool bToAttackTeam, const FString& CharacterName, int32 Count);
	
	// 【新增】：更新某个人的准备状态并广播
	void UpdatePlayerReadyState(AController* RequestingController, bool bIsReady);
	
	// ==========================================
	// 【新增】：状态机与测试开关
	// ==========================================
	
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Game State")
	ERoomState CurrentRoomState;

	// 核心开关：勾选后进图直接开打，无视房间大厅！(纯测试刀战阶段极其好用)
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Game State|Test")
	bool bSkipRoomPhaseForTesting;

	// ==========================================
	// 【新增】：开发测试模式：默认发放的角色与武器
	// ==========================================
	UPROPERTY(EditDefaultsOnly, Category = "Game Data|Test Bypass")
	TSubclassOf<ABaseCharacter> TestCharacterClass;

	UPROPERTY(EditDefaultsOnly, Category = "Game Data|Test Bypass")
	TSubclassOf<ABaseWeapon> TestWeaponClass;

	// ==========================================
	// 【数据驱动配置】：在蓝图 BP_RoomGameMode 中配置对应的 DataTable 资产
	// ==========================================
	UPROPERTY(EditDefaultsOnly, Category = "Room|Data")
	class UDataTable* CharacterDataTable;

	UPROPERTY(EditDefaultsOnly, Category = "Room|Data")
	class UDataTable* WeaponDataTable;
	
	// 处理玩家的生成请求
	void HandlePlayerRequestSpawn(AController* PlayerToSpawn, const FString& CharRowName, const FString& WeaponRowName);
	
	// AI 向上帝申请一个目标
	UFUNCTION(BlueprintCallable, Category = "AI")
	class ABaseCharacter* RequestTargetForAI(class ABaseCharacter* RequestingAI);
	
	// 辅助函数 - 查一查这个倒霉蛋现在正被几个 AI 盯着？
	int32 GetAttackerCount(ABaseCharacter* TargetEnemy);
	bool CheckAllPlayersReady();

	// 释放记录 (参数改为请求释放的 AI)
	UFUNCTION(BlueprintCallable, Category = "AI")
	void ReleaseTarget(ABaseCharacter* RequestingAI);
	
	// ==========================================
	// 核心比赛流程控制
	// ==========================================
	
	/**
	 * @brief 接收并处理玩家请求开始游戏的指令 (仅服务器运行)
	 * @param RequestingController 发起请求的玩家控制器
	 */
	UFUNCTION(BlueprintCallable, Category = "MetalSlug|Room|Match")
	void RequestStartGame(AController* RequestingController);
	
private:
	// 【新增】：AI 的唯一编号生成器，防止同名 AI 无法精准踢出
	int32 AINextID = 1;
	
protected:
	// 记录目前哪些玩家正在被 AI 追杀（防止扎堆）
	// Key = 猎物 (玩家), Value = 猎人 (追他的 AI)
	UPROPERTY()
	TMap<ABaseCharacter*, ABaseCharacter*> LockedTargets;
	
	// 遍历全场，找出对这个 AI 来说所有活着的敌人
	TArray<class ABaseCharacter*> GetAllAliveEnemiesFor(class ABaseCharacter* RequestingAI);
	
	// 现在的账本记录的是：哪个 AI (Key) 正在追杀哪个敌人 (Value)
	// 这样的好处是：一个敌人可以被多个 AI 追，我们只要数一数 Value 出现的次数就行了！
	UPROPERTY()
	TMap<ABaseCharacter*, ABaseCharacter*> AIHuntingMap;
	
	// ==========================================
	// 覆盖 UE 原生生命周期函数
	// ==========================================
	
	// 1. 核心决策：决定当前 Controller 应该生成什么 Class 的实体
	virtual UClass* GetDefaultPawnClassForController_Implementation(AController* InController) override;

	// 2. 核心生成：玩家实体生成并附身完成后的钩子（在这里安全地派发武器）
	virtual void RestartPlayer(AController* NewPlayer) override;
	
	/**
	 * @brief 权威校验通过后，真正执行开局指令下发与状态流转
	 */
	void PerformGameStart();
	
	// ==========================================
	// 比赛流程控制与实体生成
	// ==========================================

	// 开局倒计时的定时器句柄
	FTimerHandle MatchStartTimerHandle;

	// 倒计时时间（秒），可以暴露给蓝图配置
	UPROPERTY(EditDefaultsOnly, Category = "MetalSlug|Match")
	float MatchStartDelay = 3.0f;

	// 总局数（每边达到这个胜局数时比赛结束）
	UPROPERTY(EditDefaultsOnly, Category = "MetalSlug|Match")
	int32 TotalRounds = 10;

	// 生化模式总回合数
	UPROPERTY(EditDefaultsOnly, Category = "MetalSlug|Match")
	int32 ZombieTotalRounds = 5;

	/**
	 * @brief 倒计时结束后触发，负责遍历所有人并生成真实的 3D 角色
	 */
	void SpawnAllPlayersIntoBattle();

	// ==========================================
	// 角色/武器生成缓存（绕过 PlayerState 复制时序问题）
	// ==========================================
	struct FPlayerSpawnData
	{
		FString CharID;
		FString WeaponID;
	};
	// Key = PlayerState unique ID (GetUniqueID())，确保每个玩家独立
	TMap<uint32, FPlayerSpawnData> PlayerSpawnDataCache;
	
	// 比赛计时器句柄
	FTimerHandle MatchTimerHandle;

public:
	// 核心函数：根据模式初始化并开启倒计时
	void StartMatchTimer();

	// 核心函数：每秒触发一次，扣减时间
	UFUNCTION()
	void OnMatchTimerTick();

	// 核心函数：处理时间耗尽的宏观逻辑（结束本局或进下一回合）
	void HandleMatchTimeOut();

	// 生化模式回合结束处理
	void HandleZombieRoundEnd();

	// 生化模式进入下一回合
	UFUNCTION(BlueprintCallable, Category = "MetalSlug|Match")
	void StartNextZombieRound();

};
