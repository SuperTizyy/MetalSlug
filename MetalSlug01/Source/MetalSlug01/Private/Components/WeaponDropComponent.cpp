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
// 【v200.4 大厂架构】PMC 用于抛物线阶段 (替代纯 SetSimulatePhysics)
#include "GameFramework/ProjectileMovementComponent.h"
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

	// 【v200.4 大厂架构】解绑 PMC OnProjectileStop 委托
	//   根因: 武器 Actor 销毁时, PMC 可能在 BeginDestroy 中触发最后一次 OnProjectileStop
	//         如果不解除绑定, 回调会调到已销毁的 Component → 悬垂指针崩溃
	//   修复: EndPlay 显式解绑 (UE 反射系统的安全要求)
	if (ABaseWeapon* OwnerWeapon = ResolveOwnerWeapon())
	{
		if (UProjectileMovementComponent* PMC = ResolveProjectileMovement())
		{
			PMC->OnProjectileStop.RemoveDynamic(this, &UWeaponDropComponent::OnProjectileStopHandler);
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
			// 【v200.4.3 强化诊断】打 PMC 实际状态 — 排查 "武器不飞" / "重力失效" / "OnProjectileStop 不触发"
			FString PMCStatus = TEXT("N/A");
			if (UProjectileMovementComponent* PMC = ResolveProjectileMovement())
			{
				const UWorld* W = OwnerWeapon->GetWorld();
				PMCStatus = FString::Printf(
					TEXT("Active=%d Velocity=%s GravityScale=%.2f WorldGravityZ=%.1f MaxSpeed=%.1f BounceStopThresh=%.1f UpdatedComp=%s"),
					PMC->IsActive() ? 1 : 0,
					*PMC->Velocity.ToCompactString(),
					PMC->ProjectileGravityScale,
					W ? W->GetGravityZ() : 0.0f,
					PMC->MaxSpeed,
					PMC->BounceVelocityStopSimulatingThreshold,
					PMC->UpdatedComponent ? *PMC->UpdatedComponent->GetName() : TEXT("None"));
			}

			UE_LOG(LogTemp, Display,
				TEXT("[WeaponDropComponent][Tick 诊断] Weapon=%s Loc=%s 最近玩家=%s 距离=%.0fcm | PMC=%s"),
				*OwnerWeapon->GetName(),
				*WeaponLoc.ToString(),
				*NearestPlayerName,
				MinDist,
				*PMCStatus);
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

	// 【v200.4.2 P0 修复】完全删除老代码 SetSimulatePhysics(true) + 物理速度设置 + 强制 collision 覆盖
	//
	// 【v200.4.2 P0 修复 根因链】
	//   v200.4 重构遗漏: 老 SetSimulatePhysics(true) 没删除 → 武器被物理引擎 + PMC 双重模拟
	//   结果: 物理引擎重力主导, PMC 水平速度被覆盖 → 武器直接下落 (用户 2026.08.05 Session1 反馈)
	//
	// v200.4.2 修复:
	//   - 完全删除老 SetSimulatePhysics(true) 调用
	//   - 完全删除老物理速度/角速度设置
	//   - 物理引擎只在 "BP 没配 PMC 的退化路径" 才启用 (保留在 else 分支)
	//   - 物理引擎在 SettleWeaponOnGround 关闭一次
	//
	// 大厂原则: 单一运动源 — 抛物线阶段只跑 PMC, 落地阶段立即冻结, 永不双重模拟

	// 【v200.4 大厂架构重构 — 用 ProjectileMovementComponent 替代纯 SetSimulatePhysics】
	//
	// 旧 (v200.3.13) 反模式:
	//   1. SetSimulatePhysics(true) + SetPhysicsLinearVelocity + SetPhysicsAngularVelocityInDegrees
	//   2. 物理引擎持续模拟 → 武器位置持续抖动
	//   3. 即使 0.5s 后 SettleWeaponOnGround 把武器压到地面, 物理引擎下一帧又把它推回去
	//   4. ReplicateMovement 同步 → 客户端看到穿透地下的位置 → "客户端陷进地面"
	//
	// v200.4 改造 (UE 官方最优解):
	//   - 抛物线阶段: ProjectileMovementComponent (内置网络预测, 速度低于阈值自动停)
	//   - 落地判定: PMC->OnProjectileStop 回调 → 触发 SettleWeaponOnGround
	//   - 沉淀阶段: 关闭 PMC + 关闭物理 + LineTrace 贴齐 + Multicast 冻结
	//   - 关键: 落地后立即关闭物理, 服务器位置永远不变 → 客户端不会插值陷地

	// 9. 计算掉落方向 (沿用旧版 LaunchForwardSpeed / LaunchUpwardSpeed 字段)
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

	// 10. 【v200.4.1 大厂架构修复】启动 ProjectileMovementComponent (替代 SetSimulatePhysics)
	UProjectileMovementComponent* PMC = ResolveProjectileMovement();
	if (PMC)
	{
		// BP 子类已配 PMC → 用 PMC 抛物线 (UE 官方最优解)
		//
		// 【v200.4.1 P0 修复】根因: PMC 默认 bInitialVelocityInLocalSpace=true
		//   直接设 Velocity=WorldDir 会被当成 LOCAL SPACE 处理 → Actor 朝向歪时武器乱飞
		//   用户反馈: "扔枪没有向前扔, 而是直接向下掉落" 正是 Actor 旋转 → 世界方向被扭曲 → 重力主导
		// 修复: SetVelocityInLocalSpace (UE 官方推荐 API, 自动按 Actor 朝向转换)
		//   - 内部实现: Velocity = UpdatedComponent->GetComponentToWorld().TransformVectorNoScale(LocalVel)
		//   - 这样 LocalVel=(1,0,0) 总是世界 X+, LocalVel=(0,0,1) 总是世界 Z+
		//   - 用户视角下: LaunchDirection = Forward * LaunchForwardSpeed + Z * LaunchUpwardSpeed
		//     Forward = DropInstigator->GetActorForwardVector() (世界坐标)
		//   - 需要转换: LocalVel = WorldToLocal(LaunchDirection) → SetVelocityInLocalSpace(LocalVel)
		//   - 然后 PMC 内部再做 LocalToWorld → 还原成 LaunchDirection (世界坐标)

		PMC->SetActive(true);
		PMC->SetUpdatedComponent(MeshComp); // UE 5 官方推荐: 重新指向更新组件

		// 【v200.4.5 P0 修复 — 终极根因】UE PMC 与 SimulatePhysics 互斥!
		//   UE 官方文档 (Movement Components):
		//     "If the Updated Component is simulating physics, only the initial launch parameters
		//      (when initial velocity is non-zero) will affect the projectile, and the physics
		//      simulation will take over from there."
		//   根因链 (v200.4.4 仍然失败的根因):
		//     1. BP_Weapon_AK47 BP 子类设了 "Always Create Physics State = true" (UE 默认 SkeletalMesh 推荐)
		//     2. SkeletalMeshComponent.IsSimulatingPhysics() == true (即使我们没主动调 SetSimulatePhysics(true))
		//     3. PMC 启动时检测 UpdatedComponent.IsSimulatingPhysics=true → 拒绝控制
		//     4. 物理引擎接管 → 应用初速度 (Tick 1 Velocity 仍是 100, 因为 PMC 退出控制, 物理引擎只应用了初速度)
		//     5. 物理引擎用 SkeletalMesh 物理资源 + LinearDamping 应用"软重力" → 武器 1 秒只下落 130cm (而不是 490cm)
		//   修复: SetActive 之前显式 SetSimulatePhysics(false) + 强制 RecreatePhysicsState 让 PMC 控制生效
		//   UE 官方推荐路径: StaticMesh + PMC 或 SkeletalMesh + PMC 必须 SimulatePhysics=false
		MeshComp->SetSimulatePhysics(false);
		if (USkeletalMeshComponent* SkelMesh = Cast<USkeletalMeshComponent>(MeshComp))
		{
			SkelMesh->RecreatePhysicsState(); // 强制同步
		}
		UE_LOG(LogTemp, Display,
			TEXT("[v200.4.5][WeaponDropComponent] StartDroppedState: 已强制 MeshComp->SetSimulatePhysics(false) — 让 PMC 完全控制 (否则物理引擎会接管 → 武器下沉过快/过慢)"));

		// 【v200.4.3 P0 修复】PMC 默认值陷阱!
		//   UE C++ 默认值: InitialSpeed=0, MaxSpeed=0
		//   如果 BP 子类 (BP_Weapon_AK47) 没在 DetailsPanel 显式改这两个值
		//   那么即使我们 SetVelocityInLocalSpace(111 cm/s) 也会被 MaxSpeed=0 截断到 0
		//   结果: 只有重力向下, 完全不前飞 (用户 2026.08.05 Session1 反馈)
		//
		// 修复: 在运行时强制覆盖为策划配置的 LaunchForwardSpeed + LaunchUpwardSpeed (200 cm/s)
		//   - 不能依赖 BP 配, 大厂原则 — 运行时必填字段永远不能拿 BP 默认值兜底
		//   - 强制 MaxSpeed = 期望飞行速度的 1.5 倍 (避免 LaunchDirection 的长度被截断)
		//   - 强制 InitialSpeed = 期望飞行速度 (保证 PMC 第一次 Tick 用正确的速度)
		const float ConfiguredSpeed = FMath::Max(LaunchForwardSpeed, LaunchUpwardSpeed);
		const float TargetMaxSpeed = FMath::Max(ConfiguredSpeed * 1.5f, 200.0f); // 至少 200 cm/s

		PMC->MaxSpeed = TargetMaxSpeed;
		PMC->InitialSpeed = FMath::Max(PMC->InitialSpeed, ConfiguredSpeed); // 不动 BP 配的 InitialSpeed, 至少达到 ConfiguredSpeed

		// 【v200.4.4 P0 修复】直接设世界空间 Velocity (绕开 SetVelocityInLocalSpace)
		//   根因链:
		//     SetVelocityInLocalSpace(LocalVel) 内部 = LocalToWorld(LocalVel) using UpdatedComponent's transform
		//     双转换 (InverseTransform + LocalToWorld) 抵消后 = 世界 LaunchDirection (理论上)
		//     但实际:
		//       1. SetUpdatedComponent(MeshComp) 刚调, UpdatedComponent transform 可能未刷新
		//       2. InverseTransform 用 OwnerWeapon transform, SetVelocityInLocalSpace 用 MeshComp transform
		//       3. 两者不一致 (Actor transform != RootComponent transform != MeshComp transform) → 旋转偏差
		//     UE 官方建议 (2026 文章):
		//       直接设 Velocity = ThrowDirection * Speed + UpVector * UpSpeed (世界空间)
		//       不绕 LocalToWorld, 直接走世界系, 不出错
		//   验证证据: Session1 日志看到 WorldVelocity=(X=148, Y=-21, Z=100), 水平方向 Z=100 都对
		//            但 Tick 1 (1.012s) Z=1314, Tick 2 (4.276s) Z=1303 — 4 秒只落 11cm
		//            实际应该是 v_z = 100 - 980*4 = -3820 cm/s, z = 起始 - 7000cm
		//            → 武器根本没在 Tick! 或 Tick 没应用重力!
		PMC->Velocity = LaunchDirection; // 直接世界空间 — 简单稳定, 不需要双转换
		PMC->bRotationFollowsVelocity = true; // 视觉上让枪头朝速度方向 (UE 官方建议)

		PMC->BounceVelocityStopSimulatingThreshold = ProjectileStopVelocityThreshold;
		PMC->ProjectileGravityScale = 1.0f; // 【v200.4.4】UE 默认重力缩放, 防止 BP 子类设了 0 → 武器不落
		PMC->UpdateComponentVelocity(); // UE 官方推荐: 设置后调用一次, 同步组件速度

		// 绑定 OnProjectileStop (v200.4 新增)
		//   注意: RemoveDynamic 先解绑防重复 (多次 StartDroppedState 调用时)
		PMC->OnProjectileStop.RemoveDynamic(this, &UWeaponDropComponent::OnProjectileStopHandler);
		PMC->OnProjectileStop.AddDynamic(this, &UWeaponDropComponent::OnProjectileStopHandler);

		UE_LOG(LogTemp, Display,
			TEXT("[v200.4.4][WeaponDropComponent] StartDroppedState: 武器 '%s' 启动 PMC 抛物线 (世界速度). ")
			TEXT("WorldVelocity=%s, MaxSpeed=%.1f, InitialSpeed=%.1f, RotationFollowsVel=%d, StopThreshold=%.1f"),
			*OwnerWeapon->GetName(),
			*LaunchDirection.ToCompactString(),
			PMC->MaxSpeed,
			PMC->InitialSpeed,
			PMC->bRotationFollowsVelocity ? 1 : 0,
			ProjectileStopVelocityThreshold);
	}
	else
	{
		// BP 子类没配 PMC → 退化到旧版 SetSimulatePhysics 路径
		//   - 防御性: 如果策划忘配 PMC, 武器仍能掉落 (虽然会有穿透问题)
		//   - 大厂原则: 不静默跳过, Log Warning 让策划知道
		UE_LOG(LogTemp, Warning,
			TEXT("[v200.4][WeaponDropComponent] StartDroppedState: 武器 '%s' 没有 ProjectileMovementComponent 子对象! "
			     "退化到 SetSimulatePhysics 路径 (可能会有穿透问题). "
			     "【零兜底】修复: 在 BP 武器蓝图 Components 面板添加 ProjectileMovementComponent 子对象."),
			*OwnerWeapon->GetName());

		// 旧版物理引擎抛物线 (保留作为 fallback)
		if (USkeletalMeshComponent* SkelMesh = Cast<USkeletalMeshComponent>(MeshComp))
		{
			SkelMesh->RecreatePhysicsState();
		}

		MeshComp->SetSimulatePhysics(true);

		OwnerWeapon->SetActorEnableCollision(true);
		MeshComp->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);

		MeshComp->SetPhysicsLinearVelocity(LaunchDirection);

		FVector RandomTorque = FVector(
			FMath::RandRange(-500.0f, 500.0f),
			FMath::RandRange(-500.0f, 500.0f),
			FMath::RandRange(-500.0f, 500.0f)
		);
		MeshComp->SetPhysicsAngularVelocityInDegrees(RandomTorque);

		UE_LOG(LogTemp, Display,
			TEXT("[v200.4][WeaponDropComponent] StartDroppedState: 武器 '%s' (退化路径) 已启用 SetSimulatePhysics."),
			*OwnerWeapon->GetName());
	}

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
			FVector_NetQuantize(LaunchDirection),  // 【v200.4】传 velocity 给客户端, 让客户端启动本地 PMC 预测
			FVector::ZeroVector);                   // 不传 angular velocity (PMC 旋转由重力 + 命中决定)
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

	// 【v200.4 大厂架构重构 — 删除了 v200.3.13 的 0.5s SettleTimer】
	//   旧版: 0.5s 后用 LineTrace 找地面 + 服务器 SetActorLocation
	//         问题: 物理引擎持续模拟, Settle 后下一帧又被推回去
	//   新版: PMC->OnProjectileStop 回调 (Step 10 绑定) → 自动触发 SettleWeaponOnGround
	//         落地瞬间立即冻结, 不会再陷

	UE_LOG(LogTemp, Display,
		TEXT("[WeaponDropComponent] StartDroppedState: 武器 '%s' 已进入掉落状态. DropInstigator='%s', PickupRadius=%.1f, Lifetime=%.1f秒."),
		*OwnerWeapon->GetName(),
		InDropInstigator ? *InDropInstigator->GetName() : TEXT("None"),
		PickupRadius,
		PickupLifetimeSeconds);
}

// ==========================================
// ResolveProjectileMovement
// 【v200.4 大厂架构】懒加载解析 PMC (与 v40.6 WeaponFireComponent 同模式)
// ==========================================
UProjectileMovementComponent* UWeaponDropComponent::ResolveProjectileMovement() const
{
	ABaseWeapon* OwnerWeapon = ResolveOwnerWeapon();
	if (!OwnerWeapon)
	{
		return nullptr;
	}

	// 不缓存, 每次按需 FindComponentByClass — 避免 BP 子类初始化时序问题
	return OwnerWeapon->FindComponentByClass<UProjectileMovementComponent>();
}

// ==========================================
// OnProjectileStopHandler
// 【v200.4 大厂架构】PMC 落地回调 → 转发到 SettleWeaponOnGround
// ==========================================
void UWeaponDropComponent::OnProjectileStopHandler(const FHitResult& HitResult)
{
	ABaseWeapon* OwnerWeapon = ResolveOwnerWeapon();
	if (!OwnerWeapon)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[v200.4][WeaponDropComponent] OnProjectileStopHandler: OwnerWeapon 无效, 跳过沉淀."));
		return;
	}

	// 服务器权威: 客户端不应该走到这里 (客户端 PMC 不参与权威逻辑)
	if (!OwnerWeapon->HasAuthority())
	{
		UE_LOG(LogTemp, Verbose,
			TEXT("[v200.4][WeaponDropComponent] OnProjectileStopHandler: 客户端路径, 跳过 (服务器权威). Weapon=%s"),
			*OwnerWeapon->GetName());
		return;
	}

	UE_LOG(LogTemp, Display,
		TEXT("[v200.4][WeaponDropComponent] OnProjectileStopHandler: PMC 触发落地. Weapon=%s, HitLocation=%s"),
		*OwnerWeapon->GetName(),
		*HitResult.ImpactPoint.ToCompactString());

	// 转发到 SettleWeaponOnGround (单一沉淀入口)
	SettleWeaponOnGround(OwnerWeapon);
}

// ==========================================
// 【v200.3.13】物理静止后地面贴齐
//
// 大厂架构 (服务器权威):
//   - 0.5s 后执行: UE 5 物理引擎 penetration resolution 已完成, 武器静止或近静止
//   - 从武器当前位置向下 LineTrace 500cm 找 WorldStatic 地面
//   - 命中 → 把武器位置 Z 抬高到 GroundZ + GroundOffsetCm
//   - SetActorLocation + TeleportPhysics (物理引擎同步更新 body transform)
//   - ReplicateMovement 自动同步给所有客户端 → 客户端精确贴地
//
// 边界处理 (零兜底):
//   - 没找到地面 (LineTrace 未命中) → Log Warning + 不动 (避免把武器传送到错位置)
//   - OwnerWeapon 无效 → return
//   - 不在 HasAuthority() → return (客户端不能改位置, 服务器权威)
//   - GroundOffsetCm <= 0 → 使用硬编码 1.0 (避免配置错导致负穿透)
//
// ==========================================
void UWeaponDropComponent::SettleWeaponOnGround(ABaseWeapon* OwnerWeapon)
{
	if (!IsValid(OwnerWeapon))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[v200.4][WeaponDropComponent] SettleWeaponOnGround: OwnerWeapon 已失效, 跳过贴齐."));
		return;
	}

	if (!OwnerWeapon->HasAuthority())
	{
		// 客户端不应该调到这个函数 — 这是 OnProjectileStop 绑定的服务器回调
		return;
	}

	UWorld* World = OwnerWeapon->GetWorld();
	if (!World)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[v200.4][WeaponDropComponent] SettleWeaponOnGround: World 无效."));
		return;
	}

	// 【v200.4 大厂架构 阶段 2：落地点沉淀】
	//
	// 旧版 (v200.3.13) 问题:
	//   1. 服务器 SetSimulatePhysics(true) 持续模拟
	//   2. 0.5s 后 SetActorLocation 把武器压到地面
	//   3. 物理引擎下一帧又把它推回去
	//   4. ReplicateMovement 同步 → 客户端看到穿透地下的位置
	//
	// v200.4 改造 (UE 官方最优解):
	//   1. 关闭 PMC (停止弹道模拟)
	//   2. 关闭物理 (落地后不需要物理引擎)
	//   3. LineTrace 找地面 (用 HitResult.ImpactPoint 优先, 失败再用当前位置)
	//   4. SetActorLocation + TeleportPhysics (冻结位置)
	//   5. Multicast_FreezeWeaponTransform 通知客户端冻结 (ReplicateMovement 不复制 PMC 状态)

	// 1. 关闭 ProjectileMovementComponent
	UProjectileMovementComponent* PMC = ResolveProjectileMovement();
	if (PMC)
	{
		PMC->StopMovementImmediately();
		PMC->SetActive(false);
		PMC->SetUpdatedComponent(nullptr); // UE 5 官方推荐: 解耦 UpdatedComponent, 防止后续 tick 重新激活
		PMC->Velocity = FVector::ZeroVector; // 清零速度, 防止残留

		UE_LOG(LogTemp, Display,
			TEXT("[v200.4][WeaponDropComponent] SettleWeaponOnGround: 关闭 PMC. Weapon=%s"),
			*OwnerWeapon->GetName());
	}

	// 2. 关闭物理引擎
	UMeshComponent* MeshComp = Cast<UMeshComponent>(OwnerWeapon->GetMeshComponent());
	if (MeshComp && MeshComp->IsSimulatingPhysics())
	{
		MeshComp->SetSimulatePhysics(false);

		// 【v200.3.6 关键】停止物理后必须 RecreatePhysicsState
		// 否则 SkeletalMesh 的 Physics Body Transform 不会同步到 Component
		if (USkeletalMeshComponent* SkelMesh = Cast<USkeletalMeshComponent>(MeshComp))
		{
			SkelMesh->RecreatePhysicsState();
		}
	}

	// 3. LineTrace 找地面 (UE 5 大厂标准 — 用 PMC HitResult 优先, 没 HitResult 时本地 trace)
	const FVector StartLoc = OwnerWeapon->GetActorLocation();
	const FVector TraceEndLoc = StartLoc - FVector(0.0f, 0.0f, 200.0f); // 200cm (比 500cm 短, 因为 OnProjectileStop 时武器已经接近地面)

	FHitResult GroundHit;
	FCollisionQueryParams QueryParams(SCENE_QUERY_STAT(WeaponSettle), false, OwnerWeapon);
	QueryParams.bTraceComplex = false;

	const bool bHit = World->LineTraceSingleByChannel(
		GroundHit,
		StartLoc,
		TraceEndLoc,
		ECC_WorldStatic,
		QueryParams
	);

	if (!bHit)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[v200.4][WeaponDropComponent] SettleWeaponOnGround: 武器 '%s' 向下 200cm 没找到地面 (位置=%s). 跳过贴齐, 武器保留当前位置."),
			*OwnerWeapon->GetName(),
			*StartLoc.ToCompactString());
		return;
	}

	// 4. 计算贴齐位置 (GroundZ + GroundOffsetCm)
	const float SafeOffset = FMath::Max(GroundOffsetCm, 0.0f);
	const FVector SettledLocation(
		StartLoc.X,
		StartLoc.Y,
		GroundHit.Location.Z + SafeOffset
	);

	const float DeltaZ = SettledLocation.Z - StartLoc.Z;

	// 5. TeleportPhysics 模式 SetActorLocation — 关闭物理后不需要 TeleportPhysics
	//    (v200.3.13 用 TeleportPhysics 是因为还 simulate physics, v200.4 已关闭)
	OwnerWeapon->SetActorLocation(
		SettledLocation,
		false,
		nullptr,
		ETeleportType::TeleportPhysics  // 保留 TeleportPhysics 防御性 — 即使物理残留也能 teleport
	);

	// 6. 【关键 v200.4】Multicast 通知客户端冻结
	//    ReplicateMovement 复制 Actor.Location 但不复制 PMC/物理状态
	//    必须显式 Multicast 通知客户端关闭本地 PMC + 关闭物理 + SetActorLocation
	OwnerWeapon->Multicast_FreezeWeaponTransform(
		FVector_NetQuantize(SettledLocation),
		OwnerWeapon->GetActorRotation()
	);

	UE_LOG(LogTemp, Display,
		TEXT("[v200.4][WeaponDropComponent] SettleWeaponOnGround: 武器 '%s' 落地沉淀完成. ")
		TEXT("原位置=%s, 冻结位置=%s, 地面 Z=%.2f, DeltaZ=%.2fcm (GroundOffsetCm=%.2f). ")
		TEXT("已 Multicast 通知客户端冻结."),
		*OwnerWeapon->GetName(),
		*StartLoc.ToCompactString(),
		*SettledLocation.ToCompactString(),
		GroundHit.Location.Z,
		DeltaZ,
		SafeOffset);
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

	// 【v200.4 大厂架构】解绑 PMC OnProjectileStop (玩家提前捡起时)
	//   防止"贴在玩家手上的武器"被 PMC 误触发 OnProjectileStop 回调
	if (UProjectileMovementComponent* PMC = ResolveProjectileMovement())
	{
		PMC->StopMovementImmediately();
		PMC->SetActive(false);
		PMC->SetUpdatedComponent(nullptr);
		PMC->OnProjectileStop.RemoveDynamic(this, &UWeaponDropComponent::OnProjectileStopHandler);
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
