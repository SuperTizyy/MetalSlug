// Copyright (c) 2026.
//
// 【v107 2026.07.28 生化模式 AI】BTTask — 主武器换弹
//
// 职责:
//   - 调 CurrentWeapon->WeaponFireComponent->StartReload
//
// 大厂原则:
//   - 弹匣已满 / 备用弹药为 0 / 已在换弹中 都是明确业务结果 (WeaponFireComponent 拒绝 + Log Verbose)
//   - 弹药为 0 且 ReserveAmmo = 0 → 业务正常, 拒绝换弹 (BT 上层 Decorator 处理, 不制造假弹药)
//   - 无武器/组件/未初始化 → Failed (强制修复)

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTTask_ReloadPrimaryWeapon.generated.h"

/**
 * 主武器换弹 Task
 *
 * 业务流:
 *   BT 检测 BB.CurrentAmmo == 0 → 调本 Task → WeaponFireComponent.StartReload
 *   Timer 到期自动填弹匣 + 广播 OnReloadStateChanged(false)
 *   BT 可在 WaitMontageFinish 等价物上监听 OnReloadStateChanged 或轮询 BB.bIsReloading
 */
UCLASS(Blueprintable, meta = (DisplayName = "Reload Primary Weapon (主武器换弹)"))
class METALSLUG01_API UBTTask_ReloadPrimaryWeapon : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_ReloadPrimaryWeapon();

	/** @brief BT 编辑器静态描述 (显示 "主武器换弹") */
	virtual FString GetStaticDescription() const override;

	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
};