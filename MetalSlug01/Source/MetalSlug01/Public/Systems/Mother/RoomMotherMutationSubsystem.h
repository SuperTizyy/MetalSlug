// Copyright (c) 2026. All Rights Reserved.
// URoomMotherMutationSubsystem — 生化模式母体变异业务权威调度

#pragma once

#include "CoreMinimal.h"
#include "Subsystems/WorldSubsystem.h"
#include "UObject/ObjectPtr.h"
#include "GameplayTagContainer.h"
// v108 — EMotherSelectionPolicy (母体变异目标选择策略)
#include "Systems/AI/AIBehaviorTypes.h"
#include "RoomMotherMutationSubsystem.generated.h"

class ABaseCharacter;
class ARoomGameMode;
class ARoomGameState;
class URoomLifecycleSubsystem;
class URoomSpawnSubsystem;

/**
 * 【大厂架构 — 单一职责】母体变异业务子系统
 *
 * 职责边界:
 *   - 本 Subsystem 唯一负责: 倒计时到期 → 选母体 → 触发变异
 *   - 不负责: 倒计时显示 (URoomLifecycleSubsystem + ARoomGameState 已就绪)
 *   - 不负责: Pawn 生成/销毁 (URoomSpawnSubsystem 唯一真理源)
 *   - 不负责: 阵营系统 (FFactionTags 单一真理源)
 *
 * 大厂原则 — 零兜底:
 *   - GameMode 找不到 → Log Error + 中断
 *   - 选母体清单为空 → Log Error + 不选
 *   - BP_MuTi 蓝图类加载失败 → Log Error + 不变异 (防止"半截变异")
 *
 * 大厂原则 — RPC 边界:
 *   - 服务器: 本 Subsystem 在 GameMode 中执行 HasAuthority 校验后, 通过 Multicast_PlayMutationFX 广播
 *   - 客户端: 只被动接收 (MutatedTargetName 用来 UI 显示, 母体本人不显示自己)
 *
 * 调用链 (服务器权威):
 *   URoomLifecycleSubsystem::StartMotherMutationCountdown
 *     └─ SetTimer 到期 → URoomMotherMutationSubsystem::HandleCountdownExpired (Server)
 *         ├─ 1. 校验重入 (MotherMutationHasFired)
 *         ├─ 2. 收集活着的"人类"角色 (GetEligibleHumanTargets)
 *         ├─ 3. 随机选 1 个 (SelectRandomTarget)
 *         ├─ 4. 调 ARoomGameState::MarkMotherMutationFired (Replicated 标记, 防重复触发)
 *         └─ 5. 调 MutateCharacterToMother(Selected) (核心业务)
 *             ├─ 服务器: 销毁旧 Pawn + 用 BP_MuTi 蓝图类 Spawn 新 Pawn (走 SpawnSubsystem)
 *             ├─ 服务器: 写 Selected->Multicast_PlayMutationFX(TargetName, NewClassPath)
 *             └─ 客户端: 收到 RPC → 播母体变异特效 + UI 提示
 */
UCLASS()
class METALSLUG01_API URoomMotherMutationSubsystem : public UWorldSubsystem
{
	GENERATED_BODY()

public:
	/**
	 * 【大厂原则 — 标准子系统钩子】只在 Game World 实例化, 不在 Editor/Preview 实例化
	 * 大厂原则: 子系统若不写 ShouldCreateSubsystem, 在编辑器世界也会创建 → 浪费内存
	 */
	virtual bool ShouldCreateSubsystem(UObject* Outer) const override;

	/**
	 * 【标准 Subsystem Get 接口 — 镜像其他 Room Subsystem 风格 (v31.5 教训)】
	 *
	 * 大厂原则 — UE 子系统访问标准模式:
	 *   - WorldContextObject → GetWorld() → World->GetSubsystem<URoomMotherMutationSubsystem>()
	 *   - 调用方使用 `URoomMotherMutationSubsystem::Get(this)` 一行拿到
	 *
	 * 为什么不放 UPROPERTY 字段:
	 *   - UE WorldSubsystem 是由 World 自动管理的, 不需要引用计数
	 *   - World 卸载时 Subsystem 自动销毁 (UE 引擎保证)
	 *
	 * @param WorldContextObject UE 标准 Context Object (this / World / GameInstance 等)
	 * @return 找到的 Subsystem, nullptr=World 未就绪 或 Game World 不是 Subsystem 管理范围
	 */
	static URoomMotherMutationSubsystem* Get(const UObject* WorldContextObject);

	/**
	 * 【依赖注入 — 镜像 v31.5 风格】服务器初始化时由 ARoomGameMode::InjectSubsystemConfigs 调用
	 * 大厂原则 — 显式依赖: 不允许 lazy 解析 (URoomSpawnSubsystem 等都可能晚于本子系统初始化)
	 *
	 * @param InGameMode  GameMode 引用 (业务调用与权威校验的入口)
	 * @param InLifecycle 倒计时权威 (本子系统从它接收"倒计时到期"事件)
	 * @param InSpawn     Pawn 生成权威 (MutateCharacterToMother 通过它重建母体 Pawn)
	 */
	void InitializeSubsystem(ARoomGameMode* InGameMode,
	                         URoomLifecycleSubsystem* InLifecycle,
	                         URoomSpawnSubsystem* InSpawn);

	/**
	 * 【服务器权威入口 — 倒计时到期回调】
	 *
	 * 大厂原则 — RPC 时序保证:
	 *   - 本函数由 URoomLifecycleSubsystem::SetTimer 在 Duration 到期时调用 (Server only)
	 *   - 客户端不调本函数 — 客户端倒计时走 GameState OnRep + UI 自渲染
	 *
	 * 大厂原则 — 零兜底:
	 *   - 调用前必须校验 HasAuthority (由 LifecycleSubsystem 调用方负责, 本函数内仍 defensive 检查)
	 *   - 选母体失败/重复触发 → Log Error + 提前返回, 不允许"沉默失败"
	 */
	void HandleCountdownExpired();

	/**
	 * 【业务核心 — 母体变异】把一个角色变成 BP_MuTi 母体
	 *
	 * 大厂原则 — 单一入口:
	 *   - 本函数是"对局内任何角色变母体"的唯一入口
	 *   - 触发场景: HandleCountdownExpired 倒计时到期; 后续可能扩展 (感染 / 触发器等)
	 *
	 * @param Target  被选中的目标角色 (必须是有效且活着的 Pawn)
	 * @return true=变异成功, false=变异失败 (Log Error 已记录)
	 */
	UFUNCTION(BlueprintCallable, Category = "Room|Mother")
	bool MutateCharacterToMother(ABaseCharacter* Target);

	/**
	 * 【业务查询 — 已变母体清单】对局内所有母体 (用于 UI / 胜负判定)
	 * 大厂原则 — 镜像 v39: 母体账本是"业务层唯一真理源", 不依赖 GetAllActorsOfClass 散查
	 *
	 * 注意: 不用 UFUNCTION 标记 — 返回 const TArray<TWeakObjectPtr<...>>& 不被 UHT 支持
	 *       (BlueprintPure 反射不允许 const T& 返回值), 走 C++ 直接调
	 *
	 * @return 弱引用数组 (避免持有悬空指针)
	 */
	const TArray<TWeakObjectPtr<ABaseCharacter>>& GetMotherCharacters() const { return MotherCharacters; }

	/**
	 * 【v128 2026.08.02 大厂架构】母体账本注册入口 — 单一真理源
	 *
	 * 业务背景 (用户 2026.08.02 反馈):
	 *   "NearestMotherTarget BB Key 在母体被击杀复活后不更新值"
	 *
	 * 设计原则 (大厂 — 集中调度 + 单一真理源):
	 *   - 母体账本写入 = 母体 Pawn 创建唯一入口 (URoomSpawnSubsystem::MutatePawnToMother)
	 *   - 本接口是该入口写账本的通道,业务层禁止其他路径直接 AddUnique
	 *   - 集中调度: 任何"添加母体到账本"都走这里 → 日志统一 + 大厂原则可观测
	 *
	 * 调用方:
	 *   - URoomSpawnSubsystem::MutatePawnToMother 末尾 (复活链 + 首次变异链)
	 *   - URoomMotherMutationSubsystem::MutateCharacterToMother 兼容保留 (业务层账本不变量,双保险)
	 *
	 * 幂等性:
	 *   - 内部 AddUnique 用 == 比较,同一 Pawn 多次调用安全
	 *
	 * @param MotherPawn 新母体 Pawn (服务器权威,跨场景同步由它 Replicated bIsMother 字段负责)
	 */
	UFUNCTION(BlueprintCallable, Category = "Room|Mother")
	void RegisterMotherPawn(ABaseCharacter* MotherPawn);

	/**
	 * 【v128 2026.08.02 大厂架构】母体账本反注册 (母体死亡时调) — 主动清理失效条目
	 *
	 * 业务背景:
	 *   - TWeakObjectPtr 在 Pawn Destroy 后自动失效,Get()->IsValid=false
	 *   - 但 TWeakObjectPtr 本身还在数组里,长期累积会膨胀 (Round 跨局变 100+ 条)
	 *   - 主动反注册 → 账本清爽 + 业务查询 O(1)
	 *
	 * 调用方:
	 *   - URoomSpawnSubsystem::MutatePawnToMother Step 5 之前 (销毁旧 Pawn 时调)
	 *   - 或 URoomSpawnSubsystem::MutatePawnToMother 末尾 AddUnique 前清除失效项
	 *
	 * @param MotherPawn 要清理的 Pawn (可为 nullptr,安全 no-op)
	 */
	UFUNCTION(BlueprintCallable, Category = "Room|Mother")
	void UnregisterMotherPawn(ABaseCharacter* MotherPawn);

	/**
	 * 【业务查询 — 母体变异计数】对局内已触发母体变异的总次数 (Replicated)
	 * 大厂原则 — SSOT: ARoomGameState::MotherMutationCount 是唯一真理源, 本函数转发
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Room|Mother")
	int32 GetMotherMutationCount() const;

	/**
	 * 【v107 2026.07.28 生化模式 BT】存活母体数 — 严格定义"还在场景里 + 没死 + IsValid"
	 *
	 * 业务规则 (用户 2026.07.28 明确):
	 *   - "只剩一个母体" = AliveMotherCount == 1
	 *   - 死掉的母体不计入 (死亡 Pawn 不再复活才算)
	 *   - 客户端/服务器调用统一读同一份业务账本 (MotherCharacters, TWeakObjectPtr 失效自动跳过)
	 *
	 * 调用方:
	 *   - BTService_UpdateZombieState 每 ZombieTargetRefreshIntervalSeconds 派生写 BB.AliveMotherCount
	 *   - BTDecorator_Zombie_MotherCount (人类分支条件)
	 *
	 * 大厂原则 — 单一真理源:
	 *   - 不 GetAllActorsOfClass 散查
	 *   - MotherCharacters 是业务层唯一账本 (与 v93.1 SpawnedAICharacters 同思路)
	 *   - TWeakObjectPtr 自动失效 → 死亡 Pawn 不计入, 无需主动清理
	 *
	 * @return 当前存活母体数 (>=0)
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Room|Mother")
	int32 GetAliveMotherCount() const;

	/**
	 * 【v107 2026.07.28 生化模式 BT】存活人类数 — 对局内所有活着的非母体 ABaseCharacter
	 *
	 * 定义 (严格):
	 *   - 对局内所有 ABaseCharacter (玩家 Pawn + AI Pawn)
	 *   - !IsDead() && !bIsMother (排除死亡 + 已变异母体)
	 *
	 * 调用方:
	 *   - BTService_UpdateZombieState 写 BB.AliveHumanCount
	 *   - URoomZombieRallySubsystem::CountHumanNearPoint (集合点选点人数统计用)
	 *
	 * 大厂原则 — 单一真理源:
	 *   - 走 URoomSpawnSubsystem::GetAllBattleCharacters() (业务层账本单一入口)
	 *   - 不 GetAllActorsOfClass 散查
	 *
	 * @return 当前存活人类数 (>=0)
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Room|Mother")
	int32 GetAliveHumanCount() const;

	/**
	 * 【v108 大厂架构】新回合开始时重置母体账本
	 *
	 * 根因 (用户 2026.07.29):
	 *   - AliveMotherCount=120 → MotherCharacters 跨回合累积, 10 回合 × 12 变异/回合 = 120
	 *   - StartNextZombieRound() 启动新回合时没有清理 MotherCharacters
	 *
	 * 调用方:
	 *   - URoomLifecycleSubsystem::StartNextZombieRound() 末尾调
	 *   - 业务: 每回合开始, 旧母体状态清零, 重新走倒计时变异
	 *
	 * 大厂原则 — 零兜底:
	 *   - 本函数幂等: 多次调用效果相同
	 *   - 不检查当前回合数 (回合切换逻辑由 Lifecycle 负责)
	 */
	UFUNCTION(BlueprintCallable, Category = "Room|Mother")
	void ResetForNewRound();

protected:
	/** GameMode 引用 (业务调用与权威校验的入口) */
	UPROPERTY(Transient)
	TObjectPtr<ARoomGameMode> GameMode;

	/** 倒计时权威 — 用于反向查询当前倒计时状态 */
	UPROPERTY(Transient)
	TObjectPtr<URoomLifecycleSubsystem> Lifecycle;

	/** Pawn 生成权威 — 用于重建母体 Pawn */
	UPROPERTY(Transient)
	TObjectPtr<URoomSpawnSubsystem> Spawn;

	/** 母体账本 — 业务层唯一真理源 (与 v39 SpawnedAICharacters 同思路) */
	UPROPERTY(Transient)
	TArray<TWeakObjectPtr<ABaseCharacter>> MotherCharacters;

	/**
	 * 【重入守卫 — 服务器本地】本子系统内"已触发变异" 标记
	 *
	 * 大厂原则 — 多层防御:
	 *   - 防御层 1 (本字段): 单进程内防止 HandleCountdownExpired 被重复调
	 *   - 防御层 2 (ARoomGameState::MotherMutationHasFired): 跨进程/客户端防重入
	 *
	 * 注意: 真正的分布式真理源是 GameState.MotherMutationHasFired (Replicated)
	 *       本字段只是性能优化 + 防御性 guard
	 */
	bool bMotherMutationFired_Local = false;

	/**
	 * 【业务核心 — 选母体清单】收集对局内所有"有效人类目标"
	 *
	 * 大厂原则 — 零兜底:
	 *   - 找不到任何人 → 返回空数组 (由调用方决定是否 Log Error)
	 *   - 已变母体的不返回 (否则会出现"母体二次变异")
	 *   - 已死的角色不返回 (死人不可能再变异)
	 *
	 * @return 活着的"人类"角色数组
	 */
	TArray<ABaseCharacter*> GetEligibleHumanTargets();

	/**
	 * 【业务核心 — 随机选母体】从候选清单随机选 1 个
	 *
	 * 大厂原则 — 零兜底:
	 *   - 候选空 → Log Error + return nullptr
	 *   - 选出的 nullptr (中途死亡) → Log Error + return nullptr
	 *
	 * @param Candidates  候选清单 (来自 GetEligibleHumanTargets)
	 * @return 选中的目标, nullptr=失败
	 */
	ABaseCharacter* SelectRandomTarget(const TArray<ABaseCharacter*>& Candidates);

	/**
	 * 【v108 大厂架构新增】按策略过滤候选清单
	 *
	 * 业务规则 (用户 2026.07.30 明确):
	 *   母体变异目标选择策略有 3 种:
	 *   - Random:     不过滤, 玩家+AI 都可选 (默认)
	 *   - AIOnly:     只保留 AI (Cast<AAIController>(Char->GetController()) 非空)
	 *   - PlayerOnly: 只保留玩家 (GetController() 是 APlayerController 派生)
	 *
	 * 大厂原则 — 零兜底:
	 *   - AIOnly / PlayerOnly 候选过滤后可能为空 → 返回空数组 (由调用方 Log Error)
	 *   - Controller 为空 (异常 Pawn) → Log Warning + 跳过 (不阻塞主流程)
	 *
	 * @param Candidates  全部候选清单 (来自 GetEligibleHumanTargets)
	 * @param Policy      选择策略 (来自 Lifecycle.CachedMotherSelectionPolicy)
	 * @return 过滤后的候选清单 (可能为空)
	 */
	TArray<ABaseCharacter*> FilterCandidatesByPolicy(
		const TArray<ABaseCharacter*>& Candidates,
		EMotherSelectionPolicy Policy);
};