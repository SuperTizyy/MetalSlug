// ==========================================
// UDissolveComponent 实现
// ==========================================
#include "Components/DissolveComponent.h"
#include "Logs/MetalSlugLogChannels.h"

UDissolveComponent::UDissolveComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}

void UDissolveComponent::StartDissolve()
{
	if (bIsDissolving)
	{
		return;
	}

	bIsDissolving = true;
	DissolveProgress = 0.0f;
	UE_LOG(LogCombat, Verbose, TEXT("[DissolveComponent] 开始溶解: %s"), *GetOwner()->GetName());
}

void UDissolveComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!bIsDissolving)
	{
		return;
	}

	if (DissolveDuration <= 0.0f)
	{
		DissolveProgress = 1.0f;
		bIsDissolving = false;
		return;
	}

	DissolveProgress = FMath::Clamp(DissolveProgress + DeltaTime / DissolveDuration, 0.0f, 1.0f);
	if (DissolveProgress >= 1.0f)
	{
		bIsDissolving = false;
		UE_LOG(LogCombat, Verbose, TEXT("[DissolveComponent] 溶解完成: %s"), *GetOwner()->GetName());
	}
}
