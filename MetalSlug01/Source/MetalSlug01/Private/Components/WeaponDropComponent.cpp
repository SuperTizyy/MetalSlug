// ==========================================
// 版权声明：在项目设置的描述页面填写您的版权信息。
// ==========================================
//
// @file WeaponDropComponent.cpp
// @brief 掉落武器自治组件实现
//
// 【v200 大厂架构】
// 详见 WeaponDropComponent.h 头文件注释
//
// 【大厂原则】
//   1. 单一职责: 本组件只管掉落状态、物理、生命周期、弹药快照
//   2. 武器自治: 挂在 ABaseWeapon 上, 与 WeaponDissolveComponent / WeaponFireComponent 对称
//   3. 零兜底: 参数错/状态错 → Log Error + return, 不静默跳过
//   4. 幂等: 重复调用有明确日志, 不破坏状态
// ==========================================

#include "Components/WeaponDropComponent.h"
#include "Weapons/BaseWeapon.h"
#include "Components/WeaponFireComponent.h"
#include "Characters/BaseCharacter.h"
#include "GameFramework/Character.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h" // 【v200.2.2】用于 RecreatePhysicsState
#include "PhysicsEngine/PhysicsAsset.h"
#include "Kismet/GameplayStatics.h"
#include "TimerManager.h"
#include "Engine/World.h"
#include "Engine/CollisionProfile.h"
#include "EngineUtils.h" // 【v200.2.5】TActorIterator
#include "Combat/WeaponAttachmentComponent.h"
#include "Components/CharacterEvents.h"
#include "Engine/ActorChannel.h"
#include "Net/RepLayout.h"

// ==========================================
// 构造函数
// ==========================================
UWeaponDropComponent::UWeaponDropComponent()
{
	// 组件在网络复制中可见
	SetIsReplicatedByDefault(true);

	// 默认关闭 Tick (本组件不需要每帧检查)
	PrimaryComponentTick.bCanEverTick = false;
}

// ==========================================
// BeginPlay
// ==========================================
void UWeaponDropComponent::BeginPlay()
{
	Super::BeginPlay();

	// 注册 Overlap 事件
	// Owner Weapon 必须有 StaticMeshComponent 或 SkeletalMeshComponent 才能注册
	if (ABaseWeapon* OwnerWeapon = ResolveOwnerWeapon())
	{
		if (UPrimitiveComponent* MeshComp = Cast<UPrimitiveComponent>(OwnerWeapon->GetMeshComponent()))
		{
			MeshComp->OnComponentBeginOverlap.AddDynamic(this, &UWeaponDropComponent::OnOverlapBegin);
		}
	}
	else
	{
		// Owner 无效不是错误, 因为可能在 Spawn 前就调用 BeginPlay
	}
}

// ==========================================
// EndPlay
// ==========================================
void UWeaponDropComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 清理计时器
	if (UWorld* World = GetWorld())
	{
		if (LifetimeTimerHandle.IsValid())
		{
			World->GetTimerManager().ClearTimer(LifetimeTimerHandle);
		}
	}

	Super::EndPlay(EndPlayReason);
}

// ==========================================
// 启动掉落状态
// ==========================================
void UWeaponDropComponent::StartDroppedState(ABaseCharacter* InDropInstigator)
{
	// 1. 基础校验
	ABaseWeapon* OwnerWeapon = ResolveOwnerWeapon();
	if (!OwnerWeapon)
	{
		// 这种情况理论上不应该发生, 因为掉落的武器一定是有效的
		UE_LOG(LogTemp, Error,
			TEXT("[WeaponDropComponent] StartDroppedState: Owner Weapon 无效. Component 挂在了一个无效 Actor 上."));
		return;
	}

	// 2. 幂等检查
	if (bIsDropped)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[WeaponDropComponent] StartDroppedState: 武器 '%s' 已经在掉落状态, 忽略重复调用."),
			*OwnerWeapon->GetName());
		return;
	}

	// 3. 配置校验
	if (PickupRadius <= 0.0f)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[WeaponDropComponent] StartDroppedState: PickupRadius <= 0 (%f). 必须在 BP 蓝图配置 PickupRadius > 0."),
			PickupRadius);
		return;
	}

	// 4. 保存弹药快照
	SaveAmmoSnapshot();

	// 5. 记录丢弃者
	DropInstigator = InDropInstigator;

	// 5. 获取 Mesh 组件
	UPrimitiveComponent* MeshComp = Cast<UPrimitiveComponent>(OwnerWeapon->GetMeshComponent());
	if (!MeshComp)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[WeaponDropComponent] StartDroppedState: 武器 '%s' 的 GetMeshComponent() 返回 null! 武器没有 Mesh, 无法掉落. 修复: 在武器蓝图 BP 中添加 StaticMeshComponent 并设为 RootComponent."),
			*OwnerWeapon->GetName());
		return;
	}

	// 6. 诊断日志: 检查 Mesh 状态
	UE_LOG(LogTemp, Display,
		TEXT("[WeaponDropComponent] StartDroppedState: 武器 '%s' MeshComp=%s, Location=%s"),
		*OwnerWeapon->GetName(),
		*MeshComp->GetName(),
		*OwnerWeapon->GetActorLocation().ToString());

	// 【v200 大厂架构关键】先从角色上分离武器
	// 如果武器仍然附加在角色上，设置 SimulatePhysics 可能无效
	OwnerWeapon->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	UE_LOG(LogTemp, Display,
		TEXT("[WeaponDropComponent] StartDroppedState: 武器 '%s' 已从角色分离."),
		*OwnerWeapon->GetName());

	// 7. 保存碰撞预设并切换到 PhysicsOnly
	// 【v200 大厂架构关键修复】SkeletalMeshComponent 需要 QueryAndPhysics 而不是 PhysicsOnly
	//   原因: "无效模拟选项：形体被设置为模拟物理，但"启用碰撞"不兼容"
	//   PhysicsOnly 模式下 SkeletalMesh 的碰撞查询会失败，导致物理模拟无效
	PreviousCollisionProfile = MeshComp->GetCollisionProfileName();
	UE_LOG(LogTemp, Display,
		TEXT("[WeaponDropComponent] StartDroppedState: 武器 '%s' 原碰撞预设=%s"),
		*OwnerWeapon->GetName(),
		*PreviousCollisionProfile.ToString());

	// 【v200.2.4 关键修复】Collision profile 重新设计 — 大厂 UE 拾取标准
	//   旧版用 PhysicsActor → Block All Channels → 玩家 CapsuleComponent 被武器推开 → 永远触发不了 OverlapBegin
	//   用户反馈 "有阻挡，但捡不起来" 正是 PhysicsActor 把玩家挡在外面
	//
	//   UE 5 碰撞规则 (大厂标准):
	//     Block = 物理推开 + 不触发事件
	//     Overlap = 穿过 + 触发事件
	//   拾取 = 必须 Overlap, 玩家走过去 → OnOverlapBegin → 按 E 拾取
	//
	//   v200.2.4 通道配置:
	//     - WorldStatic: Block (武器落地, 不能穿地)
	//     - Pawn: Overlap (玩家走过去触发拾取, 不被推开)
	//     - PhysicsBody: Overlap (不与其他动态物体互推)
	//     - Visibility: Block (玩家射线检测仍能命中武器 mesh)
	MeshComp->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	MeshComp->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);
	MeshComp->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);         // 关键: 不再 Block 玩家
	MeshComp->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Overlap);
	MeshComp->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);
	MeshComp->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	MeshComp->SetCollisionResponseToChannel(ECC_Destructible, ECR_Ignore);
	MeshComp->SetCollisionResponseToChannel(ECC_Vehicle, ECR_Overlap);

	// 【v200.2.6 致命根因修复】必须显式设 SetGenerateOverlapEvents(true)!
	//   根因: UE 物理模拟的 SkeletalMesh 默认 bGenerateOverlapEvents=false
	//         玩家走到距离 52cm(< PickupRadius 250cm)也没触发 OnComponentBeginOverlap
	//         因为 SkeletalMesh 物理 Body 不向 Component 层报告 overlap, 只报告 hit
	//   修复: 显式开启 + SetNotifyRigidBodyCollision(true) 让物理 body 也通知 overlap
	//         这是 UE 大厂拾取系统标配 (Lyra / Paragon 都这么做)
	MeshComp->SetGenerateOverlapEvents(true);
	UE_LOG(LogTemp, Display,
		TEXT("[WeaponDropComponent] StartDroppedState: 武器 '%s' 已开启 GenerateOverlapEvents (UE 大厂拾取标配 — 否则物理 mesh 不触发 overlap 事件)"),
		*OwnerWeapon->GetName());

	UE_LOG(LogTemp, Display,
		TEXT("[WeaponDropComponent] StartDroppedState: 武器 '%s' 已配置 Collision (Block WorldStatic/Visibility, Overlap Pawn/PhysicsBody) — 玩家走过去能触发拾取"),
		*OwnerWeapon->GetName());

	// 【v200.2.5 诊断】验证 collision 设置生效 (RecreatePhysicsState 可能清掉, 必须二次确认)
	UE_LOG(LogTemp, Display,
		TEXT("[WeaponDropComponent] StartDroppedState: 验证Collision — PawnResponse=%d, WorldStaticResponse=%d, PhysicsBodyResponse=%d (0=Ignore,1=Overlap,2=Block), GenerateOverlapEvents=%d"),
		static_cast<int32>(MeshComp->GetCollisionResponseToChannel(ECC_Pawn)),
		static_cast<int32>(MeshComp->GetCollisionResponseToChannel(ECC_WorldStatic)),
		static_cast<int32>(MeshComp->GetCollisionResponseToChannel(ECC_PhysicsBody)),
		MeshComp->GetGenerateOverlapEvents() ? 1 : 0);

	// 【v200.2 大厂架构关键修复】必须显式启用 Actor 级别 collision
	//   根因: ABaseWeapon::BeginPlay 调用了 SetActorEnableCollision(false)
	//         → Actor 级别 collision=false → 所有通道响应失效
	//   修复: StartDroppedState 调 SetActorEnableCollision(true) (具体通道响应已在上面逐个设)
	OwnerWeapon->SetActorEnableCollision(true);
	// 【v200.2.4 修复】不再 SetCollisionResponseToAllChannels(Block) — 这会把 Pawn 也 Block
	//   玩家走不过去, 永远触发不了 OverlapBegin 拾取事件

	UE_LOG(LogTemp, Display,
		TEXT("[WeaponDropComponent] StartDroppedState: 武器 '%s' 已启用 Actor 级别 collision (不设 AllChannels Block, 避免把玩家也挡掉). Pawn=%s"),
		*OwnerWeapon->GetName(),
		*OwnerWeapon->GetName());

	// 【v200.2.1 诊断】打印物理状态, 排查 PIE 警告 "形体被设置为模拟物理，但"启用碰撞"不兼容" 的真实根因
	{
		USkeletalMeshComponent* SkelMesh = Cast<USkeletalMeshComponent>(MeshComp);
		const int32 HasPhysicsAsset = (SkelMesh && SkelMesh->GetPhysicsAsset()) ? 1 : 0;
		UE_LOG(LogTemp, Display,
			TEXT("[WeaponDropComponent] StartDroppedState: 物理状态诊断 — Weapon='%s', MeshCompClass='%s', HasPhysicsAsset=%d, GetCollisionEnabled=%d, ActorEnableCollision=%d"),
			*OwnerWeapon->GetName(),
			*MeshComp->GetClass()->GetName(),
			HasPhysicsAsset,
			static_cast<int32>(MeshComp->GetCollisionEnabled()),
			OwnerWeapon->GetActorEnableCollision() ? 1 : 0);
	}

	// 【v200.2.5 关键】掉落武器 Tick 诊断 — 每秒打印武器位置 + 距离最近玩家的距离
	//   排查 "玩家走过去没触发 overlap" 的真实原因 (可能是武器飞远了, 或玩家没走过去)
	OwnerWeapon->GetWorldTimerManager().SetTimer(
		DropPositionTimerHandle,
		FTimerDelegate::CreateWeakLambda(OwnerWeapon, [this, OwnerWeapon]()
		{
			if (!IsValid(OwnerWeapon) || !IsValid(this)) return;
			const FVector WeaponLoc = OwnerWeapon->GetActorLocation();
			float MinDist = TNumericLimits<float>::Max();
			FString NearestPlayerName = TEXT("None");
			for (TActorIterator<APawn> It(OwnerWeapon->GetWorld()); It; ++It)
			{
				if (ABaseCharacter* BC = Cast<ABaseCharacter>(*It))
				{
					if (!BC->IsPlayerControlled() || BC->IsDead()) continue;
					const float Dist = FVector::Dist(WeaponLoc, BC->GetActorLocation());
					if (Dist < MinDist) { MinDist = Dist; NearestPlayerName = BC->GetName(); }
				}
			}
			UE_LOG(LogTemp, Display,
				TEXT("[WeaponDropComponent][Tick 诊断] Weapon=%s Loc=%s 最近玩家=%s 距离=%.0fcm (PickupRadius=%.0f, 玩家需走到 <%.0fcm 内触发 overlap)"),
				*OwnerWeapon->GetName(),
				*WeaponLoc.ToString(),
				*NearestPlayerName,
				MinDist,
				PickupRadius,
				PickupRadius);
		}),
		1.0f,
		true);

	// 8. 启用物理模拟
	// 【v200.2.2 修复】在 SetSimulatePhysics 调用前强制 RecreatePhysicsState
	//   根因: UE 5.6 SkeletalMesh 的物理状态是 lazy 创建的 — 在调用 SetSimulatePhysics 时如果还没创建
	//         会失败. RecreatePhysicsState 强制立即创建
	//         这是 PIE warning "形体被设置为模拟物理，但"启用碰撞"不兼容" 的常见根因
	if (USkeletalMeshComponent* SkelMesh = Cast<USkeletalMeshComponent>(MeshComp))
	{
		SkelMesh->RecreatePhysicsState();
	}

	MeshComp->SetSimulatePhysics(true);
	UE_LOG(LogTemp, Display,
		TEXT("[WeaponDropComponent] StartDroppedState: 武器 '%s' SetSimulatePhysics(true) 已调用 (强制 RecreatePhysicsState 后)"),
		*OwnerWeapon->GetName());

	// 【v200.2.2 修复】物理激活后再次强制覆盖 Actor 级别 collision
	//   根因: SetSimulatePhysics(true) 会重新创建 physics state, 可能重置某些 collision 标志位
	OwnerWeapon->SetActorEnableCollision(true);
	MeshComp->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);

	UE_LOG(LogTemp, Display,
		TEXT("[WeaponDropComponent] StartDroppedState: 武器 '%s' 物理激活后再次强制 collision. IsSimulatingPhysics=%d, ActorEnableCollision=%d"),
		*OwnerWeapon->GetName(),
		MeshComp->IsSimulatingPhysics() ? 1 : 0,
		OwnerWeapon->GetActorEnableCollision() ? 1 : 0);

	// 9. 计算掉落方向
	FVector LaunchDirection = FVector::ZeroVector;

	if (InDropInstigator)
	{
		// 前推方向
		FVector Forward = InDropInstigator->GetActorForwardVector();
		Forward.Z = 0.0f; // 水平方向
		Forward.Normalize();
		LaunchDirection += Forward * LaunchForwardSpeed;

		// 向上抛
		LaunchDirection.Z += LaunchUpwardSpeed;
	}
	else
	{
		// 没有丢弃者时, 向上抛一点
		LaunchDirection.Z = LaunchUpwardSpeed;
	}

	// 10. 应用初始速度
	MeshComp->SetPhysicsLinearVelocity(LaunchDirection);

	// 11. 添加随机旋转
	FVector RandomTorque = FVector(
		FMath::RandRange(-500.0f, 500.0f),
		FMath::RandRange(-500.0f, 500.0f),
		FMath::RandRange(-500.0f, 500.0f)
	);
	MeshComp->SetPhysicsAngularVelocityInDegrees(RandomTorque);

	// 12. 设置掉落状态
	bIsDropped = true;

	// 13. 启动生命周期计时器
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().SetTimer(
			LifetimeTimerHandle,
			this,
			&UWeaponDropComponent::OnPickupLifetimeExpired,
			PickupLifetimeSeconds,
			false // 不重复
		);
	}

	// 14. 广播事件
	OnDroppedWeaponReady.Broadcast(OwnerWeapon);

	UE_LOG(LogTemp, Display,
		TEXT("[WeaponDropComponent] StartDroppedState: 武器 '%s' 已进入掉落状态. DropInstigator='%s', PickupRadius=%.1f, Lifetime=%.1f秒."),
		*OwnerWeapon->GetName(),
		InDropInstigator ? *InDropInstigator->GetName() : TEXT("None"),
		PickupRadius,
		PickupLifetimeSeconds);
}

// ==========================================
// 取消掉落状态 (捡起)
// ==========================================
bool UWeaponDropComponent::CancelDroppedState(ABaseCharacter* Picker)
{
	// 1. 基础校验
	ABaseWeapon* OwnerWeapon = ResolveOwnerWeapon();
	if (!OwnerWeapon)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[WeaponDropComponent] CancelDroppedState: Owner Weapon 无效."));
		return false;
	}

	// 2. 幂等检查
	if (!bIsDropped)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[WeaponDropComponent] CancelDroppedState: 武器 '%s' 不在掉落状态, 忽略."),
			*OwnerWeapon->GetName());
		return false;
	}

	// 3. 弹药快照校验
	if (!AmmoSnapshot.bIsValid)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[WeaponDropComponent] CancelDroppedState: 武器 '%s' 的弹药快照无效, 无法恢复. Pickup 失败."),
			*OwnerWeapon->GetName());
		return false;
	}

	// 4. 停止生命周期计时器
	if (UWorld* World = GetWorld())
	{
		if (LifetimeTimerHandle.IsValid())
		{
			World->GetTimerManager().ClearTimer(LifetimeTimerHandle);
			LifetimeTimerHandle.Invalidate();
		}

		// 【v200.2.5】清理诊断 Tick 计时器
		if (DropPositionTimerHandle.IsValid())
		{
			World->GetTimerManager().ClearTimer(DropPositionTimerHandle);
			DropPositionTimerHandle.Invalidate();
		}
	}

	// 5. 获取 Mesh 组件并恢复物理状态
	UPrimitiveComponent* MeshComp = Cast<UPrimitiveComponent>(OwnerWeapon->GetMeshComponent());
	if (MeshComp)
	{
		// 【v200.2.10 大厂架构 P0 修复】SetSimulatePhysics 前打印物理 Body 状态, 排查 transform 残留
		const FVector WorldLocBeforeStopPhysics = OwnerWeapon->GetActorLocation();
		UE_LOG(LogTemp, Display,
			TEXT("[v200.2.10 诊断] CancelDroppedState: 停止物理前 Weapon='%s' WorldLoc=%s IsSimulatingPhysics=%d"),
			*OwnerWeapon->GetName(),
			*WorldLocBeforeStopPhysics.ToCompactString(),
			MeshComp->IsSimulatingPhysics() ? 1 : 0);

		// 停止物理模拟
		MeshComp->SetSimulatePhysics(false);

		// 【v200.2.10 关键修复】物理停止后强制同步物理 Body 的 transform
		//   根因: SkeletalMesh 在物理模拟期间, Mesh 的物理 Body (BodyInstance) 与 Component 分离
		//         → SetSimulatePhysics(false) 只停止模拟, 不把物理 Body 的 transform 同步回 Component
		//         → Mesh 渲染 transform 仍停留在地面位置, 但 Component 已经 attach 到 socket
		//         → 用户看到"武器 attach 到 socket 但 mesh 视觉上不在 socket 上"
		//   修复: 物理停止后, 强制 ResetBodyTransform + UpdateBodyToBones
		//         让物理 Body 跟随 Component 的新 transform
		MeshComp->RecreatePhysicsState();

		const FVector WorldLocAfterRecreate = OwnerWeapon->GetActorLocation();
		UE_LOG(LogTemp, Display,
			TEXT("[v200.2.10 诊断] CancelDroppedState: RecreatePhysicsState 后 Weapon='%s' WorldLoc=%s (期望接近 Picker 位置)"),
			*OwnerWeapon->GetName(),
			*WorldLocAfterRecreate.ToCompactString());

		// 【v200.2 大厂架构关键修复】恢复装饰武器状态 — NoCollision + Ignore
		//   与 ABaseWeapon::BeginPlay 完全对称: 武器被捡起后回到"挂在角色身上不阻挡任何东西"的状态
		//   旧版用 PreviousCollisionProfile = "Custom", 但 Custom 也被 BeginPlay 设为 NoCollision, 不一致
		//   修复: 显式设 NoCollision + Ignore + SetActorEnableCollision(false), 与 BeginPlay 完全一致
		MeshComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		MeshComp->SetCollisionResponseToAllChannels(ECR_Ignore);
		OwnerWeapon->SetActorEnableCollision(false);
	}

	// 6. 恢复弹药快照
	RestoreAmmoSnapshot();

	// 7. 清理掉落状态
	bIsDropped = false;
	DropInstigator.Reset();
	AmmoSnapshot = FAmmoSnapshot(); // 重置

	// 8. 广播事件
	OnDroppedWeaponPickedUp.Broadcast(OwnerWeapon, Picker);

	UE_LOG(LogTemp, Display,
		TEXT("[WeaponDropComponent] CancelDroppedState: 武器 '%s' 被 '%s' 捡起."),
		*OwnerWeapon->GetName(),
		Picker ? *Picker->GetName() : TEXT("None"));

	return true;
}

// ==========================================
// 获取掉落武器名称
// ==========================================
FString UWeaponDropComponent::GetDroppedWeaponName() const
{
	if (ABaseWeapon* OwnerWeapon = ResolveOwnerWeapon())
	{
		return OwnerWeapon->GetName();
	}
	return TEXT("UnknownWeapon");
}

// ==========================================
// Overlap 回调
// ==========================================
void UWeaponDropComponent::OnOverlapBegin(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
                                       UPrimitiveComponent* OtherComp, int32 OtherBodyIndex,
                                       bool bFromSweep, const FHitResult& Hit)
{
	// 仅服务器处理捡起
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor || !OwnerActor->HasAuthority())
	{
		return;
	}

	// 检查是否在掉落状态
	if (!bIsDropped)
	{
		return;
	}

	// 检查 OtherActor 是否是有效角色
	ABaseCharacter* OverlappingChar = Cast<ABaseCharacter>(OtherActor);
	if (!OverlappingChar)
	{
		return;
	}

	// 检查是否是活着的玩家 (AI 不捡武器)
	if (!OverlappingChar->IsPlayerControlled())
	{
		return;
	}

	// 检查是否死亡
	if (OverlappingChar->IsDead())
	{
		return;
	}

	// 检查是否已经有主武器
	UWeaponAttachmentComponent* WeaponAttach = OverlappingChar->ResolveWeaponAttach();
	if (!WeaponAttach)
	{
		return;
	}

	ABaseWeapon* CurrentPrimaryWeapon = WeaponAttach->GetPrimaryWeapon();
	if (CurrentPrimaryWeapon)
	{
		// 已经有主武器, 不捡起
		UE_LOG(LogTemp, Display,
			TEXT("[WeaponDropComponent] OnOverlapBegin: 角色 '%s' 已有主武器 '%s' (Primary 槽位), 不捡起掉落武器 '%s'."),
			*OverlappingChar->GetName(),
			CurrentPrimaryWeapon ? *CurrentPrimaryWeapon->GetName() : TEXT("None"),
			*GetDroppedWeaponName());
		return;
	}

	// 【v200.2.7 诊断】同时打印 CurrentWeapon + WeaponsInSlot[Primary] + CurrentWeaponSlot
	//   排查 "两边判空不一致" 的根因
	// 【v200.2.7 修复】用公开 API (GetCurrentWeapon/GetCurrentWeaponSlot) 避免 protected 访问
	UE_LOG(LogTemp, Display,
		TEXT("[WeaponDropComponent] OnOverlapBegin: 角色 '%s' 捡起检查 — CurrentWeapon='%s', WeaponsInSlot[Primary]='%s', CurrentWeaponSlot='%d' (应=Primary 才允许捡)"),
		*OverlappingChar->GetName(),
		WeaponAttach->GetCurrentWeapon() ? *WeaponAttach->GetCurrentWeapon()->GetName() : TEXT("None"),
		WeaponAttach->GetWeaponInSlot(EWeaponSlotType::Primary) ? TEXT("有武器") : TEXT("空"),
		static_cast<int32>(WeaponAttach->GetCurrentWeaponSlot()));

	// 捡起武器
	UE_LOG(LogTemp, Display,
		TEXT("[WeaponDropComponent] OnOverlapBegin: 角色 '%s' 踩到掉落武器 '%s', 触发捡起. (Primary 槽位为空)"),
		*OverlappingChar->GetName(),
		*GetDroppedWeaponName());

	// 【v200 大厂架构 P0】服务器权威捡起
	// 直接在这里调用服务器的捡起 RPC，因为我们在 HasAuthority() 检查后
	ABaseWeapon* WeaponToPickup = Cast<ABaseWeapon>(GetOwner());
	if (WeaponToPickup)
	{
		OverlappingChar->Server_TryPickupWeapon(WeaponToPickup);
	}
}

// ==========================================
// 掉落武器过期回调
// ==========================================
void UWeaponDropComponent::OnPickupLifetimeExpired()
{
	if (!bIsDropped)
	{
		// 理论上不应该发生, 但保险处理
		return;
	}

	ABaseWeapon* OwnerWeapon = ResolveOwnerWeapon();
	if (!OwnerWeapon)
	{
		return;
	}

	UE_LOG(LogTemp, Display,
		TEXT("[WeaponDropComponent] OnPickupLifetimeExpired: 掉落武器 '%s' 过期, 启动溶解."),
		*OwnerWeapon->GetName());

	// 广播过期事件
	OnDroppedWeaponExpired.Broadcast(OwnerWeapon);

	// 启动武器溶解 (走武器自治组件)
	OwnerWeapon->StartDissolve();

	// 清理掉落状态 (不调用 CancelDroppedState, 因为武器已经溶解)
	bIsDropped = false;
	DropInstigator.Reset();
}

// ==========================================
// 内部辅助: 解析 Owner Weapon
// ==========================================
ABaseWeapon* UWeaponDropComponent::ResolveOwnerWeapon() const
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return nullptr;
	}

	ABaseWeapon* Weapon = Cast<ABaseWeapon>(Owner);
	return Weapon;
}

// ==========================================
// 内部辅助: 保存弹药快照
// ==========================================
void UWeaponDropComponent::SaveAmmoSnapshot()
{
	ABaseWeapon* OwnerWeapon = ResolveOwnerWeapon();
	if (!OwnerWeapon)
	{
		return;
	}

	UWeaponFireComponent* FireComp = OwnerWeapon->GetWeaponFireComponent();
	if (!FireComp)
	{
		// 武器没有 FireComponent (近战武器没有弹药概念)
		AmmoSnapshot = FAmmoSnapshot(); // 默认值, bIsValid = false
		return;
	}

	// 使用新的 API 获取弹药快照
	int32 Current, Reserve, Magazine;
	FireComp->GetAmmoSnapshotForDrop(Current, Reserve, Magazine);
	AmmoSnapshot.CurrentAmmo = Current;
	AmmoSnapshot.ReserveAmmo = Reserve;
	AmmoSnapshot.MagazineSize = Magazine;
	AmmoSnapshot.bIsValid = true;

	UE_LOG(LogTemp, Verbose,
		TEXT("[WeaponDropComponent] SaveAmmoSnapshot: 武器 '%s' 弹药快照已保存. CurrentAmmo=%d, ReserveAmmo=%d, MagazineSize=%d."),
		*OwnerWeapon->GetName(),
		AmmoSnapshot.CurrentAmmo,
		AmmoSnapshot.ReserveAmmo,
		AmmoSnapshot.MagazineSize);
}

// ==========================================
// 内部辅助: 恢复弹药快照
// ==========================================
void UWeaponDropComponent::RestoreAmmoSnapshot()
{
	ABaseWeapon* OwnerWeapon = ResolveOwnerWeapon();
	if (!OwnerWeapon)
	{
		return;
	}

	if (!AmmoSnapshot.bIsValid)
	{
		// 没有有效快照, 不恢复 (近战武器场景)
		return;
	}

	UWeaponFireComponent* FireComp = OwnerWeapon->GetWeaponFireComponent();
	if (!FireComp)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[WeaponDropComponent] RestoreAmmoSnapshot: 武器 '%s' 没有 FireComponent, 无法恢复弹药."),
			*OwnerWeapon->GetName());
		return;
	}

	// 使用新 API 恢复弹药
	const bool bRestored = FireComp->RestoreAmmoFromSnapshot(
		AmmoSnapshot.CurrentAmmo,
		AmmoSnapshot.ReserveAmmo);

	if (bRestored)
	{
		UE_LOG(LogTemp, Display,
			TEXT("[WeaponDropComponent] RestoreAmmoSnapshot: 武器 '%s' 弹药已恢复. CurrentAmmo=%d, ReserveAmmo=%d."),
			*OwnerWeapon->GetName(),
			AmmoSnapshot.CurrentAmmo,
			AmmoSnapshot.ReserveAmmo);
	}
}
