// ==========================================
// AnimNotify_DetachMagazine ??
// ==========================================
//
// ?????: ??? + ???????
//   1. ???????? Log
//   2. ????????? + ????
//   3. ???????
//
// ????: ???????????????????
// ==========================================

#include "Animation/AnimNotify_DetachMagazine.h"
#include "Components/SkeletalMeshComponent.h"
#include "Weapons/BaseWeapon.h"
#include "Characters/BaseCharacter.h"


void UAnimNotify_DetachMagazine::Notify(
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
			TEXT("[DetachMagazine] Character %s has no equipped weapon, cannot detach magazine."),
			*OwningCharacter->GetName());
		return;
	}

	// ?????????
	USkeletalMeshComponent* MagazineMesh = Weapon->ResolveMagazineSkeletalMesh();
	if (!MagazineMesh)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[DetachMagazine] MagazineSkeletalMesh not configured. Weapon=%s Character=%s. Fix: Add SkeletalMeshComponent named MagazineSkeletal in weapon BP Components panel."),
			*Weapon->GetName(),
			*OwningCharacter->GetName());
		return;
	}

	// ????????
	USceneComponent* CurrentParent = MagazineMesh->GetAttachParent();
	FName CurrentSocket = MagazineMesh->GetAttachSocketName();
	UE_LOG(LogTemp, Log,
		TEXT("[DetachMagazine] ENTER. Weapon=%s MagazineMesh=%s CurrentParent='%s' Socket='%s'"),
		*Weapon->GetName(),
		*MagazineMesh->GetName(),
		CurrentParent ? *CurrentParent->GetName() : TEXT("<null>"),
		*CurrentSocket.ToString());

	// ??????? MagazineSocket_L ??
	if (!OwningCharacter->GetMesh()->DoesSocketExist(FName("MagazineSocket_L")))
	{
		UE_LOG(LogTemp, Error,
			TEXT("[DetachMagazine] Character %s mesh has no MagazineSocket_L socket. Fix: Open character BP, go to Skeleton, add socket named MagazineSocket_L on Hand_L bone."),
			*OwningCharacter->GetName());
		return;
	}

	// ? detach ? attach, ?? KeepWorld ??????
	FDetachmentTransformRules DetachRules(EDetachmentRule::KeepWorld, true);
	MagazineMesh->DetachFromComponent(DetachRules);

	UE_LOG(LogTemp, Log,
		TEXT("[DetachMagazine] Detached. MagazineMesh Parent='%s' Socket='%s'"),
		MagazineMesh->GetAttachParent() ? *MagazineMesh->GetAttachParent()->GetName() : TEXT("<Detached>"),
		*MagazineMesh->GetAttachSocketName().ToString());

	// ??????? MagazineSocket_L
	FAttachmentTransformRules AttachmentRules(EAttachmentRule::KeepWorld, true);
	MagazineMesh->AttachToComponent(
		OwningCharacter->GetMesh(),
		AttachmentRules,
		FName("MagazineSocket_L"));

	// ?? Attach ????
	FName FinalSocket = MagazineMesh->GetAttachSocketName();
	UE_LOG(LogTemp, Log,
		TEXT("[DetachMagazine] Magazine detached to left hand MagazineSocket_L. Weapon=%s Character=%s FinalSocket='%s'"),
		*Weapon->GetName(),
		*OwningCharacter->GetName(),
		*FinalSocket.ToString());
}
