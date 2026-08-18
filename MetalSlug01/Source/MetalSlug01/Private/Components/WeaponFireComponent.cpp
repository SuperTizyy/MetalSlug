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

#include "Characters/BaseCharacter.h"
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


/**
 * @brief BeginPlay — 武器开火组件初始化入口
 *
 * 当前实现仅调父类 (保留扩展点, 如订阅弹药事件等)
 */
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

	// v82 → v241: 蒙太奇必须 Replicated (客户端 Multicast_PlayFireMontage_Implementation 靠它播放)
	//   - v82 单值字段 FireMontageHip → v241 TMap<FName, UAnimMontage*> FireMontageHipByCharacter
	//   - Replicated TMap 自动按 Map 项同步 (UE 5.6 原生支持)
	DOREPLIFETIME(UWeaponFireComponent, FireMontageHipByCharacter);
	DOREPLIFETIME(UWeaponFireComponent, ReloadMontageHipByCharacter);
}


// ==========================================
// OnRep — 客户端响应 (HUD 订阅 OnAmmoChanged / OnReloadStateChanged)
// ==========================================

void UWeaponFireComponent::OnRep_CurrentAmmo()
{
	// 【v100 大厂架构 — 完整 3 参数】
	// 客户端 OnRep_CurrentAmmo 触发时, ReserveAmmo 字段值也已 Replicated 同步到本地
	// (服务器每次改 ReserveAmmo 都会触发一系列 Replicated 字段 Bunch 推送)
	// → 直接读 this->ReserveAmmo 拿最新值, 不需要额外 OnRep_ReserveAmmo

	// 【v208.8 大厂架构修复】bSuppressOnRepBroadcast 会被复制到客户端！
	//
	// 根因分析:
	//   1. 服务器 Server_RefillAmmo 设置 bSuppressOnRepBroadcast = true
	//   2. UE 网络复制将此标志复制到客户端（UPROPERTY() 默认复制）
	//   3. 客户端 OnRep_CurrentAmmo 触发时，bSuppressOnRepBroadcast = true
	//   4. v208.7 修复前: 检测到 true → return → 跳过广播 → HUD 不更新
	//   5. 结果: 第一次吃补给箱正常（bSuppressOnRepBroadcast=false），后续不工作
	//
	// 修复方案:
	//   - 检测到 bSuppressOnRepBroadcast = true 时，重置并继续广播
	//   - 因为 Server_RefillAmmo 末尾已重置为 false，客户端收到时应该也是 false
	//   - 但如果因网络延迟等原因客户端收到的是 true，我们仍然要广播
	//   - 这是大厂原则：旗帜是防递归的，不是防广播的
	if (bSuppressOnRepBroadcast)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[WeaponFireComponent] OnRep_CurrentAmmo: bSuppressOnRepBroadcast=true (从服务器复制). 重置并继续广播. 【v208.8 诊断】"));
		bSuppressOnRepBroadcast = false;
	}
	OnAmmoChanged.Broadcast(CurrentAmmo, MagazineSize, ReserveAmmo);
}


/**
 * @brief 客户端 bIsReloading 复制回调 — 收到服务器 Replicated 字段时广播
 *
 * 客户端专用, 服务器主动调 Multicast RPC. 触发 OnReloadStateChanged 委托,
 * 订阅方 (HUD) 据此显示换弹 UI/蒙太奇.
 */
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

	// 【v117 大厂架构新增】缓存"满值"快照 — 空投补给恢复时用
	//   - 玩家打空后 ReserveAmmo=0, 补给时必须从这里拿
	//   - 一次性写入, 后续不变 (DT 改了需要重启游戏, 这是用户决策)
	InitialReserveAmmo = InWeaponConfig.ReserveAmmo;

	// 【v241.1 大厂架构 — 按角色查表】缓存蒙太奇 (Replicated 自动同步到客户端 — v82 修复)
	//   - v241 (旧): TMap<FName, UAnimMontage*> — ❌ UE 5.6 不支持 Replicated TMap (编译报错)
	//   - v241.1 (新): TArray<FAnimMontageByCharacterEntry> — ✅ UE 5.6 Replicated TArray<FStruct> 支持
	//     → 服务器 InitializeFromWeaponConfig 从 DT 复制到本字段 (Replicated)
	//     → 客户端 OnRep 自动同步 → Multicast_PlayFireMontage_Implementation 遍历数组找到匹配
	//
	// 大厂原则 — 单一真理源:
	//   - DT_WeaponInfo.FireMontageHipByCharacter 是唯一真理源 (策划在 DT 编辑器里配)
	//   - 服务器 InitializeFromWeaponConfig 从 DT 复制到本字段 (Replicated)
	//   - 客户端 OnRep 自动同步 → Multicast_PlayFireMontage_Implementation 拿到正确 Montage
	//
	// 大厂原则 — 零兜底:
	//   - DT 数组配错 (空数组 / 缺某角色) → 后续 GetFireMontageHip 会 Log Error, 强制修复 DT
	//   - 这里不校验数组内容 (DT 配错由调用方显式报错, 这里只复制)
	FireMontageHipByCharacter = InWeaponConfig.FireMontageHipByCharacter;
	ReloadMontageHipByCharacter = InWeaponConfig.ReloadMontageHipByCharacter;

	// 初始化弹药 (满弹匣)
	CurrentAmmo = MagazineSize;
	bIsReloading = false;
	bIsFiring = false;

	bIsInitialized = true;

	UE_LOG(LogTemp, Log,
		TEXT("[WeaponFireComponent] InitializeFromWeaponConfig: WeaponRow=%s MagazineSize=%d ReserveAmmo=%d FireRate=%.0fRPM"),
		*InWeaponRowName.ToString(), MagazineSize, ReserveAmmo, FireRateRPM);

	// 立即广播一次 OnAmmoChanged (服务器本地, 让 HUD 立刻更新)
	// 【v100 大厂架构 — 完整 3 参数】换弹周期内所有 Broadcast 都传 ReserveAmmo
	OnAmmoChanged.Broadcast(CurrentAmmo, MagazineSize, ReserveAmmo);
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


/**
 * @brief 停止连射入口 — 由 InputAction 释放时调用
 *
 * 服务器权威: 客户端调用会短路 (避免本地状态与服务器冲突)
 * 副作用: bIsFiring=false + 关闭 Component Tick
 */
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
	// 【v100 大厂架构 — 完整 3 参数】换弹周期内所有 Broadcast 都传 ReserveAmmo
	OnAmmoChanged.Broadcast(CurrentAmmo, MagazineSize, ReserveAmmo);

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
	// 【v100 大厂架构 — 完整 3 参数】换弹完成的 ReserveAmmo 减小, 必须传完整 3 参数
	OnAmmoChanged.Broadcast(CurrentAmmo, MagazineSize, ReserveAmmo);
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
// 【v117 大厂架构新增】空投补给接口 — 服务器权威
// ==========================================

/**
 * UWeaponFireComponent::Server_RefillAmmo
 *
 * 空投补给入口 — 玩家被空投吃掉时调用, 把主武器所有弹药恢复到"满"
 *
 * 业务规则 (用户 2026.08.03):
 *   - CurrentAmmo = MagazineSize (弹匣满)
 *   - ReserveAmmo = InitialReserveAmmo (总弹药满 — DT 行的初值)
 *   - 强制清 bIsReloading (补给优先级最高)
 *
 * 大厂原则 — 真理源:
 *   - MagazineSize: DT 行的弹匣容量 (Replicated 真理源)
 *   - InitialReserveAmmo: DT 行的备用弹药初值 (服务器权威快照)
 *   - 不从运行时 ReserveAmmo 派生"满值" — 玩家打空后永远是 0
 *
 * 大厂原则 — 零兜底:
 *   - 未初始化 (bIsInitialized=false) → Log Error + return
 *   - 服务器权威 (HasAuthority=false) → Log Error + return (客户端不调, 防御)
 *   - 弹药已经是满的也允许调 (强制写, 幂等无副作用)
 *
 * 调用方:
 *   - AAirdropPickup::Handle_OverlapBegin (v117, 唯一入口)
 *
 * 网络同步:
 *   - CurrentAmmo Replicated → 客户端 OnRep_CurrentAmmo → 广播 OnAmmoChanged
 *   - ReserveAmmo Replicated → 客户端读最新值 (没有 OnRep_, 但 OnRep_CurrentAmmo 触发时已经同步)
 */
void UWeaponFireComponent::Server_RefillAmmo()
{
	// 客户端拒绝 — 这是服务器权威方法
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		UE_LOG(LogTemp, Error,
			TEXT("[WeaponFireComponent] Server_RefillAmmo: 客户端调用非法 (HasAuthority=false). 仅服务器可执行空投补给."));
		return;
	}

	// 零兜底: 未初始化拒绝 (InitializeFromWeaponConfig 没调 = DT 配错, 强制修复)
	if (!bIsInitialized)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[WeaponFireComponent] Server_RefillAmmo: WeaponRow=%s 未初始化 (InitializeFromWeaponConfig 没调). 拒绝补给. "
			     "【修复】检查 WeaponAttachmentComponent::SpawnAndEquipWeapon 是否调用 InitializeFromWeaponConfig."),
			*CachedWeaponRowName.ToString());
		return;
	}

	// 零兜底: DT 配错 (InitialReserveAmmo < 0) → Error, 不能"补给 0 颗"
	if (InitialReserveAmmo < 0 || MagazineSize <= 0)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[WeaponFireComponent] Server_RefillAmmo: WeaponRow=%s InitialReserveAmmo=%d 或 MagazineSize=%d 配置错. 拒绝补给. "
			     "【修复】DT_WeaponInfo.ReserveAmmo 必须 >=0, MagazineSize 必须 >=1."),
			*CachedWeaponRowName.ToString(), InitialReserveAmmo, MagazineSize);
		return;
	}

	// 大厂原则 — 补给覆盖一切状态: 即使在换弹中也强制补给 (补给优先级 > 换弹)
	bIsReloading = false;
	if (ReloadTimerHandle.IsValid())
	{
		GetWorld()->GetTimerManager().ClearTimer(ReloadTimerHandle);
		OnReloadStateChanged.Broadcast(false); // 同步本地状态给 HUD
	}

	// 写入真理源 (Replicated 自动同步到客户端)
	const int32 OldCurrentAmmo = CurrentAmmo;
	const int32 OldReserveAmmo = ReserveAmmo;

	// 【v208.7 大厂架构修复】bSuppressOnRepBroadcast 重置时机修正
	//
	// 根因 (v208.4-v208.6 遗留):
	//   1. Server_RefillAmmo 设置 bSuppressOnRepBroadcast = true
	//   2. 写入 CurrentAmmo = MagazineSize
	//   3. 如果值没变，UE 不触发 OnRep_CurrentAmmo
	//   4. bSuppressOnRepBroadcast 永远保持 true
	//   5. 后续 OnAmmoChanged.Broadcast 被 OnRep_CurrentAmmo 跳过
	//   6. 结果：第一次吃补给箱正常，后续吃补给箱总子弹数不更新
	//
	// 修复方案:
	//   - OnAmmoChanged.Broadcast 之后立即重置 bSuppressOnRepBroadcast
	//   - 不依赖 OnRep_CurrentAmmo 来重置（因为 OnRep 可能不触发）
	//   - 这是大厂原则：旗帜必须显式管理，不能依赖隐式行为
	bSuppressOnRepBroadcast = true;

	CurrentAmmo = MagazineSize;
	ReserveAmmo = InitialReserveAmmo;

	// 【v208.4 关键修复】强制 Broadcast (不依赖值变化)
	//   - UE 的 OnRep 只在值变化时触发, 如果之前已经是 MagazineSize, OnRep 不触发
	//   - 空投场景: 玩家弹药已满 → CurrentAmmo=30=MagazineSize → OnRep 不触发 → HUD 不更新
	//   - 强制广播保证每次补给都触发 HUD 更新 (无论弹药是否"看起来没变")
	OnAmmoChanged.Broadcast(CurrentAmmo, MagazineSize, ReserveAmmo);

	// 【v208.7 大厂架构修复】Broadcast 之后立即重置标志
	//   - 不依赖 OnRep_CurrentAmmo（因为 OnRep 只在值变化时触发）
	//   - 这是 v208.7 与 v208.4 的核心区别
	bSuppressOnRepBroadcast = false;

	// 【v208.5 大厂架构修复】必须调 Client_RefreshWeaponAmmo RPC 推送弹药数据给客户端
	//
	// 根因 (v208.4 遗留):
	//   v208.4 只加了 OnAmmoChanged.Broadcast() — 这只在服务器本地触发 (ListenServer HUD 更新)
	//   远端客户端的 HUD 订阅的是 CharacterIconComponent::OnWeaponAmmoChanged
	//     → 这个回调只在 OnRep_CurrentAmmo 或 Client_RefreshWeaponAmmo RPC 时被触发
	//   当 CurrentAmmo 值没变时:
	//     → OnRep_CurrentAmmo 不触发 (UE 只在值变化时调用 OnRep)
	//     → bSuppressOnRepBroadcast=true 阻止 OnAmmoChanged.Broadcast 触发 OnRep 递归
	//     → 远端客户端: OnRep 不触发 + Client_RefreshWeaponAmmo 没调 → 弹匣和总子弹数都不更新
	//
	// 修复: 补给后显式调 Client_RefreshWeaponAmmo RPC — 完全不依赖 OnRep 值变化
	//   - 走 ABaseCharacter::Client_RefreshWeaponAmmo (Actor 层 RPC)
	//   - WeaponFireComponent → Weapon → Character → Client_RefreshWeaponAmmo RPC
	//   - 客户端收到后: SetCachedWeaponAmmoInfo + OnWeaponAmmoInfoReady.Broadcast → HUD 更新
	//
	// 大厂原则 — 零兜底:
	//   - GetOwner() 返回 nullptr → Log Error + return (Weapon 不存在是异常)
	//   - GetAttachedCharacter() 返回 nullptr → Log Error + return (Character 不存在是异常)
	//   - 这两个检查保证: 调 RPC 时 Owner 链必然完整
	//
	// 【v209.1 大厂架构修复】根因分析:
	//   - GetOwner() 返回 Weapon Actor 自身 (WeaponFireComponent 挂在 Weapon 上)
	//   - Cast<ABaseCharacter>(WeaponOwner) 永远失败 (Weapon 不是 Character)
	//   - 日志显示: "Owner 不是 ABaseCharacter. Owner='BP_Weapon_AK47_C_1'"
	//   - 修复: 用 ABaseWeapon::GetAttachedCharacter() 获取挂载的 Character
	{
		ABaseWeapon* Weapon = Cast<ABaseWeapon>(GetOwner());
		if (!Weapon)
		{
			UE_LOG(LogTemp, Error,
				TEXT("[WeaponFireComponent] Server_RefillAmmo: GetOwner() 返回非 ABaseWeapon. Weapon='%s'. 拒绝 RPC 推送."),
				*GetName());
		}
		else if (ABaseCharacter* Character = Weapon->GetAttachedCharacter())
		{
			// 【v208.5 核心修复】RPC 推送给客户端 (弹匣+总子弹数都更新)
			Character->Client_RefreshWeaponAmmo(CurrentAmmo, MagazineSize, ReserveAmmo);
		}
		else
		{
			// 可能是 AI 武器等非 Character Owner (AI 用的是 Melee, 没有 WeaponFireComponent, 这里理论上不会跑到)
			UE_LOG(LogTemp, Warning,
				TEXT("[WeaponFireComponent] Server_RefillAmmo: GetAttachedCharacter() 返回 nullptr. Weapon='%s'. 跳过 RPC 推送."),
				*Weapon->GetName());
		}
	}

	UE_LOG(LogTemp, Display,
		TEXT("[WeaponFireComponent] ★ Server_RefillAmmo 弹药全满完成! "
		     "WeaponRow=%s Weapon='%s' "
		     "CurrentAmmo %d→%d / %d, ReserveAmmo %d→%d (InitialReserve=%d). "
		     "【v210.2 增强日志】已调 Client_RefreshWeaponAmmo RPC. "
		     "OldAmmo=(%d/%d) NewAmmo=(%d/%d)"),
		*CachedWeaponRowName.ToString(),
		*GetName(),
		OldCurrentAmmo, CurrentAmmo, MagazineSize,
		OldReserveAmmo, ReserveAmmo, InitialReserveAmmo,
		OldCurrentAmmo, OldReserveAmmo,
		CurrentAmmo, ReserveAmmo);
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


// ==========================================
// 【v241 大厂架构新增】按角色查表 — 蒙太奇选择器
// ==========================================

/**
 * ResolveAttachedCharacterRowName — 解析当前持有武器的角色的 RowName (FName)
 *
 * 真理源链:
 *   - 玩家: ARoomPlayerState::SelectedCharacterID (Replicated FString) → FName(*str)
 *   - AI:   UWeaponAttachmentComponent::CharacterID (Replicated FString) → FName(*str)
 *   - 母体: 复用玩家 SelectedCharacterID (母体死亡/复活流程由 RoomSpawnSubsystem 处理)
 *
 * 大厂原则 (v40.6 同模式):
 *   - 不缓存 — 缓存失效 = 隐性兜底 = 隐藏 bug
 *   - 每次按需 GetOwner/PlayerState/WeaponAttachmentComponent, 保证拿到最新值
 *
 * 零兜底:
 *   - Owner Weapon 无效 → return NAME_None
 *   - GetAttachedCharacter 返回 nullptr → return NAME_None
 *   - PlayerState 和 WeaponAttachment 都拿不到 CharacterID → return NAME_None
 *   - CharacterID 是空字符串 → return NAME_None
 *   - 不静默 fallback (如 "SWAT"), 强制调用方处理 NAME_None 情况
 *
 * @return 角色 RowName (FName), 失败返回 NAME_None
 */
FName UWeaponFireComponent::ResolveAttachedCharacterRowName() const
{
	// Step 1: 拿 Owner Weapon
	ABaseWeapon* Weapon = ResolveOwnerWeapon();
	if (!Weapon)
	{
		return NAME_None;
	}

	// Step 2: 拿挂载的 Character (Replicated Attach 关系, 服务器/客户端都能拿到)
	ABaseCharacter* Character = Weapon->GetAttachedCharacter();
	if (!Character)
	{
		return NAME_None;
	}

	// Step 3: 直接委托 Character 自己暴露 RowName (单一真理源封装)
	//   - 玩家路径: Character 内部从 PlayerState 读
	//   - AI 路径: Character 内部从 WeaponAttachmentComponent 读
	return Character->GetCharacterRowName();
}


/**
 * GetFireMontageHipByRow — 按显式 CharacterRowName 查数组
 *
 * 内部 helper — GetFireMontageHip() 调它查表
 * 也可以被外部显式调用 (蓝图测试/特殊场景)
 *
 * v241.1 实现说明:
 *   - 旧版 (v241): TMap.Find(CharacterRowName) O(1)
 *   - 新版 (v241.1): TArray 遍历 O(N), N 通常 < 10 (角色总数) — UE 5.6 硬限制的妥协
 *
 * 零兜底:
 *   - CharacterRowName 是 NAME_None → Log Error + return nullptr
 *   - 数组为空 → Log Error + return nullptr (DT 配错)
 *   - 数组没有匹配项 → Log Error + return nullptr (DT 配错, 强制策划补对应角色 Montage)
 *
 * @param CharacterRowName 角色 RowName (如 "SWAT", "CosmoBunnyGirl" — 跟 DT 数组 Key 一致)
 * @return 对应 Fire Montage, 失败返回 nullptr
 */
UAnimMontage* UWeaponFireComponent::GetFireMontageHipByRow(FName CharacterRowName) const
{
	// 零兜底: RowName 无效
	if (CharacterRowName.IsNone())
	{
		UE_LOG(LogTemp, Error,
			TEXT("[WeaponFireComponent] GetFireMontageHipByRow: CharacterRowName 是 NAME_None. "
			     "【修复】1) 检查 Owner Character 的 PlayerState->SelectedCharacterID 是否非空 "
			     "2) AI 路径检查 WeaponAttachmentComponent->CharacterID 是否写入 "
			     "3) BP_BaseCharacter 蓝图是否正确配 SpawnLoadout"));
		return nullptr;
	}

	// 零兜底: 数组为空
	if (FireMontageHipByCharacter.Num() == 0)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[WeaponFireComponent] GetFireMontageHipByRow: FireMontageHipByCharacter 数组为空. "
			     "WeaponRow=%s CharacterRowName=%s. "
			     "【修复】DT_WeaponInfo 行 '%s' 的 FireMontageHipByCharacter 数组必须按角色配 (CharacterRowName + Montage)."),
			*CachedWeaponRowName.ToString(),
			*CharacterRowName.ToString(),
			*CachedWeaponRowName.ToString());
		return nullptr;
	}

	// 遍历数组查找匹配 (v241.1 — UE 5.6 不支持 TMap Replicated, 改用 TArray<FStruct>)
	for (const FAnimMontageByCharacterEntry& Entry : FireMontageHipByCharacter)
	{
		if (Entry.CharacterRowName == CharacterRowName)
		{
			if (!Entry.Montage)
			{
				UE_LOG(LogTemp, Error,
					TEXT("[WeaponFireComponent] GetFireMontageHipByRow: 找到 CharacterRowName='%s' 项, 但 Montage 为空. "
					     "WeaponRow=%s. 【修复】DT_WeaponInfo 行 '%s' 给该角色配的 Montage 不能为空."),
					*CharacterRowName.ToString(),
					*CachedWeaponRowName.ToString(),
					*CachedWeaponRowName.ToString());
				return nullptr;
			}
			return Entry.Montage;
		}
	}

	// 零兜底: 数组没有该 RowName — 列出已配置的 Keys 帮助策划调试
	FString ConfiguredKeys;
	for (const FAnimMontageByCharacterEntry& Entry : FireMontageHipByCharacter)
	{
		if (!ConfiguredKeys.IsEmpty()) ConfiguredKeys += TEXT(",");
		ConfiguredKeys += Entry.CharacterRowName.ToString();
	}

	UE_LOG(LogTemp, Error,
		TEXT("[WeaponFireComponent] GetFireMontageHipByRow: 数组没有 CharacterRowName='%s' 的项. "
		     "WeaponRow=%s 数组已配置的 CharacterRowNames=[%s]. "
		     "【修复】DT_WeaponInfo 行 '%s' 的 FireMontageHipByCharacter 数组必须包含 CharacterRowName='%s' 的行. "
		     "如果该角色确实没有这个武器的动画, 请加一行 CharacterRowName='%s' + Montage=nullptr 而不是省略."),
		*CharacterRowName.ToString(),
		*CachedWeaponRowName.ToString(),
		*ConfiguredKeys,
		*CachedWeaponRowName.ToString(),
		*CharacterRowName.ToString(),
		*CharacterRowName.ToString());
	return nullptr;
}


/**
 * GetReloadMontageHipByRow — 换弹蒙太奇查表 (与 GetFireMontageHipByRow 同模式 — v241.1)
 *
 * @param CharacterRowName 角色 RowName
 * @return 对应 Reload Montage, 失败返回 nullptr
 */
UAnimMontage* UWeaponFireComponent::GetReloadMontageHipByRow(FName CharacterRowName) const
{
	// 零兜底: RowName 无效
	if (CharacterRowName.IsNone())
	{
		UE_LOG(LogTemp, Error,
			TEXT("[WeaponFireComponent] GetReloadMontageHipByRow: CharacterRowName 是 NAME_None. "
			     "【修复】检查 Owner Character 的 PlayerState->SelectedCharacterID / WeaponAttachmentComponent->CharacterID"));
		return nullptr;
	}

	// 零兜底: 数组为空
	if (ReloadMontageHipByCharacter.Num() == 0)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[WeaponFireComponent] GetReloadMontageHipByRow: ReloadMontageHipByCharacter 数组为空. "
			     "WeaponRow=%s CharacterRowName=%s. "
			     "【修复】DT_WeaponInfo 行 '%s' 的 ReloadMontageHipByCharacter 数组必须按角色配."),
			*CachedWeaponRowName.ToString(),
			*CharacterRowName.ToString(),
			*CachedWeaponRowName.ToString());
		return nullptr;
	}

	// 遍历数组查找匹配 (v241.1)
	for (const FAnimMontageByCharacterEntry& Entry : ReloadMontageHipByCharacter)
	{
		if (Entry.CharacterRowName == CharacterRowName)
		{
			if (!Entry.Montage)
			{
				UE_LOG(LogTemp, Error,
					TEXT("[WeaponFireComponent] GetReloadMontageHipByRow: 找到 CharacterRowName='%s' 项, 但 Montage 为空. "
					     "WeaponRow=%s. 【修复】DT_WeaponInfo 行 '%s' 给该角色配的 Montage 不能为空."),
					*CharacterRowName.ToString(),
					*CachedWeaponRowName.ToString(),
					*CachedWeaponRowName.ToString());
				return nullptr;
			}
			return Entry.Montage;
		}
	}

	// 零兜底: 数组没有该 RowName
	FString ConfiguredKeys;
	for (const FAnimMontageByCharacterEntry& Entry : ReloadMontageHipByCharacter)
	{
		if (!ConfiguredKeys.IsEmpty()) ConfiguredKeys += TEXT(",");
		ConfiguredKeys += Entry.CharacterRowName.ToString();
	}

	UE_LOG(LogTemp, Error,
		TEXT("[WeaponFireComponent] GetReloadMontageHipByRow: 数组没有 CharacterRowName='%s' 的项. "
		     "WeaponRow=%s 数组已配置的 CharacterRowNames=[%s]. "
		     "【修复】DT_WeaponInfo 行 '%s' 的 ReloadMontageHipByCharacter 数组必须包含 CharacterRowName='%s' 的行."),
		*CharacterRowName.ToString(),
		*CachedWeaponRowName.ToString(),
		*ConfiguredKeys,
		*CachedWeaponRowName.ToString(),
		*CharacterRowName.ToString());
	return nullptr;
}


/**
 * GetFireMontageHip — 蓝图/C++ 标准入口, 按 Owner Character 自动查表
 *
 * 调用方: ABaseWeapon::Multicast_PlayFireMontage_Implementation
 *
 * 大厂原则 — 调用方零感知:
 *   - 蓝图调用方代码不变 (BlueprintPure 无参)
 *   - 内部自动走 ResolveAttachedCharacterRowName + GetFireMontageHipByRow
 *   - 失败 Log Error + return nullptr (上层会跳过 Montage_Play)
 *
 * @return 对应当前持有者的 Fire Montage, 失败返回 nullptr
 */
UAnimMontage* UWeaponFireComponent::GetFireMontageHip() const
{
	const FName CharacterRowName = ResolveAttachedCharacterRowName();
	return GetFireMontageHipByRow(CharacterRowName);
}


/**
 * GetReloadMontageHip — 蓝图/C++ 标准入口, 按 Owner Character 自动查表
 *
 * 调用方: ABaseWeapon::Multicast_PlayReloadMontage_Implementation
 */
UAnimMontage* UWeaponFireComponent::GetReloadMontageHip() const
{
	const FName CharacterRowName = ResolveAttachedCharacterRowName();
	return GetReloadMontageHipByRow(CharacterRowName);
}

// ==========================================
// 【v200 大厂架构新增】弹药快照恢复
// ==========================================
bool UWeaponFireComponent::RestoreAmmoFromSnapshot(int32 InCurrentAmmo, int32 InReserveAmmo)
{
	ABaseWeapon* Weapon = ResolveOwnerWeapon();

	// 服务器权威检查
	if (!Weapon || !Weapon->HasAuthority())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[WeaponFireComponent] RestoreAmmoFromSnapshot: 客户端调用非法或 Owner 无效, 拒绝恢复弹药."));
		return false;
	}

	// 校验数据合理性
	if (InCurrentAmmo < 0)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[WeaponFireComponent] RestoreAmmoFromSnapshot: InCurrentAmmo < 0 (%d), 修正为 0."),
			InCurrentAmmo);
		InCurrentAmmo = 0;
	}

	if (InReserveAmmo < 0)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[WeaponFireComponent] RestoreAmmoFromSnapshot: InReserveAmmo < 0 (%d), 修正为 0."),
			InReserveAmmo);
		InReserveAmmo = 0;
	}

	// 恢复弹药数据 (Replicated 自动同步到客户端)
	const int32 OldCurrentAmmo = CurrentAmmo;
	const int32 OldReserveAmmo = ReserveAmmo;

	CurrentAmmo = InCurrentAmmo;
	ReserveAmmo = InReserveAmmo;

	// 广播变更
	OnAmmoChanged.Broadcast(CurrentAmmo, MagazineSize, ReserveAmmo);

	UE_LOG(LogTemp, Display,
		TEXT("[WeaponFireComponent] RestoreAmmoFromSnapshot: 武器 '%s' 弹药已恢复. CurrentAmmo %d→%d, ReserveAmmo %d→%d."),
		*Weapon->GetName(),
		OldCurrentAmmo, CurrentAmmo,
		OldReserveAmmo, ReserveAmmo);

	return true;
}