// ==========================================
// 母体残血虚弱闪烁 Component 【v101.4 大厂架构 — C++ 自己驱动闪烁效果，不依赖 BP Timeline】
//
// @brief 母体被打到残血状态（≤ WeakHealthThreshold）时,身体皮肤一明一暗（虚弱视觉反馈）
//
// 【业务规则 (用户 2026.07.27 明确)】
//   - 触发: CurrentHealth <= WeakHealthThreshold (默认 100.0)
//   - 停止: CurrentHealth >  WeakHealthThreshold (血量回上去)
//   - 只对母体角色生效, 刀战模式 / 普通玩家角色 = 永远不触发
//
// 【v101.4 重构 — C++ 自己驱动闪烁效果】
//
// 原架构依赖 BP Timeline 驱动闪烁，存在以下问题：
//   - BP 没有正确订阅 OnWeakFlickerStopped 事件
//   - BP Timeline 持续调用 SetFlickerAmount
//   - 导致视觉效果无法停止
//
// 新架构：完全事件驱动，不依赖 BP
//   - HandleHealthChanged → 阈值检测 → SetIsWeakFlickering
//   - OnRep_WeakFlickeringChanged → 启动/停止 Timer 驱动闪烁
//   - C++ 自己控制 FlickerAmount，无需 BP 介入
//
// 【架构定位 — SRP 关注点分离】
//   - HealthComponent:   血量数据权威 (CurrentHealth, MaxHealth, Replicated)
//   - HealthComponent:   血量事件广播 (OnHealthChanged)
//   - 本组件:            收到事件 → 检测阈值 → Timer 驱动 MID 的 FlickerAmount 参数
//
// 【职责对等 — 与 InvincibilityFlickerComponent 完全对称】
//   单组件职责:
//     1. 身体材质蓝图必须包含标量参数 "FlickerAmount" (默认 0.0)
//     2. 材质蓝图把 FlickerAmount 接到 Emissive 或 BaseColor (亮/暗效果由 C++ 驱动)
//     3. C++ 通过 Timer 周期性设置 FlickerAmount (0→1→0 周期循环)
//     4. 不依赖 BP Timeline — 完全自包含
//
// 【网络模型 — 双发保证】
//   - 服务器: HandleHealthChanged → 设 bIsWeakFlickering (Replicated) → 启动/停止 Timer
//   - 客户端: 收到 bIsWeakFlickering → OnRep_WeakFlickeringChanged → 启动/停止 Timer
//   - 服务器/客户端各自本地启动 Timer，视觉效果各自独立
//
// 【零跨边界】
//   - 只管 Owner 自己的 Mesh, 不触碰武器（武器自治）
//   - 不触碰 HealthComponent 内部状态, 只订阅 OnHealthChanged
//
// 【Slot 过滤支持】
//   - 默认: 闪烁所有 Mesh 的 Material Slot (0..NumMaterials-1)
//   - 策划可配 TargetMaterialSlotIndices (例如 "只闪身体 0, 不闪装备 1/2") — BP 默认空数组
//   - 武器/装备/背包 Slot 不闪烁, 避免误闪
// ==========================================
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MotherLowHealthFlickerComponent.generated.h"

// 前向声明 (减少 include)
class UMaterialInstanceDynamic;
class USkeletalMeshComponent;
class UHealthComponent;


/**
 * 母体残血虚弱闪烁启动事件
 *
 * 调用场景: bIsWeakFlickering 从 false 变 true 时 (OnRep 或服务器主动)
 *
 * BP 子类职责:
 *   1. Play 虚弱闪烁 Timeline（0→1→0 周期循环, 可比无敌期闪烁更慢更弱）
 *   2. Timeline 每帧更新 → 调 SetFlickerAmount(Float) 写 MID
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnWeakFlickerStarted);

/**
 * 母体残血虚弱闪烁停止事件
 *
 * 调用场景: bIsWeakFlickering 从 true 变 false 时 (OnRep 或服务器主动)
 *
 * BP 子类职责:
 *   1. Reverse 或 Stop 虚弱闪烁 Timeline
 *   2. Timeline 归零后, 自动写 FlickerAmount=0 (调 ResetFlicker)
 *
 * 设计: 派发本事件前, C++ 已经把所有 MID 重置为 0 — 防止 BP 没订阅时残留
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE(FOnWeakFlickerStopped);


/**
 * @class UMotherLowHealthFlickerComponent
 * @brief 母体残血虚弱闪烁 — 数据/视觉分离架构 (与 InvincibilityFlickerComponent 完全对称)
 *
 * 使用方法:
 *   1. BaseCharacter 构造函数中 CreateDefaultSubobject<UMotherLowHealthFlickerComponent>
 *   2. 让 BP 子类继承, 在 BP 中编写 Timeline + 监听 OnWeakFlickerStarted/Stopped
 *   3. 配合协议: 身体材质蓝图必须有 "FlickerAmount" 标量参数 (与 InvincibilityFlickerComponent 共享)
 *
 * 大厂原则 - 单一真理源:
 *   - 触发数据: HealthComponent->CurrentHealth (Replicated, 服务器权威)
 *   - 触发判定: 本组件基于 OnHealthChanged 订阅 + WeakHealthThreshold 配置
 *   - 状态字段: bIsWeakFlickering (Replicated, 服务器权威, 客户端 OnRep 同步)
 *   - 视觉素材（MID 列表）: 本组件 PrepareMaterials() 收集
 *   - 视觉动画（Timeline）: BP 子类
 *
 * 大厂原则 - 零重复:
 *   - 不重新写 PrepareMaterials / ValidateMaterialHasFlickerParameter, 完全复用 InvincibilityFlickerComponent 的协议
 *   - 同一材质参数 FlickerAmount (与 InvincibilityFlickerComponent 共享, BP Timeline 可共用)
 *   - 不在 HealthComponent / CharacterEvents 内写视觉逻辑
 */
UCLASS(ClassGroup = (MetalSlug), meta = (BlueprintSpawnableComponent))
class METALSLUG01_API UMotherLowHealthFlickerComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMotherLowHealthFlickerComponent();

	// ==========================================
	// 蓝图事件 (BP 订阅 — 用于扩展，不必须)
	// ==========================================

	/**
	 * 母体残血虚弱闪烁启动事件
	 *
	 * 调用场景: bIsWeakFlickering 从 false 变 true 时 (OnRep 或服务器主动)
	 *
	 * BP 可订阅此事件做额外处理，例如播放音效
	 * 注: v101.4 C++ 已自己驱动闪烁效果，此事件非必须
	 */
	UPROPERTY(BlueprintAssignable, Category = "Mother|Flicker")
	FOnWeakFlickerStarted OnWeakFlickerStarted;

	/**
	 * 母体残血虚弱闪烁停止事件
	 *
	 * 调用场景: bIsWeakFlickering 从 true 变 false 时 (OnRep 或服务器主动)
	 *
	 * BP 可订阅此事件做额外处理，例如停止音效
	 * 注: v101.4 C++ 已自己驱动闪烁效果，此事件非必须
	 */
	UPROPERTY(BlueprintAssignable, Category = "Mother|Flicker")
	FOnWeakFlickerStopped OnWeakFlickerStopped;

	// ==========================================
	// 协议配置（编辑器可调）
	// ==========================================

	/**
	 * 闪烁材质参数名 — 物理协议
	 *
	 * 材质蓝图必须定义此名称的 ScalarParameter, 否则不会闪烁
	 * 默认 "FlickerAmount" — 与 InvincibilityFlickerComponent 一致
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mother|Flicker|Protocol")
	FName FlickerMaterialParameterName = FName(TEXT("FlickerAmount"));

	/**
	 * 目标 Material Slot 索引列表（精确控制）
	 *
	 * 默认空（=空 TArray）= 闪烁所有 Slot
	 * 若策划想"只闪身体 0, 不闪装备 1/2", 在 BP 默认值填 [0]
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mother|Flicker|Protocol")
	TArray<int32> TargetMaterialSlotIndices;

	/**
	 * 虚弱闪烁触发阈值 (血量 <= 该值时启动闪烁)
	 *
	 * 业务规则 (用户 2026.07.27 明确):
	 *   - 残血 (≤ 100.0) → 启动闪烁
	 *   - 非残血 (> 100.0) → 停止闪烁
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mother|Flicker|Protocol", meta = (ClampMin = "0.0", ClampMax = "500.0"))
	float WeakHealthThreshold = 100.0f;

	/**
	 * 闪烁周期 (秒) — 【v101.4 新增】
	 *
	 * 一个完整的闪烁周期时间 (0→1→0)
	 * 默认 1.0s，比无敌期闪烁更慢（无敌期约 0.5s）
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mother|Flicker|Protocol", meta = (ClampMin = "0.1", ClampMax = "5.0"))
	float FlickerPeriodSeconds = 1.0f;

protected:
	// ==========================================
	// UE 生命周期
	// ==========================================
	virtual void BeginPlay() override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/**
	 * 网络同步 — bIsWeakFlickering 复制支持
	 *
	 * 大厂原则 - 单一真理源:
	 *   - 服务器: HandleHealthChanged 内 SetIsWeakFlickering 主动设字段 + Broadcast
	 *   - 客户端: 通过 Replicated 自动同步, 本回调 Broadcast
	 *   - 双发保证: 服务器主动设字段不依赖 OnRep (服务器已主动 Broadcast); 客户端走 OnRep
	 */
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	// ==========================================
	// 内部辅助
	// ==========================================

	/**
	 * HealthComponent 血量改变回调
	 *
	 * 单一真理源订阅:
	 *   - 服务器: HealthComponent ApplyDamage/Heal 主动 Broadcast, 触发本回调
	 *   - 客户端: HealthComponent OnRep 自动 Broadcast, 触发本回调
	 *   - 双发保证: 服务器/客户端都恰好收到一次
	 *
	 * 本回调内:
	 *   1. 读 HealthComp->GetCurrent() 与 WeakHealthThreshold 比较
	 *   2. 与当前 bIsWeakFlickering 比对, 只有状态变化才 SetIsWeakFlickering
	 *   3. SetIsWeakFlickering 内部 Broadcast (服务器/客户端一致)
	 *
	 * @param NewHealth HealthComponent 广播的当前血量值
	 */
	UFUNCTION()
	void HandleHealthChanged(float NewHealth);

	/**
	 * OnRep_WeakFlickeringChanged — 客户端收到 bIsWeakFlickering 变化时广播事件
	 *
	 * 设计 - 与 InvincibilityFlickerComponent::OnRep_InvincibilityChanged 同构:
	 *   - 服务器: SetIsWeakFlickering 主动 Broadcast (服务器跑一次)
	 *   - 客户端: 通过 Replicated 自动同步, 本回调 Broadcast (客户端跑一次)
	 *   - 双发保证: 服务器自己不会进 OnRep (服务器已主动 Broadcast) — 不会重复触发
	 */
	UFUNCTION()
	void OnRep_WeakFlickeringChanged();

	/**
	 * 集中状态切换入口
	 *
	 * 大厂原则:
	 *   - 唯一入口: 设字段 + 启动/停止 Timer + Broadcast — 全部一处
	 *   - 幂等: bIsWeakFlickering == bNewState → 早返回
	 *
	 * @param bNewState 期望的新状态
	 */
	void SetIsWeakFlickering(bool bNewState);

	/**
	 * 收集 Owner 身体 Mesh 的 MID 列表
	 *
	 * 协议:
	 *   - 遍历 SkeletalMesh Slot
	 *   - 对每个 Slot CreateDynamicMaterialInstance
	 *   - 验证是否含 FlickerMaterialParameterName 参数（缺失 Log Warning 报警）
	 */
	void PrepareMaterials();

	/**
	 * 验证 MID 是否含指定参数
	 *
	 * @return true=有效, false=缺失（应报警, 让美术修材质）
	 */
	bool ValidateMaterialHasFlickerParameter(UMaterialInstanceDynamic* Mat) const;

	/**
	 * 获取 Owner 的骨骼网格
	 */
	USkeletalMeshComponent* GetOwnerSkeletalMesh() const;

	/**
	 * 启动闪烁 Timer 【v101.4 新增】
	 *
	 * 调用场景: OnRep_WeakFlickeringChanged(true) 时
	 */
	void StartFlickerTimer();

	/**
	 * 停止闪烁 Timer 【v101.4 新增】
	 *
	 * 调用场景: OnRep_WeakFlickerChanged(false) 时 或 EndPlay 时
	 */
	void StopFlickerTimer();

	/**
	 * 闪烁 Tick 回调 【v101.4 新增】
	 *
	 * Timer 周期性调本函数，驱动 FlickerAmount 在 0~1 之间正弦变化
	 */
	UFUNCTION()
	void OnFlickerTimerTick();

	/**
	 * 设置材质 FlickerAmount 参数 【v101.4 新增】
	 *
	 * 内部使用，不暴露给 BP
	 */
	void SetFlickerAmountInternal(float FlickerValue);

	/**
	 * 强制重置所有 MID 的 FlickerAmount = 0.0 【v101.4 保留供 BP 兼容】
	 */
	UFUNCTION(BlueprintCallable, Category = "Mother|Flicker")
	void ResetFlicker();

	// ==========================================
	// 字段
	// ==========================================

	/**
	 * 已收集的 MID 列表
	 *
	 * UPROPERTY 修饰防止 GC
	 */
	UPROPERTY(Transient)
	TArray<TObjectPtr<UMaterialInstanceDynamic>> FlickerMaterials;

	/** 是否已准备过材料 (幂等保证) */
	UPROPERTY(Transient)
	bool bMaterialsPrepared = false;

	/**
	 * 母体残血虚弱闪烁状态字段 — 服务器权威, Replicated
	 *
	 * 大厂原则 - 单一真理源:
	 *   - 服务器写 → 客户端自动同步 → OnRep 自动 Broadcast
	 *   - 服务器主动 SetIsWeakFlickering 也手动 Broadcast → 服务器/客户端都恰好收到一次
	 */
	UPROPERTY(ReplicatedUsing = OnRep_WeakFlickeringChanged, VisibleAnywhere, BlueprintReadOnly, Category = "Mother|Flicker")
	bool bIsWeakFlickering = false;

	/**
	 * HealthComponent 缓存 (弱引用, 避免循环依赖)
	 */
	UPROPERTY(Transient)
	TWeakObjectPtr<UHealthComponent> CachedHealthComponent;

	/** 订阅句柄（用于 EndPlay 取消订阅）*/
	FDelegateHandle HealthChangedDelegateHandle;

	/**
	 * BeginPlay 完成标记
	 */
	UPROPERTY(Transient)
	bool bHasBeginPlayCompleted = false;

	/** 闪烁 Timer 句柄 【v101.4 新增】 */
	FTimerHandle FlickerTimerHandle;

	/** 闪烁 Timer 周期 【v101.4 新增】 */
	float FlickerTimerInterval = 0.05f; // 20Hz 刷新率

	/** 闪烁起始时间 【v101.4 新增】 */
	float FlickerStartTime = 0.0f;
};