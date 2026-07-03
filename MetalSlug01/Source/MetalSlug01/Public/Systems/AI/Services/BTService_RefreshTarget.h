// Copyright (c) 2026.
//
// 【Phase 3 大厂架构】BTService — 目标刷新服务
//
// 职责:
//   - 周期性扫描战场所有敌人，按距离排序选出最近目标
//   - 将目标写入 BB Key "TargetActor"，供 BTTask_MeleeAttack 读取
//
// 设计理念:
//   - 完全独立于 OnTargetDetected 的 NearbyThreat 写入
//     （NearbyThreat 依赖 OverrideBTDistance 配置正确，在此之前不可靠）
//   - 每 Tick 直接查 AIPerception::GetKnownPerceivedActors，不依赖 BB 中间状态
//   - 与 OnTargetDetected 完全兼容：两者同时写 TargetActor（后者覆盖前者，等效）
//   - NearbyThreat 仅作为"极近距离兜底"，不作为 Primary 路径
//
// 行为:
//   - 每次 Tick 从感知组件获取已感知的敌人列表
//   - 按距离排序，选取最近的合法目标
//   - 写入 BB: TargetActor = 最近敌人
//   - 若无目标，清空 BB Key
//
// 依赖:
//   - AIPerceptionComponent 已正确配置 SightConfig
//   - BB 中存在 "TargetActor" (Object) Key

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/Services/BTService_BlackboardBase.h"
#include "BTService_RefreshTarget.generated.h"

class ABaseCharacter;
class AAIController; // 【P0 修复】IsHostileTo 参数

/** 本模块专用的日志类别 */
DECLARE_LOG_CATEGORY_EXTERN(LogBTServiceRefreshTarget, Log, All);

/**
 * UBTService_RefreshTarget
 *
 * 刀战 AI 的目标仲裁服务
 * 挂在 BT Root 下的服务节点，每隔 Interval 秒刷新目标
 *
 * 双重写入路径:
 *   - Primary:   直接查 AIPerception.GetKnownPerceivedActors（完全可靠）
 *   - Override:  NearbyThreat（极近距离，由 OnTargetDetected 写入）
 */
UCLASS()
class METALSLUG01_API UBTService_RefreshTarget : public UBTService_BlackboardBase
{
	GENERATED_BODY()

public:
	UBTService_RefreshTarget();

protected:
	/** 服务激活时的回调（立即刷新一次目标） */
	virtual void OnBecomeRelevant(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;

	/** 服务失效时的回调（清空 BB TargetActor） */
	virtual void OnCeaseRelevant(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) override;

	/** 节点 Tick（Interval 控制刷新频率） */
	virtual void TickNode(UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds) override;

	/** 返回节点描述（UE 编辑器显示） */
	virtual FString GetStaticDescription() const override;

private:
	/** 从 AIPerception 获取已感知到的敌人列表，按距离排序返回最近目标 */
	AActor* FindNearestSensedTarget(UBehaviorTreeComponent& OwnerComp) const;

	/** 将目标写入 BB TargetActor Key */
	void UpdateBlackboardTarget(UBehaviorTreeComponent& OwnerComp, AActor* NewTarget) const;

	/** 检查目标是否仍然有效（未死亡、可被攻击） */
	bool IsTargetValid(AActor* Target) const;

	/**
	 * 【P0 修复】阵营敌对判定
	 * 走 IGenericTeamAgentInterface, 同阵营直接 false (不让 AI 互殴)
	 */
	bool IsHostileTo(AAIController* AIC, AActor* Target) const;
};
