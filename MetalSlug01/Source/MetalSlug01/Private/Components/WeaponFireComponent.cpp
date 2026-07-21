// ==========================================
// UWeaponFireComponent 实现 (v60 大厂架构 — 统一射击逻辑)
// 
// 【核心设计】
//   射击频率完全由 FireRateRPM 控制
//   一把枪同时支持：按住扫射 + 快速连点
//
// 【射击机制】
//   - 按住连射: Triggered 每帧触发 → StartFire → 启动 TickComponent → 按 FireRateRPM 节流
//   - 快速连点: Started 触发一次 → StartFire → 检查冷却 → 立即射击
//   - 射击间隔: 60 / FireRateRPM 秒 (600 RPM = 0.1秒/发 = 每秒10发)
//
// 【冷却机制】
//   - 所有射击都受冷却保护，防止超过 FireRateRPM 的射击
//   - 冷却时间 = TimeBetweenShots = 60 / FireRateRPM
//   - 冷却中拒绝射击，冷却结束才允许
//
// 【零兜底】
//   - 没初始化 → 拒绝一切 (Log Error)
//   - FireRateRPM <= 0 → 拒绝 (Log Error)
//   - 弹药空 → 拒绝 + 触发换弹
// ==========================================

#include "Components/WeaponFireComponent.h"

#include "Weapons/BaseWeapon.h"
#include "Weapons/WeaponDamageStrategy.h"
#include "Data/Tables/WeaponTableRow.h"
#include "Animation/AnimInstance.h"
#include "Animation/AnimMontage.h"
#include "TimerManager.h"
#include "Engine/World.h"
#include "Net/UnrealNetwork.h"


UWeaponFireComponent::UWeaponFireComponent()
{
	// 默认不 Tick — 仅全自动模式才需要 (StartFire 内 SetComponentTickEnabled(true))
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;
	SetIsReplicatedByDefault(true);
}


void UWeaponFireComponent::BeginPlay()
{
	Super::BeginPlay();
}


void UWeaponFireComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 清理 Timer (防御型 — UE 自动清, 但显式清是工业级规范)
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(ReloadTimerHandle);
	}

	Super::EndPlay(EndPlayReason);
}


void UWeaponFireComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// 配置真理源 (MagazineSize/ReserveAmmo/FireRateRPM/ReloadTimeSeconds) — 复制给所有客户端
	DOREPLIFETIME(UWeaponFireComponent, MagazineSize);
	DOREPLIFETIME(UWeaponFireComponent, ReserveAmmo);
	DOREPLIFETIME(UWeaponFireComponent, FireRateRPM);
	DOREPLIFETIME(UWeaponFireComponent, ReloadTimeSeconds);

	// 运行时状态
	DOREPLIFETIME(UWeaponFireComponent, CurrentAmmo);
	DOREPLIFETIME(UWeaponFireComponent, bIsReloading);
	DOREPLIFETIME(UWeaponFireComponent, bIsFiring);

	// v82 修复: 蒙太奇必须 Replicated (客户端 Multicast_PlayFireMontage_Implementation 靠它播放)
	DOREPLIFETIME(UWeaponFireComponent, FireMontageHip);
	DOREPLIFETIME(UWeaponFireComponent, ReloadMontageHip);
}


// ==========================================
// OnRep — 客户端响应 (HUD 订阅 OnAmmoChanged / OnReloadStateChanged)
// ==========================================

void UWeaponFireComponent::OnRep_CurrentAmmo()
{
	OnAmmoChanged.Broadcast(CurrentAmmo, MagazineSize);
}


void UWeaponFireComponent::OnRep_IsReloading()
{
	OnReloadStateChanged.Broadcast(bIsReloading);
}


// ==========================================
// Tick — 按住连射节流
// ==========================================

void UWeaponFireComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// 客户端不跑 Tick (服务器权威)
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	// 未开火不 Tick
	if (!bIsFiring)
	{
		return;
	}

	// 换弹中不 Tick
	if (bIsReloading)
	{
		return;
	}

	// 节流: 当前时间 - 上次开火 >= 间隔 才打下一发
	const float TimeBetweenShots = CalculateTimeBetweenShots();
	if (TimeBetweenShots <= 0.0f)
	{
		// FireRateRPM 配错 (零兜底) — 已经在 CalculateTimeBetweenShots 内部 Log Error
		// 关闭 Tick 防无限报错
		SetComponentTickEnabled(false);
		return;
	}

	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const float Now = World->GetTimeSeconds();
	if (Now - LastFireTimeSeconds >= TimeBetweenShots)
	{
		// v82: 全自动节流用缓存的客户端射线 (大厂折中 — 锁定第一下的射线方向)
		PerformSingleShot(CachedClientRayOrigin, CachedClientRayDirection);
	}
}


// ==========================================
// 初始化
// ==========================================

void UWeaponFireComponent::InitializeFromWeaponConfig(const FWeaponInfo& InWeaponConfig, const FName& InWeaponRowName)
{
	// 客户端不初始化 (服务器权威)
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	CachedWeaponRowName = InWeaponRowName;

	// 零兜底: DT 字段校验
	if (InWeaponConfig.MagazineSize <= 0)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[WeaponFireComponent] InitializeFromWeaponConfig 失败: WeaponRow=%s MagazineSize<=0 (=%d). 修复: DT_WeaponInfo 中 MagazineSize 必须 >=1"),
			*InWeaponRowName.ToString(), InWeaponConfig.MagazineSize);
		return;
	}

	if (InWeaponConfig.FireRateRPM <= 0.0f)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[WeaponFireComponent] InitializeFromWeaponConfig 失败: WeaponRow=%s FireRateRPM<=0 (=%f). 修复: DT_WeaponInfo 中 FireRateRPM 必须 >=1"),
			*InWeaponRowName.ToString(), InWeaponConfig.FireRateRPM);
		return;
	}

	if (InWeaponConfig.ReloadTimeSeconds <= 0.0f)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[WeaponFireComponent] InitializeFromWeaponConfig 失败: WeaponRow=%s ReloadTimeSeconds<=0 (=%f). 修复: DT_WeaponInfo 中 ReloadTimeSeconds 必须 >=0.1"),
			*InWeaponRowName.ToString(), InWeaponConfig.ReloadTimeSeconds);
		return;
	}

	// 写入配置字段 (Replicated 自动同步到客户端)
	MagazineSize = InWeaponConfig.MagazineSize;
	ReserveAmmo = InWeaponConfig.ReserveAmmo;
	FireRateRPM = InWeaponConfig.FireRateRPM;
	ReloadTimeSeconds = InWeaponConfig.ReloadTimeSeconds;

	// 缓存蒙太奇 (Replicated 自动同步到客户端 — v82 修复)
	//   旧版 (v70-v81) 注释错误: "蒙太奇资源客户端直接从 BP 资产加载"
	//   实际: FireMontageHip 来源于 DT_WeaponInfo, 不是 BP 子对象默认属性
	//   → 客户端拿不到 → Multicast_PlayFireMontage 永远 "FireMontage 为空"
	//   新版: 字段 Replicated, 服务器写入后客户端自动同步
	FireMontageHip = InWeaponConfig.FireMontageHip;
	ReloadMontageHip = InWeaponConfig.ReloadMontageHip;

	// 初始化弹药 (满弹匣)
	CurrentAmmo = MagazineSize;
	bIsReloading = false;
	bIsFiring = false;

	bIsInitialized = true;

	UE_LOG(LogTemp, Log,
		TEXT("[WeaponFireComponent] InitializeFromWeaponConfig: WeaponRow=%s MagazineSize=%d ReserveAmmo=%d FireRate=%.0fRPM"),
		*InWeaponRowName.ToString(), MagazineSize, ReserveAmmo, FireRateRPM);

	// 立即广播一次 OnAmmoChanged (服务器本地, 让 HUD 立刻更新)
	OnAmmoChanged.Broadcast(CurrentAmmo, MagazineSize);
}


// ==========================================
// 开火控制
// ==========================================

void UWeaponFireComponent::StartFire(const FVector& ClientRayOrigin, const FVector& ClientRayDirection)
{
	ABaseWeapon* Weapon = ResolveOwnerWeapon();

	// 【v85.1 诊断】入口 Log — 确认 WeaponFireComponent::StartFire 是否被调用
	const FString WeaponName = Weapon ? *Weapon->GetName() : TEXT("(null)");
	UE_LOG(LogTemp, Warning,
		TEXT("[WeaponFireComponent] StartFire ENTER. Weapon=%s bIsInitialized=%d Auth=%d"),
		*WeaponName,
		bIsInitialized ? 1 : 0,
		(Weapon && Weapon->HasAuthority()) ? 1 : 0);

	if (!Weapon)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[WeaponFireComponent] StartFire: ResolveOwnerWeapon 失败 (Component 没挂在 ABaseWeapon 上). 修复: 把 UWeaponFireComponent 挂在武器 BP 子对象."));
		return;
	}

	// 零兜底: 没初始化 → 拒绝 (强制修复 DT 配置)
	if (!bIsInitialized)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[WeaponFireComponent] StartFire: WeaponRow=%s 未初始化 (InitializeFromWeaponConfig 没调). 修复: 检查武器 Spawn 链路是否调用 InitializeFromWeaponConfig."),
			*CachedWeaponRowName.ToString());
		return;
	}

	// 服务器权威
	if (!Weapon->HasAuthority())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[WeaponFireComponent] StartFire: 客户端直接调用 StartFire, 拒绝. 必须通过 ABaseWeapon::StartFire Server RPC 转发."));
		return;
	}

	// 换弹中 → 拒绝 (业务正常)
	if (bIsReloading)
	{
		UE_LOG(LogTemp, Verbose,
			TEXT("[WeaponFireComponent] StartFire: WeaponRow=%s 换弹中, 拒绝开火."),
			*CachedWeaponRowName.ToString());
		return;
	}

	// 弹药空 → 拒绝 + 触发自动换弹 (如果有备用弹药)
	if (CurrentAmmo <= 0)
	{
		UE_LOG(LogTemp, Verbose,
			TEXT("[WeaponFireComponent] StartFire: WeaponRow=%s 弹匣空 (CurrentAmmo=%d), 拒绝开火. 尝试自动换弹..."),
			*CachedWeaponRowName.ToString(), CurrentAmmo);

		if (ReserveAmmo > 0)
		{
			StartReload();
		}
		return;
	}

	// 统一射击逻辑: 所有射击都受冷却保护
	//
	// 【按住连射路径】
	//   - Triggered 每帧触发 → StartFire → bIsFiring=true → 启动 TickComponent
	//   - TickComponent 按 FireRateRPM 节流射击
	//
	// 【快速连点路径】
	//   - Started 触发一次 → StartFire → 检查冷却 → 立即射击
	//   - 不启动 TickComponent，按住也不会连续射击
	const float TimeBetweenShots = CalculateTimeBetweenShots();
	if (TimeBetweenShots <= 0.0f)
	{
		// FireRateRPM<=0 — 已经在 CalculateTimeBetweenShots 内部 Log Error
		return;
	}

	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const float Now = World->GetTimeSeconds();
	const float TimeSinceLastFire = Now - LastFireTimeSeconds;

	// v82 大厂架构修复: 缓存客户端射线 (全自动连射时 TickComponent 用此缓存)
	//   - 玩家按第一下: 客户端 HUD Crosshair 算射线 → RPC 传给服务器
	//   - 服务器缓存到 CachedClientRayOrigin/Direction
	//   - 全自动 Tick 触发时, 用缓存射线 (而不是服务器侧 PC->GetViewportSize — 远端玩家无效)
	//   - 半自动: 只有一次 PerformSingleShot, 用入参射线 (就是缓存值)
	//   - 大厂折中: 全自动射线方向锁定在开火第一下, 与 CS:GO / Valorant 一致
	if (!ClientRayOrigin.IsNearlyZero())
	{
		CachedClientRayOrigin = ClientRayOrigin;
		CachedClientRayDirection = ClientRayDirection;
		UE_LOG(LogTemp, Verbose,
			TEXT("[WeaponFireComponent] StartFire: 缓存客户端射线 — Origin=%s Dir=%s (全自动 Tick 用此缓存)"),
			*ClientRayOrigin.ToCompactString(),
			*ClientRayDirection.ToCompactString());
	}
	else if (CachedClientRayOrigin.IsNearlyZero())
	{
		// 没客户端射线, 也没缓存 — AI 路径 (Strategy 内部 fallback BaseAimRotation)
		UE_LOG(LogTemp, Verbose,
			TEXT("[WeaponFireComponent] StartFire: 无客户端射线, 走 AI fallback 路径 (Strategy 内部 BaseAimRotation)"));
	}

	// 冷却检查
	if (TimeSinceLastFire >= TimeBetweenShots)
	{
		// 冷却已过，立即射击 — 用缓存的客户端射线 (或入参)
		PerformSingleShot(CachedClientRayOrigin, CachedClientRayDirection);

		// 启动按住连射: 开启 TickComponent
		if (!bIsFiring)
		{
			bIsFiring = true;
			SetComponentTickEnabled(true);
		}
	}
	else
	{
		// 冷却中 — 仍然启动 TickComponent，让 TickComponent 在冷却结束后射击
		if (!bIsFiring)
		{
			bIsFiring = true;
			SetComponentTickEnabled(true);
		}
	}
}


void UWeaponFireComponent::StopFire()
{
	ABaseWeapon* Weapon = ResolveOwnerWeapon();
	if (!Weapon || !Weapon->HasAuthority())
	{
		return;
	}

	// 停止按住连射
	if (bIsFiring)
	{
		bIsFiring = false;
		SetComponentTickEnabled(false);
	}
}


void UWeaponFireComponent::StartReload()
{
	ABaseWeapon* Weapon = ResolveOwnerWeapon();
	if (!Weapon || !Weapon->HasAuthority())
	{
		return;
	}

	// 零兜底: 没初始化 → 拒绝
	if (!bIsInitialized)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[WeaponFireComponent] StartReload: WeaponRow=%s 未初始化 (InitializeFromWeaponConfig 没调). 修复: 检查武器 Spawn 链路是否调用 InitializeFromWeaponConfig."),
			*CachedWeaponRowName.ToString());
		return;
	}

	// 幂等: 已在换弹中 → 拒绝
	if (bIsReloading)
	{
		UE_LOG(LogTemp, Verbose,
			TEXT("[WeaponFireComponent] StartReload: WeaponRow=%s 已在换弹中, 拒绝 (幂等)."),
			*CachedWeaponRowName.ToString());
		return;
	}

	// 弹匣已满 → 拒绝
	if (CurrentAmmo >= MagazineSize)
	{
		UE_LOG(LogTemp, Verbose,
			TEXT("[WeaponFireComponent] StartReload: WeaponRow=%s 弹匣已满 (CurrentAmmo=%d >= MagazineSize=%d), 拒绝换弹."),
			*CachedWeaponRowName.ToString(), CurrentAmmo, MagazineSize);
		return;
	}

	// 备用弹药为 0 → 拒绝
	if (ReserveAmmo <= 0)
	{
		UE_LOG(LogTemp, Verbose,
			TEXT("[WeaponFireComponent] StartReload: WeaponRow=%s 备用弹药为 0, 拒绝换弹."),
			*CachedWeaponRowName.ToString());
		return;
	}

	// 启动换弹状态机
	bIsReloading = true;

	const UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const float Now = World->GetTimeSeconds();
	ReloadEndTimeSeconds = Now + ReloadTimeSeconds;

	// 服务器立即广播 (本地)
	OnReloadStateChanged.Broadcast(true);

	// 启动 Timer 到期自动填弹匣
	World->GetTimerManager().SetTimer(
		ReloadTimerHandle,
		this,
		&UWeaponFireComponent::OnReloadTimerExpired,
		ReloadTimeSeconds,
		false);

	// 多播播放换弹蒙太奇 (服务器 → 所有客户端)
	MulticastPlayReloadMontage();

	UE_LOG(LogTemp, Log,
		TEXT("[WeaponFireComponent] StartReload: WeaponRow=%s 启动换弹 (%.1fs). CurrentAmmo=%d/%d ReserveAmmo=%d"),
		*CachedWeaponRowName.ToString(), ReloadTimeSeconds, CurrentAmmo, MagazineSize, ReserveAmmo);
}


// ==========================================
// 内部: 实际打一发
// ==========================================

void UWeaponFireComponent::PerformSingleShot(const FVector& ClientRayOrigin, const FVector& ClientRayDirection)
{
	ABaseWeapon* Weapon = ResolveOwnerWeapon();

	// 【v85.1 诊断】PerformSingleShot 入口 Log — 确认是否执行到这里
	UE_LOG(LogTemp, Warning,
		TEXT("[WeaponFireComponent] PerformSingleShot ENTER. Weapon=%s CurrentAmmo=%d/%d"),
		Weapon ? *Weapon->GetName() : TEXT("(null)"),
		CurrentAmmo,
		MagazineSize);

	if (!Weapon)
	{
		return;
	}

	// 零兜底: 弹药不足 (双重校验)
	if (CurrentAmmo <= 0)
	{
		UE_LOG(LogTemp, Verbose,
			TEXT("[WeaponFireComponent] PerformSingleShot: WeaponRow=%s CurrentAmmo=0, 拒绝 (冗余校验)."),
			*CachedWeaponRowName.ToString());
		return;
	}

	// 【v60.3 大厂重构】启动 Trace — 立即执行 (Ranged) 或 初始化 (Melee)
	//   - 旧 (v60-v60.2) 反模式: StartTrace 只设 bIsActive, 等下一帧 BaseWeapon::Tick → TickDetection → 射线
	//     → 半自动按下 = 1 帧延迟, 全自动 StopTrace 时序错位
	//   - 新 (v60.3): Ranged 立即执行, Melee 初始化跨帧状态
	//   - 返回 false = 配置错 (Mesh 失效 / Socket 缺失), 必须回滚弹药
	TScriptInterface<IWeaponDamageStrategy> Strategy = Weapon->GetDamageStrategy();
	if (!Strategy || !Strategy.GetInterface())
	{
		UE_LOG(LogTemp, Error,
			TEXT("[WeaponFireComponent] PerformSingleShot: Weapon=%s DamageStrategy 未注入. 修复: 检查 WeaponAttachmentComponent.SpawnAndEquipWeapon 是否调用 SetDamageStrategy."),
			*Weapon->GetName());

		// 零兜底: 配置错必须显式, 不能"扣了弹药继续"或"静默跳过"
		// 决策: 不消耗弹药 (因为没打出), 拒绝播放蒙太奇, 等下次开火再试
		return;
	}

	// 大厂原则 — 配置错必须显式化, 不能扣弹药继续做事 (掩盖配置错)
	// StartTrace 返回 false = 配置错 (Mesh 失效 / Muzzle Socket 缺失 / 无 Owner)
	//
	// v82: 把客户端射线参数传给 Strategy (Ranged 用, Melee 忽略)
	const bool bExecuted = Strategy.GetInterface()->StartTrace(Weapon, /*bIsHeavy=*/false,
		ClientRayOrigin, ClientRayDirection);
	if (!bExecuted)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[WeaponFireComponent] PerformSingleShot: Weapon=%s StartTrace 失败 (配置错). 弹药未消耗 — 修复: 检查武器 Mesh/Socket 配置."),
			*Weapon->GetName());

		// 弹药不消耗, 等配置修复后下次开火再试 (大厂原则: 永远不允许静默"扣弹药 + 没射线")
		return;
	}

	// 【v60.3 P0 修复】StartTrace 成功后才消耗弹药 (顺序敏感!)
	//   - 旧 (v60-v60.2) 反模式: 先扣弹药再 StartTrace, 失败时弹药已扣
	//   - 新: 先 StartTrace, 成功才扣弹药 (失败时弹药完整保留)
	CurrentAmmo -= 1;

	// 更新 LastFireTime (节流用)
	const UWorld* World = GetWorld();
	if (World)
	{
		LastFireTimeSeconds = World->GetTimeSeconds();
	}

	// 广播 (服务器本地)
	OnAmmoChanged.Broadcast(CurrentAmmo, MagazineSize);

	// 多播播放开火蒙太奇 (服务器 → 所有客户端)
	MulticastPlayFireMontage();

	UE_LOG(LogTemp, Log,
		TEXT("[WeaponFireComponent] PerformSingleShot: WeaponRow=%s 开火. CurrentAmmo=%d/%d ReserveAmmo=%d"),
		*CachedWeaponRowName.ToString(), CurrentAmmo, MagazineSize, ReserveAmmo);

	// 弹药耗尽 → 自动触发换弹
	if (CurrentAmmo <= 0 && ReserveAmmo > 0 && !bIsReloading)
	{
		UE_LOG(LogTemp, Log,
			TEXT("[WeaponFireComponent] PerformSingleShot: WeaponRow=%s 弹匣耗尽, 自动触发换弹."),
			*CachedWeaponRowName.ToString());
		StartReload();
	}
}


// ==========================================
// 内部: 换弹 Timer 到期回调
// ==========================================

void UWeaponFireComponent::OnReloadTimerExpired()
{
	ABaseWeapon* Weapon = ResolveOwnerWeapon();
	if (!Weapon || !Weapon->HasAuthority())
	{
		return;
	}

	if (!bIsReloading)
	{
		// 防御: Timer 触发但状态已被外部清 (例如换武器)
		return;
	}

	// 计算要从 ReserveAmmo 取多少发
	const int32 AmmoNeeded = MagazineSize - CurrentAmmo;
	const int32 AmmoToLoad = FMath::Min(AmmoNeeded, ReserveAmmo);

	CurrentAmmo += AmmoToLoad;
	ReserveAmmo -= AmmoToLoad;

	// 清换弹状态
	bIsReloading = false;

	// 广播 (服务器本地)
	OnAmmoChanged.Broadcast(CurrentAmmo, MagazineSize);
	OnReloadStateChanged.Broadcast(false);

	UE_LOG(LogTemp, Log,
		TEXT("[WeaponFireComponent] OnReloadTimerExpired: WeaponRow=%s 换弹完成. CurrentAmmo=%d/%d ReserveAmmo=%d (本次填入 %d 发)"),
		*CachedWeaponRowName.ToString(), CurrentAmmo, MagazineSize, ReserveAmmo, AmmoToLoad);
}


// ==========================================
// 内部: 多播蒙太奇
// ==========================================

void UWeaponFireComponent::MulticastPlayFireMontage()
{
	ABaseWeapon* Weapon = ResolveOwnerWeapon();
	if (!Weapon)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[WeaponFireComponent] MulticastPlayFireMontage: ResolveOwnerWeapon 失败. 武器=%s. 修复: 把 UWeaponFireComponent 挂在武器 BP 子对象."),
			*GetName());
		return;
	}

	// 仅服务器发起 Multicast
	if (!Weapon->HasAuthority())
	{
		return;
	}

	// 【调试日志】确认蒙太奇是否已加载
	UAnimMontage* FireMontage = GetFireMontageHip();
	UE_LOG(LogTemp, Log,
		TEXT("[WeaponFireComponent] MulticastPlayFireMontage: 武器=%s FireMontage=%s"),
		*Weapon->GetName(),
		FireMontage ? *FireMontage->GetName() : TEXT("nullptr"));

	// 通过 ABaseWeapon::Multicast_PlayFireMontage RPC 转发
	// (RPC 必须在 Actor 上, Component 不能直接发 — 与 v40.1 头像修复同模式)
	Weapon->Multicast_PlayFireMontage();
}


void UWeaponFireComponent::MulticastPlayReloadMontage()
{
	ABaseWeapon* Weapon = ResolveOwnerWeapon();
	if (!Weapon)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[WeaponFireComponent] MulticastPlayReloadMontage: ResolveOwnerWeapon 失败. 武器=%s."),
			*GetName());
		return;
	}

	if (!Weapon->HasAuthority())
	{
		return;
	}

	// 【调试日志】确认蒙太奇是否已加载
	UAnimMontage* ReloadMontage = GetReloadMontageHip();
	UE_LOG(LogTemp, Log,
		TEXT("[WeaponFireComponent] MulticastPlayReloadMontage: 武器=%s ReloadMontage=%s"),
		*Weapon->GetName(),
		ReloadMontage ? *ReloadMontage->GetName() : TEXT("nullptr"));

	Weapon->Multicast_PlayReloadMontage();
}


// ==========================================
// 内部: 工具方法
// ==========================================

float UWeaponFireComponent::CalculateTimeBetweenShots() const
{
	if (FireRateRPM <= 0.0f)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[WeaponFireComponent] CalculateTimeBetweenShots: WeaponRow=%s FireRateRPM<=0 (=%f). 修复: DT_WeaponInfo 配置错误."),
			*CachedWeaponRowName.ToString(), FireRateRPM);
		return -1.0f;
	}

	return 60.0f / FireRateRPM;
}


ABaseWeapon* UWeaponFireComponent::ResolveOwnerWeapon() const
{
	// 大厂原则 (v40.6 同模式): 不缓存, 按需 GetOwner + Cast
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return nullptr;
	}

	ABaseWeapon* Weapon = Cast<ABaseWeapon>(Owner);
	if (!Weapon)
	{
		// 配置错 (Component 没挂在武器上)
		return nullptr;
	}

	return Weapon;
}