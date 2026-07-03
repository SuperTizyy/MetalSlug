// Copyright (c) 2026.
//
// 【P0 BT 原子库】BTDecorator — 血量阈值判断
// 架构定位: BT 决策
//   - 读 BB.HealthPercent (由 BTService_UpdateBlackboard 0.1s 写入)
//   - 比较阈值, 触发不同行为 (血量 < 20% 逃跑, < 0.01 死亡)
//   - 不做启动期护栏, 不做 IsNone 兜底 — BB 默认值由 BT 资产决定, BT 框架自己处理

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTDecorator.h"
#include "BehaviorTree/BehaviorTreeTypes.h"
#include "BTDecorator_HPThreshold.generated.h"

struct FBlackboardKeySelector;

UENUM(BlueprintType)
enum class EBTHPCheckMode : uint8
{
	LessThan      UMETA(DisplayName = "低于"),
	GreaterThan   UMETA(DisplayName = "高于"),
};

/**
 * UBTDecorator_HPThreshold
 *
 * 使用方式 (BT 编辑器):
 *   Sequence "Retreat" (血量低于 20% 逃跑)
 *   ├─ Decorator: HPThreshold, Mode=LessThan, Threshold=0.2
 *   └─ UE 原生 Move To (往反方向)
 *
 *   Sequence "Death"
 *   ├─ Decorator: HPThreshold, Mode=LessThan, Threshold=0.01
 *   └─ BTTask_PlayDeath
 *
 * BB Key 选择器 — 必须在 BT 编辑器填上, 否则编辑器闪红:
 *   HealthPercentKey: BB.HealthPercent (BT 编辑器字段中选)
 */
UCLASS(Blueprintable, meta = (DisplayName = "HP Threshold (血量阈值)"))
class METALSLUG01_API UBTDecorator_HPThreshold : public UBTDecorator
{
	GENERATED_BODY()

public:
	UBTDecorator_HPThreshold();

	virtual FString GetStaticDescription() const override;

	/** 血量百分比 BB Key — 读 BB.HealthPercent */
	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector HealthPercentKey;

	UPROPERTY(EditAnywhere, Category = "Check")
	EBTHPCheckMode Mode = EBTHPCheckMode::LessThan;

	/** 血量百分比阈值 [0, 1] */
	UPROPERTY(EditAnywhere, Category = "Check",
		meta = (ClampMin = "0.0", ClampMax = "1.0"))
	float Threshold = 0.2f;

protected:
	virtual bool CalculateRawConditionValue(UBehaviorTreeComponent& OwnerComp,
		uint8* NodeMemory) const override;
};
