#pragma once

#include "CoreMinimal.h"
#include "GameFramework/GameModeBase.h"
#include "UI/Login/Data/StaticTable.h"
#include "RoomGameMode.generated.h"

class ABaseCharacter;
class ABaseWeapon;

/**
 * 房间大厅的专属 GameMode（只在服务器/房主端运行）
 * 负责管理权威的红蓝队名单，并广播给所有人
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
	void ChangePlayerTeam(AController* RequestingController, bool bToRedTeam);
	
	// 处理玩家离开房间
	void RemovePlayerFromRoom(AController* RequestingController);
	
	// 广播玩家聊天
	void BroadcastChatMessage(const FString& SenderName, const FString& Message);
	// 广播系统绿字提示
	void BroadcastSystemMessage(const FString& Message);
	
	// 【新增】：添加 AI 玩家
	void AddAIToRoom(bool bToRedTeam, const FString& CharacterName, int32 Count);
	
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
	
};
