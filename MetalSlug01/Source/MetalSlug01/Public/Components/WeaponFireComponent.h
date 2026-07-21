// ==========================================
// 武器开火组件 (v60 大厂架构 — 弹药真理源 + 射速节流 + 换弹状态机)
//
// 【设计动机】
//   旧 URangedLineStrategy::TickDetection 每帧打一枪 (注释里写 "v52+ 会按 FireComponent 控节奏")
//   现在落地: UWeaponFireComponent 接管所有"节奏 + 弹药 + 换弹"逻辑
//   Strategy 回归纯算法 (按调用执行单发)
//
// 【大厂原则】
//   1. 单一职责: 弹药真理源 + 射速节流 + 换弹状态机, 不管"怎么判定命中"
//   2. 武器自治: 挂载在 ABaseWeapon 上, 跟 WeaponDissolveComponent 对称
//   3. 玩家/AI 共用: 同一套 Component, 真理源 = DT_WeaponInfo
//   4. 网络权威: 所有写操作走服务器 RPC, 客户端通过 Replicated 读
//   5. 零兜底: 弹药空/换弹中/没配 WeaponConfig → Log Error 拒绝开火
//
// 【数据流】
//   DT_WeaponInfo.Fire* (BP 策划配置)
//     ↓ WeaponAttachmentComponent.SpawnAndEquipWeapon 调用
//   UWeaponFireComponent::InitializeFromWeaponConfig (服务器)
//     ↓ 写入字段
//   CurrentAmmo / ReserveAmmo / MagazineSize / FireRateRPM / bIsReloading
//     ↓ Replicated
//   所有客户端 (HUD 读 CurrentAmmo / MagazineSize)
//
// 【调用方】
//   - 玩家: ABaseCharacter::OnFirePressed / OnFireReleased / OnReloadPressed
//           → BaseCharacter 转发壳 → ABaseWeapon::StartFire / StopFire / StartReload
//           → WeaponFireComponent 内部状态机
//   - 未来 AI: BTTask_Fire → 同一路径 (本次不实现)
//
// 【零兜底】
//   - 弹药为 0 → 拒绝开火 (Log Verbose, 不算 Error, 业务正常)
//   - 换弹中 → 拒绝开火 (Log Verbose, 业务正常)
//   - 没配 WeaponConfig → 拒绝一切开火/换弹 (Log Error, 强制修复 DT)
//   - FireRateRPM <= 0 → 拒绝开火 (Log Error, 配置错)
// ==========================================
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "Data/Enums/CombatEnums.h"
#include "WeaponFireComponent.generated.h"

class ABaseWeapon;
class UAnimMontage;
struct FWeaponInfo;

// 弹药状态变化多播: 服务器 OnAmmoChanged/OnReloadStateChanged → 客户端 HUD 订阅
DECLARE_DYNAMIC_MULTICAST_DELEGATE_TwoParams(FOnAmmoChanged, int32, NewAmmo, int32, MagazineSize);
DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnReloadStateChanged, bool, bIsReloading);

/**
 * @class UWeaponFireComponent
 * @brief 武器开火自治组件 (挂在 ABaseWeapon 上)
 *
 * 与 WeaponDissolveComponent 同模式 (v24 武器自治原则):
 *   - 组件拥有自己的 Replicated 字段
 *   - 不依赖外部 Controller, 不穿透 OwnerCharacter
 *   - BaseWeapon 通过 GetWeaponFireComponent() 访问
	 *
	 * 真理源 (v60 单一真理源迁移):
	 *   - MagazineSize / ReserveAmmo / FireRateRPM / ReloadTimeSeconds
	 *     全部从 FWeaponInfo (DT_WeaponInfo) 读取, 不在 BaseWeapon 字段复制
	 *   - BaseWeapon 上原有的 LightDamageBody/Head/HeavyDamage 字段作废 (v60.1)
 *     伤害统一从 DT 读 (Server_ReportHit_Implementation 改读路径)
 */
UCLASS(ClassGroup = (MetalSlug), meta = (BlueprintSpawnableComponent))
class METALSLUG01_API UWeaponFireComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UWeaponFireComponent();

	// ==========================================
	// UE 生命周期
	// ==========================================
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	// ==========================================
	// 初始化 (从 DT 注入配置)
	// ==========================================

	/**
	 * 从 FWeaponInfo 注入配置 (服务器调用, 写入 Replicated 字段)
	 *
	 * 调用方:
	 *   - ABaseWeapon::SpawnAndEquipWeapon (服务器 Spawn 后调用一次)
	 *   - DT 配错 (MagazineSize<=0 / FireRateRPM<=0 / ReloadTimeSeconds<=0) → Log Error 拒绝
	 *
	 * @param InWeaponConfig  DT_WeaponInfo 行引用
	 * @param InWeaponRowName DT 行名 (用于错误日志)
	 */
	UFUNCTION(BlueprintCallable, Category = "Weapon|Fire")
	void InitializeFromWeaponConfig(const FWeaponInfo& InWeaponConfig, const FName& InWeaponRowName);

	// ==========================================
	// 开火控制 (服务器权威入口 — 玩家 RPC/AI BT 调用)
	// ==========================================

	/**
	 * 开始开火 (服务器)
	 *
	 * 逻辑:
	 *   - 半自动: 立即尝试打一发 (受 TimeSinceLastShot 冷却)
	 *   - 全自动: 启动 Tick 节流, 每 TimeBetweenShotsSeconds 秒打一发
	 *
	 * 拒绝条件 (零兜底):
	 *   - 弹药空 → Log Verbose + 自动触发 StartReload (如果备用有弹药)
	 *   - 换弹中 → Log Verbose (拒绝)
	 *   - 未初始化 (InitializeFromWeaponConfig 没调) → Log Error + 拒绝
	 */
	UFUNCTION(BlueprintCallable, Category = "Weapon|Fire")
	void StartFire();

	/**
	 * 停止开火 (服务器, 仅全自动有意义)
	 *
	 * 半自动: no-op (一打完就停)
	 * 全自动: 取消 Tick 节流, 不再继续打
	 */
	UFUNCTION(BlueprintCallable, Category = "Weapon|Fire")
	void StopFire();

	/**
	 * 开始换弹 (服务器)
	 *
	 * 逻辑:
	 *   - 从 ReserveAmmo 取 min(MagazineSize - CurrentAmmo, ReserveAmmo) 发子弹
	 *   - 设置 bIsReloading=true, 锁定 ReloadTimeSeconds 秒
	 *   - 锁定结束自动填入弹匣 + 清 bIsReloading + 广播 OnReloadStateChanged(false)
	 *
	 * 拒绝条件 (零兜底):
	 *   - 弹匣已满 → Log Verbose (拒绝, 不浪费动画)
	 *   - 备用弹药为 0 → Log Verbose (拒绝)
	 *   - 已在换弹中 → Log Verbose (拒绝, 幂等)
	 *   - 死亡/换武器中 → Log Verbose (拒绝)
	 */
	UFUNCTION(BlueprintCallable, Category = "Weapon|Fire")
	void StartReload();

	// ==========================================
	// 查询接口 (供 HUD / Controller 调用, BlueprintPure)
	// ==========================================

	UFUNCTION(BlueprintPure, Category = "Weapon|Fire")
	int32 GetCurrentAmmo() const { return CurrentAmmo; }

	UFUNCTION(BlueprintPure, Category = "Weapon|Fire")
	int32 GetMagazineSize() const { return MagazineSize; }

	UFUNCTION(BlueprintPure, Category = "Weapon|Fire")
	int32 GetReserveAmmo() const { return ReserveAmmo; }

	UFUNCTION(BlueprintPure, Category = "Weapon|Fire")
	bool IsReloading() const { return bIsReloading; }

	UFUNCTION(BlueprintPure, Category = "Weapon|Fire")
	bool IsFiring() const { return bIsFiring; }

	// ==========================================
	// 蒙太奇查询 (供 ABaseWeapon::Multicast_PlayXxxMontage_Implementation 调用)
	// ==========================================
	//
	// 大厂原则 - 封装边界:
	//   - 蒙太奇字段保持 protected (内部写, 外部只读)
	//   - ABaseWeapon 通过这些 getter 拿蒙太奇引用 (避免穿透到组件字段)

	UFUNCTION(BlueprintPure, Category = "Weapon|Fire")
	UAnimMontage* GetFireMontageHip() const { return FireMontageHip; }

	UFUNCTION(BlueprintPure, Category = "Weapon|Fire")
	UAnimMontage* GetReloadMontageHip() const { return ReloadMontageHip; }

	// ==========================================
	// 事件订阅 (供 HUD / 客户端 UI)
	// ==========================================

	UPROPERTY(BlueprintAssignable, Category = "Weapon|Fire")
	FOnAmmoChanged OnAmmoChanged;

	UPROPERTY(BlueprintAssignable, Category = "Weapon|Fire")
	FOnReloadStateChanged OnReloadStateChanged;

protected:
	// ==========================================
	// 配置真理源 (服务器初始化, Replicated 到客户端)
	// ==========================================

	/**
	 * 弹匣容量 — 真理源: FWeaponInfo::MagazineSize
	 */
	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "Weapon|Fire")
	int32 MagazineSize = 30;

	/**
	 * 备用弹药 — 真理源: FWeaponInfo::ReserveAmmo
	 */
	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "Weapon|Fire")
	int32 ReserveAmmo = 120;

	/**
	 * 射速 — 真理源: FWeaponInfo::FireRateRPM
	 */
	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "Weapon|Fire")
	float FireRateRPM = 600.0f;

	/**
	 * 换弹时间 — 真理源: FWeaponInfo::ReloadTimeSeconds
	 */
	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "Weapon|Fire")
	float ReloadTimeSeconds = 2.0f;

	// ==========================================
	// 运行时状态 (服务器权威, Replicated)
	// ==========================================

	/**
	 * 当前弹匣内弹药
	 * 真理源: 服务器写入, OnRep_CurrentAmmo 同步到客户端 HUD
	 */
	UPROPERTY(ReplicatedUsing = OnRep_CurrentAmmo, VisibleAnywhere, BlueprintReadOnly, Category = "Weapon|Fire")
	int32 CurrentAmmo = 30;

	/**
	 * 是否在换弹中
	 */
	UPROPERTY(ReplicatedUsing = OnRep_IsReloading, VisibleAnywhere, BlueprintReadOnly, Category = "Weapon|Fire")
	bool bIsReloading = false;

	/**
	 * 是否正在开火 (全自动模式用, 半自动无意义)
	 */
	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "Weapon|Fire")
	bool bIsFiring = false;

	// ==========================================
	// 内部状态 (服务器 only, 不复制)
	// ==========================================

	/**
	 * 上次开火时间戳 (秒, World->GetTimeSeconds)
	 * 用于射速节流: 当前时间 - LastFireTime >= TimeBetweenShotsSeconds 才允许下一发
	 */
	float LastFireTimeSeconds = -1000.0f;

	/**
	 * 换弹到期时间戳 (秒)
	 * World->GetTimeSeconds() >= ReloadEndTimeSeconds → 完成换弹
	 */
	float ReloadEndTimeSeconds = -1.0f;

	/**
	 * 换弹 Timer 句柄 (服务器到时清 bIsReloading + 填弹匣)
	 */
	FTimerHandle ReloadTimerHandle;

	/**
	 * 是否已初始化 (InitializeFromWeaponConfig 是否调过)
	 * 没初始化 → StartFire/StartReload 拒绝 + Log Error
	 */
	bool bIsInitialized = false;

	/**
	 * 缓存的 DT 行名 (用于错误日志)
	 */
	FName CachedWeaponRowName;

	// ==========================================
	// 蒙太奇缓存 (服务器写入, Multicast 给所有客户端播放)
	// ==========================================

	/**
	 * 腰射状态下的开火蒙太奇 (从 DT 读)
	 */
	UPROPERTY()
	TObjectPtr<UAnimMontage> FireMontageHip;

	/**
	 * 腰射状态下的换弹蒙太奇
	 */
	UPROPERTY()
	TObjectPtr<UAnimMontage> ReloadMontageHip;

	// ==========================================
	// 网络同步
	// ==========================================

	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	UFUNCTION()
	void OnRep_CurrentAmmo();

	UFUNCTION()
	void OnRep_IsReloading();

	// ==========================================
	// 内部方法 (服务器 only)
	// ==========================================

	/**
	 * 计算节流间隔: 60 / FireRateRPM
	 * 零兜底: FireRateRPM<=0 → 返回 -1 (StartFire 拒绝)
	 */
	float CalculateTimeBetweenShots() const;

	/**
	 * 实际打一发 (消耗弹药 + 启动 Trace + 播放蒙太奇)
	 * 零兜底: 弹药空/换弹中 → 拒绝
	 *
	 * 调用方:
	 *   - StartFire (半自动, 立即触发)
	 *   - TickComponent (全自动, 节流触发)
	 *
	 * 流程:
	 *   1. 校验弹药 / 换弹中
	 *   2. CurrentAmmo - 1
	 *   3. OnAmmoChanged.Broadcast (服务器本地)
	 *   4. 调 Weapon->GetDamageStrategy().StartTrace (启动 LineTrace)
	 *   5. 触发 Multicast_PlayFireMontage (走 ABaseWeapon RPC)
	 *   6. LastFireTimeSeconds = World->GetTimeSeconds()
	 */
	void PerformSingleShot();

	/**
	 * 实际完成换弹 (Timer 到期回调)
	 *   - 把 ReloadReserveGranted 发子弹从 ReserveAmmo 取, 填入 CurrentAmmo
	 *   - 清 bIsReloading
	 *   - 广播 OnReloadStateChanged(false)
	 */
	UFUNCTION()
	void OnReloadTimerExpired();

	/**
	 * 多播播放开火蒙太奇 (服务器 → 所有客户端)
	 * 通过 ABaseWeapon::Multicast_PlayFireMontage RPC 转发
	 */
	void MulticastPlayFireMontage();

	/**
	 * 多播播放换弹蒙太奇
	 * 通过 ABaseWeapon::Multicast_PlayReloadMontage RPC 转发
	 */
	void MulticastPlayReloadMontage();

	/**
	 * 解析 Owner Weapon (按需 GetOwner + Cast)
	 * 大厂原则: 不缓存, 与 v40.6 AIAttackComponent 同模式
	 */
	ABaseWeapon* ResolveOwnerWeapon() const;
};