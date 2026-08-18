// Copyright (c) 2026
// UMotherSlowComponent.cpp — 母体被主武器击中后降速 (镜像 Invincibility 模式)

#include "Combat/MotherSlowComponent.h"

#include "Engine/World.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"


// ==========================================
// 构造 + 生命周期
// ==========================================

UMotherSlowComponent::UMotherSlowComponent()
{
	// 【大厂原则 — 组件同步必备】
	// 1. SetIsReplicatedByDefault(true) — 组件字段能复制到客户端
	// 2. GetLifetimeReplicatedProps 中 DOREPLIFETIME — 具体字段标 Replicated
	// 3. 字段 UPROPERTY 标 Replicated — 真正写入复制列表
	// 三层缺一不可 (v40.0 P0 修复教训)
	SetIsReplicatedByDefault(true);
}

void UMotherSlowComponent::BeginPlay()
{
	Super::BeginPlay();
}

void UMotherSlowComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 防御型设计: 销毁时清 Timer, 防止回调残留
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(SlowTimerHandle);
	}

	// 【v244 P0 大厂架构 — 缓存清理移到 EndPlay (镜像 MotherSkillComponent)】
	// Pawn 销毁时清缓存, 防止残留到下一个生命周期
	// 为什么不放 DeactivateSlow: DeactivateSlow 是慢速状态退出, Pawn 没销毁, 缓存仍有用 (下次激活不再重新缓存)
	// 为什么放 EndPlay: 这是 Pawn 生命周期终点, 缓存失去意义
	CachedBaseMaxWalkSpeed = 0.0f;

	Super::EndPlay(EndPlayReason);
}

void UMotherSlowComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// 【v133.5 大厂架构】镜像 Invincibility:
	//   - bIsSlowed: ReplicatedUsing (OnRep_SlowStateChanged 触发客户端广播)
	//   - SlowExpiresAtWorldTime: 普通 Replicated (辅助字段, 客户端读剩余时间)
	// 注: GetLifetimeReplicatedProps 必须 const, 不能用 const_cast 也不需要 —
	//      DOREPLIFETIME 宏展开就是标准 UE 模式
	DOREPLIFETIME(UMotherSlowComponent, bIsSlowed);
	DOREPLIFETIME(UMotherSlowComponent, SlowExpiresAtWorldTime);
}


// ==========================================
// 公开 API (真理源写入入口)
// ==========================================

void UMotherSlowComponent::ActivateSlow(float Duration, float CurrentMaxWalkSpeed)
{
	// 【v133.5.1 零兜底】配错 ≤ 0 → 立即拒绝 + Log Error, 强制策划修复 ConfigSO
	if (Duration <= 0.0f)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[MotherSlowComponent] ActivateSlow: Duration=%.2f (≤0, 配错). 拒绝激活慢速状态. "
			     "【v133.5.1 修复】请检查 ConfigSO.SlowDurationSeconds 或 PlayerConfigAsset.SlowDurationSeconds (>0)."),
			Duration);
		return;
	}

	// 【v133.5.1 零兜底】缓存速度 ≤ 0 → 拒绝缓存 (防止后续还原拿到 0)
	// 注: CurrentMaxWalkSpeed 是调用方传入, Component 不知道 MovementComponent 存在 (零跨边界)
	if (CurrentMaxWalkSpeed <= 0.0f)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[MotherSlowComponent] ActivateSlow: CurrentMaxWalkSpeed=%.2f (≤0, 配错). 拒绝激活. "
			     "【v133.5.1 修复】调用方应在调本函数前从 CharacterMovement 取 CurrentMaxWalkSpeed. 拒绝缓存 0/负数."),
			CurrentMaxWalkSpeed);
		return;
	}

	// 只对服务器生效 — 客户端不能激活, 只能收 OnRep
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor || !OwnerActor->HasAuthority())
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[MotherSlowComponent] ActivateSlow: World 无效. Owner=%s"),
			*OwnerActor->GetName());
		return;
	}

	// ==========================================
	// 【大厂原则 — 拒绝缩短】仅当新到期时间更晚才更新
	// ==========================================
	// 物理正确性: 时间只能前进, 不能后退
	// - 已经在慢速中, 新 Duration 算出更早到期 → 拒绝 (防"0.1s 反复激活欺骗")
	// - 否则正常激活
	const float Now = World->GetTimeSeconds();
	const float NewExpireTime = Now + Duration;

	if (bIsSlowed && NewExpireTime <= SlowExpiresAtWorldTime + KINDA_SMALL_NUMBER)
	{
		// 已经在慢速中, 且新到期时间不更晚 → 拒绝缩短, 保留原状态
		UE_LOG(LogTemp, Verbose,
			TEXT("[MotherSlowComponent] ActivateSlow: 已在慢速中且新到期时间 (%.2f) 不晚于原到期 (%.2f), 拒绝缩短. Owner=%s"),
			NewExpireTime, SlowExpiresAtWorldTime, *OwnerActor->GetName());
		return;
	}

	// ==========================================
	// 【v133.5.1 大厂架构 — 缓存原速度】激活前缓存调用方传入的 MaxWalkSpeed
	// ==========================================================
	// 大厂原则 — 零跨边界:
	//   - 旧版 (v133.5.1 之前): Component 内部调 OwnerChar->GetCharacterMovement() — 跨边界!
	//   - 新版: 调用方主动传入 CurrentMaxWalkSpeed — Component 不知 MovementComponent 存在
	//   - 这是大厂原则 - "Component 是被动的容器, 主动权在调用方"
	//
	// 设计动机:
	//   - AI 路径没有 MaxWalkSpeed 真理源 (ConfigSO 没这字段)
	//   - 必须缓存原值才能在 DeactivateSlow 时还原
	//   - 玩家路径也会用 (真理源 PlayerConfig 也行, 但缓存更直接)
	//
	// 唯一条件: 第一次激活时才缓存 (避免重复激活时缓存了"已经降速的值")
	//         (后续重复激活时, CachedBaseMaxWalkSpeed > 0, 这里跳过)
	if (CachedBaseMaxWalkSpeed <= 0.0f)
	{
		CachedBaseMaxWalkSpeed = CurrentMaxWalkSpeed;
	}

	// ==========================================
	// 激活慢速状态
	// ==========================================
	// 1. 写字段 (Replicated → 客户端 OnRep 自动同步)
	SlowExpiresAtWorldTime = NewExpireTime;

	// 2. 幂等: 清旧 Timer (避免重入)
	World->GetTimerManager().ClearTimer(SlowTimerHandle);

	// 3. 设新 Timer — 到期自动 ExpireSlow_Internal
	const float RealDuration = FMath::Max(Duration, 0.01f);
	World->GetTimerManager().SetTimer(
		SlowTimerHandle,
		this,
		&UMotherSlowComponent::ExpireSlow_Internal,
		RealDuration,
		false /* 不循环 */
	);

	// 4. 写 bIsSlowed = true (这是触发 OnRep + 服务器主动 Broadcast 的关键)
	//    必须先写 SlowExpiresAtWorldTime 再写 bIsSlowed, 这样 OnRep 时 GetSlowRemainingSeconds 拿到的字段已是新值
	const bool bWasSlowed = bIsSlowed;
	bIsSlowed = true;

	// 5. 服务器主动 Broadcast (与 Invincibility 镜像)
	//    客户端的 OnRep_SlowStateChanged 也会 Broadcast — 双发保证
	if (!bWasSlowed)
	{
		OnSlowStateChanged.Broadcast();

		UE_LOG(LogTemp, Display,
			TEXT("[MotherSlowComponent] ActivateSlow: 已激活慢速. Owner=%s Duration=%.2fs (到期 WorldTime=%.2f, Now=%.2f)"),
			*OwnerActor->GetName(), Duration, NewExpireTime, Now);
	}
	else
	{
		// 已在慢速中, 但属于"延长"情形 — 也 Broadcast 让订阅方刷新计时器
		OnSlowStateChanged.Broadcast();

		UE_LOG(LogTemp, Display,
			TEXT("[MotherSlowComponent] ActivateSlow: 延长慢速. Owner=%s NewDuration=%.2fs (到期由 %.2f 延至 %.2f, TimerValid=%d)"),
			*OwnerActor->GetName(), Duration, NewExpireTime - Duration, NewExpireTime,
			World->GetTimerManager().IsTimerActive(SlowTimerHandle) ? 1 : 0);
	}
}

void UMotherSlowComponent::DeactivateSlow()
{
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor || !OwnerActor->HasAuthority())
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// 1. 清 Timer (幂等)
	World->GetTimerManager().ClearTimer(SlowTimerHandle);

	// 2. 写字段 → Replicated 同步客户端
	const bool bWasSlowed = bIsSlowed;
	bIsSlowed = false;
	SlowExpiresAtWorldTime = 0.0f;

	// 3. 【v244 P0 大厂架构 — 移除清缓存 (与 MotherSkillComponent 完全对称)】
	//
	// 旧版 (v133.5.1 ~ v243) 反模式:
	//   - DeactivateSlow 内 CachedBaseMaxWalkSpeed = 0.0f
	//   - 后果: 母体第二次被击中 → 缓存是 600 (退化默认) 而不是真实母体速度
	//         → 还原路径用缓存 600 → 永远"恢复不了正常母体的速度"
	//
	// 大厂原则 (镜像 MotherSkillComponent v121.3 注释 "不清 CachedBaseMaxWalkSpeed"):
	//   - CachedBaseMaxWalkSpeed 是 Pawn 生命周期内的"原速度真理源" (替代 AI 路径没有 MaxWalkSpeed 真理源)
	//   - DeactivateSlow 是慢速状态退出, 不应销毁 Pawn 级真理源
	//   - 缓存只在 Pawn 销毁时清 (EndPlay 内, 镜像 MotherSkillComponent)
	//
	// 缓存生命周期 (v244 大厂架构):
	//   - ActivateSlow: 第一次激活时缓存 (CachedBaseMaxWalkSpeed == 0 → 设值)
	//   - DeactivateSlow: 不动缓存 (与 MotherSkillComponent 镜像)
	//   - EndPlay (Pawn 销毁): 清缓存 (防御型设计, 防止残留)
	//   - ExecuteDeathLocal 第 0 步 DeactivateSlow: 不动缓存 (Pawn 没销毁, 复用)

	// 4. 服务器主动 Broadcast (客户端 OnRep 也会 Broadcast)
	if (bWasSlowed)
	{
		OnSlowStateChanged.Broadcast();

		UE_LOG(LogTemp, Display,
			TEXT("[MotherSlowComponent] DeactivateSlow: 已取消慢速. Owner=%s (bWasSlowed=true, 已 Broadcast, 缓存保留=%.1f)"),
			*OwnerActor->GetName(), CachedBaseMaxWalkSpeed);
	}
	else
	{
		// 【v133.5.3 修复】即使 bWasSlowed=false 也要 Log, 便于排查"永远不消退" bug
		UE_LOG(LogTemp, Verbose,
			TEXT("[MotherSlowComponent] DeactivateSlow: 已取消慢速(无需 Broadcast). Owner=%s (bWasSlowed=false — 之前已 deactivated, 重复调用, 缓存=%.1f)"),
			*OwnerActor->GetName(), CachedBaseMaxWalkSpeed);
	}
}


// ==========================================
// 状态查询
// ==========================================

float UMotherSlowComponent::GetSlowRemainingSeconds() const
{
	// 【大厂原则 — 单一真理源】派生函数, 不依赖外部 bIsSlowed 副本
	// 内部统一: bIsSlowed=false → 返回 0; 否则计算剩余时间
	if (!bIsSlowed)
	{
		return 0.0f;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return 0.0f;
	}

	const float Now = World->GetTimeSeconds();
	const float Remaining = SlowExpiresAtWorldTime - Now;

	// 防御: 时间已过期 (服务器时序竞争) → 返回 0 并 Log 提示
	if (Remaining < 0.0f)
	{
		return 0.0f;
	}

	return Remaining;
}


// ==========================================
// 内部实现
// ==========================================

void UMotherSlowComponent::ExpireSlow_Internal()
{
	// Timer 触发 — 必然在服务器 (Timer 是服务器跑的)
	// 直接调 DeactivateSlow (幂等)
	DeactivateSlow();
}

void UMotherSlowComponent::OnRep_SlowStateChanged()
{
	// 客户端 OnRep — 仅触发 Broadcast, 让 BaseCharacter 改 MaxWalkSpeed
	// 注意: 客户端代码不写字段, 不调 Timer — 完全由服务器权威
	OnSlowStateChanged.Broadcast();

	UE_LOG(LogTemp, Verbose,
		TEXT("[MotherSlowComponent] OnRep_SlowStateChanged: bIsSlowed=%d (Owner=%s)"),
		bIsSlowed ? 1 : 0,
		GetOwner() ? *GetOwner()->GetName() : TEXT("None"));
}
