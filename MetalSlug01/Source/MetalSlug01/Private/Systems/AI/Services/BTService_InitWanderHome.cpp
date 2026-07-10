// Copyright (c) 2026.

#include "Systems/AI/Services/BTService_InitWanderHome.h"

#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "AIController.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Actor.h"

#include "Systems/AI/AIBehaviorTypes.h"

UBTService_InitWanderHome::UBTService_InitWanderHome()
{
	NodeName = TEXT("Init Wander Home (单次写入出生点)");

	// 【大厂原则 - 写入时机】
	// 1) Interval 必须 > 0 + bTickIntervals=true 才能激活 TickNode
	// 2) bNotifyActivate=true → 触发 OnBecomeRelevant (BT 进入本 Service 作用域时)
	// 3) 单次写入: 我们用 OnBecomeRelevant 而非 TickNode — 减少重复代码 + 必然单次
	bNotifyBecomeRelevant = true;
	bNotifyTick = false;
	bNotifyCeaseRelevant = false;

	Interval = 1.0f;
	RandomDeviation = 0.f;
	bTickIntervals = true;

	// BB Key 配置: Vector 类型 (WanderHome 是 Vector)
	WanderHomeKey.AddVectorFilter(this,
		GET_MEMBER_NAME_CHECKED(UBTService_InitWanderHome, WanderHomeKey));
}

FString UBTService_InitWanderHome::GetStaticDescription() const
{
	return TEXT("【单次执行 Service】BT 进入作用域时, 把 AI Pawn 当前位置写入 BB.WanderHome.\n"
		"用于漫游分支: BTTask_FindRandomLocation 读 WanderHome 作为漫游中心.\n"
		"不每 Tick 重写: 覆盖会破坏\"回家\"行为.");
}

void UBTService_InitWanderHome::OnBecomeRelevant(
	UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	Super::OnBecomeRelevant(OwnerComp, NodeMemory);

	// 【v40.8.5 诊断】无论后续成功与否, 都先打 Log 标记 "Service 被触发了"
	// 便于排查 "Service 是否真的挂在 BT 编辑器 + 真的被 BT 执行"
	UE_LOG(LogBehaviorTree, Log,
		TEXT("[BTService_InitWanderHome] >>> OnBecomeRelevant ENTERED (Service 已被 BT 调用)"));

	// 【v40.8.4 大厂架构】单次写入 — OnBecomeRelevant 触发时就是 BT 启动第一次
	// 此时 BB 已实例化 (RunBehaviorTree(BT) 之后), GetBlackboardComponent() 返回有效指针

	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	if (!BB)
	{
		UE_LOG(LogBehaviorTree, Error,
			TEXT("[BTService_InitWanderHome] BB 不可用. 检查 BT 是否绑 BB_AI_Melee."));
		return;
	}

	AAIController* AIC = OwnerComp.GetAIOwner();
	if (!AIC)
	{
		UE_LOG(LogBehaviorTree, Error, TEXT("[BTService_InitWanderHome] AIController 不可用."));
		return;
	}

	APawn* AIPawn = AIC->GetPawn();
	if (!AIPawn)
	{
		UE_LOG(LogBehaviorTree, Error,
			TEXT("[BTService_InitWanderHome] AIC=%s Pawn 不可用."), *AIC->GetName());
		return;
	}

	// 【零兜底】Key 必须已配置 — 否则 UE 静默 no-op, AI 永远静止
	if (WanderHomeKey.SelectedKeyName.IsNone())
	{
		UE_LOG(LogBehaviorTree, Error,
			TEXT("[BTService_InitWanderHome] AIC=%s WanderHomeKey 未配置. "
			     "【UE 编辑器配置】BT 编辑器 → Service Init Wander Home → Details → WanderHomeKey 选 BB Key 'WanderHome'."),
			*AIC->GetName());
		return;
	}

	// 【零兜底】BB 必须有此 Key — UE 静默 no-op 防御
	if (BB->GetKeyID(WanderHomeKey.SelectedKeyName) == FBlackboard::InvalidKey)
	{
		UE_LOG(LogBehaviorTree, Error,
			TEXT("[BTService_InitWanderHome] AIC=%s BB 不存在 Key '%s'. "
			     "【UE 编辑器配置】打开 BB_AI_Melee.uasset → New Key → Key Name='WanderHome', Key Type=Vector."),
			*AIC->GetName(), *WanderHomeKey.SelectedKeyName.ToString());
		return;
	}

	// 写入出生点 — 单一真理源: Pawn 当前位置
	const FVector SpawnLocation = AIPawn->GetActorLocation();
	BB->SetValueAsVector(WanderHomeKey.SelectedKeyName, SpawnLocation);

	UE_LOG(LogBehaviorTree, Log,
		TEXT("[BTService_InitWanderHome] AIC=%s 写 BB.WanderHome=%s (漫游中心)"),
		*AIC->GetName(), *SpawnLocation.ToString());
}

void UBTService_InitWanderHome::TickNode(
	UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	// 【大厂原则 - 不重写】
	// 我们故意在 TickNode 里什么都不做 — OnBecomeRelevant 已单次写入
	// 如果每 Tick 重写, 会破坏 "玩家离开后 AI 跑回原出生点漫游" 的设计意图
	Super::TickNode(OwnerComp, NodeMemory, DeltaSeconds);
}