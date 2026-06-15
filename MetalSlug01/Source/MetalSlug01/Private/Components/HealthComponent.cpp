// ==========================================
// UHealthComponent 实现 【2026-06-15 重构: 数据权威持有者】
// ==========================================
#include "Components/HealthComponent.h"
#include "Logs/MetalSlugLogChannels.h"
#include "Net/UnrealNetwork.h"

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
		UE_LOG(LogCombat, Verbose, TEXT("[HealthComponent] 死亡: Owner=%s"), *GetOwner()->GetName());
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


void UHealthComponent::AddHealth(float HealAmount)
{
	// AddHealth 与 Heal 语义相同 (供击杀奖励等场景使用)
	Heal(HealAmount);
}


void UHealthComponent::OnRep_CurrentHealth()
{
	// 【2026-06-15 新增】: 客户端收到血量更新时,广播给 HUD
	// 服务器端的 OnRep_CurrentHealth 不会被调用,所以服务器必须主动 Broadcast
	// (见 ApplyDamage 中)
	OnHealthChanged.Broadcast(CurrentHealth);
}
