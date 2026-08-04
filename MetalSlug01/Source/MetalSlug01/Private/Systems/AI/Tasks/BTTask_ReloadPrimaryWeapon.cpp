// Copyright (c) 2026.
//
// 【v109 2026.07.30 生化模式 AI 大厂镜像方案】BTTask — 主武器换弹
//
// 职责:
//   - 调 CurrentWeapon->Server_StartReload (Actor RPC,镜像玩家 R 键换弹)

#include "Systems/AI/Tasks/BTTask_ReloadPrimaryWeapon.h"

#include "AIController.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "GameFramework/Pawn.h"

#include "Characters/BaseCharacter.h"
#include "Weapons/BaseWeapon.h"
#include "Components/WeaponFireComponent.h"

UBTTask_ReloadPrimaryWeapon::UBTTask_ReloadPrimaryWeapon()
{
	NodeName = TEXT("Reload Primary Weapon");
}


FString UBTTask_ReloadPrimaryWeapon::GetStaticDescription() const
{
	return TEXT("调 CurrentWeapon->Server_StartReload (镜像玩家 R 键)\n"
				"无武器/组件/未初始化 → Failed\n"
				"业务拒绝(弹匣满/备用空/换弹中) → Succeeded (业务正常, BT 不重试)");
}


EBTNodeResult::Type UBTTask_ReloadPrimaryWeapon::ExecuteTask(
	UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	AAIController* AIC = OwnerComp.GetAIOwner();
	if (!AIC) { return EBTNodeResult::Failed; }

	ABaseCharacter* SelfPawn = Cast<ABaseCharacter>(AIC->GetPawn());
	if (!SelfPawn) { return EBTNodeResult::Failed; }

	ABaseWeapon* Weapon = SelfPawn->GetCurrentWeapon();
	if (!Weapon)
	{
		UE_LOG(LogBehaviorTree, Warning,
			TEXT("[BTTask_ReloadPrimaryWeapon] AIC='%s' 无武器, Failed. "
				 "【修复】BT 在执行 Reload 前必须确认 BB.CurrentAmmo != 0."),
			*AIC->GetName());
		return EBTNodeResult::Failed;
	}

	UWeaponFireComponent* FireComp = Weapon->GetWeaponFireComponent();
	if (!FireComp)
	{
		UE_LOG(LogBehaviorTree, Error,
			TEXT("[BTTask_ReloadPrimaryWeapon] AIC='%s' 武器 '%s' 缺 WeaponFireComponent, Failed."),
			*AIC->GetName(),
			*Weapon->GetName());
		return EBTNodeResult::Failed;
	}

	// 业务拒绝 (弹匣满 / 备用空 / 换弹中) → 视为 Succeeded (业务正常, BT 上层 Decorator 处理)
	// 真正的配置错 (没初始化 / DT 配错) → Failed (WeaponFireComponent 内部 Log Error)
	const int32 CurrentAmmoBefore = FireComp->GetCurrentAmmo();
	const int32 ReserveBefore = FireComp->GetReserveAmmo();

	if (CurrentAmmoBefore >= FireComp->GetMagazineSize())
	{
		UE_LOG(LogBehaviorTree, Verbose,
			TEXT("[BTTask_ReloadPrimaryWeapon] AIC='%s' 弹匣已满 (%d/%d), 拒绝换弹."),
			*AIC->GetName(), CurrentAmmoBefore, FireComp->GetMagazineSize());
		return EBTNodeResult::Succeeded;
	}

	if (ReserveBefore <= 0)
	{
		UE_LOG(LogBehaviorTree, Warning,
			TEXT("[BTTask_ReloadPrimaryWeapon] AIC='%s' ReserveAmmo=0, 拒绝换弹. "
				 "BT 应守卫: 弹匣空 + 备用空 + 无补给 → 不开火守点."),
			*AIC->GetName());
		return EBTNodeResult::Succeeded;
	}

	if (FireComp->IsReloading())
	{
		UE_LOG(LogBehaviorTree, Verbose,
			TEXT("[BTTask_ReloadPrimaryWeapon] AIC='%s' 已在换弹中, 跳过."),
			*AIC->GetName());
		return EBTNodeResult::Succeeded;
	}

	// ===========================================
	// 【v200.3.3 修复】走 SelfPawn->Server_StartReload (Character RPC)
	// 玩家/AI 都走 Character->Server_StartReload (Character 有 owning connection)
	SelfPawn->Server_StartReload();

	UE_LOG(LogBehaviorTree, Display,
		TEXT("[BTTask_ReloadPrimaryWeapon] 【v200.3.3】AIC='%s' Server_StartReload 已调. Weapon='%s'"),
		*AIC->GetName(),
		*Weapon->GetName());

	return EBTNodeResult::Succeeded;
}