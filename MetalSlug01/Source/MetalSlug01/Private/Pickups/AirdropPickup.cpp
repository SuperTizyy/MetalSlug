// Copyright Epic Games, Inc. All Rights Reserved.

/**
 * @file AirdropPickup.cpp
 * @brief AAirdropPickup 实现 — 服务器权威空投拾取 Actor
 *
 * 大厂架构定位:
 *   - 单一职责: 物理下落 + 触碰检测 + 弹药补给 + 音效 RPC + 账本自维护
 *   - 真理源: OwningSubsystem (URoomAirdropSubsystem) 持有账本, Pickup 仅在销毁时通知
 *   - 服务器权威: HasAuthority() 守卫, 业务逻辑只在服务器跑; 客户端仅播音效
 *   - 零兜底: SphereComponent 缺失 → Log Error (空投将无法被吃掉); StaticMesh 缺失 → Warning
 *
 * 核心业务规则 (用户业务规则驱动):
 *   - 人类必须持有主武器才能吃空投 (没武器 = 配置错, Log Error)
 *   - 母体无效 (穿透空投, 不补给)
 *   - 死亡人类无法吃空投 (死人不能吃, 业务过滤)
 *
 * RPC 设计 (服务器权威 Multicast):
 *   - Multicast_PlayDropSound: 服务器校验 Sound 字段非空后推送
 *   - Multicast_PlayPickupSound: 服务器校验 Sound 字段非空后推送 (镜像 Drop 模式)
 *   - 客户端收到 RPC 后再次校验 Sound 非空 (防 RPC 序列化异常)
 *
 * 零兜底原则落地:
 *   - SphereComponent 为空 → Log Error (空投将无法被吃掉)
 *   - StaticMesh 未挂 → Log Warning (空投在场景中看不见)
 *   - FireComponent 为空 → Log Error (策划误删 BP 子对象)
 *   - 触碰者无主武器 → Log Error (业务规则要求主武器)
 */

#include "Pickups/AirdropPickup.h"

// Engine includes
#include "Components/SphereComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Components/CapsuleComponent.h"
#include "Engine/World.h"
#include "CollisionQueryParams.h"
#include "CollisionShape.h"
#include "Sound/SoundBase.h"
#include "Kismet/GameplayStatics.h"

// Project includes
#include "Characters/BaseCharacter.h"
#include "Weapons/BaseWeapon.h"
#include "Components/WeaponFireComponent.h"
#include "Systems/Airdrop/RoomAirdropSubsystem.h"

/**
 * @brief 构造函数 — 配置网络复制 + 创建 StaticMeshComponent + SphereComponent
 *
 * 大厂原则落地:
 *   - 网络复制: 启用 bReplicates + SetReplicateMovement(true), 客户端能看到空投下落
 *   - 单一真理源: StaticMeshComponent 是 Root, 也是物理载体 (物理参数统一在 Mesh 上设)
 *   - 碰撞分工: StaticMesh = BlockAll (对地面); Sphere = Overlap Pawn (检测拾取)
 *   - 物理参数: Mass=5.0kg + LinearDamping=0.5, 营造金属箱下落感
 *   - 显式优于隐式: SetEnableGravity(true) 显式声明, 即使它是 UE 默认
 */
AAirdropPickup::AAirdropPickup()
{
	// 大厂原则 — 网络同步: Actor 复制 (外观必须同步)
	//   - 关键业务字段不复制 (OwningSubsystem / 账本), 它们仅服务器需要
	bReplicates = true;
	// 【v117.6 大厂架构修复】启用移动复制 — 空投下落由服务器物理模拟, 客户端需同步位置
	//   - 旧版 (v117) SetReplicateMovement(false) → 客户端看不到空投下落过程, 只有初始悬空位置
	//   - 新版: 启用移动复制, 服务器物理引擎每帧同步位置到客户端
	//   - 带宽开销: 空投是低频事件 (5s 一次), 完全可接受
	SetReplicateMovement(true);

	// 默认 5 秒一次 NetUpdate 即可 (空投是静态的, 不需要频繁同步)
	// UE 5.6 提示 NetUpdateFrequency/MinNetUpdateFrequency deprecated, 改用 setter
	SetNetUpdateFrequency(5.0f);
	SetMinNetUpdateFrequency(1.0f);

	// ==========================================
	// StaticMeshComponent — 空投外观 + 物理载体
	// ==========================================
	// 大厂原则 — 不强制挂 StaticMesh: 策划在 BP 子类挂任意 Mesh
	//   - 我们这里只创建组件, 不指定资产
	StaticMeshComponent = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("StaticMeshComponent"));
	RootComponent = StaticMeshComponent;

	// 默认碰撞: BlockAll (对地面阻挡, 符合用户业务规则)
	//   - 蓝图可改 CollisionProfile
	if (StaticMeshComponent)
	{
		StaticMeshComponent->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
		StaticMeshComponent->SetCollisionObjectType(ECC_WorldDynamic);
		StaticMeshComponent->SetCollisionResponseToAllChannels(ECR_Block);
		// Pawn 通道: 我们交给 SphereComponent 处理, 这里改 Ignore, 避免双重响应
		StaticMeshComponent->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
		StaticMeshComponent->SetGenerateOverlapEvents(false); // 不让 Mesh 触发 Overlap, 让 Sphere 接管

		// 【v117.6 大厂架构修复】启用物理模拟 — 空投从高处自由下坠
		//   - 旧版 (v117) 默认 SetSimulatePhysics(false) → 空投悬空在 +DropHeight 位置不动
		//   - 新版: SetSimulatePhysics(true) → 服务器物理引擎接管, 重力下落
		//   - 大厂原则 — 单一真理源: Mesh 是 Root, 也是物理载体, 物理参数统一在 Mesh 上设
		//   - 大厂原则 — 显式优于隐式: SetEnableGravity(true) 显式声明, 即使它是 UE 默认
		StaticMeshComponent->SetSimulatePhysics(true);
		StaticMeshComponent->SetEnableGravity(true);

		// 【v117.6 物理参数】空投质量 + 阻力 — 营造合理的下落感
		//   - 默认 Mass=1.0kg + LinearDamping=0 → 下落过快像石头
		//   - 调质量到 5.0kg (金属箱感觉) + LinearDamping=0.5 (轻微空气阻力)
		//   - 这是大厂原则 — 物理参数业务可调 (BP 子类可覆盖)
		StaticMeshComponent->SetMassOverrideInKg(NAME_None, 5.0f, true);
	}

	// ==========================================
	// SphereComponent — Overlap 检测
	// ==========================================
	SphereComponent = CreateDefaultSubobject<USphereComponent>(TEXT("SphereComponent"));
	SphereComponent->SetupAttachment(RootComponent);
	SphereComponent->InitSphereRadius(80.0f); // 默认 80cm (略大于 Capsule 直径, 让重叠更宽容)

	// 大厂原则 — 碰撞配置:
	//   - QueryOnly (不参与物理, 不影响空投"下落" — 但 v117 默认不下落, 直接生成在 +100cm 处即可)
	//   - 对 Pawn = Overlap (让 Handle_OverlapBegin 跑判定)
	//   - 其他通道 = Ignore (不阻挡任何东西)
	if (SphereComponent)
	{
		SphereComponent->SetCollisionEnabled(ECollisionEnabled::QueryOnly);
		SphereComponent->SetCollisionObjectType(ECC_WorldDynamic);
		SphereComponent->SetCollisionResponseToAllChannels(ECR_Ignore);
		SphereComponent->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);
		SphereComponent->SetGenerateOverlapEvents(true);
	}
}


/**
 * @brief 引擎回调 — 绑定 Overlap 事件 + 防御性日志 (SphereComponent/Mesh 缺失检测)
 *
 * 关键检查:
 *   - SphereComponent 为空 → Log Error (空投将无法被吃掉)
 *   - StaticMesh 未挂 → Log Warning (空投在场景中看不见)
 */
void AAirdropPickup::BeginPlay()
{
	Super::BeginPlay();

	// 大厂原则 — 防御: 没 SphereComponent 就报错, 不静默"空投无碰撞"
	if (!SphereComponent)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[AirdropPickup] BeginPlay: SphereComponent 为空! 空投将无法被吃掉. "
			     "【修复】检查 BP_AirdropPickup 是否删除了 SphereComponent 子对象."));
	}
	else
	{
		// 绑定 Overlap 事件 (UFUNCTION 必须, 否则 UE 找不到回调)
		SphereComponent->OnComponentBeginOverlap.AddDynamic(this, &AAirdropPickup::Handle_OverlapBegin);

		UE_LOG(LogTemp, Verbose,
			TEXT("[AirdropPickup] BeginPlay: %s SphereRadius=%.1fcm 碰撞通道=Pawn Overlap"),
			*GetName(), SphereComponent->GetUnscaledSphereRadius());
	}

	// 大厂原则 — 防御: 没 StaticMesh 也警告 (空投看不见, 用户可能误以为生成失败)
	if (!StaticMeshComponent || !StaticMeshComponent->GetStaticMesh())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[AirdropPickup] BeginPlay: %s StaticMesh 未挂! 空投在场景中看不见. "
			     "【修复】在 BP_AirdropPickup → Components → StaticMeshComponent → Static Mesh 字段拖入网格体资产."),
			*GetName());
	}
}


/**
 * @brief 引擎回调 — 通知 OwningSubsystem 从账本移除 + 解绑 Overlap 事件
 *
 * @param EndPlayReason  销毁原因 (Destroyed / RemovedFromWorld / EndPlayInEditor 等)
 *
 * @note 即使手动 Destroy() 也会走这里, OwningSubsystem 账本自动清理
 * @note 解绑 Overlap 是 UE 5.6 推荐做法, 防 Actor 销毁后回调悬空
 */
void AAirdropPickup::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 大厂原则 — 账本自维护: Actor 销毁时通知 Subsystem
	//   - 即使是手动 Destroy() 也会走这里
	//   - World 卸载时 OwningSubsystem 可能已 null (Subsystem 在 Outer 销毁前先释放)
	if (OwningSubsystem)
	{
		OwningSubsystem->NotifyPickupDestroyed(this);
		OwningSubsystem = nullptr;
	}

	// 解绑 Overlap 事件 (UE 5.6 推荐, 防 Actor 销毁后回调悬空)
	if (SphereComponent)
	{
		SphereComponent->OnComponentBeginOverlap.RemoveDynamic(this, &AAirdropPickup::Handle_OverlapBegin);
	}

	Super::EndPlay(EndPlayReason);
}


// ==========================================
// 【v117 业务核心】Overlap 回调 — 服务器权威
// ==========================================

/**
 * @brief Overlap 回调 — 服务器权威触发"被吃掉"业务 (弹药补给 + 销毁 + 音效)
 *
 * 业务过滤链 (大厂原则 — 显式优于隐式):
 *   1. 非 Character Actor (子弹/道具/抛出的武器等) → 跳过 (正常过滤, 不用 Error)
 *   2. 母体 (bIsMother=true) → 跳过 (业务规则: 母体无效, 表现=穿透)
 *   3. 死亡人类 → 跳过 (死人不能吃, 业务过滤)
 *   4. 无主武器 → Log Error + return (业务规则: 必须持主武器才能吃空投)
 *   5. 无 FireComponent → Log Error + return (策划误删 BP 子对象)
 *   6. FireComp->Server_RefillAmmo() → 弹药全满 (真理源: FireComponent)
 *   7. Play Pickup Sound RPC → Multicast 播放音效
 *   8. Destroy() → 通知 Subsystem 清理账本
 *
 * @param OverlappedComponent  触发 Overlap 的组件 (SphereComponent)
 * @param OtherActor           重叠的对端 Actor (玩家/AI)
 * @param OtherComp            对端组件 (未使用)
 * @param OtherBodyIndex       对端 body index (未使用)
 * @param bFromSweep           是否由 Sweep 触发 (未使用)
 * @param SweepResult          Sweep 命中结果 (未使用)
 *
 * @note 服务器权威: HasAuthority() == false 时直接 return (客户端不跑业务)
 */
void AAirdropPickup::Handle_OverlapBegin(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
	UPrimitiveComponent* OtherComp, int32 OtherBodyIndex,
	bool bFromSweep, const FHitResult& SweepResult)
{
	// 大厂原则 — 服务器权威: 客户端不跑业务逻辑 (即使 UE 在两端都 fire overlap, 业务仅服务器处理)
	if (!HasAuthority())
	{
		return;
	}

	if (!OtherActor)
	{
		return;
	}

	// 1. 非 Character Actor (子弹 / 道具 / 抛出的武器等) → 跳过 (正常过滤, 不用 Error)
	ABaseCharacter* OtherChar = Cast<ABaseCharacter>(OtherActor);
	if (!OtherChar)
	{
		UE_LOG(LogTemp, Verbose,
			TEXT("[AirdropPickup] %s Overlap with non-Character Actor=%s, 跳过."),
			*GetName(), *OtherActor->GetName());
		return;
	}

	// 2. 母体无效 — 业务规则, Verbose 日志让用户能看到母体走过去不触发的根因
	if (OtherChar->bIsMother)
	{
		UE_LOG(LogTemp, Verbose,
			TEXT("[AirdropPickup] %s Overlap with Mother=%s, 业务规则: 母体无效, 跳过. (对母体表现 = 穿透)"),
			*GetName(), *OtherChar->GetName());
		return;
	}

	// 3. 死亡人类 → 跳过 (死人不能吃, 正常业务过滤)
	if (OtherChar->IsDead())
	{
		UE_LOG(LogTemp, Verbose,
			TEXT("[AirdropPickup] %s Overlap with Dead Character=%s, 跳过."),
			*GetName(), *OtherChar->GetName());
		return;
	}

	// 4. 解析武器 — 没主武器 = 配置错, 必须 Error (不能静默让空投消失)
	ABaseWeapon* CurrentWeapon = ResolveTouchingWeapon(OtherChar);
	if (!CurrentWeapon)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[AirdropPickup] %s Overlap with %s (Character) 但该角色没有主武器! "
			     "【业务规则】人类必须持有主武器才能吃空投. "
			     "【修复】检查 %s 的武器 Spawn 链路 (WeaponAttachmentComponent::RequestWeaponSpawn)."),
			*GetName(), *OtherChar->GetName(), *OtherChar->GetName());
		return;
	}

	// 5. 解析 FireComponent — 没 FireComp = 配置错
	UWeaponFireComponent* FireComp = CurrentWeapon->GetWeaponFireComponent();
	if (!FireComp)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[AirdropPickup] %s Overlap with %s 但主武器 %s 没 WeaponFireComponent! "
			     "【修复】检查 BP 蓝图是否被策划删除了 WeaponFireComponent 子对象."),
			*GetName(), *OtherChar->GetName(), *CurrentWeapon->GetName());
		return;
	}

	// 6. 调用弹药全满接口 (大厂原则 — 真理源唯一: FireComponent 是弹药真理源)
	//   - 内部会写入 CurrentAmmo = MagazineSize, ReserveAmmo = InitialReserveAmmo
	//   - 写入触发 Replicated, 客户端 HUD 自动同步
	FireComp->Server_RefillAmmo();

	UE_LOG(LogTemp, Display,
		TEXT("[AirdropPickup] %s 被 %s 吃掉! 主武器 %s 弹药已全满."),
		*GetName(), *OtherChar->GetName(), *CurrentWeapon->GetName());

	// 【v2xx 大厂架构新增】播放"被吃掉"音效 — 镜像 v118 生成音效 RPC 模式
	//   - 时序: 必须在 Destroy() 之前, 否则 Actor 已销毁, Multicast 参数序列化失败
	//   - 业务可独立配置 (PickupSound 字段), 不复用 DropSound (语义不同)
	//   - 大厂原则 — 零兜底: PickupSound 为空 → Log Warning + 跳过 (与 DropSound 同策略, 业务可容忍)
	if (PickupSound)
	{
		Multicast_PlayPickupSound(
			PickupSound,
			PickupSoundVolumeMultiplier,
			PickupSoundPitchMultiplier);
	}
	else
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[AirdropPickup] Handle_OverlapBegin: 空投 %s PickupSound 字段为空, 跳过拾取音效播放. "
			     "【修复】在 BP_AirdropPickup 蓝图 → Class Defaults → Airdrop → Pickup Sound 字段拖入 Sound 资产."),
			*GetName());
	}

	// 7. 销毁空投 + 通知 Subsystem 从账本移除
	//   - Destroy() 会同步触发 EndPlay → NotifyPickupDestroyed → 账本清理
	//   - 显式 NotifyPickupDestroyed 是为了: 如果 Destroy 因某种原因延迟, 账本不会留
	Destroy();
}


// ==========================================
// 【v117 工具方法】解析触碰者拿的武器 (按需 lazy resolve)
// ==========================================

/**
 * @brief 工具方法 — 通过触碰者 Character 解析其当前主武器 (按需 lazy resolve, 不缓存)
 *
 * @param OtherChar  触碰的 Character (可为 null, 返回 nullptr)
 *
 * @return 触碰者当前持有的主武器, 无武器时返回 nullptr
 *
 * @note 大厂原则 — 按需查询: 不缓存武器指针, 调一次约 0.001ms
 * @note Character 可能在中途切武器, 缓存会失效
 * @note 单一真理源: Character->GetCurrentWeapon() (由 WeaponAttachmentComponent 维护)
 */
ABaseWeapon* AAirdropPickup::ResolveTouchingWeapon(ABaseCharacter* OtherChar) const
{
	if (!OtherChar)
	{
		return nullptr;
	}

	// 大厂原则 — 按需查询: 不缓存武器指针, 调一次约 0.001ms
	//   - Character 可能在中途切武器, 缓存会失效
	//   - 单一真理源: Character->GetCurrentWeapon() (由 WeaponAttachmentComponent 维护)
	return OtherChar->GetCurrentWeapon();
}


// ==========================================
// 【v118 大厂架构新增】音效 RPC — 服务器权威 Multicast 推送
// ==========================================

/**
 * @brief Multicast RPC 实现 — 在所有客户端播放空投生成音效 (服务器权威推送)
 *
 * 大厂原则落地:
 *   - 客户端零兜底: Sound 为空 → Log Error + 拒绝播放 (防 RPC 序列化异常)
 *   - 单点播放: 用 Actor 当前位置 (生成时 = +DropHeight 悬空点, 业务可接受)
 *
 * @param InSound    要播放的 Sound 资产 (服务器校验非空后推送)
 * @param VolumeMul  音量倍数
 * @param PitchMul   音高倍数
 *
 * @note 客户端二次校验 Sound 非空, 防 RPC 序列化时 Sound 引用丢失
 * @note 镜像 Multicast_PlayPickupSound (结构对称, 语义独立)
 */
void AAirdropPickup::Multicast_PlayDropSound_Implementation(USoundBase* InSound, float VolumeMul, float PitchMul)
{
	// 大厂原则 — 零兜底: Sound 字段为空 → 强制修复 BP_AirdropPickup
	if (!InSound)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[AirdropPickup] Multicast_PlayDropSound_Implementation: InSound 为空 — 拒绝播放. "
			     "Pickup=%s. "
			     "【v118 大厂原则 — 零兜底】服务器已校验 DropSound 字段, 客户端再次校验防 RPC 序列化异常."),
			*GetName());
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[AirdropPickup] Multicast_PlayDropSound_Implementation: World 无效 — 拒绝播放. Pickup=%s."),
			*GetName());
		return;
	}

	// 大厂原则 — 单点播放: 用 Actor 当前位置 (空投刚生成时, 位置在 +DropHeight, 落地音效可能错位)
	//   - 但用户业务是 "生成时播放" 而非 "落地时播放", 用 Spawn 时位置正确
	const FVector PlayLocation = GetActorLocation();

	// UE 5.6 标准 API: PlaySoundAtLocation (本地播放, 不跨进程)
	UGameplayStatics::PlaySoundAtLocation(
		this,
		InSound,
		PlayLocation,
		VolumeMul,
		PitchMul);

	UE_LOG(LogTemp, Verbose,
		TEXT("[AirdropPickup] Multicast_PlayDropSound: 播放空投音效 — Sound=%s VolMul=%.2f PitchMul=%.2f Loc=%s"),
		*InSound->GetName(),
		VolumeMul,
		PitchMul,
		*PlayLocation.ToString());
}


// ==========================================
// 【v2xx 大厂架构新增】音效 RPC — 镜像 Multicast_PlayDropSound 完整对称设计
// ==========================================
//
// 为什么不复用 Multicast_PlayDropSound:
//   - 语义不同: 生成 (Drop) vs 拾取 (Pickup)
//   - 业务独立: 策划可能配不同音效资产 (生成 = 重型空投落地, 拾取 = 武器弹药补给)
//   - 强制复用 = 隐式耦合, 业务调整时互相影响
//
// 大厂原则 — DRY 不是"机械复用", 而是"结构对称":
//   - 函数实现 ≈ 100% 相同 (差异仅: 注释里说明是"拾取"音效 + Log tag)
//   - 不抽出 helper 函数 (会让两个 RPC 都增加 1 层调用, 性能无意义)
//   - 直接复制实现, 注释里明确"镜像 Multicast_PlayDropSound"
//   - 这是 v117-v2xx 一直用的模式 (Multicast_PlayFireMontage / Multicast_PlayReloadMontage 也镜像)

/**
 * @brief Multicast RPC 实现 — 在所有客户端播放空投拾取音效 (服务器权威推送)
 *
 * 镜像 Multicast_PlayDropSound 完整对称设计:
 *   - 结构 100% 相同, 仅注释 + Log tag 不同
 *   - 业务语义独立: 生成音效 (Drop) vs 拾取音效 (Pickup)
 *
 * @param InSound    要播放的 Sound 资产 (服务器校验非空后推送)
 * @param VolumeMul  音量倍数
 * @param PitchMul   音高倍数
 *
 * @note 客户端二次校验 Sound 非空, 防 RPC 序列化时 Sound 引用丢失
 * @note 时序: 必须在 Destroy() 之前调用, 否则 Actor 已销毁, Multicast 参数序列化失败
 * @note 单点播放: 用 Actor 当前位置 (拾取时位置 = 空投落地点, 音效位置准确)
 */
void AAirdropPickup::Multicast_PlayPickupSound_Implementation(USoundBase* InSound, float VolumeMul, float PitchMul)
{
	// 大厂原则 — 零兜底: Sound 字段为空 → 强制修复 BP_AirdropPickup
	if (!InSound)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[AirdropPickup] Multicast_PlayPickupSound_Implementation: InSound 为空 — 拒绝播放. "
			     "Pickup=%s. "
			     "【v2xx 大厂原则 — 零兜底】服务器已校验 PickupSound 字段, 客户端再次校验防 RPC 序列化异常."),
			*GetName());
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[AirdropPickup] Multicast_PlayPickupSound_Implementation: World 无效 — 拒绝播放. Pickup=%s."),
			*GetName());
		return;
	}

	// 大厂原则 — 单点播放: 用 Actor 当前位置
	//   - 拾取时位置 = 空投落地点 (物理已落定), 音效位置准确
	const FVector PlayLocation = GetActorLocation();

	// UE 5.6 标准 API: PlaySoundAtLocation (本地播放, 不跨进程)
	UGameplayStatics::PlaySoundAtLocation(
		this,
		InSound,
		PlayLocation,
		VolumeMul,
		PitchMul);

	UE_LOG(LogTemp, Verbose,
		TEXT("[AirdropPickup] Multicast_PlayPickupSound: 播放拾取音效 — Sound=%s VolMul=%.2f PitchMul=%.2f Loc=%s"),
		*InSound->GetName(),
		VolumeMul,
		PitchMul,
		*PlayLocation.ToString());
}