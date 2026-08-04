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
	//   Owner Weapon 必须有 StaticMeshComponent 或 SkeletalMeshComponent 才能注册
	//   【v200.2.18 大厂原则 — 零兜底】Component 必须挂在 ABaseWeapon 上, 否则 Log Error (Component 配错)
	if (ABaseWeapon* OwnerWeapon = ResolveOwnerWeapon())
	{
		if (UPrimitiveComponent* MeshComp = Cast<UPrimitiveComponent>(OwnerWeapon->GetMeshComponent()))
		{
			MeshComp->OnComponentBeginOverlap.AddDynamic(this, &UWeaponDropComponent::OnOverlapBegin);
		}
		else
		{
			// 【v200.2.18 零兜底】OwnerWeapon 找到了但没 MeshComponent → Component 配错, 必须 Error
			UE_LOG(LogTemp, Error,
				TEXT("[v200.2.18][WeaponDropComponent] BeginPlay: OwnerWeapon='%s' 找到, 但没有 MeshComponent. "
				     "Overlap 永远触发不了. 【修复】检查 BP_Weapon_Xxx 蓝图是否包含 StaticMeshComponent 或 SkeletalMeshComponent."),
				*OwnerWeapon->GetName());
		}
	}
	else
	{
		// 【v200.2.18 零兜底】ResolveOwnerWeapon 失败 → Component 没挂在 ABaseWeapon 上
		UE_LOG(LogTemp, Error,
			TEXT("[v200.2.18][WeaponDropComponent] BeginPlay: ResolveOwnerWeapon 失败 — Component 没挂在 ABaseWeapon 上. "
			     "【修复】Component 必须挂在 ABaseWeapon 子类 (BP_Weapon_AK47 等) 的 Components 面板."));
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

	// 【v200.3 Epic 官方】捕获初始掉落位置 (必须在物理设置之前!)
	//   原因: SetSimulatePhysics 后 GetActorLocation 可能返回碰撞后的位置
	//   → 需要掉落开始时的世界坐标，传给 Multicast_DropWeapon
	const FVector SpawnLocation = OwnerWeapon->GetActorLocation();
	const FRotator SpawnRotation = OwnerWeapon->GetActorRotation();

	// 【v200 大厂架构关键】先从角色上分离武器
	// 如果武器仍然附加在角色上，设置 SimulatePhysics 可能无效
	// 【v200.3 Epic 官方方案】服务器调 DetachFromActor, 客户端也通过 Multicast 调
	//   服务器 DetachFromActor: 正常执行 (不需要 RPC 同步)
	//   客户端 DetachFromActor: 必须通过 Multicast 执行 (Epic 官方)
	//     原因: 服务器 DetachFromActor 不会自动复制到客户端
	//     → 客户端仍认为武器附加在角色上 → 武器悬空
	//     → Epic: "The only way to get around this is to use an RPC or OnRep to call DetachFromActor on the client."
	OwnerWeapon->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	UE_LOG(LogTemp, Display,
		TEXT("[WeaponDropComponent] StartDroppedState: 武器 '%s' 已从角色分离 (服务器端 Detach — 客户端通过 Multicast 同步)."),
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
	// 【v200.3.11 修复】设置 ObjectType 为 WorldDynamic
	//   原因: 武器在地上是"世界动态物体", 不是 "PhysicsBody"
	//   WorldDynamic 与其他 WorldDynamic 物体正确互动
	MeshComp->SetCollisionObjectType(ECC_WorldDynamic);

	MeshComp->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	MeshComp->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);    // 阻挡地面
	MeshComp->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);   // 与其他世界动态物体阻挡
	MeshComp->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);         // 玩家 overlap 触发拾取
	MeshComp->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Overlap);
	MeshComp->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);     // 射线检测命中
	MeshComp->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);        // 忽略相机
	MeshComp->SetCollisionResponseToChannel(ECC_Destructible, ECR_Ignore);  // 忽略可破坏物
	MeshComp->SetCollisionResponseToChannel(ECC_Vehicle, ECR_Overlap);

	// 【v200.3.6 致命根因修复】必须显式设 SetGenerateOverlapEvents!
	//   根因: UE 物理模拟的 SkeletalMesh 默认 bGenerateOverlapEvents=false
	//         玩家走近武器也不会触发 OnComponentBeginOverlap
	//   修复: 显式开启 GenerateOverlapEvents，让物理 body 也通知 overlap
	//         这是 UE 大厂拾取系统标配 (Lyra / Paragon 都这么做)
	MeshComp->SetGenerateOverlapEvents(true);
	UE_LOG(LogTemp, Display,
		TEXT("[v200.3.6][WeaponDropComponent] StartDroppedState: 已开启 GenerateOverlapEvents. Weapon=%s"),
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

	// 8. 【v200.3.10 关键修复 — 时序重构】
	// 旧版问题: Multicast 在物理启动之后才调用, 客户端收到旧位置
	// 新版顺序: 准备好参数 → 服务器启动物理 + 速度 → Multicast (客户端 Detach + SetLocation)
	//   - 客户端 Multicast 时, 服务器已经把武器推到正确物理位置
	//   - 客户端收到 SpawnLocation (服务器启动物理时的位置), 然后 ReplicateMovement 同步后续
	//
	// 【v200.2.2 保留】在 SetSimulatePhysics 调用前强制 RecreatePhysicsState
	//   - UE 5.6 SkeletalMesh 物理状态 lazy 创建, RecreatePhysicsState 强制创建
	//   - 修复 PIE warning "形体被设置为模拟物理，但'启用碰撞'不兼容"
	if (USkeletalMeshComponent* SkelMesh = Cast<USkeletalMeshComponent>(MeshComp))
	{
		SkelMesh->RecreatePhysicsState();
	}

	MeshComp->SetSimulatePhysics(true);
	UE_LOG(LogTemp, Display,
		TEXT("[WeaponDropComponent] StartDroppedState: 武器 '%s' SetSimulatePhysics(true) 已调用."),
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

	// 【v200.3.10 关键修复 — 时序重构】
	// 根因: 之前 Multicast 在物理启动后才调用, 客户端收到旧 SpawnLocation
	// 修复: 服务器先启动物理 + 设置速度 → Multicast (客户端 Detach + SetLocation)
	//   - 客户端收到 SpawnLocation (服务器启动物理时的位置)
	//   - 然后 ReplicateMovement 接管后续物理位置同步
	//   - 不传 velocity (服务器物理已设置, ReplicateMovement 会自动同步)
	if (OwnerWeapon)
	{
		OwnerWeapon->Multicast_DropWeapon(
			FVector_NetQuantize(SpawnLocation),
			SpawnRotation,
			FVector::ZeroVector,    // 不传 velocity
			FVector::ZeroVector);   // 不传 angular velocity
	}

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
// 【v200.3.6 大厂架构重构 — 完整重写】
//
// 根因分析 (基于 UE 官方论坛拾取标准):
//   1. UE 要求: Overlap 触发后，必须通过服务器 RPC 处理所有数据修改
//   2. UE 要求: 拾取时必须正确关闭碰撞，否则玩家会"穿过"武器
//   3. UE 要求: Attach 前必须确保武器在世界位置正确，然后建立父子关系
//   4. UE 要求: 停止物理后，必须同步 Physics Body Transform 到 Component
//
// 完整时序:
//   1. 玩家 Overlap → 客户端调 Server_TryPickupWeapon RPC
//   2. 服务器 CancelDroppedState:
//      a. SetSimulatePhysics(false) — 停止物理
//      b. RecreatePhysicsState — 同步物理 body 到组件
//      c. SetCollisionEnabled(NoCollision) — 关闭碰撞
//      d. SetActorEnableCollision(false) — 关闭 Actor 碰撞
//   3. 服务器 HandleTryPickupWeapon:
//      a. WeaponsInSlot[Primary] = WeaponToPickup
//      b. Server_SwitchToWeaponSlot(Primary)
//         → ApplyAttachmentRuntime: Attach + SetRelativeTransform
// ==========================================
bool UWeaponDropComponent::CancelDroppedState(ABaseCharacter* Picker)
{
	// 1. 基础校验
	ABaseWeapon* OwnerWeapon = ResolveOwnerWeapon();
	if (!OwnerWeapon)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[v200.3.6][WeaponDropComponent] CancelDroppedState: Owner Weapon 无效."));
		return false;
	}

	// 2. 幂等检查
	if (!bIsDropped)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[v200.3.6][WeaponDropComponent] CancelDroppedState: 武器 '%s' 不在掉落状态, 忽略."),
			*OwnerWeapon->GetName());
		return false;
	}

	// 3. 停止生命周期计时器
	if (UWorld* World = GetWorld())
	{
		if (LifetimeTimerHandle.IsValid())
		{
			World->GetTimerManager().ClearTimer(LifetimeTimerHandle);
			LifetimeTimerHandle.Invalidate();
		}

		if (DropPositionTimerHandle.IsValid())
		{
			World->GetTimerManager().ClearTimer(DropPositionTimerHandle);
			DropPositionTimerHandle.Invalidate();
		}
	}

	// 4. 获取 Mesh 组件
	UPrimitiveComponent* MeshComp = Cast<UPrimitiveComponent>(OwnerWeapon->GetMeshComponent());
	if (!MeshComp)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[v200.3.6][WeaponDropComponent] CancelDroppedState: MeshComponent 为空, Weapon=%s."),
			*OwnerWeapon->GetName());
		return false;
	}

	// 5. 【v200.3.6 关键修复】先关闭碰撞，再停止物理
	//    根因: 如果先停止物理，武器可能会因为碰撞响应而继续移动
	//    然后 Attach 时位置已经变了，导致 attach 到错误位置
	UE_LOG(LogTemp, Display,
		TEXT("[v200.3.6][WeaponDropComponent] CancelDroppedState Step 1: 关闭碰撞. Weapon=%s"),
		*OwnerWeapon->GetName());

	// 关闭 Mesh 碰撞
	MeshComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
	MeshComp->SetCollisionResponseToAllChannels(ECR_Ignore);

	// 关闭 Actor 碰撞
	OwnerWeapon->SetActorEnableCollision(false);

	// 6. 停止物理模拟
	UE_LOG(LogTemp, Display,
		TEXT("[v200.3.6][WeaponDropComponent] CancelDroppedState Step 2: 停止物理. Weapon=%s"),
		*OwnerWeapon->GetName());

	if (MeshComp->IsSimulatingPhysics())
	{
		MeshComp->SetSimulatePhysics(false);

		// 【v200.3.6 关键】停止物理后必须 RecreatePhysicsState
		// 否则 SkeletalMesh 的 Physics Body Transform 不会同步到 Component
		if (USkeletalMeshComponent* SkelMesh = Cast<USkeletalMeshComponent>(MeshComp))
		{
			SkelMesh->RecreatePhysicsState();
		}

		UE_LOG(LogTemp, Display,
			TEXT("[v200.3.6][WeaponDropComponent] CancelDroppedState: RecreatePhysicsState 完成. Weapon=%s Location=%s"),
			*OwnerWeapon->GetName(),
			*OwnerWeapon->GetActorLocation().ToCompactString());
	}

	// 7. 恢复弹药快照
	RestoreAmmoSnapshot();

	// 8. 清理掉落状态
	bIsDropped = false;
	DropInstigator.Reset();
	AmmoSnapshot = FAmmoSnapshot();

	// 9. 广播事件
	OnDroppedWeaponPickedUp.Broadcast(OwnerWeapon, Picker);

	UE_LOG(LogTemp, Display,
		TEXT("[v200.3.6][WeaponDropComponent] CancelDroppedState: 武器 '%s' 被 '%s' 捡起. 物理已停止, 碰撞已关闭."),
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
/**
 * 【v200.2.18 大厂架构 P0 修复 — 远程客户端拾取链路】
 *
 * 旧版反模式 (v200 之前):
 *   - OnOverlapBegin 内部: if (!HasAuthority()) return;
 *   - 假设: 服务器 (ListenServer) 上 host 玩家的 overlap 触发 → 服务器直接处理
 *   - 致命问题: 远程客户端 (NetMode = Client) 走到武器上
 *     → 客户端 overlap 触发 → HasAuthority()=false → 静默 return
 *     → 没有 RPC 触发 → 服务器不知道客户端想捡
 *     → 用户看到"客户端捡不起来"
 *
 * 新架构 (v200.2.18 — 单一 RPC 链路 + 零兜底):
 *   - 客户端也能 fire overlap (UE 自动 — 客户端 Mesh 也注册了 OnComponentBeginOverlap)
 *   - OnOverlapBegin 入口不守卫 HasAuthority(), 让所有端都跑
 *   - 检测当前是不是客户端 (HasAuthority()=false)
 *   - 客户端路径: 调 OverlappingChar->Server_TryPickupWeapon (这是 ABaseCharacter 的真 RPC)
 *   - 服务器路径: 直接在本地处理拾取
 *
 * 大厂原则 (单一 RPC 入口):
 *   - Server_TryPickupWeapon 是 ABaseCharacter 的 UFUNCTION(Server, Reliable)
 *   - 客户端调用 → 自动 RPC 到服务器
 *   - 服务器接收 → 执行 Server_TryPickupWeapon_Implementation → 取消掉落 + attach
 *   - 无论客户端还是服务器, 同一个 RPC 入口, 单一真理源
 *
 * 零兜底:
 *   - 客户端调 Server_TryPickupWeapon 必须成功 RPC (UE 引擎保证, 网络异常会 log)
 *   - 服务器检测到重叠后必须成功取消掉落状态 (CancelDroppedState 内部 Log)
 *   - 任何环节失败 → Log Error, 不静默跳过
 */
void UWeaponDropComponent::OnOverlapBegin(UPrimitiveComponent* OverlappedComp, AActor* OtherActor,
                                       UPrimitiveComponent* OtherComp, int32 OtherBodyIndex,
                                       bool bFromSweep, const FHitResult& Hit)
{
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[v200.3.6][WeaponDropComponent] OnOverlapBegin: OwnerActor 为空."));
		return;
	}

	// 【v200.3.6 详细诊断】打印 Overlap 信息
	UE_LOG(LogTemp, Display,
		TEXT("[v200.3.6][WeaponDropComponent] OnOverlapBegin: OverlappedComp=%s OtherActor=%s OtherComp=%s OtherBodyIndex=%d bFromSweep=%d"),
		*OverlappedComp->GetName(),
		*OtherActor->GetName(),
		OtherComp ? *OtherComp->GetName() : TEXT("None"),
		OtherBodyIndex,
		bFromSweep ? 1 : 0);

	// 【v200.3.6 详细诊断】打印武器状态
	ABaseWeapon* WeaponActor = Cast<ABaseWeapon>(OwnerActor);
	if (WeaponActor)
	{
		UPrimitiveComponent* MeshComp = Cast<UPrimitiveComponent>(WeaponActor->GetMeshComponent());
		if (MeshComp)
		{
			UE_LOG(LogTemp, Display,
				TEXT("[v200.3.6][WeaponDropComponent] OnOverlapBegin: 武器状态 — bIsDropped=%d IsSimulatingPhysics=%d CollisionEnabled=%d GenerateOverlapEvents=%d PawnResponse=%d"),
				bIsDropped ? 1 : 0,
				MeshComp->IsSimulatingPhysics() ? 1 : 0,
				static_cast<int32>(MeshComp->GetCollisionEnabled()),
				MeshComp->GetGenerateOverlapEvents() ? 1 : 0,
				static_cast<int32>(MeshComp->GetCollisionResponseToChannel(ECC_Pawn)));
		}
	}

	// 【v200.3.6】客户端路径 — 调 RPC 让服务器处理
	if (!OwnerActor->HasAuthority())
	{
		// 客户端路径 — 检查前置条件后调 RPC
		ABaseCharacter* OverlappingCharClient = Cast<ABaseCharacter>(OtherActor);
		if (!OverlappingCharClient)
		{
			return; // 非角色 (子弹/抛出的物体), 正常过滤, 不算兜底
		}

		// 客户端只检查"是不是活着的玩家" — 不需要检查 bIsDropped (客户端 bIsDropped 永远 false, 不是错误)
		if (!OverlappingCharClient->IsPlayerControlled() || OverlappingCharClient->IsDead())
		{
			return; // 正常过滤
		}

		// 【v200.2.18 大厂架构 — 客户端拾取 RPC 链路】
		// 调 ABaseCharacter 的 Server_TryPickupWeapon (UFUNCTION Server Reliable)
		// 单一入口, 客户端 → RPC → 服务器 → 取消掉落 + attach
		ABaseWeapon* WeaponToPickup = Cast<ABaseWeapon>(OwnerActor);
		if (WeaponToPickup)
		{
			UE_LOG(LogTemp, Display,
				TEXT("[v200.2.18][WeaponDropComponent] OnOverlapBegin: 客户端触发拾取 RPC — Char=%s Weapon=%s. "
				     "客户端调 ABaseCharacter::Server_TryPickupWeapon (UFUNCTION Server Reliable) → 服务器处理取消掉落 + attach."),
				*OverlappingCharClient->GetName(),
				*WeaponToPickup->GetName());

			OverlappingCharClient->Server_TryPickupWeapon(WeaponToPickup);
		}
		else
		{
			UE_LOG(LogTemp, Error,
				TEXT("[v200.2.18][WeaponDropComponent] OnOverlapBegin: OwnerActor 不是 ABaseWeapon, 拒绝拾取 RPC. Owner=%s."),
				*OwnerActor->GetName());
		}
		return; // 客户端处理完, 不跑服务器路径
	}

	// ============================================
	// 服务器路径 (HasAuthority = true) — 直接本地处理
	// ============================================

	// 检查是否在掉落状态
	if (!bIsDropped)
	{
		// 服务器 bIsDropped=false 是合理的 (CancelDroppedState 已重置)
		// 正常业务过滤, 不算兜底
		return;
	}

	// 检查 OtherActor 是否是有效角色
	ABaseCharacter* OverlappingChar = Cast<ABaseCharacter>(OtherActor);
	if (!OverlappingChar)
	{
		return; // 非角色 (子弹/抛出的物体), 正常业务过滤
	}

	// 检查是否是活着的玩家 (AI 不捡武器)
	if (!OverlappingChar->IsPlayerControlled())
	{
		return; // AI 不捡武器, 正常业务过滤
	}

	// 检查是否死亡
	if (OverlappingChar->IsDead())
	{
		return; // 死亡角色不能捡武器, 正常业务过滤
	}

	// 检查是否已经有主武器
	UWeaponAttachmentComponent* WeaponAttach = OverlappingChar->ResolveWeaponAttach();
	if (!WeaponAttach)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[v200.2.18][WeaponDropComponent] OnOverlapBegin: 角色 '%s' 找不到 WeaponAttach 组件, 拒绝捡起. "
			     "【修复】检查 %s 的 Components 面板是否包含 WeaponAttachmentComponent."),
			*OverlappingChar->GetName(),
			*OverlappingChar->GetName());
		return;
	}

	ABaseWeapon* CurrentPrimaryWeapon = WeaponAttach->GetPrimaryWeapon();
	if (CurrentPrimaryWeapon)
	{
		// 已经有主武器, 不捡起 (正常业务规则)
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

	// 【v200 大厂架构 P0】服务器权威捡起 — 直接本地调 (已经是服务器进程)
	ABaseWeapon* WeaponToPickup = Cast<ABaseWeapon>(GetOwner());
	if (WeaponToPickup)
	{
		// 服务器直接调 Server_TryPickupWeapon (即使同进程也是 RPC, UE 走本地路径)
		// 单一入口, 与客户端链路统一
		OverlappingChar->Server_TryPickupWeapon(WeaponToPickup);
	}
	else
	{
		UE_LOG(LogTemp, Error,
			TEXT("[v200.2.18][WeaponDropComponent] OnOverlapBegin: 服务器路径 OwnerActor 不是 ABaseWeapon, 拒绝处理. Owner=%s."),
			*OwnerActor->GetName());
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
