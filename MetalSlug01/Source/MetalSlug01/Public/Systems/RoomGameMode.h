// 版权声明：在项目设置的描述页面填写您的版权信息。

#pragma once

// ==========================================
// 头文件包含说明
// ==========================================
// UE 引擎核心最小化头文件（FString/TArray/基础宏）
#include "CoreMinimal.h"

// 引入 UE 原生 AGameModeBase 类（基类）
#include "GameFramework/GameModeBase.h"

// 引入房间相关枚举（ERoomState/ERoomMatchMode — ERoomTeam 已于 2026.07.10 删除）
// 改造: 改为精确子表头
#include "Data/Enums/RoomEnums.h"
#include "GameplayTagContainer.h" // 【2026.07.10 P0 重构】FGameplayTag 阵营

// 【Phase 1 新增】AI 行为类型定义 (含 FAISpawnRequest / FPendingAIEntry)
#include "Systems/AI/AIBehaviorTypes.h"

// 【v54 大厂架构】AI 行为配置 DataAsset (关卡预放 AI 走 ConfigSO.LevelPlacedAI_xxx)
#include "Data/AI/AIBehaviorConfigSO.h"

// UE 自动生成的头文件（必须放在最后一行）
#include "RoomGameMode.generated.h"

// ==========================================
// 前置声明（避免头文件互相包含）
// ==========================================
class ABaseCharacter;     // 角色基类
class ABaseWeapon;        // 武器基类
class APlayerStart;       // 玩家出生点 Actor
class ARoomPlayerController; // 房间玩家控制器
class AMeleeAIController;   // 近战 AI 控制器（AddAIToRoom 需要 Spawn）
class AAirdropPickup;       // 空投 Pickup (v117)

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
	// ==========================================
	// 【v76 大厂架构 — 武器切换音效】访问器
	// ==========================================
	//
	// 单一真理源 (v76 落地):
	//   - GM->WeaponSoundMapAsset 字段 (SoftObjectPtr, UE 编辑器 BG 加载)
	//   - GetWeaponSoundMapAsset() 返回同步加载后的 UWeaponSoundMapAsset*
	//   - 调用方 (WeaponAttachmentComponent) 走这个 API 拿真理源
	//   - 找不到 / 未配 → Log Error + return nullptr (零兜底)
	//
	// 大厂原则 - 配置可发现性:
	//   - 错误日志明确指出"BP_GM_RoomGameMode → Room|Audio → Weapon Sound Map Asset"
	//
	// @return 同步加载后的 UWeaponSoundMapAsset (nullptr 表示 Log Error 已记录)
	// ==========================================
	class UWeaponSoundMapAsset* GetWeaponSoundMapAsset() const;

	// ==========================================
	// 【2026.07.11 v29.5】出生点 PlayerStartTag 常量定义
	// 【2026.07.11 v29.8 重命名】统一阵营命名, "Faction_Attack" → "Faction_Offense"
	//
	// 背景:
	//   - 玩家阵营枚举用的是 Faction.Offense / Faction.Defense (FGameplayTag)
	//   - PlayerStartTag 用的是 Faction_Attack / Faction_Defense (FName)
	//   - 命名不一致 → 工程师 / 策划 / UI 容易混淆 (Offense vs Attack 在不同上下文含义模糊)
	//   - v29.8 决定统一: PlayerStartTag 也用 Offense / Defense
	//
	// 命名映射:
	//   - 旧 "Faction_Attack"   → 新 "Faction_Offense" (攻方)
	//   - "Faction_Defense"     → 保持不变 (守方)
	//
	// 兼容性 (大厂原则 - 显式优于隐式):
	//   - 旧 "Faction_Attack" 字符串: ScanPlayerStarts 仍接受, 但 Log Warning 提示用户改 Tag
	//   - 这样既不丢已有配置, 又推动用户迁移到新命名
	//
	// 用法: 在 UE 编辑器里打开每个 PlayerStart, Details 面板 → Player Start Tag
	//       攻方点填 "Faction_Offense", 守方点填 "Faction_Defense"
	//       GameMode 启动时按 Tag 自动分类到 AttackSpawnPoints / DefenseSpawnPoints
	//
	// 为什么用常量 (而不是 #define / inline FName):
	//   - FName 是 UE 字符串表 (Name Table) 索引, 必须池化才能 O(1) 比较
	//   - 静态 const FName 是 UE 官方推荐做法 (单次哈希, 后续 0 开销)
	//   - 反射层 UPROPERTY 标记让蓝图也能用
	// ==========================================
	static const FName TAG_Faction_Offense;
	// 【v29.8 兼容旧 Tag】保留 "Faction_Attack" 作为已废弃别名, 接收但不推荐
	//   - 新代码不要引用这个常量
	//   - 只用于 ScanPlayerStarts 兼容旧版 PlayerStart 配置
	//   - 后续 v30 计划删除 (给用户足够迁移窗口)
	static const FName TAG_Faction_Defense;

	/**
	 * 构造函数: 在 GameMode 被加载时调用
	 * 目的: 配置默认的玩家类、控制器类、HUD 类等
	 */
	ARoomGameMode(const FObjectInitializer& ObjectInitializer);

	/**
	 * @brief v31.3 数据注入入口 — GameMode → Subsystem
	 *
	 * 触发时机: InitGame / PostInitProperties
	 * 用途: 把 GameMode 配置 (DT / ModeRules) 注入到 Subsystem
	 *      让 SpawnSubsystem 成为运行时唯一真理源 (GameMode 只做编辑器面板)
	 *
	 * 【v54 大厂架构重构】ProfilesByMode 字段已删除
	 *   - 关卡预放 AI 走 ConfigSO (BaseAIController.GetConfig()) — 不经过 Subsystem
	 *   - 大厅入队 AI 走 Request — 不需要 Profile 反查
	 *   - 只注入 ControllerClass + ModeRules
	 */
	void InjectSubsystemConfigs();

	/**
	 * @brief UE override: GameMode 初始化时调用 — World 已存在可调 Subsystem
	 */
	virtual void InitGame(const FString& MapName, const FString& Options, FString& ErrorMessage) override;

	/**
	 * @brief UE override: GameState 已创建后调用 (URL ?Mode= 解析写入 GS 的入口)
	 *
	 * 大厂原则 (Single Source of Truth):
	 *   - InitGame 时 GameState **尚未创建** (GameState 在 InitGameState 中创建)
	 *   - InitGameState 是写入 GS->CurrentMatchMode 的最早安全时机
	 *   - InitGame 只负责 InjectSubsystemConfigs (DT / ModeRules / 复活延迟)
	 */
	virtual void InitGameState() override;

	/**
	 * @brief UE override: 属性初始化后调用 — Editor 反射可注入 Subsystem
	 */
	virtual void PostInitProperties() override;

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
	 * 【P0 架构升级】服务端主动变更房主
	 *
	 * 用途: 房主离房/被踢时, 服务器自动把房主权限转交给下一个玩家
	 * 副作用:
	 *  - 修改 GameState->HostPlayerName (ReplicatedUsing 触发客户端 OnRep)
	 *  - 服务器主动广播 URoomService::BroadcastHostChanged (本地 OnRep 不会触发)
	 *  - 广播系统提示"X 成为新房主"
	 *
	 * @param NewHostPlayerName 新房主名 (为空表示"随机选下一个在线玩家")
	 * @return 是否成功转交 (false 表示房间没人了/新房主名不合法)
	 */
	UFUNCTION(BlueprintCallable, Category = "Room|Host")
	bool TransferHostTo(const FString& NewHostPlayerName);

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
	 * 【v54 大厂架构重构 — 删除 ProfilesByMode / DefaultProfileTag】
	 *
	 * 历史 (v53 及之前):
	 *   - 持有 TMap<ERoomMatchMode, FAIProfileRegistry> ProfilesByMode (二级 TMap 包装)
	 *   - 持有 FGameplayTag DefaultProfileTag (兜底 Tag)
	 *   - 配合 FAIProfileRegistry / UAIProfileAsset / ResolveProfileByTag 等反查链路
	 *
	 * v54 重构 (用户决策 2026.07.16):
	 *   - UAIProfileAsset 整个类已删除 (DA_AIProfile_*.uasset 整张表废弃)
	 *   - FAIProfileRegistry 已删除 (依赖 UAIProfileAsset)
	 *   - 关卡预放 AI 走 ConfigSO.LevelPlacedAI_DefaultXxx (不依赖 Profile)
	 *   - 大厅入队 AI 走 Request (UI 直接传武器/AIController, 不依赖 Profile)
	 *   - ProfilesByMode / DefaultProfileTag 整个删除, 不再保留中间层
	 *
	 * 大厂原则:
	 *   - 真理源不分裂 (一个 AI 类型只有一个 ConfigSO, 没有 Profile 中间层)
	 *   - 删除中间层 = 删除配置反模式 + 删除反查链 = 大厂零兜底
	 */

	/**
	 * 【v55 大厂架构重构 — 删除 DefaultControllerClass】
	 *
	 * 历史 (v29-v54):
	 *   - UPROPERTY(EditDefaultsOnly, Category = "AI|Phase2") DefaultControllerClass
	 *   - 曾作为 AIControllerClass 的 fallback (当 Request.AIControllerClass 和 ConfigSO 都为空时)
	 *
	 * v55 重构 (用户决策 2026.07.16):
	 *   - DefaultControllerClass 是"错误配置模式" — GM 全局默认值, 违反"按模式配置"原则
	 *   - 关卡预放 AI: 走 ConfigSO.LevelPlacedAIControllerClass (ConfigSO 是 AI 类型专属配置)
	 *   - 大厅入队 AI: 走 ModeRulesByMode[Mode].AIControllerClass (FAIModeRules 是模式专属配置)
	 *   - 两者都是"专属配置", 不需要 GM 全局默认值兜底
	 *
	 * 大厂原则 (零兜底):
	 *   - 删除 GM 全局默认值兜底
	 *   - ModeRules.AIControllerClass 为空 → Log Error + 拒绝 Spawn (强制配置)
	 *   - ConfigSO.LevelPlacedAIControllerClass 为空 → Log Error + 拒绝 Spawn (强制配置)
	 *   - 禁止任何 fallback
	 *
	 * UE 编辑器配置路径 (替代方案):
	 *   - 大厅入队 AI: GM_RoomGameMode → ClassDefaults → ModeRulesByMode → Melee/Zombie → AIControllerClass
	 *   - 关卡预放 AI: DA_AIBehaviorConfig_MeleeGrunt → LevelPlacedAI → LevelPlacedAIControllerClass
	 */

	/**
	 * 【Phase 2 模式化】模式专属规则集合 (替代老路径里的硬编码)
	 * 设计:
	 *   - 父类不再写死任何阵营 Tag
	 *   - 策划在 BP_RoomGameMode 里按 Mode 配置:
	 *       Melee:  AttackFaction=Faction.Offense,  DefenseFaction=Faction.Defense
	 *       Zombie: AttackFaction=Faction.Offense,  DefenseFaction=Faction.Defense
	 *       (哪个 Tag 属于攻/守 = 业务决策, 不是固定规则)
	 *   - 加新模式 (CF 当年有救世主/幽灵) → 加枚举 + ModeRules, GameMode 一行不改.
	 *
	 * 阵营 (Offense/Defense) 是**通用身份**, 客户端玩家和 AI 都可用:
	 *   - Melee 模式下玩家可以是 Offense 或 Defense, AI 同样可以是 Offense 或 Defense
	 *   - 玩家/AI 在哪个阵营, 由 GameMode::AddPlayer/AddAIByRequest 调用方决定
	 */
	UPROPERTY(EditDefaultsOnly, Category = "AI|Phase2")
	TMap<ERoomMatchMode, FAIModeRules> ModeRulesByMode;

	/**
	 * 【2026.07.11 v28 大厂架构重构】大厅阶段 AI 占位入队 (替代 AddAIToRoom)
	 *
	 * 设计意图:
	 *   旧版 AddAIToRoom / AddAIByRequest 立刻生成 Pawn + AIController, 违反新业务规则:
	 *     "开始游戏在阵营复活点生成 AI, 大厅阶段 UI 显示占位"
	 *   新版:
	 *     1. 房主 UI 点击"添加 AI [攻方 x3]" → 调本函数入队, **不生成任何 Actor**
	 *     2. URoomStateService::GetAttackFactionSnapshots 读本类 PendingAIQueue → UI 渲染占位
	 *     3. 开始游戏: SpawnAllPlayersIntoBattle 消费本队列 → 在阵营复活点 Spawn
	 *     4. 战斗阶段: AI 死后复用同一 AIController (v24 不销毁), UI 已隐藏, 无影响
	 *
	 * 大厂原则:
	 *   - 显式意图: Queue 入队 = "预订", Spawn 入场 = "兑现", 完全分离
	 *   - 单一真理源: 本队列只在 GameMode, Queue/Consume 是唯一读写入口
	 *   - 零兜底: FactionTag 必须为 Offense/Defense, 其它 Tag 显式拒绝入队
	 *
	 * 【v54 大厂架构重构】ProfileTag 已从 FAISpawnRequest 删除
	 * @param Request 包含 FactionTag / CharacterInfoRowName / WeaponID / AIPawnClass / Mode / Count
	 * @return 实际入队数量 (Count 字段值, 已通过验证后)
	 *
	 * @deprecated 旧版 AddAIToRoom / AddAIByRequest 已删除 — 它们违反新业务规则
	 */
	UFUNCTION(BlueprintCallable, Category = "AI")
	int32 QueueAIForBattleSpawn(const FAISpawnRequest& Request);

	/**
	 * 更新某个人的准备状态并广播
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
	// 【架构升级 2026.07.06 17:00】战斗开始广播
	// 解决: AI 出生后立即 RunBehaviorTree, 玩家还在大厅就追玩家
	//       改为: AI 启动 BT 时检查 BattleInProgress, 不在则等本事件
	// ==========================================

	/**
	 * 战斗开始广播事件 (服务器触发, 仅一次, 客户端 GameMode 不存在可忽略)
	 * AI 等订阅者收到此事件后才激活 BT
	 */
	DECLARE_MULTICAST_DELEGATE(FOnBattleStartedSignature);
	/** 战斗开始委托 (单播多订阅), AI 控制器订阅它来激活 BT */
	FOnBattleStartedSignature OnBattleStarted;

	/**
	 * 是否已经广播过 OnBattleStarted (幂等保护, 重复调用 PerformGameStart 不会重复触发)
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Game State")
	bool bBattleStartedBroadcasted;

	// bSpawnInProgress / FSpawnInProgressGuard 已彻底删除 (v31.5 大厂重构)
	// 历史:
	//   - v30 引入 RAII guard 替代裸 bool (防御函数中途 return 导致标志位残留)
	//   - v31.1 将 bSpawnInProgress 真理源迁移到 URoomSpawnSubsystem
	//   - v31.5 删除本类 FSpawnInProgressGuard 死代码 (cpp 中既无实现也无调用方)
	//
	// 大厂原则 (v31.5):
	//   - 真理源唯一: URoomSpawnSubsystem::IsSpawnInProgress() / bSpawnInProgress
	//   - RAII 防护归 GameMode::SpawnAllPlayersIntoBattle 局部作用域
	//   - 严禁 RoomGameMode 再持有任何 SpawnInProgress 相关字段

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
	 * 【v41 大厂架构】玩家角色战斗参数配置表
	 *
	 * 真理源 (v41 新增):
	 *   - 玩家角色所有战斗参数从本资产读取 (MaxHealth / MaxEnergy / RespawnDelay / SpawnInvincibility 等)
	 *   - 消除 BaseCharacter / HealthComponent / EnergyComponent / HealthRegenComponent 内的硬编码
	 *   - 策划在一个地方调整所有参数
	 *
	 * 资产类型: UPlayerConfigAsset (DataAsset)
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Room|Config", meta = (AllowedClasses = "/Script/MetalSlug01.PlayerConfigAsset"))
	TSoftObjectPtr<class UPlayerConfigAsset> PlayerConfigAsset;

	/**
	 * 【v37 单一真理源】武器挂载数据表 (Socket + RelativeLocation + RelativeRotation + RelativeScale)
	 *
	 * 真理源迁移 (v37 大厂重构):
	 *   - 旧 (v32-v36): UWeaponAttachmentComponent 内每个 BP 都需要配这个字段
	 *     - BP_BaseCharacter 配一次 / BP_SWAT_C 配一次 / BP_GruntAI 配一次
	 *     - 配置反模式: 3 个 BP 持有同一资产, 改一处忘一处 (用户实际踩坑)
	 *   - 新 (v37): 集中在 GameMode 配一次, 所有角色通过 GM 拿到
	 *     - BP_GM_RoomGameMode → Class Defaults → Room|Data → Weapon Attachment DataTable
	 *     - 运行时: UWeaponAttachmentComponent::GetWeaponAttachmentDataTable() 走 GM
	 *     - 找不到 GM / GM 字段为空 → Log Error + 强制修复 (零兜底)
	 *
	 * 行结构: FWeaponAttachmentConfig
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Room|Data")
	class UDataTable* WeaponAttachmentDataTable;

	/**
	 * 【v76 大厂架构 — 武器切换音效】武器切换音效映射配置 DataAsset
	 *
	 * 真理源 (v76 新增):
	 *   - 所有武器切换音效从本资产读取 (按 EWeaponSlotType 映射 USoundBase)
	 *   - 玩家按 1/2/3 切武器 → Server_SwitchToWeaponSlot 走此资产
	 *
	 * 单一真理源 (与 PlayerConfigAsset / WeaponDataTable 同位置):
	 *   - BP_GM_RoomGameMode → Class Defaults → Room|Audio → Weapon Sound Map Asset
	 *   - 运行时: UWeaponAttachmentComponent::GetWeaponSoundMapAsset() 走 GM
	 *   - 找不到 GM / Asset 字段为空 → Log Error + 强制修复 (零兜底)
	 *
	 * 资产类型: UWeaponSoundMapAsset (DataAsset)
	 * 默认值: 策划需创建 DA_WeaponSoundMap.uasset 并赋给此字段
	 *
	 * 大厂原则 - 零兜底:
	 *   - GM->WeaponSoundMapAsset 未配置 → Log Error, 切武器时拒绝播放音效
	 *   - 某个 Slot 的 Sound 字段为空 → Log Error (强制修复 DA), 不"默认 fallback"
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Room|Audio", meta = (AllowedClasses = "/Script/MetalSlug01.WeaponSoundMapAsset"))
	TSoftObjectPtr<class UWeaponSoundMapAsset> WeaponSoundMapAsset;

	/**
	 * 【v100 大厂架构 — 单一真理源迁移】连杀图标/音效数据表 (共享 HUD + KillSound)
	 *
	 * 真理源迁移 (v100 大厂重构):
	 *   - 旧 (v41 之前): UGameHUDWidget 内字段
	 *     - BP_HUD_xxx 各自配一次 → 多 HUD 蓝图配同一资产 = 配置反模式(类似 v36 前 BP 角色配 DT 模式)
	 *   - 新 (v100): 集中在 GameMode 配一次, 所有消费者通过 GM 拿到
	 *     - BP_GM_RoomGameMode → Class Defaults → Room|Data → Kill Streak Icon DataTable
	 *     - 运行时消费者:
	 *       ① UGameHUDWidget::NativeConstruct 走 GM 拉 → 注入 Widget_KillStreak (图标)
	 *       ② UKillSoundComponent::EnsureKillStreakDataTable lazy 拉 (音效)
	 *     - 找不到 GM / GM 字段为空 → Log Error + 强制修复 (零兜底)
	 *
	 * 行结构: FKillStreakIconInfo (含 StreakType/StreakIcon/KillSound 三个字段)
	 *
	 * 大厂原则 — 共享数据源:
	 *   - 图标 + 音效属于同一业务概念"连杀显示"
	 *   - 一张表, 一行配, 一对一映射 = 单一真理源
	 *   - BP 策划在一个地方就能改图标和音效
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Room|Data")
	class UDataTable* KillStreakIconDataTable;

	/**
	 * @brief 处理玩家的生成请求
	 * @param PlayerToSpawn           目标玩家控制器
	 * @param CharRowName             角色 DataTable 的行名
	 * @param WeaponPrimaryRowName    主武器 (Slot 1) DataTable 行名
	 * @param WeaponSecondaryRowName  副武器 (Slot 2) DataTable 行名 (允许空)
	 * @param WeaponMeleeRowName      近战武器 (Slot 3) DataTable 行名 (允许空)
	 */
	void HandlePlayerRequestSpawn(AController* PlayerToSpawn, const FString& CharRowName, const FString& WeaponPrimaryRowName, const FString& WeaponSecondaryRowName, const FString& WeaponMeleeRowName);

	/**
	 * 【Phase 2 模式化】AI 向上帝申请一个目标
	 *
	 * 旧实现: 在这里写死了"按分数排序/扎堆均摊", 刀战勉强能用, 生化完全废.
	 * 新实现:
	 *   1. 走 GetAllAliveEnemiesFor 收集候选 + 反扎堆账本
	 *   2. 按 RequestingAI 持有的 ConfigSO.HuntPolicy 选评分函数 (Nearest/Random/HighestScore/Mother-Weight)
	 *   3. 反扎堆均摊仍保留 (HuntPolicy.AntiHuddleWeight 控制强度)
	 *
	 * 【v54 大厂架构重构】Profile 已删除, 改读 ConfigSO.HuntPolicy
	 *
	 * @param RequestingAI 请求分配的 AI
	 * @return 分配的敌人目标 (找不到返回 nullptr)
	 */
	UFUNCTION(BlueprintCallable, Category = "AI")
	class ABaseCharacter* RequestTargetForAI(class ABaseCharacter* RequestingAI);

	/**
	 * 【Phase 2 模式化】按 ConfigSO.HuntPolicy 求一个候选的"分数"
	 * 内部供 RequestTargetForAI 调用. 外部 BP 也可调 (调试).
	 * 评分维度:
	 *   - 距离 (越小越高)
	 *   - 积分 (越高越高)
	 *   - 剩余时间 (越少越高, Mother 用)
	 *   - 反扎堆惩罚 (被多个 AI 锁定的目标会被扣分)
	 * 综合得分 = Σ (权重 * 维度归一值)
	 *
	 * @param RequestingAI 请求方
	 * @param Candidate   候选敌人
	 * @param Policy      Hunt Policy 配置
	 * @return 评分 (0~1, 越大越适合被选为猎物)
	 */
	UFUNCTION(BlueprintCallable, Category = "AI")
	float ScoreCandidateForAI(class ABaseCharacter* RequestingAI,
		const class ABaseCharacter* Candidate, const FAIHuntPolicy& Policy) const;

	/**
	 * 【Phase 2 模式化】取当前 GameMode 的模式规则
	 * 供 AddAIByRequest / RequestTargetForAI 查 Faction / 难度用
	 * @param Mode 目标模式
	 * @param OutRules 输出 (找不到时留默认值)
	 * @return 是否找到
	 */
	bool GetModeRules(ERoomMatchMode Mode, FAIModeRules& OutRules) const;

	/**
	 * @brief 查一查这个倒霉蛋现在正被几个 AI 盯着？
	 * @param TargetEnemy 目标敌人
	 * @return 正在追杀该敌人的 AI 数量
	 */
	int32 GetAttackerCount(ABaseCharacter* TargetEnemy);

	/**
	 * @brief 检查某个敌人是否已被其他 AI 锁定（用于 AI 侧查询"这个人还能不能抢"）
	 * @param TargetEnemy 目标敌人
	 * @return 是否已被锁定
	 */
	UFUNCTION(BlueprintCallable, Category = "AI")
	bool IsTargetLocked(ABaseCharacter* TargetEnemy);

	/**
	 * @brief 检查某个敌人是否已被其他 AI 锁定（排除指定 AI）
	 *        用于 RequestTargetForAI 内部：优先选"未被锁"的目标，已被锁的降权
	 * @param TargetEnemy 目标敌人
	 * @param ExcludeAI 排除的 AI（通常是自己）
	 * @return 是否被排除对象以外的 AI 锁定
	 */
	UFUNCTION(BlueprintCallable, Category = "AI")
	bool IsTargetLockedByOthers(ABaseCharacter* TargetEnemy, ABaseCharacter* ExcludeAI);

	/**
	 * @brief 检查所有玩家是否都已准备
	 * @return 是否全部准备
	 */
	bool CheckAllPlayersReady();

	/**
	 * 【Phase 2 模式化】释放目标记录（AI 死亡或换目标时调用）
	 * @param RequestingAI 请求释放的 AI
	 */
	UFUNCTION(BlueprintCallable, Category = "AI")
	void ReleaseTarget(ABaseCharacter* RequestingAI);

	/**
	 * 【v54 大厂架构重构 — 删除 ResolveProfileExact】
	 *
	 * 历史 (v53 及之前):
	 *   - ResolveProfileExact / ResolveProfileByTag 都是从 ProfilesByMode 反查 Profile 的入口
	 *   - 它们依赖 UAIProfileAsset 类
	 *
	 * v54 重构 (用户决策 2026.07.16):
	 *   - ProfilesByMode 字段已删除
	 *   - UAIProfileAsset 已删除
	 *   - 反查链整体删除 (v54 大厂原则: 真理源不分裂)
	 *   - 关卡预放 AI 走 ConfigSO, 大厅入队 AI 走 Request, 不需要反查
	 */

	/**
	 * 【v54 大厂架构重构】从 AI Pawn 拿它当前生效的 HuntPolicy
	 * 内部: ABaseAIController.RuntimeConfig.GetConfig().HuntPolicy (v54 改走 ConfigSO)
	 * @return 找不到时返回默认 (NearestDistance 兜底)
	 */
	FAIHuntPolicy GetEffectiveHuntPolicy(ABaseCharacter* AI) const;

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
	 * 每次 QueueAIForBattleSpawn 时自增
	 */
	int32 AINextID = 1;

	// ==========================================
	// 【v50 大厂架构重构 — 删除死代码】PendingAIQueue 字段已删除
	//
	// 旧 (v28-v49): ARoomGameMode 持有 PendingAIQueue 字段, 走 Get/Consume/Queue 访问
	// 新 (v50): 真理源已迁移到 URoomSpawnSubsystem::PendingAIQueue
	//   - 所有访问器 (Get/Queue/Consume/IsPending/Remove) 都是委派壳
	//   - RoomGameMode.h 的字段本身已无任何读写 → 删除
	//   - 这消除一处数据源分裂 + 减少内存浪费
	// ==========================================
	// PendingAIQueue 字段已删除 — 真在 URoomSpawnSubsystem

	/**
	 * 【v50 大厂重构 — PendingAISequenceID 已删除】
	 * 旧版用 GameMode 字段生成 SequenceID, 新版由 URoomSpawnSubsystem 内部管理
	 */
	// PendingAISequenceID 字段已删除 — 真在 URoomSpawnSubsystem

	/**
	 * 【2026.07.11 v28】内部: 把一条 PendingAI 转成 FAISpawnRequest (供 SpawnAllPlayersIntoBattle 调用 SpawnAIInternal)
	 * 大厂原则: 转换逻辑集中一处, 避免 SpawnAIInternal 调用方各自拼凑
	 *
	 * 【v50】此函数仍存在但作为 GameMode 委派壳转发到 URoomSpawnSubsystem::BuildSpawnRequestFromPending
	 */
	FAISpawnRequest BuildSpawnRequestFromPending(const FPendingAIEntry& Entry) const;

public:
	/**
	 * 【2026.07.11 v28】对外只读查询: 返回指定阵营的 PendingAI 列表 (供 URoomStateService 渲染 UI)
	 * 大厂原则: 外部只读不写, 写只走 QueueAIForBattleSpawn
	 *
	 * @param FactionTag 阵营 (Faction.Offense / Faction.Defense), 其它值返回空数组
	 * @return 该阵营的 PendingAI 列表 (空 FactionTag 返回空)
	 */
	UFUNCTION(BlueprintPure, Category = "AI|Pending")
	TArray<FPendingAIEntry> GetPendingAIInFaction(FGameplayTag FactionTag) const;

	/**
	 * 【2026.07.11 v28】对外只读查询: 所有 PendingAI (供调试 UI / 总览)
	 *
	 * 【v50 重构】改为委派到 URoomSpawnSubsystem (GameMode 不再持有 PendingAIQueue 字段)
	 */
	UFUNCTION(BlueprintPure, Category = "AI|Pending")
	TArray<FPendingAIEntry> GetAllPendingAI() const;

	/**
	 * 【2026.07.11 v28】内部消费: SpawnAllPlayersIntoBattle 调用, 把队列转成实际 Spawn 请求
	 * 调用后 PendingAIQueue 被清空 (消费完即弃)
	 *
	 * @return 该模式的所有 SpawnRequest (按入队顺序)
	 */
	TArray<FAISpawnRequest> ConsumePendingAIForBattleSpawn();

	/**
	 * 【2026.07.11 v28】对外只读: 检查 DisplayName 是否在 PendingAIQueue 里
	 * 用途: Server_KickPlayer 判断被踢名字是真人还是 AI 占位
	 *
	 * @param DisplayName 玩家标签上的名字 (例如 "AI_GruntAI_1")
	 * @return true=在队列里 (AI 占位), false=不在 (真人或已 Spawn)
	 */
	UFUNCTION(BlueprintPure, Category = "AI|Pending")
	bool IsPendingAIByName(const FString& DisplayName) const;

	/**
	 * 【2026.07.11 v28】对外只写: 从 PendingAIQueue 移除指定 DisplayName 的条目
	 * 用途: Server_KickPlayer 房主踢 AI 占位 (大厅阶段, AI 还没生成)
	 *
	 * 大厂原则: 调用方必须先 IsPendingAIByName 检查, 避免误删
	 *
	 * @param DisplayName 要移除的 AI 占位名字
	 * @return true=找到并移除, false=没找到 (调用方应先 IsPendingAIByName)
	 */
	UFUNCTION(BlueprintCallable, Category = "AI|Pending")
	bool RemovePendingAIByName(const FString& DisplayName);

protected:
	/**
	 * 【v31 大厂重构】AI 仇恨账本已迁到 URoomTargetingSubsystem
	 *   - LockedTargets / AIHuntingMap 已删除
	 *   - 任何读取请走 URoomTargetingSubsystem::Get(this)
	 *
	 * 遍历全场，找出对这个 AI 来说所有活着的敌人
	 * @param RequestingAI 发起查询的 AI
	 * @return 活着的敌人列表
	 */
	TArray<class ABaseCharacter*> GetAllAliveEnemiesFor(class ABaseCharacter* RequestingAI);

	/**
	 * 【Phase 2 模式化】私有 Spawn 内部实现 — 单一入口
	 *
	 * 替代 AddAIToRoom 内联 30 行代码:
	 *   1. 校验 Request.AIPawnClass 非空 (UI 反查时已拿到 Class 强类型)
	 *   2. 关卡预放 AI 时校验 Config 非空, 大厅入队 AI 时 Config 可空
	 *   3. 分配出生点 (按 Team)
	 *   4. Spawn or Reuse AI Controller (复用模式 = AI 复活)
	 *   5. Spawn Pawn 并 Possess
	 *   6. 应用 FactionTag (从 FAIModeRules 里读, 不再 hardcoded)
	 *   7. 调 InitializeFromConfig(Config) 走统一入口 (关卡预放 AI)
	 *
	 * 【v54 大厂架构重构 — UAIProfileAsset 删除】
	 *   - Profile 解析链路整个删除
	 *   - Config 参数: 关卡预放 AI 必传 (从 AIC.GetConfig()), 大厅 AI 可空 (走 Request)
	 *
	 * 【v30 大厂架构】单一入口原则:
	 *   - 大厅开局/战斗 Spawn: OptionalExistingController = nullptr → 新建 AIC
	 *   - AI 复活: 传入已存在的 AIC (DeadController) → 复用, 不销毁
	 *   - 任何新增 AI 生成路径都必须走本函数, 禁止复制实现
	 *
	 * @return 生成的 AI 数量 (失败可能 < Count)
	 */
	int32 SpawnAIInternal(const FAISpawnRequest& Request, UAIBehaviorConfigSO* Config, AAIController* OptionalExistingController = nullptr);

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

// ==========================================
// 复活管理
// ==========================================

public:
	/**
	 * 【大厂架构重构 2026.07.06】统一复活入口 (玩家 & AI 共用)
	 *
	 * 设计原则:
	 *   - 复活逻辑集中化, 避免在 BaseCharacter::Die() 中直接 Cast 判断 Controller 类型
	 *   - Die() 只负责死亡事件派发, 复活决策由 GameMode 统一处理
	 *   - 支持玩家和 AI 两种 Controller 的复活流程
	 *
	 * @param DeadController 死亡的 Controller (玩家或 AI)
	 * @param bImmediateRespawn 是否跳过复活延迟立即复活
	 */
	UFUNCTION(BlueprintCallable, Category = "Game|Respawn")
	void RequestRespawn(AController* DeadController, bool bImmediateRespawn = false);

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
	 *
	 * 大厂架构 (v48):
	 *   - 默认 3s: 给玩家和 AI 一个短暂的"准备"时间
	 *   - 设为 0: 立即 Spawn (测试模式推荐)
	 *   - 设负数: 被 ClampMin 钳到 0
	 */
	UPROPERTY(EditDefaultsOnly, Category = "MetalSlug|Match",
		meta = (ClampMin = "0.0", ClampMax = "30.0"))
	float MatchStartDelay = 3.0f;

	/**
	 * 【大厂架构重构 2026.07.06】复活延迟 (秒)
	 *
	 * 设计: 原本复活延迟硬编码在 BaseCharacter.RespawnDelaySeconds
	 *       新设计: 在 GameMode 统一定义, 保持与 MatchStartDelay 一致的可配置风格
	 *       BaseCharacter::RespawnDelaySeconds 仍保留作为默认值兜底
	 */
	UPROPERTY(EditDefaultsOnly, Category = "MetalSlug|Match")
	float RespawnDelaySeconds = 3.0f;

	/**
	 * 总局数（生化模式使用，UI 显示 "总局数：xx"）
	 *
	 * 大厂原则 — 单一真理源:
	 *   - 策划在 GameMode Class Defaults 配置
	 *   - 运行时通过 InjectSubsystemConfigs 写入 GameState.TotalRounds (Replicated)
	 *   - UI 订阅 GameState.TotalRounds 显示
	 *
	 * 【v92 大厂架构】ZombieTotalRounds 已删除, 统一用 TotalRounds 字段 (消除重复字段)
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MetalSlug|Match",
		meta = (ClampMin = "1", ClampMax = "20"))
	int32 TotalRounds = 5;

	/**
	 * 刀战模式每局比赛时长（秒），在此可配置任意值
	 * 例如 300=5分钟、600=10分钟、900=15分钟
	 *
	 * 单一真理源 — UI 的 Text_RoundCountdown 直接用此值:
	 *   - ServerStartMatchTimer (Melee 模式) 写入 GameState.MatchEndTime = Now + 此值
	 *   - UI Widget 通过 GetMatchRemainingSeconds() 计算剩余秒数
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MetalSlug|Match",
		meta = (ClampMin = "30", ClampMax = "3600"))
	int32 MeleeMatchDurationSeconds = 600;

	/**
	 * 生化模式每局比赛时长（秒），在此可配置任意值
	 * 例如 60=1分钟、120=2分钟、180=3分钟
	 *
	 * 单一真理源 — UI 的 Text_RoundCountdown 直接用此值:
	 *   - ServerStartMatchTimer (Zombie 模式) 写入 GameState.MatchEndTime = Now + 此值
	 *   - UI Widget 通过 GetMatchRemainingSeconds() 计算剩余秒数
	 *
	 * 大厂原则 — 对称设计:
	 *   - MeleeMatchDurationSeconds (刀战每局时长) + ZombieMatchDurationSeconds (生化每局时长)
	 *   - 两模式都有独立的倒计时, UI 镜像显示
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MetalSlug|Match",
		meta = (ClampMin = "30", ClampMax = "3600"))
	int32 ZombieMatchDurationSeconds = 120;

	/**
	 * 【生化模式】空投降临间隔（秒）
	 *
	 * 业务规则:
	 *   - 每小局母体变异倒计时结束后，才开始首轮空投倒计时
	 *   - 每次空投实际降临完毕后，由空投系统调用生命周期事件入口重启倒计时
	 *   - 默认 120 秒，策划可在 GameMode 蓝图中调整
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MetalSlug|Match|Airdrop",
		meta = (ClampMin = "1.0", ClampMax = "3600.0"))
	float AirdropIntervalSeconds = 120.0f;

	// ==========================================
	// 【v117 大厂架构新增】空投降临配置 — 单一真理源
	// ==========================================
	//
	// 业务规则 (用户 2026.08.03):
	//   - 倒计时到期 → 在 AirDropPoints 列表的每个点位上方 AirDropPickupDropHeight cm 处生成空投
	//   - 没被人类吃掉的旧空投先全部销毁
	//   - 策划在 BP_GM_RoomGameMode 蓝图拖入配置, 不需要改代码
	//
	// 大厂原则 — 单一真理源:
	//   - AirDropPoints / AirDropPickupClass / AirDropPickupDropHeight 是空投系统的唯一配置入口
	//   - Subsystem 只读不修改, 改配置需要重启游戏 (用户决策)
	//   - 刀战模式留空即可, Subsystem 永远不会生成空投 (模式守卫)
	//
	// 大厂原则 — 不污染项目设置:
	//   - 不创建自定义 CollisionProfile
	//   - 不改 DefaultEngine.ini
	//   - 业务行为全部在 BP/GameMode 字段配置, 可追踪

	/**
	 * 空投降临点位 Tag 列表 (策划在 BP 配置)
	 *
	 * 用途: 关卡里摆 N 个空 Actor, 给它们打 Actor.Tag, Tag 名填到这里
	 * 读取方: URoomAirdropSubsystem::SpawnAirdropAtAllPoints
	 *
	 * 【v117.2 修复】TArray<AActor*> → TArray<FName>
	 *   - 根因: UE 5.6 禁止跨 Outer 引用 — GameMode 蓝图类在 /Game/UI/, 关卡 Place Actor
	 *     在 /Game/Japanese_Temple/maps/Japanese_Temple_Demo/PersistentLevel
	 *     → BP 序列化器阻止 BP_GM_RoomGameMode 引用 BP_AirDropPoint 关卡实例
	 *     → 报错: "Illegal TEXT reference to a private object in external package"
	 *   - 修复: 改用 Tag 扫描 — BP 只持有 FName 字符串, 运行时 TActorIterator 扫描
	 *   - 这是 UE 5.6 大厂标准 (LYRA / Fortnite 都用 Actor Tag 扫描而不是直接引用)
	 *   - 策划工作流: BP_AirDropPoint 关卡 Actor → Details Panel → Actor → Tags → 加 "AirdropPoint"
	 *     然后在 GameMode.AirDropPointTags 填 "AirdropPoint"
	 *
	 * 默认空数组 → 业务禁用空投 (不静默, Log Warning 显式告知)
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MetalSlug|Match|Airdrop")
	TArray<FName> AirDropPointTags;

	/**
	 * 空投 Pickup 蓝图类 (策划在 BP 配置)
	 *
	 * 用途: 必须是继承自 AAirdropPickup 的 BP 类, 蓝图挂 StaticMesh
	 * 默认 nullptr → Log Error 拒绝 Spawn (强制策划配置)
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MetalSlug|Match|Airdrop")
	TSubclassOf<AAirdropPickup> AirDropPickupClass;

	/**
	 * 空投生成高度偏移 (cm)
	 *
	 * 用途: 营造"从高处下落"的感觉 — 空投在点位上方 N cm 处生成
	 * 默认 100cm, 策划可调 (建议 50~500)
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MetalSlug|Match|Airdrop",
		meta = (ClampMin = "0.0", ClampMax = "1000.0"))
	float AirDropPickupDropHeight = 100.0f;

	/**
	 * 【v92 大厂架构新增】生化模式母体变异倒计时（秒）
	 *
	 * 业务规则:
	 *   生化模式每局开局, 玩家/AI 都是人类, 互相无敌
	 *   此秒数倒计时结束后才会"变异"为母体 (Phase 3 母体死亡广播)
	 *   默认 8 秒, 可由策划在 BP 中调整
	 *
	 * 大厂原则 — 业务可配:
	 *   - 设 0 或负数 → 业务禁用母体变异倒计时 (按用户决策 A 静默跳过启动)
	 *   - 设 > 0 → 启动倒计时, 写入 GameState.MotherMutationDuration Replicated 字段
	 *
	 * 调用方:
	 *   - ARoomGameMode::InitLifecycleSubsystem 注入到 URoomLifecycleSubsystem
	 *   - URoomLifecycleSubsystem::StartMotherMutationCountdown 读取并启动
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MetalSlug|Match",
		meta = (ClampMin = "0.0", ClampMax = "60.0"))
	float MotherMutationDurationSeconds = 8.0f;

	/**
	 * 【v90 大厂架构新增】生化模式母体 Pawn RowName — 真理源
	 *
	 * 业务规则:
	 *   倒计时结束后, 选中的目标 Pawn 类变更为"母体"
	 *   - 蓝图 BP_MuTi 在 DT_CharacterInfo 中配 CharacterBlueprint (单源)
	 *   - 本字段只配 RowName 字符串 (业务可调, 无需改 C++)
	 *
	 * 大厂原则 — 配置可调:
	 *   - 必须非空 (零兜底)
	 *   - 业务层 (RoomMotherMutationSubsystem::MutateCharacterToMother) 只读本字段
	 *   - 不复活成出生点 (镜生态要求: 任何情况变母体都是原地变)
	 *
	 * 调用方:
	 *   - URoomMotherMutationSubsystem::MutateCharacterToMother 读取
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MetalSlug|Match")
	FString MotherCharacterRowName = TEXT("MT001");

	/**
	 * 【v93.4 大厂架构新增】生化模式母体最大血量 — 配置驱动真理源
	 *
	 * 业务规则 (用户 2026.07.25 明确):
	 *   变成母体后, 血量上限 = 本字段值 (默认 200), 满血
	 *   人类的各种武器都能对母体进行攻击 (走 FFactionTags::CanDamage 守卫, 异阵营 = 通过)
	 *   人类和人类之间无法互相伤害 (同阵营守卫拒判, 已自动生效)
	 *
	 * 大厂原则 — 配置可调 + 单一真理源:
	 *   - 必须 > 0 (零兜底)
	 *   - 业务层 (RoomSpawnSubsystem::MutatePawnToMother Step 5.6) 只读本字段
	 *   - 调 HealthComponent->InitializeHealth(MotherMaxHealth) 自动同步血量上限到所有客户端
	 *     (MaxHealth 已 Replicated + OnRep_CurrentHealth 自动 broadcast)
	 *
	 * 不破坏刀战模式 (大厂原则 — 零耦合):
	 *   - 本字段只被 MutatePawnToMother 读取, 母体变异专属入口
	 *   - 刀战模式不调 MutatePawnToMother → 永远不读本字段 → 刀战逻辑零影响
	 *
	 * 调用方:
	 *   - URoomSpawnSubsystem::MutatePawnToMother Step 5.6
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MetalSlug|Match",
		meta = (ClampMin = "1.0", ClampMax = "9999.0"))
	float MotherMaxHealth = 200.0f;

	/**
	 * 【v108 大厂架构新增】生化模式母体变异目标选择策略
	 *
	 * 业务规则 (用户 2026.07.30 明确):
	 *   倒计时结束后, 按本字段指定的策略选 1~N 个目标变异为母体
	 *   - Random:     玩家+AI 一起抽签 (默认)
	 *   - AIOnly:     只选 AI
	 *   - PlayerOnly: 只选玩家
	 *
	 * 大厂原则 — 业务可配 + 单一真理源:
	 *   - 真理源: 本字段 (UE 编辑器 BP_GM_RoomGameMode 配置)
	 *   - 数据流: GM → URoomLifecycleSubsystem (SetMotherSelectionPolicy) → URoomMotherMutationSubsystem
	 *   - 注入时机: InitGame 阶段一次性注入 (用户决策: 改配置需重启游戏)
	 *   - 候选不足 → Log Error + 中断循环 (零兜底)
	 *
	 * 不影响刀战模式:
	 *   - 刀战模式不读本字段 (HandleCountdownExpired 模式守卫已拦)
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MetalSlug|Match")
	EMotherSelectionPolicy MotherSelectionPolicy = EMotherSelectionPolicy::Random;

	/**
	 * 【v108 大厂架构新增】生化模式母体变异数量
	 *
	 * 业务规则 (用户 2026.07.30 明确):
	 *   倒计时结束后生成多少个母体 (默认 1)
	 *   - 1 = 单母体 (现状, 经典生化玩法)
	 *   - 2-3 = 多母体 (高难度 / 后期残局)
	 *
	 * 大厂原则 — 业务可调 + 零兜底:
	 *   - ClampMin=1 (至少 1 个, 否则循环退出无意义)
	 *   - ClampMax=10 (场景中活人可能不够, 超过没意义)
	 *   - 候选不足 → Log Error + 中断循环 (用户决策: 强制策划扩玩家/AI 数量)
	 *   - 选出的目标从候选清单移除 (避免重复选同一人)
	 *
	 * 不影响刀战模式:
	 *   - 刀战模式不读本字段
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "MetalSlug|Match",
		meta = (ClampMin = "1", ClampMax = "10"))
	int32 MotherMutationCount = 1;

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

	// PlayerSpawnDataCache 已迁移到 URoomSpawnSubsystem (v31.1 单一真理源 — 严禁 RoomGameMode 再持有)

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
	 * @brief 【2026.07.11 v30 零兜底】根据阵营 (FGameplayTag) 获取一个未被占用的出生点
	 *
	 * 设计:
	 *   - 优先分配未被占用的点
	 *   - 所有点都被占用 → Log Error + return nullptr (不静默复用)
	 *
	 * @param PlayerFactionTag 玩家所属阵营 (Faction.Offense / Faction.Defense)
	 * @param bRemoveOccupied 分配后是否标记该点为已占用
	 * @param OccupancyOwner 【v39 新增】占用者 Controller (用于死亡时精准释放)
	 * @return 返回一个可用的出生点 Actor，如果找不到则返回 nullptr
	 */
	UFUNCTION(BlueprintCallable, Category = "MetalSlug|Spawn")
	class AActor* GetAvailableSpawnPointForFaction(FGameplayTag PlayerFactionTag, bool bRemoveOccupied = true, AController* OccupancyOwner = nullptr);

	/**
	 * @brief 当玩家离开（断开连接或退出房间）时，释放其占用的出生点
	 * @param PlayerStart 要释放的出生点
	 *
	 * 【v39】新增更精准的 ReleaseSpawnPointByController 接口 (推荐使用)
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
	// v31.3 SSOT: OccupiedSpawnPoints / bSpawnPointsScanned 已迁移到 URoomSpawnSubsystem
	//   - 这两个字段在 RoomGameMode.h 是"死字段", cpp 不再读写
	//   - 真理源唯一: URoomSpawnSubsystem 内部 protected 成员
};
