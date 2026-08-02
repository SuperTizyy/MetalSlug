// Copyright (c) 2026
// UMotherSkillComponent.cpp — 母体加速技能 (镜像 MotherSlowComponent 模式)

#include "Combat/MotherSkillComponent.h"

#include "Engine/World.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"


// ==========================================
// 构造 + 生命周期
// ==========================================

UMotherSkillComponent::UMotherSkillComponent()
{
	// 【大厂原则 — 组件同步必备】
	// 1. SetIsReplicatedByDefault(true) — 组件字段能复制到客户端
	// 2. GetLifetimeReplicatedProps 中 DOREPLIFETIME — 具体字段标 Replicated
	// 3. 字段 UPROPERTY 标 Replicated — 真正写入复制列表
	// 三层缺一不可 (v40.0 P0 修复教训)
	SetIsReplicatedByDefault(true);
}

void UMotherSkillComponent::BeginPlay()
{
	Super::BeginPlay();
}

void UMotherSkillComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 防御型设计: 销毁时清 Timer, 防止回调残留
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(SkillTimerHandle);
	}

	Super::EndPlay(EndPlayReason);
}

void UMotherSkillComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// 【v119 大厂架构】镜像 MotherSlowComponent:
	//   - bIsSkillActive: ReplicatedUsing (OnRep_SkillActiveChanged 触发客户端广播)
	//   - SkillActiveExpiresAtWorldTime: 普通 Replicated (辅助字段, 客户端读剩余时间)
	//   - SkillCooldownEndTime: 普通 Replicated (冷却时间, UI/BT 都读)
	DOREPLIFETIME(UMotherSkillComponent, bIsSkillActive);
	DOREPLIFETIME(UMotherSkillComponent, SkillActiveExpiresAtWorldTime);
	DOREPLIFETIME(UMotherSkillComponent, SkillCooldownEndTime);
	DOREPLIFETIME(UMotherSkillComponent, TotalCooldownDuration);
}


// ==========================================
// 公开 API (真理源写入入口)
// ==========================================

void UMotherSkillComponent::ActivateSkill(float SpeedMultiplier, float Duration, float Cooldown, float CurrentMaxWalkSpeed)
{
	AActor* OwnerActor = GetOwner();

	// 【v121.3 零兜底】拒绝在 CDO (Class Default Object) 上调用
	// 根因: BP 在 HotReload 后错误地在 CDO 上调用此方法
	if (!OwnerActor || OwnerActor->HasAnyFlags(RF_ClassDefaultObject))
	{
		UE_LOG(LogTemp, Error,
			TEXT("[MotherSkillComponent] ActivateSkill: Owner 为空或是 CDO, 拒绝调用. Owner=%s"),
			OwnerActor ? *OwnerActor->GetName() : TEXT("NULL"));
		return;
	}

	// 【v120 零兜底】SpeedMultiplier ≤ 0 → 拒绝激活
	if (SpeedMultiplier <= 0.0f)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[MotherSkillComponent] ActivateSkill: SpeedMultiplier=%.2f (≤0, 配错). 拒绝激活技能. "
			     "【v120 零兜底】请检查 DA_PlayerConfig → Config|Combat|Mother → MotherSkillSpeedMultiplier (>0)."),
			SpeedMultiplier);
		return;
	}


	// 【v120 零兜底】Duration ≤ 0 → 拒绝激活
	if (Duration <= 0.0f)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[MotherSkillComponent] ActivateSkill: Duration=%.2f (≤0, 配错). 拒绝激活技能. "
			     "【v120 零兜底】请检查 DA_PlayerConfig → Config|Combat|Mother → MotherSkillDurationSeconds (>0)."),
			Duration);
		return;
	}

	// 【v120 零兜底】Cooldown ≤ 0 → 拒绝激活
	if (Cooldown <= 0.0f)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[MotherSkillComponent] ActivateSkill: Cooldown=%.2f (≤0, 配错). 拒绝激活技能. "
			     "【v120 零兜底】请检查 DA_PlayerConfig → Config|Combat|Mother → MotherSkillCooldownSeconds (>0)."),
			Cooldown);
		return;
	}

	// 【v119 零兜底】缓存速度 ≤ 0 → 拒绝激活
	if (CurrentMaxWalkSpeed <= 0.0f)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[MotherSkillComponent] ActivateSkill: CurrentMaxWalkSpeed=%.2f (≤0, 配错). 拒绝激活. "
			     "【v119 零兜底】调用方应在调本函数前从 CharacterMovement 取 CurrentMaxWalkSpeed."),
			CurrentMaxWalkSpeed);
		return;
	}

	// 只对服务器生效 — 客户端不能激活, 只能收 OnRep
	if (!OwnerActor || !OwnerActor->HasAuthority())
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[MotherSkillComponent] ActivateSkill: World 无效. Owner=%s"),
			OwnerActor ? *OwnerActor->GetName() : TEXT("NULL"));
		return;
	}

	const float Now = World->GetTimeSeconds();

	// 【v119 业务规则】冷却中 → 拒绝激活 (不是 bug, 是业务规则)
	if (Now < SkillCooldownEndTime)
	{
		UE_LOG(LogTemp, Verbose,
			TEXT("[MotherSkillComponent] ActivateSkill: 冷却中, 拒绝激活. Owner=%s Now=%.2f CooldownEndTime=%.2f"),
			*OwnerActor->GetName(), Now, SkillCooldownEndTime);
		return;
	}

	// ==========================================
	// 【大厂原则 — 拒绝缩短】仅当新到期时间更晚才更新
	// ==========================================
	const float NewActiveExpireTime = Now + Duration;

	if (bIsSkillActive && NewActiveExpireTime <= SkillActiveExpiresAtWorldTime + KINDA_SMALL_NUMBER)
	{
		// 已经在加速中, 且新到期时间不更晚 → 拒绝缩短, 保留原状态
		UE_LOG(LogTemp, Verbose,
			TEXT("[MotherSkillComponent] ActivateSkill: 已在加速中且新到期时间 (%.2f) 不晚于原到期 (%.2f), 拒绝缩短. Owner=%s"),
			NewActiveExpireTime, SkillActiveExpiresAtWorldTime, *OwnerActor->GetName());
		return;
	}

	// ==========================================
	// 【v119 大厂架构 — 缓存原速度】激活前缓存调用方传入的 MaxWalkSpeed
	// ==========================================
	// 第一次激活时才缓存 (避免重复激活时缓存了"已经加速的值")
	if (CachedBaseMaxWalkSpeed <= 0.0f)
	{
		CachedBaseMaxWalkSpeed = CurrentMaxWalkSpeed;
		CachedSpeedMultiplier = SpeedMultiplier;
	}

	// ==========================================
	// 激活技能状态
	// ==========================================
	// 1. 写字段 (Replicated → 客户端 OnRep 自动同步)
	SkillActiveExpiresAtWorldTime = NewActiveExpireTime;
	SkillCooldownEndTime = Now + Cooldown; // 激活技能时立即开始冷却
	TotalCooldownDuration = Cooldown; // 保存总冷却时间用于 UI 倒计时

	// 2. 幂等: 清旧 Timer
	World->GetTimerManager().ClearTimer(SkillTimerHandle);

	// 3. 设新 Timer — 到期自动 ExpireSkillActive_Internal
	const float RealDuration = FMath::Max(Duration, 0.01f);
	World->GetTimerManager().SetTimer(
		SkillTimerHandle,
		this,
		&UMotherSkillComponent::ExpireSkillActive_Internal,
		RealDuration,
		false /* 不循环 */
	);

	// 4. 写 bIsSkillActive = true (触发 OnRep + 服务器主动 Broadcast)
	const bool bWasActive = bIsSkillActive;
	bIsSkillActive = true;

	// 5. 服务器主动 Broadcast (客户端 OnRep 也会 Broadcast — 双发保证)
	OnSkillStateChanged.Broadcast();

	UE_LOG(LogTemp, Display,
		TEXT("[MotherSkillComponent] ActivateSkill: 已激活加速. Owner=%s SpeedMul=%.2fx Duration=%.2fs Cooldown=%.2fs (Now=%.2f, ExpireActive=%.2f, ExpireCD=%.2f)"),
		*OwnerActor->GetName(), SpeedMultiplier, Duration, Cooldown, Now, NewActiveExpireTime, SkillCooldownEndTime);
}


void UMotherSkillComponent::DeactivateSkill()
{
	AActor* OwnerActor = GetOwner();

	// 只对服务器生效
	if (!OwnerActor || !OwnerActor->HasAuthority())
	{
		return;
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(SkillTimerHandle);
	}

	// 幂等: 未激活时调用是 no-op
	if (!bIsSkillActive)
	{
		return;
	}

	// 清字段
	bIsSkillActive = false;
	SkillActiveExpiresAtWorldTime = 0.0f;
	// 【v121.3 修复】不清 CachedBaseMaxWalkSpeed — 用于恢复原始速度
	// CachedSpeedMultiplier 也保留 (可能被其他系统查询)
	CachedSpeedMultiplier = 1.0f;

	// 服务器主动 Broadcast (客户端 OnRep 也会 — 双发保证)
	OnSkillStateChanged.Broadcast();

	UE_LOG(LogTemp, Display,
		TEXT("[MotherSkillComponent] DeactivateSkill: 已停用加速. Owner=%s"),
		*OwnerActor->GetName());
}


// ==========================================
// 蓝图可读状态
// ==========================================

bool UMotherSkillComponent::IsCooldownReady() const
{
	if (UWorld* World = GetWorld())
	{
		return World->GetTimeSeconds() >= SkillCooldownEndTime;
	}
	return true; // World 无效时默认放行
}

float UMotherSkillComponent::GetSkillActiveRemainingSeconds() const
{
	if (!bIsSkillActive)
	{
		return 0.0f;
	}

	if (UWorld* World = GetWorld())
	{
		const float Remaining = SkillActiveExpiresAtWorldTime - World->GetTimeSeconds();
		return FMath::Max(Remaining, 0.0f);
	}
	return 0.0f;
}

float UMotherSkillComponent::GetSkillCooldownRemainingSeconds() const
{
	if (UWorld* World = GetWorld())
	{
		const float Remaining = SkillCooldownEndTime - World->GetTimeSeconds();
		return FMath::Max(Remaining, 0.0f);
	}
	return 0.0f;
}


// ==========================================
// 内部辅助
// ==========================================

void UMotherSkillComponent::ExpireSkillActive_Internal()
{
	// 服务器端: 停止加速状态 (不清冷却时间)
	// 注意: 不清 SkillCooldownEndTime — 那是技能冷却, 不是加速持续
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor || !OwnerActor->HasAuthority())
	{
		return;
	}

	bIsSkillActive = false;
	SkillActiveExpiresAtWorldTime = 0.0f;
	// 【v121.3 修复】不清 CachedBaseMaxWalkSpeed — 用于恢复原始速度
	// CachedSpeedMultiplier 也保留 (可能被其他系统查询)
	CachedSpeedMultiplier = 1.0f;

	// 服务器主动 Broadcast (客户端 OnRep 也会 — 双发保证)
	OnSkillStateChanged.Broadcast();

	UE_LOG(LogTemp, Display,
		TEXT("[MotherSkillComponent] ExpireSkillActive_Internal: 技能持续时间到期, 停止加速. Owner=%s"),
		*OwnerActor->GetName());
}

void UMotherSkillComponent::OnRep_SkillActiveChanged()
{
	// 客户端收到复制值变化 → Broadcast 给所有订阅方
	OnSkillStateChanged.Broadcast();

	UE_LOG(LogTemp, Verbose,
		TEXT("[MotherSkillComponent] OnRep_SkillActiveChanged: bIsSkillActive=%d Owner=%s"),
		bIsSkillActive ? 1 : 0,
		GetOwner() ? *GetOwner()->GetName() : TEXT("NULL"));
}
