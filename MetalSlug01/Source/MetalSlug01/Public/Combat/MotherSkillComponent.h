// ==========================================
// 母体加速技能 Component 【v120 2026.08.03 大厂架构】
//
// @brief 母体加速技能: 移速瞬间变快, 持续 N 秒后结束, 进入冷却期
//
// 【v120 重构】真理源变更:
//   - 旧: DataAsset = ConfigSO.MotherSkillSpeedMultiplier/Duration/Cooldown
//   - 新: DataAsset = PlayerConfigAsset.MotherSkillSpeedMultiplier/Duration/Cooldown
//
// 【架构定位 — SRP 关注点分离 (完全镜像 MotherSlowComponent)】
//   - DataAsset:        PlayerConfigAsset.MotherSkillSpeedMultiplier / Duration / Cooldown
//   - 本组件:           技能状态数据权威 (bIsSkillActive, SkillCooldownEndTime) + 事件广播
//   - BaseCharacter:     订阅 OnSkillStateChanged → 改 MaxWalkSpeed (实际业务执行)
//   - BTDecorator:      读取 BB.SkillCooldownEndTime → 判断"冷却是否结束"
//   - BTTask:           调用 ActivateSkill → 触发状态变更 (AI 路径)
//   - PlayerComboComponent: 调用 ActivateSkill → 触发状态变更 (玩家路径)
//
// 【职责对等 — 与 MotherSlowComponent / InvincibilityFlickerComponent 同款协议】
//   - 本组件只管"技能状态" + "事件广播", 不动 MaxWalkSpeed
//   - BaseCharacter 订阅事件后自己改 MaxWalkSpeed (CachedBaseMaxWalkSpeed × SpeedMultiplier)
//   - 这避免组件跨边界穿透 — Component 不知道 MovementComponent 存在
//
// 【网络模型 — 与 MotherSlowComponent / Invincibility 完全对称】
//   - 服务器: ActivateSkill 写字段 + SetTimer → Broadcast (服务器跑一次)
//   - 客户端: OnRep_SkillActiveChanged 自动 Broadcast (每客户端一次)
//   - 双发保证: 服务器自己不会进 OnRep (服务器已主动 Broadcast) — 不会重复触发
//
// 【冷却时间复制策略 — 服务端权威 + BB 辅助】
//   - SkillCooldownEndTime: Replicated (服务端写入 -> 客户端同步 -> UI 可读)
//   - BB.SkillCooldownEndTime: BTTask 写一次 (BT 装饰器读的辅助黑板)
//   - 两个冷却时间是同一个值 (服务器唯一权威, 不分裂)
//
// 【大厂原则 — 集中调度 + 单一真理源】
//   - 真理源 = 本组件的 bIsSkillActive + SkillCooldownEndTime (Replicated)
//   - 触发入口唯一: BTTask_ActivateMotherSpeedBoost → ActivateSkill
//   - 拒绝缩短逻辑 (与 Invincibility/MotherSlow 镜像): 多次激活取较晚到期
//   - 死亡时强制取消 (与 Invincibility/MotherSlow 镜像): ExecuteDeathLocal 卸下所有临时状态
//
// 【零兜底】
//   - Duration ≤ 0 → 拒绝激活 (强制策划修复)
//   - Cooldown ≤ 0 → 拒绝激活 (强制策划修复)
//   - 技能冷却中 → 拒绝激活 (业务规则, 不是 bug)
//   - 死亡时立即 Deactivate (防御型设计 — 防止 Timer 残留影响新 Pawn)
// ==========================================
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MotherSkillComponent.generated.h"

class ABaseCharacter;


// ==========================================
// 事件定义 — 蓝图/C++ 订阅这些事件
// ==========================================

/**
 * 技能状态切换事件
 *
 * 触发场景: BTTask_ActivateMotherSpeedBoost → ActivateSkill
 *
 * 订阅方:
 *   - BaseCharacter: 订阅 → 改 MaxWalkSpeed = CachedBaseMaxWalkSpeed × SpeedMultiplier
 *   - UI: 订阅 → 显示技能冷却状态 (可选)
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnSkillStateChanged);


/**
 * @class UMotherSkillComponent
 * @brief 母体加速技能 — 数据/表现分离架构
 *
 * 使用方法:
 *   1. BaseCharacter 构造函数中 CreateDefaultSubobject<UMotherSkillComponent>
 *   2. BaseCharacter BeginPlay 订阅 OnSkillStateChanged → 改 MaxWalkSpeed
 *   3. BTTask_ActivateMotherSpeedBoost::ExecuteTask 末尾调 ActivateSkill
 *
 * 大厂原则 - 单一真理源:
 *   - 数据: 本组件 bIsSkillActive + SkillCooldownEndTime (Replicated)
 *   - 表现: BaseCharacter 改 MaxWalkSpeed
 *   - 触发: BTTask_ActivateMotherSpeedBoost 唯一入口
 */
UCLASS(ClassGroup = (MetalSlug), meta = (BlueprintSpawnableComponent))
class METALSLUG01_API UMotherSkillComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMotherSkillComponent();

	// ==========================================
	// 蓝图事件 (BaseCharacter / BP 都可订阅)
	// ==========================================

	/**
	 * 技能状态切换事件 — true=进入加速, false=退出加速
	 *
	 * 大厂原则 — 双发保证:
	 *   - 服务器: ActivateSkill / DeactivateSkill 主动 Broadcast (服务器跑一次)
	 *   - 客户端: OnRep_SkillActiveChanged 自动 Broadcast (每客户端一次)
	 *   - 服务器自己不会进 OnRep (服务器已主动 Broadcast) — 不会重复触发
	 */
	UPROPERTY(BlueprintAssignable, Category = "MotherSkill")
	FOnSkillStateChanged OnSkillStateChanged;

	// ==========================================
	// 公开 API (真理源写入入口)
	// ==========================================

	/**
	 * 激活母体加速技能
	 *
	 * @param SpeedMultiplier 移速倍率 (相对于当前 MaxWalkSpeed)
	 *                          通常从 ConfigSO.MotherSkillSpeedMultiplier 传入
	 * @param Duration        加速持续时间 (秒), ≤ 0 → 拒绝激活 (零兜底)
	 * @param Cooldown        技能冷却时间 (秒), ≤ 0 → 拒绝激活 (零兜底)
	 * @param CurrentMaxWalkSpeed 调用方当前 MaxWalkSpeed 值 (用于缓存原速度)
	 *                          必须在调用本函数前从 CharacterMovement 取值
	 *
	 * 设计原则 (零跨边界):
	 *   - Component 内部不调 OwnerChar->GetCharacterMovement() → 跨边界!
	 *   - 调用方主动传入 CurrentMaxWalkSpeed → Component 不知 MovementComponent 存在
	 *
	 *   - 拒绝缩短: 加速中再次激活, 仅当新到期时间更晚才更新 (大厂原则 - 防欺骗)
	 *   - 冷却中: 拒绝激活 (不是 bug, 是业务规则)
	 *   - 幂等: 重复激活不会创建多个 Timer
	 *   - 服务器权威: 写入字段 → Replicated → 客户端 OnRep 自动同步
	 *
	 * 调用方: BTTask_ActivateMotherSpeedBoost::ExecuteTask
	 */
	UFUNCTION(BlueprintCallable, Category = "MotherSkill")
	void ActivateSkill(float SpeedMultiplier, float Duration, float Cooldown, float CurrentMaxWalkSpeed);

	/**
	 * 强制取消技能状态
	 *
	 * 触发场景:
	 *   - 死亡时 (ExecuteDeathLocal 防御型设计)
	 *   - 回合切换 / 关卡卸载
	 *   - 调试强制清除
	 *
	 * 幂等: 未激活时调用是 no-op
	 */
	UFUNCTION(BlueprintCallable, Category = "MotherSkill")
	void DeactivateSkill();

	// ==========================================
	// 蓝图可读状态 (BP / UI / AI BT 都能查)
	// ==========================================

	/** 是否正在加速状态 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "MotherSkill")
	bool IsSkillActive() const { return bIsSkillActive; }

	/** 技能冷却是否已结束 (可用于 BT 装饰器 / UI 判断) */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "MotherSkill")
	bool IsCooldownReady() const;

	/** 加速状态剩余时间 (秒); 未激活时返回 0 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "MotherSkill")
	float GetSkillActiveRemainingSeconds() const;

	/** 技能冷却剩余时间 (秒); 冷却未结束时返回 >0, 已结束返回 0 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "MotherSkill")
	float GetSkillCooldownRemainingSeconds() const;

	/** 获取技能总冷却时间 (秒); 用于 UI 计算冷却进度倒计时 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "MotherSkill")
	float GetTotalCooldownDuration() const { return TotalCooldownDuration; }

	/** 获取缓存的"原速度" — 用于 DeactivateSkill 时还原 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "MotherSkill")
	float GetCachedBaseMaxWalkSpeed() const { return CachedBaseMaxWalkSpeed; }

	/** 获取当前生效的速度倍率 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "MotherSkill")
	float GetCurrentSpeedMultiplier() const { return CachedSpeedMultiplier; }

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
	 * 技能持续时间到期回调 — Timer 触发后由服务器调用
	 *
	 * 任务:
	 *   - 写 bIsSkillActive = false (Replicated)
	 *   - 清 SkillTimerHandle
	 *   - 服务器主动 Broadcast (客户端 OnRep 也 Broadcast)
	 */
	UFUNCTION()
	void ExpireSkillActive_Internal();

	/**
	 * 客户端 OnRep 回调
	 *
	 * 任务: Broadcast OnSkillStateChanged (与 MotherSlow 镜像)
	 *
	 * 为什么 ReplicatedUsing 而不是 OnRep + 手动 Broadcast:
	 *   - 单字段 OnRep 自动保证"值变了才触发", 避免重复 Broadcast
	 *   - 服务器主动 Broadcast 已覆盖, 客户端 OnRep 也会 Broadcast → 双发保证
	 */
	UFUNCTION()
	void OnRep_SkillActiveChanged();

	// ==========================================
	// 字段 (Replicated 状态机)
	// ==========================================

	/**
	 * 当前是否处于加速状态 — 真理源字段
	 *
	 * ReplicatedUsing = OnRep_SkillActiveChanged
	 *   - 服务器写值 → Replicated 自动同步
	 *   - 客户端收到值变化 → 触发 OnRep_SkillActiveChanged
	 */
	UPROPERTY(ReplicatedUsing = OnRep_SkillActiveChanged, VisibleAnywhere, BlueprintReadOnly, Category = "MotherSkill")
	bool bIsSkillActive = false;

	/**
	 * 加速状态到期时间 (WorldTime, 秒)
	 *
	 * 设计: 派生于 bIsSkillActive 的辅助字段, 方便 UI / BT 算剩余时间
	 */
	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "MotherSkill")
	float SkillActiveExpiresAtWorldTime = 0.0f;

	/**
	 * 技能冷却到期时间 (WorldTime, 秒)
	 *
	 * 设计: 用于 BT 装饰器判断冷却是否结束 + UI 显示冷却倒计时
	 *       激活技能时写入 Now + Cooldown
	 *       BTDecorator_MotherSkillReady 读取此字段与 WorldTime 对比
	 */
	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "MotherSkill")
	float SkillCooldownEndTime = 0.0f;

	/** 技能总冷却时间 (秒) — 用于 UI 计算冷却进度倒计时 */
	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "MotherSkill")
	float TotalCooldownDuration = 0.0f;

	/** 技能持续时间 Timer 句柄 (到期时停止加速) */
	FTimerHandle SkillTimerHandle;

	/** 缓存的"原速度" — 激活技能前的 MaxWalkSpeed */
	UPROPERTY(Transient)
	float CachedBaseMaxWalkSpeed = 0.0f;

	/** 缓存的"速度倍率" — 激活时记录, 退出时用 */
	UPROPERTY(Transient)
	float CachedSpeedMultiplier = 1.0f;
};
