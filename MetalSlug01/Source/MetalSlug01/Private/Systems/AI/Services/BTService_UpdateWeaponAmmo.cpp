// Copyright (c) 2026.
//
// 【v107 2026.07.28 生化模式 AI】BTService — 弹药快照实现

#include "Systems/AI/Services/BTService_UpdateWeaponAmmo.h"

#include "AIController.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "GameFramework/Pawn.h"

#include "Characters/BaseCharacter.h"
#include "Weapons/BaseWeapon.h"
#include "Components/WeaponFireComponent.h"

UBTService_UpdateWeaponAmmo::UBTService_UpdateWeaponAmmo()
{
	NodeName = TEXT("Update Weapon Ammo");

	// 弹药快照频率比 Target 派生更频繁 (HUD 同步用 0.1s 即可)
	Interval = 0.1f;
	RandomDeviation = 0.f;
	bTickIntervals = true;

	CurrentAmmoKey.AddIntFilter(this, GET_MEMBER_NAME_CHECKED(UBTService_UpdateWeaponAmmo, CurrentAmmoKey));
	MagazineSizeKey.AddIntFilter(this, GET_MEMBER_NAME_CHECKED(UBTService_UpdateWeaponAmmo, MagazineSizeKey));
	ReserveAmmoKey.AddIntFilter(this, GET_MEMBER_NAME_CHECKED(UBTService_UpdateWeaponAmmo, ReserveAmmoKey));
	bIsReloadingKey.AddBoolFilter(this, GET_MEMBER_NAME_CHECKED(UBTService_UpdateWeaponAmmo, bIsReloadingKey));
}


FString UBTService_UpdateWeaponAmmo::GetStaticDescription() const
{
	return FString::Printf(
		TEXT("每 %.2fs 从武器 WeaponFireComponent 派生 BB:\n"
			 "- CurrentAmmo / MagazineSize / ReserveAmmo / bIsReloading\n"
			 "- 无武器/组件 → 写入不可开火状态 + Log Error, 不创造默认弹药"),
		Interval);
}


void UBTService_UpdateWeaponAmmo::TickNode(
	UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	Super::TickNode(OwnerComp, NodeMemory, DeltaSeconds);

	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	if (!BB) { return; }

	AAIController* AIC = OwnerComp.GetAIOwner();
	if (!AIC) { return; }

	ABaseCharacter* SelfPawn = Cast<ABaseCharacter>(AIC->GetPawn());
	if (!SelfPawn) { return; }

	ABaseWeapon* Weapon = SelfPawn->GetCurrentWeapon();
	if (!Weapon)
	{
		// 【零兜底】无武器 → 写入"不可开火状态", BT 装饰器自动拒绝 OpenFire 分支
		BB->SetValueAsInt(CurrentAmmoKey.SelectedKeyName, 0);
		BB->SetValueAsInt(MagazineSizeKey.SelectedKeyName, 0);
		BB->SetValueAsInt(ReserveAmmoKey.SelectedKeyName, 0);
		BB->SetValueAsBool(bIsReloadingKey.SelectedKeyName, true); // true = 视同"在换弹", BT 不开火
		return;
	}

	UWeaponFireComponent* FireComp = Weapon->GetWeaponFireComponent();
	if (!FireComp)
	{
		// 【零兜底】WeaponFireComponent 缺失 = BP 没挂, 强制修复
		UE_LOG(LogBehaviorTree, Warning,
			TEXT("[BTService_UpdateWeaponAmmo] %s: 武器 '%s' 缺 WeaponFireComponent, "
				 "BB 写入不可开火状态. 【修复】打开 BP_Weapon_*** → Add Component → WeaponFireComponent."),
			*AIC->GetName(),
			*Weapon->GetName());

		BB->SetValueAsInt(CurrentAmmoKey.SelectedKeyName, 0);
		BB->SetValueAsInt(MagazineSizeKey.SelectedKeyName, 0);
		BB->SetValueAsInt(ReserveAmmoKey.SelectedKeyName, 0);
		BB->SetValueAsBool(bIsReloadingKey.SelectedKeyName, true);
		return;
	}

	// 写 BB (真理源 = WeaponFireComponent 字段, 已 Replicated)
	BB->SetValueAsInt(CurrentAmmoKey.SelectedKeyName, FireComp->GetCurrentAmmo());
	BB->SetValueAsInt(MagazineSizeKey.SelectedKeyName, FireComp->GetMagazineSize());
	BB->SetValueAsInt(ReserveAmmoKey.SelectedKeyName, FireComp->GetReserveAmmo());
	BB->SetValueAsBool(bIsReloadingKey.SelectedKeyName, FireComp->IsReloading());
}