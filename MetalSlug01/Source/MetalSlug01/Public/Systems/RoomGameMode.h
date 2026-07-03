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

// 【Phase 1 新增】AI 数据驱动 Profile 资产（编辑器里拖入 DA_MeleeGrunt）
#include "Data/AI/AIProfileAsset.h"

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

// ==========================================
// 全局默认常量（数据驱动兜底）
// ==========================================
// 设计动机 (2026-07-03 bug fix):
//   - 旧代码 GM 兜底硬编码 "Knife" / "Warrior", 与 DT_WeaponInfo / DT_CharacterInfo 实际 RowName 完全对不上
//   - 导致 bSkipLoginDirectToLobby 等测试路径下角色/武器永远生成失败
//   - 新做法: 优先从 DataTable 取第一行 (数据驱动), 全部失败时才退到全局兜底常量
//   - 这些常量仍然作为最后兜底, 但绝不应再被作为正常路径使用
namespace MetalSlugGameDefaults
{
	// 兜底角色 RowName (DT_CharacterInfo 第一行也找不到时使用)
	static const FString FallbackCharacterRowName = TEXT("Warrior");

	// 兜底武器 RowName (DT_WeaponInfo 第一行也找不到时使用)
	// 注意: 这是绝对底线, 如果你看到它被实际命中, 说明 DT_WeaponInfo 没配置武器数据
	static const FString FallbackWeaponRowName = TEXT("Knife01");
}

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
	 * 【Phase 2 模式化】AI Profile 注册表 (替代原 DefaultMeleeProfile 单数引用)
	 *
	 * 数据来源: 编辑器 (BP_RoomGameMode Details 面板) 把策划拖进来的所有 AI Profile 放这里.
	 * Key 设计:
	 *   - 一级 Key: ERoomMatchMode (模式)
	 *   - 二级: FAIProfileRegistry.Profiles (内含 FGameplayTag -> Profile)
	 *   理由: UHT 不允许 TMap 内层再嵌 TMap, 用 USTRUCT 包装.
	 *         同模式可能多类AI (普通僵尸 / 母体), 用 Tag 区分.
	 *
	 * 留空兜底:
	 *   - 按 Mode → Tag 取不到时, Fallback 到 DefaultProfileTag
	 *   - 整个 Mode 没配 Profile, Spawn 流程报警但继续 Spawn 一个裸 AI (Base).
	 */
	UPROPERTY(EditDefaultsOnly, Category = "AI|Phase2")
	TMap<ERoomMatchMode, FAIProfileRegistry> ProfilesByMode;

	/**
	 * 【Phase 2 模式化】默认 Profile Tag - 兜底查找时使用
	 * 例如: AI.Profile.Default = 普通近战僵尸, 各模式不配置时退化到此.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "AI|Phase2", meta = (Categories = "AI.Profile"))
	FGameplayTag DefaultProfileTag;

	/**
	 * 【Phase 2 模式化】默认 AI Controller 类
	 * 当 Profile.ControllerClass 留空时, 用此值兜底.
	 * 默认值: ABaseAIController::StaticClass() (刀战通用).
	 * 生化模式时, 会用 BP_RoomGameMode 默认填 AZombieAIController::StaticClass().
	 */
	UPROPERTY(EditDefaultsOnly, Category = "AI|Phase2")
	TSubclassOf<class AAIController> DefaultControllerClass;

	/**
	 * 【Phase 2 模式化】模式专属规则集合 (替代老路径里的硬编码)
	 * 设计:
	 *   - 父类不再写死 "Faction.Player/Zombie/TotalRounds" 等
	 *   - 策划在 BP_RoomGameMode 里按 Mode 配置:
	 *       Melee:  AttackFaction="Faction.Player",  DefenseFaction="Faction.Enemy"
	 *       Zombie: AttackFaction="Faction.Human",   DefenseFaction="Faction.Zombie"
	 *   - 加新模式 (CF 当年有救世主/幽灵) → 加枚举 + ModeRules, GameMode 一行不改.
	 */
	UPROPERTY(EditDefaultsOnly, Category = "AI|Phase2")
	TMap<ERoomMatchMode, FAIModeRules> ModeRulesByMode;

	/**
	 * 添加 AI 到房间（兼容老 API）
	 * @deprecated 已被 AddAIByRequest 替代, 留它仅为蓝图已有调用不报错
	 * @param bToAttackTeam true=攻方，false=守方
	 * @param CharacterName AI 角色名（用于查找 DataTable）
	 * @param Count 添加数量
	 */
	UFUNCTION(BlueprintCallable, Category = "AI", meta = (DeprecatedProperty,
		DeprecationMessage = "Use AddAIByRequest with FAISpawnRequest instead."))
	void AddAIToRoom(bool bToAttackTeam, const FString& CharacterName, int32 Count);

	/**
	 * 【Phase 2 推荐】按 SpawnRequest 批量添加 AI
	 * 这是新统一入口, 同时支持刀战/生化.
	 *
	 * 流程:
	 *   1. 从 ProfilesByMode → 按 Mode + ProfileTag 查 Profile
	 *   2. 查 CharacterDataTable 确定 Pawn Class
	 *   3. 队内依次 Spawn (按 bUseTeamSpawnPoint 分配出生点)
	 *   4. 用 Profile.ControllerClass (兜底 DefaultControllerClass) Spawn AI Controller
	 *   5. Possess 后注入 Profile (感知+BT)
	 *   6. Faction 设置走 ModeRules, 不再硬编码 "Faction.Player"
	 */
	UFUNCTION(BlueprintCallable, Category = "AI")
	int32 AddAIByRequest(const FAISpawnRequest& Request);

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

	/**
	 * 【P0 2026.07.10 v27】是否正在执行 SpawnAllPlayersIntoBattle
	 *
	 * 用途: 防止引擎的 RestartPlayer 与 SpawnAllPlayersIntoBattle 冲突
	 * 流程:
	 *   1. PerformGameStart -> SetTimer(SpawnAllPlayersIntoBattle)
	 *   2. SpawnAllPlayersIntoBattle 开始时: bSpawnInProgress = true
	 *   3. 如果引擎自动调用 RestartPlayer, RestartPlayer 检测到 bSpawnInProgress=true, 直接返回
	 *   4. SpawnAllPlayersIntoBattle 结束时: bSpawnInProgress = false
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Game State")
	bool bSpawnInProgress;

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
	 * 【Phase 2 模式化】AI 向上帝申请一个目标
	 *
	 * 旧实现: 在这里写死了"按分数排序/扎堆均摊", 刀战勉强能用, 生化完全废.
	 * 新实现:
	 *   1. 走 GetAllAliveEnemiesFor 收集候选 + 反扎堆账本
	 *   2. 按 RequestingAI 持有的 Profile.HuntPolicy 选评分函数 (Nearest/Random/HighestScore/Mother-Weight)
	 *   3. 反扎堆均摊仍保留 (HuntPolicy.AntiHuddleWeight 控制强度)
	 *
	 * @param RequestingAI 请求分配的 AI
	 * @return 分配的敌人目标 (找不到返回 nullptr)
	 */
	UFUNCTION(BlueprintCallable, Category = "AI")
	class ABaseCharacter* RequestTargetForAI(class ABaseCharacter* RequestingAI);

	/**
	 * 【Phase 2 模式化】按 Profile.HuntPolicy 求一个候选的"分数"
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
		class ABaseCharacter* Candidate, const FAIHuntPolicy& Policy) const;

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
	 * 【Phase 2 模式化】查 Profile (按 Mode + Tag, 多级兜底)
	 * 查询顺序:
	 *   1. ProfilesByMode[Mode][Tag]
	 *   2. ProfilesByMode[Mode][DefaultProfileTag]
	 *   3. ProfilesByMode[ERoomMatchMode::Melee][DefaultProfileTag]   (兜底 Melee)
	 *   4. nullptr
	 * 同步加载 — Spawn 路径才走这, BT 启动走 Profile.LoadBehaviorConfigAsync
	 */
	UAIProfileAsset* TryResolveProfile(ERoomMatchMode Mode, FGameplayTag ProfileTag) const;

	/**
	 * 【Phase 2 模式化】从 AI Pawn 拿它当前生效的 HuntPolicy
	 * 内部: ABaseAIController.CurrentProfile -> HuntPolicy
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

	/**
	 * 【Phase 2 模式化】私有 Spawn 内部实现
	 * 替代 AddAIToRoom 内联 30 行代码:
	 *   1. 解析 Profile (Mode+Tag)
	 *   2. 查 CharacterDataTable 拿 Pawn Class
	 *   3. 分配出生点 (按 Team)
	 *   4. Spawn AI Controller (按 Profile.ControllerClass -> 兜底 DefaultControllerClass)
	 *   5. Spawn Pawn 并 Possess
	 *   6. 应用 FactionTag (从 FAIModeRules 里读, 不再 hardcoded)
	 *   7. 调 InitializeFromProfile(Profile) 走统一入口
	 * @return 生成的 AI 数量 (失败可能 < Count)
	 */
	int32 SpawnAIInternal(const FAISpawnRequest& Request, UAIProfileAsset* Profile);

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
	 */
	UPROPERTY(EditDefaultsOnly, Category = "MetalSlug|Match")
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

	// ==========================================
	// 工具方法 (数据驱动兜底)
	// ==========================================

	/**
	 * @brief 从 WeaponDataTable 解析一个有效的武器 RowName (数据驱动兜底)
	 *
	 * 设计动机 (2026-07-03 bug fix):
	 *   - 旧代码在 FinalWeaponID 为空时硬编码 TEXT("Knife"), 但 DT_WeaponInfo 实际 RowName 是 "Knife01"/"WQ001"
	 *   - 导致 FindRow 永远找不到, 武器永远生成失败
	 *
	 * 新策略 (3 级兜底):
	 *   1. 优先 WeaponDataTable 第一行 (数据驱动)
	 *   2. DT 为空时回退 MetalSlugGameDefaults::FallbackWeaponRowName
	 *   3. 全部失败时返回空字符串 + 打 Error 日志 (绝不静默失败)
	 *
	 * @return 武器 RowName (绝不为空, 真失败时也会返回兜底常量)
	 */
	UFUNCTION(BlueprintCallable, Category = "MetalSlug|Spawn")
	FString ResolveFallbackWeaponRow() const;

	/**
	 * @brief 从 CharacterDataTable 解析一个有效的角色 RowName (数据驱动兜底)
	 *
	 * @see ResolveFallbackWeaponRow (同款 3 级兜底策略)
	 * @return 角色 RowName (绝不为空)
	 */
	UFUNCTION(BlueprintCallable, Category = "MetalSlug|Spawn")
	FString ResolveFallbackCharacterRow() const;

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
