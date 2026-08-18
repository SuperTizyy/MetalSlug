// Copyright (c) 2026.
//
// 【v107 2026.07.28 生化模式 AI】BTTask — 主武器停火
//
// 职责:
//   - 调 CurrentWeapon->WeaponFireComponent->StopFire
//
// 大厂原则:
//   - 无武器/组件 → Succeeded (业务正常, BT 不需要重试)
//   - 这是面向"全自动武器" 的接口 — 半自动打一发就停, StopFire 是 no-op
//
// 与 BTTask_StartPrimaryFire 配套使用:
//   - Simple Parallel 主任务: MoveTo (持续移动)
//   - 后台子树: StartFire → Wait N 秒 → StopFire → Loop

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BTTask_StopPrimaryFire.generated.h"

/**
 * 主武器停火 Task
 */
UCLASS(Blueprintable, meta = (DisplayName = "Stop Primary Fire (主武器停火)"))
class METALSLUG01_API UBTTask_StopPrimaryFire : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_StopPrimaryFire();

	/** @brief BT 编辑器静态描述 (显示 "主武器停火 — 全自动武器用") */
	virtual FString GetStaticDescription() const override;

	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;
};