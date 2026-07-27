// Copyright (c) 2026.
//
// 【v110 2026.07.30 生化模式 AI】BTDecorator — 母体太近则后退实现
//
// 职责:
//   - 读取 BB.DistanceToMother
//   - 从 ConfigSO 读取 RetreatDistanceThreshold
//   - 返回 true (后退) / false (不后退)
//
// 大厂原则:
//   - 纯条件判断, 无副作用
//   - 阈值从 ConfigSO 派生 (策划可调)

#include "Systems/AI/Decorators/BTDecorator_MotherTooClose.h"

#include "AIController.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "GameFramework/Pawn.h"

#include "Characters/BaseCharacter.h"
#include "Systems/BaseAIController.h"
#include "Systems/AI/AIRuntimeConfigComponent.h"
#include "Systems/AI/AIBehaviorTypes.h"
#include "Data/AI/AIBehaviorConfigSO.h"

UBTDecorator_MotherTooClose::UBTDecorator_MotherTooClose()
{
	NodeName = TEXT("Mother Too Close");

	// BB Key 过滤器
	DistanceKey.AddFloatFilter(this,
		GET_MEMBER_NAME_CHECKED(UBTDecorator_MotherTooClose, DistanceKey));

	// 【v110 大厂可观测性】激活时打印日志
	bNotifyActivation = true;
}


FString UBTDecorator_MotherTooClose::GetStaticDescription() const
{
	return FString::Printf(
		TEXT("母体太近后退\n"
			 "- 读取 BB.DistanceToMother\n"
			 "- 读取 ConfigSO.RetreatDistanceThreshold\n"
			 "- Distance < Threshold → 后退 (true)\n"
			 "- Distance >= Threshold → 不后退 (false)\n"
			 "- 无目标 (Dist<0) → 不后退 (false)\n"
			 "【BT 为主】Decorator 只决策, 后退执行归 BTTask_MoveAwayFromTarget."));
}


bool UBTDecorator_MotherTooClose::CalculateRawConditionValue(
	UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) const
{
	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	if (!BB)
	{
		UE_LOG(LogBehaviorTree, Error,
			TEXT("[BTDecorator_MotherTooClose] BB 为空, 阻止进入后退分支."));
		return false;
	}

	AAIController* AIC = OwnerComp.GetAIOwner();
	if (!AIC)
	{
		UE_LOG(LogBehaviorTree, Error,
			TEXT("[BTDecorator_MotherTooClose] AIC 为空, 阻止进入后退分支."));
		return false;
	}

	// ──────────────────────────────────────────────
	// 1. 读取 BB.DistanceToMother
	// ──────────────────────────────────────────────
	const float Distance = BB->GetValueAsFloat(DistanceKey.SelectedKeyName);

	// 无目标 → Distance = -1.f → 不后退
	if (Distance < 0.f)
	{
		UE_LOG(LogBehaviorTree, Display,
			TEXT("[BTDecorator_MotherTooClose] %s: 无母体目标 (Distance=%.1f), 阻止后退."),
			*AIC->GetName(),
			Distance);
		return false;
	}

	// ──────────────────────────────────────────────
	// 2. 从 ConfigSO 读取 RetreatDistanceThreshold
	// ──────────────────────────────────────────────
	ABaseAIController* BaseAIC = Cast<ABaseAIController>(AIC);
	if (!BaseAIC)
	{
		UE_LOG(LogBehaviorTree, Error,
			TEXT("[BTDecorator_MotherTooClose] %s: AIC 不是 ABaseAIController 派生, 阻止后退."),
			*AIC->GetName());
		return false;
	}

	const UAIRuntimeConfigComponent* RuntimeConfig = BaseAIC->GetRuntimeConfig();
	if (!RuntimeConfig)
	{
		UE_LOG(LogBehaviorTree, Error,
			TEXT("[BTDecorator_MotherTooClose] %s: RuntimeConfig 不可用. "
			     "【修复】检查 BP_ZombieAI 是否创建了 UAIRuntimeConfigComponent, "
			     "或 ModeRules.AIControllerClass 是否正确配置."),
			*AIC->GetName());
		return false;
	}

	const UAIBehaviorConfigSO* Config = RuntimeConfig->GetConfig();
	if (!Config)
	{
		UE_LOG(LogBehaviorTree, Error,
			TEXT("[BTDecorator_MotherTooClose] %s: ConfigSO 未应用. "
			     "【修复】检查 SetupZombieAI 或 ModeRules 配置是否正确."),
			*AIC->GetName());
		return false;
	}

	// 从 ConfigSO 读取后退阈值 (零兜底 — 字段有默认值 300cm)
	const float Threshold = Config->RetreatDistanceThreshold;

	// ──────────────────────────────────────────────
	// 3. 比较阈值
	// ──────────────────────────────────────────────
	const bool bTooClose = Distance < Threshold;

	UE_LOG(LogBehaviorTree, Display,
		TEXT("[BTDecorator_MotherTooClose] %s: DistanceToMother=%.0fcm vs Threshold=%.0fcm (ConfigSO) → %s"),
		*AIC->GetName(),
		Distance,
		Threshold,
		bTooClose ? TEXT("后退 (TRUE)") : TEXT("不后退 (FALSE)"));

	return bTooClose;
}
