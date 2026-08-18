// Copyright (c) 2026.
//
// 【v107 2026.07.28 生化模式 AI】BTService — 母体/人类目标选择实现

#include "Systems/AI/Services/BTService_UpdateZombieTargets.h"

#include "AIController.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "GameFramework/Pawn.h"

#include "Characters/BaseCharacter.h"
#include "Systems/BaseAIController.h"
#include "Systems/Mother/RoomMotherMutationSubsystem.h"
#include "Systems/Spawn/RoomSpawnSubsystem.h"
#include "Systems/AI/AIBehaviorTypes.h"
#include "Data/AI/AIBehaviorConfigSO.h"
#include "Systems/AI/AIRuntimeConfigComponent.h"

UBTService_UpdateZombieTargets::UBTService_UpdateZombieTargets()
{
	NodeName = TEXT("Update Zombie Targets");

	Interval = 0.25f;
	RandomDeviation = 0.f;
	bTickIntervals = true;

	NearestHumanTargetKey.AddObjectFilter(this, GET_MEMBER_NAME_CHECKED(UBTService_UpdateZombieTargets, NearestHumanTargetKey), AActor::StaticClass());
	NearestMotherTargetKey.AddObjectFilter(this, GET_MEMBER_NAME_CHECKED(UBTService_UpdateZombieTargets, NearestMotherTargetKey), AActor::StaticClass());
	TargetActorKey.AddObjectFilter(this, GET_MEMBER_NAME_CHECKED(UBTService_UpdateZombieTargets, TargetActorKey), AActor::StaticClass());
	PrimaryFireRangeKey.AddFloatFilter(this, GET_MEMBER_NAME_CHECKED(UBTService_UpdateZombieTargets, PrimaryFireRangeKey));
	HomeLocationKey.AddVectorFilter(this, GET_MEMBER_NAME_CHECKED(UBTService_UpdateZombieTargets, HomeLocationKey));
}


/**
 * @brief 生成 BT 节点描述 — 展示母体/人类目标选择频率与身份分支
 * @return 多行描述,展示 Interval/3 个目标 BB Key/未知身份 Pre-Mutation 写入 HomeLocation
 */
FString UBTService_UpdateZombieTargets::GetStaticDescription() const
{
	return FString::Printf(
		TEXT("每 %.2fs 按距离最近选目标:\n"
			 "- 母体 → 最近存活人类 (BB.NearestHumanTarget + BB.TargetActor)\n"
			 "- 人类 → 最近存活母体 (BB.NearestMotherTarget + BB.TargetActor)\n"
			 "- 未知身份 → 清目标 + 写 HomeLocation (Pre-Mutation 期间不动)"),
		Interval);
}


namespace
{
	/**
	 * 平面距离 (Z 忽略) — 与 URoomZombieRallySubsystem 同算法, 避免楼层差误判
	 */
	FORCEINLINE float ZombieFlatDistSq(const FVector& A, const FVector& B)
	{
		const float DX = A.X - B.X;
		const float DY = A.Y - B.Y;
		return DX * DX + DY * DY;
	}
}


/**
 * @brief Service 周期 Tick — 按身份选最近存活目标写入 BB
 * @param OwnerComp BT 组件引用
 * @param NodeMemory Service 节点内存(本类未使用)
 * @param DeltaSeconds 距上次 Tick 的间隔秒
 *
 * 流程:
 *   1. 派生 PrimaryFireRange(ConfigSO)→ 写 BB
 *   2. 母体 → 扫描全角色选最近存活人类,写 BB.NearestHumanTarget + TargetActor
 *   3. 人类 → 扫描母体账本选最近存活母体,写 BB.NearestMotherTarget + TargetActor
 *   4. 未知身份 → 清 3 个目标 Key,写 BB.HomeLocation(原地应急),实际移动由 BT_SelectRallyPoint 驱动
 *
 * 零兜底:Subsystem 缺失立即清 BB + Log Error,不允许 fallback.
 * v117/v118 大重构:未知身份不再"原地不动",Pre-Mutation 期间由 BT 驱动移动.
 */
void UBTService_UpdateZombieTargets::TickNode(
	UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	Super::TickNode(OwnerComp, NodeMemory, DeltaSeconds);

	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	if (!BB)
	{
		UE_LOG(LogBehaviorTree, Error,
			TEXT("[BTService_UpdateZombieTargets] BB 为空, Service 不工作!"));
		return;
	}

	AAIController* AIC = OwnerComp.GetAIOwner();
	if (!AIC)
	{
		UE_LOG(LogBehaviorTree, Error,
			TEXT("[BTService_UpdateZombieTargets] AIC 为空, Service 不工作!"));
		return;
	}

	ABaseCharacter* SelfPawn = Cast<ABaseCharacter>(AIC->GetPawn());
	if (!SelfPawn)
	{
		UE_LOG(LogBehaviorTree, Error,
			TEXT("[BTService_UpdateZombieTargets] %s: Pawn 不是 ABaseCharacter, Service 不工作!"), *AIC->GetName());
		return;
	}

	// ──────────────────────────────────────────────
	// 1. 派生 PrimaryFireRange (真理源 ConfigSO)
	// ──────────────────────────────────────────────
	float PrimaryFireRange = 0.f;
	if (const ABaseAIController* BaseAIC = Cast<ABaseAIController>(AIC))
	{
		if (const UAIRuntimeConfigComponent* RuntimeConfig = BaseAIC->GetRuntimeConfig())
		{
			if (const UAIBehaviorConfigSO* Config = RuntimeConfig->GetConfig())
			{
				PrimaryFireRange = Config->ZombiePrimaryFireRange;
				BB->SetValueAsFloat(PrimaryFireRangeKey.SelectedKeyName, PrimaryFireRange);
			}
		}
	}

	// ──────────────────────────────────────────────
	// 2. 收集场景角色 + 选目标
	// ──────────────────────────────────────────────
	const FVector SelfLoc = SelfPawn->GetActorLocation();
	const float MaxDistSq = PrimaryFireRange * PrimaryFireRange;

	URoomMotherMutationSubsystem* MotherSys = URoomMotherMutationSubsystem::Get(this);
	URoomSpawnSubsystem* SpawnSys = URoomSpawnSubsystem::Get(this);
	if (!MotherSys || !SpawnSys)
	{
		// 【零兜底】Subsystem 缺失 = 配置错, 清理 BB 让 BT 知道"无目标"
		BB->ClearValue(TargetActorKey.SelectedKeyName);
		BB->ClearValue(NearestHumanTargetKey.SelectedKeyName);
		BB->ClearValue(NearestMotherTargetKey.SelectedKeyName);

		UE_LOG(LogBehaviorTree, Error,
			TEXT("[BTService_UpdateZombieTargets] %s: Subsystem 缺失 (MotherSys=%d SpawnSys=%d)! "
			     L"【根因】1) Mode!=Zombie; 2) MotherMutationSubsystem::InitializeSubsystem 未调用."),
			*AIC->GetName(),
			!!MotherSys, !!SpawnSys);
		return;
	}

	const TArray<ABaseCharacter*> AllChars = SpawnSys->GetAllBattleCharacters();

	ABaseCharacter* BestTarget = nullptr;
	float BestDistSq = TNumericLimits<float>::Max();

	if (SelfPawn->bIsMother)
	{
		// 母体 → 最近存活人类
		for (ABaseCharacter* Char : AllChars)
		{
			if (!IsValid(Char))   { continue; }
			if (Char == SelfPawn) { continue; }
			if (Char->IsDead())   { continue; }
			if (Char->bIsMother) { continue; } // 母体不要母体

			const float DistSq = ZombieFlatDistSq(SelfLoc, Char->GetActorLocation());
			if (MaxDistSq > 0.f && DistSq > MaxDistSq) { continue; }

			if (DistSq < BestDistSq)
			{
				BestDistSq = DistSq;
				BestTarget = Char;
			}
		}

		BB->SetValueAsObject(NearestHumanTargetKey.SelectedKeyName, BestTarget);
		BB->SetValueAsObject(TargetActorKey.SelectedKeyName, BestTarget);
		BB->ClearValue(NearestMotherTargetKey.SelectedKeyName);

		UE_LOG(LogBehaviorTree, Display,
			TEXT("[BTService_UpdateZombieTargets] %s: 母体, 扫描 %d 个角色, 找到人类目标=%s (Dist=%.0f)"),
			*AIC->GetName(), AllChars.Num(),
			BestTarget ? *BestTarget->GetName() : TEXT("NONE"),
			BestTarget ? FMath::Sqrt(BestDistSq) : -1.f);
	}
	else if (SelfPawn->bIsHuman)
	{
		// 人类 → 最近存活母体
		const TArray<TWeakObjectPtr<ABaseCharacter>>& Mothers = MotherSys->GetMotherCharacters();
		for (const TWeakObjectPtr<ABaseCharacter>& WeakMother : Mothers)
		{
			ABaseCharacter* Mother = WeakMother.Get();
			if (!IsValid(Mother))  { continue; }
			if (Mother == SelfPawn) { continue; }
			if (Mother->IsDead())  { continue; }

			const float DistSq = ZombieFlatDistSq(SelfLoc, Mother->GetActorLocation());
			if (MaxDistSq > 0.f && DistSq > MaxDistSq) { continue; }

			if (DistSq < BestDistSq)
			{
				BestDistSq = DistSq;
				BestTarget = Mother;
			}
		}

		BB->SetValueAsObject(NearestMotherTargetKey.SelectedKeyName, BestTarget);
		BB->SetValueAsObject(TargetActorKey.SelectedKeyName, BestTarget);
		BB->ClearValue(NearestHumanTargetKey.SelectedKeyName);

		UE_LOG(LogBehaviorTree, Display,
			TEXT("[BTService_UpdateZombieTargets] %s: 人类, 扫描 %d 个母体, 找到母体目标=%s (Dist=%.0f)"),
			*AIC->GetName(), Mothers.Num(),
			BestTarget ? *BestTarget->GetName() : TEXT("NONE"),
			BestTarget ? FMath::Sqrt(BestDistSq) : -1.f);
	}
	else
	{
	// ============================================================
	// 【v117 2026.08.01 重构 + v118 2026.08.01 bug fix】未知身份分支 — 不再"原地不动"
	//
	// 业务背景 (用户 2026.08.01 反馈):
	//   "AI 进入游戏就能移动, 无需等到生化变异倒计时结束才能移动"
	// 旧行为 (v107 原设计):
	//   - 未知身份 → ClearValue(TargetActor) + SetValueAsVector(HomeLocation, SelfLoc)
	//   - 含义: "Pre-Mutation 期间默认不动, 待在原地"
	//   - 这与业务期望冲突 — 出生就应该能移动
	//
	// 新行为 (v117):
	//   - 未知身份 → 仍清 TargetActor (信任 BTTask_SelectZombieRallyPoint + MoveTo 选真实目标)
	//   - 仍写 HomeLocation = SelfLoc (作为"原地应急"目标)
	//   - ★ 关键: BT 不动 这里写, 由 BT_ZombieModeAI 在 Unknown 路径上选 SelectRallyPoint → MoveTo
	//   - 这样 AI 出生后∶ BTService_UpdateZombieTargets 0.25s 派生一次 → BT 进入 Rally 分支
	//     → SelectRallyPoint 选最近点 → BTTask_MoveTo 走向集合点 → 变进入"移动状态"
	//
	// 【v118 2026.08.01 P0 修复】真根因 — v117 重构时把这一段从 else 分支里拆出来,
	//   忘了加 else 守卫!导致:
	//   - 上一行 bIsMother=true 分支 SetValueAsObject(NearestHumanTarget, BestTarget) 写入
	//   - 紧接着末尾无 if 守卫的 ClearValue(NearestHumanTarget) 又把它清空
	//   - 用户原话: "NearestHumanTarget 和 NearestMotherTarget 在 ai 是人类和母体都不显示"
	//   - 修复: 整段包进 else { ... } 内, 仅 bIsMother/bIsHuman 都为 false 时执行清理
	//
	// 大厂原则 - 零兜底:
	//   - 不可在这里隐藏 "原地不动" 的默认行为 (那是反模式):
	//         不动 能从 UI 看到就是 bug, 不允许
	//   - 不可静默调走动代码 (那是 SceneComponent 修改 + 与 BT 状态冲突)
	//   - 不可与 BTTask_SelectZombieRallyPoint 冲突 (这里是 BB 写入, 那边是 BT 决策)
	//
	// 大厂原则 - 与 BTService_UpdateZombieState 零重复:
	//   - State 写 BB.bIsMother / bIsHuman / AliveMotherCount / AliveHumanCount (身份+人数)
	//   - Targets 写 BB.TargetActor / NearestHuman / NearestMother (目标派生物)
	//   - 同一身份 bIsMother=true 时: State 写 BB.bIsMother=true, Targets 写 BB.NearestHumanTarget=BestTarget
	//     不重复 — 两个 Service 写不同的 BB Key, 互为补充, 同一真理源 (Pawn.bIsMother)
	//
	// 其实详情 (现在未改革):
	//   - 依然是 ClearValue + SetValueAsVector 模式
	//   - 被动的 "AI 动" 由 BT_ZombieModeAI 调用 BTTask_SelectZombieRallyPoint 驱动
	//   - 本 Task 需要在 BT 里挂上 → 选最近点 → 调动 MoveTo → AI 就会移动
	// ============================================================
	BB->ClearValue(TargetActorKey.SelectedKeyName);
	BB->ClearValue(NearestHumanTargetKey.SelectedKeyName);
	BB->ClearValue(NearestMotherTargetKey.SelectedKeyName);
	BB->SetValueAsVector(HomeLocationKey.SelectedKeyName, SelfLoc);

	UE_LOG(LogBehaviorTree, Display,
		TEXT("[BTService_UpdateZombieTargets] %s: 未知身份 (bIsMother=0 bIsHuman=0), Pre-Mutation 期间. "
		     L"BB.TargetActor 已清, BB.HomeLocation=(%s) 作为应急原地点. "
		     L"实际移动由 BT_ZombieModeAI 调度 BTTask_SelectZombieRallyPoint (v117 PeriodicReselect) 驱动. "
		     L"【设计预期】AI 出生即可移动."),
		*AIC->GetName(), *SelfLoc.ToString());
	}
}