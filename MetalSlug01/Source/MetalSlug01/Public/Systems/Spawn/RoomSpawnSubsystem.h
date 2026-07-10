// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

// ==========================================
// URoomSpawnSubsystem — 房间生成子系统
//
// 【2026.07.11 v31 大厂架构重构】从 RoomGameMode 拆出
//
// 设计原则:
//   - 单一职责: 所有 Pawn 生成/出生点分配
//   - 不持有业务状态: 比赛状态归 URoomLifecycleSubsystem
//   - 不管玩家入队: 玩家/AI 入队逻辑归 URoomMembershipSubsystem
//   - AI Spawn 单一入口: 不管大厅开局/战斗 Spawn/死亡复活, 全走 SpawnAIInternal
//
// 职责清单:
//   - 出生点扫描/分类 (ScanAndCachePlayerStarts)
//   - 出生点分配/释放 (GetAvailableSpawnPointForFaction/ReleaseSpawnPoint)
//   - AI Spawn (SpawnAIInternal, 单一入口支持新建/复用 Controller)
//   - 玩家 Spawn (HandlePlayerRequestSpawn)
//   - AI 大厅入队 (QueueAIForBattleSpawn + 查询 API)
//   - 玩家生成数据缓存 (PlayerSpawnDataCache)
//
// 大厂原则 - 单一真理源:
//   - 所有 Spawn 点分配走本 Subsystem, 不允许 RoomGameMode 直读 AttackSpawnPoints/DefenseSpawnPoints
//   - AI 生成走 SpawnAIInternal 单一入口, 不允许 RoomGameMode 直调 SpawnActor
//
// 访问入口:
//   URoomSpawnSubsystem* SpawnSys = URoomSpawnSubsystem::Get(this);
// ==========================================

// UE 引擎基础
#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "GameplayTagContainer.h"
#include "Engine/DataAsset.h"

// AI 行为类型 (FAISpawnRequest / FPendingAIEntry / FAIModeRules / ERoomMatchMode)
#include "Systems/AI/AIBehaviorTypes.h"

// 【v54 大厂架构重构】关卡预放 AI 默认配置走 ConfigSO (不再走 Profile)
#include "Data/AI/AIBehaviorConfigSO.h"

// Faction / 阵营系统 (FGameplayTag 阵营协议 — Profile.FactionTag 等)
#include "Data/Enums/RoomEnums.h"
#include "Data/Faction/FactionTags.h"

// 自动生成的反射头 — 必须放在所有 #include 之后, forward declaration 之前
// 大厂注意 (UE 5.6 UHT 严格模式): .generated.h 必须用裸文件名, 不能带目录前缀
//   UHT Parser 中: includeNameString 跟 GeneratedHeaderFileName 做字面 OrdinalIgnoreCase 比对
//   "Systems/Spawn/RoomSpawnSubsystem.generated.h" 永远 != "RoomSpawnSubsystem.generated.h"
#include "RoomSpawnSubsystem.generated.h"

// ==========================================
// 前向声明 — 避免在本头中 include 完整定义
// ==========================================
class ABaseCharacter;
class ABaseWeapon;
class ABaseAIController;
class AAIController;
class AController;
class APlayerController;
class APlayerStart;
class UDataTable;

UCLASS()
class METALSLUG01_API URoomSpawnSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	// ==========================================
	// 生命周期
	// ==========================================

	/**
	 * 标准 UE Subsystem 访问入口
	 */
	static URoomSpawnSubsystem* Get(const UObject* WorldContextObject);

	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

	// ==========================================
	// 配置入口 (由 GameMode 在构造时注入)
	// ==========================================

	/**
	 * 注入角色数据表 (DT_CharacterInfo)
	 */
	void SetCharacterDataTable(UDataTable* InTable) { CharacterDataTable = InTable; }

	/**
	 * 注入武器数据表 (DT_WeaponInfo)
	 */
	void SetWeaponDataTable(UDataTable* InTable) { WeaponDataTable = InTable; }

	/**
	 * 【v54.3 大厂架构 — 武器 Class 翻译器】WeaponID (FString) → WeaponClass (TSubclassOf<ABaseWeapon>)
	 *
	 * 用户原话 2026.07.16:
	 *   "DefaultWeaponRowName 属性为什么不直接引用 DT_WeaponInfo 表的 WeaponBlueprint 内容啊"
	 *   → 关卡预放 AI 改用 TSoftClassPtr 直接持 BP (零中间层)
	 *   → 大厅/玩家路径仍用 WeaponID 字符串 (UI 真理源), 但生成武器 Class 走这个 helper
	 *
	 * 调用方:
	 *   - SpawnAIInternal (大厅 AI 路径)
	 *   - HandlePlayerRequestSpawn (玩家路径)
	 *   - RequestRespawn (复活路径, WeaponID 来自 Controller.CachedWeaponID)
	 *
	 * 大厂原则:
	 *   - 单一反查入口: 全项目 WeaponID → WeaponClass 只此一处
	 *   - 不缓存 WeaponClass (软引用/Instance 自动管理)
	 *   - DT_WeaponInfo 查表失败 → Log Error + return nullptr (零兜底)
	 *   - 这不是兜底, 是显式翻译器 — 失败调用方拒绝 Spawn
	 *
	 * 数据流:
	 *   DT_WeaponInfo[RowName=WeaponID].WeaponBlueprint.LoadSynchronous() → WeaponClass
	 */
	TSubclassOf<class ABaseWeapon> ResolveWeaponClassFromID(const FString& WeaponID) const;

	/**
	 * 【v41 大厂架构】注入玩家角色战斗参数配置资产 (DA_PlayerConfig)
	 *
	 * 用途:
	 *   - MaxHealth / MaxEnergy / RespawnDelay / SpawnInvincibility 等参数
	 *   - 由 HandlePlayerRequestSpawn / SpawnAIInternal 读取并初始化各组件
	 */
	void SetPlayerConfigAsset(class UPlayerConfigAsset* InAsset) { PlayerConfigAsset = InAsset; }

	/**
	 * 【v41 大厂架构】从配置资产读取并应用角色参数到角色
	 *
	 * 职责:
	 *   - 从 DA_PlayerConfig 读取战斗参数
	 *   - 应用到角色的 HealthComponent / EnergyComponent / HealthRegenComponent
	 *   - 设置 BaseCharacter 的复活/奖励相关字段
	 *
	 * 调用方:
	 *   - HandlePlayerRequestSpawn (玩家 Spawn 成功后)
	 *   - SpawnAIInternal (AI Spawn 成功后)
	 *
	 * @param Character 目标角色 (必须已 Spawn)
	 */
	void ApplyCharacterConfigToCharacter(ABaseCharacter* Character);

	/**
	 * 【v54 大厂架构重构】注入 AI 默认配置
	 *
	 * 历史 (v53 及之前):
	 *   - 注入 ProfilesByMode (二级 TMap 包装的 Profile 注册表) + DefaultProfileTag
	 *   - 依赖 UAIProfileAsset + FAIProfileRegistry (中间层)
	 *
	 * v54.2 重构 (用户决策 2026.07.16 — 删死代码 + 单一真理源):
	 *   - UAIProfileAsset 整个类已删除
	 *   - ProfilesByMode / DefaultProfileTag 整个字段删除
	 *   - DefaultControllerClass 死代码删除 (此字段没有任何 SpawnAIInternal 调用点引用)
	 *   - 仅保留 ModeRulesByMode (运行时需要, 攻/守方 Faction 映射)
	 *   - 关卡预放 AI 走 ConfigSO (BaseAIController.GetConfig() 直接读, 不需要 Subsystem 中转)
	 *   - 大厅入队 AI 走 Request (UI 直接传武器/AIController, 不需要 Subsystem 反查)
	 *
	 * 大厂原则:
	 *   - 真理源不分裂 (一个 AI 类型只有一个 ConfigSO, 没有 Profile 中间层)
	 *   - 删除中间层 = 删除配置反模式 + 删除反查链
	 *   - 删除死代码 = 严禁"可能用到" 妄想, 真用就订阅, 没用就删
	 */
	void SetModeRules(const TMap<ERoomMatchMode, FAIModeRules>& InModeRules)
	{
		ModeRulesByMode = InModeRules;
	}

	// 【v56 诊断用】打印已配置的 ModeRules 键列表
	FString DumpModeRulesKeys() const;

	// ==========================================
	// 出生点管理
	// ==========================================

	/**
	 * @brief 扫描并缓存地图中所有 PlayerStart, 按 PlayerStartTag 分类
	 * @param bReScan 强制重新扫描 (默认只在首次扫描)
	 *
	 * 分类规则:
	 *   - "Faction_Offense" tag → 攻方 (AttackSpawnPoints)
	 *   - "Faction_Defense" tag → 守方 (DefenseSpawnPoints)
	 *   - 旧 "Faction_Attack" → 兼容接收 + Log Warning
	 *   - 无 tag / 不匹配 → Log Error + 跳过 (零兜底)
	 */
	UFUNCTION(BlueprintCallable, Category = "Room|Spawn")
	void ScanAndCachePlayerStarts(bool bReScan = false);

	/**
	 * @brief 根据阵营获取一个未被占用的出生点
	 * @param PlayerFactionTag Faction.Offense 或 Faction.Defense
	 * @param bRemoveOccupied 是否标记已占用
	 * @return 出生点 Actor, 找不到返回 nullptr
	 *
	 * 【v39 修复】OccupancyOwner: 传入占用者 Controller (用于大厂原则 - 集中调度精准释放)
	 *   - Spawn 时记录 Controller → PlayerStart 的映射
	 *   - 死亡时通过 Controller 反查释放 (避免 ResetAll 误清空)
	 */
	UFUNCTION(BlueprintCallable, Category = "Room|Spawn")
	AActor* GetAvailableSpawnPointForFaction(FGameplayTag PlayerFactionTag, bool bRemoveOccupied = true, AController* OccupancyOwner = nullptr);

	/**
	 * @brief 【v39 新增】按 Controller 释放出生点 (集中调度精准释放)
	 *
	 * 大厂原则:
	 *   - 死亡链路唯一释放入口 (UCombatDeathComponent::ReleaseOccupiedSpawnPoint)
	 *   - 通过 Controller 反查上次 Spawn 时的 PlayerStart, 精准释放
	 *   - 不会误清空其他玩家的占用
	 */
	UFUNCTION(BlueprintCallable, Category = "Room|Spawn")
	void ReleaseSpawnPointByController(AController* Controller);

	/**
	 * @brief 释放出生点
	 */
	UFUNCTION(BlueprintCallable, Category = "Room|Spawn")
	void ReleaseSpawnPoint(AActor* PlayerStart);

	/**
	 * @brief 重置所有出生点占用 (每回合/每局开始)
	 */
	UFUNCTION(BlueprintCallable, Category = "Room|Spawn")
	void ResetAllSpawnPointOccupancy();

	// ==========================================
	// AI Spawn (单一入口)
	// ==========================================

	/**
	 * @brief AI 生成 (单一入口)
	 * @param Request FAISpawnRequest (含 UI 传入的武器/AIController)
	 * @param Config 关卡预放 AI 时传入 ConfigSO, 大厅 AI 时可为 nullptr (走 Request)
	 * @param OptionalExistingController 复活场景传入, nullptr 表示新建 AIC
	 * @return 实际生成数
	 *
	 * 【v54 大厂架构重构】Profile 参数改 ConfigSO
	 *   - 关卡预放 AI: 必须传 ConfigSO (从 AIC.GetConfig() 来) — 零兜底
	 *   - 大厅入队 AI: Config 可为 nullptr, 直接走 Request (UI 直接传入武器/AIController)
	 *   - 不再走 UAIProfileAsset, 不再走反查链
	 */
	UFUNCTION(BlueprintCallable, Category = "Room|Spawn")
	int32 SpawnAIInternal(const FAISpawnRequest& Request, UAIBehaviorConfigSO* Config,
		AAIController* OptionalExistingController = nullptr);

	/**
	 * @brief 查 ModeRules
	 * @return 是否找到 (找不到时 OutRules 不被填充)
	 */
	bool GetModeRules(ERoomMatchMode Mode, FAIModeRules& OutRules) const;

	// ==========================================
	// AI 大厅入队 (v28)
	// ==========================================

	UFUNCTION(BlueprintCallable, Category = "Room|Spawn")
	int32 QueueAIForBattleSpawn(const FAISpawnRequest& Request);

	UFUNCTION(BlueprintPure, Category = "Room|Spawn")
	TArray<FPendingAIEntry> GetPendingAIInFaction(FGameplayTag FactionTag) const;

	UFUNCTION(BlueprintPure, Category = "Room|Spawn")
	TArray<FPendingAIEntry> GetAllPendingAI() const { return PendingAIQueue; }

	UFUNCTION(BlueprintPure, Category = "Room|Spawn")
	bool IsPendingAIByName(const FString& DisplayName) const;

	UFUNCTION(BlueprintCallable, Category = "Room|Spawn")
	bool RemovePendingAIByName(const FString& DisplayName);

	/**
	 * 消费队列转 SpawnRequest 列表 (一次性清空)
	 */
	TArray<FAISpawnRequest> ConsumePendingAIForBattleSpawn();

	// ==========================================
	// 玩家 Spawn
	// ==========================================

	/**
	 * @brief 玩家 Spawn 主入口
	 * @param PlayerToSpawn 目标玩家 Controller
	 * @param CharRowName DT_CharacterInfo 行名
	 * @param WeaponRowName DT_WeaponInfo 行名
	 */
	UFUNCTION(BlueprintCallable, Category = "Room|Spawn")
	void HandlePlayerRequestSpawn(AController* PlayerToSpawn, const FString& CharRowName, const FString& WeaponRowName);

	/**
	 * @brief 倒计时结束后触发 — 遍历 GS->PlayerArray 调 HandlePlayerRequestSpawn
	 */
	UFUNCTION(BlueprintCallable, Category = "Room|Spawn")
	void SpawnAllPlayersIntoBattle();

	/**
	 * @brief 引擎 override: Spawn 前询问 Pawn Class
	 * 业务下沉到本 Subsystem, RoomGameMode 只做壳转发
	 */
	UFUNCTION(BlueprintCallable, Category = "Room|Spawn")
	TSubclassOf<APawn> GetDefaultPawnClassForController(AController* InController);

	/**
	 * @brief 引擎 override: RestartPlayer 业务实现
	 * 壳在 RoomGameMode, 这里做实际业务 (缓存检查 + 转发 HandlePlayerRequestSpawn)
	 */
	UFUNCTION(BlueprintCallable, Category = "Room|Spawn")
	void RestartPlayer(AController* NewPlayer);

	/**
	 * @brief 玩家/AI 复活入口
	 * @param DeadController 已死亡的 Controller
	 * @param bImmediateRespawn true=立即, false=延迟
	 */
	UFUNCTION(BlueprintCallable, Category = "Room|Spawn")
	void RequestRespawn(AController* DeadController, bool bImmediateRespawn);

	/**
	 * @brief 复活延迟秒数 (RoomGameMode 可调)
	 */
	void SetRespawnDelaySeconds(float InSeconds) { RespawnDelaySeconds = InSeconds; }

	/**
	 * @brief 查询是否正在 Spawn 中 (引擎 override 拦截用)
	 */
	bool IsSpawnInProgress() const { return bSpawnInProgress; }

	// ==========================================
	// 玩家生成数据缓存 (供复活用)
	// ==========================================

	struct FPlayerSpawnData
	{
		FString CharID;
		FString WeaponID;
	};

	bool GetPlayerSpawnData(uint32 ControllerUniqueID, FString& OutCharID, FString& OutWeaponID) const;

	/**
	 * 写入玩家生成数据 (由 GameMode::AddPlayerToRoom 调用)
	 */
	void SetPlayerSpawnData(uint32 ControllerUniqueID, const FString& CharID, const FString& WeaponID);

	// ==========================================
	// AI 命名序号 (大厅入队用)
	// ==========================================

	int32 AllocateAINextID() { return ++AINextID; }
	int32 AllocatePendingAISequenceID() { return ++PendingAISequenceID; }

	// ==========================================
	// 公共数据 (供 GameMode 查询)
	// ==========================================

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Room|Spawn")
	FName TAG_Faction_Offense;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Room|Spawn")
	FName TAG_Faction_Defense;

	// ==========================================
	// 转换辅助方法 (public 让 RoomGameMode 委派入口可调)
	// ==========================================

	/**
	 * @brief 把 FPendingAIEntry 转 FAISpawnRequest (大厅入队 → 战斗 Spawn 数据转换)
	 *
	 * 大厂原则 (v31.5):
	 *   - public 暴露给 RoomGameMode 委派入口 (避免重复写一遍转换逻辑)
	 *   - 内部 cpp 也用这个, 所以不能放 private
	 *   - 调用方应优先用 ConsumePendingAIForBattleSpawn(), 那个自动调这个
	 */
	FAISpawnRequest BuildSpawnRequestFromPending(const FPendingAIEntry& Entry) const;

	// 【v56 新增】扫描场景中的关卡预放 AI 并缓存其阵营
	//   调用时机: PerformGameStart 前
	//   扫描: 所有 ABaseCharacter 子类 Pawn
	//   缓存: PawnClass.GetName() → Pawn.FactionTag
	UFUNCTION(BlueprintCallable, Category = "AI")
	void ScanAndCacheLevelPlacedAIFactions();

	// 【v56 新增】查询关卡预放 AI 的阵营 (SetupMeleeAI 调用)
	//   先查缓存, 缓存没有则从 Pawn.FactionTag 读取
	UFUNCTION(BlueprintCallable, Category = "AI")
	FGameplayTag GetCachedLevelPlacedAIFaction(TSubclassOf<AActor> AIPawnClass, ABaseCharacter* AIPawn) const;

protected:
	// ==========================================
	// 内部状态
	// ==========================================

	UPROPERTY()
	TArray<APlayerStart*> AttackSpawnPoints;

	UPROPERTY()
	TArray<APlayerStart*> DefenseSpawnPoints;

	UPROPERTY()
	TSet<APlayerStart*> OccupiedSpawnPoints;

	UPROPERTY()
	bool bSpawnPointsScanned = false;

	// 数据表 (由 GameMode 注入)
	UPROPERTY()
	TObjectPtr<UDataTable> CharacterDataTable;

	UPROPERTY()
	TObjectPtr<UDataTable> WeaponDataTable;

	/**
	 * 【v41 大厂架构】玩家角色战斗参数配置资产 (DA_PlayerConfig)
	 *
	 * 由 GameMode 通过 SetPlayerConfigAsset 注入
	 */
	UPROPERTY()
	TObjectPtr<class UPlayerConfigAsset> PlayerConfigAsset;

	// 【v54.2 大厂架构】AI 运行时配置 (由 GameMode 注入) — ProfilesByMode/DefaultProfileTag/DefaultControllerClass 已全删除
	UPROPERTY()
	TMap<ERoomMatchMode, FAIModeRules> ModeRulesByMode;

	// AI 大厅入队
	UPROPERTY()
	TArray<FPendingAIEntry> PendingAIQueue;

	int32 AINextID = 1;
	int32 PendingAISequenceID = 0;

	// 玩家生成数据缓存
	TMap<uint32, FPlayerSpawnData> PlayerSpawnDataCache;

	// 复活延迟秒数 (RoomGameMode 注入, 大厂原则: 数据从 GameMode 流到 Subsystem)
	float RespawnDelaySeconds = 3.0f;

	// 是否正在 Spawn 中 (引擎 override 拦截用)
	bool bSpawnInProgress = false;

	// 【v39 新增】出生点占用 SSOT — Controller → PlayerStart 映射
	//   大厂原则: 死亡时通过 Controller 反查释放, 不误清空其他玩家占用
	//   真理源: GetAvailableSpawnPointForFaction 写入, ReleaseSpawnPointByController 清除
	UPROPERTY()
	TMap<TWeakObjectPtr<AController>, TWeakObjectPtr<APlayerStart>> OccupiedSpawnByController;

	// 【v56 新增】关卡预放 AI 阵营缓存 — PawnClass → FactionTag 映射
	//   设计决策: 阵营从角色类 (Pawn BP) 的 FactionTag 属性获取
	//   扫描时机: PerformGameStart 前 (BattleStarted 广播前)
	//   用途: AIController::SetupMeleeAI 从这里获取关卡预放 AI 的阵营
	UPROPERTY()
	TMap<FString, FGameplayTag> LevelPlacedAIFactionByClassName;
};