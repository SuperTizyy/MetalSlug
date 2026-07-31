// Copyright (c) 2026. All Rights Reserved.
//
// 【v107 2026.07.28 生化模式 AI】集合点账本 + 选点 Subsystem 实现

#include "Systems/Zombie/RoomZombieRallySubsystem.h"

#include "World/Objectives/ZombieRallyPoint.h"
#include "Characters/BaseCharacter.h"
#include "Systems/Mother/RoomMotherMutationSubsystem.h"
#include "Systems/Spawn/RoomSpawnSubsystem.h"
#include "GameFramework/Controller.h"
#include "Engine/World.h"
#include "EngineUtils.h"

// ==========================================
// 标准 Subsystem 钩子
// ==========================================

bool URoomZombieRallySubsystem::ShouldCreateSubsystem(UObject* Outer) const
{
	if (!Super::ShouldCreateSubsystem(Outer))
	{
		return false;
	}
	if (const UWorld* World = Cast<UWorld>(Outer))
	{
		return World->IsGameWorld();
	}
	return false;
}


URoomZombieRallySubsystem* URoomZombieRallySubsystem::Get(const UObject* WorldContextObject)
{
	if (!WorldContextObject)
	{
		return nullptr;
	}
	if (UWorld* World = WorldContextObject->GetWorld())
	{
		return World->GetSubsystem<URoomZombieRallySubsystem>();
	}
	return nullptr;
}


// ==========================================
// 注册 / 注销
// ==========================================

bool URoomZombieRallySubsystem::RegisterRallyPoint(AZombieRallyPoint* Point)
{
	// 【大厂原则 — 零兜底】入参校验全部开启
	if (!IsValid(Point))
	{
		UE_LOG(LogTemp, Error,
			TEXT("[ZombieRally] RegisterRallyPoint: Point=nullptr 或已销毁, 拒绝注册."));
		return false;
	}

	const FString& NewID = Point->PointID;
	if (NewID.IsEmpty())
	{
		UE_LOG(LogTemp, Error,
			TEXT("[ZombieRally] RegisterRallyPoint: Actor '%s' PointID 为空, 拒绝注册. "
			     "【修复】在 UE 编辑器里打开 BP_ZombieRallyPoint 子类实例 → Details → PointID 字段配置唯一非空字符串."),
			*Point->GetName());
		return false;
	}

	if (RallyPointsByID.Contains(NewID))
	{
		// 【零兜底】重复 ID = 策划配错或关卡脚本重复生成, 拒绝
		UE_LOG(LogTemp, Error,
			TEXT("[ZombieRally] RegisterRallyPoint: PointID '%s' 已在账本里 (现有 Actor='%s', 新注册 Actor='%s'). "
			     "【修复】保证关卡内所有集合点 PointID 唯一, 不要复制粘贴."),
			*NewID,
			*RallyPointsByID[NewID].Get()->GetName(),
			*Point->GetName());
		return false;
	}

	if (Point->PopulationRadius <= 0.f)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[ZombieRally] RegisterRallyPoint: Actor '%s' PointID='%s' PopulationRadius=%.1f (必须 > 0). "
			     "【修复】PopulationRadius 是选'人类最多点'的统计半径, 至少 100cm, 推荐 500-1500cm."),
			*Point->GetName(), *NewID, Point->PopulationRadius);
		return false;
	}

	// 加入账本 (TWeakObjectPtr, Actor 销毁后自动失效)
	RallyPointsByID.Add(NewID, Point);

	UE_LOG(LogTemp, Log,
		TEXT("[ZombieRally] RegisterRallyPoint: '%s' (PointID='%s', PopulationRadius=%.0fcm) 注册成功. 当前账本=%d 个."),
		*Point->GetName(), *NewID, Point->PopulationRadius, RallyPointsByID.Num());

	return true;
}


bool URoomZombieRallySubsystem::UnregisterRallyPoint(AZombieRallyPoint* Point)
{
	if (!IsValid(Point))
	{
		// 已经在销毁中, 视为已注销
		return true;
	}

	const FString& PointID = Point->PointID;

	// 【大厂原则 — 账本完整性】拒绝注销被锁定的点
	// 锁定账本里搜这个 PointID, 找到任何 AI 都不允许
	bool bLockedByAnyone = false;
	for (const auto& Pair : LockedRallyByAI)
	{
		if (Pair.Value == PointID)
		{
			bLockedByAnyone = true;
			break;
		}
	}

	if (bLockedByAnyone)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[ZombieRally] UnregisterRallyPoint: Point '%s' (PointID='%s') 仍有 AI 锁定, 拒绝注销. "
			     "【修复】集合点应设计为'整局不被销毁', 不要在游戏过程中 RemoveAllActorsOfClass."),
			*Point->GetName(), *PointID);
		return false;
	}

	const int32 RemovedCount = RallyPointsByID.Remove(PointID);
	if (RemovedCount > 0)
	{
		UE_LOG(LogTemp, Log,
			TEXT("[ZombieRally] UnregisterRallyPoint: '%s' (PointID='%s') 注销成功. 当前账本=%d 个."),
			*Point->GetName(), *PointID, RallyPointsByID.Num());
		return true;
	}

	// 不在账本里 (重复调用 BeginPlay 注册过一次 + 现在 EndPlay)
	// 静默成功 (幂等)
	return true;
}


// ==========================================
// 锁定账本
// ==========================================

bool URoomZombieRallySubsystem::LockRallyPointForAI(AController* Controller, const FString& PointID)
{
	// 【大厂原则 — 零兜底】入参校验
	if (!IsValid(Controller))
	{
		UE_LOG(LogTemp, Error,
			TEXT("[ZombieRally] LockRallyPointForAI: Controller=nullptr, 拒绝锁点."));
		return false;
	}

	if (PointID.IsEmpty())
	{
		UE_LOG(LogTemp, Error,
			TEXT("[ZombieRally] LockRallyPointForAI: Controller='%s' PointID 为空, 拒绝锁点. "
			     "【修复】BTService_ReflexRallyChange 在 Subsystem 选点后传入 PointID, 不能为空."),
			*Controller->GetName());
		return false;
	}

	// 检查 PointID 是否在账本里
	if (!RallyPointsByID.Contains(PointID))
	{
		UE_LOG(LogTemp, Error,
			TEXT("[ZombieRally] LockRallyPointForAI: Controller='%s' PointID='%s' 不在账本里, 拒绝锁点. "
			     "【修复】检查 BP_ZombieRallyPoint 子类是否在地图里放置, 地图加载后 Actor 应已 Register."),
			*Controller->GetName(), *PointID);
		return false;
	}

	// 【v117 2026.08.01 重要】记录"账本登记" — 区别于"业务锁锁定"
	//
	// 业务背景:
	//   原 LockRallyPointForAI 语义是"业务锁" — Controller 锁定后不允许迁移 (返回 false + Log Error)
	//   v117 BTTask_SelectZombieRallyPoint 改为 PeriodicReselect, 每次 ExecuteTask 都会调用本函数
	//   - 同一 Controller 锁同一 PointID → 幂等成功 (return true) ✓
	//   - 同一 Controller 锁不同 PointID → 旧实现 Log Error + reject
	//     新实现: 视为"账本更新" (PeriodicReselect 是合规流程), 覆盖 PointID
	//
	// 大厂原则:
	//   - 幂等 / 覆盖 都是合规 — 都不 Log Error
	//   - 业务上的"一局只锁一次"约束, 由 BTTask_SelectZombieRallyPoint 节点的 ReselectPolicy = LockOnce 保障
	//   - 账本登记语义 = "记录当前选点", 不含"不让改"的强约束
	//   - 哪个点都不能被其他人"同时锁定" (本函数 LockedRallyByAI 值 = 单 PointID)
	//
	// 注: BB.LockedRallyPoint 才是 BT 决策用 — 那些写在 BTService_ReflexRallyChange 写 BB 步骤 3
	//   v122: BTTask_SelectZombieRallyPoint 已删除, bRallyPointLocked bool Key 也已删除
	//   零值原则: 未锁点 = BB.LockedRallyPoint == nullptr; 已锁点 = BB.LockedRallyPoint != nullptr
	const bool bAlreadyLocked = LockedRallyByAI.Contains(Controller);
	LockedRallyByAI.Add(Controller, PointID);

	if (bAlreadyLocked)
	{
		UE_LOG(LogTemp, Verbose,
			TEXT("[ZombieRally] LockRallyPointForAI: Controller='%s' 更新锁定 PointID='%s' (v117 PeriodicReselect 账本同步)."),
			*Controller->GetName(), *PointID);
	}
	else
	{
		UE_LOG(LogTemp, Log,
			TEXT("[ZombieRally] LockRallyPointForAI: Controller='%s' 锁定 PointID='%s' 成功."),
			*Controller->GetName(), *PointID);
	}

	return true;
}


FString URoomZombieRallySubsystem::GetLockedRallyPointID(AController* Controller) const
{
	if (const FString* Existing = LockedRallyByAI.Find(Controller))
	{
		return *Existing;
	}
	return FString();
}


bool URoomZombieRallySubsystem::UnlockRallyPointForAI(AController* Controller)
{
	if (!IsValid(Controller))
	{
		return false;
	}

	const int32 RemovedCount = LockedRallyByAI.Remove(Controller);
	return RemovedCount > 0;
}


// ==========================================
// 人数统计
// ==========================================

int32 URoomZombieRallySubsystem::CountAliveHumanNearPoint(AZombieRallyPoint* Point) const
{
	if (!IsValid(Point))
	{
		return 0;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return 0;
	}

	// 拿人类账本入口 (业务层唯一)
	URoomMotherMutationSubsystem* MotherSys = URoomMotherMutationSubsystem::Get(this);
	if (!MotherSys)
	{
		// 静默返回 0 (不刷错误日志 — MotherSys 缺失是更大的问题, BT Service 会暴露)
		return 0;
	}
	const int32 AliveHumanCount = MotherSys->GetAliveHumanCount();

	// 拿到对局内所有活角色, 逐一过滤"是否在 PopulationRadius 内"
	// 大厂原则: 不 GetAllActorsOfClass (走 URoomSpawnSubsystem 业务账本)
	URoomSpawnSubsystem* SpawnSys = URoomSpawnSubsystem::Get(this);
	if (!SpawnSys)
	{
		return 0;
	}

	const FVector PointLoc = Point->GetActorLocation();
	const float PopRadius = Point->PopulationRadius;

	int32 NearCount = 0;
	const TArray<ABaseCharacter*> AllChars = SpawnSys->GetAllBattleCharacters();
	for (ABaseCharacter* Char : AllChars)
	{
		if (!IsValid(Char))     { continue; }
		if (Char->IsDead())     { continue; }
		if (Char->bIsMother)    { continue; } // 母体不算人类

		// 平面距离 (Z 轴忽略 — 楼层差异不应影响"附近"判定)
		const FVector CharLoc = Char->GetActorLocation();
		const float DistSq2D = FVector::DistSquaredXY(PointLoc, CharLoc);
		const float PopRadiusSq = PopRadius * PopRadius;

		if (DistSq2D <= PopRadiusSq)
		{
			++NearCount;
		}
	}

	(void)AliveHumanCount; // 防止 unused 警告 (当前实现不用, 留扩展用)
	return NearCount;
}


// ==========================================
// 选点策略
// ==========================================

namespace
{
	/**
	 * 平面距离 (Z 轴忽略) — 集合点专用, 与 ABaseAIController::ComputeActorCenterDistance 不同
	 * 为什么不用 UE 自带 Dist:
	 *   - FVector::Distance 包含 Z, 但生化模式集合点选点应只看平面 (楼层差不应换点)
	 */
	FORCEINLINE float ComputeFlatDistanceSq(const FVector& A, const FVector& B)
	{
		const float DX = A.X - B.X;
		const float DY = A.Y - B.Y;
		return DX * DX + DY * DY;
	}
}


AZombieRallyPoint* URoomZombieRallySubsystem::SelectRallyPoint_Nearest(ABaseCharacter* QueryingCharacter) const
{
	if (!IsValid(QueryingCharacter))
	{
		UE_LOG(LogTemp, Error,
			TEXT("[ZombieRally] SelectRallyPoint_Nearest: QueryingCharacter=nullptr, 拒绝选点."));
		return nullptr;
	}

	if (RallyPointsByID.Num() == 0)
	{
		// 账本空 → 调用方负责 Log Error (BTTask 会处理)
		return nullptr;
	}

	const FVector CharLoc = QueryingCharacter->GetActorLocation();

	AZombieRallyPoint* Best = nullptr;
	float BestDistSq = TNumericLimits<float>::Max();
	FString BestID; // 稳定排序用

	for (const auto& Pair : RallyPointsByID)
	{
		AZombieRallyPoint* Point = Pair.Value.Get();
		if (!IsValid(Point))
		{
			// TWeakObjectPtr 失效 (Actor 销毁), 跳过
			continue;
		}

		const float DistSq = ComputeFlatDistanceSq(CharLoc, Point->GetActorLocation());
		const FString& ID = Pair.Key;

		// 【大厂原则 — 稳定排序】并列最近时按 PointID 字典序升序
		if (DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			Best = Point;
			BestID = ID;
		}
		else if (FMath::IsNearlyEqual(DistSq, BestDistSq))
		{
			if (ID < BestID)
			{
				Best = Point;
				BestID = ID;
			}
		}
	}

	return Best;
}


AZombieRallyPoint* URoomZombieRallySubsystem::SelectRallyPoint_MostPopulated(ABaseCharacter* QueryingCharacter) const
{
	if (!IsValid(QueryingCharacter))
	{
		UE_LOG(LogTemp, Error,
			TEXT("[ZombieRally] SelectRallyPoint_MostPopulated: QueryingCharacter=nullptr, 拒绝选点."));
		return nullptr;
	}

	if (RallyPointsByID.Num() == 0)
	{
		return nullptr;
	}

	const FVector CharLoc = QueryingCharacter->GetActorLocation();

	// 收集每个点的 (人数, 距离, PointID) 三元组
	struct FRallyCandidate
	{
		int32 HumanCount = 0;
		float DistSq = 0.f;
		FString ID;
		AZombieRallyPoint* Point = nullptr;
	};

	TArray<FRallyCandidate> Candidates;
	Candidates.Reserve(RallyPointsByID.Num());

	for (const auto& Pair : RallyPointsByID)
	{
		AZombieRallyPoint* Point = Pair.Value.Get();
		if (!IsValid(Point))
		{
			continue;
		}

		FRallyCandidate C;
		C.HumanCount = CountAliveHumanNearPoint(Point);
		C.DistSq = ComputeFlatDistanceSq(CharLoc, Point->GetActorLocation());
		C.ID = Pair.Key;
		C.Point = Point;
		Candidates.Add(C);
	}

	if (Candidates.Num() == 0)
	{
		return nullptr;
	}

	// 排序: 人数 desc → 距离 asc → ID asc (稳定)
	Candidates.Sort([](const FRallyCandidate& A, const FRallyCandidate& B)
	{
		if (A.HumanCount != B.HumanCount)
		{
			return A.HumanCount > B.HumanCount;
		}
		if (!FMath::IsNearlyEqual(A.DistSq, B.DistSq))
		{
			return A.DistSq < B.DistSq;
		}
		return A.ID < B.ID;
	});

	return Candidates[0].Point;
}


// 【v120 2026.08.01】反射式换点专用 — 与 SelectRallyPoint_Nearest 镜像, 排除指定 PointID
//
// 实现要点 (大厂原则 - 零重复 + 复用现有算法):
//   - 距离算法: 复用 SelectRallyPoint_Nearest 同款 ComputeFlatDistanceSq (匿名 namespace 内)
//   - 稳定排序: 复用 PointID 字典序破局
//   - 排除逻辑: 跳过 ExcludePointID 匹配的项, 选剩余中最近的
//   - 边界: 账本空 / 排除后空 → return nullptr (调用方负责 Log Error)
//
// 与 SelectRallyPoint_Nearest 唯一区别: 循环里加 `if (ID == ExcludePointID) continue;`
// 为什么不直接复用 SelectRallyPoint_Nearest + 二次过滤:
//   - "选最近后再排除" 如果选出的就是 ExcludePointID, 第二次选谁? 还得重新排
//   - 直接在循环里跳过 = 单次排序, 复杂度 O(N), 0 重复计算
AZombieRallyPoint* URoomZombieRallySubsystem::SelectRallyPoint_Nearest_Excluding(
	ABaseCharacter* QueryingCharacter, const FString& ExcludePointID) const
{
	if (!IsValid(QueryingCharacter))
	{
		UE_LOG(LogTemp, Error,
			TEXT("[ZombieRally] SelectRallyPoint_Nearest_Excluding: QueryingCharacter=nullptr, 拒绝选点."));
		return nullptr;
	}

	if (RallyPointsByID.Num() == 0)
	{
		// 账本空 → 调用方负责 Log Error
		return nullptr;
	}

	const FVector CharLoc = QueryingCharacter->GetActorLocation();

	AZombieRallyPoint* Best = nullptr;
	float BestDistSq = TNumericLimits<float>::Max();
	FString BestID;

	for (const auto& Pair : RallyPointsByID)
	{
		AZombieRallyPoint* Point = Pair.Value.Get();
		if (!IsValid(Point))
		{
			// TWeakObjectPtr 失效 (Actor 销毁), 跳过
			continue;
		}

		const FString& ID = Pair.Key;

		// ★ 反射式换点专用: 排除当前锁点本身
		// ExcludePointID 可能是空字符串 (调用方未锁点) — 不会匹配任何 PointID (假设策划 PointID 全部非空)
		// 业务上 PointID 必非空 (RegisterRallyPoint 零兜底校验), 所以空 ExcludePointID = 不排除任何点
		if (!ExcludePointID.IsEmpty() && ID == ExcludePointID)
		{
			continue;
		}

		const float DistSq = ComputeFlatDistanceSq(CharLoc, Point->GetActorLocation());

		// 稳定排序: 并列最近时按 PointID 字典序升序
		if (DistSq < BestDistSq)
		{
			BestDistSq = DistSq;
			Best = Point;
			BestID = ID;
		}
		else if (FMath::IsNearlyEqual(DistSq, BestDistSq))
		{
			if (ID < BestID)
			{
				Best = Point;
				BestID = ID;
			}
		}
	}

	return Best;
}


// ==========================================
// 查询
// ==========================================

TArray<AZombieRallyPoint*> URoomZombieRallySubsystem::GetAllRallyPoints() const
{
	TArray<AZombieRallyPoint*> Out;
	Out.Reserve(RallyPointsByID.Num());

	for (const auto& Pair : RallyPointsByID)
	{
		if (AZombieRallyPoint* P = Pair.Value.Get())
		{
			Out.Add(P);
		}
	}

	return Out;
}


bool URoomZombieRallySubsystem::IsRallyPointRegistered(const FString& PointID) const
{
	if (PointID.IsEmpty())
	{
		return false;
	}
	return RallyPointsByID.Contains(PointID);
}


AZombieRallyPoint* URoomZombieRallySubsystem_GetRallyPointByID_Impl(const TMap<FString, TWeakObjectPtr<AZombieRallyPoint>>& Map, const FString& PointID)
{
	if (PointID.IsEmpty())
	{
		return nullptr;
	}
	const TWeakObjectPtr<AZombieRallyPoint>* Found = Map.Find(PointID);
	if (!Found)
	{
		return nullptr;
	}
	return Found->Get();
}

AZombieRallyPoint* URoomZombieRallySubsystem::GetRallyPointByID(const FString& PointID) const
{
	return URoomZombieRallySubsystem_GetRallyPointByID_Impl(RallyPointsByID, PointID);
}


// ============================================================
// 【v125 2026.08.01】visited set + 冷却窗口 — 反射式换点 v125 升级版
// ============================================================

// 【v125】记录 AI 访问集合点 (账本写入入口)
//
// 调用方:
//   - BTService_ReflexRallyChange::PerformReflexChange 写 BB 成功后立即调
//   - 第一次"出生即选"也算 (写 BB 后调本函数)
//
// 大厂原则:
//   - 幂等: 重复写入同一 PointID → 更新时间戳 (用于冷却窗口判定)
//   - 失效保护: Controller 销毁 → TWeakObjectPtr 自动失效 → 不会有悬挂
//   - 静默退出: Controller 无效 / PointID 空 → return (不 Log, 由调用方决定)
void URoomZombieRallySubsystem::RecordRallyVisit(AController* Controller, const FString& PointID)
{
	if (!IsValid(Controller))
	{
		// Controller 已失效 (AI 死亡/销毁触发) — 静默退出, 不刷错误日志
		return;
	}

	if (PointID.IsEmpty())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[ZombieRally] RecordRallyVisit: Controller='%s' PointID 为空, 跳过记录. "
			     "【修复】BTService_ReflexRallyChange::PerformReflexChange 写 BB 成功后传有效 PointID."),
			*Controller->GetName());
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const float Now = World->GetTimeSeconds();
	const TWeakObjectPtr<AController> WeakCtrl(Controller);

	// FindOrAdd 模式 — 第一次访问时新建, 后续访问时复用
	FRallyPointVisitRecord& Record = VisitHistoryByController.FindOrAdd(WeakCtrl);
	Record.LastVisitTimeByPoint.FindOrAdd(PointID) = Now;

	UE_LOG(LogTemp, Verbose,
		TEXT("[ZombieRally] RecordRallyVisit: Controller='%s' 访问 PointID='%s' 时间戳=%.2f. "
		     "(历史 PointID 数=%d, 用于反射式换点冷却 / visited set 偏好)"),
		*Controller->GetName(), *PointID, Now, Record.LastVisitTimeByPoint.Num());
}


// 【v125】反射式换点 v125 升级版 — 冷却窗口 + visited set
//
// 核心算法 (2 阶段):
//   阶段 1: 冷却窗口检查 — ExcludePointID 在 VisitHistory 中距离上次锁定 < MinLockDurationBeforeReflexChange → 拒绝换点
//   阶段 2: 选点 — 遍历账本, 跳过 ExcludePointID, 给已访问过的点加权 (bias)
//
// 为什么这样设计:
//   - 旧 (v124) 只有"排除当前点"逻辑 → BT Service 0.2s Tick 连续触发 → 0.2s 换一次 → 来回切换 A↔B
//   - v125 加冷却窗口 + visited set 加权 → 刚锁 B 后 8s 内不能"换走 B" → 不会 A↔B 来回
//
// 同分处理 (大厂原则 - 稳定排序):
//   - Score = DistSq × Bias² (平方后 bias 1.0 还是 4.0 区别更明显)
//   - AgeFactor = TimeSince / 30s (Clamp 0..1, 0=刚访问, 1=30s 前访问)
//   - Bias = Lerp(VisitBiasMultiplier, 1.0, AgeFactor) — 越久远 bias 越接近 1.0
//   - 并列 Score → PointID 字典序升序
AZombieRallyPoint* URoomZombieRallySubsystem::SelectRallyPoint_Nearest_ReflexChange(
	ABaseCharacter* QueryingCharacter,
	AController* Controller,
	const FString& ExcludePointID,
	float VisitBiasMultiplier) const
{
	if (!IsValid(QueryingCharacter))
	{
		UE_LOG(LogTemp, Error,
			TEXT("[ZombieRally] SelectRallyPoint_Nearest_ReflexChange: QueryingCharacter=nullptr, 拒绝选点."));
		return nullptr;
	}

	// 参数验证: VisitBiasMultiplier >= 1.0 (1.0 = 无惩罚, < 1.0 = 反向奖励, 不合理)
	if (VisitBiasMultiplier < 1.f)
	{
		// 防御: 错误的参数, 用默认 4.0
		VisitBiasMultiplier = DefaultVisitBiasMultiplier;
	}

	UWorld* World = GetWorld();
	const float Now = World ? World->GetTimeSeconds() : 0.f;

	// 阶段 1: 冷却窗口检查
	//
	// 大厂原则: 物理正确性 — 时间只能前进, 冷却期内不允许"再换走"
	// 业务原因: 防止 A↔B 来回切换 (Service 0.2s Tick, 条件一直满足 → 0.2s 换一次)
	const TWeakObjectPtr<AController> WeakCtrl(Controller);
	if (Controller && IsValid(Controller) && !ExcludePointID.IsEmpty())
	{
		if (const FRallyPointVisitRecord* Record = VisitHistoryByController.Find(WeakCtrl))
		{
			if (const float* LastTime = Record->LastVisitTimeByPoint.Find(ExcludePointID))
			{
				const float Elapsed = Now - *LastTime;
				if (Elapsed < MinLockDurationBeforeReflexChange)
				{
					UE_LOG(LogTemp, Verbose,
						TEXT("[ZombieRally] SelectRallyPoint_Nearest_ReflexChange: "
						     "Controller='%s' 反射式换点冷却中 (ExcludePointID='%s' 仅过去 %.1fs, 需 %.1fs). 拒绝换点, 返回 nullptr."),
						*Controller->GetName(), *ExcludePointID, Elapsed, MinLockDurationBeforeReflexChange);
					return nullptr;
				}
			}
		}
	}

	// 阶段 2: 选点 (visited set 加权)
	if (RallyPointsByID.Num() == 0)
	{
		// 账本空 → 调用方负责 Log Error
		return nullptr;
	}

	// 拿 AI 的访问历史 (visited set 镜像)
	// 历史空 → 走纯距离 (等价于 v124 行为, 不报错)
	const FRallyPointVisitRecord* History = Controller && IsValid(Controller)
		? VisitHistoryByController.Find(WeakCtrl)
		: nullptr;

	const FVector CharLoc = QueryingCharacter->GetActorLocation();

	AZombieRallyPoint* Best = nullptr;
	float BestScore = TNumericLimits<float>::Max();
	FString BestID; // 稳定排序用

	for (const auto& Pair : RallyPointsByID)
	{
		AZombieRallyPoint* Point = Pair.Value.Get();
		if (!IsValid(Point))
		{
			// TWeakObjectPtr 失效 (Actor 销毁), 跳过
			continue;
		}

		const FString& ID = Pair.Key;

		// 排除当前 BB 锁点本身 (v124 继承)
		if (!ExcludePointID.IsEmpty() && ID == ExcludePointID)
		{
			continue;
		}

		const float DistSq = ComputeFlatDistanceSq(CharLoc, Point->GetActorLocation());

		// 【v125】visited set 加权
		// 越久远访问过的点 Bias 越接近 1.0 (不惩罚)
		// 越近访问过的点 Bias = VisitBiasMultiplier (大幅惩罚)
		float Bias = 1.0f;
		if (History)
		{
			if (const float* LastTime = History->LastVisitTimeByPoint.Find(ID))
			{
				const float TimeSince = Now - *LastTime;
				// 30s 内访问过 → bias 拉满; 30s 前访问过 → bias 接近 1.0
				const float AgeFactor = FMath::Clamp(TimeSince / 30.0f, 0.0f, 1.0f);
				Bias = FMath::Lerp(VisitBiasMultiplier, 1.0f, AgeFactor);
			}
		}
		// Score = DistSq × Bias² (平方后 bias 差更明显)
		const float Score = DistSq * Bias * Bias;

		// 稳定排序: 并列 Score → PointID 字典序升序
		if (Score < BestScore)
		{
			BestScore = Score;
			Best = Point;
			BestID = ID;
		}
		else if (FMath::IsNearlyEqual(Score, BestScore))
		{
			if (ID < BestID)
			{
				Best = Point;
				BestID = ID;
			}
		}
	}

	if (Best)
	{
		UE_LOG(LogTemp, Verbose,
			TEXT("[ZombieRally] SelectRallyPoint_Nearest_ReflexChange: "
			     "Controller='%s' 选点='%s' (Score=%.0f, 排除='%s', Bias=%.2f). "
			     "v124 → v125 升级: 已访问过的点优先度降低, 近期换过点冷却期内拒绝再换."),
			*QueryingCharacter->GetName(), *BestID, BestScore, *ExcludePointID, VisitBiasMultiplier);
	}

	return Best;
}