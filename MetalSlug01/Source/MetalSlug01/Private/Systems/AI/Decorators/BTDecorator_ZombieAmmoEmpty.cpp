// Copyright (c) 2026.
//
// 【v107 2026.07.28 生化模式 AI】BTDecorator — 弹药空判断实现

#include "Systems/AI/Decorators/BTDecorator_ZombieAmmoEmpty.h"

#include "AIController.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"

UBTDecorator_ZombieAmmoEmpty::UBTDecorator_ZombieAmmoEmpty()
{
	NodeName = TEXT("Zombie Ammo Empty");

	CurrentAmmoKey.AddIntFilter(this, GET_MEMBER_NAME_CHECKED(UBTDecorator_ZombieAmmoEmpty, CurrentAmmoKey));
}


/**
 * @brief 生成 BT 节点描述 — 显示生化 AI 弹夹空判定
 * @return 多行描述,展示 "CurrentAmmo == 0 → 放行 (弹夹空)" 判定语义
 */
FString UBTDecorator_ZombieAmmoEmpty::GetStaticDescription() const
{
	return TEXT("BB.CurrentAmmo == 0 → 放行 (弹夹空)\n"
				"否则 → 拒判");
}


bool UBTDecorator_ZombieAmmoEmpty::CalculateRawConditionValue(
	UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) const
{
	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	if (!BB) { return false; }

	const int32 CurrentAmmo = BB->GetValueAsInt(CurrentAmmoKey.SelectedKeyName);
	return CurrentAmmo <= 0;
}