// ==========================================
// UEnergyComponent 实现 【2026-06-15 重构: 升级为数据权威持有者 + 网络复制】
// ==========================================
#include "Components/EnergyComponent.h"
#include "Logs/MetalSlugLogChannels.h"

UEnergyComponent::UEnergyComponent()
{
	// 【2026-06-15 新增】: 启用复制 (服务器修改后自动同步到所有客户端)
	SetIsReplicatedByDefault(true);

	// Component 自身不需要 Tick (能量改变通过事件通知)
	PrimaryComponentTick.bCanEverTick = false;
}


void UEnergyComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// 【修复 Q4】: SetIsReplicatedByDefault(true) 已自动复制所有 UPROPERTY
	// DOREPLIFETIME 是冗余的（且会与 ReplicatedUsing 产生双重复制通知），删除之
	// CurrentEnergy 的 ReplicatedUsing=OnRep_CurrentEnergy 单独生效
}


void UEnergyComponent::InitializeEnergy(float InMax, float InCurrent)
{
	// 【2026-06-15 重构】: 改为通过 SetIsReplicatedByDefault 全局复制
	// 不再接收外部 InMax/InCurrent, 初值由 UPROPERTY 决定
	MaxEnergy = FMath::Max(InMax, 0.0f);
	CurrentEnergy = FMath::Clamp(InCurrent, 0.0f, MaxEnergy);
}


bool UEnergyComponent::Consume(float Amount)
{
	// 防御: 非法数值直接短路
	if (Amount <= 0.0f)
	{
		return false;
	}

	// 能量不足: 短路
	if (CurrentEnergy < Amount)
	{
		return false;
	}

	// 服务器直接修改 (CurrentEnergy 已 Replicated, 会自动同步)
	const float OldEnergy = CurrentEnergy;
	CurrentEnergy -= Amount;

	// 服务器端主动广播事件 (OnRep 只在客户端触发)
	if (!FMath::IsNearlyEqual(OldEnergy, CurrentEnergy))
	{
		OnEnergyChanged.Broadcast(CurrentEnergy);
	}

	return true;
}


void UEnergyComponent::Add(float Amount)
{
	if (Amount <= 0.0f)
	{
		return;
	}

	const float OldEnergy = CurrentEnergy;
	CurrentEnergy = FMath::Min(MaxEnergy, CurrentEnergy + Amount);

	if (!FMath::IsNearlyEqual(OldEnergy, CurrentEnergy))
	{
		OnEnergyChanged.Broadcast(CurrentEnergy);
	}
}


void UEnergyComponent::OnRep_CurrentEnergy()
{
	// 【2026-06-15 新增】: 客户端收到能量更新时, 广播给 HUD
	// 服务器端的 OnRep_CurrentEnergy 不会被调用, 所以服务器必须主动 Broadcast
	// (见 Consume/Add 中)
	OnEnergyChanged.Broadcast(CurrentEnergy);
}
