// ===========================================
// URangedLineStrategy 实现 (大厂架构 v60.3 + v60.16)
// 相机起点 + 枪口偏移 (所见即所射)
//
// 【v60.16 射线起点公式重构】
//   - 射线起点 = 相机位置 + 相机方向 × (TAL - MuzzleOffset)
//   - 射线方向 = 准星屏幕坐标 → DeprojectScreenPositionToWorld
//   - 效果: 子弹从枪口位置射出，方向对准星，所见即所射
//
// 【核心算法 v60.16】
//   - 起点: 相机位置 + CameraForward × (TAL - MuzzleOffset)
//           (第三人称: 角色 → 枪口 → 相机)

//   - 方向: 准星屏幕坐标 → 世界射线 (Deproject)
//   - 终点: 起点 + 方向 × AttackRange
//
// 【大厂标准对照】
//   CS:GO / Apex / PUBG / Fortnite: 子弹从枪口射出，方向对准星
//   你的 v60.16: 完全一致
//
// 【v60.3 关键修复】
//   - StartTrace 立即执行一发射线，无 1 帧延迟
//
// 【零兜底】
//   - TargetArmLength 获取失败 → Log Warning (用默认值 300cm)
//   - Deproject 失败 → Log Error
// ===========================================

#include "Weapons/RangedLineStrategy.h"

#include "Weapons/BaseWeapon.h"
#include "Components/MeshComponent.h"
#include "Characters/BaseCharacter.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Actor.h"
#include "GameFramework/PlayerController.h"
#include "GameFramework/SpringArmComponent.h"
#include "Camera/PlayerCameraManager.h"
#include "Camera/CameraComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Kismet/KismetSystemLibrary.h"
#include "Engine/EngineTypes.h"
#include "Engine/HitResult.h"
#include "EngineUtils.h"              // 【v97.0】TActorIterator
#include "Engine/World.h"             // 【v97.0】UWorld
#include "GameplayTagContainer.h"     // 【v97.0】FGameplayTag


URangedLineStrategy::URangedLineStrategy()
{
	bIsCurrentHeavy = false;
	TraceState = EWeaponTraceState::Idle;
}


// ===========================================
// 1. StartTrace — v60.3 立即执行一发射线
// ===========================================

bool URangedLineStrategy::StartTrace(ABaseWeapon* Weapon, bool bIsHeavy,
	const FVector& ClientRayOrigin,
	const FVector& ClientRayDirection)
{
	if (!Weapon)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[URangedLineStrategy::StartTrace] Weapon 为空 — 拒绝启动检测."));
		return false;
	}

	// v60.3 大厂重构: 缓存状态 + 立即执行一次射线
	ActiveWeapon = Weapon;
	bIsCurrentHeavy = bIsHeavy;

	UE_LOG(LogTemp, Log,
		TEXT("[URangedLineStrategy::StartTrace] 立即执行枪械射线 — Weapon=%s Type=%d Heavy=%d ClientRayOrigin=%s Dir=%s"),
		*Weapon->GetName(),
		static_cast<int32>(Weapon->GetMeshType()),
		bIsHeavy ? 1 : 0,
		*ClientRayOrigin.ToCompactString(),
		*ClientRayDirection.ToCompactString());

	// 立即打一发 (不再延后)
	const bool bExecuted = PerformSingleShot(Weapon, ClientRayOrigin, ClientRayDirection);
	return bExecuted;
}


// ===========================================
// 2. StopTrace — v60.3 对 Ranged 是 no-op
// ===========================================

void URangedLineStrategy::StopTrace(ABaseWeapon* Weapon)
{
	// v60.3 大厂原则: Ranged 无激活态，StopTrace 仅清理 TWeakObjectPtr
	ActiveWeapon.Reset();
	bIsCurrentHeavy = false;
	// v74 — Ranged 立即 Idle (StartTrace 单帧 Tracing 不留持久状态)
	TraceState = EWeaponTraceState::Idle;

	UE_LOG(LogTemp, Verbose,
		TEXT("[URangedLineStrategy::StopTrace] 清理 Ranged 状态 — Weapon=%s (Ranged 无激活态)"),
		Weapon ? *Weapon->GetName() : TEXT("<null>"));
}


// ===========================================
// 3. TickDetection — v60.3 对 Ranged 永远 no-op
// ===========================================

void URangedLineStrategy::TickDetection(ABaseWeapon* Weapon, float DeltaTime)
{
	// v60.3 大厂原则: 枪械节奏由 UWeaponFireComponent 控
}


// ===========================================
// 4. PerformSingleShot — 单次射线 (v60.16 相机起点 + 枪口偏移)
// 子弹从枪口射出，方向对准星 (所见即所射)
// ===========================================

bool URangedLineStrategy::PerformSingleShot(ABaseWeapon* Weapon,
	const FVector& ClientRayOrigin,
	const FVector& ClientRayDirection)
{
	if (!Weapon)
	{
		return false;
	}

	// ===========================================
	// 步骤 1: 获取 Owner (攻击者)
	// ===========================================
	AActor* WeaponOwner = Weapon->GetOwner();
	if (!WeaponOwner)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[URangedLineStrategy::PerformSingleShot] Weapon=%s 没有 Owner — 拒绝射线."),
			*Weapon->GetName());
		return false;
	}

	// ===========================================
	// 步骤 2: 获取 PlayerController (仅本地玩家)
	// ===========================================
	APlayerController* PC = Cast<APlayerController>(WeaponOwner->GetInstigatorController());
	if (!PC)
	{
		// AI 路径: 不走相机射线，由 BaseCharacter::GetAimRayFromCrosshairOrEyes 的 AI 路径处理
		UE_LOG(LogTemp, Error,
			TEXT("[URangedLineStrategy::PerformSingleShot] Weapon=%s Owner 不是 PlayerController — 拒绝射线. (AI 路径应由 BaseCharacter::GetAimRay 处理)"),
			*Weapon->GetName());
		return false;
	}

	// ===========================================
	// 步骤 3 (v82 大厂架构修复): 决定射线来源和方向
	//
	// 【v85.4 大厂架构修复】玩家路径正确计算射线起点
	//
	// 根因 (从 Session1.log 定位):
	//   用户报告: "主武器射线检测没有算 TargetArmLength，之前好的，被改坏了"
	//
	// 问题分析:
	//   客户端用 DeprojectScreenPositionToWorld 从屏幕中心算出射线起点
	//   - Deproject 返回的 WorldOrigin = 相机位置 = Pawn位置 + CameraBoom偏移
	//   - 这个偏移已经包含了 TargetArmLength
	//   - 所以服务器不应该再添加 TargetArmLength
	//
	// 修复方案:
	//   玩家路径: 直接用客户端传来的射线起点（所见即所射）
	//   AI 路径: 服务器用自己的相机数据 (需要加 TargetArmLength)
	// ===========================================
	FVector RayDirection;
	FVector RayOrigin;

	if (!ClientRayOrigin.IsNearlyZero())
	{
		// 玩家路径: 射线起点 = 相机位置 + 相机前方 × (枪长度 + 弹簧臂长度)
		//   - ClientRayOrigin = 相机位置 (来自 HUD Deproject)
		//   - ClientRayDirection = 相机前方 (来自 HUD Deproject)
		// ============================================================
		// 步骤 3a: 获取相机位置和弹簧臂长度
		// ============================================================
		const FVector CameraLocation = ClientRayOrigin;
		const FVector CameraForward = ClientRayDirection.GetSafeNormal();

		if (CameraForward.IsNearlyZero())
		{
			UE_LOG(LogTemp, Error,
				TEXT("[URangedLineStrategy::PerformSingleShot] Weapon=%s 相机前方为零 — 拒绝射线."),
				*Weapon->GetName());
			return false;
		}

		// ============================================================
		// 步骤 3b: 获取弹簧臂长度 (TargetArmLength) — 【v85.x 零兜底】必须获取真实值
		// ============================================================
		ACharacter* CharacterOwner = Cast<ACharacter>(WeaponOwner);
		if (!CharacterOwner)
		{
			UE_LOG(LogTemp, Error,
				TEXT("[URangedLineStrategy::PerformSingleShot] Weapon=%s Owner 不是 ACharacter — 拒绝射线."),
				*Weapon->GetName());
			return false;
		}

		USpringArmComponent* CameraBoom = Cast<USpringArmComponent>(
			CharacterOwner->GetComponentByClass(USpringArmComponent::StaticClass()));
		if (!CameraBoom)
		{
			UE_LOG(LogTemp, Error,
				TEXT("[URangedLineStrategy::PerformSingleShot] Weapon=%s 找不到 CameraBoom (SpringArmComponent) — 拒绝射线. 修复: 检查角色 BP 的 CameraBoom 是否正确挂载."),
				*Weapon->GetName());
			return false;
		}

		const float TargetArmLength = CameraBoom->TargetArmLength;

		// ============================================================
		// 步骤 3c: 计算射线起点 = 相机位置 + 相机前方 × (枪长度 + 弹簧臂长度)
		// ============================================================
		const float MuzzleOffset = Weapon->MuzzleOffset;
		const float TotalOffset = TargetArmLength + MuzzleOffset;
		RayOrigin = CameraLocation + CameraForward * TotalOffset;
		RayDirection = CameraForward;

		UE_LOG(LogTemp, Log,
			TEXT("[URangedLineStrategy::PerformSingleShot] 玩家路径 — Weapon=%s CameraLoc=%s TAL=%.1f MuzzleOffset=%.1f TotalOffset=%.1f RayOrigin=%s RayDir=%s"),
			*Weapon->GetName(),
			*CameraLocation.ToCompactString(),
			TargetArmLength,
			MuzzleOffset,
			TotalOffset,
			*RayOrigin.ToCompactString(),
			*RayDirection.ToCompactString());
	}
	else
	{
		// AI 路径 / 兼容旧调用: 服务器侧自己算射线
		//   - 如果是 AI 控制的 Pawn: 用 BaseAimRotation (BT 控制的旋转)
		//   - 如果是 ListenServer 本地玩家: 用本地 PC->Deproject
		// ===========================================
		// 步骤 3a: 获取相机位置和方向 (v60.16 核心)
		// ===========================================
		APlayerCameraManager* CameraMgr = PC->PlayerCameraManager;
		if (!CameraMgr)
		{
			UE_LOG(LogTemp, Error,
				TEXT("[URangedLineStrategy::PerformSingleShot] Weapon=%s PlayerCameraManager 为空 — 拒绝射线."),
				*Weapon->GetName());
			return false;
		}

		const FVector CameraLocation = CameraMgr->GetCameraLocation();

		// 获取相机旋转，计算相机朝向方向
		const FRotator CameraRotation = CameraMgr->GetCameraRotation();
		const FVector CameraForward = CameraRotation.Vector(); // 单位向量，指向相机朝向

		if (CameraForward.IsNearlyZero())
		{
			UE_LOG(LogTemp, Error,
				TEXT("[URangedLineStrategy::PerformSingleShot] Weapon=%s 相机朝向为零 — 拒绝射线."),
				*Weapon->GetName());
			return false;
		}

		// ===========================================
		// 步骤 3b: 获取 TargetArmLength (相机到角色中心的距离)
		// ===========================================
		ACharacter* CharacterOwner = Cast<ACharacter>(WeaponOwner);
		if (!CharacterOwner)
		{
			UE_LOG(LogTemp, Error,
				TEXT("[URangedLineStrategy::PerformSingleShot] Weapon=%s Owner 不是 ACharacter — 拒绝射线."),
				*Weapon->GetName());
			return false;
		}

		// 获取角色的 CameraBoom (SpringArmComponent)
		USpringArmComponent* CameraBoom = Cast<USpringArmComponent>(
			CharacterOwner->GetComponentByClass(USpringArmComponent::StaticClass()));

		float TargetArmLength = 300.0f; // 默认值
		if (CameraBoom)
		{
			TargetArmLength = CameraBoom->TargetArmLength;
		}
		else
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[URangedLineStrategy::PerformSingleShot] Weapon=%s 找不到 CameraBoom，用默认值 TAL=300cm."),
				*Weapon->GetName());
		}

		// ===========================================
		// 步骤 3c: 计算枪口偏移后的射线起点 (v60.16 核心公式)
		// ===========================================
		const float MuzzleOffset = Weapon->MuzzleOffset;
		const float TotalOffset = TargetArmLength + MuzzleOffset;
		RayOrigin = CameraLocation + CameraForward * TotalOffset;

		// ===========================================
		// 步骤 3d: 获取射击方向 (准星屏幕坐标 → 世界射线)
		// ===========================================
		int32 ViewportSizeX, ViewportSizeY;
		PC->GetViewportSize(ViewportSizeX, ViewportSizeY);

		const FVector2D CrosshairScreenPos = FVector2D(
			ViewportSizeX * 0.5f,
			ViewportSizeY * 0.5f
		);

		FVector WorldOrigin;
		WorldOrigin = FVector::ZeroVector;
		FVector WorldDirection;
		const bool bDeprojectOK = PC->DeprojectScreenPositionToWorld(
			CrosshairScreenPos.X,
			CrosshairScreenPos.Y,
			WorldOrigin,
			WorldDirection
		);

		if (!bDeprojectOK || WorldDirection.IsNearlyZero())
		{
			UE_LOG(LogTemp, Error,
				TEXT("[URangedLineStrategy::PerformSingleShot] Weapon=%s DeprojectScreenPositionToWorld 失败 — 拒绝射线."),
				*Weapon->GetName());
			return false;
		}

		RayDirection = WorldDirection;
	}

	// ===========================================
	// 步骤 4: 计算射线终点 (玩家/AI 路径统一)
	// ===========================================
	const float Range = Weapon->AttackRange;
	const FVector EndLoc = RayOrigin + RayDirection * Range;

	// ===========================================
	// 步骤 5: 命中过滤 (自己 + Owner)
	// ===========================================
	TArray<AActor*> IgnoreActors;
	IgnoreActors.Add(Weapon);
	if (AActor* Owner = Weapon->GetOwner())
	{
		IgnoreActors.Add(Owner);
	}

	// ===========================================
	// 步骤 6: 【v97.0.2 大厂架构 P0 修复】两阶段检测 — LineTraceMulti + Pawn 路径搜索
	// ===========================================
	//
	// 【v97.0.2 修复动机 - 大厂原则】
	//   旧版 (v60.3-v97.0.1) 反模式: LineTraceSingle/LineTraceMulti 只用单一线性射线
	//   - 问题 (Session1.log line 797-1268, 18.27.x): 所有 LineTrace 都只命中 Landscape_0 / SM_building_wall_109
	//           即使母体 (BP_MuTi_C_0) 在 trace 路径上 (300-500cm 处), trace 仍然不命中
	//   - 真根因: BP_MuTi 蓝图 CapsuleComponent 对 ECC_Visibility trace channel 是 Ignore / NoCollision
	//           - 这导致 LineTrace "穿过" 母体 Pawn → 命中远处 Landscape_0
	//   - 后果: 人类武器 trace 永远不命中母体 → Server_ReportHit 不调用 → 母体不扣血
	//
	// 【v97.0.2 大厂架构】两阶段命中检测 (无 BP 依赖, 拒绝粗检测):
	//   阶段 1: LineTraceMulti — 沿准星方向的精确射线 (CS:GO/Apex/PUBG 标准 hit-scan)
	//           - 大厂原则: 射线是射线, 不能加粗, 不能改 SphereTrace
	//           - CS:GO / Apex / PUBG / Fortnite 子弹都是 hit-scan 精确射线, 不容忍粗检测
	//   阶段 2: Pawn 路径搜索 — 仅在阶段 1 完全没命中 Pawn 时启用
	//           - 沿 trace 路径 30cm 容差搜索 ABaseCharacter (基于距离投影 + 横向距离)
	//           - 解决 BP_MuTi collision 完全关闭的情况 (基于世界坐标, 不依赖 BP 蓝图 collision)
	//           - 大厂原则: 容差严格 < Pawn Capsule 半径, 不会误命中旁边的 Pawn
	//
	// 【v97.0.2 重大设计修正 - 拒绝 SphereTrace】
	//   v97.0.1 我加了 SphereTraceMulti (半径 50cm) 作为 Phase 2 兜底
	//   这是**错误设计** - 用户反馈"把射线搞得太粗了"
	//   - 反模式: 把 hit-scan 改成胶囊检测, 玩家点哪打哪的承诺被破坏
	//   - v97.0.2 删除 SphereTrace, 改用 Pawn 路径搜索 (世界坐标, 不依赖 collision)
	//
	// 【零兜底保证】
	//   - 不允许"撞上地形就完事" (反模式 - 隐藏 Pawn 命中)
	//   - 不允许"把射线变粗" (反模式 - CS:GO/Apex/PUBG 都是 hit-scan 精确射线)
	//   - Phase 2 必须在 Phase 1 完全失败时才启用 (不会无脑开搜)
	//   - Phase 2 必须有阵营守卫 (只对敌人搜索, 避免误伤队友)
	//
	// 【服务器权威】HasAuthority=true 时才调 (v95.1 大厂原则: 服务器权威伤害)
	// ===========================================
	TArray<FHitResult> HitResults;
	const bool bHit = UKismetSystemLibrary::LineTraceMulti(
		Weapon,
		RayOrigin,     // 起点: 玩家=客户端射线起点 / AI=相机+枪口偏移
		EndLoc,        // 终点: 起点 + 方向 * 射程
		UEngineTypes::ConvertToTraceType(ECC_Visibility),
		false,
		IgnoreActors,
		EDrawDebugTrace::ForDuration,
		HitResults,
		true,
		FLinearColor::Red,       // TraceColor: 未命中
		FLinearColor::Green,     // TraceHitColor: 命中
		IWeaponDamageStrategy::kDebugTraceLifeTimeSeconds  // 5分钟持久显示
	);

	// ===========================================
	// 步骤 6.1: 【v97.0.2】判断 LineTraceMulti 是否找到了 Pawn
	// ===========================================
	bool bHasPawnInTrace = false;
	if (bHit && HitResults.Num() > 0)
	{
		for (const FHitResult& HR : HitResults)
		{
			if (Cast<ABaseCharacter>(HR.GetActor()))
			{
				bHasPawnInTrace = true;
				break;
			}
		}
	}

	// ===========================================
	// 步骤 6.2: 【v97.0.2 P0 修复】Phase 2 — Pawn 路径搜索 (世界坐标, 不依赖 BP collision)
	// ===========================================
	//
	// 【大厂原则 - 拒绝 SphereTrace】
	//   v97.0.1 我加了 SphereTraceMulti (半径 50cm) 作为兜底 — 这是错误设计
	//   v97.0.2 删除 SphereTrace, 改用 Pawn 路径搜索 (TActorIterator)
	//   - 路径搜索不依赖 collision, 严格基于 Pawn 世界坐标 + trace 路径几何关系
	//   - 容差 30cm (严格 < Pawn Capsule 半径 42cm), 不会误命中旁边的 Pawn
	//
	// 【启用条件】
	//   仅当 Phase 1 (LineTraceMulti) 完全没命中 Pawn 时启用
	//   这是 BP_MuTi Capsule collision 完全配错的应急方案, 不是常规路径
	//
	// 【同阵营守卫】
	//   只对**敌人**做路径搜索, 跳过队友 (避免误伤)
	ABaseCharacter* FallbackPawn = nullptr;
	float FallbackPawnPathDistance = TNumericLimits<float>::Max();

	if (!bHasPawnInTrace)
	{
		UWorld* World = Weapon->GetWorld();
		if (World)
		{
			// 攻击者阵营 — 只对敌人做 fallback
			const ABaseCharacter* AttackerChar = Cast<ABaseCharacter>(Weapon->GetOwner());
			const FGameplayTag AttackerFaction = AttackerChar ? AttackerChar->GetFactionTag() : FGameplayTag();

			const FVector TraceDir = (EndLoc - RayOrigin).GetSafeNormal();
			const float TraceLength = (EndLoc - RayOrigin).Size();

			for (TActorIterator<ABaseCharacter> It(World); It; ++It)
			{
				ABaseCharacter* Candidate = *It;
				if (!Candidate || Candidate == AttackerChar)
				{
					continue; // 跳过自己
				}

				// 同阵营守卫 — 队友不参与 fallback
				if (!AttackerFaction.IsValid() ||
					Candidate->GetFactionTag() == AttackerFaction)
				{
					continue;
				}

				const FVector PawnLoc = Candidate->GetActorLocation();
				const FVector PawnToRayOrigin = PawnLoc - RayOrigin;

				// 沿 trace 方向的投影距离 (0 = 在起点, TraceLength = 在终点)
				const float ProjectedDist = FVector::DotProduct(PawnToRayOrigin, TraceDir);

				// Pawn 不在 trace 路径范围内 → 跳过 (前后 50cm 容差, 防止起/终点边界误判)
				if (ProjectedDist < -50.0f || ProjectedDist > TraceLength + 50.0f)
				{
					continue;
				}

				// Pawn 到 trace 路径的最近点 + 横向距离
				const FVector ClosestPointOnPath = RayOrigin + TraceDir * ProjectedDist;
				const float LateralDistance = FVector::Distance(PawnLoc, ClosestPointOnPath);

				// 横向距离 ≤ 30cm (严格 < Pawn Capsule 半径) → 视为命中
				// 30cm = 接近人体 Capsule 半径 (UE 默认 42cm), 但严格小于, 不会误命中旁边 Pawn
				if (LateralDistance <= 30.0f && ProjectedDist < FallbackPawnPathDistance)
				{
					FallbackPawnPathDistance = ProjectedDist;
					FallbackPawn = Candidate;
				}
			}
		}
	}

	// ===========================================
	// 步骤 6.5: 【v97.0.2】从 HitResults 中选最近的 Pawn 命中
	//   - 业务核心目标: Pawn (人类玩家/AI Pawn) — 要扣血
	//   - 兜底: 没 Pawn 命中, 用最近地形 (旧 LineTraceSingle 行为)
	//   - 终极兜底: 阶段 2 找到的 Pawn (大厂数据驱动兜底, 不依赖 BP 蓝图配置)
	// ===========================================
	FHitResult HitResult;
	AActor* SelectedTargetActor = nullptr;
	bool bSelectedIsPawn = false;
	bool bUsedFallbackPawnSearch = false;

	if (FallbackPawn)
	{
		// 【v97.0.2】Phase 2 找到的 Pawn 是最权威的兜底 (基于真实世界坐标)
		SelectedTargetActor = FallbackPawn;
		bSelectedIsPawn = true;
		bUsedFallbackPawnSearch = true;

		// 构造 HitResult (用 Pawn 位置和路径最近点)
		//   - ImpactNormal 简化处理: 用 Pawn 指向 trace 起点的反方向
		FVector ToPawnFromOrigin = (FallbackPawn->GetActorLocation() - RayOrigin).GetSafeNormal();
		HitResult.Location = FallbackPawn->GetActorLocation();
		HitResult.ImpactPoint = FallbackPawn->GetActorLocation();
		HitResult.ImpactNormal = -ToPawnFromOrigin;
		HitResult.Distance = FallbackPawnPathDistance;
		HitResult.BoneName = NAME_None;
	}
	else if (bHit && HitResults.Num() > 0)
	{
		const FHitResult* NearestPawnHit = nullptr;
		float NearestPawnDistance = TNumericLimits<float>::Max();
		for (const FHitResult& HR : HitResults)
		{
			if (ABaseCharacter* HitChar = Cast<ABaseCharacter>(HR.GetActor()))
			{
				if (HR.Distance < NearestPawnDistance)
				{
					NearestPawnDistance = HR.Distance;
					NearestPawnHit = &HR;
				}
			}
		}

		if (NearestPawnHit)
		{
			// 优先: 最近的 Pawn (业务核心目标)
			HitResult = *NearestPawnHit;
			SelectedTargetActor = HitResult.GetActor();
			bSelectedIsPawn = true;
		}
		else
		{
			// 兜底: 没 Pawn 命中, 用最近地形 (旧 LineTraceSingle 行为)
			HitResult = HitResults[0];
			SelectedTargetActor = HitResult.GetActor();
			bSelectedIsPawn = false;
		}
	}

	// ===========================================
	// 步骤 7: 命中处理 (v97.0 大厂原则 - 优先 Pawn + Phase 2 兜底)
	// ===========================================
	if (bHit && SelectedTargetActor)
	{
		// 玩家/AI 判定
		bool bAIDriven = false;
		if (const ABaseCharacter* OwnerChar = Cast<ABaseCharacter>(Weapon->GetOwner()))
		{
			bAIDriven = OwnerChar->IsAttackerAI();
		}

		// 调 Server_ReportHit (RPC, 服务器验算伤害)
		Weapon->Server_ReportHit(
			SelectedTargetActor,
			0.0f, // 伤害值由服务器重算
			HitResult.ImpactPoint,
			HitResult.ImpactNormal,
			HitResult.BoneName,
			false, // 枪械暂不分轻重
			bAIDriven
		);

		UE_LOG(LogTemp, Log,
			TEXT("[URangedLineStrategy::PerformSingleShot] ★命中★ — Weapon=%s Target=%s Bone=%s bIsPawn=%d bFallback=%d Origin=%s End=%s (Range=%.1fcm, HitResults.Num=%d)"),
			*Weapon->GetName(),
			*SelectedTargetActor->GetName(),
			*HitResult.BoneName.ToString(),
			bSelectedIsPawn ? 1 : 0,
			bUsedFallbackPawnSearch ? 1 : 0,
			*RayOrigin.ToCompactString(),
			*EndLoc.ToCompactString(),
			Range,
			HitResults.Num());
	}
	else
	{
		UE_LOG(LogTemp, Log,
			TEXT("[URangedLineStrategy::PerformSingleShot] 未命中 — Weapon=%s Origin=%s Dir=%s Range=%.1fcm"),
			*Weapon->GetName(),
			*RayOrigin.ToCompactString(),
			*RayDirection.ToCompactString(),
			Range);
	}

	// ============================================================
	// 【v83 大厂架构 — 客户端屏幕可见】服务器 trace 完后 Multicast 给所有客户端画线
	//   - 服务器本地 (ListenServer 自己的玩家) 跳过 — 服务器进程已经用 EDrawDebugTrace::ForDuration 画过
	//   - 远端客户端收到 RPC 后, 客户端进程画一条完全一致的线
	//   - 大厂原则: 服务器权威 trace → 客户端视觉同步, 不重复 trace
	// ============================================================
	if (Weapon->HasAuthority())
	{
		const FVector HitLocation = (bHit && SelectedTargetActor) ? HitResult.ImpactPoint : EndLoc;
		Weapon->Multicast_PlayFireTraceVisual(RayOrigin, EndLoc, bHit, HitLocation);
	}

	return true;
}
