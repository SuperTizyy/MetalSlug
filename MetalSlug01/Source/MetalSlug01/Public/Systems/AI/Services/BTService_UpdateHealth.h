// Copyright (c) 2026.
//
// 【P0 2026.07.09 BT 原子库】BTService — HP 观察 (单一职责)
//
// 架构定位:
//   - 本 Service 只派生"HP 百分比"这 1 类事实
//   - 上层取代原 BTService_UpdateBlackboard 里的 HP 计算 (上帝 Service 反模式)
//   - HP 阈值判断 → 留给 BTDecorator_HPThreshold (Decorator 自决, 不需要 Token)
//
// 大厂原则:
//   - 单一职责: 1 个 Service 只写 1 类派生量
//   - 周期: 0.1s 对 HP 足够 — HP 不会在 100ms 内大幅波动
//   - 防御性编程: HealthComponent 为 null → 写 0 (表示"无法判断",Decorator 自然 Fail 路径)

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTService.h"
#include "BehaviorTree/BehaviorTreeTypes.h"
#include "BTService_UpdateHealth.generated.h"

struct FBlackboardKeySelector;

/**
 * UBTService_UpdateHealth
 * 派生量周期写入器 — 只写 BB.HealthPercent (Float, 0~1)
 *
 * 使用方式 (BT 编辑器):
 *   BT_Grunt 根节点 Selector 上挂本 Service
 *   在 Details 面板配 BB Key 字段:
 *     - HealthPercentKey: BB.HealthPercent
 *   Interval = 0.1s (默认)
 *
 * 数据源: BaseCharacter::HealthComponent->GetCurrent() / GetMax()
 *         HealthComponent 不存在时 → 写 0 (Decorator 自然 Fail,AI 进入死亡分支)
 */
UCLASS(Blueprintable, meta = (DisplayName = "Update Health (HP 观察)"))
class METALSLUG01_API UBTService_UpdateHealth : public UBTService
{
	GENERATED_BODY()

public:
	UBTService_UpdateHealth();

	virtual FString GetStaticDescription() const override;

	/** 血量百分比 BB Key — 服务把 HP/MaxHP 写到这里 (Float, 0~1) */
	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector HealthPercentKey;

protected:
	virtual void TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory,
		float DeltaSeconds) override;
};
