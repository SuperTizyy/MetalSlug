// ==========================================
// UHealthComponent 实现 【2026-06-15 重构: 数据权威持有者】
// ==========================================
#include "Components/HealthComponent.h"
#include "Logs/MetalSlugLogChannels.h"
#include "Net/UnrealNetwork.h"

// 【2026.07.11 P0 新增】无敌期需要 World (GetTimeSeconds) 和 TimerHandle
//   - 包含 Engine/World.h 是 UE 标准做法 (而非依赖前置头)
//   - TimerManager.h 是 FTimerHandle 完整定义所在
#include "Engine/World.h"
#include "TimerManager.h"

// 【v40.9 新增】GetServerWorldTimeSeconds 需要 GameStateBase 完整定义
//   - AGameStateBase::GetServerWorldTimeSeconds() 是 UE 内置 API
//   - 客户端通过内置 RPC 自动补偿网络延迟
//   - 不依赖前置 transitive include (避免 refactor 编译失败)
#include "GameFramework/GameStateBase.h"

UHealthComponent::UHealthComponent()
{
	// 【2026-06-15 新增】: 启用复制 (修复前未开启,服务器修改客户端看不到)
	SetIsReplicatedByDefault(true);

	// Component 自身不需要 Tick (血量改变通过事件通知)
	PrimaryComponentTick.bCanEverTick = false;
}


void UHealthComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// 【2026-06-15 新增】: 同步两个权威字段
	DOREPLIFETIME(UHealthComponent, CurrentHealth);
	DOREPLIFETIME(UHealthComponent, bIsDead);

	// 【2026.07.11 P0 新增】: 同步无敌期字段
	//   - bIsInvincible: 必须 ReplicatedUsing, 客户端 OnRep 自动广播事件
	//   - InvincibilityExpiresAtWorldTime: 普通 Replicated 即可, 客户端只读计算剩余
	//   - InvincibilityTotalDuration: 【v40.7 新增】进度条总时长, 客户端只读
	DOREPLIFETIME(UHealthComponent, bIsInvincible);
	DOREPLIFETIME(UHealthComponent, InvincibilityExpiresAtWorldTime);
	DOREPLIFETIME(UHealthComponent, InvincibilityTotalDuration);
}


void UHealthComponent::InitializeHealth(float InMax)
{
	// 【2026-06-15 简化】: 不再接收 InCurrent,初值 = MaxHealth
	// 修复前构造函数传 InCurrent 是为了与 BaseCharacter 双轨同步,现在不再需要
	MaxHealth = FMath::Max(InMax, 1.0f);
	CurrentHealth = MaxHealth;
	bIsDead = false;
}


float UHealthComponent::ApplyDamage(float DamageAmount)
{
	// 【2026-06-15 强化】: 防御性检查
	if (DamageAmount <= 0.0f)
	{
		return 0.0f;
	}

	// 已死亡: 短路 (避免重复触发死亡事件)
	if (bIsDead)
	{
		return 0.0f;
	}

	// ============================================================
	// 【2026.07.11 P0 大厂架构】复活无敌期守卫 (Layer 0 - 单一真理源)
	//
	// 为什么在 Component 里加 (而不是 BaseCharacter / 3 个伤害入口分别加):
	//   - HealthComponent 是数据权威持有者 — 跟 bIsDead 同源 → 单一真理源
	//   - 所有伤害入口 (TakeDamage / Server_ReportHit / Server_ReportAIAttackHit)
	//     最终都过 ApplyDamage → 这里拦截一次, 3 个入口全部覆盖
	//   - 新增伤害通道 (例如 UE 新组件, 武器特殊伤害) 也自动受益
	//
	// 大厂原则 - 零兜底:
	//   - 不是"不在阵营 = 不挡", 而是"bIsInvincible 是真理, true = 无视所有伤害"
	//   - 同阵营伤害 (Friendly Fire) 的拦截由 BaseCharacter 层的 FFactionTags::CanDamage 控制
	//     两层守卫不同: 一个管"无敌期" (这个), 一个管"阵营阻击" (FFactionTags::CanDamage)
	//
	// 设计: 无敌期间不广播血量变化 (不调用 OnHealthChanged)
	//       因为 UE 网络模式下, 客户端可能有 Instant Hit 命中动画, 不能让 BP 误触发
	// ============================================================
	if (bIsInvincible)
	{
		UE_LOG(LogTemp, Verbose,
			TEXT("[HealthComponent] ★ 拒绝扣血: 复活无敌期 ★ Owner=%s 剩余=%.2fs"),
			*GetNameSafe(GetOwner()),
			GetInvincibilityRemainingSeconds());
		return 0.0f;
	}

	// 服务器/单机: 直接修改 (由于 CurrentHealth 已 Replicated,会自动同步)
	// 客户端: 这次修改会被服务器后续的 Replicated Update 覆盖,本地仅作为即时反馈
	const float OldHealth = CurrentHealth;
	CurrentHealth = FMath::Max(0.0f, CurrentHealth - DamageAmount);
	const float ActualDamage = OldHealth - CurrentHealth;

	// 【2026-06-15 保留】: 服务器主动广播事件
	// 服务器端 Broadcast 是必要的,因为 Component 不会"自动"通知服务器
	// (OnRep 只在客户端触发)
	OnHealthChanged.Broadcast(CurrentHealth);

	if (CurrentHealth <= 0.0f && !bIsDead)
	{
		bIsDead = true;
		UE_LOG(LogCombat, Warning, TEXT("[HealthComponent] 服务器触发 OnDeath: Owner=%s"), *GetOwner()->GetName());
		OnDeath.Broadcast();
	}

	return ActualDamage;
}


void UHealthComponent::Heal(float HealAmount)
{
	if (bIsDead || HealAmount <= 0.0f)
	{
		return;
	}

	const float OldHealth = CurrentHealth;
	CurrentHealth = FMath::Min(MaxHealth, CurrentHealth + HealAmount);

	if (!FMath::IsNearlyEqual(OldHealth, CurrentHealth))
	{
		OnHealthChanged.Broadcast(CurrentHealth);
	}
}


void UHealthComponent::OnRep_CurrentHealth()
{
	// 【2026-06-15 新增】: 客户端收到血量更新时,广播给 HUD
	// 服务器端的 OnRep_CurrentHealth 不会被调用,所以服务器必须主动 Broadcast
	// (见 ApplyDamage 中)
	OnHealthChanged.Broadcast(CurrentHealth);
}


void UHealthComponent::OnRep_bIsDead()
{
	// 【P0 2026-07-01 P0 新增】客户端收到 bIsDead=true 时,触发 OnDeath 事件
	// 这样客户端无需依赖 Multicast_Die RPC 也能进入死亡流程
	//
	// 服务器端在 ApplyDamage 已经触发过 OnDeath.Broadcast (不会重复触发)
	// OnRep_bIsDead 仅在客户端被调用 → 保证 OnDeath 在每台机器上恰好触发一次
	UE_LOG(LogTemp, Error, // 使用 Error 让日志更显眼
		TEXT("[HealthComponent] ★★★ OnRep_bIsDead ★★★: 客户端收到死亡状态 Owner=%s bIsDead=%d"),
		*GetNameSafe(GetOwner()), bIsDead ? 1 : 0);
	OnDeath.Broadcast();
}


// ============================================================
// 【2026.07.11 P0 大厂架构】复活无敌期实现 — 单一真理源
// ============================================================
//
// 职责边界:
//   - HealthComponent 是无敌期数据权威 (跟 bIsDead 同构)
//   - 服务器 ActivateInvincibility() 是唯一写入路径
//   - 服务器 Timer 到期 / 显式 Deactivate 都清字段 + 触发 OnRep 同步
//   - 客户端 OnRep_InvincibilityChanged 自动广播 OnInvincibilityChanged
//
// 关键设计:
//   - GetWorld()->GetTimeSeconds() 是 UE 标准 World 级别时钟 — 适合关卡内无敌期计时
//   - Timer 用世界 Timer Manager (而非 Component 内 Tick) — 大厂原则: 不浪费 Tick
//   - 服务器主动 Broadcast (字段写时立即触发), 客户端通过 OnRep 触发 — 双发保证
// ============================================================

void UHealthComponent::ActivateInvincibility(float DurationSeconds)
{
	// 【大厂原则 - 零兜底】用户决策 A:
	//   - Duration <= 0 → 跳过激活 (既不报警, 也不强制使用默认值)
	//   - 这是用户的明确选择: 不强制数值, 由调用方负责
	if (DurationSeconds <= 0.0f)
	{
		UE_LOG(LogTemp, Verbose,
			TEXT("[HealthComponent] ActivateInvincibility: Duration=%.2f <= 0, 跳过激活 (用户决策: 静默跳过, 不强制默认值). Owner=%s"),
			DurationSeconds, *GetNameSafe(GetOwner()));
		return;
	}

	// 仅服务器修改权威字段 (客户端调用则不生效, 因为 DOREPLIFETIME 不允许逆向同步)
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		UE_LOG(LogTemp, Verbose,
			TEXT("[HealthComponent] ActivateInvincibility: 非服务器调用, 忽略 (权威字段仅服务器可写). Owner=%s"),
			*GetNameSafe(GetOwner()));
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogCombat, Warning,
			TEXT("[HealthComponent] ActivateInvincibility: World=null, 无法启动 Timer. Owner=%s"),
			*GetNameSafe(GetOwner()));
		return;
	}

	// 计算新到期时间
	const float NowWorldTime = World->GetTimeSeconds();
	const float NewExpireTime = NowWorldTime + DurationSeconds;

	// 【大厂原则 - 拒绝缩短】已激活时再次调用, 取较晚到期时间
	//   防止反复激活欺骗无敌 (例如 "我 0.1s 激活一下, 始终保持无敌")
	//   这不是兜底, 而是物理正确性: 时间只能前进不能后退
	const float CurrentExpireTime = InvincibilityExpiresAtWorldTime;
	const float FinalExpireTime = FMath::Max(CurrentExpireTime, NewExpireTime);
	const float FinalDuration = FinalExpireTime - NowWorldTime;

	if (bIsInvincible && FinalExpireTime <= CurrentExpireTime + KINDA_SMALL_NUMBER)
	{
		UE_LOG(LogTemp, Verbose,
			TEXT("[HealthComponent] ActivateInvincibility: 已激活且新到期不晚于现有到期, 拒绝缩短. "
			     "Owner=%s, 当前剩余=%.2fs, 请求=%.2fs"),
			*GetNameSafe(GetOwner()),
			GetInvincibilityRemainingSeconds(),
			DurationSeconds);
		return;
	}

	// 写入真理源字段 (DOREPLIFETIME 自动同步 + OnRep 触发客户端回调)
	bIsInvincible = true;
	InvincibilityExpiresAtWorldTime = FinalExpireTime;
	InvincibilityTotalDuration = FinalDuration; // 【v40.7 新增】存储总时长供进度条使用

	// 清理旧 Timer (如果有), 启动新 Timer
	World->GetTimerManager().ClearTimer(InvincibilityTimerHandle);
	World->GetTimerManager().SetTimer(
		InvincibilityTimerHandle,
		this,
		&UHealthComponent::ExpireInvincibility_Internal,
		FinalDuration,
		false); // 单次触发

	UE_LOG(LogCombat, Log,
		TEXT("[HealthComponent] ★ 复活无敌期激活 ★ Owner=%s Duration=%.2fs 到期=%.2f (Now=%.2f)"),
		*GetNameSafe(GetOwner()), FinalDuration, FinalExpireTime, NowWorldTime);

	// 服务器主动广播 (OnRep 在服务器不会触发, 这里手动保证服务器本地 UI/特效也能响应)
	OnInvincibilityChanged.Broadcast(true);
}


void UHealthComponent::DeactivateInvincibility()
{
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		// 客户端调用: 不修改字段, 但可以广播 (本地特效立即关掉)
		// 这跟 bIsDead 不同: bIsDead 是状态, bIsInvincible 是临时 Buff
		// 这里采用更保守的设计: 仅服务器 Deactivate 有效, 客户端忽略
		// 原因: 客户端没有权威时钟, 它"不知道现在还有多久到期"
		return;
	}

	if (!bIsInvincible)
	{
		// 未激活 → 幂等 no-op
		return;
	}

	UWorld* World = GetWorld();
	if (World)
	{
		World->GetTimerManager().ClearTimer(InvincibilityTimerHandle);
	}

	bIsInvincible = false;
	InvincibilityExpiresAtWorldTime = 0.0f;
	// 【v40.9 修复 — 镜像对称 v40.7】同步清 TotalDuration
	//   根因: 旧版 DeactivateInvincibility 没清 InvincibilityTotalDuration, 只有 Expire 才清
	//         → 显式 Deactivate 后, 字段残留 → 下次 Activate 之前的 GetInvincibilityDuration() 返回旧值
	//   镜像对称: Deactivate 与 Expire 行为应完全一致 (都是 "无敌结束"), 字段清除必须对称
	InvincibilityTotalDuration = 0.0f;

	UE_LOG(LogCombat, Log,
		TEXT("[HealthComponent] ★ 复活无敌期取消 ★ Owner=%s"),
		*GetNameSafe(GetOwner()));

	OnInvincibilityChanged.Broadcast(false);
}


void UHealthComponent::ExpireInvincibility_Internal()
{
	// 仅服务器跑 Timer (客户端无 Timer 启动)
	if (!GetOwner() || !GetOwner()->HasAuthority())
	{
		return;
	}

	if (!bIsInvincible)
	{
		// 边缘 case: 被显式 Deactivate 后, Timer 仍在 → no-op
		return;
	}

	bIsInvincible = false;
	InvincibilityExpiresAtWorldTime = 0.0f;
	InvincibilityTotalDuration = 0.0f; // 【v40.7 新增】清除总时长

	UE_LOG(LogCombat, Log,
		TEXT("[HealthComponent] ★ 复活无敌期到期 ★ Owner=%s"),
		*GetNameSafe(GetOwner()));

	OnInvincibilityChanged.Broadcast(false);
}


float UHealthComponent::GetInvincibilityRemainingSeconds() const
{
	// 【v40.9 大厂架构 — 真理源迁移】
	//   旧版 (v40.8) 根因:
	//     - 第一行用 bIsInvincible 字段短路 (if (!bIsInvincible) return 0.0f)
	//     - UE 网络复制: bool 字段在某些时序下不可靠 (OnRep 初始值不触发, true→false 间隔 < HUD Bind 时间)
	//     - HUD Bind 时如果 bIsInvincible 已变 false (服务器到期已 OnRep), 返回 0 → Show 不触发
	//     - 即使 Bind 之前没到期, 客户端 OnInvincibilityChanged(true) 触发 Show 之后, OnInvincibilityChanged(false) 紧接着触发 Hide → 闪一下又消失
	//   Session1.log 实证:
	//     - Line 00253.451 [Bind-Snapshot] Remaining=23.58s (因为 bIsInvincible=true 时 OnRep 时序竞争)
	//     - Line 00255.324 [OnInvincibilityChanged(false)] Hide 触发, 整个无敌期显示不完整
	//   修复 (v40.9):
	//     - **真理源: 派生字段 (InvincibilityExpiresAtWorldTime) 而不是衍生 bool (bIsInvincible)**
	//     - 大厂原则: 派生字段 > 衍生 bool (派生字段在所有时序下都准确)
	//     - ExpiresAt 字段 Replicated (普通 Replicated), 客户端读到时如果 ExpiresAt > Now → 还在无敌期
	//     - 用 GameState->GetServerWorldTimeSeconds() 而非 World->GetTimeSeconds():
	//       * 服务器时间 = 权威时钟, 客户端通过 UE 内置 RPC 自动补偿网络延迟
	//       * 避免客户端加载晚于服务器导致的 Remaining 计算偏差
	//       * 与 RoomGameState::GetMatchRemainingSeconds() 模式对称 (大厂原则)
	//   零兜底:
	//     - ExpiresAt=0 (Deactivate/Expire 后) → ExpiresAt - Now 是负数 → Max 钳到 0 → 返回 0
	//     - 不需要 bool 字段守卫, 派生逻辑自然处理
	//   性能: GetServerWorldTimeSeconds 是 UE 内置 API, 单次调用约 1μs, 10Hz Tick 完全无压力

	if (const UWorld* World = GetWorld())
	{
		double NowD = 0.0;
		if (const AGameStateBase* GS = World->GetGameState())
		{
			// 大厂原则: 服务器权威时钟 + 自动网络延迟补偿
			// 【UE 5.6 API 变更】GetServerWorldTimeSeconds() 返回 double (UE 5.6 内部服务器时间改为 double 精度)
			//   旧版 UE 4.x / 5.0 返回 float, 5.6 已切换为 double — 必须用 double 接收
			NowD = GS->GetServerWorldTimeSeconds();
		}
		else
		{
			// GameState 不在 (过渡期, 极少见) → fallback 到本地 World time
			// 这是显式降级, 不是兜底 — GameState 必然存在 (UE GameMode 强制)
			UE_LOG(LogTemp, Verbose,
				TEXT("[HealthComponent] GetInvincibilityRemainingSeconds: GameState=null, fallback 到 World->GetTimeSeconds. "
					 "Owner=%s"),
				*GetNameSafe(GetOwner()));
			NowD = World->GetTimeSeconds();
		}
		// 显式 double → float (避免 UE 5.6 double 精度丢失)
		const float Now = static_cast<float>(NowD);
		const float Remaining = FMath::Max(0.0f, InvincibilityExpiresAtWorldTime - Now);
		UE_LOG(LogTemp, Verbose,
			TEXT("[HealthComponent] GetInvincibilityRemainingSeconds: Owner=%s, ExpiresAt=%.2f, Now=%.2f, Remaining=%.2fs"),
			*GetNameSafe(GetOwner()), InvincibilityExpiresAtWorldTime, Now, Remaining);
		return Remaining;
	}
	return 0.0f;
}

float UHealthComponent::GetInvincibilityDuration() const
{
	// 直接返回激活时存储的总时长
	// 该字段在 ActivateInvincibility 时写入
	return InvincibilityTotalDuration;
}


void UHealthComponent::OnRep_InvincibilityChanged()
{
	// 服务器不会进这里 (服务器主动 Broadcast) — 客户端专用
	// 大厂原则 - 双发保证:
	//   - 服务器: ActivateInvincibility/ExpireInvincibility_Internal 主动 Broadcast
	//   - 客户端: Replicated 字段同步 → 本回调 Broadcast
	//   - 服务器上字段变化不会有 OnRep, 客户端字段变化触发 OnRep
	UE_LOG(LogTemp, Verbose,
		TEXT("[HealthComponent] ★ OnRep_InvincibilityChanged ★ Client 收到 bIsInvincible=%d, Owner=%s"),
		bIsInvincible ? 1 : 0, *GetNameSafe(GetOwner()));

	OnInvincibilityChanged.Broadcast(bIsInvincible);
}
