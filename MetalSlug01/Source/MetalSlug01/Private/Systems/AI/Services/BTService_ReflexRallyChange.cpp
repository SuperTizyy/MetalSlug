// Copyright (c) 2026.
//
// 【v122 2026.08.01 生化模式 AI】BTService — 反射式换点 / 首次选点实现
//
// 【v125 2026.08.01】反射式换点升级: visited set + 冷却窗口 — 解决 A↔B 来回切换抖动
//
// v122 痛点:
//   BTService 0.2s Tick, 条件 1 (AI 距任意集合点 ≤ TriggerRadius) + 条件 2 (母体 ThreatRadius)
//   持续满足 → 每 0.2s 触发一次 PerformReflexChange → 选最近其他点 → 下一次又触发 → 来回切换
//   Session1.txt 现象: 同一 AI 在 200ms 内 RallyA → RallyB → RallyA → RallyB 切换
//
// v125 修复 (大厂 - visited set + 反射式换点冷却):
//   1. SelectRallyPoint_Nearest_Excluding → SelectRallyPoint_Nearest_ReflexChange
//      - 内部已加冷却窗口 (MinLockDurationBeforeReflexChange = 8s, 在 URoomZombieRallySubsystem)
//      - 内部已加 visited set 加权 (已访问过的点距离加大权重)
//   2. PerformReflexChange 写 BB 成功后 → RallySys->RecordRallyVisit(AIC, NewPoint->PointID)
//      - 写入 VisitHistoryByController (冷却窗口的真理源)
//
// 详见头文件注释. 关键实现要点:
//   1. 构造函数: 注册 2 个 BB Key 过滤器 + Interval 默认值
//   2. TickNode: 派生 2 条件 → 都满足 → PerformReflexChange → 写 BB
//   3. 3 个 helper: CheckRallyPointProximity / CheckMotherThreatProximity / PerformReflexChange
//
// 【v122 根因重写】条件 1 不再依赖 BB.LockedRallyPoint, 改为遍历账本任意集合点:
//   旧 (v120) 误读: "AI 距 BB.LockedRallyPoint ≤ TriggerRadius" → 出生永远 null → 永远没值
//   新 (v122) 正确: "AI 距任意集合点 ≤ TriggerRadius" → 首次选点 + 换点合一
//
// 错误处理 (大厂原则 — 零兜底):
//   - 任何"必须配"的字段无效 → Log Error + 退出
//   - 账本空 / 排除后空 → Log Error + 拒绝写 BB
//   - Subsystem 缺失 → Log Error + 退出 (不允许 fallback)
//
// 调试日志:
//   - Display: 触发 (首次选点 / 换点, 一次性, 业务可观测)
//   - Verbose: 条件不满足 (高频, 默认隐藏)
//   - Error: 配置错 / 账本错 (一次性, 强制修复)

#include "Systems/AI/Services/BTService_ReflexRallyChange.h"

#include "AIController.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "GameFramework/Pawn.h"

#include "Characters/BaseCharacter.h"
#include "World/Objectives/ZombieRallyPoint.h"
#include "Systems/Zombie/RoomZombieRallySubsystem.h"
#include "Systems/Mother/RoomMotherMutationSubsystem.h"
#include "Systems/BaseAIController.h"
#include "Systems/AI/AIRuntimeConfigComponent.h"
#include "Data/AI/AIBehaviorConfigSO.h"

UBTService_ReflexRallyChange::UBTService_ReflexRallyChange()
{
	NodeName = TEXT("Reflex Rally Change");

	// 默认 0.2s — 与 ConfigSO.ReflexChangeTickIntervalSeconds 同步
	// 策划在 BT 编辑器可单独调 Interval 字段 (override ConfigSO)
	Interval = 0.2f;
	RandomDeviation = 0.f;
	bTickIntervals = true;

	// BB Key 过滤器 — v122 删除 bRallyPointLocked (零值原则: 未锁点 = LockedRallyPoint == nullptr)
	LockedRallyPointKey.AddObjectFilter(this,
		GET_MEMBER_NAME_CHECKED(UBTService_ReflexRallyChange, LockedRallyPointKey),
		AActor::StaticClass());
	DistanceToRallyPointKey.AddFloatFilter(this,
		GET_MEMBER_NAME_CHECKED(UBTService_ReflexRallyChange, DistanceToRallyPointKey));
}


FString UBTService_ReflexRallyChange::GetStaticDescription() const
{
	return FString::Printf(
		TEXT("反射式换点 / 首次选点 Service (v122)\n"
		     "- 条件 1: AI 距任意集合点 ≤ TriggerRadius (本字段 %.0f cm, 0=用ConfigSO)\n"
		     "- 条件 2: AI 附近 %.0f cm 内存在任何存活母体\n"
		     "- 都满足 → 选下一个最近集合点 (排除当前 PointID) → 同步账本 → 写 BB\n"
		     "- 首次选点: BB.LockedRallyPoint=null 时也会触发 → 写入最近集合点\n"
		     "- 频率: %.2fs (本 Service Interval, 也可走 ConfigSO)"),
		RallyPointTriggerRadius,
		MotherThreatRadius,
		Interval);
}


void UBTService_ReflexRallyChange::TickNode(
	UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory, float DeltaSeconds)
{
	Super::TickNode(OwnerComp, NodeMemory, DeltaSeconds);

	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	if (!BB)
	{
		// 与现有 Service 家族一致 — BB 缺失静默退出 (BT 框架层已 Log Error)
		return;
	}

	AAIController* AIC = OwnerComp.GetAIOwner();
	if (!AIC)
	{
		return;
	}

	ABaseCharacter* SelfPawn = Cast<ABaseCharacter>(AIC->GetPawn());
	if (!SelfPawn || SelfPawn->IsDead())
	{
		// AI 死亡 / Pawn 失效 → 静默退出 (与 BTService_UpdateMotherDistance 等镜像)
		return;
	}

	// ──────────────────────────────────────────────
	// 1. 派生配置 (ConfigSO 真理源 + Service override 路径)
	// ──────────────────────────────────────────────
	float TriggerRadius = RallyPointTriggerRadius;
	float ThreatRadius = MotherThreatRadius;
	float VisitedBias = VisitBiasMultiplier;

	// 0 = 强制用 ConfigSO 值 (策划没在 BT 编辑器 override)
	if (TriggerRadius <= 0.f || ThreatRadius <= 0.f)
	{
		ABaseAIController* BaseAIC = Cast<ABaseAIController>(AIC);
		if (!BaseAIC)
		{
			UE_LOG(LogBehaviorTree, Error,
				TEXT("[BTService_ReflexRallyChange] %s: AIC 不是 ABaseAIController 派生, 无法读 ConfigSO. "
				     "【修复】检查 BT 资产里挂的 AIControllerClass."),
				*AIC->GetName());
			return;
		}

		const UAIRuntimeConfigComponent* RuntimeConfig = BaseAIC->GetRuntimeConfig();
		if (!RuntimeConfig)
		{
			UE_LOG(LogBehaviorTree, Error,
				TEXT("[BTService_ReflexRallyChange] %s: RuntimeConfig 不可用. "
				     "【修复】检查 BP_ZombieAI 是否创建了 UAIRuntimeConfigComponent."),
				*AIC->GetName());
			return;
		}

		const UAIBehaviorConfigSO* Config = RuntimeConfig->GetConfig();
		if (!Config)
		{
			UE_LOG(LogBehaviorTree, Error,
				TEXT("[BTService_ReflexRallyChange] %s: ConfigSO 未应用. "
				     "【修复】检查 SetupZombieAI 配置."),
				*AIC->GetName());
			return;
		}

		if (TriggerRadius <= 0.f)
		{
			TriggerRadius = Config->ReflexChangeRallyPointRadius;
		}
		if (ThreatRadius <= 0.f)
		{
			ThreatRadius = Config->ReflexChangeMotherThreatRadius;
		}
	}

	// 零兜底 — 配错 (ConfigSO = 0) 立即报错
	if (TriggerRadius <= 0.f)
	{
		UE_LOG(LogBehaviorTree, Error,
			TEXT("[BTService_ReflexRallyChange] %s: ReflexChangeRallyPointRadius = 0 (ConfigSO 配错). "
			     "【修复】DA_AIBehaviorConfig_ZombieHuman → Behavior|Zombie|ReflexChange → RallyPoint Radius > 0."),
			*AIC->GetName());
		return;
	}
	if (ThreatRadius <= 0.f)
	{
		UE_LOG(LogBehaviorTree, Error,
			TEXT("[BTService_ReflexRallyChange] %s: ReflexChangeMotherThreatRadius = 0 (ConfigSO 配错). "
			     "【修复】DA_AIBehaviorConfig_ZombieHuman → Behavior|Zombie|ReflexChange → Mother Threat Radius > 0."),
			*AIC->GetName());
		return;
	}

	// 【v125 2026.08.01】VisitedBias 派生 — 0 = 用 Subsystem 默认值
	//
	// v125 优先级链:
	//   1. BT 编辑器 override (VisitBiasMultiplier > 0) → 用本字段值
	//   2. BT 编辑器没 override (VisitBiasMultiplier = 0) → 用 URoomZombieRallySubsystem::DefaultVisitBiasMultiplier (默认 4.0)
	//
	// 大厂原则: Subsystem 默认值是真理源, BT Service 不重复硬编码
	if (VisitedBias <= 0.f)
	{
		URoomZombieRallySubsystem* TempRallySys = URoomZombieRallySubsystem::Get(this);
		VisitedBias = TempRallySys ? TempRallySys->DefaultVisitBiasMultiplier : 4.0f;
	}

	// ──────────────────────────────────────────────
	// 2.5 【v123 出生即选点 — 真根因修复】
	//    用户原话 (2026.08.01): "LockedRallyPoint 还是一进游戏就一直没值"
	//    历史根因:
	//      v120-v122: BTService_ReflexRallyChange 仅在"AI 距任意集合点 ≤ 触发半径 + 母体 ≤ 威胁半径"时写 BB
	//      → 但 AI 出生时这两个条件几乎从不同时满足 → BB.LockedRallyPoint 永远 null
	//      → BT 装饰器 / 移动任务读 BB.LockedRallyPoint = null → 全部分支拒判 → AI 静止
	//    修复: 在 TickNode 入口加一个"BornLockRallyPoint"分支
	//      - BB.LockedRallyPoint == null (出生未选) → 立即选最近集合点 → 写 BB → 同步账本 → 退出
	//      - BB.LockedRallyPoint != null (已选) → 走原条件 1+2 (反射式换点)
	//    这是"出生即锁"的初始状态语义, 与"反射式换点"语义正交, 不破坏现有逻辑
	// ──────────────────────────────────────────────
	URoomZombieRallySubsystem* BornRallySys = URoomZombieRallySubsystem::Get(this);
	if (!BornRallySys)
	{
		UE_LOG(LogBehaviorTree, Error,
			TEXT("[BTService_ReflexRallyChange] %s: BornLock 路径 RallySubsystem=null, 拒绝执行. "
			     "【修复】检查地图是否加载 URoomZombieRallySubsystem (默认 WorldSubsystem 自动生成)."),
			*AIC->GetName());
		return;
	}

	UObject* PriorLockedObj = BB->GetValueAsObject(LockedRallyPointKey.SelectedKeyName);
	AZombieRallyPoint* PriorLockedPoint = Cast<AZombieRallyPoint>(PriorLockedObj);
	if (!PriorLockedPoint || !IsValid(PriorLockedPoint))
	{
		// BB.LockedRallyPoint 为空 或 已销毁 (TWeakObjectPtr 失效) → 触发"出生即选"
		AZombieRallyPoint* InitialPoint = BornRallySys->SelectRallyPoint_Nearest(SelfPawn);
		if (!InitialPoint)
		{
			// 账本空 (地图没摆 BP_ZombieRallyPoint) → 不写 BB, 不报错 (Service 每 0.2s 重试, 账本非空就生效)
			UE_LOG(LogBehaviorTree, Verbose,
				TEXT("[BTService_ReflexRallyChange] %s: BornLock 路径账本空 (SelectRallyPoint_Nearest 返回 null), "
				     "等账本非空后自动生效. (检查地图是否摆 BP_ZombieRallyPoint)"),
				*AIC->GetName());
			return;
		}

		// 同步账本 (大厂原则 - 账本是真理源, BB 是镜像)
		const bool bLocked = BornRallySys->LockRallyPointForAI(AIC, InitialPoint->PointID);
		if (!bLocked)
		{
			// LockRallyPointForAI 内部已 Log Error, 不重复
			return;
		}

		// 写 BB (LockedRallyPoint + DistanceToRallyPoint)
		const float InitDist = FVector::Dist(SelfPawn->GetActorLocation(), InitialPoint->GetActorLocation());
		BB->SetValueAsObject(LockedRallyPointKey.SelectedKeyName, InitialPoint);
		BB->SetValueAsFloat(DistanceToRallyPointKey.SelectedKeyName, InitDist);

		// 打断 MoveTo 异步任务 (v122 修复 — BB 变了但 MoveTo 还在旧点)
		AIC->StopMovement();

		UE_LOG(LogBehaviorTree, Display,
			TEXT("[BTService_ReflexRallyChange] %s: 【v123 出生即选】LockedRallyPoint='%s' 距离=%.0fcm "
			     "BB.Key='%s' 写入完成. 后续条件满足时进入反射式换点逻辑."),
			*AIC->GetName(), *InitialPoint->PointID, InitDist,
			*LockedRallyPointKey.SelectedKeyName.ToString());
		return;
	}

	// ──────────────────────────────────────────────
	// 2. 条件 1: AI 距任意集合点 ≤ TriggerRadius (v122 真根因修复)
	// ──────────────────────────────────────────────
	URoomZombieRallySubsystem* RallySys = URoomZombieRallySubsystem::Get(this);
	if (!RallySys)
	{
		UE_LOG(LogBehaviorTree, Error,
			TEXT("[BTService_ReflexRallyChange] %s: RallySubsystem=null, 拒绝执行. "
			     "【修复】检查地图是否加载 URoomZombieRallySubsystem (默认 WorldSubsystem 自动生成)."),
			*AIC->GetName());
		return;
	}

	FString CurrentPointID;
	if (!CheckRallyPointProximity(SelfPawn, RallySys, BB, TriggerRadius, CurrentPointID))
	{
		// 条件 1 不满足 → 退出 (不写 BB, 不换点)
		UE_LOG(LogBehaviorTree, Verbose,
			TEXT("[BTService_ReflexRallyChange] %s: 条件 1 不满足 (AI 距任意集合点 > %.0fcm 或账本空)."),
			*AIC->GetName(), TriggerRadius);
		return;
	}

	// ──────────────────────────────────────────────
	// 3. 条件 2: AI 附近 ThreatRadius 范围内有存活母体
	// ──────────────────────────────────────────────
	URoomMotherMutationSubsystem* MotherSys = URoomMotherMutationSubsystem::Get(this);
	if (!MotherSys)
	{
		UE_LOG(LogBehaviorTree, Error,
			TEXT("[BTService_ReflexRallyChange] %s: MotherMutationSubsystem 不可用. "
			     "【修复】检查地图是否加载 URoomMotherMutationSubsystem (默认 WorldSubsystem 自动生成)."),
			*AIC->GetName());
		return;
	}

	if (!CheckMotherThreatProximity(SelfPawn, MotherSys, ThreatRadius))
	{
		// 条件 2 不满足 → 退出
		UE_LOG(LogBehaviorTree, Verbose,
			TEXT("[BTService_ReflexRallyChange] %s: 条件 2 不满足 (%.0fcm 范围内无存活母体)."),
			*AIC->GetName(), ThreatRadius);
		return;
	}

	// ──────────────────────────────────────────────
	// 4. 两条件都满足 → 触发换点 (RallySys 已在步骤 2 派生, 此处复用)
	// ──────────────────────────────────────────────
	PerformReflexChange(SelfPawn, AIC, BB, RallySys, CurrentPointID, VisitedBias);
}


// ============================================================
// Private Helpers
// ============================================================

float UBTService_ReflexRallyChange::ComputeFlatDistanceSq(const FVector& A, const FVector& B)
{
	// 平面距离平方 (Z 轴忽略) — 与 URoomZombieRallySubsystem::ComputeFlatDistanceSq 镜像
	// 楼层差异不应影响"附近"判定
	const float DX = A.X - B.X;
	const float DY = A.Y - B.Y;
	return DX * DX + DY * DY;
}


bool UBTService_ReflexRallyChange::CheckRallyPointProximity(
	ABaseCharacter* SelfPawn, URoomZombieRallySubsystem* RallySys,
	UBlackboardComponent* BB, float TriggerRadius, FString& OutPointID) const
{
	OutPointID.Empty();

	// 【v122 根因修复】用户原话语义: "AI 在地图中任意的集合点附近 100cm"
	// 旧实现 (v120) 误解为 "AI 距离当前 BB.LockedRallyPoint ≤ TriggerRadius"
	//   → 出生时 BB.LockedRallyPoint = nullptr → 永远拒判 → 永远没值
	// 正确语义: 遍历账本全部集合点, AI 距任意一个集合点 ≤ TriggerRadius 即视为"在集合点附近"
	//
	// 大厂原则 - 走单一真理源业务账本 (URoomZombieRallySubsystem), 不 GetAllActorsOfClass
	if (!RallySys)
	{
		// RallySys null 已经在 TickNode 入口校验过, 这里是防御
		return false;
	}

	const TArray<AZombieRallyPoint*> AllPoints = RallySys->GetAllRallyPoints();
	if (AllPoints.Num() == 0)
	{
		// 账本空 → 条件 1 不满足 (没集合点可判断)
		return false;
	}

	const FVector SelfLoc = SelfPawn->GetActorLocation();
	const float TriggerRadiusSq = TriggerRadius * TriggerRadius;

	// 找出最近的一个集合点 (供 PerformReflexChange 写 BB 使用)
	AZombieRallyPoint* NearestPoint = nullptr;
	float NearestDistSq = TNumericLimits<float>::Max();

	for (AZombieRallyPoint* Point : AllPoints)
	{
		if (!IsValid(Point))
		{
			continue; // TWeakObjectPtr 失效 = 集合点销毁
		}

		const float DistSq = ComputeFlatDistanceSq(SelfLoc, Point->GetActorLocation());
		if (DistSq <= TriggerRadiusSq && DistSq < NearestDistSq)
		{
			NearestPoint = Point;
			NearestDistSq = DistSq;
		}
	}

	if (!NearestPoint)
	{
		// 没找到 ≤ TriggerRadius 的集合点 → 条件 1 不满足
		// 注: 不 Log Error, 高频路径, Verbose 日志放行
		return false;
	}

	// 条件 1 满足 — 返回最近集合点供 PerformReflexChange 写入 BB
	OutPointID = NearestPoint->PointID;
	return true;
}


bool UBTService_ReflexRallyChange::CheckMotherThreatProximity(
	ABaseCharacter* SelfPawn, URoomMotherMutationSubsystem* MotherSys, float ThreatRadius) const
{
	const FVector SelfLoc = SelfPawn->GetActorLocation();
	const float ThreatRadiusSq = ThreatRadius * ThreatRadius;

	// 遍历母体账本 (URoomMotherMutationSubsystem::MotherCharacters)
	// 大厂原则 - 走单一真理源业务账本, 不 GetAllActorsOfClass
	const TArray<TWeakObjectPtr<ABaseCharacter>>& Mothers = MotherSys->GetMotherCharacters();
	for (const TWeakObjectPtr<ABaseCharacter>& WeakMother : Mothers)
	{
		ABaseCharacter* Mother = WeakMother.Get();
		if (!IsValid(Mother))   { continue; } // TWeakObjectPtr 失效
		if (Mother->IsDead())  { continue; } // 死亡不算威胁

		const float DistSq = ComputeFlatDistanceSq(SelfLoc, Mother->GetActorLocation());
		if (DistSq <= ThreatRadiusSq)
		{
			// 找到任何一个在威胁范围内的存活母体 → 条件 2 满足
			return true;
		}
	}

	// 遍历完无命中
	return false;
}


bool UBTService_ReflexRallyChange::PerformReflexChange(
	ABaseCharacter* SelfPawn, AController* AIC, UBlackboardComponent* BB,
	URoomZombieRallySubsystem* RallySys, const FString& CurrentPointID,
	float VisitedBias)
{
	// 0. 读 BB.LockedRallyPoint (用于日志区分"首次选点" vs "换点" + 决定排除 ID)
	UObject* PriorLockedObj = BB->GetValueAsObject(LockedRallyPointKey.SelectedKeyName);
	AZombieRallyPoint* PriorLockedPoint = Cast<AZombieRallyPoint>(PriorLockedObj);
	const bool bIsFirstLock = !PriorLockedPoint;

	// 【v124 真根因修复】必须排除"BB 锁点", 不是"AI 附近的集合点"
	//
	// 旧 (v122) 反模式:
	//   排除 = CurrentPointID (条件 1 命中 = "AI 距 ≤ TriggerRadius 的最近集合点")
	//   反例: AI 出生锁 A, 走到 B 附近, B ≤ TriggerRadius, 母体 50cm 内 → 触发
	//         → 排除 B, SelectRallyPoint_Nearest_Excluding → 选 A (BB 没变!)
	//         → AI 看着"没换点" (实际锁点 A 没变, 但用户预期"换成 B")
	//
	// 正确 (v124):
	//   排除 = BB.LockedRallyPoint.PointID (无论 BB 锁的点在不在 TriggerRadius 内都排除)
	//   - AI 在 B 附近, BB 锁 A → 排除 A → 选 B → BB 真变了 ✓
	//   - AI 在 A 附近, BB 锁 A → 排除 A → 选 B → 反射换点 ✓
	//   - BB 没锁点 (首次选点路径, 不走这里) → 不排除 → 选最近
	//
	// 大厂原则 - 零值原则: BB 是真理源, 排除必须按 BB 当前值, 不能按"附近点"
	FString ExcludePointID;
	if (PriorLockedPoint && IsValid(PriorLockedPoint))
	{
		ExcludePointID = PriorLockedPoint->PointID;
	}

	// 1. 选下一个最近集合点 (排除 BB 锁点本身, 避免"换点 = 没换")
	//    v122: 若账本只有 1 个点, 排除后空 → 触发失败 (Log Error) → 强制策划补点
	//
	// 【v125 2026.08.01】升级 v124 → v125:
	//   v124: SelectRallyPoint_Nearest_Excluding — 纯最近返回 (无冷却, 无 visited)
	//   v125: SelectRallyPoint_Nearest_ReflexChange — 加冷却窗口 + visited set 加权
	//
	// 业务效果 (用户 2026.08.01 反馈 "来回切换"):
	//   v124: 同帧 200ms 内 A↔B 来回跳 (Service 0.2s Tick + 条件持续满足)
	//   v125: A→B 后 8s 内 B 冷却, 不会换走 (不会 A↔B 来回)
	//       已访问过的点距离加大权重, 优先选"没去过"的点
	//
	// 【v125 2026.08.01】VisitedBias 派生 (TickNode 1.段):
	//   - BT 编辑器 override (VisitBiasMultiplier > 0) → 用本字段值
	//   - BT 编辑器没 override (VisitBiasMultiplier = 0) → 用 Subsystem 默认 4.0
	// 这里用 VisitedBias 局部变量 (TickNode 1.段已派生)
	AZombieRallyPoint* NewPoint = RallySys->SelectRallyPoint_Nearest_ReflexChange(
		SelfPawn, AIC, ExcludePointID, VisitedBias);

	if (!NewPoint)
	{
		// 计数兜底: 检查是"冷却中"还是"账本空"
		// 三种可能原因:
		//   - 账本空
		//   - 排除后空 (只有 1 个集合点)
		//   - 冷却中 (v125 新增) — 反射式换点冷却期, 拒绝换点
		//
		// 业务上:
		//   - 冷却中 (v125) 是正常节流, 不应该是 Error
		//   - 账本空 / 排除后空 是配置错, 必须 Error
		//
		// 区分方法: 查账本 + 冷却时间
		if (RallySys->GetAllRallyPoints().Num() <= 1)
		{
			UE_LOG(LogBehaviorTree, Error,
				TEXT("[BTService_ReflexRallyChange] %s: 反射式换点失败 — 排除 BB 锁点 PointID='%s' 后无可用集合点. "
				     "【修复】地图里至少放 2 个 BP_ZombieRallyPoint, 避免锁点被排除后无候选."),
				*AIC->GetName(), *ExcludePointID);
		}
		// 否则认为 SelectRallyPoint_Nearest_ReflexChange 内部已 Log Verbose (冷却中), 这里不重复 Log
		return false;
	}

	// 2. 同步账本 (复用 LockRallyPointForAI)
	// v117 修订: 同一 Controller 从 A 点换到 B 点 → 覆盖旧记录 (账本同步, 合法)
	// v122: 首次选点 (PriorLockedPoint=null) 也允许, 账本新登记
	const bool bLocked = RallySys->LockRallyPointForAI(AIC, NewPoint->PointID);
	if (!bLocked)
	{
		// LockRallyPointForAI 内部已 Log Error, 这里不再重复
		return false;
	}

	// 3. 写 BB (v122: 只写 2 个 Key — LockedRallyPoint + DistanceToRallyPoint)
	//    零值原则: 已锁点 = LockedRallyPoint != nullptr; 未锁点 = LockedRallyPoint == nullptr
	//    不需要 bRallyPointLocked bool 标记 (那是冗余重复)
	const float NewDist = FVector::Dist(SelfPawn->GetActorLocation(), NewPoint->GetActorLocation());

	BB->SetValueAsObject(LockedRallyPointKey.SelectedKeyName, NewPoint);
	BB->SetValueAsFloat(DistanceToRallyPointKey.SelectedKeyName, NewDist);

	// 【v125 2026.08.01】记录访问历史 — 写 BB 成功后立即调
	//
	// 为什么在写 BB 后调 (不是 LockRallyPointForAI 里调):
	//   - LockRallyPointForAI 是账本操作 (账本 = 真理源), RecordRallyVisit 是 visited set 操作
	//   - 职责分离: Subsystem 内部函数 vs BT Service 调用方
	//   - 账本可能在 LockRallyPointForAI 失败/拒登记, 但 visited set 仍可能更新 (业务上)
	//   - 当前实现: 写 BB 成功才记, 避免"账本没锁+visited 有记录"的数据不一致
	//
	// 大厂原则: 真理源 = 账本 (LockedRallyByAI), visited set = 服务于反射式换点冷却的辅助账本
	//   两个账本解耦, 各自独立管理
	RallySys->RecordRallyVisit(AIC, NewPoint->PointID);

	// ★ 【v122 关键修复】打断 MoveTo 异步任务
	// 根因 (用户 2026.08.01 反馈 "达到了条件 LockedRallyPoint 没更新集合点"):
	//   UE 5.6 BT 的 MoveTo 节点是异步任务: 收到 BB.LockedRallyPoint 变化后, MoveTo 仍在前往旧点的途中
	//   - 旧 MoveTo 不会因 BB.LockedRallyPoint 改了而自动取消
	//   - BT 决策下一 Tick 走 MoveTo 节点 → 拿 BB.LockedRallyPoint (新点) → 重新 MoveTo
	//   - 但: BT 可能还在 MoveTo 节点执行中, 本 Tick 不会重入 MoveTo
	//   - 表现为: "AI 看着像没换点" (实际锁点变了, 但 MoveTo 仍在前往旧点)
	// 修复: 写 BB 后立即 StopMovement, 让 MoveTo 异步任务"半途夭折" → BT 下一 Tick 重新派发 MoveTo
	// 注: StopMovement 是 AController 公共 API, 只中断当前 MoveTo 不影响 BT 决策 — 大厂异步任务打断标准做法
	//
	// 防御: StopMovement 只对活着的 AI 调, IsDead 已在校验路径保证
	if (AIC)
	{
		AIC->StopMovement();
	}

	// 4. 写 BB 后的实际值校验 (零兜底 — 大厂可观测)
	// 读回 BB 确认 Service 真的写入了,避免反射式换点"看起来触发但 BB 实际没换"
	UObject* WrittenObj = BB->GetValueAsObject(LockedRallyPointKey.SelectedKeyName);
	AZombieRallyPoint* WrittenPoint = Cast<AZombieRallyPoint>(WrittenObj);
	const bool bWrittenOK = (WrittenPoint == NewPoint);

	// 5. Display 日志 (业务可观测 — 一次性, 不刷屏)
	// v122 日志完整: 区分"首次选点" vs "换点", AI 附近最近点 vs 写入 BB 的新点
	UE_LOG(LogBehaviorTree, Display,
		TEXT("[BTService_ReflexRallyChange] %s: %s触发! "
		     "AI 附近集合点='%s' (距离 ≤ 触发阈值) → 锁账本新点='%s' (距离=%.0fcm). "
		     "BB.Key='%s' 写入校验=%s. "
		     "已 StopMovement 打断旧 MoveTo."),
		*AIC->GetName(),
		bIsFirstLock ? TEXT("【首次选点】") : TEXT("【反射式换点】"),
		*CurrentPointID,
		*NewPoint->PointID, NewDist,
		*LockedRallyPointKey.SelectedKeyName.ToString(),
		bWrittenOK ? TEXT("OK") : TEXT("FAILED"));

	if (!bWrittenOK)
	{
		// 【零兜底】BB 写入失败最常见原因:
		//   1. FBlackboardKeySelector.SelectedKeyName 配错 (策划在 BT 编辑器没指定正确的 Key)
		//   2. BB 资产里 Key 类型是 Object 但 SelectedKeyName 拼写错误
		UE_LOG(LogBehaviorTree, Error,
			TEXT("[BTService_ReflexRallyChange] %s: BB.LockedRallyPoint 写入校验失败! "
			     "Service 字段 Key='%s' 与 BB 资产实际 Key 不匹配. "
			     "【修复】打开 BT_ZombieModeAI.uasset → BTService_ReflexRallyChange 节点 → 检查 LockedRallyPointKey 字段, 必须选 BB 资产中定义的那个 Key."),
			*AIC->GetName(), *LockedRallyPointKey.SelectedKeyName.ToString());
	}

	return true;
}
