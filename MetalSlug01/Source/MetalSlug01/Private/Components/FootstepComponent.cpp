// ==========================================
// UFootstepComponent 实现
// ==========================================
#include "Components/FootstepComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/World.h"
#include "Logs/MetalSlugLogChannels.h"

UFootstepComponent::UFootstepComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

void UFootstepComponent::PlayFootstep()
{
	UWorld* World = GetWorld();
	if (!World || !DefaultFootstepSound)
	{
		return;
	}

	AActor* Owner = GetOwner();
	if (!Owner)
	{
		return;
	}

	// 简化版: 直接在脚下播放默认音效
	// 后续可扩展: 用 LineTrace 检测地面物理材质, 选择不同脚步音效
	const FVector Location = Owner->GetActorLocation();
	UGameplayStatics::PlaySoundAtLocation(World, DefaultFootstepSound, Location);
}
