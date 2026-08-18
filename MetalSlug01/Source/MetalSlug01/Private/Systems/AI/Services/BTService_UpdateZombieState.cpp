// Copyright (c) 2026.
//
// 【v107 2026.07.28 生化模式 AI】BTService — 身份 + 人数快照实现

#include "Systems/AI/Services/BTService_UpdateZombieState.h"

#include "AIController.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "GameFramework/Pawn.h"

#include "Characters/BaseCharacter.h"
#include "Systems/Mother/RoomMotherMutationSubsystem.h"
#include "Systems/AI/AIBehaviorTypes.h"

UBTService_UpdateZombieState::UBTService_UpdateZombieState()
{
	NodeName = TEXT("Update Zombie State");

	// 默认频率 — 业务可调 (ConfigSO.ZombieTargetRefreshIntervalSeconds 默认 0.25s)
	Interval = 0.25f;
	RandomDeviation = 0.f;
	bTickIntervals = true;

	// BB Key 过滤器
	bIsMotherKey.AddBoolFilter(this, GET_MEMBER_NAME_CHECKED(UBTService_UpdateZombieState, bIsMotherKey));
	bIsHumanKey.AddBoolFilter(this, GET_MEMBER_NAME_CHECKED(UBTService_UpdateZombieState, bIsHumanKey));
	AliveMotherCountKey.AddIntFilter(this, GET_MEMBER_NAME_CHECKED(UBTService_UpdateZombieState, AliveMotherCountKey));
	AliveHumanCountKey.AddIntFilter(this, GET_MEMBER_NAME_CHECKED(UBTService_UpdateZombieState, AliveHumanCountKey));
	TargetActorKey.AddObjectFilter(this, GET_MEMBER_NAME_CHECKED(UBTService_UpdateZombieState, TargetActorKey), AActor::StaticClass());
	NearestHumanTargetKey.AddObjectFilter(this, GET_MEMBER_NAME_CHECKED(UBTService_UpdateZombieState, NearestHumanTargetKey), AActor::StaticClass());
	NearestMotherTargetKey.AddObjectFilter(this, GET_MEMBER_NAME_CHECKED(UBTService_UpdateZombieState, NearestMotherTargetKey), AActor::StaticClass());
}


/**
 * @brief 生成 BT 节点描述 — 展示生化身份 + 人数快照频率与 BB Key 派生
 * @return 多行描述,展示 Interval/bIsMother-Human/AliveMother-Human/身份切换清理 BB
 */
FString UBTService_UpdateZombieState::GetStaticDescription() const
{
	return FString::Printf(
		TEXT("每 %.2fs 派生 BB:\n"
			 "- bIsMother / bIsHuman (Pawn 字段)\n"
			 "- AliveMotherCount / AliveHumanCount (业务账本)\n"
			 "- 身份切换 → 自动清理 TargetActor/NearestHuman/Mother"),
		Interval);
}


void UBTService_UpdateZombieState::TickNode(
	UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	Super::TickNode(OwnerComp, NodeMemory, DeltaSeconds);

	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	if (!BB)
	{
		UE_LOG(LogBehaviorTree, Error,
			TEXT("[BTService_UpdateZombieState] BB 为空, Service 不工作!"));
		return;
	}

	AAIController* AIC = OwnerComp.GetAIOwner();
	if (!AIC)
	{
		UE_LOG(LogBehaviorTree, Error,
			TEXT("[BTService_UpdateZombieState] AIC 为空, Service 不工作!"));
		return;
	}

	APawn* Pawn = AIC->GetPawn();
	if (!Pawn)
	{
		UE_LOG(LogBehaviorTree, Error,
			TEXT("[BTService_UpdateZombieState] %s: Pawn 为空, Service 不工作!"), *AIC->GetName());
		return;
	}

	ABaseCharacter* Char = Cast<ABaseCharacter>(Pawn);
	if (!Char)
	{
		UE_LOG(LogBehaviorTree, Error,
			TEXT("[BTService_UpdateZombieState] %s: Pawn 不是 ABaseCharacter, Service 不工作!"), *AIC->GetName());
		return;
	}

	// ──────────────────────────────────────────────
	// 1. 写身份 (真理源 = Pawn 字段, 已 Replicated)
	// ──────────────────────────────────────────────
	BB->SetValueAsBool(bIsMotherKey.SelectedKeyName, Char->bIsMother);
	BB->SetValueAsBool(bIsHumanKey.SelectedKeyName, Char->bIsHuman);

	// ──────────────────────────────────────────────
	// 2. 写人数快照 (真理源 = MotherMutationSubsystem 业务账本)
	// ──────────────────────────────────────────────
	int32 MotherCount = 0;
	int32 HumanCount = 0;
	URoomMotherMutationSubsystem* MotherSys = URoomMotherMutationSubsystem::Get(this);
	if (MotherSys)
	{
		MotherCount = MotherSys->GetAliveMotherCount();
		HumanCount  = MotherSys->GetAliveHumanCount();
		BB->SetValueAsInt(AliveMotherCountKey.SelectedKeyName, MotherCount);
		BB->SetValueAsInt(AliveHumanCountKey.SelectedKeyName, HumanCount);
	}
	else
	{
		// 【零兜底】Subsystem 缺失 = 配置错, 写入 0 让 BT 知道"无数据"
		BB->SetValueAsInt(AliveMotherCountKey.SelectedKeyName, 0);
		BB->SetValueAsInt(AliveHumanCountKey.SelectedKeyName, 0);
		UE_LOG(LogBehaviorTree, Error,
			TEXT("[BTService_UpdateZombieState] %s: MotherMutationSubsystem 未找到! "
			     "【修复路径】检查 ARoomGameMode::InjectSubsystemConfigs 是否调用了 InitializeSubsystem."),
			*AIC->GetName());
	}

	// ──────────────────────────────────────────────
	// 3. 身份切换检测 — 清理旧身份的目标
	// ──────────────────────────────────────────────
	const bool bCurrentIsMother = Char->bIsMother;
	const bool bCurrentIsHuman  = Char->bIsHuman;

	// 【v108 大厂可观测性】每 Tick 显示 BB 当前状态 (Display 级, 控制台默认可见)
	UE_LOG(LogBehaviorTree, Display,
		TEXT("[BTService_UpdateZombieState] %s: 快照写入 → bIsMother=%d bIsHuman=%d "
		     L"AliveMother=%d AliveHuman=%d \u2502 BB.bIsMother=%s BB.bIsHuman=%s \u2502 "
		     L"BB.AliveMotherCount=%d BB.AliveHumanCount=%d \u2502 "
		     L"AliveMotherKey=%s AliveHumanKey=%s"),
		*AIC->GetName(),
		bCurrentIsMother ? 1 : 0,
		bCurrentIsHuman  ? 1 : 0,
		MotherCount,
		HumanCount,
		BB->GetValueAsBool(bIsMotherKey.SelectedKeyName) ? TEXT("true") : TEXT("false"),
		BB->GetValueAsBool(bIsHumanKey.SelectedKeyName)  ? TEXT("true") : TEXT("false"),
		BB->GetValueAsInt(AliveMotherCountKey.SelectedKeyName),
		BB->GetValueAsInt(AliveHumanCountKey.SelectedKeyName),
		*AliveMotherCountKey.SelectedKeyName.ToString(),
		*AliveHumanCountKey.SelectedKeyName.ToString());

	// 未知身份 → 显式 Display 提醒 (大厂可观测性 — 根因立刻可见)
	// 【v117 2026.08.01】不作为 Warning, 仅作为 Detail, 业务决策上这是合法的"Pre-Mutation"状态
	if (!bCurrentIsMother && !bCurrentIsHuman)
	{
		UE_LOG(LogBehaviorTree, Verbose,
			TEXT("[BTService_UpdateZombieState] %s: 未知身份 (Pre-Mutation, bIsMother=0 bIsHuman=0). "
			     L"BT_ZombieModeAI 进入 Pre-Mutation 路径 → 选集合点 → MoveTo → AI 移动. "
			     L"变异后身份切换为 Mother/Human, BT 跳入对应战斗分支."),
			*AIC->GetName());
	}

	if (bHasLastIdentity)
	{
		const bool bIdentityChanged =
			(bLastIsMother != bCurrentIsMother) ||
			(bLastIsHuman  != bCurrentIsHuman);

		if (bIdentityChanged)
		{
			// 清理 TargetActor + NearestHuman + NearestMother
			BB->ClearValue(TargetActorKey.SelectedKeyName);
			BB->ClearValue(NearestHumanTargetKey.SelectedKeyName);
			BB->ClearValue(NearestMotherTargetKey.SelectedKeyName);

			UE_LOG(LogBehaviorTree, Display,
				TEXT("[BTService_UpdateZombieState] %s: 身份切换 (was=%d/%d \u2192 now=%d/%d), 清理 BB 旧目标."),
				*AIC->GetName(),
				bLastIsMother ? 1 : 0, bLastIsHuman ? 1 : 0,
				bCurrentIsMother ? 1 : 0, bCurrentIsHuman ? 1 : 0);
		}
	}

	bLastIsMother = bCurrentIsMother;
	bLastIsHuman  = bCurrentIsHuman;
	bHasLastIdentity = true;
}