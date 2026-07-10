// Copyright (c) 2026.

#include "Systems/AI/Tasks/BTTask_FindRandomLocation.h"

#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "AIController.h"
#include "GameFramework/Pawn.h"
#include "GameFramework/Actor.h"
#include "NavigationSystem.h"
#include "NavFilters/NavigationQueryFilter.h"
#include "Engine/World.h"

#include "Systems/BaseAIController.h"
#include "Systems/AI/AIRuntimeConfigComponent.h"
#include "Data/AI/AIBehaviorConfigSO.h"

UBTTask_FindRandomLocation::UBTTask_FindRandomLocation()
{
	NodeName = TEXT("Find Random Location (漫游原子)");

	// 同步任务: FindRandomLocation 是瞬时操作 (调 NavMesh API), 不阻塞 BT
	// 不需要 InProgress 异步模式
	bNotifyTick = false;

	OriginLocationKey.AddVectorFilter(this,
		GET_MEMBER_NAME_CHECKED(UBTTask_FindRandomLocation, OriginLocationKey));
	WanderTargetKey.AddVectorFilter(this,
		GET_MEMBER_NAME_CHECKED(UBTTask_FindRandomLocation, WanderTargetKey));
}

FString UBTTask_FindRandomLocation::GetStaticDescription() const
{
	return TEXT("【漫游原子能力】以 OriginLocation 为中心, WanderRadius (ConfigSO) 为半径, "
		"在 NavMesh 随机选可达点写入 WanderTarget.\n"
		"失败 = Failed (无兜底). ConfigSO.Movement.WanderRadius 唯一真理源.");
}

EBTNodeResult::Type UBTTask_FindRandomLocation::ExecuteTask(
	UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	Super::ExecuteTask(OwnerComp, NodeMemory);

	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	if (!BB)
	{
		// 【零兜底】BB 不可用 = BT 配置错, 不允许走任何 fallback
		UE_LOG(LogBehaviorTree, Error,
			TEXT("[BTTask_FindRandomLocation] BB 组件无效. 检查 BT 是否绑定了 BB_AI_Melee.uasset."));
		return EBTNodeResult::Failed;
	}

	AAIController* AIC = OwnerComp.GetAIOwner();
	if (!AIC)
	{
		UE_LOG(LogBehaviorTree, Error,
			TEXT("[BTTask_FindRandomLocation] AIController 无效."));
		return EBTNodeResult::Failed;
	}

	APawn* AIPawn = AIC->GetPawn();
	if (!AIPawn)
	{
		UE_LOG(LogBehaviorTree, Error,
			TEXT("[BTTask_FindRandomLocation] AIC=%s Pawn 无效. BT Wander 分支被 Possess 时序错触发?"),
			*AIC->GetName());
		return EBTNodeResult::Failed;
	}

	UWorld* World = AIC->GetWorld();
	if (!World)
	{
		UE_LOG(LogBehaviorTree, Error, TEXT("[BTTask_FindRandomLocation] World 无效."));
		return EBTNodeResult::Failed;
	}

	// 1. 读 OriginLocation (漫游中心) — 通常是 WanderHome (出生点)
	// 【v40.8.2 关键修复】Key 检查 — UE 静默行为
	//   UE 的 SetValueAsVector/GetValueAsVector 传入不存在的 FName Key 会静默 no-op
	//   这是导致 "WanderHome 和 WanderTarget 都是空" 的最常见根因 — Key 没在 BB 添加
	if (OriginLocationKey.SelectedKeyName.IsNone())
	{
		UE_LOG(LogBehaviorTree, Error,
			TEXT("[BTTask_FindRandomLocation] AIC=%s OriginLocationKey 未配置 (SelectedKeyName=None). "
			     "【UE 编辑器配置】在 BT 编辑器 → Find Random Location 节点 → Details → OriginLocationKey 选择 Key 名."),
			*AIC->GetName());
		return EBTNodeResult::Failed;
	}
	if (WanderTargetKey.SelectedKeyName.IsNone())
	{
		UE_LOG(LogBehaviorTree, Error,
			TEXT("[BTTask_FindRandomLocation] AIC=%s WanderTargetKey 未配置 (SelectedKeyName=None). "
			     "【UE 编辑器配置】在 BT 编辑器 → Find Random Location 节点 → Details → WanderTargetKey 选择 Key 名."),
			*AIC->GetName());
		return EBTNodeResult::Failed;
	}

	// 【v40.8.3 编译修复】BB 里必须有这 2 个 Key — UE 静默行为防御
	// 【之前 (v40.8.2) 我犯的错】 BB->GetKeyID() 返回 FBlackboard::FKey (struct), 不是 uint8
	//                              不能用 ! 当 bool, 必须用 == FBlackboard::InvalidKey
	//                              这是 UE 5.6 的 FKey 类型 — wrapper struct 而非索引
	// 这次用 UE 的标准 API: FBlackboard::FKey::IsValid() 或 == InvalidKey
	if (BB->GetKeyID(OriginLocationKey.SelectedKeyName) == FBlackboard::InvalidKey)
	{
		UE_LOG(LogBehaviorTree, Error,
			TEXT("[BTTask_FindRandomLocation] AIC=%s BB 不存在 Key '%s'. "
			     "【UE 编辑器配置】打开 BB_AI_Melee.uasset → New Key → Key Name='%s', Key Type=Vector."),
			*AIC->GetName(),
			*OriginLocationKey.SelectedKeyName.ToString(),
			*OriginLocationKey.SelectedKeyName.ToString());
		return EBTNodeResult::Failed;
	}
	if (BB->GetKeyID(WanderTargetKey.SelectedKeyName) == FBlackboard::InvalidKey)
	{
		UE_LOG(LogBehaviorTree, Error,
			TEXT("[BTTask_FindRandomLocation] AIC=%s BB 不存在 Key '%s'. "
			     "【UE 编辑器配置】打开 BB_AI_Melee.uasset → New Key → Key Name='%s', Key Type=Vector."),
			*AIC->GetName(),
			*WanderTargetKey.SelectedKeyName.ToString(),
			*WanderTargetKey.SelectedKeyName.ToString());
		return EBTNodeResult::Failed;
	}

	const FVector OriginLocation = BB->GetValueAsVector(OriginLocationKey.SelectedKeyName);

	// 【v40.8.4 抗 sentinel 浮点】UE BB Key 未初始化时返回的不是 (0,0,0)
	//   而是接近 MAX_FLOAT 的 sentinel (3.4e38) — 这是 UE BB 内部标记 "未初始化" 的约定
	//   必须检测这个值 — 否则 NavMesh 调用会基于一个不合理的点返回 false
	//
	//   用户日志 (Session1.log) 显示:
	//     OriginLocation=X=340282346638528859811704183484516925440 ...
	//   解读: 这是 Q_NAN/MAX_FLOAT sentinel, 不是真实坐标, 说明 BB.WanderHome Key 没值
	if (FMath::IsNaN(OriginLocation.X) || FMath::IsNaN(OriginLocation.Y) || FMath::IsNaN(OriginLocation.Z) ||
		FMath::IsNearlyEqual(OriginLocation.X, MAX_FLT) || FMath::IsNearlyEqual(OriginLocation.Y, MAX_FLT) || FMath::IsNearlyEqual(OriginLocation.Z, MAX_FLT))
	{
		UE_LOG(LogBehaviorTree, Error,
			TEXT("[BTTask_FindRandomLocation] AIC=%s OriginLocation 是 UE BB uninitialized sentinel (%s). "
			     "BB.WanderHome 没值 (BB Key 不在 / 没写入). "
			     "【修复路径】1) 打开 BB_AI_Melee.uasset → 必须有 Key 'WanderHome', Type=Vector "
			     "2) BT 编辑器挂 BTService_InitWanderHome (单次执行, 写 WanderHome = Pawn.Location) "
			     "3) BT 编辑器 → Find Random Location 节点 → OriginLocationKey = WanderHome"),
			*AIC->GetName(), *OriginLocation.ToString());
		return EBTNodeResult::Failed;
	}

	// 【v40.8.1 抗边缘】WanderHome 必须非 (0,0,0)
	//   实际出生位置可能是 (0,0,0) (地图原点), 但这种情况极少
	//   必须用 FVector::ZeroVector 严格比较, 不能用 IsNearlyZero (它有 1e-4 容差 — 浮点不精确)
	//   如果真的生在原点, 那不是 BTTask 的问题, 是地图原点 AI 这种极端边界
	if (OriginLocation == FVector::ZeroVector)
	{
		// 【零兜底】坐标为 (0,0,0) = WanderHome 未初始化或出生在地图原点
		// 拒绝 fallback 到当前位置: 那会导致 AI 永远在原地反复随机选点 = 抖动
		UE_LOG(LogBehaviorTree, Error,
			TEXT("[BTTask_FindRandomLocation] AIC=%s OriginLocation=(0,0,0). "
			     "WanderHome 未写入或 AI 出生在地图原点. "
			     "【修复路径】1) 检查 OnPossess 是否成功执行 (BB 应在 BT 启动前已写) "
			     "2) 如果 AI 真的出生在地图原点, 编辑 BB.WanderHome Key 类型确认是 Vector 而非 Quaternion "
			     "3) Wander 分支 WanderHome Key 在 BT 编辑器内绑定正确"),
			*AIC->GetName());
		return EBTNodeResult::Failed;
	}

	// 2. 读 WanderRadius — 单一真理源: DataAsset → AIRuntimeConfigComponent
	ABaseAIController* BaseAIC = Cast<ABaseAIController>(AIC);
	if (!BaseAIC)
	{
		UE_LOG(LogBehaviorTree, Error,
			TEXT("[BTTask_FindRandomLocation] AIC=%s 不是 ABaseAIController 派生. 配置错."),
			*AIC->GetName());
		return EBTNodeResult::Failed;
	}

	const UAIRuntimeConfigComponent* RuntimeConfig = BaseAIC->GetRuntimeConfig();
	if (!RuntimeConfig)
	{
		UE_LOG(LogBehaviorTree, Error,
			TEXT("[BTTask_FindRandomLocation] AIC=%s RuntimeConfig 组件不可用. "
			     "修复: 检查 BP_GruntAI 是否创建了 UAIRuntimeConfigComponent."),
			*AIC->GetName());
		return EBTNodeResult::Failed;
	}

	const UAIBehaviorConfigSO* Config = RuntimeConfig->GetConfig();
	if (!Config)
	{
		UE_LOG(LogBehaviorTree, Error,
			TEXT("[BTTask_FindRandomLocation] AIC=%s ConfigSO 未应用. "
			     "修复: 检查 SetupMeleeAI 是否调 ApplyConfig."),
			*AIC->GetName());
		return EBTNodeResult::Failed;
	}

	const FAIMovementParams Movement = RuntimeConfig->GetScaledMovement();
	const float WanderRadius = Movement.WanderRadius;
	if (WanderRadius <= 0.f)
	{
		// 【零兜底】WanderRadius = 0 或负数 = ConfigSO 配错, 拒绝用默认值
		UE_LOG(LogBehaviorTree, Error,
			TEXT("[BTTask_FindRandomLocation] AIC=%s WanderRadius=%.1f 必须 > 0. "
			     "【v54 修复】在 DA_AIBehaviorConfig_MeleeGrunt.uasset → Movement → WanderRadius 设置 (DA_AIProfile_*.uasset 已删除)."),
			*AIC->GetName(), WanderRadius);
		return EBTNodeResult::Failed;
	}

	// 3. 调 NavMesh 随机点 (UE 原子 API)
	UNavigationSystemV1* NavSys = UNavigationSystemV1::GetCurrent(World);
	if (!NavSys)
	{
		UE_LOG(LogBehaviorTree, Error,
			TEXT("[BTTask_FindRandomLocation] AIC=%s NavSystem 不可用. 地图必须放 NavMeshBoundsVolume."),
			*AIC->GetName());
		return EBTNodeResult::Failed;
	}

	FNavLocation RandomPoint;
	const bool bFound = NavSys->GetRandomReachablePointInRadius(
		OriginLocation, WanderRadius, RandomPoint);

	if (!bFound)
	{
		// 【零兜底】半径内找不到有效 NavMesh 点 = 地图配置错 (NavMesh 没覆盖出生点附近)
		// 拒绝 fallback 到当前位置: 那会导致 AI 永远在原地无法漫游
		UE_LOG(LogBehaviorTree, Warning,
			TEXT("[BTTask_FindRandomLocation] AIC=%s 在 OriginLocation=%s, Radius=%.0f 内找不到 NavMesh 随机点. "
			     "可能是: 1) NavMesh 没构建 2) OriginLocation 在 NavMesh 外 3) WanderRadius 太小"),
			*AIC->GetName(), *OriginLocation.ToString(), WanderRadius);
		return EBTNodeResult::Failed;
	}

	// 4. 写入 BB.WanderTarget (BTTask_MoveTo 绑这个 Key)
	BB->SetValueAsVector(WanderTargetKey.SelectedKeyName, RandomPoint.Location);
	UE_LOG(LogBehaviorTree, Log,
		TEXT("[BTTask_FindRandomLocation] AIC=%s 漫游选点: OriginLocation=%s, WanderRadius=%.0f, Target=%s"),
		*AIC->GetName(), *OriginLocation.ToString(), WanderRadius, *RandomPoint.Location.ToString());

	// 同步任务: 立即返回 Succeeded
	return EBTNodeResult::Succeeded;
}
