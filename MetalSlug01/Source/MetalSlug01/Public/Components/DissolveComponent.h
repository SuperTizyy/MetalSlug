// ==========================================
// 溶解特效 Component 【2026-06-15 重构: 完整迁移 BaseCharacter 溶解逻辑】
// 目的: BaseCharacter 的溶解特效全部下沉到本 Component
// 优势:
//   1. 数据权威唯一: DynamicMaterials / DissolveDelay / DissolveSpeed 全部在 Component 内
//   2. 自动触发: BeginPlay 中订阅 HealthComponent->OnDeath，无需外部手动调用
//   3. 定时器自治: Component 持有 TimerHandle，EndPlay 时自动清理
//   4. 武器联动: 自动收集 Owner 角色和武器的动态材质一起溶解
// ==========================================
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DissolveComponent.generated.h"

class UMaterialInstanceDynamic;
class UHealthComponent;

// 【内部事件】溶解完成后通知 Character（此时 Owner 还活着，可执行清理）
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnDissolveFinished);

UCLASS(ClassGroup = (MetalSlug), meta = (BlueprintSpawnableComponent))
class METALSLUG01_API UDissolveComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UDissolveComponent();

	/**
	 * 是否正在溶解中
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Dissolve")
	bool IsDissolving() const { return bIsDissolving; }

	/**
	 * 溶解完成后广播 (通知 Character 可执行清理如 Destroy)
	 */
	UPROPERTY(BlueprintAssignable, Category = "Dissolve")
	FOnDissolveFinished OnDissolveFinished;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/**
	 * 【内部】HealthComponent 死亡回调，触发溶解定时器
	 */
	UFUNCTION()
	void OnOwnerDeath();

	/**
	 * 【内部】定时器回调：开始溶解材质渐隐
	 */
	UFUNCTION()
	void StartDissolveEffect();

public:
	/**
	 * 【2026-07-01 新增】收集指定武器 Mesh 的动态材质 (死亡时外部主动传入, 不依赖 Owner 查找)
	 *
	 * 【大厂 P0 修复】 旧实现 GetOwnerWeaponMesh() 用 Char->FindComponentByClass<UMeshComponent>(),
	 *   武器 Detach 后已不在 Char 的子组件里 → 永远找不到 → 武器**永不溶解**
	 *   新实现: 死亡流程主动传入已 Detach 的武器 Mesh 引用, 解决查找失败 bug
	 *
	 * @param WeaponMesh  已 Detach 的武器网格组件 (可能为 nullptr, 内部已防御)
	 */
	UFUNCTION(BlueprintCallable, Category = "Dissolve")
	void CollectWeaponDynamicMaterials(UMeshComponent* WeaponMesh);

	/**
	 * 【2026-07-01 P0 新增】公开 API: 立即启动溶解
	 *
	 * 取代旧的 BeginPlay 订阅 OnDeath 自启动模式
	 * 死亡流程编排器 (BaseCharacter::ExecuteDeathLocal) 显式调用此方法,
	 *   可以精确控制溶解与复活的时序
	 *
	 * 调用时机: 角色死亡后立即调用 (无延迟), 与死亡动画/Ragdoll 并行
	 * 溶解时长: 1.0/DissolveSpeed 秒 (默认 1.5 表示 ~0.67 秒)
	 */
	UFUNCTION(BlueprintCallable, Category = "Dissolve")
	void StartDissolveImmediate();

	/**
	 * 【2026-07-01 新增】收集所有已挂载的动态材质 (角色 + 已注册武器)
	 * 公开为 UFUNCTION 允许外部在死亡前主动触发, 不再依赖 OnDeath 定时器
	 */
	UFUNCTION(BlueprintCallable, Category = "Dissolve")
	void CollectAllDynamicMaterials();

private:
	/**
	 * 从 Owner 角色及其武器收集动态材质
	 * 内部调用 CreateDynamicMaterialInstance，后续 Tick 直接驱动参数
	 */
	void CollectDynamicMaterials();

	/**
	 * 获取 Owner 的骨骼网格组件（用于创建动态材质）
	 */
	class USkeletalMeshComponent* GetOwnerSkeletalMesh() const;

	/**
	 * 获取 Owner 当前武器的网格组件（用于创建动态材质）
	 */
	class UMeshComponent* GetOwnerWeaponMesh() const;

public:
	/**
	 * 【2026-07-01 P0 重构】溶解启动延迟 (秒)
	 * 旧默认 5.0s 太长, 与 RespawnDelaySeconds=3s 冲突, 身体来不及溶解就被销毁
	 * 新默认 0.5s: 给死亡动画留出最小时长, 之后立即开始溶解
	 * 死亡流程: ExecuteDeathLocal 立即调用 StartDissolveImmediate
	 *           溶解速度由 DissolveSpeed 决定 (典型 1.5~2.0s 完成)
	 *           RespawnDelaySeconds 必须 > 溶解完成时间, 否则身体提前销毁
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dissolve|Config")
	float DissolveDelay = 0.5f;

	/** 溶解速度（值越大溶解越快，典型 1.0~2.0 表示 1~2 秒内完成） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dissolve|Config", meta = (ClampMin = "0.1", ClampMax = "10.0"))
	float DissolveSpeed = 1.5f;

protected:
	/** 溶解时使用的材质（蓝图可配置） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dissolve|Material")
	TObjectPtr<UMaterialInterface> DissolveMaterial = nullptr;

private:
	/** 动态材质实例数组（驱动 DissolveAmount 参数） */
	UPROPERTY()
	TArray<TObjectPtr<UMaterialInstanceDynamic>> DynamicMaterials;

	/** 当前溶解进度 0.0~1.1 */
	float CurrentDissolveValue = 0.0f;

	/** 是否正在溶解中 */
	bool bIsDissolving = false;

	/** 溶解定时器句柄（成员化以便 EndPlay 精确清理） */
	FTimerHandle DissolveTimerHandle;

	/** 是否已收集过材质（防止重复收集） */
	bool bMaterialsCollected = false;
};
