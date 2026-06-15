// 版权声明：在项目设置的描述页面填写您的版权信息。

#pragma once

// ==========================================
// 头文件包含说明
// ==========================================
// UE 引擎核心最小化头文件（FString/TArray/基础宏）
#include "CoreMinimal.h"

// 引入 UE 原生 AGameModeBase 类（基类）
#include "GameFramework/GameModeBase.h"

// 引入房间相关枚举（ERoomState/ERoomTeam/ERoomMatchMode 等）
// 改造: 改为精确子表头
#include "Data/Enums/RoomEnums.h"

// UE 自动生成的头文件（必须放在最后一行）
#include "RoomGameMode.generated.h"

// ==========================================
// 前置声明（避免头文件互相包含）
// ==========================================
class ABaseCharacter;     // 角色基类
class ABaseWeapon;        // 武器基类
class APlayerStart;       // 玩家出生点 Actor
class ARoomPlayerController; // 房间玩家控制器

/**
 * @class ARoomGameMode
 * @brief 房间大厅的专属 GameMode（只在服务器/房主端运行）
 *
 * 职责说明:
 * - 管理权威的攻守方名单，并广播给所有人
 * - 处理玩家加入/换队/准备/踢人/聊天
 * - 控制比赛开始、倒计时、回合结束
 * - 负责所有玩家和 AI 的 3D 角色生成与武器派发
 * - 维护出生点池、目标仇恨分配
 *
 * 架构理念:
 * 1. 利用 UE 原生 PlayerState + ReplicatedUsing 实现数据自动同步
 * 2. 避免手写广播，最大化利用引擎自带的复制机制
 * 3. 攻守方名单/AI 名单/准备状态全部走 PlayerState 数组
 */
UCLASS()
class METALSLUG01_API ARoomGameMode : public AGameModeBase
{
	GENERATED_BODY()

public:
	/**
	 * 构造函数: 在 GameMode 被加载时调用
	 * 目的: 配置默认的玩家类、控制器类、HUD 类等
	 */
	ARoomGameMode(const FObjectInitializer& ObjectInitializer);

	// ==========================================
	// 玩家管理接口（由 RoomPlayerController 通过 RPC 调用）
	// ==========================================

	/**
	 * @brief 处理新玩家加入房间
	 * @param RequestingController 发起请求的玩家控制器
	 * @param PlayerName 玩家展示名
	 */
	void AddPlayerToRoom(AController* RequestingController, const FString& PlayerName);

	/**
	 * @brief 处理玩家主动请求换队伍
	 * @param RequestingController 发起请求的玩家控制器
	 * @param bToAttackTeam true=攻方，false=守方
	 */
	void ChangePlayerTeam(AController* RequestingController, bool bToAttackTeam);

	/**
	 * @brief 处理玩家离开房间
	 * @param RequestingController 离开的玩家控制器
	 */
	void RemovePlayerFromRoom(AController* RequestingController);

	/**
	 * @brief 广播玩家聊天
	 * @param SenderName 发送者名称
	 * @param Message 聊天内容
	 */
	void BroadcastChatMessage(const FString& SenderName, const FString& Message);

	/**
	 * @brief 广播系统绿字提示
	 * @param Message 系统提示内容
	 */
	void BroadcastSystemMessage(const FString& Message);

	/**
	 * @brief 添加 AI 玩家到指定队伍
	 * @param bToAttackTeam true=攻方，false=守方
	 * @param CharacterName AI 角色名（用于查找 DataTable）
	 * @param Count 添加数量
	 */
	void AddAIToRoom(bool bToAttackTeam, const FString& CharacterName, int32 Count);

	/**
	 * @brief 更新某个人的准备状态并广播
	 * @param RequestingController 发起请求的玩家控制器
	 * @param bIsReady 是否准备
	 */
	void UpdatePlayerReadyState(AController* RequestingController, bool bIsReady);

	// ==========================================
	// 状态机与测试开关
	// ==========================================

	/**
	 * 当前房间的状态（大厅等待 / 战斗中）
	 * 供 UI 通过 ARoomPlayerState 同步获取
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Game State")
	ERoomState CurrentRoomState;

	/**
	 * 核心测试开关: 勾选后进图直接开打，无视房间大厅！
	 * 用途: 纯测试刀战阶段极其好用
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Game State|Test")
	bool bSkipRoomPhaseForTesting;

	// ==========================================
	// 开发测试模式: 默认发放的角色与武器
	// ==========================================

	/**
	 * 测试模式下默认发放的角色蓝图
	 * 配合 bSkipRoomPhaseForTesting 使用
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Game Data|Test Bypass")
	TSubclassOf<ABaseCharacter> TestCharacterClass;

	/**
	 * 测试模式下默认发放的武器蓝图
	 * 配合 bSkipRoomPhaseForTesting 使用
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Game Data|Test Bypass")
	TSubclassOf<ABaseWeapon> TestWeaponClass;

	// ==========================================
	// 【数据驱动配置】: 在蓝图 BP_RoomGameMode 中配置对应的 DataTable 资产
	// ==========================================

	/**
	 * 角色信息数据表（用于查表生成 3D 角色）
	 * 行结构: FCharacterInfo
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Room|Data")
	class UDataTable* CharacterDataTable;

	/**
	 * 武器信息数据表（用于查表生成 3D 武器并派发）
	 * 行结构: FWeaponInfo
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Room|Data")
	class UDataTable* WeaponDataTable;

	/**
	 * @brief 处理玩家的生成请求
	 * @param PlayerToSpawn 目标玩家控制器
	 * @param CharRowName 角色 DataTable 的行名
	 * @param WeaponRowName 武器 DataTable 的行名
	 */
	void HandlePlayerRequestSpawn(AController* PlayerToSpawn, const FString& CharRowName, const FString& WeaponRowName);

	/**
	 * @brief AI 向上帝申请一个目标
	 * @param RequestingAI 请求分配的 AI
	 * @return 分配的敌人目标（避免多个 AI 扎堆追杀同一人）
	 */
	UFUNCTION(BlueprintCallable, Category = "AI")
	class ABaseCharacter* RequestTargetForAI(class ABaseCharacter* RequestingAI);

	/**
	 * @brief 查一查这个倒霉蛋现在正被几个 AI 盯着？
	 * @param TargetEnemy 目标敌人
	 * @return 正在追杀该敌人的 AI 数量
	 */
	int32 GetAttackerCount(ABaseCharacter* TargetEnemy);

	/**
	 * @brief 检查所有玩家是否都已准备
	 * @return 是否全部准备
	 */
	bool CheckAllPlayersReady();

	/**
	 * @brief 释放目标记录（AI 死亡或换目标时调用）
	 * @param RequestingAI 请求释放的 AI
	 */
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
	/**
	 * AI 的唯一编号生成器，防止同名 AI 无法精准踢出
	 * 每次 AddAIToRoom 时自增
	 */
	int32 AINextID = 1;

protected:
	/**
	 * 记录目前哪些玩家正在被 AI 追杀（防止扎堆）
	 * Key = 猎物 (玩家), Value = 猎人 (追他的 AI)
	 */
	UPROPERTY()
	TMap<ABaseCharacter*, ABaseCharacter*> LockedTargets;

	/**
	 * 遍历全场，找出对这个 AI 来说所有活着的敌人
	 * @param RequestingAI 发起查询的 AI
	 * @return 活着的敌人列表
	 */
	TArray<class ABaseCharacter*> GetAllAliveEnemiesFor(class ABaseCharacter* RequestingAI);

	/**
	 * 现在的账本记录的是: 哪个 AI (Key) 正在追杀哪个敌人 (Value)
	 * 这样的好处是: 一个敌人可以被多个 AI 追，我们只要数一数 Value 出现的次数就行了
	 */
	UPROPERTY()
	TMap<ABaseCharacter*, ABaseCharacter*> AIHuntingMap;

	// ==========================================
	// 覆盖 UE 原生生命周期函数
	// ==========================================

	/**
	 * 1. 核心决策: 决定当前 Controller 应该生成什么 Class 的实体
	 * 用途: 根据 ERoomMatchMode 选择不同蓝图子类的角色
	 */
	virtual UClass* GetDefaultPawnClassForController_Implementation(AController* InController) override;

	/**
	 * 2. 核心生成: 玩家实体生成并附身完成后的钩子（在这里安全地派发武器）
	 */
	virtual void RestartPlayer(AController* NewPlayer) override;

	/**
	 * @brief 权威校验通过后，真正执行开局指令下发与状态流转
	 */
	void PerformGameStart();

	// ==========================================
	// 比赛流程控制与实体生成
	// ==========================================

	/**
	 * 开局倒计时的定时器句柄
	 */
	FTimerHandle MatchStartTimerHandle;

	/**
	 * 倒计时时间（秒），可以暴露给蓝图配置
	 */
	UPROPERTY(EditDefaultsOnly, Category = "MetalSlug|Match")
	float MatchStartDelay = 3.0f;

	/**
	 * 总局数（每边达到这个胜局数时比赛结束）
	 */
	UPROPERTY(EditDefaultsOnly, Category = "MetalSlug|Match")
	int32 TotalRounds = 10;

	/**
	 * 生化模式总回合数
	 */
	UPROPERTY(EditDefaultsOnly, Category = "MetalSlug|Match")
	int32 ZombieTotalRounds = 5;

	/**
	 * 刀战模式每局比赛时长（秒），在此可配置任意值
	 * 例如 300=5分钟、600=10分钟、900=15分钟
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MetalSlug|Match")
	int32 MeleeMatchDurationSeconds = 600;

	/**
	 * @brief 倒计时结束后触发，负责遍历所有人并生成真实的 3D 角色
	 */
	void SpawnAllPlayersIntoBattle();

	// ==========================================
	// 角色/武器生成缓存（绕过 PlayerState 复制时序问题）
	// ==========================================

	/**
	 * 玩家生成数据: 角色ID + 武器ID
	 */
	struct FPlayerSpawnData
	{
		FString CharID;
		FString WeaponID;
	};

	/**
	 * Key = PlayerState unique ID (GetUniqueID())，确保每个玩家独立
	 * 作用: 在 RestartPlayer 之前缓存玩家选中的角色与武器，避免时序问题
	 */
	TMap<uint32, FPlayerSpawnData> PlayerSpawnDataCache;

	/**
	 * 比赛计时器句柄
	 */
	FTimerHandle MatchTimerHandle;

public:
	/**
	 * 核心函数: 根据模式初始化并开启倒计时
	 */
	void StartMatchTimer();

	/**
	 * 核心函数: 每秒触发一次，扣减时间
	 */
	UFUNCTION()
	void OnMatchTimerTick();

	/**
	 * 核心函数: 处理时间耗尽的宏观逻辑（结束本局或进下一回合）
	 */
	void HandleMatchTimeOut();

	/**
	 * 生化模式回合结束处理
	 */
	void HandleZombieRoundEnd();

	/**
	 * 生化模式进入下一回合
	 */
	UFUNCTION(BlueprintCallable, Category = "MetalSlug|Match")
	void StartNextZombieRound();

	// ==========================================
	// 攻守双方出生点管理系统
	// ==========================================

	/**
	 * @brief 在游戏开始时扫描地图中的所有 PlayerStart，按名称前缀分类存储
	 * 自动识别 "Attack" 前缀为攻方出生点，"Defense" 前缀为守方出生点
	 * @param bReScan 是否强制重新扫描（默认只在首次或切换地图时扫描）
	 */
	UFUNCTION(BlueprintCallable, Category = "MetalSlug|Spawn")
	void ScanAndCachePlayerStarts(bool bReScan = false);

	/**
	 * @brief 根据玩家所属队伍获取一个未被占用的出生点
	 * 复活时使用：优先分配未被占用的点，如果都用过了则随机分配
	 * @param PlayerTeam 玩家所属队伍
	 * @param bRemoveOccupied 分配后是否标记该点为已占用
	 * @return 返回一个可用的出生点 Actor，如果找不到则返回 nullptr
	 */
	UFUNCTION(BlueprintCallable, Category = "MetalSlug|Spawn")
	class AActor* GetAvailableSpawnPointForTeam(ERoomTeam PlayerTeam, bool bRemoveOccupied = true);

	/**
	 * @brief 当玩家离开（断开连接或退出房间）时，释放其占用的出生点
	 * @param PlayerStart 要释放的出生点
	 */
	UFUNCTION(BlueprintCallable, Category = "MetalSlug|Spawn")
	void ReleaseSpawnPoint(class AActor* PlayerStart);

	/**
	 * @brief 强制重置所有出生点的占用状态（在每回合/每局开始时调用）
	 */
	UFUNCTION(BlueprintCallable, Category = "MetalSlug|Spawn")
	void ResetAllSpawnPointOccupancy();

	/**
	 * @brief 获取玩家生成数据缓存的接口（供 BaseCharacter 复活时使用）
	 * @param ControllerUniqueID 控制器唯一ID
	 * @param OutCharID 输出: 角色ID
	 * @param OutWeaponID 输出: 武器ID
	 * @return 是否成功获取
	 */
	bool GetPlayerSpawnData(uint32 ControllerUniqueID, FString& OutCharID, FString& OutWeaponID) const;

protected:
	/**
	 * 攻方（Attack）出生点列表
	 */
	UPROPERTY()
	TArray<class APlayerStart*> AttackSpawnPoints;

	/**
	 * 守方（Defense）出生点列表
	 */
	UPROPERTY()
	TArray<class APlayerStart*> DefenseSpawnPoints;

	/**
	 * 已占用的出生点集合（使用 Set 便于快速查找和去重）
	 */
	UPROPERTY()
	TSet<class APlayerStart*> OccupiedSpawnPoints;

	/**
	 * 出生点扫描标记（防止重复扫描）
	 */
	UPROPERTY()
	bool bSpawnPointsScanned = false;
};
