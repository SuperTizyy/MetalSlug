// Copyright (c) 2026.
// UBTDecorator_MotherSkillReady.cpp — 母体加速技能冷却装饰器

#include "Systems/AI/Decorators/BTDecorator_MotherSkillReady.h"
#include "AIController.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "GameFramework/Pawn.h"


UBTDecorator_MotherSkillReady::UBTDecorator_MotherSkillReady()
{
	// NodeName 显示在 BT 编辑器中
	NodeName = TEXT("MotherSkillReady");

	// 冷却截止时间默认 0 (从未施放过技能时直接放行)
	CooldownEndTimeKey.AddFloatFilter(this, GET_MEMBER_NAME_CHECKED(UBTDecorator_MotherSkillReady, CooldownEndTimeKey));
}


FString UBTDecorator_MotherSkillReady::GetStaticDescription() const
{
	return FString::Printf(
		TEXT("%s: WorldTime >= BB.%s ? CooldownReady : Wait"),
		*NodeName,
		*CooldownEndTimeKey.SelectedKeyName.ToString());
}


/**
 * @brief 母体加速技能冷却判定 — 比较 WorldTime 与 BB.CooldownEndTime
 * @param OwnerComp BT 组件引用, 用于获取 BB / AIController
 * @param NodeMemory Decorator 节点内存(本类未使用)
 * @return 当前 WorldTime >= CooldownEndTime → 技能冷却完毕允许释放
 *
 * 三层防御: BB/AIC/World 任一无效 → false 拒判. Key 不存在时 GetValueAsFloat
 * 默认返回 0, 此时视为"从未施放过技能"立即允许.
 */
bool UBTDecorator_MotherSkillReady::CalculateRawConditionValue(
	UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) const
{
	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	if (!BB)
	{
		UE_LOG(LogTemp, Verbose,
			TEXT("[BTDecorator_MotherSkillReady] BB 为空, 拒绝评估. OwnerComp=%s"),
			*OwnerComp.GetName());
		return false;
	}

	AAIController* AIC = OwnerComp.GetAIOwner();
	if (!AIC)
	{
		UE_LOG(LogTemp, Verbose,
			TEXT("[BTDecorator_MotherSkillReady] AIC 为空, 拒绝评估. OwnerComp=%s"),
			*OwnerComp.GetName());
		return false;
	}

	UWorld* World = AIC->GetWorld();
	if (!World)
	{
		UE_LOG(LogTemp, Verbose,
			TEXT("[BTDecorator_MotherSkillReady] World 无效, 拒绝评估. OwnerComp=%s"),
			*OwnerComp.GetName());
		return false;
	}

	// 读 BB.MotherSkillCooldownEndTime (Float, GameTime 秒)
	// GetValueAsFloat 返回 0 (UE 默认) 如果 Key 不存在或类型错误
	const float CooldownEndTime = BB->GetValueAsFloat(CooldownEndTimeKey.SelectedKeyName);
	const float CurrentTime = World->GetTimeSeconds();

	const bool bReady = CurrentTime >= CooldownEndTime;

	UE_LOG(LogTemp, Verbose,
		TEXT("[BTDecorator_MotherSkillReady] %s: CurrentTime=%.2f CooldownEndTime=%.2f → %s"),
		*AIC->GetName(),
		CurrentTime, CooldownEndTime,
		bReady ? TEXT("Ready") : TEXT("CoolingDown"));

	return bReady;
}
