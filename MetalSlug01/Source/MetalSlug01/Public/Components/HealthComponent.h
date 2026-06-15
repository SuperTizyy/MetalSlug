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

	// === 事件订阅 (供 BaseCharacter::BeginPlay 中绑定) ===
	/** 死亡事件: Actor 订阅后触发 Die() 流程 */
	UPROPERTY(BlueprintAssignable, Category = "Health")
	FOnDeath OnDeath;

	/** 血量改变事件: HUD/特效订阅 */
	UPROPERTY(BlueprintAssignable, Category = "Health")
	FOnHealthChanged OnHealthChanged;

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

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Health")
	float MaxHealth = 100.0f;

	/**
	 * 【2026-06-15 升级】: 改为 Replicated
	 * 原版只是普通字段,服务器修改后客户端看不到
	 */
	UPROPERTY(ReplicatedUsing = OnRep_CurrentHealth, VisibleAnywhere, BlueprintReadOnly, Category = "Health")
	float CurrentHealth = 100.0f;

	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "Health")
	bool bIsDead = false;
};
