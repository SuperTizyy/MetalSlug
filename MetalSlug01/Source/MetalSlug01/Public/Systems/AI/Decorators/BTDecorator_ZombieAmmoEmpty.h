// Copyright (c) 2026.
//
// 【v107 2026.07.28 生化模式 AI】BTDecorator — 弹药空判断
//
// 职责:
//   - BB.CurrentAmmo == 0 → 返回 true (弹夹空, 允许进换弹 / 找补给分支)
//   - 否则 → false (弹夹不空, 不许进换弹分支)
//
// 大厂原则:
//   - 严格走 BB.CurrentAmmo 数值 (不是 BB.bIsReloading)
//   - 决策放 BT, Service 只写事实

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTDecorator.h"
#include "BehaviorTree/BehaviorTreeTypes.h"
#include "BTDecorator_ZombieAmmoEmpty.generated.h"

struct FBlackboardKeySelector;

/**
 * 弹药空判断 — 弹夹为 0 时放行
 *
 * 编辑器配置:
 *   - CurrentAmmoKey (BB.CurrentAmmo Int)
 */
UCLASS(Blueprintable, meta = (DisplayName = "Zombie Ammo Empty (弹药空守卫)"))
class METALSLUG01_API UBTDecorator_ZombieAmmoEmpty : public UBTDecorator
{
	GENERATED_BODY()

public:
	UBTDecorator_ZombieAmmoEmpty();

	virtual FString GetStaticDescription() const override;

	/** BB.CurrentAmmo (Int) */
	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector CurrentAmmoKey;

protected:
	virtual bool CalculateRawConditionValue(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) const override;
};