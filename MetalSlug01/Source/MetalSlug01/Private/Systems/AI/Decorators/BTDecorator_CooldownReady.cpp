// Copyright (c) 2026.

#include "Systems/AI/Decorators/BTDecorator_CooldownReady.h"

#include "AIController.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Float.h"

UBTDecorator_CooldownReady::UBTDecorator_CooldownReady()
{
	NodeName = TEXT("Cooldown Ready");

	// 配 FBlackboardKeySelector — 改 BB Key 名走 Details 面板自动同步
	CooldownEndTimeKey.AddFloatFilter(this,
		GET_MEMBER_NAME_CHECKED(UBTDecorator_CooldownReady, CooldownEndTimeKey));

	// 重评策略: 冷却结束的判定是"实时量"对比, 不需要让 BB 变化触发重评
	// (因为我们直接读 World.Time 与 BB.CooldownEndTime, BT 每次执行本分支都会重新评估)
	bNotifyBecomeRelevant = false;
	bNotifyTick = false;

	// FlowAbortMode: Self
	// 当 BT 已经在 Chase 分支运行时, 如果 CooldownEndTime 被 BTTask 写入 (< World.Time, 冷却结束)
	// BT 需要能自我中断去重新评估, 否则会"卡在 Chase 不攻击"
	FlowAbortMode = EBTFlowAbortMode::Self;
}

FString UBTDecorator_CooldownReady::GetStaticDescription() const
{
	return FString::Printf(TEXT("WorldTime >= BB.%s (冷却结束才通过)"),
		*CooldownEndTimeKey.SelectedKeyName.ToString());
}

bool UBTDecorator_CooldownReady::CalculateRawConditionValue(
	UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) const
{
	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	if (!BB)
	{
		return false;
	}

	UWorld* World = OwnerComp.GetWorld();
	if (!World)
	{
		return false;
	}

	// 读 BB.CooldownEndTime — 走 FBlackboardKeySelector (与项目其它 Key 配置风格一致)
	const float CooldownEndTime = BB->GetValueAsFloat(CooldownEndTimeKey.SelectedKeyName);

	// 读当前 GameTime — 这是"实时量", 每次评估都拿最新值
	const float CurrentTime = World->GetTimeSeconds();

	// CooldownEndTime <= 0 表示"从未设置过冷却" (BB 默认值), 视为已冷却 (首次攻击可进入)
	if (CooldownEndTime <= 0.f)
	{
		UE_LOG(LogTemp, Verbose,
			TEXT("[BTDecorator_CooldownReady] %s: CooldownEndTime=%.2f <= 0 → PASS (首次可攻击)"),
			*GetNameSafe(OwnerComp.GetAIOwner()), CooldownEndTime);
		return true;
	}

	// 当前时间 >= 冷却截止时间 → 冷却结束 → 放行
	const bool bReady = CurrentTime >= CooldownEndTime;
	UE_LOG(LogTemp, Verbose,
		TEXT("[BTDecorator_CooldownReady] %s: CurrentTime=%.2f vs CooldownEndTime=%.2f → %s"),
		*GetNameSafe(OwnerComp.GetAIOwner()), CurrentTime, CooldownEndTime,
		bReady ? TEXT("PASS") : TEXT("FAIL (冷却中)"));
	return bReady;
}
