// ==========================================
// 复活无敌期视觉闪烁 Component 【2026.07.14 P0 大厂架构】
//
// @brief 角色身体皮肤一明一暗（无敌期视觉反馈）
//
// 【架构定位 — SRP 关注点分离】
//   - HealthComponent:   无敌期数据权威 (bIsInvincible)
//   - HealthComponent:   无敌期事件广播 (OnInvincibilityChanged)
//   - 本组件:            收到事件后准备 MID, 派发 BP 视觉事件 (完全不管数据)
//   - BP 子类:           接收 BP 事件 → Timeline → 驱动 MID 的 FlickerAmount 参数
//
// 【职责对等 — 与 DissolveComponent 同款协议】
//   单一协议:
//     1. 身体材质蓝图必须包含标量参数 "FlickerAmount"（默认 0.0）
//     2. 材质蓝图把 FlickerAmount 接到 Emissive 或 BaseColor（亮/暗效果由 BP 决定）
//     3. 不允许用 "颜色 × 0" 等 "假参数" 代替 — 必须真实参数
//
//   协议验证（零兜底）:
//     - C++ 启动时会查 MID 是否真有 FlickerAmount 参数
//     - 缺失 → Log(Warning) 报警, 闪光功能失效但不崩
//     - 强制修复: 美术在 M_Character 材质蓝图加 ScalarParameter "FlickerAmount"
//
// 【为什么不在 HealthComponent / CharacterEvents 加闪烁代码】
//   - Material 操作 = 视觉层职责, 与"血量数据"无关, 不能混入数据组件
//   - 与 DissolveComponent 完全对称: 死亡时身体溶解也是单独组件 (v24 P0 重构落地)
//   - 新增 BTTask/Weapon/Trap 等想用"无敌期闪烁"时, 可挂这个组件即可, 不污染 HealthComp
//
// 【零跨边界】
//   - 只管 Owner 自己的 Mesh, 不触碰武器（武器自治）
//   - 不触碰 HealthComponent 内部状态, 只订阅事件
//
// 【Slot 过滤支持】
//   - 默认: 闪烁所有 Mesh 的 Material Slot (0..NumMaterials-1)
//   - 策划可配 TargetMaterialSlotIndices (例如 "只闪身体 0, 不闪装备 1/2") — BP 默认空数组
//   - 武器/装备/背包 Slot 不闪烁, 避免误闪
//
// 【网络模型】
//   - 服务器不直接 Tick MID, 客户端也不（视觉不需要网络同步）
//   - 服务器/客户端各自本地订阅 OnInvincibilityChanged → 各自启动 Timeline
//   - 这是大厂原则 - "同一份视觉逻辑, 服务器/客户端独立执行"（HUD/特效都是这个模式）
// ==========================================
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "InvincibilityFlickerComponent.generated.h"

// 前向声明 (减少 include)
class UMaterialInstanceDynamic;
class USkeletalMeshComponent;
class UHealthComponent;


// ==========================================
// 事件定义 — 蓝图订阅这些事件驱动 Timeline
// ==========================================

/**
 * 闪烁启动事件
 *
 * 调用场景: HealthComponent->OnInvincibilityChanged(true) 触发本事件
 *
 * BP 子类职责:
 *   1. Play 闪烁 Timeline（0→1→0 周期循环）
 *   2. Timeline 每帧更新 → 调 SetFlickerAmount(Float) 写 MID
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnFlickerStarted);

/**
 * 闪烁停止事件
 *
 * 调用场景: HealthComponent->OnInvincibilityChanged(false) 触发本事件
 *
 * BP 子类职责:
 *   1. Reverse 或 Stop 闪烁 Timeline
 *   2. Timeline 归零后, 自动写 FlickerAmount=0 (调 ResetFlicker)
 *
 * 设计: 派发本事件前, C++ 已经把所有 MID 重置为 0 — 防止 BP 没订阅时残留
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnFlickerStopped);


/**
 * @class UInvincibilityFlickerComponent
 * @brief 复活无敌期视觉闪烁 — 数据/视觉分离架构
 *
 * 使用方法:
 *   1. BaseCharacter 构造函数中 CreateDefaultSubobject<UInvincibilityFlickerComponent>
 *   2. 让 BP 子类继承, 在 BP 中编写 Timeline + 监听 OnFlickerStarted/Stopped
 *   3. 配合协议: 身体材质蓝图必须有 "FlickerAmount" 标量参数
 *
 * 大厂原则 - 单一真理源:
 *   - 数据: HealthComponent->bIsInvincible (Replicated)
 *   - 视觉素材（MID 列表）: 本组件 PrepareMaterials() 收集
 *   - 视觉动画（Timeline）: BP 子类
 */
UCLASS(ClassGroup = (MetalSlug), meta = (BlueprintSpawnableComponent))
class METALSLUG01_API UInvincibilityFlickerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UInvincibilityFlickerComponent();

	// ==========================================
	// 蓝图事件 (BP 子类订阅 — Timeline 驱动)
	// ==========================================

	/** 无敌期开始 — BP 启动闪烁 Timeline */
	UPROPERTY(BlueprintAssignable, Category = "Flicker")
	FOnFlickerStarted OnFlickerStarted;

	/** 无敌期结束 — BP 停止闪烁 Timeline */
	UPROPERTY(BlueprintAssignable, Category = "Flicker")
	FOnFlickerStopped OnFlickerStopped;

	// ==========================================
	// 蓝图调用 API (BP Timeline 的 Update 回调中调用)
	// ==========================================

	/**
	 * 设置所有已收集 MID 的 FlickerAmount 参数
	 * @param FlickerValue 0.0~1.0 (BP Timeline 输出范围, 由 BP 自己决定)
	 *
	 * 大厂原则 - 零兜底:
	 *   - MID 列表为空 → Log(Warning) + return (不抛异常)
	 *   - 提醒 BP 调用前应等待 OnFlickerStarted 后再调用本方法
	 */
	UFUNCTION(BlueprintCallable, Category = "Flicker")
	void SetFlickerAmount(float FlickerValue);

	/**
	 * 强制重置所有 MID 的 FlickerAmount = 0.0
	 * 调用场景:
	 *   - OnFlickerStopped 时 C++ 自动调（不依赖 BP）
	 *   - 死亡/退出关卡时的兜底
	 */
	UFUNCTION(BlueprintCallable, Category = "Flicker")
	void ResetFlicker();

	// ==========================================
	// 蓝图可读状态 (BP 也好判断)
	// ==========================================

	/** 是否已收集过 MID（用于 BP 在 OnFlickerStarted 时判断） */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Flicker")
	bool HasPreparedMaterials() const { return bMaterialsPrepared; }

	// ==========================================
	// 协议配置（编辑器可调）
	// ==========================================

	/**
	 * 闪烁材质参数名 — 物理协议
	 *
	 * 材质蓝图 M_Character 必须定义此名称的 ScalarParameter, 否则不会闪烁
	 * 默认 "FlickerAmount" — 与 MF_Dissolve/DissolveAmount 命名一致
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flicker|Protocol")
	FName FlickerMaterialParameterName = FName(TEXT("FlickerAmount"));

	/**
	 * 目标 Material Slot 索引列表（精确控制）
	 *
	 * 默认空（=空 TArray）= 闪烁所有 Slot
	 * 若策划想"只闪身体 0, 不闪装备 1/2", 在 BP 默认值填 [0]
	 *
	 * 大厂原则 - 配置驱动而非代码分支:
	 *   旧版会写 if (Slot==0) FlickerSlot(); else SkipSlot();
	 *   现在 BP 默认值控制, C++ 0 分支
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Flicker|Protocol")
	TArray<int32> TargetMaterialSlotIndices;

protected:
	// ==========================================
	// UE 生命周期
	// ==========================================
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// ==========================================
	// 内部辅助
	// ==========================================

	/**
	 * HealthComponent 无敌状态变化回调
	 *
	 * 单一真理源订阅:
	 *   - 服务器: HealthComponent Activate/Expire 主动 Broadcast, 触发本回调
	 *   - 客户端: HealthComponent OnRep_InvincibilityChanged 自动 Broadcast, 触发本回调
	 *   - 双发保证: 服务器/客户端都恰好收到一次
	 *
	 * @param bIsNowInvincible true=进入无敌, false=退出无敌
	 */
	UFUNCTION()
	void HandleInvincibilityChanged(bool bIsNowInvincible);

	/**
	 * 收集 Owner 身体 Mesh 的 MID 列表
	 *
	 * 协议:
	 *   - 遍历 SkeletalMesh Slot
	 *   - 对每个 Slot CreateDynamicMaterialInstance
	 *   - 验证是否含 FlickerAmount 参数（缺失 Log Warning 报警）
	 *
	 * 触发时机: OnFlickerStarted 第一次启动时调用（懒加载）
	 *   - 不在 BeginPlay 调：避免 Mesh 在 BeginPlay 时未初始化
	 *   - 不依赖 Tick：完全事件驱动
	 */
	void PrepareMaterials();

	/**
	 * 验证 MID 是否含指定参数
	 *
	 * @return true=有效, false=缺失（应报警, 让美术修材质）
	 *
	 * 注: UE 5.6 验证方式 — GetAllScalarParameterInfo + 遍历找 Name
	 */
	bool ValidateMaterialHasFlickerParameter(UMaterialInstanceDynamic* Mat) const;

	/**
	 * 获取 Owner 的骨骼网格（与 DissolveComponent::GetOwnerSkeletalMesh 模式一致）
	 */
	USkeletalMeshComponent* GetOwnerSkeletalMesh() const;

	// ==========================================
	// 字段
	// ==========================================

	/**
	 * 已收集的 MID 列表 (与 DissolveComponent::DynamicMaterials 同款模式)
	 *
	 * UPROPERTY 修饰防止 GC, 确保 BP 调用时引用有效
	 */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> FlickerMaterials;

	/** 是否已准备过材料 (幂等保证) */
	UPROPERTY(Transient)
	bool bMaterialsPrepared = false;

	/** 是否正在闪烁（仅用于调试日志, 真理源在 HealthComp） */
	UPROPERTY(Transient)
	bool bIsFlickering = false;

	/**
	 * HealthComponent 缓存 (弱引用, 避免循环依赖)
	 *
	 * 大厂原则:
	 *   - OwnerCharacter 弱引用同 DissolveComponent 模式 (避免 BeginPlay 缓存失效)
	 *   - HealthComponent 必须能从 Owner 拿到, 用 TWeakObjectPtr 持有 (零跨边界)
	 */
	UPROPERTY(Transient)
	TWeakObjectPtr<UHealthComponent> CachedHealthComponent;

	/** 订阅句柄（用于 EndPlay 取消订阅）*/
	FDelegateHandle InvincibilityDelegateHandle;
};
