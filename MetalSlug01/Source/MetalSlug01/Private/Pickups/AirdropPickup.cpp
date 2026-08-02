// Copyright Epic Games, Inc. All Rights Reserved.

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

	// 7. 销毁空投 + 通知 Subsystem 从账本移除
	//   - Destroy() 会同步触发 EndPlay → NotifyPickupDestroyed → 账本清理
	//   - 显式 NotifyPickupDestroyed 是为了: 如果 Destroy 因某种原因延迟, 账本不会留
	Destroy();
}


// ==========================================
// 【v117 工具方法】解析触碰者拿的武器 (按需 lazy resolve)
// ==========================================

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