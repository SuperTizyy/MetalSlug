// ==========================================
// 母体出生粒子特效 Component 实现【v99.2 大厂架构 — 零等待】(2026.07.26)
//
// 大厂架构 — 与 InvincibilityFlickerComponent 同款模式(本地播放,不复制):
//   - 组件不复制(视觉不需要跨网络)
//   - 服务器 / 客户端各自本地订阅 Multicast_PlayMutationFX → 各自启动粒子
//   - 双发保证: 服务器本地 RPC 实现也跑一次,客户端 RPC 到达后再跑一次
//
// 【v99.2 零等待重大重构】
//   - 旧版(v99.1)的 IsAllowedByCurrentMatchMode / ResolveGameState 删掉
//   - 原理: GameState.CurrentMatchMode 是 Replicated, 客户端必须等同步 = 1 帧以上延时
//   - 用户反馈: "角色一生成粒子应立刻出现", 旧实现违反该期望
//   - 真理源已经在 Server 侧 URoomSpawnSubsystem::MutatePawnToMother(Pattern锁死只走 Zombie 路径)
//   - 客户端重复校验 = 兜底反模式 = v99.2 删除
//
// 大厂原则 — 零兜底:
//   - 缺资产 / Mesh / World → Log Error + 拒绝播放
//   - 不延长时长(5 秒内重复调用 → 拒绝)
//   - EndPlay 强制清理 Timer + 粒子组件
//   - 不再运行期校验 GameMode (Server 侧入口已锁死)
// ==========================================
#include "Combat/MotherSpawnParticleComponent.h"

// UE 引擎 API
#include "Engine/World.h"                       // GetWorld + TimerManager
#include "Components/SkeletalMeshComponent.h"   // USkeletalMeshComponent
#include "Kismet/GameplayStatics.h"             // SpawnEmitterAttached
#include "Particles/ParticleSystem.h"           // UParticleSystem
#include "Particles/ParticleSystemComponent.h"  // UParticleSystemComponent

// 项目内
#include "Characters/BaseCharacter.h"   // Owner + GetMesh()


// ==========================================
// 1. 构造函数
// ==========================================
/**
 * UMotherSpawnParticleComponent 构造函数
 *
 * 大厂原则 - 零 Tick:
 *   - 粒子生命周期由 Timer 调度,不依赖 Component Tick
 *   - 5 秒到期自动销毁,不需要每帧轮询
 */
UMotherSpawnParticleComponent::UMotherSpawnParticleComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	PrimaryComponentTick.bStartWithTickEnabled = false;

	// 不需要 SetIsReplicatedByDefault:
	//   - 视觉粒子本地播放,服务器 / 客户端各自独立
	//   - 双发保证靠 Multicast RPC 跨网络同步(组件不参与复制)
}


// ==========================================
// 2. UE 生命周期
// ==========================================
/**
 * EndPlay — 防御型清理
 *
 * 根因(防御):
 *   - 母体 Pawn 在 5 秒内被销毁 / 退出关卡时,Timer 可能仍 pending
 *   - 若不清理 → Timer 回调在 Pawn 销毁后跑 → ActiveParticleComponent 指向已销毁的粒子 → 崩溃
 *   - 大厂原则 - 防御: EndPlay 必须无条件清理所有 Timer + 活动粒子
 */
void UMotherSpawnParticleComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	StopSpawnParticle();

	Super::EndPlay(EndPlayReason);
}


// ==========================================
// 3. 公共 API — 播放 / 停止
// ==========================================
/**
 * PlaySpawnParticle — 在母体 Mesh 上附着粒子 5 秒
 *
 * 触发方: ABaseCharacter::Multicast_PlayMutationFX_Implementation(单一入口)
 *
 * 【v99.2 零等待重构】校验链精简:
 *   1. Owner(ABaseCharacter)有效
 *   2. World 有效
 *   3. MotherSpawnParticleSystem 已配置
 *   4. Body Mesh(USkeletalMeshComponent)有效
 *   5. 幂等 — 没有已存在的活动粒子
 *
 * v99.2 删除的校验:
 *   - IsAllowedByCurrentMatchMode: 真理源在 Server 侧 MutatePawnToMother,客户端重复校验 = 兜底
 *   - ResolveGameState: 同上,顺手删除避免未来误用
 *
 * 全部通过 → SpawnEmitterAttached → 设 Timer → 5 秒后 Stop
 * 任一失败 → Log Error + 拒绝播放(不抛异常,不影响 Pawn)
 */
void UMotherSpawnParticleComponent::PlaySpawnParticle()
{
	// ===== 校验层 1: Owner =====
	ABaseCharacter* OwnerChar = ResolveOwnerCharacter();
	if (!OwnerChar)
	{
		// ResolveOwnerCharacter 内部已 Log Error 解释根因
		return;
	}

	// ===== 校验层 2: World =====
	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[MotherSpawnParticle] PlaySpawnParticle: World 无效. Owner=%s. 拒绝播放粒子."),
			*OwnerChar->GetName());
		return;
	}

	// ===== 校验层 3: 粒子资产 =====
	if (!MotherSpawnParticleSystem)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[MotherSpawnParticle] PlaySpawnParticle: MotherSpawnParticleSystem 未配置. "
				 "Owner=%s. "
				 "【修复】在 BP_MuTi 的 MotherSpawnParticle 组件 Class Defaults → MotherSpawnParticleSystem 字段配 Cascade ParticleSystem. "
				 "刀战 BP 不需要配置 — 不会收到该 RPC."),
			*OwnerChar->GetName());
		return;
	}

	// ===== 校验层 4: Body Mesh =====
	USkeletalMeshComponent* BodyMesh = OwnerChar->GetMesh();
	if (!BodyMesh)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[MotherSpawnParticle] PlaySpawnParticle: Owner 没有 SkeletalMesh(Body Mesh 为空). "
				 "Owner=%s. "
				 "【修复】检查 BP_MuTi 是否挂了 SkeletalMeshComponent."),
			*OwnerChar->GetName());
		return;
	}

	// ===== 校验层 5: 幂等 =====
	if (bIsParticleActive || ActiveParticleComponent)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[MotherSpawnParticle] PlaySpawnParticle: 已有活动粒子,拒绝重复生成(幂等). "
				 "Owner=%s, ActiveParticle=%s. "
				 "【根因】上游重复触发 Multicast_PlayMutationFX — 检查 MutatePawnToMother 是否被多次调用."),
			*OwnerChar->GetName(),
			*GetNameSafe(ActiveParticleComponent));
		return;
	}

	// ===== 执行层: SpawnEmitterAttached =====
	//
	// 大厂原则 - 附着而非世界坐标:
	//   - EAttachLocation::SnapToTarget 锁定到 Mesh 组件原点 / Socket
	//   - 粒子随 Pawn 移动 / 骨骼动画
	//
	// 大厂原则 - 自动销毁关闭:
	//   - bAutoDestroy=false
	//   - 粒子生命周期由组件 Timer 统一控制(5 秒),不依赖粒子资产自带循环 / 寿命
	//   - 若粒子资产自带"完成即销毁"且 bAutoDestroy=true,5 秒 Timer 会过早销毁
	UParticleSystemComponent* SpawnedPSC = UGameplayStatics::SpawnEmitterAttached(
		MotherSpawnParticleSystem,
		BodyMesh,
		AttachSocketName,
		FVector::ZeroVector,        // RelativeLocation: 本地空间零偏移
		FRotator::ZeroRotator,       // RelativeRotation: 不旋转
		FVector::OneVector,          // RelativeScale: 1.0
		EAttachLocation::SnapToTarget,
		/*bAutoDestroy=*/false);

	if (!SpawnedPSC)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[MotherSpawnParticle] PlaySpawnParticle: SpawnEmitterAttached 返回 null. "
				 "Owner=%s, Particle=%s, Socket=%s. "
				 "【根因】Cascade 资产内部错误或附着目标无效."),
			*OwnerChar->GetName(),
			*GetNameSafe(MotherSpawnParticleSystem),
			*AttachSocketName.ToString());
		return;
	}

	ActiveParticleComponent = SpawnedPSC;
	bIsParticleActive = true;

	UE_LOG(LogTemp, Display,
		TEXT("[MotherSpawnParticle] PlaySpawnParticle: 粒子已附着. Owner=%s, Particle=%s, Socket=%s, Lifetime=%.2fs."),
		*OwnerChar->GetName(),
		*GetNameSafe(SpawnedPSC),
		*AttachSocketName.ToString(),
		ParticleLifetimeSeconds);

	// ===== Timer 调度: 5 秒后自动销毁 =====
	World->GetTimerManager().SetTimer(
		LifetimeTimerHandle,
		this,
		&UMotherSpawnParticleComponent::HandleLifetimeExpired,
		ParticleLifetimeSeconds,
		/*bLoop=*/false);
}


/**
 * StopSpawnParticle — 立即停止并销毁活动粒子
 *
 * 幂等: 没有活动粒子 → no-op
 *
 * 调用方:
 *   - EndPlay(防御清理)
 *   - 未来可能的"母体立即下线"路径(本项目目前无)
 */
void UMotherSpawnParticleComponent::StopSpawnParticle()
{
	// 清理 Timer(防止 EndPlay 后回调跑)
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(LifetimeTimerHandle);
	}
	LifetimeTimerHandle.Invalidate();

	// 销毁活动粒子
	if (ActiveParticleComponent)
	{
		ActiveParticleComponent->DeactivateSystem();
		ActiveParticleComponent->DestroyComponent();
		ActiveParticleComponent = nullptr;
	}

	bIsParticleActive = false;
}


// ==========================================
// 4. Timer 回调 — 5 秒到期
// ==========================================
/**
 * HandleLifetimeExpired — Timer 到期回调
 *
 * 大厂原则 — Timer 链终点:
 *   - 必须显式 StopSpawnParticle,而非仅清 Timer
 *   - 不允许"Timer 到期但粒子还在跑"的悬挂状态
 *
 * 幂等: StopSpawnParticle 内部已处理"无活动粒子"
 */
void UMotherSpawnParticleComponent::HandleLifetimeExpired()
{
	UE_LOG(LogTemp, Verbose,
		TEXT("[MotherSpawnParticle] HandleLifetimeExpired: 5 秒到期,停止粒子. Owner=%s."),
		*GetNameSafe(GetOwner()));

	StopSpawnParticle();
}


// ==========================================
// 5. 私有辅助 — Lazy Resolve Owner
// ==========================================
/**
 * ResolveOwnerCharacter — 按需 lazy 解析 Owner,避开 BeginPlay 缓存陷阱
 *
 * 大厂原则(同 v40.6 / v40.7 AIAttackComponent / PlayerComboComponent):
 *   - 不缓存 raw pointer: BP archetype 会覆写 C++ 默认字段
 *   - 每次 GetOwner() + Cast<T>: 真理源 = UE 标准 API
 *   - Cast 失败 / GetOwner 无效 → Log Error + return nullptr
 */
ABaseCharacter* UMotherSpawnParticleComponent::ResolveOwnerCharacter() const
{
	const AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[MotherSpawnParticle] ResolveOwnerCharacter: GetOwner() 返回 null. "
				 "Component 必须挂在 Actor 上."));
		return nullptr;
	}

	const ABaseCharacter* OwnerChar = Cast<ABaseCharacter>(OwnerActor);
	if (!OwnerChar)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[MotherSpawnParticle] ResolveOwnerCharacter: Owner '%s' 不是 ABaseCharacter 派生. "
				 "本组件必须挂在 BP_*.uasset (继承 ABaseCharacter) 上. "
				 "实际 OwnerClass=%s."),
			*OwnerActor->GetName(),
			*OwnerActor->GetClass()->GetName());
		return nullptr;
	}

	// Cast 在 const 上下文返回 const,工程上不修改字段,这里做一次解包
	return const_cast<ABaseCharacter*>(OwnerChar);
}
