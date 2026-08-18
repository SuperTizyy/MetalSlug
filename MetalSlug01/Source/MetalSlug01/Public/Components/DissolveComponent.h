// ==========================================
// 溶解特效 Component 【2026-07-10 大厂 P0 重构 — 职责对等 + 武器自治】
//
// 目的: BaseCharacter 的溶解特效全部下沉到本 Component
// 职责 (本版本):
//   1. 驱动角色自身骨骼网格的材质溶解
//   2. 单一协议: DissolveAmount 标量参数 + MF_Dissolve Material Function
//   3. 零兜底: 协议不满足时显式报警, 不静默兼容
//
// 历史:
//   旧版 (v22 及以前): DissolveComponent 也管武器的溶解 (跨边界)
//   问题: 武器 Detach 后 FindComponentByClass 找不到, 武器永远不溶解
//   v23 修复: CollectWeaponDynamicMaterials 让角色主动传入武器 Mesh (依然跨边界)
//   v24 (本版本): 武器自己持有 WeaponDissolveComponent, 角色不再管武器溶解
//   → 职责对等: 身体管身体, 武器管武器
//   → 零跨边界: 武器 Detach 后完全自治
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
	void StartDissolveImmediate()
	{
		StartDissolveEffect();
	}

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
	virtual void BeginPlay() override;
	/**
	 * @brief UE 组件注销时清理定时器与材质引用 (防止 GC 抖动)
	 */
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

	/**
	 * 【大厂 P0 2026.07.10 重构 — 职责收窄】
	 * 只收集 Owner 角色自己的骨骼网格材质
	 * 武器溶解已下放给 ABaseWeapon::WeaponDissolveComponent, 不再管武器
	 */
	void CollectDynamicMaterials();

	/**
	 * 获取 Owner 的骨骼网格组件（用于创建动态材质）
	 */
	class USkeletalMeshComponent* GetOwnerSkeletalMesh() const;

private:
	/** 动态材质实例数组（驱动 DissolveAmount 参数）—— 真理源: 由 CollectDynamicMaterials 在 StartDissolveEffect 时一次性收集 */
	UPROPERTY()
	TArray<TObjectPtr<UMaterialInstanceDynamic>> DynamicMaterials;

	/** 当前溶解进度 0.0~1.1 (>= 1.1 时视为溶解完成) */
	float CurrentDissolveValue = 0.0f;

	/** 是否正在溶解中 —— Tick 守卫位, 避免在非溶解状态做材质驱动 */
	bool bIsDissolving = false;

	/** 溶解定时器句柄（成员化以便 EndPlay 精确清理）—— 当前已不再使用 (StartDissolveImmediate 立即触发) */
	FTimerHandle DissolveTimerHandle;

	/** 是否已收集过材质（防止重复收集）—— 幂等保护, 多次 StartDissolveEffect 只收集一次 */
	bool bMaterialsCollected = false;

	// 【已删除 2026.07.10 大厂 P0 重构 — 武器溶解已下放】
	//   - DissolveMaterial 字段 (大厂原则: 不再"替换"材质, UE 默认 nullptr 行为已正确)
	//   - CollectWeaponDynamicMaterials (跨边界调用, 武器已自治)
	//   - GetOwnerWeaponMesh (不再需要)
	//   - CollectAllDynamicMaterials (公开 API, 已无意义)
	// 替代方案: ABaseWeapon::StartDissolve() (武器自治入口)
};
