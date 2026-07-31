// ==========================================
// 母体被主武器击中后降速 Component 【v133.5 2026.08.02 大厂架构】
//
// @brief 母体被主武器命中后, 2 秒内移动速度降到 MotherSlowSpeed, 走自动恢复
//
// 【架构定位 — SRP 关注点分离 (完全镜像 InvincibilityFlickerComponent)】
//   - HealthComponent:   血量数据权威
//   - CombatDeathComponent: TAKE DAMAGE 入口 + 判定"主武器击中母体" → ActivateSlow
//   - 本组件:            慢速状态数据权威 (bIsSlowed, SlowExpiresAtWorldTime) + 事件广播
//   - BaseCharacter:     订阅 OnSlowStateChanged → 改 MaxWalkSpeed (实际业务执行)
//
// 【职责对等 — 与 InvincibilityFlickerComponent / DissolveComponent 同款协议】
//   - 本组件只管"状态" + "事件广播", 不动 MaxWalkSpeed
//   - BaseCharacter 订阅事件后自己改 MaxWalkSpeed (ConfigSO.MotherSlowSpeed / MotherMaxWalkSpeed)
//   - 这避免组件跨边界穿透 — Component 不知道 MovementComponent 存在
//
// 【网络模型 — 与 Invincibility 完全对称】
//   - 服务器: ActivateSlow 写字段 + SetTimer → Broadcast (服务器跑一次)
//   - 客户端: OnRep_SlowStateChanged 自动 Broadcast (每客户端一次)
//   - 双发保证: 服务器自己不会进 OnRep (服务器已主动 Broadcast) — 不会重复触发
//   - 为什么 ReplicatedUsing 而不是 OnRep + 手动 Broadcast: 单字段 OnRep 自动保证"值变了才触发"
//
// 【大厂原则 — 集中调度 + 单一真理源】
//   - 真理源 = 本组件的 bIsSlowed (Replicated) + SlowExpiresAtWorldTime (Replicated)
//   - 触发入口唯一: CombatDeathComponent::TakeDamage 末尾 (Layer 0 拦截 + 阵营守卫之后)
//   - 拒绝缩短逻辑 (与 Invincibility 镜像): 多次激活时取较晚到期
//   - 死亡时强制取消 (与 Invincibility 镜像): ExecuteDeathLocal 卸下所有临时状态
//
// 【零兜底】
//   - 字段 ≤ 0 → 拒绝激活 (跟 Invincibility 一样)
//   - 配错 ConfigSO (MotherSlowSpeed <= 0) → Log Error 让策划修复
//   - 死亡时立即 Deactivate (防御型设计 — 防止 Timer 残留影响新 Pawn)
//
// 【为什么不像 InvincibilityFlickerComponent 那样躺着订阅 OnHealthChanged】
//   - Invincibility 是血量侧业务的 ack (装备/UI/BP 视觉都订阅)
//   - Slow 是"主武器击中母体"特殊条件, 不是血量变化 — 触发源在 CombatDeathComponent
//   - 显式调 ActivateSlow 比让组件订阅事件更精准 (避免误触发)
// ==========================================
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MotherSlowComponent.generated.h"

class ABaseCharacter;


// ==========================================
// 事件定义 — 蓝图/C++ 订阅这些事件
// ==========================================

/**
 * 慢速状态激活事件
 *
 * 触发场景: CombatDeathComponent::TakeDamage 判定主武器击中母体 → ActivateSlow
 *
 * 订阅方:
 *   - BaseCharacter: 订阅 → 改 MaxWalkSpeed = MotherSlowSpeed
 *   - UI: 订阅 → 显示"减速"图标 (可选)
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnSlowStateChanged);


/**
 * @class UMotherSlowComponent
 * @brief 母体被主武器击中后降速 — 数据/表现分离架构
 *
 * 使用方法:
 *   1. BaseCharacter 构造函数中 CreateDefaultSubobject<UMotherSlowComponent>
 *   2. BaseCharacter BeginPlay 订阅 OnSlowStateChanged → 改 MaxWalkSpeed
 *   3. CombatDeathComponent::TakeDamage 末尾调 ActivateSlow (判定主武器+母体)
 *
 * 大厂原则 - 单一真理源:
 *   - 数据: 本组件 bIsSlowed + SlowExpiresAtWorldTime (Replicated)
 *   - 表现: BaseCharacter 改 MaxWalkSpeed
 *   - 触发: CombatDeathComponent 唯一入口
 */
UCLASS(ClassGroup = (MetalSlug), meta = (BlueprintSpawnableComponent))
class METALSLUG01_API UMotherSlowComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMotherSlowComponent();

	// ==========================================
	// 蓝图事件 (BaseCharacter / BP 都可订阅)
	// ==========================================

	/**
	 * 慢速状态切换事件 — true=进入慢速, false=退出慢速
	 *
	 * 大厂原则 — 双发保证:
	 *   - 服务器: ActivateSlow / DeactivateSlow 主动 Broadcast (服务器跑一次)
	 *   - 客户端: OnRep_SlowStateChanged 自动 Broadcast (每客户端一次)
	 *   - 服务器自己不会进 OnRep (服务器已主动 Broadcast) — 不会重复触发
	 */
	UPROPERTY(BlueprintAssignable, Category = "Slow")
	FOnSlowStateChanged OnSlowStateChanged;

	// ==========================================
	// 公开 API (真理源写入入口)
	// ==========================================

	/**
	 * 激活慢速状态
	 *
	 * @param Duration 慢速持续时间 (秒), ≤ 0 → 拒绝激活 (零兜底)
	 * @param CurrentMaxWalkSpeed 调用方当前 MaxWalkSpeed 值 (用于大厂架构 — 缓存原速度)
	 *                         - 通常调用方 (BaseCharacter) 在调本函数前先 GetCharacterMovement()->MaxWalkSpeed
	 *                         - 仅在第一次激活时缓存 (Component 内部判断)
	 *                         - ≤ 0 → 拒绝缓存 (零兜底)
	 *
	 * 设计原则 (v133.5.1 修复 — 零跨边界):
	 *   - 旧版: Component 内部调 OwnerChar->GetCharacterMovement() → 跨边界
	 *   - 新版: 调用方主动传入 CurrentMaxWalkSpeed → Component 不知 MovementComponent 存在
	 *   - 这是大厂原则 - "数据流向是单向的, Component 是被动的容器"
	 *
	 *   - 拒绝缩短: 多次激活时, 仅当新到期时间更晚才更新 (大厂原则 - 防欺骗)
	 *   - 幂等: 重复激活不会创建多个 Timer
	 *   - 服务器权威: 写入字段 → Replicated → 客户端 OnRep 自动同步
	 *
	 * 调用方: CombatDeathComponent::TakeDamage 末尾
	 */
	UFUNCTION(BlueprintCallable, Category = "Slow")
	void ActivateSlow(float Duration, float CurrentMaxWalkSpeed);

	/**
	 * 强制取消慢速状态
	 *
	 * 触发场景:
	 *   - 死亡时 (ExecuteDeathLocal 防御型设计)
	 *   - 回合切换 / 关卡卸载
	 *   - 调试强制清除
	 *
	 * 幂等: 未激活时调用是 no-op
	 */
	UFUNCTION(BlueprintCallable, Category = "Slow")
	void DeactivateSlow();

	// ==========================================
	// 蓝图可读状态 (BP / UI / AI BT 都能查)
	// ==========================================

	/** 是否正在慢速状态 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Slow")
	bool IsSlowed() const { return bIsSlowed; }

	/** 慢速状态剩余时间 (秒); 未慢速时返回 0 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Slow")
	float GetSlowRemainingSeconds() const;

	/**
	 * 【v133.5.1 大厂架构】获取缓存的"原速度" — 用于 DeactivateSlow 时还原
	 *
	 * 调用方: BaseCharacter::HandleSlowStateChanged (慢速状态退出时还原速度)
	 *
	 * 大厂原则 — 最小破坏:
	 *   - AI 路径 ConfigSO 没有 MaxWalkSpeed 字段 (真理源分离)
	 *   - 给 ConfigSO 加字段会破坏 "ConfigSO = 战斗参数, 速度 = 角色属性" 的真理源分工
	 *   - 妥协方案: 激活慢速前缓存 MoveComp 当前值 → 退出时还原
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Slow")
	float GetCachedBaseMaxWalkSpeed() const { return CachedBaseMaxWalkSpeed; }

protected:
	// ==========================================
	// UE 生命周期
	// ==========================================
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// ==========================================
	// 内部辅助
	// ==========================================

	/**
	 * 到期回调 — Timer 触发后由服务器调用
	 *
	 * 任务:
	 *   - 写 bIsSlowed = false (Replicated)
	 *   - 清 Timer
	 *   - 服务器主动 Broadcast (客户端 OnRep 也 Broadcast)
	 */
	void ExpireSlow_Internal();

	/**
	 * 客户端 OnRep 回调
	 *
	 * 任务: Broadcast OnSlowStateChanged (与 Invincibility 镜像)
	 *
	 * 为什么 ReplicatedUsing 而不是 OnRep + 手动 Broadcast:
	 *   - 单字段 OnRep 自动保证"值变了才触发", 避免重复 Broadcast
	 *   - 服务器主动 Broadcast 已覆盖, 客户端 OnRep 也会 Broadcast → 双发保证
	 */
	UFUNCTION()
	void OnRep_SlowStateChanged();

	// ==========================================
	// 字段 (Replicated 状态机)
	// ==========================================

	/**
	 * 当前是否处于慢速状态 — 真理源字段
	 *
	 * ReplicatedUsing = OnRep_SlowStateChanged
	 *   - 服务器写值 → Replicated 自动同步
	 *   - 客户端收到值变化 → 触发 OnRep_SlowStateChanged
	 */
	UPROPERTY(ReplicatedUsing = OnRep_SlowStateChanged, VisibleAnywhere, BlueprintReadOnly, Category = "Slow")
	bool bIsSlowed = false;

	/**
	 * 慢速状态到期时间 (WorldTime, 秒)
	 *
	 * 设计: 派生于 bIsSlowed 的辅助字段, 方便 UI / BT 算剩余时间
	 * 不依赖 bIsSlowed 独立判断"是否到期" — 用 GetSlowRemainingSeconds() 内部统一
	 *
	 * bIsSlowed=false 时此字段值无意义 (可能被遗留, 但 GetSlowRemainingSeconds() 会返回 0)
	 */
	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "Slow")
	float SlowExpiresAtWorldTime = 0.0f;

	/** 服务器 Timer 句柄 (到期时清慢速状态) */
	FTimerHandle SlowTimerHandle;

	/**
	 * 【v133.5.1 大厂架构修复 — 缓存"原速度"】激活慢速前的 MaxWalkSpeed
	 *
	 * 设计动机:
	 *   - AI 路径没有 MaxWalkSpeed 真理源 (ConfigSO 没这字段)
	 *   - 玩家路径真理源是 PlayerConfig->MaxWalkSpeed / MotherMaxWalkSpeed
	 *   - 简单可靠: 激活慢速时缓存 MoveComp 当前值 → DeactivateSlow 时还原
	 *   - 避免"加 ConfigSO 字段"破坏真理源架构 (ConfigSO 是血量/伤害, 不应该管速度)
	 *
	 * 大厂原则 — 真理源驱动 (这里用妥协方案):
	 *   - 真理源: 慢速速度 → 真理源字段 (MotherSlowSpeed)
	 *   - 还原速度: 缓存机制 (而非真理源推算, 因为 AI 路径无真理源)
	 *   - 这与 Invincibility 不同 (Invincibility 不需要"还原"), 但符合"最小破坏"原则
	 *
	 * 初始化时机:
	 *   - ActivateSlow 内 — 慢速激活前抓取当前值
	 *   - CachedBaseMaxWalkSpeed == 0 → 拒绝激活 (零兜底 — 防止后续还原错误)
	 */
	UPROPERTY(Transient)
	float CachedBaseMaxWalkSpeed = 0.0f;
};
