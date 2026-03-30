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
	
	// 服务器上保存的权威名单
	TArray<FString> RedTeamNames;
	TArray<FString> BlueTeamNames;

	// 处理新玩家加入的逻辑
	void AddPlayerToRoom(const FString& PlayerName);

	// 把最新名单广播给房间里的所有玩家
	void BroadcastRoomUpdate();
	
	// 处理玩家主动请求换队伍
	void ChangePlayerTeam(const FString& PlayerName, bool bToRedTeam);
	
	// 处理玩家离开房间
	void RemovePlayerFromRoom(const FString& PlayerName);
	
	// 广播玩家聊天
	void BroadcastChatMessage(const FString& SenderName, const FString& Message);
	// 广播系统绿字提示
	void BroadcastSystemMessage(const FString& Message);
	
	// 【新增】：添加 AI 玩家
	void AddAIToRoom(bool bToRedTeam, const FString& CharacterName, int32 Count);
	
	// 【新增】：全频道广播函数（每当名单有变动，立刻通知所有人刷新UI）
	void BroadcastRoomUIUpdate();
	
	// 【新增】：用一个字典(Map)记录所有人的准备状态 (名字 -> 是否准备)
	UPROPERTY()
	TMap<FString, bool> PlayerReadyStates;

	// 【新增】：更新某个人的准备状态并广播
	void UpdatePlayerReadyState(const FString& PlayerName, bool bIsReady);
	
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
	// 【新增】：核心战斗生成接口
	// ==========================================
	// 处理玩家的生成请求
	void HandlePlayerRequestSpawn(class APlayerController* PC, FString CharRowName, FString WeaponRowName);
	
	// AI 向上帝申请一个目标
	UFUNCTION(BlueprintCallable, Category = "AI")
	class ABaseCharacter* RequestTargetForAI(class ABaseCharacter* RequestingAI);
	
	// 辅助函数 - 查一查这个倒霉蛋现在正被几个 AI 盯着？
	int32 GetAttackerCount(ABaseCharacter* TargetEnemy);

	// 释放记录 (参数改为请求释放的 AI)
	UFUNCTION(BlueprintCallable, Category = "AI")
	void ReleaseTarget(ABaseCharacter* RequestingAI);
	
private:
	// 【新增】：AI 的唯一编号生成器，防止同名 AI 无法精准踢出
	int32 AINextID = 1;
	
protected:
	// 真正执行生成和发枪的内部函数
	void SpawnAndEquip(APlayerController* PC, TSubclassOf<ABaseCharacter> CharClass, TSubclassOf<ABaseWeapon> WeaponClass);
	
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
	
};
