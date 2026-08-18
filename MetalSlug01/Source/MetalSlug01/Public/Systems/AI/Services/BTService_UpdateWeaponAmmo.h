// Copyright (c) 2026.
//
// 【v107 2026.07.28 生化模式 AI】BTService — 弹药快照
//
// 职责:
//   - 读 BaseCharacter->GetCurrentWeapon()->WeaponFireComponent
//   - 派生 BB.CurrentAmmo / BB.MagazineSize / BB.ReserveAmmo / BB.bIsReloading
//
// 大厂原则:
//   - 写事实 (Service 写 BB), 不做决策 (Decorator 判断)
//   - 真理源 = 武器的 UWeaponFireComponent (不重新计算弹药)
//   - 无武器/组件/配置时显式 Error + 写入不可开火状态 (bIsReloading=true), 不创造默认弹药
//   - 不接管 ABaseCharacter 弹药计算 (那是刀战用的同机制, 必须复用)

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTService.h"
#include "BehaviorTree/BehaviorTreeTypes.h"
#include "BTService_UpdateWeaponAmmo.generated.h"

struct FBlackboardKeySelector;

/**
 * 生化模式弹药快照 Service
 *
 * 编辑器配置 (BT 根节点挂):
 *   - CurrentAmmoKey / MagazineSizeKey / ReserveAmmoKey / bIsReloadingKey
 */
UCLASS(Blueprintable, meta = (DisplayName = "Update Weapon Ammo (弹药快照)"))
class METALSLUG01_API UBTService_UpdateWeaponAmmo : public UBTService
{
	GENERATED_BODY()

public:
	UBTService_UpdateWeaponAmmo();

	/** @brief BT 编辑器静态描述 (显示 4 个 Ammo Key + Tick 频率) */
	virtual FString GetStaticDescription() const override;

	/** BB.CurrentAmmo (Int) */
	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector CurrentAmmoKey;

	/** BB.MagazineSize (Int) */
	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector MagazineSizeKey;

	/** BB.ReserveAmmo (Int) */
	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector ReserveAmmoKey;

	/** BB.bIsReloading (Bool) */
	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector bIsReloadingKey;

protected:
	/** @brief Tick: 派生 4 类事实到 BB (身份 + 人数), 切换时清理 TargetActor/NearestXxx */
	virtual void TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory,
		float DeltaSeconds) override;
};