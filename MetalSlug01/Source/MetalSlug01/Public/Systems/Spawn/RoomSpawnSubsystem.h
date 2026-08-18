// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

// ==========================================
// URoomSpawnSubsystem — 房间生成子系统
//
// @file RoomSpawnSubsystem.h
// @brief 房间生成子系统 — Pawn 生成/出生点分配唯一入口
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
	// 【v230 大厂架构新增】AI 母体变人类分阶段处理数据结构
	// ==========================================
	//
	// 根因 (用户 2026.08.18 反馈):
	//   "偶尔有时候进入新的小局，会有母体出现，没有把所有母体全部变成人类"
	//
	// 触发链:
	//   1. RestartZombieRoundPlayers 使用 TActorIterator 遍历所有角色
	//   2. 遍历中 Destroy + Spawn 导致迭代器捕获新 Spawn 的 Pawn
	//   3. 新 Spawn 的 Pawn 在本循环中被错误处理
	//
	// 修复: 分两阶段遍历
	//   阶段 2a: 收集需要销毁重生的母体 AI 信息 (AIController 作为唯一标识符)
	//   阶段 2b: 执行销毁重生 (新 Spawn 的 Pawn 不在本循环中被处理)
	//   阶段 2c: 处理"已经是人类"的 AI
	//
	struct FPendingDemuteInfo
	{
		TWeakObjectPtr<class ABaseCharacter> OldPawn;    // 旧母体 Pawn
		FVector SpawnLoc;                                // 新 Spawn 位置
		FRotator SpawnRot;                               // 新 Spawn 旋转
		FString HumanWeaponID;                           // 人类武器 ID
	};

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
	 * 【v220 大厂架构 — 异步预加载武器资产】消除 32 秒主线程阻塞
	 *
	 * 业务背景 (Session1.txt 2026.08.09):
	 *   "监听服务器点击开始游戏后客户端直接卡死" — 客户端武器 BeginPlay 触发
	 *   LoadSynchronous() 同步加载 SM_Modern_Melee_ShovelLarge (Marketplace asset),
	 *   首次启动无 DDC → 编译阻塞主线程 32 秒
	 *
	 * 根因 (大厂架构根因):
	 *   - ResolveWeaponClassFromID 用 TSoftClassPtr::LoadSynchronous() — 强制同步加载
	 *   - 5 个 Spawn 入口 (HandlePlayerRequestSpawn, SpawnAIInternal, RequestRespawn 玩家/AI, RestartZombieRoundPlayers)
	 *     全部在 Tick 路径上调用, 第一次执行就是同步加载
	 *   - 客户端第一次启动, DDC 没 Marketplace asset 编译缓存 → 编译阻塞 30+ 秒
	 *
	 * 修复 (大厂架构 — 异步预加载):
	 *   - PerformGameStart 倒计时阶段 (MatchStartDelay 默认 3s) 调用本函数
	 *   - 异步遍历 PlayerArray + AI Profile, 收集所有 WeaponBlueprint 软引用
	 *   - StreamableManager.RequestAsyncLoad 一次性加载所有 → DDC 编译在后台进行
	 *   - 倒计时结束 Spawn 时, LoadSynchronous() 命中缓存 → 0 阻塞
	 *
	 * 大厂原则:
	 *   - 单一预加载入口: 全项目只有 PerformGameStart 调一次, 不在每个 Spawn 路径重复
	 *   - 零兜底: 不收集到的武器会在 LoadSynchronous 时报 Error, 用户主动配 (符合现有 ResolveWeaponClassFromID 行为)
	 *   - 不阻塞主线程: RequestAsyncLoad 是 fire-and-forget, 不等结果
	 *
	 * @return 预加载请求数 (用于诊断日志)
	 */
	UFUNCTION(BlueprintCallable, Category = "Spawn|Preload")
	int32 PreloadWeaponMeshesAsync();

	/**
	 * 【v209 / v212 大厂架构 — 默认武器兜底器】从 DT_WeaponInfo 取第 RowIndex 行的 RowName
	 *
	 * ⚠️ v212 状态: 此函数已**部分废弃** — 近战武器兜底已改用 FRoomLoadoutDefaults::MeleeDefaultRowName (JZ001)
	 *   当前仍保留供: 主武器兜底 (玩家没存档 + DT 第 1 行 = 主武器默认)
	 *
	 * 业务背景 (用户 2026.08.08 / 2026.08.09):
	 *   v209 (2026.08.08): "玩家主武器/近战武器, 在玩家没可以选择的状况下, 默认就第一把武器带入游戏"
	 *     → RowIndex=0 (主武器默认) 保留
	 *   v212 (2026.08.09): "玩家如果没选近战武器, 那就默认使用 DT_WeaponInfo 的 RowName=JZ001 的武器"
	 *     → 近战武器改用 FRoomLoadoutDefaults::MeleeDefaultRowName (JZ001), 不再用 RowIndex=1
	 *
	 * 调用方:
	 *   - HandlePlayerRequestSpawn: 主武器 Slot 1 为空时 → ResolveDefaultWeaponRowName(0) 兜底 (v209 保留)
	 *   - HandlePlayerRequestSpawn: 近战武器 Slot 3 为空时 → 用 FRoomLoadoutDefaults::MeleeDefaultRowName (v212 重构)
	 *   - UI RoomInsidePage (NativeConstruct): 主武器存档为空时 (历史兜底, 已存在)
	 *
	 * 大厂原则 (单一真理源 + 业务默认值 ≠ 配置兜底):
	 *   - v209 真理源: DT_WeaponInfo 行序 (项目级, 策划改 DT 第 N 行 = 改默认武器)
	 *   - v212 真理源: FRoomLoadoutDefaults (业务默认值显式声明, 不依赖 DT 行序)
	 *   - 这是**业务默认** (玩家没选), 不是**配置兜底** (RowName 配错)
	 *   - 兜底只发生在运行时 Spawn 链, **不写回 PS.SelectedWeaponID** (避免误导 UI 显示"已选")
	 *
	 * 不破坏 v54.3 零兜底原则:
	 *   - v54.3 零兜底针对 "RowName 配错" (Log Error + return nullptr)
	 *   - v209 / v212 默认兜底针对 "玩家没选武器" (Log Warning + return DT 第 N 行 / JZ001)
	 *   - 两者场景不同, 不冲突
	 *
	 * @param RowIndex  DT_WeaponInfo 的行序号 (0 = 第 1 行 = 主武器默认; 1 = 已废弃, 用 FRoomLoadoutDefaults::MeleeDefaultRowName)
	 * @return          兜底 RowName (非空); DT 为空/行数不足 → 返回空字符串
	 */
	UFUNCTION(BlueprintCallable, Category = "Room|Spawn")
	FString ResolveDefaultWeaponRowName(int32 RowIndex) const;

	/**
	 * 【v41 大厂架构】注入玩家角色战斗参数配置资产 (DA_PlayerConfig)
	 *
	 * 用途:
	 *   - MaxHealth / MaxEnergy / RespawnDelay / SpawnInvincibility 等参数
	 *   - 由 HandlePlayerRequestSpawn / SpawnAIInternal 读取并初始化各组件
	 */
	void SetPlayerConfigAsset(class UPlayerConfigAsset* InAsset) { PlayerConfigAsset = InAsset; }

	/**
	 * 【v133.4.1 2026.08.02 大厂架构 — 真理源唯一入口】获取 PlayerConfigAsset 直指针
	 *
	 * 为什么需要这个 getter:
	 *   - GM.PlayerConfigAsset 是 TSoftObjectPtr (只配资产引用)
	 *   - SpawnSubsystem.PlayerConfigAsset 才是 TObjectPtr 直指针 (GM 注入进来)
	 *   - 业务层要读资产字段 (如 MotherMaxHealth), 必须经过这个 getter — 不许乱读 TSoftObjectPtr
	 *
	 * 调用方:
	 *   - URoomMotherMutationSubsystem::MutateCharacterToMother Step 3.6 (母体血量验证)
	 *   - 任何需要读 PlayerConfigAsset 业务字段的地方
	 */
	class UPlayerConfigAsset* GetPlayerConfigAsset() const { return PlayerConfigAsset; }

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
	 * 【v133.4 2026.08.02 大厂架构】应用 AI ConfigSO 配置到 AI Pawn
	 *
	 * 真理源分离:
	 *   - ApplyCharacterConfigToCharacter: 玩家 Pawn 读 PlayerConfigAsset
	 *   - ApplyAICharacterConfigToCharacter: AI Pawn 读 ConfigSO.AIBehaviorConfig
	 *
	 * 职责分工:
	 *   - 玩家 vs AI 是两个独立真理源 (大厂原则 — 单一真理源 + 职责对等)
	 *   - 不允许 AI 用 PlayerConfigAsset (会污染真理源)
	 *
	 * 设计:
	 *   - ConfigSO 走 BaseAIController->GetConfig() 取得 (运行时)
	 *   - 真理源 = ConfigSO.Health.MaxHealth / MotherMaxHealth (按 bIsMother 分流)
	 *   - 其他字段 (无敌期、武器等) 由 SetupMeleeAI / SpawnAIInternal 各处自己负责
	 *
	 * 调用方:
	 *   - URoomSpawnSubsystem::SpawnAIInternal (大厅 AI Spawn 成功后)
	 *   - AMeleeAIController::SetupMeleeAI 末尾 (关卡预放 AI)
	 *   - URoomSpawnSubsystem::MutatePawnToMother 末尾 (母体 AI 复活)
	 *
	 * @param Character AI Pawn (必须已 Spawn)
	 */
	void ApplyAICharacterConfigToCharacter(ABaseCharacter* Character);

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
	 * 【v201 大厂架构新增】获取生化模式人类专用复活点
	 *
	 * 背景:
	 *   - 生化模式人类玩家需要从 HumanSurvivorSpawnPoints 中分配
	 *   - 不能用 GetAvailableSpawnPointForFaction，因为它走 Offense/Defense 阵营复活点
	 *
	 * 大厂原则 — 零兜底:
	 *   - HumanSurvivorSpawnPoints 为空 → Log Error + return nullptr
	 *   - 所有点都被占用 → Log Error + return nullptr
	 *
	 * @param OccupancyOwner 占用者 Controller (用于释放时精准定位)
	 * @return 可用的 HumanSurvivor 复活点，如果找不到则返回 nullptr
	 */
	UFUNCTION(BlueprintCallable, Category = "Room|Spawn")
	AActor* GetAvailableHumanSurvivorSpawnPoint(AController* OccupancyOwner);

	/**
	 * 【v201 大厂架构新增】小局结束后重新分配所有人类玩家到 HumanSurvivor 复活点
	 *
	 * 业务场景:
	 *   - 生化模式小局结束
	 *   - 所有存活人类玩家需要重新分配到新的 HumanSurvivor 复活点
	 *   - 死亡玩家在复活时自动走新复活点
	 *
	 * 调用链:
	 *   - URoomLifecycleSubsystem::StartNextZombieRound 末尾调用
	 *
	 * 大厂原则 — 集中调度:
	 *   - 单一入口管理所有人类玩家的复活点重分配
	 *   - 不破坏刀战模式（刀战不走本函数）
	 */
	UFUNCTION(BlueprintCallable, Category = "Room|Spawn")
	void RestartZombieRoundPlayers();

	/**
	 * @brief 【v210 大厂架构新增】延迟弹药重置回调
	 *
	 * 用途:
	 *   - RestartZombieRoundPlayers 末尾设置 0.1s 延迟 Timer
	 *   - 确保武器完全 Attach 后再调用 Server_RefillAmmo
	 *   - 避免 GetAttachedCharacter() 返回 nullptr 导致 RPC 推送失败
	 */
	void OnDelayedAmmoRefill();

	/**
	 * @brief 【v39 新增】按 Controller 释放出生点 (集中调度精准释放)
	 *
	 * 大厂原则:
	 *   - 死亡链路唯一释放入口 (UCombatDeathComponent::ReleaseOccupiedSpawnPoint)
	 *   - 通过 Controller 反查上次 Spawn 时的 PlayerStart, 精准释放
	 *   - 不会误清空其他玩家的占用
	 */
	// ==========================================
	// 【v242 大厂架构重构】占点 + 映射登记单一入口
	// ==========================================

	/**
	 * @brief 原子占点 + 映射登记 (单一入口, v242 P0 修复)
	 *
	 * 大厂原则 — 零兜底 + 单一入口:
	 *   - 所有占用出生点逻辑必须走这里
	 *   - 同时维护 OccupiedSpawnPoints (数组) + OccupiedSpawnByController (Map)
	 *   - 两表一致性保证: 总是同时 Add,Always Remove
	 *
	 * 入参校验 (零兜底):
	 *   - SpawnPoint == nullptr → Log Error + return false
	 *   - OccupancyOwner == nullptr → Log Error + return false (首次 Spawn 必须等 AIC 创建后调)
	 *
	 * 重复占点防御:
	 *   - Controller 已占过其他点 → 撤销旧映射,重新占新点
	 *   - SpawnPoint 已被占 → Log Error + return false (双重占点 bug)
	 *
	 * @param SpawnPoint 要占用的出生点 (不能为 nullptr)
	 * @param OccupancyOwner 占点 Controller (不能为 nullptr)
	 * @return true 占点成功,false 占点失败
	 */
	UFUNCTION(BlueprintCallable, Category = "Room|Spawn")
	bool ClaimSpawnPointAndRegisterMapping(APlayerStart* SpawnPoint, AController* OccupancyOwner);

	/**
	 * @brief 【v242 零兜底 — 精准释放】按 Controller 反查释放其占用的出生点
	 *
	 * 调用方: CombatDeathComponent::ExecuteDeathLocal (死亡链路必经)
	 *
	 * 大厂原则 — 细粒度 > 粗粒度:
	 *   - 旧版 ReleaseSpawnPoint(AActor*) 在多玩家同帧死亡时会误清空
	 *   - 新版按 Controller 精准释放,互不干扰
	 *
	 * 双表一致性: 同时维护 OccupiedSpawnPoints + OccupiedSpawnByController
	 *
	 * @param Controller 已死的玩家/AI Controller (nullptr → no-op)
	 */
	UFUNCTION(BlueprintCallable, Category = "Room|Spawn")
	void ReleaseSpawnPointByController(AController* Controller);

	/**
	 * @brief 【v246 大厂架构】释放所有出生点占用 — 语义化入口
	 *
	 * 用途:
	 *   - ARoomGameMode::ResetAllSpawnPointOccupancy() 转发壳内部使用
	 *   - 任何需要"清空所有占用"的合法场景 (如整局重置)
	 *
	 * 大厂原则 — 暴露行为, 不暴露数据结构:
	 *   - 内部复制 OccupiedSpawnByController keys, 逐个调 ReleaseSpawnPointByController
	 *   - 调用方 (RoomGameMode) 不需要知道 OccupiedSpawnByController 是 TMap 还是 TSet
	 *   - 隐藏实现细节, 后续重构 TMap → TArray 时不影响调用方
	 *
	 * 双表一致性保证:
	 *   - ReleaseSpawnPointByController 内部已维护 OccupiedSpawnPoints + OccupiedSpawnByController 双表一致
	 *   - 本函数循环调用 ReleaseSpawnPointByController, 双表自动一致
	 *
	 * 零兜底:
	 *   - OccupiedSpawnByController 已为空 → no-op (合法)
	 *   - Controller 已 Destroy (TWeakObjectPtr.Get() 返回 nullptr) → 跳过 (不报错)
	 *
	 * @return 实际释放的 Controller 数量 (0 = 无占用)
	 */
	UFUNCTION(BlueprintCallable, Category = "Room|Spawn")
	int32 ReleaseAllSpawnPointOccupancy();

	/**
	 * @brief 【v246 大厂架构】按出生点释放所有占用此点的 Controller — 语义化入口
	 *
	 * 用途:
	 *   - ARoomGameMode::ReleaseSpawnPoint(AActor*) 转发壳内部使用
	 *   - 任何需要"释放此出生点所有占用"的合法场景 (如玩家放弃某点)
	 *
	 * 大厂原则 — 暴露行为, 不暴露数据结构:
	 *   - 内部反查 OccupiedSpawnByController, 找到所有映射此 PlayerStart 的 Controller
	 *   - 调 ReleaseSpawnPointByController 精准释放每个
	 *   - 调用方 (RoomGameMode) 不需要知道 OccupiedSpawnByController 是 TMap 还是 TSet
	 *
	 * 双表一致性保证:
	 *   - ReleaseSpawnPointByController 内部已维护 OccupiedSpawnPoints + OccupiedSpawnByController 双表一致
	 *
	 * 零兜底:
	 *   - PlayerStart 为空 → Log Warning + return 0
	 *   - PlayerStart 不是 APlayerStart → Log Warning + return 0
	 *   - 反查无 Controller 占用此点 → return 0 (合法)
	 *
	 * @param PlayerStart 要释放的出生点
	 * @return 实际释放的 Controller 数量 (0 = 无占用)
	 */
	UFUNCTION(BlueprintCallable, Category = "Room|Spawn")
	int32 ReleaseSpawnPointBySpawnPoint(AActor* PlayerStart);

	/**
	 * @brief 释放出生点
	 *
	 * 【v242 ZERO-FALLBACK 重构】DEPRECATED — 走 ReleaseSpawnPointByController 精准释放
	 *   保留只是为了兼容旧 BP 调用方,不再用于新代码
	 */
	UE_DEPRECATED(5.6, "【v242 零兜底】ReleaseSpawnPoint(AActor*) 是粗粒度接口, 请改用 ReleaseSpawnPointByController(AController*) 精准释放.")
	UFUNCTION(BlueprintCallable, Category = "Room|Spawn", meta = (DeprecatedFunction, DeprecationMessage = "【v242 零兜底】请改用 ReleaseSpawnPointByController(AController*) 精准释放, 避免多玩家同帧死亡误清空."))
	void ReleaseSpawnPoint(AActor* PlayerStart);

	/**
	 * @brief 重置所有出生点占用 (每回合/每局开始)
	 *
	 * 【v242 ZERO-FALLBACK 重构】DEPRECATED — 走精细化释放入口
	 *   保留只是为了兼容旧 BP 调用方,不再用于新代码
	 */
	UE_DEPRECATED(5.6, "【v242 零兜底】ResetAllSpawnPointOccupancy 是粗粒度接口, 请改用 ReleaseSpawnPointByController 精准释放每个 Controller.")
	UFUNCTION(BlueprintCallable, Category = "Room|Spawn", meta = (DeprecatedFunction, DeprecationMessage = "【v242 零兜底】请改用 ReleaseSpawnPointByController 精细化释放, 避免多玩家同帧死亡误清空."))
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

	/**
	 * @brief 按阵营拉取大厅占位 AI 列表 (UI 显示用)
	 *
	 * 大厂原则 — 单一真理源:
	 *   - UI 只读 PendingAIQueue, 不直接读 SpawnActor 后的 Pawn (AI 大厅阶段不生成 Pawn, 走 v28)
	 */
	UFUNCTION(BlueprintPure, Category = "Room|Spawn")
	TArray<FPendingAIEntry> GetPendingAIInFaction(FGameplayTag FactionTag) const;

	/**
	 * @brief 返回 PendingAIQueue 的常量引用 (供序列化/调试)
	 */
	UFUNCTION(BlueprintPure, Category = "Room|Spawn")
	TArray<FPendingAIEntry> GetAllPendingAI() const { return PendingAIQueue; }

	/**
	 * @brief 按 DisplayName 检查是否在大厅占位队列中 (大厂原则 — 字段驱动判定, 不用 StartsWith)
	 */
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
	 *
	 * 【v52 P0 扩展 3 槽位】主武器 + 副武器 + 近战武器
	 * - WeaponSecondaryRowName 留空 = 玩家没选副武器 (走主武器槽)
	 * - WeaponMeleeRowName 留空 = 玩家没选近战武器 (走主武器槽)
	 *
	 * @param PlayerToSpawn           目标玩家 Controller
	 * @param CharRowName             DT_CharacterInfo 行名
	 * @param WeaponPrimaryRowName    DT_WeaponInfo 主武器行名 (Slot 1)
	 * @param WeaponSecondaryRowName  DT_WeaponInfo 副武器行名 (Slot 2, 允许空)
	 * @param WeaponMeleeRowName      DT_WeaponInfo 近战武器行名 (Slot 3, 允许空)
	 */
	UFUNCTION(BlueprintCallable, Category = "Room|Spawn")
	void HandlePlayerRequestSpawn(AController* PlayerToSpawn, const FString& CharRowName, const FString& WeaponPrimaryRowName, const FString& WeaponSecondaryRowName, const FString& WeaponMeleeRowName);

	// ==========================================
	// 【v89 大厂架构新增】母体 Pawn 重建 — 业务层唯一入口
	// ==========================================
	//
	// 大厂原则 — 业务分层 (MR → v89 落地):
	//   - RoomMotherMutationSubsystem 旧实现: 占位代码直接设 bIsMother=true + 跳过 Pawn 重建
	//   - 新实现: 由本函数销毁旧 Pawn + 用 BP_MuTi 蓝图类 Spawn 新 Pawn + 重新 Possess
	//   - 母体变异是 Pawn 类变更, 业务语义上仍然属于"Spawn 调度", 走 SpawnSubsystem 单一入口
	//   - 避免散布"我自己 Destroy + Spawn" — 违反 SRP / 集中调度
	//
	// 调用方:
	//   - URoomMotherMutationSubsystem::MutateCharacterToMother (生化模式变异)

	/**
	 * @brief 母体变异 — 销毁旧人类 Pawn + Spawn BP_MuTi 蓝图类新 Pawn + 重新 Possess
	 *
	 * 大厂原则:
	 *   - 母体 RowName 强类型输入 (不允许空字符串, 零兜底)
	 *   - 母体可能无武器 (MotherCharRowName 配 BP_MuTi 蓝图类), 武器 SetSpawnLoadout 允许空
	 *   - 释放原 Pawn 占用出生点 (复用 ReleaseSpawnPointByController)
	 *   - 重新分配新出生点 (复用 GetAvailableSpawnPointForFaction)
	 *   - Controller->Possess 同步触发 Pawn.FactionTag 复制 (v27 链路, 阵营不变)
	 *
	 * @param Controller         目标 Controller (玩家或 AI)
	 * @param MotherCharRowName  DT_CharacterInfo 中 BP_MuTi 对应的 RowName (非空)
	 * @return 是否成功 (失败 = Controller/RowName/出生点/Spawn 任意环节失败)
	 */
	UFUNCTION(BlueprintCallable, Category = "Room|Spawn")
	bool MutatePawnToMother(AController* Controller, const FString& MotherCharRowName);

/**
 * @brief 倒计时结束后触发 — 遍历 GS->PlayerArray 调 HandlePlayerRequestSpawn
 */
	UFUNCTION(BlueprintCallable, Category = "Room|Spawn")
	void SpawnAllPlayersIntoBattle();

	// ==========================================
	// 【v93.1 大厂架构新增】业务层角色账本查询
	// ==========================================
	//
	// 大厂原则 — 业务查询 vs GetAllActorsOfClass:
	//   - 旧: MotherMutationSubsystem 直接 GetAllActorsOfClass<ABaseCharacter> — 散查
	//   - 新: 走本函数单一入口 — 业务层账本唯一真理源
	//   - 不维护 SpawnedAICharacters 字段 (会引入账本同步问题)
	//   - 本函数内部 GetAllActorsOfClass (一次性扫描, N<=20, 选母体每局 N 次, 性能可接受)
	//
	// 调用方:
	//   - URoomMotherMutationSubsystem::GetEligibleHumanTargets (选母体)
	//   - 未来扩展: UI 渲染对局内角色, 比赛结算等

	/**
	 * @brief 获取对局内所有 ABaseCharacter 派生角色 (玩家 Pawn + AI Pawn)
	 *
	 * 大厂原则 — 单一入口:
	 *   - 业务方应走本函数, 不直接 GetAllActorsOfClass
	 *   - 本函数是未来账本缓存的预留接口 (若性能不够, 可内部加 TArray<TWeakObjectPtr<ABaseCharacter>>)
	 *
	 * @return 对局内所有 ABaseCharacter 指针数组 (含已死角色, 调用方需 IsDead 过滤)
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Room|Spawn")
	TArray<ABaseCharacter*> GetAllBattleCharacters() const;

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
		/**
		 * 【v52 P0】主武器 ID (Slot 1)
		 * 大厂原则: 与 ARoomPlayerState::SelectedWeaponID1 对称 (真理源)
		 */
		FString WeaponPrimaryID;
		/**
		 * 【v52 P0】副武器 ID (Slot 2)
		 */
		FString WeaponSecondaryID;
		/**
		 * 【v52 P0】近战武器 ID (Slot 3)
		 */
		FString WeaponMeleeID;
	};

	bool GetPlayerSpawnData(uint32 ControllerUniqueID, FString& OutCharID, FString& OutWeaponID) const;

	/**
	 * 【v52 P0 扩展 3 槽位】读取玩家 3 把武器缓存
	 * 复活时通过这个接口一次性恢复 Loadout (主+副+近战)
	 */
	bool GetPlayerSpawnDataAllWeapons(uint32 ControllerUniqueID, FString& OutCharID, FString& OutPrimaryID, FString& OutSecondaryID, FString& OutMeleeID) const;

	/**
	 * 写入玩家生成数据 (由 GameMode::AddPlayerToRoom 调用)
	 * 【v52 P0】3 把武器一起写, 主+副+近战
	 */
	void SetPlayerSpawnData(uint32 ControllerUniqueID, const FString& CharID, const FString& PrimaryWeaponID, const FString& SecondaryWeaponID, const FString& MeleeWeaponID);

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

	/**
	 * 【v104 新增】母体复活点 TAG
	 *
	 * 来源: ScanAndCachePlayerStarts 扫描 PlayerStartTag=Faction_Mother 的 PlayerStart
	 * 用途: 母体出生和复活时从 MotherSpawnPoints 中随机选取
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Room|Spawn")
	FName TAG_Faction_Mother;

	/**
	 * 【v201 大厂架构新增】生化模式人类专用复活点 TAG
	 *
	 * 背景:
	 *   - 生化模式人类玩家需要专属复活点，不是 Offense/Defense 阵营复活点
	 *   - 用户在场景中添加 PlayerStart，Tag 设为 "Faction_HumanSurvivor"
	 *
	 * 来源: ScanAndCachePlayerStarts 扫描 PlayerStartTag=Faction_HumanSurvivor 的 PlayerStart
	 * 用途: 生化模式人类玩家复活时从 HumanSurvivorSpawnPoints 中随机选取
	 *
	 * 大厂原则 — 零兜底:
	 *   - 生化模式人类复活必须走 HumanSurvivorSpawnPoints
	 *   - 不允许 fallback 到 Offense/Defense 复活点
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Room|Spawn")
	FName TAG_Faction_HumanSurvivor;

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

	/**
	 * @brief 【v213 大厂架构新增】刀战模式 Loadout 净化工具函数 — 单一真理源 + DRY
	 *
	 * 【业务背景 — 用户 2026.08.09 明确要求】
	 *   "刀战模式专属逻辑：主武器和副武器进游戏不能加载, 要为空, 只能拿近战武器.
	 *    现在进游戏还是拿了主武器. 修复一下bug"
	 *
	 * 【根因 — 5 个 Spawn 入口全部未检查 CurrentMatchMode】
	 *   1. HandlePlayerRequestSpawn — 玩家开局/复活/母体变人类, 直接用 PrimaryID/SecondaryID Spawn
	 *   2. SpawnAIInternal — AI 大厅入队, 用 Request.WeaponID (未按模式过滤)
	 *   3. RequestRespawn 玩家分支 — 调 HandlePlayerRequestSpawn, 自动净化 (链式生效)
	 *   4. RequestRespawn AI 分支 — 调 SpawnAIInternal, 自动净化 (链式生效)
	 *   5. RestartZombieRoundPlayers — 生化模式专用, 仅在 Zombie 模式调, 0 影响
	 *
	 * 【大厂原则 — 职责集中 + DRY】
	 *   - **静态工具函数**: 不需要 Subsystem 实例状态, 纯函数行为
	 *   - **单一入口**: 5 个 Spawn 入口全部调它, 不写 5 份 if/else 重复代码
	 *   - **零兜底**: 模式识别失败 (None / GameState 为空) → 默认放行 (与现有 v50 行为一致, 不引入新错误)
	 *   - **不破坏生化模式**: Mode != Melee → 返回原值, 0 行为变更
	 *
	 * 【数据结构 (3 个 RowName 一起净化)】
	 *   输入: PrimaryID / SecondaryID / MeleeID (来自玩家存档 / AI CachedWeaponID)
	 *   输出: 同 3 个 RowName (Melee 模式 = Primary/Secondary 清空, Melee 保留)
	 *
	 * @param Mode           当前房间模式 (来自 ARoomGameState::CurrentMatchMode, 单一真理源)
	 * @param PrimaryRowName 输入的主武器 RowName (输出会被清空 if Melee)
	 * @param SecondaryRowName 输入的副武器 RowName (输出会被清空 if Melee)
	 * @param MeleeRowName   输入的近战武器 RowName (永远不变, 保留玩家选择 + 业务默认)
	 */
	UFUNCTION(BlueprintCallable, Category = "Room|Spawn|Purification", meta = (DisplayName = "v213 Purify Loadout For Melee Mode"))
	static void PurifyLoadoutForMeleeMode(
		ERoomMatchMode Mode,
		FString& PrimaryRowName,
		FString& SecondaryRowName,
		FString& MeleeRowName);

	/**
	 * @brief 【v213 大厂架构新增】AI WeaponID 净化 — Request.WeaponID 是单字符串, 走专用净化
	 *
	 * 【与 PurifyLoadoutForMeleeMode 区别】
	 *   - 玩家路径 = 3 个 RowName (主+副+近战), 上面工具函数覆盖
	 *   - AI 路径 = 1 个 Request.WeaponID (单字符串, 不区分 Slot)
	 *   - AI 在 Melee 模式下必须强制只拿 Melee 武器
	 *     → 如果 Request.WeaponID 是 Primary/Secondary → 清空, 让 AI 无武器
	 *     → 调用方应另外从 AI CachedCharacterRowName / Profile 派生 Melee 默认 (JZ001)
	 *
	 * 【如何识别 Primary/Secondary vs Melee?】
	 *   - 调 DT_WeaponInfo::FindRow<FWeaponInfo>(RowName) 查表
	 *   - 读 Row->MeshType: Melee 保留, Primary/Secondary 清空
	 *
	 * @param Mode           当前房间模式
	 * @param InOutAIWeaponID  [in] AI 传入的 WeaponID, [out] 净化后 (Melee 模式 = 清空 if 不是 Melee)
	 * @param OutMeleeDefaultID [out] 如果输入被清空 (AI 强制只拿 Melee), 返回业务默认 Melee ID (JZ001)
	 * @return true=有净化动作 (Melee 模式 + 原值不是 Melee), false=无需净化
	 */
	UFUNCTION(BlueprintCallable, Category = "RoomSpawn")
	bool PurifyAIWeaponForMeleeMode(
		ERoomMatchMode Mode,
		FString& InOutAIWeaponID,
		FString& OutMeleeDefaultID);

	/**
	 * @brief 【v213 大厂架构新增】房间净化状态查询 — UI 用
	 *
	 * UI 层 (URoomInsidePage) 在 Init 阶段调一次, 判断当前房间模式是否需要净化 Primary/Secondary 选择
	 *   - Melee 模式 → true (UI 应清空 TempSelectedWeaponsByType[Primary/Secondary])
	 *   - 其他模式 → false (UI 不动)
	 *
	 * 调用方: URoomInsidePage::InitializeTempSelectedWeaponsByDefault 调用前后
	 *   注意: InitializeTempSelectedWeaponsByDefault 只预填 Melee, Primary/Secondary 默认就是空的
	 *   本函数用于**显式清除玩家在 Zombie 模式下选过、然后切到 Melee 模式的残留选择**
	 *
	 * @return true = 当前模式是 Melee, UI 应该净化 Primary/Secondary 选择
	 */
	UFUNCTION(BlueprintPure, Category = "Room|Spawn|Purification", meta = (DisplayName = "v213 Is Melee Mode"))
	static bool ShouldPurifyForMeleeMode(ERoomMatchMode Mode);

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

	/**
	 * 【v104 新增】母体复活点数组
	 *
	 * 来源: ScanAndCachePlayerStarts 扫描 PlayerStartTag=Faction_Mother 的 PlayerStart
	 * 用途: 母体出生和复活时从这些点中随机选取
	 *
	 * 大厂原则 — 单一真理源:
	 *   - 与 AttackSpawnPoints / DefenseSpawnPoints 同级管理
	 *   - 母体专用的复活点，不与攻守方共享
	 */
	UPROPERTY()
	TArray<APlayerStart*> MotherSpawnPoints;

	/**
	 * 【v201 大厂架构新增】生化模式人类专用复活点数组
	 *
	 * 大厂原则 — 职责分层:
	 *   - 与 AttackSpawnPoints / DefenseSpawnPoints / MotherSpawnPoints 同级管理
	 *   - 生化模式人类玩家专用的复活点，不与其他阵营共享
	 *   - 小局结束后玩家重新随机分配到这些点
	 *
	 * 用途:
	 *   - GetAvailableSpawnPointForFaction 生化模式分支读取
	 *   - RestartZombieRoundPlayers 分配新复活点
	 */
	UPROPERTY()
	TArray<APlayerStart*> HumanSurvivorSpawnPoints;

	UPROPERTY()
	TSet<APlayerStart*> OccupiedSpawnPoints;

	// 【v210 大厂架构新增】延迟弹药重置 Timer Handle
	UPROPERTY()
	FTimerHandle AmmoRefillTimerHandle;

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