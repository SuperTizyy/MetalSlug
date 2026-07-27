// Copyright (c) 2026.
//
// 【v107 2026.07.28 生化模式 AI】BTTask — 集合点选择与锁定
//
// 职责:
//   - 调 URoomZombieRallySubsystem::SelectRallyPoint_Nearest 或 _MostPopulated
//   - 锁定到账本 (一局内只选一次)
//   - 写 BB.LockedRallyPoint + BB.bRallyPointLocked
//   - 计算 BB.DistanceToRallyPoint
//
// 大厂原则:
//   - 选点失败 → Failed (不兜底, 不漫游, 不选当前位置)
//   - 已锁点 → 跳过选点, 复用 LockedRallyPoint (不重新选)
//   - 模式: Nearest (默认) / MostPopulated (母体数>人类数)
//   - Nearest 时 : 选最近集合点
//   - MostPopulated 时: 选当前人类最多集合点 (对"未锁点 AI"有效)
//
// 配置 (BTTask_SelectZombieRallyPoint):
//   - SelectionMode (枚举: Nearest / MostPopulated)
//   - LockedRallyPointKey (BB.Object) — 写入 AZombieRallyPoint*
//   - bRallyPointLockedKey (BB.Bool)
//   - DistanceToRallyPointKey (BB.Float)

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTTaskNode.h"
#include "BehaviorTree/BehaviorTreeTypes.h"
#include "BTTask_SelectZombieRallyPoint.generated.h"

struct FBlackboardKeySelector;

UENUM(BlueprintType)
enum class EZombieRallySelectionMode : uint8
{
	Nearest        UMETA(DisplayName = "Nearest (最近集合点, 人类初始)"),
	MostPopulated  UMETA(DisplayName = "MostPopulated (人类最多集合点, 母体多时)"),
};

/**
 * 集合点选择并锁定 Task — BT 决策原子
 */
UCLASS(Blueprintable, meta = (DisplayName = "Select Zombie Rally Point (选集合点+锁定)"))
class METALSLUG01_API UBTTask_SelectZombieRallyPoint : public UBTTaskNode
{
	GENERATED_BODY()

public:
	UBTTask_SelectZombieRallyPoint();

	virtual FString GetStaticDescription() const override;

	/** 选点策略 */
	UPROPERTY(EditAnywhere, Category = "Rally")
	EZombieRallySelectionMode SelectionMode = EZombieRallySelectionMode::Nearest;

	/** BB.LockedRallyPoint (Object) — 写入 AZombieRallyPoint* */
	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector LockedRallyPointKey;

	/** BB.bRallyPointLocked (Bool) */
	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector bRallyPointLockedKey;

	/** BB.DistanceToRallyPoint (Float) */
	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector DistanceToRallyPointKey;

	virtual EBTNodeResult::Type ExecuteTask(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;

protected:
	/** 选点+锁定+写 BB — 抽出共用逻辑 */
	EBTNodeResult::Type PerformSelection(UBehaviorTreeComponent& OwnerComp, class ABaseCharacter* SelfPawn, class AController* AIC);
};