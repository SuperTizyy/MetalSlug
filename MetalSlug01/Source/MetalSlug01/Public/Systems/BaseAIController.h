// Copyright (c) 2026.
//
// 【v54.4 大厂架构重构】BaseAIController 删除 UAIProfileAsset 中间层
//
// 关键变化 (v54.4):
//   - 移除 UAIProfileAsset 类 (整个删除)
//   - BaseAIController 不再持有 CurrentProfile 字段
//   - InitializeFromProfile 改名 InitializeFromConfig (直接接 UAIBehaviorConfigSO)
//   - Config 是真理源 (RuntimeConfig->GetConfig() 返回 UAIBehaviorConfigSO)
//   - ConfigSO 新增 LevelPlacedBehaviorTree / LevelPlacedWeaponClass / LevelPlacedAIControllerClass (关卡预放 AI 专用)
//   - 大厅 AI BehaviorTree 来源 = FAIModeRules.BehaviorTree (按游戏模式配置)

#pragma once

// ==========================================
// 头文件包含说明
// ==========================================
#include "CoreMinimal.h"

#include "AIController.h"
#include "Perception/AIPerceptionTypes.h"
#include "GenericTeamAgentInterface.h"
#include "Systems/AI/AIBehaviorTypes.h"
#include "Weapons/BaseWeapon.h"     // 【v54.4】GetDefaultWeaponClass 返回 TSoftClassPtr<ABaseWeapon> 需要完整类型
#include "BaseAIController.generated.h"

// 前置声明
class UAIPerceptionComponent;
class UBehaviorTree;
class UAIRuntimeConfigComponent;
class UAIBehaviorConfigSO;
class UBlackboardComponent;

/**
 * ABaseAIController — 项目所有 AI 控制器的 C++ 基类
 *
 * 【v54.4 重构】配置源 = UAIBehaviorConfigSO (关卡预放 AI) + FAIModeRules (大厅 AI)
 *   - v54.3 之前: ConfigSO 同时给关卡预放 AI 和大厅 AI 提供 BehaviorTree
 *   - v54.4 之后: ConfigSO.BehaviorTree 删除
 *     → 关卡预放 AI: ConfigSO.LevelPlacedBehaviorTree (行为树来源)
 *     → 大厅 AI: FAIModeRules.BehaviorTree (按模式配置)
 *   - RuntimeConfig 组件持有的 Config 就是 ConfigSO, 无中间层
 */
UCLASS()
class METALSLUG01_API ABaseAIController : public AAIController
{
	GENERATED_BODY()

public:
	ABaseAIController();

	/**
	 * 【v42 2026.07.14】NAV Arrival 决策结果 — 距离驱动停车
	 */
	struct FArrivalDecision
	{
		enum class EAction : uint8
		{
			Chase,     // 追击
			LockStop,  // 停车
		};

		EAction Action = EAction::Chase;

		// v42: WalkSpeed > 0 前进, < 0 后退, = 0 停止
		float WalkSpeed = 0.f;
		bool bShouldLock = false;
	};

	/**
	 * @param Distance    AI 与目标距离 (cm)
	 * @param AttackRange 攻击范围 (cm)
	 * @param bAttacking  AI 是否在攻击蒙太奇中
	 * @param bAllowMovementDuringAttack 冲锋型 AI 是否允许攻击中移动
	 * @param bInCooldown 冷却标志
	 * @param Hysteresis 迟滞缓冲 (cm)
	 * @param Movement   移动参数
	 * @return 决策结构
	 */
	static FArrivalDecision ComputeArrivalDecision(
		float Distance, float AttackRange,
		bool bAttacking, bool bAllowMovementDuringAttack,
		bool bInCooldown, float Hysteresis,
		const struct FAIMovementParams& Movement);

	// C++ 与 BT 通信的攻击状态
	UFUNCTION(BlueprintCallable, Category = "AI|Combat")
	void SetCurrentlyAttacking(bool bAttacking);

	UFUNCTION(BlueprintPure, Category = "AI|Combat")
	bool IsCurrentlyAttacking() const { return bIsCurrentlyAttacking; }

	UFUNCTION(BlueprintCallable, Category = "AI|Combat")
	void SetInAttackCooldown(bool bInCooldown);

	UFUNCTION(BlueprintPure, Category = "AI|Combat")
	bool IsInAttackCooldown() const { return bIsInAttackCooldown; }

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI|Combat")
	bool bIsInAttackCooldown = false;

	/**
	 * 【v54.4 大厂架构重构】AI 初始化入口（行为参数 + 行为树）
	 *
	 * @param InConfig             ConfigSO — 提供行为参数 (Combat/Perception/Movement)
	 * @param BehaviorTreeOverride 可选 — 行为树来源
	 *   - nullptr (默认): 从 ConfigSO.LevelPlacedBehaviorTree 读 (关卡预放 AI 路径)
	 *   - 非空: 直接用这个 BT (大厅 AI 路径: SpawnAIInternal 从 ModeRules.BehaviorTree 传)
	 *
	 * 调用方:
	 *   - 关卡预放 AI: AMeleeAIController::SetupMeleeAI → InitializeFromConfig(ConfigSO, nullptr)
	 *     → 读 ConfigSO.LevelPlacedBehaviorTree (关卡预放 AI 的 BT)
	 *   - 大厅 AI: URoomSpawnSubsystem::SpawnAIInternal → InitializeFromConfig(ConfigSO, ModeRules.BehaviorTree)
	 *     → 用 ModeRules.BehaviorTree (大厅 AI 的 BT, 按游戏模式区分)
	 *
	 * 大厂原则 — 职责分离:
	 *   - ConfigSO = 行为参数 (Combat/Perception) + 关卡预放 AI 专用配置
	 *   - BehaviorTree 独立传参, 不绑死在 ConfigSO 里
	 */
	UFUNCTION(BlueprintCallable, Category = "AI")
	void InitializeFromConfig(UAIBehaviorConfigSO* InConfig, UBehaviorTree* BehaviorTreeOverride = nullptr);

	/**
	 * 【v201.10 大厂架构新增】重启 BT + 清空 Blackboard
	 *
	 * 业务场景: RestartZombieRoundPlayers (每小局开始前)
	 *   - 旧 (v201.9 之前): BT 在第二小局仍持有第一小局的 BB 数据 (TargetActor/CooldownEndTime 等)
	 *   - 用户反馈: "AI 在每小局开始前应该先清理所有黑板键的数据再启动"
	 *   - 修复: 停 BT → 清空所有 BB Key → 重启 BT (让 BT Service 重新派生)
	 *
	 * 调用方: URoomSpawnSubsystem::RestartZombieRoundPlayers 末尾
	 *
	 * 大厂原则:
	 *   - 单一入口: 所有"重启 BT"需求都走这一处 (避免散落)
	 *   - 零兜底: BT/BB 为空 → Log Warning + return (不静默跳过)
	 */
	UFUNCTION(BlueprintCallable, Category = "AI")
	void RestartBehaviorTreeAndClearBlackboard();

	UFUNCTION(BlueprintCallable, Category = "AI")
	void SetDifficultyTier(EAIDifficultyTier NewTier);

	UFUNCTION(BlueprintPure, Category = "AI")
	UAIRuntimeConfigComponent* GetRuntimeConfig() const { return RuntimeConfig; }

	/**
	 * 【v54 重构】读取当前生效的 ConfigSO (真理源)
	 *
	 * 替代 v53 之前的 GetCurrentProfile() — Profile 中间层已删除
	 * 调用方: BT/BTTask 通过 GetConfig() 读 ConfigSO 的所有参数
	 */
	UFUNCTION(BlueprintPure, Category = "AI")
	const UAIBehaviorConfigSO* GetConfig() const;

	// 运行时阵营真理源 (v26)
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI|Spawn")
	FGameplayTag CachedFactionTag;

	UFUNCTION(BlueprintPure, Category = "AI|Spawn")
	FGameplayTag GetCachedFactionTag() const { return CachedFactionTag; }

	UFUNCTION(BlueprintCallable, Category = "AI|Spawn")
	void SetCachedFactionTag(const FGameplayTag& InTag) { CachedFactionTag = InTag; }

	// ==========================================
	// 【v109 大厂架构 — AI 母体状态运行时真理源】CachedIsMother
	// ==========================================
	//
	// 设计动机 (用户原话 2026.07.31):
	//   "生化模式：ai是母体。被打死后成了人类，这是错的，应该还是母体。
	//    请参照玩家的业务逻辑。"
	//
	// 业务核心 (生化模式, 镜像 PS->bIsMother):
	//   - 玩家母体死亡 → 复活时读 PS->bIsMother=true → 走 MutatePawnToMother 路径
	//   - AI 母体死亡 → 复活时读 AIC->CachedIsMother=true → 走 MutatePawnToMother 路径
	//   - 真理源对称: 玩家有 PS->bIsMother (Server 权威, Replicated), AI 必须在 AIC 上有等价字段
	//
	// 为什么需要新字段 (而不是直接读 Pawn->bIsMother):
	//   - Pawn->bIsMother 死时随 Pawn.Destroy() 销毁 (死 Pawn = 回收, 字段失效)
	//   - 复活时 Controller 复用, 但 Pawn 已不存在 → 读 Pawn->bIsMother 永远 false
	//   - AIC.CachedIsMother 是"上一次存活时的状态", Pawn 销毁后仍存活于 Controller 上
	//   - 与 CachedFactionTag / CachedAIPawnClass / CachedWeaponID 一致: 复活真理源都在 Controller
	//
	// 不复制 (大厂原则 — 镜像 CachedFactionTag):
	//   - AI 不跨网络切换阵营 (服务器权威), 客户端永远不知道也不需要知道 AI 的 CachedIsMother
	//   - 客户端通过 Pawn.bIsMother (DOREPLIFETIME) + OnRep_bIsMother 知道当前 Pawn 是母体
	//   - 与 CachedFactionTag / CachedAIPawnClass / CachedWeaponID 一致: 都不 Replicate
	//
	// 不破坏刀战模式:
	//   - 刀战模式从不调 MutatePawnToMother → CachedIsMother 永远是 false → RequestRespawn 走老路径
	// ==========================================
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI|Spawn")
	bool CachedIsMother = false;

	UFUNCTION(BlueprintPure, Category = "AI|Spawn")
	bool GetCachedIsMother() const { return CachedIsMother; }

	UFUNCTION(BlueprintCallable, Category = "AI|Spawn")
	void SetCachedIsMother(bool bInIsMother) { CachedIsMother = bInIsMother; }

	// ============================================================
	// 【v111 大厂架构 — 复活 Timer 生命周期绑定】MotherRespawnTimerHandle
	// ============================================================
	//
	// 业务核心 (用户 2026.07.31 反馈):
	//   "AI是母体被打死后不复活了"
	//
	// 根因 (Session1.log 2026.07.31):
	//   - Die() 派发 RequestRespawn → SetTimer(3s) → Timer 存储在 Subsystem 局部变量
	//   - 3s 后 Timer 回调: Controller=INVALID → AIController 在 3s 内被销毁
	//   - 结果: 母体无法复活
	//
	// v111 大厂架构修复:
	//   - 旧: Timer 存储在 Subsystem 局部变量, Controller 销毁时 Timer 回调找不到 Controller
	//   - 新: Timer 存储在 AIController 上, 生命周期与 Controller 绑定
	//   - 回调中直接用 this (AIController), 不需要 WeakPtr
	//
	// 镜像对称:
	//   - 玩家复活 Timer: PlayerController.RespawnTimerHandle (已有)
	//   - AI 复活 Timer: AIController.MotherRespawnTimerHandle (新增)
	//
	// 不破坏刀战模式:
	//   - 刀战模式 AI 不调 MutatePawnToMother → 不设 Timer → 字段永远是默认值
	//   - 不影响现有逻辑
	// ============================================================
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI|Spawn")
	FTimerHandle MotherRespawnTimerHandle;

	// ============================================================
	// 【v54 大厂架构 — 运行时真理源 (复活用)】AI Pawn Class + WeaponID
	// ============================================================
	//
	// 设计动机 (用户原话 2026.07.16):
	//   "房间页面添加的ai，应该是知道每个ai的武器，角色，阵营的，
	//    且应该存在内存中不是吗？所以应该不存在ai复活因为不知道武器，
	//    角色，阵营，而导致复活不了的情况发生。"
	//
	// v53-v54 之前的反模式 (用户反馈 "AI 杀着杀着全没了"):
	//   - RequestRespawn 复活 AI 时读 Profile.PawnClass / Profile.WeaponID / Profile.FactionTag
	//   - 这些字段在 DA_AIProfile.uasset 里没配 → Log Error → 拒绝复活 → 场上 AI 逐渐消失
	//
	// v54 修复 (大厂原则 - 单一真理源 + 职责分离):
	//   - 关卡预放 AI (level-placed): 走 ConfigSO 默认值 (DA_AIBehaviorConfig_*.uasset)
	//   - 大厅入队 AI: 走 FPendingAIEntry (UI 入队时已记录 AIPawnClass/WeaponID/FactionTag)
	//   - 复活 AI: 走 Controller.CachedXXX (Spawn 时锁定, 复活直接复用)
	//
	// 单一真理源链 (v54):
	//   - 大厅 AI: UI ComboBox → FPendingAIEntry.{AIPawnClass, WeaponID, FactionTag}
	//                            ↓ Spawn 时写入
	//              Controller.Cached{AIPawnClass, WeaponID, FactionTag}
	//                            ↓ 死亡后
	//              RequestRespawn → 直接读 Controller 内存字段, 不再反查 ConfigSO
	//
	//   - 关卡预放 AI: UE 自动 Possess → AMeleeAIController::SetupMeleeAI → 读 ConfigSO.DefaultXXX
	//                                  → 写入 Controller.Cached* (运行时真理源)
	//                                  → RequestRespawn 走 Cached* 路径
	//
	// 真理源优先级 (v54.2 复活路径):
	//   1. Controller.CachedXXX (大厅 AI + 关卡预放 AI 复活) - 内存真理
	//   2. ConfigSO.DefaultXXX (关卡预放 AI 第一次 Spawn fallback, 例如 DefaultWeaponRowName) - 配置真理
	//   3. 都为空 → Log Error + return (零兜底, 强制修复)
	//
	// 这是用户洞察的直接落地: 房间页面添加的 AI 的所有 Spawn 信息都在内存里, 不依赖 ConfigSO 反查
	// ============================================================

	/**
	 * 【v54 大厂架构】运行时真理源 - AI Pawn Class
	 *
	 * 写入时机:
	 *   - AMeleeAIController::SetupMeleeAI 末尾 (关卡预放 AI 路径)
	 *   - ARoomGameMode::SpawnAIInternal Possess 之前 (大厅 AI 路径)
	 *
	 * 读取时机:
	 *   - URoomSpawnSubsystem::RequestRespawn AI 路径 (复活用)
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI|Spawn")
	TSubclassOf<class ABaseCharacter> CachedAIPawnClass;

	UFUNCTION(BlueprintPure, Category = "AI|Spawn")
	TSubclassOf<class ABaseCharacter> GetCachedAIPawnClass() const { return CachedAIPawnClass; }

	UFUNCTION(BlueprintCallable, Category = "AI|Spawn")
	void SetCachedAIPawnClass(TSubclassOf<class ABaseCharacter> InClass) { CachedAIPawnClass = InClass; }

	/**
	 * 【v54.3 大厂架构重构 — 升级为 Class 强类型真理源】运行时 AI Weapon Class (BP 软引用)
	 *
	 * 用户原话 2026.07.16:
	 *   "DefaultWeaponRowName 属性为什么不直接引用 DT_WeaponInfo 表的 WeaponBlueprint 内容啊"
	 *
	 * 旧 (v54 — FString WeaponID):
	 *   - CachedWeaponID: 写 Request.WeaponID (FString)
	 *   - 复活路径: URoomSpawnSubsystem::RequestRespawn 读 CachedWeaponID → 查 DT_WeaponInfo → 拿 Class
	 *   - 中间层冗余 (DT_WeaponInfo), 字符串拼错永远不可见
	 *
	 * 新 (v54.3 — Class 强类型):
	 *   - CachedWeaponClass: 直接存 Class (TSubclassOf<ABaseWeapon>)
	 *   - 复活路径: 直接调 Pawn->RequestWeaponSpawn(CachedWeaponClass), 跳过 DT_WeaponInfo
	 *   - 单一真理源: Controller.CachedWeaponClass 是唯一运行时武器 BP
	 *
	 * 写入时机:
	 *   - ARoomGameMode::SpawnAIInternal 末尾 (大厅 AI: 拿 Request.WeaponID 查 DT_WeaponInfo 拿 Class)
	 *   - AMeleeAIController::SetupMeleeAI (关卡预放: 读 ConfigSO.DefaultWeaponClass.LoadSynchronous())
	 *
	 * 读取时机:
	 *   - URoomSpawnSubsystem::RequestRespawn AI 路径 (直接传 Class 给 RequestWeaponSpawn)
	 *
	 * ⚠️ 与 v54 CachedWeaponID (FString) 共存过渡期:
	 *   - 旧 CachedWeaponID 字段保留 (复活读兼容), 但优先用 Class
	 *   - 后续版本可彻底删除 FString 字段
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI|Spawn")
	TSubclassOf<class ABaseWeapon> CachedWeaponClass;

	UFUNCTION(BlueprintPure, Category = "AI|Spawn")
	TSubclassOf<class ABaseWeapon> GetCachedWeaponClass() const { return CachedWeaponClass; }

	UFUNCTION(BlueprintCallable, Category = "AI|Spawn")
	void SetCachedWeaponClass(TSubclassOf<class ABaseWeapon> InClass) { CachedWeaponClass = InClass; }

	/**
	 * 【v54 大厂架构】运行时真理源 — AI Weapon ID (FString, 兼容旧复活链路)
	 *
	 * 历史:
	 *   - v54: 新增, 复活路径用 FString 查 DT_WeaponInfo
	 *   - v54.3: 与 CachedWeaponClass (Class 强类型) 共存, 优先用 Class 路径
	 *
	 * 写入时机 (旧链路, 兼容):
	 *   - ARoomGameMode::SpawnAIInternal 末尾 (大厅 AI 路径, 来自 Request.WeaponID)
	 *
	 * 读取时机:
	 *   - 旧复活链路使用 (新版本优先用 CachedWeaponClass)
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI|Spawn", meta = (DeprecatedProperty))
	FString CachedWeaponID;

	UFUNCTION(BlueprintPure, Category = "AI|Spawn")
	FString GetCachedWeaponID() const { return CachedWeaponID; }

	UFUNCTION(BlueprintCallable, Category = "AI|Spawn")
	void SetCachedWeaponID(const FString& InWeaponID) { CachedWeaponID = InWeaponID; }

	/**
	 * 【v99.1 大厂架构 — 母体复活位置真理源】AI 上次死亡 Transform
	 *
	 * 写入时机:
	 *   - UCombatDeathComponent::ExecuteDeathLocal 头部 (HasAuthority) — 死亡瞬间缓存
	 *   - 复活前由 URoomSpawnSubsystem::MutatePawnToMother 读取并清空
	 *
	 * 读取时机:
	 *   - URoomSpawnSubsystem::MutatePawnToMother 复活链路径 (OldPawn 为空时)
	 *
	 * 大厂原则 — 单一真理源:
	 *   - 旧版复活链用 ZeroVector → SpawnActor(0,0,0) → 原点碰撞失败
	 *   - v99.1: 死亡时主动缓存, 复活时读取, 移除所有原点兜底
	 *
	 * 跨模式安全:
	 *   - 刀战模式不调本字段(刀战走出生点路径,不读死亡 Transform)
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI|Spawn")
	FTransform CachedDeathTransform;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI|Spawn")
	bool bHasCachedDeathTransform = false;

	UFUNCTION(BlueprintCallable, Category = "AI|Spawn")
	void SetCachedDeathTransform(const FTransform& InTransform)
	{
		CachedDeathTransform = InTransform;
		bHasCachedDeathTransform = true;
	}

	UFUNCTION(BlueprintCallable, Category = "AI|Spawn")
	void ClearCachedDeathTransform() { bHasCachedDeathTransform = false; }

	/**
	 * 【v54.4 大厂架构】关卡预放 AI 默认 AIController Class
	 *
	 * 调用方: AMeleeAIController::SetupMeleeAI (关卡预放 AI 路径)
	 * 真理源: ConfigSO.LevelPlacedAIControllerClass
	 */
	UFUNCTION(BlueprintPure, Category = "AI|Spawn")
	TSubclassOf<class AAIController> GetDefaultAIControllerClass() const;

	/**
	 * 【v54.4 大厂架构重构】关卡预放 AI 默认武器
	 *
	 * 调用方: AMeleeAIController::SetupMeleeAI (关卡预放 AI 路径)
	 * 真理源: ConfigSO.LevelPlacedWeaponClass (TSoftClassPtr<ABaseWeapon>)
	 *
	 * 返回 TSoftClassPtr 而不是 TSubclassOf — 与 ConfigSO 字段类型严格一致
	 * 调用方: GetDefaultWeaponClass().LoadSynchronous() 拿强类型 Class
	 */
	UFUNCTION(BlueprintPure, Category = "AI|Spawn")
	TSoftClassPtr<class ABaseWeapon> GetDefaultWeaponClass() const;

	/**
	 * 【v54 重构 — 从 DA_AIProfile 吸收】关卡预放 AI 复活无敌期
	 *
	 * 调用方: AMeleeAIController::SetupMeleeAI 末尾激活
	 * 真理源: ConfigSO.SpawnInvincibilitySeconds
	 */
	UFUNCTION(BlueprintPure, Category = "AI|Spawn")
	float GetSpawnInvincibilitySeconds() const;

	// 攻击间隔 — 直接读 ConfigSO.Combat.AttackCooldown (v54 简化, 无 Profile 中间层)
	UFUNCTION(BlueprintPure, Category = "AI|Combat")
	float GetEffectiveAttackInterval() const;

	// 攻击伤害
	UFUNCTION(BlueprintPure, Category = "AI|Combat")
	float GetEffectiveAttackDamage() const;

	// 当前目标
	UFUNCTION(BlueprintPure, Category = "AI|Combat")
	AActor* GetCurrentTargetActor() const;

	// 攻击范围
	UFUNCTION(BlueprintPure, Category = "AI|Combat")
	float GetEffectiveAttackRange() const;

	// 攻击中是否允许移动
	UFUNCTION(BlueprintPure, Category = "AI|Combat")
	bool GetAllowMovementDuringAttack() const;

	// AttackRangeHysteresis (deprecated, always returns 0)
	UFUNCTION(BlueprintPure, Category = "AI|Combat", meta = (DeprecatedFunction, DeprecationMessage = "v13 Hysteresis removed. Returns 0."))
	float GetAttackRangeHysteresis() const;

	// 计算两角色中心点距离 (cm)
	UFUNCTION(BlueprintPure, Category = "AI|Combat")
	static float ComputeActorCenterDistance(const AActor* A, const AActor* B);

	// 计算角色中心点
	UFUNCTION(BlueprintPure, Category = "AI|Combat")
	static FVector ComputeActorCenter(const AActor* A);

	/**
	 * @param bLock true=锁步, false=恢复
	 */
	UFUNCTION(BlueprintCallable, Category = "AI|Combat")
	void LockMovementForCooldown(bool bLock);

	// 阵营接口
	virtual void SetGenericTeamId(const FGenericTeamId& NewTeamID) override;
	virtual FGenericTeamId GetGenericTeamId() const override;
	virtual ETeamAttitude::Type GetTeamAttitudeTowards(const AActor& Other) const override;

	// 主动扫描最近敌人
	UFUNCTION(BlueprintCallable, Category = "AI")
	AActor* ScanForNearestEnemy(ACharacter* MyCharacter, float ScanRange = -1.f);

	// 单元测试
	static void SelfTestArrivalDecision();

protected:
	virtual void BeginPlay() override;
	virtual void OnPossess(APawn* InPawn) override;
	virtual void BeginDestroy() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI")
	UAIPerceptionComponent* AIPerception;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI")
	TObjectPtr<UAIRuntimeConfigComponent> RuntimeConfig;

	UFUNCTION()
	virtual void OnTargetDetected(AActor* Actor, FAIStimulus Stimulus);

	// 派生类钩子
	void StartBehaviorTreeFromConfig();

private:
	void OnConfigLoaded();
	void StartBehaviorTreeFromConfigInternal();
	void RunLegacyBehaviorTree();
	void TryStartBehaviorTreeOrWaitForBattleStart();

	FDelegateHandle OnBattleStartedHandle;

	// 【v54.4 大厂架构重构】大厅 AI 的 BT 来源 (按游戏模式)
	//   - nullptr: 关卡预放 AI 路径 → StartBehaviorTreeFromConfigInternal 读 ConfigSO.LevelPlacedBehaviorTree
	//   - 非空: 大厅 AI 路径 → StartBehaviorTreeFromConfigInternal 直接用这个 BT
	TWeakObjectPtr<UBehaviorTree> PendingBehaviorTreeOverride;

	bool bIsCurrentlyAttacking = false;
	bool bBehaviorTreeStarted = false;

	void ConfigurePerceptionFromConfig();
	void DiagnoseAndLogBootStatus() const;

	// 冷却锁步状态
	bool bMovementLockedForCooldown = false;
};