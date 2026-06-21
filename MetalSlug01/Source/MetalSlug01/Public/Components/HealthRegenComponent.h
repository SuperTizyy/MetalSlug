// ==========================================
// 生命回复 Component 【2026-07-01 重构: 独立组件, 自治回复逻辑】
// 目的: 把原本散落在 ABaseCharacter::Tick() 里的回血回蓝逻辑
//       抽到独立 Component, 实现:
//       1. 单一职责: Component 只管回复, 不关心其他战斗逻辑
//       2. 配置开关: bEnableAutoRegen 默认关闭 (格斗游戏设计)
//       3. 可复用: 任何需要回血的 Pawn 都可以挂这个 Component
//       4. 可测试: 单元测试不需要完整 Character
// ==========================================
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "HealthRegenComponent.generated.h"

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
	 * 蓝图可配置: 子类可开启 (例如 Boss 战的护盾)
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Health|Regen")
	bool bEnableAutoRegen = false;

	/**
	 * 停止移动后开始回血的延迟 (秒)
	 * 默认 2.0: 战斗节奏紧凑, 不要让回血过快
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Health|Regen", meta = (ClampMin = "0.0", ClampMax = "10.0"))
	float RegenerationDelay = 2.0f;

	/**
	 * 生命回复速度 (血/秒)
	 * 默认 0.0: 关闭自动回血
	 * 设为 > 0 开启自动回血 (典型 2~5)
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Health|Regen", meta = (ClampMin = "0.0", ClampMax = "100.0"))
	float HealthRegenRate = 0.0f;

	/**
	 * 能量回复速度 (能量/秒)
	 * 默认 0.0: 关闭自动回能量
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Health|Regen", meta = (ClampMin = "0.0", ClampMax = "100.0"))
	float EnergyRegenRate = 0.0f;

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
};