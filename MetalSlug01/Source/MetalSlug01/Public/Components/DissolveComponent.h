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
	/** 死亡后多久开始溶解（秒） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dissolve|Config")
	float DissolveDelay = 5.0f;

	/** 溶解速度（值越大溶解越快） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dissolve|Config")
	float DissolveSpeed = 0.5f;

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
