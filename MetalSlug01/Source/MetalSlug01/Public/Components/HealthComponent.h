// ==========================================
// 生命值 Component 【2026-06-15 重构: 升级为数据权威持有者】
// 目的: BaseCharacter 的血量/受伤/死亡数据全部下沉到本 Component
// 优势:
//   1. 业务逻辑独立测试/可被多个角色类复用
//   2. 数据权威唯一,避免双轨不一致 (修复前 BaseCharacter 与 Component 各持一份)
//   3. 网络同步在 Component 内自治,Actor 只编排行为
// ==========================================
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "HealthComponent.generated.h"

// 死亡多播: 通知 Actor 触发死亡流程
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnDeath);

// 血量改变多播: 供 UI/HUD/特效订阅
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnHealthChanged, float, NewHealth);

// 【2026.07.11 P0 大厂架构】无敌期状态变化多播 (进入/退出)
//
// 用途:
//   - HUD 显示倒计时 UI 的"激活/隐藏"开关
//   - 特效/材质的闪烁 (视觉反馈)
//   - AI 蓝图可以订阅以避免在无敌期尝试攻击 (避免误判)
//
// 时序保证:
//   - 服务器: HealthComponent::ActivateInvincibility 内 Broadcast (首次激活)
//   - 服务器: ExpireInvincibility_Internal 内 Broadcast (到期)
//   - 客户端: 通过 Replicated 字段 + OnRep 自动获得最终状态
//     但 OnRep 不会广播, 仅本机 OnRep_InvincibilityChanged 内部 Broadcast (每机器一次)
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnInvincibilityChanged, bool, bIsNowInvincible);

// 【v201.5 大厂架构新增】复活移动锁定状态变更事件
//
// 业务规则: 复活后 N 秒内无法移动 (N = RespawnDelaySeconds)
//           玩家和 AI 都走这个机制 (通用)
//
// 订阅方:
//   - ABaseCharacter::OnRespawnMovementLockedChanged: 收到事件后修改 MaxWalkSpeed
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnRespawnMovementLockedChanged, bool, bIsLocked, float, Duration);

UCLASS(ClassGroup = (MetalSlug), meta = (BlueprintSpawnableComponent))
class METALSLUG01_API UHealthComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UHealthComponent();

	/**
	 * 【2026-06-15 新增】: 初始化(在 Actor BeginPlay 中调用,从外部传入最大血量)
	 * 背景: 血量初始值来自 BaseCharacter 的配置数据表,Component 不应写死
	 */
	void InitializeHealth(float InMax);

	/**
	 * 【2026-06-15 新增】: 服务器应用伤害
	 * 内部会通过 Server RPC 走网络同步(实际上 Server 端直接调用即可,Component 已 Replicated)
	 * 返回真实扣除血量
	 */
	UFUNCTION(BlueprintCallable, Category = "Health")
	float ApplyDamage(float DamageAmount);

	/** 治疗 (服务器调用,自动同步) */
	UFUNCTION(BlueprintCallable, Category = "Health")
	void Heal(float HealAmount);

	// === 访问器 (替代 BaseCharacter 上的 GetCurrentHealth/GetMaxHealth/IsDead) ===
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Health")
	float GetCurrent() const { return CurrentHealth; }

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Health")
	float GetMax() const { return MaxHealth; }

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Health")
	float GetPercent() const { return MaxHealth > 0 ? CurrentHealth / MaxHealth : 0.0f; }

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Health")
	bool IsDead() const { return bIsDead; }

	/**
	 * 【v100.3 大厂架构 — 单一真理源】是否满血
	 *
	 * 业务规则 (用户 2026.07.26): 母体回血触发条件之一 = "满血状态"
	 *   - 旧版: HealthRegenComponent 不知道"满血"概念, 任何时候都能回血
	 *   - 新版: 满血检验 = HealthComponent 权威数据源 (大厂原则 - 单一真理源)
	 *
	 * 实现细节:
	 *   - 用 KINDA_SMALL_NUMBER 容差 (避免浮点误差: 100.0 vs 99.99999)
	 *   - MaxHealth > 0 守卫 (除 0 保护)
	 *   - BlueprintPure 可被蓝图调用, BP 类也能复用
	 *
	 * 调用方: HealthRegenComponent::TickComponent (开始回血前先校验)
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Health")
	bool IsFullHealth() const
	{
		return MaxHealth > 0.0f && CurrentHealth >= MaxHealth - KINDA_SMALL_NUMBER;
	}

	// === 事件订阅 (供 BaseCharacter::BeginPlay 中绑定) ===
	/** 死亡事件: Actor 订阅后触发 Die() 流程 */
	UPROPERTY(BlueprintAssignable, Category = "Health")
	FOnDeath OnDeath;

	/** 血量改变事件: HUD/特效订阅 */
	UPROPERTY(BlueprintAssignable, Category = "Health")
	FOnHealthChanged OnHealthChanged;

	// ============================================================
	// 【2026.07.11 P0 大厂架构】复活无敌期 (Spawn Invincibility Frame)
	// ============================================================
	//
	// 单一真理源: HealthComponent 是无敌期数据权威持有者
	//   - 服务器 ActivateInvincibility → 写 Replicated 字段 → 客户端 OnRep 自动同步
	//   - 计时到期由服务器 Timer 触发 → 自动清字段 → 客户端 OnRep → bIsInvincible=false
	//   - 所有扣血入口只需在 ApplyDamage 守卫 IsInvincible() 即可
	//
	// 职责对等: 跟 bIsDead 同款 — 数据在 Component, Actor 只编排行为
	// 防止散落: 不在 ABaseCharacter / ABaseWeapon / BTTask 重复定义无敌字段
	//            这就是"单一真理源"消除散落 Bool 字段的设计威力
	//
	// 用户决策 (2026.07.11 AskQuestion 选项 A):
	//   - 数据源放 HealthComponent (✅)
	//   - 默认 3 秒 (✅)
	//   - 配置错误 (≤0) → 静默跳过激活 (✅)
	//   - 战斗开始 SpawnAllPlayersIntoBattle 也要激活 (✅)
	// ============================================================

	/**
	 * 激活无敌期 (仅服务器有效)
	 *
	 * 设计原则:
	 *   - Duration <= 0 → 跳过激活 (零兜底: 既不报警, 也不强制使用默认值)
	 *   - 已激活时再次调用 → 取较晚到期时间 (拒绝缩短, 防止反复激活欺骗无敌)
	 *
	 * 调用方:
	 *   - ABaseCharacter::ActivateSpawnInvincibility (玩家/AI 复活统一入口)
	 *   - RoomGameMode::RequestRespawn / SpawnAllPlayersIntoBattle 末尾
	 *
	 * 副作用:
	 *   - 设 InvincibilityExpiresAtWorldTime = World->GetTimeSeconds() + Duration
	 *   - SetTimer 到期 → ExpireInvincibility_Internal → Broadcast OnInvincibilityChanged(false)
	 *   - 立即 Broadcast OnInvincibilityChanged(true)
	 */
	UFUNCTION(BlueprintCallable, Category = "Health|Invincibility")
	void ActivateInvincibility(float DurationSeconds);

	/**
	 * 取消无敌期 (任何机器都可以调, 但仅服务器真正修改字段)
	 *
	 * 调用场景:
	 *   - 死亡时强制取消 (防止死亡→复活边缘 case 的无敌残留)
	 *   - 调试/反作弊强制清零
	 */
	UFUNCTION(BlueprintCallable, Category = "Health|Invincibility")
	void DeactivateInvincibility();

	/** 是否当前处于无敌状态 (任何机器可读, 伤害拦截时调用) */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Health|Invincibility")
	bool IsInvincible() const { return bIsInvincible; }

	/**
	 * 获取无敌期剩余秒数
	 *
	 * 返回:
	 *   - >  0 : 还在无敌期 (精确剩余秒数)
	 *   - <= 0 : 未激活 或 已到期
	 *
	 * 用途: HUD 显示倒计时数字 + 进度条
	 *
	 * 【v40.9 大厂架构 — 真理源是派生字段, 不是衍生 bool】
	 *   - 用 GameState->GetServerWorldTimeSeconds() 计算 (服务器权威时钟, 自动补偿网络延迟)
	 *   - 派生于 InvincibilityExpiresAtWorldTime (Replicated 字段), 不依赖 bIsInvincible
	 *   - 即使 bIsInvincible 还没同步过来 (OnRep 时序竞争), 只要 ExpiresAt > Now → 还在无敌期
	 *   - 避免 UE bool 字段复制的边缘 case (初始值不同步 / true→false 间隔 < Bind 时间)
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Health|Invincibility")
	float GetInvincibilityRemainingSeconds() const;

	/** 无敌状态切换事件: HUD/特效订阅 (进入/退出无敌都 Broadcast) */
	UPROPERTY(BlueprintAssignable, Category = "Health|Invincibility")
	FOnInvincibilityChanged OnInvincibilityChanged;

	/**
	 * 【2026.07.14 新增】获取无敌期到期时间（World Time 秒）
	 * 用途: UI 层计算进度条进度
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Health|Invincibility")
	float GetInvincibilityExpiresAtWorldTime() const { return InvincibilityExpiresAtWorldTime; }

	/**
	 * 【2026.07.14 新增】获取无敌期总时长（秒）
	 *
	 * 计算方式: Duration = ExpiresAt - (ExpiresAt - Remaining) = ExpiresAt - (Now - ActivationTime)
	 *          由于没有存储 ActivationTime，用: Duration = Remaining + (Now - ActivationTime)
	 *          = ExpiresAt - Now + (Now - ActivationTime)
	 *          = ExpiresAt - ActivationTime
	 *          = 在激活时写入的总时长
	 *
	 * 用途: RespawnProgressWidget 显示进度条总长度（不是每帧动态计算的剩余时间）
	 *
	 * @return 无敌期总时长（秒），未激活时返回 0
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Health|Invincibility")
	float GetInvincibilityDuration() const;

protected:
	// === 复制支持 (2026-06-15 新增) ===
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/**
	 * 【2026-06-15 新增】: OnRep 回调,用于在客户端触发 HUD 刷新
	 * 注意: OnRep_Health 只在"值变化"时触发,初值同步不会触发
	 *       所以 HUD 初值刷新应通过 InitializeHealth 后手动调用
	 */
	UFUNCTION()
	void OnRep_CurrentHealth();

	/**
	 * 【2026-07-01 新增】OnRep 回调: 客户端收到 bIsDead=true 时触发死亡事件
	 * 解决原架构 bug:
	 *   旧 bIsDead 是普通 Replicated,客户端不会触发任何回调
	 *   → 客户端只能依赖 Multicast_Die RPC, 如果 RPC 失败 / 丢失 / 时序错乱
	 *     客户端永远不会进入死亡流程 → 布娃娃/武器不消失
	 * 新架构:
	 *   - 服务器: HealthComponent::ApplyDamage 内 OnDeath.Broadcast (已有)
	 *   - 客户端: 本回调触发 OnDeath.Broadcast (新增) → 客户端独立进入死亡流程
	 *   这样死亡事件在所有机器上**统一由 HealthComponent 事件总线驱动**
	 */
	UFUNCTION()
	void OnRep_bIsDead();

	/**
	 * 【2026.07.11 P0 新增】OnRep 回调: 客户端收到 bIsInvincible 变化时广播无敌事件
	 *
	 * 设计 - 与 OnRep_bIsDead 同构:
	 *   - 服务器: ActivateInvincibility/ExpireInvincibility_Internal 主动 Broadcast (服务器跑一次)
	 *   - 客户端: 通过 Replicated 自动同步, 本回调 Broadcast
	 *   - 双发保证: 服务器自己不会进 OnRep (服务器已主动 Broadcast) — 不会重复触发
	 *
	 * 为什么 ReplicatedUsing 而不是 OnRep + 手动 Broadcast:
	 *   - 单字段 OnRep 自动保证"值变了才触发", 避免重复 Broadcast
	 *   - 客户端没收到首次激活 (BC 之前已激活) → 客户端字段是初值 false, 服务器激活后 replicate, OnRep 触发
	 *   - 这是 UE 大厂标准组件设计模式 (跟 bIsDead 完全对称)
	 */
	UFUNCTION()
	void OnRep_InvincibilityChanged();

	/** Timer 到期回调: 清除无敌状态 (服务器 only) */
	void ExpireInvincibility_Internal();

	/**
	 * 【v93.4 大厂架构修复】最大血量 — 必须 Replicated 才能客户端正确同步
	 *
	 * 业务规则 (用户 2026.07.25 明确):
	 *   - 母体变异时, HealthComponent->InitializeHealth(200) 写 MaxHealth=200
	 *   - 客户端 HUD 读 GetMax() 必须返回 200, 否则血条进度算错 (CurrentHealth/MaxHealth)
	 *
	 * 旧 (v93.3 之前) 反模式:
	 *   - UPROPERTY 没标 Replicated → MaxHealth 是服务器本地字段, 客户端永远是默认值 100
	 *   - 母体变异时服务器 MaxHealth=200, CurrentHealth=200, 但客户端 MaxHealth=100
	 *   - HUD 显示: CurrentHealth=200 / MaxHealth=100 (看似满血,但客户端读到 100, 进度条算错)
	 *   - 客户端 OnRep_CurrentHealth 只触发 OnHealthChanged(CurrentHealth), 没传 MaxHealth
	 *
	 * 新 (v93.4) 单一真理源:
	 *   - MaxHealth Replicated → 客户端自动收到 200
	 *   - InitializeHealth 末尾 broadcast OnHealthChanged(MaxHealth) → 服务器同步通知 HUD
	 *   - 客户端 OnRep_CurrentHealth 同步 broadcast OnHealthChanged(CurrentHealth)
	 *   - HUD 在收到广播后调用 GetMax() → 正确读到 200
	 *
	 * 不破坏现有逻辑 (大厂原则 — 零副作用):
	 *   - 加 Replicated 不会影响 ApplyDamage / Heal 等任何现有路径
	 *   - 默认 100 保持不变, 所有非母体路径都是 100
	 *   - 仅母体变异时显式改 200
	 */
	UPROPERTY(Replicated, EditDefaultsOnly, BlueprintReadOnly, Category = "Health")
	float MaxHealth = 100.0f;

	/**
	 * 【2026-06-15 升级】: 改为 Replicated
	 * 原版只是普通字段,服务器修改后客户端看不到
	 */
	UPROPERTY(ReplicatedUsing = OnRep_CurrentHealth, VisibleAnywhere, BlueprintReadOnly, Category = "Health")
	float CurrentHealth = 100.0f;

	UPROPERTY(ReplicatedUsing = OnRep_bIsDead, VisibleAnywhere, BlueprintReadOnly, Category = "Health")
	bool bIsDead = false;

	// ============================================================
	// 【2026.07.11 P0 新增】无敌期字段 — 单一真理源
	// ============================================================
	//
	// 为什么用 WorldTime 而不是 ServerTime:
	//   - World->GetTimeSeconds() 是 World 级别, 跨 Level 不准确
	//   - 但 PIE 单 Level 场景完全够用, 跟 UE 标准 ServerTime 一致
	//   - 客户端只读不写, 不存在时钟漂移问题 (client = server 同步复制)
	//
	// Replicated 用 Notify 模式:
	//   - 客户端读到 `bIsInvincible = true` 立即广播 OnInvincibilityChanged(true)
	//   - 客户端读到 `bIsInvincible = false` 立即广播 OnInvincibilityChanged(false)
	//   - 服务器在 Activate/Expire 内部手动 Broadcast 一次, 不会重复 (服务器不进 OnRep)
	UPROPERTY(ReplicatedUsing = OnRep_InvincibilityChanged, VisibleAnywhere, BlueprintReadOnly, Category = "Health|Invincibility")
	bool bIsInvincible = false;

	/**
	 * 无敌期到期时刻 (World 时间, 秒)
	 *
	 * 客户端只读, 服务器是唯一写入方
	 * 客户端没有 World 的服务器时间, 但通过 DOREPLIFETIME 复制后, 用 GetWorld()->GetTimeSeconds() 计算剩余
	 *
	 * 注意: UE 的 GetTimeSeconds 是 World 级别时钟, 不是 GameState 同步的 MatchTime
	 *       如果 Level 流转, 时间会重置, 但这是大厂允许的边界 (跨 Level 复活规则罕见)
	 */
	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "Health|Invincibility")
	float InvincibilityExpiresAtWorldTime = 0.0f;

	/**
	 * 【2026.07.14 新增】无敌期总时长（秒）
	 *
	 * 在 ActivateInvincibility 时写入, 客户端通过 DOREPLIFETIME 同步
	 * 用于 RespawnProgressWidget 显示进度条的总长度
	 */
	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "Health|Invincibility")
	float InvincibilityTotalDuration = 0.0f;

	/** 服务器端 Timer 句柄, 用于到期自动清无敌 */
	FTimerHandle InvincibilityTimerHandle;

	/** 复活移动锁定是否激活 */
	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "Health|Respawn")
	bool bIsRespawnMovementLocked = false;

	/** 复活移动锁定到期时刻 (World 时间, 秒) */
	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "Health|Respawn")
	float RespawnMovementLockedUntilTime = 0.0f;

	/** 复活移动锁定总时长 (秒) */
	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "Health|Respawn")
	float RespawnMovementLockedTotalDuration = 0.0f;

	/** 服务器端 Timer 句柄, 用于到期自动解除移动锁定 */
	FTimerHandle RespawnMovementLockedTimerHandle;

public:
	/** 复活移动锁定状态变更事件 */
	FOnRespawnMovementLockedChanged OnRespawnMovementLockedChanged;

	/**
	 * 【v201.5 大厂架构新增】激活复活移动锁定
	 *
	 * 业务规则: 复活后 N 秒内无法移动 (N = RespawnDelaySeconds)
	 *           玩家和 AI 都走这个机制 (通用)
	 *
	 * 调用方 (集中调度):
	 *   - RoomSpawnSubsystem::HandlePlayerRequestSpawn 末尾
	 *   - RoomSpawnSubsystem::SpawnAIInternal 末尾
	 *   - RoomSpawnSubsystem::MutatePawnToMother 末尾
	 *
	 * 副作用:
	 *   - 设 RespawnMovementLockedUntilTime = World->GetTimeSeconds() + Duration
	 *   - SetTimer 到期 → OnRespawnMovementTimerExpired → Broadcast OnRespawnMovementLockedChanged(false)
	 *   - 立即 Broadcast OnRespawnMovementLockedChanged(true)
	 */
	UFUNCTION(BlueprintCallable, Category = "Health|Respawn")
	void ActivateRespawnMovementLock(float DurationSeconds);

	/**
	 * 【v201.5 大厂架构新增】复活移动锁定到期回调 (服务器内部)
	 */
	void OnRespawnMovementTimerExpired();

	/**
	 * 【v201.5 大厂架构新增】获取复活移动锁定剩余秒数
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Health|Respawn")
	float GetRespawnMovementLockedRemainingSeconds() const;

	/**
	 * 【v201.5 大厂架构新增】是否当前处于复活移动锁定状态
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Health|Respawn")
	bool IsRespawnMovementLocked() const;
};
