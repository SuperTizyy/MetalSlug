// ==========================================
// UEnergyComponent 实现
// ==========================================
#include "Components/EnergyComponent.h"
#include "Logs/MetalSlugLogChannels.h"

UEnergyComponent::UEnergyComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UEnergyComponent::InitializeEnergy(float InMax, float InCurrent)
{
	MaxEnergy = FMath::Max(InMax, 0.0f);
	CurrentEnergy = FMath::Clamp(InCurrent, 0.0f, MaxEnergy);
}

bool UEnergyComponent::Consume(float Amount)
{
	if (Amount <= 0.0f || CurrentEnergy < Amount)
	{
		return false;
	}

	CurrentEnergy -= Amount;
	OnEnergyChanged.Broadcast(CurrentEnergy);
	return true;
}

void UEnergyComponent::Add(float Amount)
{
	if (Amount <= 0.0f)
	{
		return;
	}

	CurrentEnergy = FMath::Min(MaxEnergy, CurrentEnergy + Amount);
	OnEnergyChanged.Broadcast(CurrentEnergy);
}
