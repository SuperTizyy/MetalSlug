// ==========================================
// WeaponDissolveComponent
// 武器专用溶解特效组件 — 职责对等 + 单一协议
//
// 【2026.07.10 大厂 P0 重构】
// 历史 (2026.07.10 之前):
//   ABaseCharacter::DropAndFadeWeapon 调 DissolveComponent->CollectWeaponDynamicMaterials(WeaponMesh)
//   把武器的 MID 放进角色 DissolveComponent 的 DynamicMaterials 数组
//   → 责任错位 (角色的组件驱动武器的材质)
//   → 跨边界引用 (武器 Detach 后, 角色 Component 还 hold 武器 MID, 销毁时序混乱)
//
// 现在 (2026.07.10 大厂架构):
//   ABaseWeapon 自己持有一个 UWeaponDissolveComponent
//   → 职责对等: 武器管理自己的溶解, 角色管理自己的溶解
//   → 零跨边界: 武器 Detach 后完全自治, 不再依赖角色任何状态
//   → 单一协议: 武器材质蓝图必须包含 MF_Dissolve 节点 (与身体一致)
//   → 零兜底: 协议不满足时, UE_LOG(Warning) 报警, 不静默兼容
//   → 显式状态机: Attached / Dropping / Dissolving / Destroyed
//
// 调用方:
//   1. ABaseWeapon::BeginPlay - 自动创建组件, 不需手动 AddComponent
//   2. ABaseCharacter::DropAndFadeWeapon - 调 Weapon->StartDissolve()
//   3. UE 引擎 SetLifeSpan(3.0) 到期 - 武器自动 Destroy
//
// 不允许的用法:
//   - 在外部直接调 StartDissolve 多次 (幂等保护: 已 Dissolving 则忽略)
//   - 在外部访问 DynamicMaterials (私有, 禁止跨边界驱动)
// ==========================================
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "WeaponDissolveComponent.generated.h"

class UMaterialInstanceDynamic;
class UMeshComponent;

/** 【公开】溶解完成事件 (通知武器可以安全销毁) */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnWeaponDissolveFinished);

UCLASS(ClassGroup = (MetalSlug), meta = (BlueprintSpawnableComponent))
class METALSLUG01_API UWeaponDissolveComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UWeaponDissolveComponent();

	/**
	 * 【公开唯一入口】启动武器溶解
	 *
	 * 死亡流程调用方: ABaseCharacter::DropAndFadeWeapon
	 *
	 * 时序 (大厂规范, 无兜底):
	 *   调用时: 必须已 Detach (Mesh 不再跟随角色) + 已启用物理模拟
	 *   调用后: 组件立即收集自己 Owner Weapon 的 Mesh 材质
	 *   Tick:   累加 DissolveAmount 直到 1.1
	 *   完成:   广播 OnWeaponDissolveFinished (UE SetLifeSpan 仍由 Character 控, 组件不主动 Destroy)
	 *
	 * 幂等:
	 *   - 多次调用: 第二次起 no-op, 已经在 Dissolving 状态
	 *   - 调用前 Mesh 已失效: 立即 Log + return, 不阻塞调用方
	 */
	UFUNCTION(BlueprintCallable, Category = "Weapon|Dissolve")
	void StartDissolve();

	/** 是否正在溶解中 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Weapon|Dissolve")
	bool IsDissolving() const { return bIsDissolving; }

	/**
	 * 溶解完成后广播 (外部可订阅, 默认连接 UE SetLifeSpan 倒计时)
	 * 注: 组件自身**不**调用 Actor::Destroy, 销毁由 Character::DropAndFadeWeapon
	 *     通过 SetLifeSpan 统一控制 (避免双销毁时序冲突)
	 */
	UPROPERTY(BlueprintAssignable, Category = "Weapon|Dissolve")
	FOnWeaponDissolveFinished OnWeaponDissolveFinished;

protected:
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/**
	 * 【大厂单一协议 — 零兜底】收集武器 Mesh 的动态材质
	 *
	 * 协议 (与身体 DissolveComponent 完全一致):
	 *   - 武器 Mesh 的每个材质槽必须包含 MF_Dissolve Material Function 节点
	 *   - MF_Dissolve 节点有 DissolveAmount 标量参数
	 *   - C++ 协议: CreateDynamicMaterialInstance(i, nullptr) → UE 自动复制当前槽材质
	 *   - 协议不满足 (材质蓝图没调用 MF_Dissolve): 材质能创建但驱动无效
	 *     → 我们不静默兜底, 直接 Log(Warning) 报警 (让美术知道要修材质)
	 *
	 * 失败处理 (大厂原则 — 协议必须满足, 不满足就显式失败):
	 *   - Mesh 失效: Ensure + Log(Error) + 标记失败, 组件禁用 Tick
	 *   - 材质创建失败: Log(Error) 单条, 不静默
	 */
	void CollectDynamicMaterials();

	/** 【大厂协议验证】检查材质是否包含 DissolveAmount 参数 */
	bool ValidateMaterialHasDissolveParameter(UMaterialInstanceDynamic* MID) const;

public:
	/**
	 * 溶解速度 (值越大溶解越快, 典型 1.0~2.0 表示 1~2 秒完成)
	 * 默认 1.5 = 约 0.67 秒, 与身体 DissolveComponent 默认值保持一致
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Weapon|Dissolve|Config",
		meta = (ClampMin = "0.1", ClampMax = "10.0"))
	float DissolveSpeed = 1.5f;

protected:
	/** 动态材质实例数组 (驱动 DissolveAmount 参数) */
	UPROPERTY()
	TArray<TObjectPtr<UMaterialInstanceDynamic>> DynamicMaterials;

	/** 当前溶解进度 0.0~1.1 */
	float CurrentDissolveValue = 0.0f;

	/** 是否正在溶解中 */
	bool bIsDissolving = false;

	/** 是否已收集过材质 (防止重复收集, 协议: StartDissolve 只触发一次收集) */
	bool bMaterialsCollected = false;
};
