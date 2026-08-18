// Copyright (c) 2026.
//
// 【v109 2026.07.30 生化模式 AI 大厂镜像方案】BTTask — 主武器停火
//
// 职责:
//   - 调 CurrentWeapon->Server_StopFire (Actor RPC,镜像玩家 IA_Fire Released 链路)

#include "Systems/AI/Tasks/BTTask_StopPrimaryFire.h"

#include "AIController.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "GameFramework/Pawn.h"

#include "Characters/BaseCharacter.h"
#include "Weapons/BaseWeapon.h"
#include "Components/WeaponFireComponent.h"

UBTTask_StopPrimaryFire::UBTTask_StopPrimaryFire()
{
	NodeName = TEXT("Stop Primary Fire");
}


/**
 * @brief 生成 BT 节点描述 — 展示主武器停火调用镜像玩家 IA_Fire Released
 * @return 多行描述,展示全自动停止连射 + 半自动 no-op 语义
 */
FString UBTTask_StopPrimaryFire::GetStaticDescription() const
{
	return TEXT("调 CurrentWeapon->Server_StopFire (镜像玩家 IA_Fire Released)\n"
				"全自动停止连射 | 半自动 no-op");
}


EBTNodeResult::Type UBTTask_StopPrimaryFire::ExecuteTask(
	UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	AAIController* AIC = OwnerComp.GetAIOwner();
	if (!AIC)
	{
		UE_LOG(LogBehaviorTree, Warning,
			TEXT("[BTTask_StopPrimaryFire] 无 AIController, Failed."));
		return EBTNodeResult::Failed;
	}

	ABaseCharacter* SelfPawn = Cast<ABaseCharacter>(AIC->GetPawn());
	if (!SelfPawn)
	{
		UE_LOG(LogBehaviorTree, Warning,
			TEXT("[BTTask_StopPrimaryFire] AIC='%s' Pawn 不是 ABaseCharacter, Failed."),
			*AIC->GetName());
		return EBTNodeResult::Failed;
	}

	ABaseWeapon* Weapon = SelfPawn->GetCurrentWeapon();
	if (!Weapon)
	{
		// 业务正常: AI 死/切武器 → StopFire no-op
		UE_LOG(LogBehaviorTree, Verbose,
			TEXT("[BTTask_StopPrimaryFire] AIC='%s' 无武器, no-op."),
			*AIC->GetName());
		return EBTNodeResult::Succeeded;
	}

	UWeaponFireComponent* FireComp = Weapon->GetWeaponFireComponent();
	if (!FireComp)
	{
		// 业务正常: 配置错由 StartFire 拦截,这里不报警
		UE_LOG(LogBehaviorTree, Verbose,
			TEXT("[BTTask_StopPrimaryFire] AIC='%s' 武器 '%s' 缺 WeaponFireComponent, no-op."),
			*AIC->GetName(),
			*Weapon->GetName());
		return EBTNodeResult::Succeeded;
	}

	// ===========================================
	// 【v200.3.3 修复】走 SelfPawn->Server_StopFire (Character RPC)
	// 玩家/AI 都走 Character->Server_StopFire (Character 有 owning connection)
	SelfPawn->Server_StopFire();

	UE_LOG(LogBehaviorTree, Display,
		TEXT("[BTTask_StopPrimaryFire] 【v200.3.3】AIC='%s' Server_StopFire 已调. Weapon='%s'"),
		*AIC->GetName(),
		*Weapon->GetName());

	return EBTNodeResult::Succeeded;
}