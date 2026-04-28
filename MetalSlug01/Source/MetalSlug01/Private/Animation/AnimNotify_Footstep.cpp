#include "Animation/AnimNotify_Footstep.h"
#include "Components/SkeletalMeshComponent.h"
#include "GameFramework/Character.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetMathLibrary.h"

FString UAnimNotify_Footstep::GetNotifyName_Implementation() const
{
	return TEXT("Footstep");
}

FVector UAnimNotify_Footstep::GetFootstepLocation(const USkeletalMeshComponent* MeshComp) const
{
	if (!MeshComp)
	{
		return FVector::ZeroVector;
	}

	FVector Location = MeshComp->GetComponentLocation();

	FVector foot_l = MeshComp->GetSocketLocation(FName("foot_l"));
	FVector foot_r = MeshComp->GetSocketLocation(FName("foot_r"));

	if (!foot_l.IsZero() && !foot_r.IsZero())
	{
		Location = (foot_l + foot_r) * 0.5f;
		Location.Z = MeshComp->GetComponentLocation().Z;
	}

	return Location;
}

void UAnimNotify_Footstep::Notify(
	USkeletalMeshComponent* MeshComp,
	UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	if (!MeshComp || !Animation)
	{
		return;
	}

	ACharacter* OwningCharacter = Cast<ACharacter>(MeshComp->GetOwner());
	if (!OwningCharacter)
	{
		return;
	}

	FVector FootLocation = GetFootstepLocation(MeshComp);

	// 编辑器预览时 MeshComp->GetWorld() 可能无效，直接跳过音效播放
	UWorld* World = MeshComp->GetWorld();
	if (!World || !FootstepSound)
	{
		return;
	}

	float FinalPitch = 1.0f;
	if (PitchVariation > 0.0f)
	{
		FinalPitch = UKismetMathLibrary::RandomFloatInRange(1.0f - PitchVariation, 1.0f + PitchVariation);
	}

	UGameplayStatics::PlaySoundAtLocation(
		this,
		FootstepSound,
		FootLocation,
		FRotator::ZeroRotator,
		VolumeMultiplier,
		FinalPitch
	);

	OnFootstep(MeshComp, FootLocation);
}

void UAnimNotify_Footstep::OnFootstep_Implementation(const USkeletalMeshComponent* MeshComp, FVector Location) const
{
	// 虚函数实现，蓝图子类可覆盖此逻辑
	// 例如在此处触发地面尘土粒子特效
}
