// ==========================================
// 生命回复 Component 【2026-07-01 重构: 独立组件, 自治回复逻辑】
// 目的: 把原本散落在 ABaseCharacter::Tick() 里的回血回蓝逻辑
//       抽到独立 Component, 实现:
//       1. 单一职责: Component 只管回复, 不关心其他战斗逻辑
//       2. 配置开关: bEnableAutoRegen 默认关闭 (格斗游戏设计)
//       3. 可复用: 任何需要回血的 Pawn 都可以挂这个 Component
//       4. 可测试: 单元测试不需要完整 Character
//
// 【2026.07.26 v100.1 大厂架构】母体"待机回血"扩展:
//   - 业务规则 (用户 2026.07.26): 母体在不被打 + 不移动 N 秒后, 每秒回 M 滴血
//   - 加回血开始/结束委托 OnRegenStarted/OnRegenStopped (驱动声音 RPC)
//   - 改用 SetTimer(1.0s) 整秒节拍 (业务规则: "每秒 30 滴" 精准整数)
//   - RegenSound 字段 (USoundBase*) 配 DataAsset
//   - 复用现有 TakeDamage → NotifyDamageTaken 链路 (v40 P0 已通)
//   - 复用现有 HealthComponent::Heal 已封顶 MaxHealth (零额外代码)
// ==========================================
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "HealthRegenComponent.generated.h"

class USoundBase;

/**
 * 【v100.1 大厂架构 — 母体待机回血事件总线】
 *
 * 触发场景 (服务器权威):
 *   - 服务器 TickComponent 检测到状态变化 (false → true / true → false)
 *   - 触发委托 → BaseCharacter 订阅 → 调 Multicast_PlayRegenSound RPC
 *
 * 业务规则 (用户 2026.07.26):
 *   - "开始回血后开始播放声音, 结束回血后关闭此声音"
 *   - 用委托而不是 Component 直接调 RPC (保持 Component 自治 + 不绑死 Actor 类型)
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnRegenStateChanged, bool, bIsNowRegenerating);

/**
 * @class UHealthRegenComponent
 * @brief 生命/能量回复组件 - 自动回复状态机 (主要用于母体待机回血)
 *
 * 单一职责: 检测 Owner 状态 (移动/满血/死亡) + 整秒节拍调 HealthComponent->Heal/EnergyComponent->Add
 *
 * 大厂架构中的角色:
 *   - 自治状态机: bIsRegenerating 字段 + SetRegeneratingState 集中入口
 *   - 整秒节拍: SetTimer(1.0s, loop) 触发回血 (业务规则: "每秒 30 滴" 精准整数, 非 DeltaTime 累加)
 *   - 单一真理源: "满血"概念归 HealthComponent 拥有 (调 HealthComp->IsFullHealth())
 *   - 事件总线: OnRegenStateChanged 触发回血音效 RPC
 *
 * 设计原则:
 *   - 零重复广播: SetRegeneratingState 状态无变化时直接 return
 *   - 防御性检查: 死亡/未启用/移动中/未到延迟 → 都不回血
 *   - 配错零兜底: bEnableAutoRegen=false 时完全跳过
 *
 * 业务触发条件 (母体, 用户 2026.07.26 明确):
 *   1. 不是满血状态
 *   2. 非移动状态
 *   3. 不被攻击 (NotifyDamageTaken 链路)
 *   4. RegenerationDelay 秒静止后
 *
 * 使用方式:
 *   - ABaseCharacter::PossessedBy 调用 ResetRegenerationState
 *   - HealthComponent::ApplyDamage 调用 NotifyDamageTaken
 *   - ABaseCharacter::BeginPlay 订阅 OnRegenStateChanged 触发音效 RPC
 */
UCLASS(ClassGroup = (MetalSlug), meta = (BlueprintSpawnableComponent))
class METALSLUG01_API UHealthRegenComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UHealthRegenComponent();

	/**
	 * 【2026-07-01 P0 新增】手动通知受到伤害 (打断回复延迟)
	 * 调用时机: HealthComponent::ApplyDamage 之后, 立即调用
	 *   这样受伤后必须再等 RegenerationDelay 才能开始回血
	 *   防止"刚被打就开始回"的瞬时回血 (但当前默认 0 速率, 此调用主要用于未来开启回血时)
	 */
	UFUNCTION(BlueprintCallable, Category = "Health|Regen")
	void NotifyDamageTaken();

	/**
	 * 【2026-07-01 P0 新增】重置回复计时 (复活/重生时调用)
	 * 调用时机: 角色重生 (PossessedBy) 或 死亡复活后
	 *   重置 LastMoveTime 和 bIsRegenerating 状态
	 */
	UFUNCTION(BlueprintCallable, Category = "Health|Regen")
	void ResetRegenerationState();

	// ===== 配置接口 =====
	/**
	 * 是否启用自动回血
	 * 默认关闭: 格斗游戏设计原则, 被攻击后血量保持不变
	 * 蓝图可配置: 子类可开启 (例如 Boss 战的护盾 / 母体待机回血)
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Health|Regen")
	bool bEnableAutoRegen = false;

	/**
	 * 停止移动后开始回血的延迟 (秒)
	 * 默认 5.0: 业务规则 (用户 2026.07.26) 母体"不被打不移动5秒"开始回血
	 * BP 子类可覆盖 (刀战模式默认 0 = 不开启自动回血)
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Health|Regen", meta = (ClampMin = "0.0", ClampMax = "30.0"))
	float RegenerationDelay = 5.0f;

	/**
	 * 生命回复速度 (血/秒)
	 * 默认 0.0: 关闭自动回血
	 * 母体配置: 30.0 (业务规则: "每秒 30 滴")
	 * BP 设为 > 0 开启自动回血
	 *
	 * 大厂原则 - 整秒节拍:
	 *   - 实现用 SetTimer(1.0s) 每整秒精准 +HealthRegenRate 滴
	 *   - 不是 DeltaTime 累加 (避免 0.6s 时已经回 18 滴的浮点误差)
	 *   - HealthComponent::Heal 内部 FMath::Min(MaxHealth, ...) 已封顶 (不超总血量)
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Health|Regen", meta = (ClampMin = "0.0", ClampMax = "1000.0"))
	float HealthRegenRate = 0.0f;

	/**
	 * 能量回复速度 (能量/秒)
	 * 默认 0.0: 关闭自动回能量
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Health|Regen", meta = (ClampMin = "0.0", ClampMax = "1000.0"))
	float EnergyRegenRate = 0.0f;

	/**
	 * 【v100.1 大厂架构】回血音 (循环播放, 用于驱动 UI 反馈)
	 * 业务规则 (用户 2026.07.26): "开始回血后开始播放声音, 结束回血后关闭此声音"
	 * 配 nullptr = 不播放声音 (业务可禁用)
	 * 注意: Sound 在服务器 TriggerMulticast 时由所有客户端本地播放 (不复制 Sound 资产)
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Health|Regen")
	TObjectPtr<USoundBase> RegenSound = nullptr;

	// ===== 委托 (事件总线) =====
	/**
	 * 【v100.1 大厂架构】回血状态变化多播
	 * - bIsNowRegenerating=true:  开始回血 (服务器 TickComponent 检测到 false→true 切换)
	 * - bIsNowRegenerating=false: 停止回血 (服务器 TickComponent 检测到 true→false 切换)
	 * - 触发场景: 被打 / 开始移动 / 满血 / 死亡 / 离开 bEnableAutoRegen 区间
	 * - 订阅方: BaseCharacter::BeginPlay 订阅 → 触发 Multicast_PlayRegenSound RPC
	 */
	UPROPERTY(BlueprintAssignable, Category = "Health|Regen")
	FOnRegenStateChanged OnRegenStateChanged;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

private:
	/**
	 * 最后移动时间 (服务器时间)
	 * 用于计算 "停止移动多久"
	 */
	float LastMoveTime = 0.0f;

	/**
	 * 是否正在回血/回能量
	 * 标志: 受伤后会置 false, 等待 RegenerationDelay 后再置 true
	 */
	bool bIsRegenerating = false;

	/**
	 * 检测 Owner 是否在移动
	 * 通过速度向量判断
	 */
	bool IsOwnerMoving() const;

	/**
	 * 【v100.1 大厂架构】整秒节拍回血 Timer
	 * - 触发场景: bIsRegenerating 从 false → true
	 * - 频率: 1.0s 整秒 (业务规则: "每秒 30 滴")
	 * - 单次回调: TimerCallback_RegenTick
	 * - 关闭: bIsRegenerating 变 false 时 Clear
	 */
	FTimerHandle RegenTickTimerHandle;

	/**
	 * 【v100.1 大厂架构】Timer 回调 - 整秒回血
	 * - 调 HealthComp->Heal(HealthRegenRate) (内部 FMath::Min 封顶)
	 * - 调 EnergyComp->Add(EnergyRegenRate)
	 * - 死亡时由 HealthComponent::OnDeath 链路打断 (HealthComp->IsDead 守卫)
	 */
	UFUNCTION()
	void TimerCallback_RegenTick();

	/**
	 * 【v100.1 大厂架构】设置 bIsRegenerating 状态 + 触发 OnRegenStateChanged 委托
	 * - 集中状态切换入口 — 唯一调用方是 TickComponent
	 * - 切换 true 时启动整秒 Timer
	 * - 切换 false 时停止整秒 Timer
	 * - 状态真变化才广播委托 (避免每帧 Broadcast 浪费)
	 */
	void SetRegeneratingState(bool bNewState);
};