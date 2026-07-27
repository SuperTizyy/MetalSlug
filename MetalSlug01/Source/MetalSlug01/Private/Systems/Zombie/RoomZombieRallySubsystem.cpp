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
			     "【修复】BTTask_SelectZombieRallyPoint 在 Subsystem 选点后写入 BB.LockedRallyPoint, 不能为空."),
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

	// 【大厂原则 — 一局只锁一次】检查是否已经锁定其他点
	if (const FString* ExistingLock = LockedRallyByAI.Find(Controller))
	{
		if (*ExistingLock != PointID)
		{
			UE_LOG(LogTemp, Error,
				TEXT("[ZombieRally] LockRallyPointForAI: Controller='%s' 已锁 PointID='%s', 拒绝覆盖锁定新点 '%s'. "
				     "【v107 大厂原则】一局内只选一次, 已锁点 AI 不迁移. "
				     "【修复】BT Decorator 必须用 bRallyPointLocked 拒判, 不允许绕过."),
				*Controller->GetName(), **ExistingLock, *PointID);
			return false;
		}

		// 同一 Controller 锁同一 PointID → 幂等成功
		return true;
	}

	LockedRallyByAI.Add(Controller, PointID);

	UE_LOG(LogTemp, Log,
		TEXT("[ZombieRally] LockRallyPointForAI: Controller='%s' 锁定 PointID='%s' 成功."),
		*Controller->GetName(), *PointID);

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