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
class UProjectileMovementComponent; // 【v200.4 大厂架构】PMC 用于抛物线阶段 (前向声明避免循环 include)

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
/**
 * @class UWeaponDropComponent
 * @brief 掉落武器自治组件 - 管理玩家丢弃武器的生命周期/物理/弹药快照
 *
 * 单一职责: 玩家按 G 键丢弃主武器 → 武器落地 → 其他玩家可捡起 → 过期溶解
 *
 * 大厂架构中的角色:
 *   - 武器自治: 挂在 ABaseWeapon 上, 与 WeaponDissolveComponent/WeaponFireComponent 对称
 *   - 单一真理源: 弹药数据归 WeaponFireComponent, 本组件只保存快照用于恢复
 *   - 零兜底: 参数错/状态错 → Log Error + return, 不静默跳过
 *   - 单一 RPC 入口: 客户端 → Server_TryPickupWeapon → 服务器处理拾取
 *
 * 关键设计:
 *   - PMC 抛物线 + OnProjectileStop 落地 (替代旧版 SetSimulatePhysics 双重模拟)
 *   - 服务器权威: 客户端只发 RPC, 服务器处理 CancelDroppedState + Attach
 *   - 扔飞刀模式: 锁定 throw_yaw → 抛出姿态 = 落地姿态, 无 yaw 突变
 *   - 弹药快照: SaveAmmoSnapshot (丢弃时) + RestoreAmmoSnapshot (捡起时)
 *
 * 业务规则:
 *   - AI 不捡武器 (玩家路径专用)
 *   - 玩家只有 Primary 槽位为空时才能捡起
 *   - 默认 60s 寿命到期 → 启动溶解消失
 *
 * 使用方式:
 *   - ABaseWeapon 构造函数 CreateDefaultSubobject<UWeaponDropComponent>
 *   - UWeaponAttachmentComponent::DetachWeaponToGround 调 StartDroppedState
 *   - UWeaponAttachmentComponent::Server_TryPickupWeapon 调 CancelDroppedState
 */
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
	 * 【v200.4 大厂架构】落地点沉淀 — 服务器权威冻结位置
	 *
	 * 触发链路 (UE 官方最优解):
	 *   1. StartDroppedState 启动 ProjectileMovementComponent (PMC) 抛物线
	 *   2. PMC 内部速度 < BounceVelocityStopSimulatingThreshold → 触发 OnProjectileStop 回调
	 *   3. OnProjectileStopHandler → 调本函数 SettleWeaponOnGround
	 *
	 * 大厂原则 (UE 官方 + 客户端同步):
	 *   - 关闭 PMC: 停止弹道模拟 (武器不再移动)
	 *   - 关闭物理: 落地后不需要物理引擎
	 *   - LineTrace 找地面: 服务器精确知道地面 Z (客户端本地 trace 可能不准)
	 *   - SetActorLocation (TeleportPhysics): 服务器冻结位置
	 *   - Multicast_FreezeWeaponTransform: 客户端同步冻结 (ReplicateMovement 不复制 PMC 状态)
	 */
	void SettleWeaponOnGround(ABaseWeapon* OwnerWeapon, const FHitResult& InHitResult);

	/**
	 * 【v200.4 大厂架构】PMC OnProjectileStop 回调处理器
	 *
	 * 绑定: StartDroppedState 末尾 → PMC->OnProjectileStop.AddDynamic(this, &UWeaponDropComponent::OnProjectileStopHandler)
	 *
	 * 触发: PMC 速度 < 阈值时自动调用
	 * 职责: 转发到 SettleWeaponOnGround (复用统一沉淀逻辑)
	 *
	 * 大厂原则:
	 *   - 单一入口: 任何"武器落地"都走 OnProjectileStopHandler → SettleWeaponOnGround
	 *   - 不能直接从 BTTask / Blueprint / 其他代码调 SettleWeaponOnGround (时序可能错)
	 *
	 * @param HitResult PMC 命中的 HitResult (地面碰撞点, 可用于直接拿 GroundLocation)
	 */
	UFUNCTION()
	void OnProjectileStopHandler(const FHitResult& HitResult);

	/**
	 * 【v200.4 大厂架构】解析 ProjectileMovementComponent (懒加载)
	 *
	 * 根因 (v200.4 之前的反模式):
	 *   BeginPlay 缓存 PMC 指针 → BP 子类 PMC 创建顺序问题 → 缓存 null
	 * 修复 (与 v40.6 WeaponFireComponent 同模式):
	 *   每次按需 FindComponentByClass — 不缓存, 不假设 BeginPlay 时序
	 *
	 * @return PMC (可能为 null — BP 子类没配 PMC 时合法返回 null, 走物理引擎 fallback)
	 */
	class UProjectileMovementComponent* ResolveProjectileMovement() const;

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
		meta = (ClampMin = "50.0", ClampMax = "1000.0"))
	float PickupRadius = 400.0f; // 【v200.4.6】从 250 提到 400 — 配 5.8 米抛掷距离,玩家不用走很远

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
	 * 【v200.4.6 调整为中距离档位】原 150 cm/s ≈ 1 米 (太近)
	 *   调到 600 cm/s ≈ 5.8 米 (游戏感: 扔到对面能抢枪的位置, 又不会飞出场)
	 *   飞行距离计算公式:
	 *     飞行时间 = (Vy + sqrt(Vy² + 2*g*H)) / g
	 *     飞行距离 = Vx * 飞行时间
	 *     假设手部 H=130cm, g=980cm/s²
	 *
	 * 可调档位 (BP 子类可覆盖):
	 *   - 近 (150): 武器落玩家面前 1 米
	 *   - 中 (600,推荐): 武器落 5.8 米 (敌人脚边可抢)
	 *   - 远 (1200): 武器飞 16 米 (战术投掷)
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Drop|Config",
		meta = (ClampMin = "0.0", ClampMax = "2000.0"))
	float LaunchForwardSpeed = 600.0f;

	/**
	 * 掉落时向上抛力 (厘米/秒)
	 * 让武器有点小抛物线, 决定弧度高点
	 *
	 * 【v200.4.6 调整】原 100 cm/s 弧度太平
	 *   调到 300 cm/s (中等弧度 — 武器小抛物线升到 0.5 米高峰再落)
	 *
	 * 可调档位 (与 LaunchForwardSpeed 配对):
	 *   - 近 (100): 弧度平
	 *   - 中 (300,推荐): 中等弧度
	 *   - 远 (500): 高抛物线
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Drop|Config",
		meta = (ClampMin = "0.0", ClampMax = "1000.0"))
	float LaunchUpwardSpeed = 300.0f;

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
	 * 【v240.13 大厂架构 — 扔飞刀模式】抛出瞬间的武器 yaw (度)
	 *   - 在 StartDroppedState 中, PMC 启动前缓存 OwnerWeapon->GetActorRotation().Yaw
	 *   - 飞行中 PMC 不修改 Actor rotation (bRotationFollowsVelocity=false)
	 *   - 落地时 SettleWeaponOnGround 用此值构造 FinalRotation=(P=0, Y=ThrowYaw, R=0)
	 *     → 抛出姿态 = 落地姿态,无 yaw 突变
	 *   - 不用 bRotationFollowsVelocity=true + bRotationRemainsVertical=true
	 *     因为前者必然让 yaw 跟 velocity (87°→162°),落地瞬间仍有视觉突变
	 */
	UPROPERTY()
	float CachedThrowYaw = 0.0f;

	/**
	 * 【v200.3.13 配置】地面贴齐抬升的安全距离 (cm)
	 *   - 太小 (< 0.5cm): 可能仍然有微小 penetration (浮点精度)
	 *   - 太大 (> 5cm): 视觉上"漂浮"在地面上方
	 *   - 默认 5cm: 明显"贴地" 但又不会视觉上漂浮
	 *   (用户 v200.3.13.1 反馈: 1cm 视觉上仍然贴着地面, 调到 5cm 更明显)
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Drop|Config",
		meta = (ClampMin = "0.0", ClampMax = "10.0"))
	float GroundOffsetCm = 5.0f;

	/**
	 * 【v200.4 大厂架构】PMC 速度停止阈值 (cm/s)
	 *   - 太小 (< 10): 武器飞行中可能误触发停止
	 *   - 太大 (> 200): 武器落地后仍滚动几秒才停
	 *   - 默认 50: 武器落地后弹一两次就停 (符合大厂标准)
	 *   (UE 官方 BounceVelocityStopSimulatingThreshold 文档推荐值)
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon|Drop|Config",
		meta = (ClampMin = "10.0", ClampMax = "500.0"))
	float ProjectileStopVelocityThreshold = 50.0f;

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
