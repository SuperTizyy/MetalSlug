// ==========================================
// UHealthRegenComponent 实现
// 【2026-07-01 P0 重构】把 BaseCharacter::Tick() 里的回血回蓝逻辑独立化
// 【2026.07.26 v100.1 大厂架构】母体"待机回血"扩展
// ==========================================
#include "Components/HealthRegenComponent.h"
#include "Components/HealthComponent.h"
#include "Components/EnergyComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"
#include "TimerManager.h"

UHealthRegenComponent::UHealthRegenComponent()
{
	// 必须 Tick - 每帧检测移动状态和驱动状态机
	// 注: 实际回血用 SetTimer 整秒节拍 (业务规则: "每秒 30 滴" 精准)
	//      Tick 只负责状态机切换, 不再每帧累加回血
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = 0.0f; // 每帧
}


void UHealthRegenComponent::BeginPlay()
{
	Super::BeginPlay();

	// 初始化 LastMoveTime 为当前时间
	// 这样角色刚开始静止时, 需要等 RegenerationDelay 才开始回血
	if (UWorld* World = GetWorld())
	{
		LastMoveTime = World->GetTimeSeconds();
	}
}


void UHealthRegenComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 清理 Timer 防止悬挂回调
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RegenTickTimerHandle);
	}
	Super::EndPlay(EndPlayReason);
}


void UHealthRegenComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// 防御: 必须有 Owner, 必须有 World, 必须有 Character
	UWorld* World = GetWorld();
	ACharacter* OwnerChar = Cast<ACharacter>(GetOwner());
	if (!World || !OwnerChar)
	{
		return;
	}

	// 防御: 死亡不回复 (HealthComponent 已短路, 这里再次防御性检查)
	if (UHealthComponent* HealthComp = OwnerChar->FindComponentByClass<UHealthComponent>())
	{
		if (HealthComp->IsDead())
		{
			// 大厂原则: 走 SetRegeneratingState 集中入口 (内部 ClearTimer + Broadcast false 委托)
			SetRegeneratingState(false);
			return;
		}
	}

	// 只在服务器跑回复逻辑 (客户端 HealthComponent 已 Replicated, 不需要本地回血)
	// 这样可以避免客户端预测导致的双轨不一致
	if (!OwnerChar->HasAuthority())
	{
		return;
	}

	// 未启用自动回血: 完全跳过
	if (!bEnableAutoRegen || (HealthRegenRate <= 0.0f && EnergyRegenRate <= 0.0f))
	{
		SetRegeneratingState(false);
		return;
	}

	// ============================================================
	// 【v100.3 大厂架构 — 满血状态守卫】业务规则 (用户 2026.07.26 明确: 必须不是满血)
	//
	// 母体回血触发条件 = 4 个必须全满足:
	//   1. **不是满血状态** (IsFullHealth == false) ← 用户强调: 满血时不能激活回血
	//   2. 非移动状态   (IsOwnerMoving == false)
	//   3. 不被攻击 (NotifyDamageTaken 路径)
	//   4. RegenerationDelay 秒后
	//
	// 业务语义 (用户 2026.07.26 反馈原话):
	//   "只要是母体满血状态是不能激活回血的, 只有在不是满血, 不被攻击, 不在移动, 五秒后才能激活回血"
	//
	// v100.3 P1 修复方向: 上一版我用错方向 — 旧版守卫 `!IsFullHealth() → 拒绝` (残血永不能回血, 用户 bug 2 根因)
	//                     新版守卫 `IsFullHealth() → 拒绝` (满血不能回血, 残血才能回血)
	//
	// 满血守卫的双重职责:
	//   - 阻止满血时回血 (避免 Heal 是 no-op 浪费时间 + 防止音效播放)
	//   - 回血到满血线后, 下一帧 TickComponent 检测到满血 → SetRegeneratingState(false) → 自然停止音效
	//
	// 大厂原则 - 单一真理源: "满血" 概念归 HealthComponent 拥有
	//   - HealthRegenComponent 不允许自己写 `CurrentHealth >= MaxHealth`
	//   - 永远调 HealthComp->IsFullHealth() (复用)
	// ============================================================
	if (UHealthComponent* HealthComp = OwnerChar->FindComponentByClass<UHealthComponent>())
	{
		if (HealthComp->IsFullHealth())
		{
			// 满血: 拒绝回血, 走状态机集中入口 (ClearTimer + Broadcast false → 音效停)
			// 大厂原则: 状态机集中入口, 不直接 ClearTimer, 让 SetRegeneratingState 统一处理 + 触发事件
			SetRegeneratingState(false);
			return;
		}
	}

	// 检测移动状态
	if (IsOwnerMoving())
	{
		// 在移动: 更新 LastMoveTime, 打断回血
		LastMoveTime = World->GetTimeSeconds();
		SetRegeneratingState(false);
		return;
	}

	// 静止: 检查是否过了 RegenerationDelay
	float TimeSinceLastMove = World->GetTimeSeconds() - LastMoveTime;
	if (TimeSinceLastMove < RegenerationDelay)
	{
		// 还在等待期: 不修改状态 (保持之前状态, 由上面 if 分支保证进入此处时已 false)
		return;
	}

	// 静止且已过延迟 → 开始回血 (状态切换)
	// 大厂原则: SetRegeneratingState 内部会启动 SetTimer 整秒节拍
	SetRegeneratingState(true);
}


void UHealthRegenComponent::NotifyDamageTaken()
{
	// 受伤后立即打断回血, 重新计时
	// 大厂原则: 走 SetRegeneratingState 集中入口 (内部 ClearTimer + Broadcast false 委托)
	SetRegeneratingState(false);
	if (UWorld* World = GetWorld())
	{
		LastMoveTime = World->GetTimeSeconds();
	}
}


void UHealthRegenComponent::ResetRegenerationState()
{
	// 重生/复活时调用: 重置所有回复状态
	SetRegeneratingState(false);
	if (UWorld* World = GetWorld())
	{
		LastMoveTime = World->GetTimeSeconds();
	}
}


bool UHealthRegenComponent::IsOwnerMoving() const
{
	ACharacter* OwnerChar = Cast<ACharacter>(GetOwner());
	if (!OwnerChar)
	{
		return false;
	}

	// 通过速度向量判断移动
	// 注意: 速度是上一帧累积的, 即使物理引擎给了 0.01 的小数值也算移动
	FVector Velocity = OwnerChar->GetVelocity();
	return !Velocity.IsNearlyZero(0.1f);
}


// ==========================================
// 【v100.1 大厂架构】回血状态机 + 整秒节拍
// ==========================================

/**
 * SetRegeneratingState — 集中状态切换入口
 *
 * 大厂原则:
 *   - 单一入口: 所有"开始/停止回血"都走这里 (TickComponent / NotifyDamageTaken / 死亡)
 *   - 状态真变化才 Broadcast (避免每帧浪费)
 *   - 切到 true: 启动 SetTimer(1.0s) 整秒节拍回血
 *   - 切到 false: ClearTimer 整秒节拍
 *   - 状态变化时 Broadcast 委托 → BaseCharacter 订阅 → Multicast_PlayRegenSound/StopRegenSound RPC
 */
void UHealthRegenComponent::SetRegeneratingState(bool bNewState)
{
	if (bIsRegenerating == bNewState)
	{
		return; // 状态无变化, 不做任何事 (大厂原则: 零重复 Broadcast)
	}

	bIsRegenerating = bNewState;

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	if (bIsRegenerating)
	{
		// ==========================================
		// 状态切换: false → true
		// 启动整秒节拍回血
		// ==========================================
		World->GetTimerManager().ClearTimer(RegenTickTimerHandle); // 幂等: 清旧 timer
		World->GetTimerManager().SetTimer(
			RegenTickTimerHandle,
			this,
			&UHealthRegenComponent::TimerCallback_RegenTick,
			1.0f, // 整秒节拍 (业务规则: "每秒 30 滴")
			true  // loop
		);
		UE_LOG(LogTemp, Display,
			TEXT("[HealthRegenComponent][v100.1] ★ 开始回血 ★ Owner=%s HealthRate=%.1f/s EnergyRate=%.1f/s"),
			*GetNameSafe(GetOwner()), HealthRegenRate, EnergyRegenRate);
	}
	else
	{
		// ==========================================
		// 状态切换: true → false
		// 停止整秒节拍回血
		// ==========================================
		World->GetTimerManager().ClearTimer(RegenTickTimerHandle);
		UE_LOG(LogTemp, Display,
			TEXT("[HealthRegenComponent][v100.1] ★ 停止回血 ★ Owner=%s"),
			*GetNameSafe(GetOwner()));
	}

	// 触发事件总线 (大厂原则: 唯一委托触发入口)
	// 订阅方: BaseCharacter::BeginPlay → OnRegenStateChanged.AddDynamic → 触发 Multicast_PlayRegenSound/Stop
	OnRegenStateChanged.Broadcast(bIsRegenerating);
}


/**
 * TimerCallback_RegenTick — 整秒节拍回血
 *
 * 大厂原则:
 *   - 整秒触发: 1 次 / 秒 (业务规则 "每秒 30 滴" 精准整数)
 *   - Heal/Add 内部已封顶到 MaxHealth (HealthComponent::Heal: FMath::Min)
 *   - 死亡守卫: HealthComponent->IsDead() 时 Heal 内部短路
 *   - 零冗余: 不重复检测 IsDead / IsOwnerMoving (TickComponent 已把关)
 */
void UHealthRegenComponent::TimerCallback_RegenTick()
{
	ACharacter* OwnerChar = Cast<ACharacter>(GetOwner());
	if (!OwnerChar)
	{
		return;
	}

	// 生命回血 (HealthComponent::Heal 内部 FMath::Min(MaxHealth, ...) 已封顶, 不超总血量)
	UHealthComponent* HealthComp = OwnerChar->FindComponentByClass<UHealthComponent>();
	if (HealthComp && HealthRegenRate > 0.0f)
	{
		HealthComp->Heal(HealthRegenRate); // 整秒: 30 滴 / 秒
	}

	// 能量回复
	UEnergyComponent* EnergyComp = OwnerChar->FindComponentByClass<UEnergyComponent>();
	if (EnergyComp && EnergyRegenRate > 0.0f)
	{
		EnergyComp->Add(EnergyRegenRate);
	}
}
