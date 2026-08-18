// Copyright (c) 2026.
//
// 【v132 2026.08.02 P0 新增】BTDecorator — 有清晰射击线? 实现
//
// 详见 BTDecorator_HasClearShot.h 头文件注释
//
// 核心实现:
//   1. 从 BB 拿 Target / Self Actor
//   2. 调用 Self->CanBeSeenFrom(Observer=Self.Eyes, Target, ...) (UE 5.6 API)
//   3. 距离上限检查 (避免大范围远处 trace)
//   4. 根据 Policy 返回 Visible / Hidden

#include "Systems/AI/Decorators/BTDecorator_HasClearShot.h"

#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BehaviorTree.h"            // 【v132.1】InitializeFromAsset 需要
#include "BehaviorTree/BlackboardData.h"          // 【v132.1】UBlackboardData
#include "BehaviorTree/BlackboardComponent.h"
#include "AIController.h"
#include "GameFramework/Pawn.h"
#include "Perception/AISense_Sight.h"            // UAISense_Sight::EVisibilityResult
#include "Perception/AISightTargetInterface.h"    // FCanBeSeenFromContext

#include "Characters/BaseCharacter.h"             // 我们 override 的 IAISightTargetInterface

#if ENABLE_DRAW_DEBUG
#include "DrawDebugHelpers.h"
#endif

// 【v241.1 大厂架构】trace debug 可视化统一开关 — 见 Debug/MetalSlugDebugCVars.h
#include "Debug/MetalSlugDebugCVars.h"

namespace HasClearShotPrivate
{
	/** ObserverLocation = AI 眼睛位置 (业界共识:UE 5.3 forum 推荐) */
	static FVector ResolveObserverEyesLocation(AActor* Observer)
	{
		FVector EyesLoc;
		FRotator EyesRot;
		if (ABaseCharacter* BC = Cast<ABaseCharacter>(Observer))
		{
			BC->GetActorEyesViewPoint(EyesLoc, EyesRot);
		}
		else if (AAIController* AIC = Cast<AAIController>(Observer))
		{
			AIC->GetActorEyesViewPoint(EyesLoc, EyesRot);
		}
		else
		{
			EyesLoc = Observer->GetActorLocation();
			EyesRot = Observer->GetActorRotation();
		}
		return EyesLoc;
	}
}

UBTDecorator_HasClearShot::UBTDecorator_HasClearShot()
{
	NodeName = TEXT("Has Clear Shot?");

	// 注册过滤器,让 BT 编辑器下拉只显示 Object Key
	TargetActorKey.AddObjectFilter(this,
		GET_MEMBER_NAME_CHECKED(UBTDecorator_HasClearShot, TargetActorKey),
		AActor::StaticClass());

	SelfActorKey.AddObjectFilter(this,
		GET_MEMBER_NAME_CHECKED(UBTDecorator_HasClearShot, SelfActorKey),
		AActor::StaticClass());
}

/**
 * @brief 生成 BT 节点描述 — 显示 IAISightTargetInterface 调用链路与关键参数
 * @return 多行描述字符串,展示 TargetKey / SelfKey / Policy / MaxRange
 *
 * 仅用于 BT 编辑器可视化, 不参与运行时决策. 配错 Target Key → 提示拒绝运行.
 */
FString UBTDecorator_HasClearShot::GetStaticDescription() const
{
	const FString PolicyStr = (Policy == EHasClearShotPolicy::RequireVisible)
		? TEXT("Visible (默认)")
		: TEXT("Hidden");
	const FString TargetKeyStr = TargetActorKey.SelectedKeyName.IsNone()
		? TEXT("未配 TargetKey(拒绝运行)")
		: TargetActorKey.SelectedKeyName.ToString();
	const FString SelfKeyStr = SelfActorKey.SelectedKeyName.IsNone()
		? TEXT("默认 Self Pawn")
		: SelfActorKey.SelectedKeyName.ToString();

	return FString::Printf(TEXT(
		"调 IAISightTargetInterface::CanBeSeenFrom (v132 复用,零重复)\n"
		"  - Target Key = %s\n"
		"  - Self Key   = %s\n"
		"  - Policy     = %s\n"
		"  - MaxRange   = %.0f cm\n"
		"无 Target / Self / 越界 → Failed (零兜底)"),
		*TargetKeyStr, *SelfKeyStr, *PolicyStr, MaxRange);
}

// 【v132.1 业界共识 — InitializeFromAsset 必须 ResolveSelectedKey】
//
// 来源: UE 5.6 开发者论坛 (https://forums.unrealengine.com/t/blackboard-key-selector-getselectedkeyid-not-working/351066)
//   "The id seems to be failing... debug value 255..."
//   "you have to initialize your blackboard key selector in your cpp like this"
//   "void UMETA_Decorator_IsInRange::InitializeFromAsset(UBehaviorTree& Asset) {
//      Super::InitializeFromAsset(Asset);
//      UBlackboardData* BBAsset = GetBlackboardAsset();
//      if (ensure(BBAsset)) { m_RangeBlackboardKey.ResolveSelectedKey(*BBAsset); }
//    }"
//
// 不调 → SelectedKeyID = 255 (debug 值) → BB 查找永远失败
// 没 BT 资产时 (CDO) → ensure 失败可以接受,运行时 BT 才用
void UBTDecorator_HasClearShot::InitializeFromAsset(UBehaviorTree& Asset)
{
	Super::InitializeFromAsset(Asset);

	if (UBlackboardData* BBAsset = GetBlackboardAsset())
	{
		// 仅配过 key 名才 resolve (None → 不动)
		if (!TargetActorKey.SelectedKeyName.IsNone())
		{
			TargetActorKey.ResolveSelectedKey(*BBAsset);
		}
		if (!SelfActorKey.SelectedKeyName.IsNone())
		{
			SelfActorKey.ResolveSelectedKey(*BBAsset);
		}
	}
	else
	{
		UE_LOG(LogBehaviorTree, Display,
			TEXT("[HasClearShot] BBAsset 为空 (CDO 或没配 BB), 跳过 ResolveSelectedKey."));
	}
}

bool UBTDecorator_HasClearShot::CalculateRawConditionValue(
	UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) const
{
	using namespace HasClearShotPrivate;

	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	if (!BB)
	{
		UE_LOG(LogBehaviorTree, Display,
			TEXT("[HasClearShot] BB 无效,拒绝评估."));
		return false;
	}

	// 第 1 步: TargetActor — 必须配 (零兜底,无效 → false)
	UObject* TargetObj = BB->GetValueAsObject(TargetActorKey.SelectedKeyName);
	AActor* TargetActor = Cast<AActor>(TargetObj);
	if (!TargetActor || !IsValid(TargetActor))
	{
		// 【零兜底】无效 → 不静默跳过,Log Warning 让上层修复
		UE_LOG(LogBehaviorTree, Display,
			TEXT("[HasClearShot] TargetKey='%s' BB 空 / 无效, Decorator 拒判."),
			*TargetActorKey.SelectedKeyName.ToString());
		return false;
	}

	// 第 2 步: Self Actor — 默认用 OwnerComp.AIController.GetPawn()
	AActor* SelfActor = nullptr;
	if (!SelfActorKey.SelectedKeyName.IsNone())
	{
		UObject* SelfObj = BB->GetValueAsObject(SelfActorKey.SelectedKeyName);
		SelfActor = Cast<AActor>(SelfObj);
	}
	if (!SelfActor)
	{
		if (AAIController* AIC = OwnerComp.GetAIOwner())
		{
			SelfActor = AIC->GetPawn();
		}
	}
	if (!SelfActor || !IsValid(SelfActor))
	{
		// 【零兜底】无效 → false
		return false;
	}

	// 第 3 步: 距离上限检查 (业界共识方案,SightRadius 之外不评估)
	const FVector ObserverEyesLoc = ResolveObserverEyesLocation(SelfActor);
	const float DistToTarget = FVector::Dist(ObserverEyesLoc, TargetActor->GetActorLocation());
	if (DistToTarget > MaxRange)
	{
		UE_LOG(LogBehaviorTree, Display,
			TEXT("[HasClearShot] '%s' → '%s' Dist=%.0f > MaxRange=%.0f, 越界拒判."),
			*SelfActor->GetName(), *TargetActor->GetName(), DistToTarget, MaxRange);
		return false;
	}

	// 第 4 步: 调 IAISightTargetInterface::CanBeSeenFrom (UE 官方接口)
	//   业界共识 (UE 5.3 forum):
	//   "By overriding IAISightTargetInterface::CanBeSeenFrom you can do any number of LoS checks
	//    against different locations on your character..."
	//   UE 官方推荐: 让 Target Actor 自己处理视线,AI 不自己重写 trace 逻辑
	IAISightTargetInterface* TargetSight = Cast<IAISightTargetInterface>(TargetActor);
	if (!TargetSight)
	{
		// Target Actor 没实现 IAISightTargetInterface → 不能用此方案
		// 大厂原则 - 零兜底:这是 BP 配错,显式报 Warning
		UE_LOG(LogBehaviorTree, Warning,
			TEXT("[HasClearShot] Target='%s' 没实现 IAISightTargetInterface. "
				 "【v132 P0】确认 Target Actor 继承自 ABaseCharacter (BaseCharacter 已 override)."),
			*TargetActor->GetName());
		return false;
	}

	FCanBeSeenFromContext Context;
	Context.ObserverLocation = ObserverEyesLoc;
	Context.IgnoreActor = SelfActor; // 自己不要 ignore (AIPerception 通常也不 ignore)

	FVector SeenLocation = FVector::ZeroVector;
	int32 LoSChecks = 0;
	int32 AsyncChecks = 0;
	float SightStrength = 0.f;

	const UAISense_Sight::EVisibilityResult Result =
		TargetSight->CanBeSeenFrom(Context, SeenLocation, LoSChecks, AsyncChecks, SightStrength);

#if ENABLE_DRAW_DEBUG
	// 【v241 大厂架构】受全局 CVar g.MetalSlug.ShowTraceDebug 控制 — 默认 0 (关闭)
	if (bDrawDebugTrace && CVarShowTraceDebug.GetValueOnGameThread() != 0)
	{
		const FColor LineColor = (Result == UAISense_Sight::EVisibilityResult::Visible)
			? FColor::Green
			: FColor::Red;
		DrawDebugLine(GetWorld(), ObserverEyesLoc, TargetActor->GetActorLocation(),
			LineColor, false, 0.5f, 0, 2.f);
	}
#endif

	const bool bVisible = (Result == UAISense_Sight::EVisibilityResult::Visible);

	UE_LOG(LogBehaviorTree, Display,
		TEXT("[HasClearShot] '%s' → '%s' Dist=%.0f Result=%s SightStrength=%.2f → %s"),
		*SelfActor->GetName(), *TargetActor->GetName(), DistToTarget,
		(bVisible ? TEXT("Visible") : TEXT("NotVisible")),
		SightStrength,
		(Policy == EHasClearShotPolicy::RequireVisible) ? (bVisible ? TEXT("PASS") : TEXT("FAIL"))
		                                              : (bVisible ? TEXT("FAIL") : TEXT("PASS")));

	// 第 5 步: 匹配 Policy 输出
	switch (Policy)
	{
	case EHasClearShotPolicy::RequireVisible: return bVisible;
	case EHasClearShotPolicy::RequireHidden:  return !bVisible;
	default: return false;
	}
}
