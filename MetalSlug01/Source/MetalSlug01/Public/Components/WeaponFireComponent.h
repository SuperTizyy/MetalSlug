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
//
// 【v100 大厂架构 — 完整 3 参数】弹药全状态同步
//   旧 (v85-v99) 委托签名 = (NewAmmo, MagazineSize) 故意不传 ReserveAmmo
//   → 客户端 CharacterIconComponent 用缓存里的 "OldReserveAmmo" 兜底 (反模式)
//   → 换弹时 ReserveAmmo 减小, HUD 总子弹不变
//   新 (v100) 委托签名 = (NewAmmo, MagazineSize, ReserveAmmo) — 完整弹药状态
//   → 客户端/服务器本地都拿到完整真理源, 删兜底
//   → 真理源在 WeaponFireComponent 字段 (Replicated), 委托直接广播
//   → 替换 CallSite 4 处: OnRep_CurrentAmmo / InitializeFromWeaponConfig / PerformSingleShot / OnReloadTimerExpired
DECLARE_DYNAMIC_MULTICAST_DELEGATE_ThreeParams(FOnAmmoChanged, int32, NewAmmo, int32, MagazineSize, int32, ReserveAmmo);
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
	 *
	 * v82 客户端射线参数:
	 *   - 不传参数 = AI 路径 (Strategy 内部 fallback BaseAimRotation)
	 *   - 玩家 RPC 调用: 传入 HUD Crosshair 算出的 Origin + Direction
	 */
	UFUNCTION(BlueprintCallable, Category = "Weapon|Fire")
	void StartFire(const FVector& ClientRayOrigin = FVector::ZeroVector,
		const FVector& ClientRayDirection = FVector::ForwardVector);

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
	// 【v117 大厂架构新增】空投补给接口
	// ==========================================
	//
	// 业务规则 (用户 2026.08.03):
	//   - 生化模式空投被人类吃掉时, 主武器所有弹药恢复全满
	//   - 弹匣 = MagazineSize (DT 配置), 总弹药 = InitialReserveAmmo (DT 配置的初始值)
	//
	// 大厂原则 — 单一真理源:
	//   - MagazineSize 是 Replicated 真理源 (InitializeFromWeaponConfig 写入)
	//   - InitialReserveAmmo 是 DT 行"满值"快照 (不复制, 服务器权威)
	//   - ReserveAmmo 是运行时值 (Replicated, 玩家实际剩余)
	//
	// 大厂原则 — 弹药全满语义:
	//   - 玩家打空后 ReserveAmmo=0 → 不能"恢复到 ReserveAmmo" (永远是 0)
	//   - 必须从 InitialReserveAmmo (DT 行的初值) 恢复, 不是从 ReserveAmmo 衍生
	//   - 这是大厂原则 — "满" 永远是配置真理源, 不是运行时衍生
	//
	// 大厂原则 — 弹药补给不是换弹:
	//   - 强制清 bIsReloading (补给时不可能在换弹中, 但保险清掉, 防状态错乱)
	//   - 强制 CurrentAmmo = MagazineSize (不管之前是 0 还是一半)
	//   - 强制 ReserveAmmo = InitialReserveAmmo (不管之前是 0 还是一半)
	//   - 广播 OnAmmoChanged (服务器本地, 客户端 OnRep 也会广播)
	//
	// 调用方: AAirdropPickup::Handle_OverlapBegin (v117)
	//
	// 零兜底:
	//   - 未初始化 (InitializeFromWeaponConfig 没调) → Log Error + return
	//   - 重复调用幂等 (强制写, 不重复写也无所谓, 不会有副作用)
	//   - 不检查 bIsReloading (补给优先级最高, 覆盖换弹状态)
	UFUNCTION(BlueprintCallable, Category = "Weapon|Fire")
	void Server_RefillAmmo();

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
	// 【v200 大厂架构新增】弹药快照 — 供 WeaponDropComponent 掉落/捡起使用
	// ==========================================

	/**
	 * 【v200 大厂架构新增】获取弹药快照 (掉落武器时保存)
	 *
	 * 调用方: UWeaponDropComponent::SaveAmmoSnapshot
	 *
	 * 大厂原则 — 单一真理源:
	 *   - 返回当前弹药数据的只读快照
	 *   - WeaponDropComponent 用此快照保存掉落时刻的弹药
	 *
	 * @return 当前弹药快照 (CurrentAmmo, ReserveAmmo, MagazineSize)
	 */
	UFUNCTION(BlueprintPure, Category = "Weapon|Fire")
	void GetAmmoSnapshotForDrop(int32& OutCurrentAmmo, int32& OutReserveAmmo, int32& OutMagazineSize) const
	{
		OutCurrentAmmo = CurrentAmmo;
		OutReserveAmmo = ReserveAmmo;
		OutMagazineSize = MagazineSize;
	}

	/**
	 * 【v200 大厂架构新增】从快照恢复弹药 (捡起武器时调用)
	 *
	 * 调用方: UWeaponDropComponent::RestoreAmmoSnapshot
	 *
	 * 业务场景:
	 *   - 武器从掉落状态被捡起时, 恢复掉落时刻的弹药数据
	 *   - 弹药快照保存在 WeaponDropComponent 中
	 *
	 * 大厂原则 — 服务器权威:
	 *   - 仅服务器可调用 (HasAuthority 检查)
	 *   - 客户端调用会被拒绝 + Log Warning
	 *
	 * @param InCurrentAmmo 弹匣内子弹数
	 * @param InReserveAmmo 备用弹药数
	 * @return true=成功恢复, false=被拒绝 (权限不足)
	 */
	UFUNCTION(BlueprintCallable, Category = "Weapon|Fire")
	bool RestoreAmmoFromSnapshot(int32 InCurrentAmmo, int32 InReserveAmmo);

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
	 * 【v117 大厂架构新增】备用弹药初始值 (DT 配置的"满值")
	 *
	 * 大厂原则 — 单一真理源 + 防止循环依赖:
	 *   - 初始化时由 InitializeFromWeaponConfig 写入, 后续不变
	 *   - 空投 Server_RefillAmmo 把 ReserveAmmo 恢复到 InitialReserveAmmo
	 *   - 不复制: 客户端不需要 (HUD 只显示当前 ReserveAmmo)
	 *
	 * 为什么需要:
	 *   - 玩家打空全部弹药后 ReserveAmmo=0, 不能"恢复到 ReserveAmmo" (永远是 0)
	 *   - 真理源必须是 DT 行的初始值, 而不是运行时变量
	 *   - 这是 v117 大厂原则 — 弹药"全满"的语义必须从真理源拿, 不能从运行时衍生
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Weapon|Fire")
	int32 InitialReserveAmmo = 120;

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
	 * 【v208.4 大厂架构新增】防止 OnRep_CurrentAmmo 递归广播的标志
	 *
	 * 业务场景: Server_RefillAmmo 需要强制广播 OnAmmoChanged (因为值可能没变),
	 *           但 OnAmmoChanged 回调里可能触发再次设置 CurrentAmmo,
	 *           又触发 OnRep_CurrentAmmo → 无限递归
	 *
	 * 大厂原则 — 零递归:
	 *   - Server_RefillAmmo 写入值前设 bSuppressOnRepBroadcast=true
	 *   - OnRep_CurrentAmmo 检测到标志 → 跳过广播, 清除标志
	 *   - 防止 Server_RefillAmmo → Broadcast → 某处回调 → 设置 CurrentAmmo → OnRep → 递归
	 */
	UPROPERTY()
	bool bSuppressOnRepBroadcast = false;

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
	 * 【v82 大厂架构修复】客户端射线缓存 — 服务器 Tick 全自动连射时复用
	 *
	 * 用途:
	 *   - 玩家按第一下: OnFirePressed 计算 HUD Crosshair 射线 → RPC 传到服务器
	 *   - 服务器 StartFire 把射线存入此缓存 → 立即 PerformSingleShot 用这个射线
	 *   - 服务器 Tick 全自动节流: PerformSingleShot() 无参数, 用此缓存射线 (不再依赖 Viewport)
	 *
	 * 折中 (大厂标准 — CS:GO / Valorant 都有类似机制):
	 *   - 全自动射线方向"锁定"到开火第一下的方向
	 *   - 玩家移动/转向时, 后续子弹射线起点不会更新 (仍按开火瞬间的位置)
	 *   - 准星实时移动时, 全自动连射的子弹是"扇形扫射" 而不是"精确跟随" — 这是 FPS 行业标准
	 */
	UPROPERTY()
	FVector CachedClientRayOrigin = FVector::ZeroVector;

	UPROPERTY()
	FVector CachedClientRayDirection = FVector(1.f, 0.f, 0.f);

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
	// 蒙太奇缓存 (服务器写入, Replicated 给所有客户端 — v82 修复)
	// ==========================================

	/**
	 * 腰射状态下的开火蒙太奇 (从 DT 读)
	 *
	 * 【v82 客户端武器可用性修复】Replicated:
	 *   - 旧版 (v70-v81) 注释错误: "不复制, 蒙太奇资源客户端直接从 BP 资产加载"
	 *   - 实际: DT_WeaponInfo 中的 FireMontage_Ironsights 是策划在 DataTable 配的字段,
	 *           不是 BP 子对象的默认属性, 客户端不会自动加载
	 *   - 客户端 Multicast_PlayFireMontage_Implementation 调 GetFireMontageHip() → nullptr
	 *   - → Multicast_PlayFireMontage 全部 Log "FireMontage 为空"
	 *   - → 远端玩家开火 fire 动画永远不播
	 *
	 *   - 新版 (v82): FireMontageHip Replicated → 服务器写入 → 客户端自动同步
	 *   - 客户端 Multicast_PlayFireMontage_Implementation 拿到正确 FireMontage → 播放
	 */
	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "Weapon|Fire")
	TObjectPtr<UAnimMontage> FireMontageHip;

	/**
	 * 腰射状态下的换弹蒙太奇 (与 FireMontageHip 同模式 — v82 修复)
	 */
	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "Weapon|Fire")
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
	 *
	 * v82 大厂架构修复 — 客户端射线参数:
	 *   - 旧 (v70-v81): PerformSingleShot 无参数, Strategy.StartTrace 内部用 PC->GetViewportSize/Deproject
	 *     → 服务器对远端玩家 PC 调 GetViewportSize 返回 0,0 → Deproject 失败 → trace 永远失败
	 *   - 新 (v82): PerformSingleShot 接收 ClientRayOrigin/Direction 参数, 透传到 Strategy.StartTrace
	 *     → 客户端玩家路径用 HUD Crosshair 算出射线 → RPC 传给服务器 → 服务器用客户端射线做权威 trace
	 *     → 兼容 AI 路径: 不传参数 = AI fallback (Strategy 内部用 BaseAimRotation)
	 *
	 * @param ClientRayOrigin     客户端射线起点 (默认 ZeroVector = AI/无客户端射线)
	 * @param ClientRayDirection  客户端射线方向 (默认 ForwardVector = AI)
	 */
	void PerformSingleShot(const FVector& ClientRayOrigin = FVector::ZeroVector,
		const FVector& ClientRayDirection = FVector::ForwardVector);

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