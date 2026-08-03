// ==========================================
// 版权声明：在项目设置的描述页面填写您的版权信息。
// ==========================================
//
// @file WeaponDropComponent.h
// @brief 掉落武器自治组件 — 管理落地武器的生命周期、物理模拟、弹药快照
//
// 【v200 大厂架构 — 新增功能】
// 功能背景:
//   - 玩家按 G 键丢弃主武器
//   - 丢弃的武器带原先的弹夹内子弹数和总子弹数
//   - 其他玩家/AI 走到掉落武器上能自动捡起
//   - 主武器丢弃后，玩家手上自动切换为近战武器
//
// 【架构定位】
//   - 挂在 ABaseWeapon 上 (与 UWeaponDissolveComponent / UWeaponFireComponent 对称)
//   - 武器掉落时由 UWeaponAttachmentComponent 调用 StartDroppedState()
//   - 武器被捡起时由 UWeaponAttachmentComponent 调用 CancelDroppedState()
//
// 【大厂原则 — 单一真理源】
//   - 弹药数据: UWeaponFireComponent 是弹药真理源, 本组件只保存快照用于恢复
//   - 掉落状态: 本组件持有 bIsDropped + DroppedOwner + PickupRadius 等掉落状态
//   - 不持有武器数据引用, 只持有快照数据 (弹药数值)
//
// 【大厂原则 — 零兜底】
//   - Owner Weapon 无效 → Log Error + return
//   - 重复掉落 → Log Warning (幂等)
//   - PickupRadius 未配置 → Log Error + return
//
// 【AI 不做行为树 — 玩家路径专用】
//   - Server_TryPickupWeapon 供玩家调用 (AI 不走这个路径)
//   - AI 捡武器后续通过 AIController / BT 扩展 (如果需要)
// ==========================================
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "WeaponDropComponent.generated.h"

// 前置声明
class ABaseCharacter;
class ABaseWeapon;
class UPrimitiveComponent;

// ==========================================
// 弹药快照 — 保存掉落时刻的弹药数据, 供捡起时恢复
// ==========================================
USTRUCT(BlueprintType)
struct FAmmoSnapshot
{
	GENERATED_BODY()

	// 当前弹匣内子弹数
	UPROPERTY(BlueprintReadOnly)
	int32 CurrentAmmo = 0;

	// 备用弹药总数
	UPROPERTY(BlueprintReadOnly)
	int32 ReserveAmmo = 0;

	// 弹匣容量 (用于校验恢复数据)
	UPROPERTY(BlueprintReadOnly)
	int32 MagazineSize = 0;

	// 是否有效 (用于判断是否已快照)
	UPROPERTY(BlueprintReadOnly)
	bool bIsValid = false;
};

// ==========================================
// 掉落武器事件委托
// ==========================================
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnDroppedWeaponReady, ABaseWeapon*, DroppedWeapon);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnDroppedWeaponPickedUp, ABaseWeapon*, DroppedWeapon, ABaseCharacter*, Picker);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnDroppedWeaponExpired, ABaseWeapon*, ExpiredWeapon);

// ==========================================
// UWeaponDropComponent
// ==========================================
UCLASS(ClassGroup = (MetalSlug), meta = (BlueprintSpawnableComponent))
class METALSLUG01_API UWeaponDropComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	// ==========================================
	// 构造函数
	// ==========================================
	UWeaponDropComponent();

	// ==========================================
	// 掉落状态控制
	// ==========================================

	/**
	 * 【v200 大厂架构 P0】启动掉落状态 — 丢弃武器时调用
	 *
	 * 调用方: UWeaponAttachmentComponent::DetachWeaponToGround (玩家按 G 键路径)
	 *
	 * 流程:
	 *   1. 幂等检查: 已掉落 → Log Warning + return
	 *   2. 保存弹药快照 (从 WeaponFireComponent 读)
	 *   3. 设置掉落状态 bIsDropped=true
	 *   4. 启用物理模拟 (模拟重力掉落)
	 *   5. 设置碰撞为 PhysicsOnly (能被踩但不被穿过)
	 *   6. 启动生命周期计时器 (PickupLifetimeSeconds)
	 *   7. 开始 Overlap 检测 (等待玩家捡起)
	 *   8. 广播 OnDroppedWeaponReady 事件
	 *
	 * @param DropInstigator 丢弃者角色 (用于掉落方向计算)
	 *
	 * 零兜底:
	 *   - Owner Weapon 无效 → Log Error + return
	 *   - 重复调用 → Log Warning + return (幂等)
	 *   - PickupRadius <= 0 → Log Error + return
	 */
	UFUNCTION(BlueprintCallable, Category = "Weapon|Drop")
	void StartDroppedState(ABaseCharacter* DropInstigator);

	/**
	 * 【v200 大厂架构 P0】取消掉落状态 — 武器被捡起时调用
	 *
	 * 调用方: UWeaponAttachmentComponent::Server_TryPickupWeapon (玩家走到掉落武器上)
	 *
	 * 流程:
	 *   1. 幂等检查: 未掉落 → Log Warning + return false
	 *   2. 停止生命周期计时器
	 *   3. 禁用物理模拟 (转为挂载状态)
	 *   4. 恢复弹药快照 (写入 WeaponFireComponent)
	 *   5. 设置掉落状态 bIsDropped=false
	 *   6. 关闭 Overlap 检测
	 *   7. 广播 OnDroppedWeaponPickedUp 事件
	 *
	 * @param Picker 捡起者角色
	 * @return true=成功捡起, false=失败
	 *
	 * 零兜底:
	 *   - 未在掉落状态 → Log Warning + return false
	 *   - 弹药快照无效 → Log Error + return false
	 */
	UFUNCTION(BlueprintCallable, Category = "Weapon|Drop")
	bool CancelDroppedState(ABaseCharacter* Picker);

	/**
	 * 【v200 大厂架构】查询是否在掉落状态
	 */
	UFUNCTION(BlueprintPure, Category = "Weapon|Drop")
	bool IsDropped() const { return bIsDropped; }

	/**
	 * 【v200 大厂架构】查询掉落者
	 */
	UFUNCTION(BlueprintPure, Category = "Weapon|Drop")
	ABaseCharacter* GetDropInstigator() const { return DropInstigator.Get(); }

	// ==========================================
	// 查询接口
	// ==========================================

	/**
	 * 【v200 大厂架构】获取弹药快照 (供捡起时恢复弹药)
	 */
	UFUNCTION(BlueprintPure, Category = "Weapon|Drop")
	FAmmoSnapshot GetAmmoSnapshot() const { return AmmoSnapshot; }

	/**
	 * 【v200 大厂架构】获取掉落武器名称 (供 UI 显示)
	 */
	UFUNCTION(BlueprintPure, Category = "Weapon|Drop")
	FString GetDroppedWeaponName() const;

	// ==========================================
	// 事件广播 (供 UI / 特效系统订阅)
	// ==========================================
	UPROPERTY(BlueprintAssignable, Category = "Weapon|Drop|Events")
	FOnDroppedWeaponReady OnDroppedWeaponReady;

	UPROPERTY(BlueprintAssignable, Category = "Weapon|Drop|Events")
	FOnDroppedWeaponPickedUp OnDroppedWeaponPickedUp;

	UPROPERTY(BlueprintAssignable, Category = "Weapon|Drop|Events")
	FOnDroppedWeaponExpired OnDroppedWeaponExpired;

protected:
	// ==========================================
	// UE 生命周期
	// ==========================================
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	// ==========================================
	// Overlap 检测
	// ==========================================
	/**
	 * 玩家进入掉落武器范围时的回调
	 */
	UFUNCTION()
	void OnOverlapBegin(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
	                   UPrimitiveComponent* OtherComp, int32 OtherBodyIndex,
	                   bool bFromSweep, const FHitResult& Hit);

	// ==========================================
	// 生命周期计时器回调
	// ==========================================
	/**
	 * 掉落武器过期回调
	 * 触发: PickupLifetimeSeconds 到期
	 * 行为: 广播 OnDroppedWeaponExpired + 调用 Weapon->StartDissolve()
	 */
	UFUNCTION()
	void OnPickupLifetimeExpired();

	// ==========================================
	// 内部辅助
	// ==========================================

	/**
	 * 解析 Owner Weapon
	 * 大厂原则: 不缓存, 每次 GetOwner() + Cast (与 v40.6 WeaponFireComponent 同模式)
	 */
	ABaseWeapon* ResolveOwnerWeapon() const;

	/**
	 * 保存弹药快照 (从 WeaponFireComponent 读当前弹药)
	 */
	void SaveAmmoSnapshot();

	/**
	 * 恢复弹药快照 (写入 WeaponFireComponent)
	 */
	void RestoreAmmoSnapshot();

	// ==========================================
	// 配置属性 (BP 可调)
	// ==========================================

	/**
	 * 掉落武器可被捡起的半径 (厘米)
	 * 玩家走进此范围即触发捡起
	 *
	 * 【v200.2.3 调整】默认 100cm 过小 — 玩家一帧移动 100cm+ 会错过 overlap
	 *   用户反馈 "走到武器上武器消失，但是身上没主武器" 是因 100cm 太苛刻
	 *   调到 250cm (玩家正常移动能稳定触发 overlap)
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Drop|Config",
		meta = (ClampMin = "50.0", ClampMax = "500.0"))
	float PickupRadius = 250.0f;

	/**
	 * 掉落武器存在时长 (秒)
	 * 到期后武器溶解消失
	 * 默认 60 秒
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Drop|Config",
		meta = (ClampMin = "5.0", ClampMax = "300.0"))
	float PickupLifetimeSeconds = 60.0f;

	/**
	 * 掉落时前推力 (厘米/秒)
	 * 玩家面朝方向施加速度, 让武器"丢在面前"
	 *
	 * 【v200.2.3 调整】500cm/s 太大 — 武器飞出 5m+ 玩家追不上
	 *   用户反馈 "走到武器上武器消失" 正是武器飞太远
	 *   调到 150cm/s (武器落地点落在玩家面前 1-2m, 视觉自然, 玩家能踩到)
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Drop|Config",
		meta = (ClampMin = "0.0", ClampMax = "2000.0"))
	float LaunchForwardSpeed = 150.0f;

	/**
	 * 掉落时向上抛力 (厘米/秒)
	 * 让武器有点小抛物线, 但不能太高 (否则飞太远)
	 *
	 * 【v200.2.3 调整】200cm/s 太大 — 武器会飞 1m 高 + 飞 2m 远
	 *   调到 100cm/s (简单抛一下, 落地就在玩家附近)
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Drop|Config",
		meta = (ClampMin = "0.0", ClampMax = "1000.0"))
	float LaunchUpwardSpeed = 100.0f;

	// ==========================================
	// 掉落状态 (运行时)
	// ==========================================

	/**
	 * 是否在掉落状态
	 * true = 武器已从玩家手中掉落, 在地上等待被捡起
	 * false = 武器在玩家手中或不存在
	 */
	UPROPERTY()
	bool bIsDropped = false;

	/**
	 * 丢弃武器的玩家引用
	 * 用于判断是否可以捡起 (可能需要避免立即捡起自己的武器)
	 */
	UPROPERTY()
	TWeakObjectPtr<ABaseCharacter> DropInstigator;

	/**
	 * 弹药快照
	 * 掉落时保存, 捡起时恢复
	 */
	UPROPERTY()
	FAmmoSnapshot AmmoSnapshot;

	/**
	 * 生命周期计时器句柄
	 */
	UPROPERTY()
	FTimerHandle LifetimeTimerHandle;

	/**
	 * 【v200.2.5 诊断】每秒 Tick 位置日志计时器
	 * 排查 "玩家走过去没触发 overlap" 的根因 (武器位置 vs 玩家位置)
	 */
	UPROPERTY()
	FTimerHandle DropPositionTimerHandle;

	/**
	 * 物理模拟是否已启用 (用于恢复)
	 */
	UPROPERTY()
	bool bWasSimulatingPhysics = false;

	/**
	 * 掉落前的碰撞预设名称 (用于恢复)
	 */
	UPROPERTY()
	FName PreviousCollisionProfile = NAME_None;
};
