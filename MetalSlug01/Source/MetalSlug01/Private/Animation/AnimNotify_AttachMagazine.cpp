// ==========================================
// AnimNotify_AttachMagazine ??
// ==========================================
//
// ?????: ??? + ???????
//   1. ???????? Log
//   2. ????????? + ????
//   3. ???????
//
// ????: ??????????? RestoreMagazineToWeapon()
// ==========================================

#include "Animation/AnimNotify_AttachMagazine.h"
#include "Components/SkeletalMeshComponent.h"
#include "Weapons/BaseWeapon.h"
#include "Characters/BaseCharacter.h"


void UAnimNotify_AttachMagazine::Notify(
	USkeletalMeshComponent* MeshComp,
	UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	if (!MeshComp)
	{
		return;
	}

	// ????
	ACharacter* OwningCharacter = Cast<ACharacter>(MeshComp->GetOwner());
	if (!OwningCharacter)
	{
		return;
	}

	// ?????????
	ABaseCharacter* BaseChar = Cast<ABaseCharacter>(OwningCharacter);
	if (!BaseChar)
	{
		return;
	}

	ABaseWeapon* Weapon = BaseChar->GetCurrentWeapon();
	if (!Weapon)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[AttachMagazine] Character %s has no equipped weapon, cannot attach magazine."),
			*OwningCharacter->GetName());
		return;
	}

	// ?????????
	USkeletalMeshComponent* MagazineMesh = Weapon->ResolveMagazineSkeletalMesh();
	if (!MagazineMesh)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[AttachMagazine] MagazineSkeletalMesh not configured. Weapon=%s Character=%s. Fix: Add SkeletalMeshComponent named MagazineSkeletal in weapon BP Components panel."),
			*Weapon->GetName(),
			*OwningCharacter->GetName());
		return;
	}

	// ????????
	USceneComponent* CurrentParent = MagazineMesh->GetAttachParent();
	FName CurrentSocket = MagazineMesh->GetAttachSocketName();
	UE_LOG(LogTemp, Log,
		TEXT("[AttachMagazine] ENTER. Weapon=%s MagazineMesh=%s CurrentParent='%s' Socket='%s'"),
		*Weapon->GetName(),
		*MagazineMesh->GetName(),
		CurrentParent ? *CurrentParent->GetName() : TEXT("<Detached>"),
		*CurrentSocket.ToString());

	// ?????????
	Weapon->RestoreMagazineToWeapon();

	// ????????
	FName FinalSocket = MagazineMesh->GetAttachSocketName();
	USceneComponent* FinalParent = MagazineMesh->GetAttachParent();
	UE_LOG(LogTemp, Log,
		TEXT("[AttachMagazine] Magazine attached. Weapon=%s Character=%s FinalParent='%s' FinalSocket='%s'"),
		*Weapon->GetName(),
		*OwningCharacter->GetName(),
		FinalParent ? *FinalParent->GetName() : TEXT("<Detached>"),
		*FinalSocket.ToString());
}
