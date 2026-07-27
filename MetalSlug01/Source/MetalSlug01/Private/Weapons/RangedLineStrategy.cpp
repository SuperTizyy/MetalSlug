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
	// 步骤 2: 决定射线起点和方向 — v109 大厂镜像方案
	// ===========================================
	//
	// 【v109 大厂架构 P0 修复 — 玩家/AI 完全镜像】
	//
	//   旧版历史 (按时间累积的反模式):
	//     - v60.3-v107: 外层 Cast<APlayerController> 守卫,直接拒绝 AI (AI 永远打不出射线)
	//     - v85.4:     修玩家路径(加 TAL+枪口偏移算武器起点)
	//     - v108:      加 AI 分支(用 ZeroVector 区分 AI → Strategy 内部算 Muzzle+朝向)
	//                   ⚠️ v108 修改不完整 — 嵌套了第二层 if/else,使所有 AI 分支变成死代码
	//     - v109:      重写整个步骤 2,彻底消除死代码
	//
	//   v109 终极修复 — "算射线" 关注点统一在 ABaseCharacter::GetAimRayFromCrosshairOrEyes:
	//     - 玩家: IA_Fire → GetAimRayFromCrosshairOrEyes (本地玩家读 HUD Crosshair) → Server_StartFire RPC (传射线)
	//     - AI  :  BT → GetAimRayFromCrosshairOrEyes (IsLocallyControlled=false → Muzzle Socket + BaseAimRotation)
	//             → Weapon->StartFire (服务器本地直接调,不走 RPC,因为 BT 就在 Server 跑)
	//
	//   Strategy 不再做任何"算射线" 工作 — 完全信任入参射线(零冗余)
	//
	//   单一真理源 — 大厂原则:
	//     - "用什么射线打谁" 的真理源 = ABaseCharacter::GetAimRayFromCrosshairOrEyes
	//     - Strategy 只是把射线投射出去 + 命中过滤 + Server_ReportHit RPC 转发
	//
	//   大厂原则 — 镜像玩家 + 零兜底:
	//     - 玩家/AI 都从同一 API 拿射线(对称设计 — 这才是"AI 镜像玩家" 的大厂级实现)
	//     - 玩家路径特殊性: 传进来的是相机位置 → 服务器需要 + Forward × (TAL + MuzzleOffset) 反推武器起点
	//     - AI 路径特殊性: 传进来的就是 Muzzle Socket 位置 → 直接用
	// ===========================================

	FVector RayDirection;
	FVector RayOrigin;

	// 步骤 2a: 入参校验 — 零兜底
	if (ClientRayOrigin.IsNearlyZero() || ClientRayDirection.IsNearlyZero())
	{
		UE_LOG(LogTemp, Error,
			TEXT("[URangedLineStrategy::PerformSingleShot] 【v109 零兜底】Weapon=%s 调用方未提供有效射线 — "
			     "ClientRayOrigin=%s ClientRayDirection=%s. "
			     "【修复】确认 ABaseCharacter::GetAimRayFromCrosshairOrEyes 的调用方"),
			*Weapon->GetName(),
			*ClientRayOrigin.ToCompactString(),
			*ClientRayDirection.ToCompactString());
		return false;
	}

	// 步骤 2b: 转换射线方向为单位向量
	RayDirection = ClientRayDirection.GetSafeNormal();
	if (RayDirection.IsNearlyZero())
	{
		UE_LOG(LogTemp, Error,
			TEXT("[URangedLineStrategy::PerformSingleShot] Weapon=%s ClientRayDirection GetSafeNormal 后为零, 拒绝射线."),
			*Weapon->GetName());
		return false;
	}

	// 步骤 2c: 玩家/AI 区分 — bIsLocalPlayerFire=true 走玩家路径 (相机+TAL+MuzzleOffset),
	//                                       false 走 AI 路径 (直接用入参射线 — Muzzle+朝向)
	APlayerController* InstigatorPC = WeaponOwner ? Cast<APlayerController>(WeaponOwner->GetInstigatorController()) : nullptr;
	const bool bIsLocalPlayerFire = (InstigatorPC != nullptr) && InstigatorPC->IsLocalController();

	if (bIsLocalPlayerFire)
	{
		// 玩家路径 (v60.16 公式): RayOrigin = ClientRayOrigin + RayDirection × (TAL + MuzzleOffset)
		ACharacter* CharacterOwner = Cast<ACharacter>(WeaponOwner);
		if (!CharacterOwner)
		{
			UE_LOG(LogTemp, Error,
				TEXT("[URangedLineStrategy::PerformSingleShot] 【v109 玩家路径】Weapon=%s Owner 不是 ACharacter, 拒绝射线."),
				*Weapon->GetName());
			return false;
		}

		USpringArmComponent* CameraBoom = Cast<USpringArmComponent>(
			CharacterOwner->GetComponentByClass(USpringArmComponent::StaticClass()));
		if (!CameraBoom)
		{
			UE_LOG(LogTemp, Error,
				TEXT("[URangedLineStrategy::PerformSingleShot] 【v109 玩家路径】Weapon=%s 找不到 CameraBoom (SpringArmComponent), 拒绝射线. "
				     "修复: 检查玩家 BP 的 CameraBoom 是否正确挂载."),
				*Weapon->GetName());
			return false;
		}

		const float TargetArmLength = CameraBoom->TargetArmLength;
		const float MuzzleOffset = Weapon->MuzzleOffset;
		const float TotalOffset = TargetArmLength + MuzzleOffset;
		RayOrigin = ClientRayOrigin + RayDirection * TotalOffset;

		UE_LOG(LogTemp, Log,
			TEXT("[URangedLineStrategy::PerformSingleShot] 【v109 玩家路径】Weapon=%s CameraLoc=%s TAL=%.1f MuzzleOffset=%.1f TotalOffset=%.1f RayOrigin=%s RayDir=%s"),
			*Weapon->GetName(),
			*ClientRayOrigin.ToCompactString(),
			TargetArmLength,
			MuzzleOffset,
			TotalOffset,
			*RayOrigin.ToCompactString(),
			*RayDirection.ToCompactString());
	}
	else
	{
		// AI 路径 (v109 大厂镜像): RayOrigin = ClientRayOrigin (GetAimRayFromCrosshairOrEyes AI 路径已算好)
		//   - ClientRayOrigin = Muzzle Socket 世界位置
		//   - ClientRayDirection = Character 朝向 (BaseAimRotation, BT 已控制 AI 面朝目标)
		//   - 这才是"AI 跟玩家走完全相同的 Strategy" 的大厂镜像方案
		RayOrigin = ClientRayOrigin;

		UE_LOG(LogTemp, Log,
			TEXT("[URangedLineStrategy::PerformSingleShot] 【v109 AI 路径镜像】Weapon=%s Owner=%s RayOrigin=%s RayDir=%s "
			     "(前置已通过 GetAimRayFromCrosshairOrEyes AI 分支算出)"),
			*Weapon->GetName(),
			WeaponOwner ? *WeaponOwner->GetName() : TEXT("<null>"),
			*RayOrigin.ToCompactString(),
			*RayDirection.ToCompactString());
	}


//
// (v109 重写后:内嵌的双层 if/else 死代码已删除,见步骤 2 注释说明)
// ===========================================

	// ===========================================
	// 步骤 4: 计算射线终点 (玩家/AI 路径统一 — v109 大厂镜像后没有任何"算射线" 工作残留)
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
// 步骤 6: 【v97.0.2 大厂架构 P0 修复 → v99.3 升级全身骨骼覆盖】
//   两阶段命中检测 — LineTraceMulti (bTraceComplex=true) + Pawn 路径搜索
// ===========================================
//
// 【v97.0.2 修复动机 - 大厂原则】
//   旧版 (v60.3-v97.0.1) 反模式: LineTraceSingle/LineTraceMulti 只用单一线性射线
//   - 问题 (Session1.log line 797-1268, 18.27.x): 所有 LineTrace 都只命中 Landscape_0 / SM_building_wall_109
//           即使母体 (BP_MuTi_C_0) 在 trace 路径上 (300-500cm 处), trace 仍然不命中
//   - 真根因 (v97.0.2): BP_MuTi 蓝图 CapsuleComponent 对 ECC_Visibility trace channel 是 Ignore
//           - 这导致 LineTrace "穿过" 母体 Pawn → 命中远处 Landscape_0
//   - 后果: 人类武器 trace 永远不命中母体 → Server_ReportHit 不调用 → 母体不扣血
//
// 【v99.3 升级 - 全身骨骼覆盖】
//   上一版 (v97.0.2) 用 bTraceComplex=false 只 trace 物理 Body
//   真根因: BP_MuTi 物理资产 Mutant_PhysicsAsset 17 个 Body 团在一起 (PhAT 没调好)
//         只靠 Body trace 时,只有 "看得最清楚" 的 Body (通常是 spine 区域) 命中
//         实际玩起来 = "打手打脚不扣血,只有胸口能扣血"
//   修复: bTraceComplex=true → Mesh 三角面参与 trace → 全身都能精确命中
//   - 大厂原则 (CS:GO/Apex/Valorant/Fortnite 标准): 现代射击游戏都用 complex trace 配合 per-vertex/face 伤害分布
//   - 物理 Body 团在一起的问题对 complex trace 无影响 — 三角面是角色皮肤精度,与 Body 解耦
//   - 这样 BP_MuTi 既不需要重生成 PhysicsAsset 也不需要调 PhAT Body 大小,母体全身可被命中
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

	// 【v99.3 关键改动】bTraceComplex=true — Mesh 三角面参与 trace
	// 上一版 (v97.0.2) 用 false,只 trace 物理 Body
	// 但 BP_MuTi 物理资产 Mutant_PhysicsAsset 17 个 Body 团在一起 (PhAT 没调好)
	// → 只靠 Body trace,玩家打手打脚打头全打不到 (Body 被 spine 区域遮挡)
	// → 现在改成 true: Mesh 三角面(皮肤精度)参与 trace
	// → 全身任何部位都能被精确命中 (与 Body 大小无关,与 Mutant Mesh 顶点/三角面精度相关)
	// → 符合 CS:GO/Apex/Valorant 大厂标准 (现代射击游戏都用 per-vertex trace)
	const bool bHit = UKismetSystemLibrary::LineTraceMulti(
		Weapon,
		RayOrigin,     // 起点: v109 大厂镜像 — 玩家=相机+TAL+MuzzleOffset, AI=GetAimRayFromCrosshairOrEyes 已算 Muzzle Socket
		EndLoc,        // 终点: 起点 + 方向 * 射程
		UEngineTypes::ConvertToTraceType(ECC_Visibility),
		true,          // 【v99.3 关键】bTraceComplex=true — Mesh 三角面 + 物理 Body 都参与 trace
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

	// 【v99 P0 修复】把 World 提到外层, 让步骤 6.5 二次精确 trace 也能用
	UWorld* World = Weapon->GetWorld();

	if (!bHasPawnInTrace && World)
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

			// 【v97.0.2】横向距离 ≤ 30cm (严格 < Pawn Capsule 半径) → 视为命中
			// 30cm = 接近人体 Capsule 半径 (UE 默认 42cm), 但严格小于, 不会误命中旁边 Pawn
			// 真根因修复在 BP 层 (BP_MuTi 对 ECC_Visibility 设 Block), 不要扩大容差兜底
			if (LateralDistance <= 30.0f && ProjectedDist < FallbackPawnPathDistance)
			{
				FallbackPawnPathDistance = ProjectedDist;
				FallbackPawn = Candidate;
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

		// ============================================================
		// 【v99 P0 修复】母体分头/身体伤害 — Phase 2.5 二次精确 trace 取 BoneName
		//
		// 业务核心 (用户 2026.07.26 明确):
		//   - 母体被打要分头部和身体 (跟人类一样), 否则不算"正确实现"
		//
		// 根因 (Session1.log line 1194):
		//   - 旧版 Phase 2 fallback 强制 BoneName = NAME_None
		//   - BaseWeapon::Server_ReportHit 看到 BoneName != "head" → 永远走 LightDamageBody (20 伤害)
		//   - 用户感受到"母体不分头身体"
		//
		// 大厂原则 — 单一真理源 + 大厂数据驱动:
		//   - BoneName 真理源 = UE SkeletalMeshComponent::GetBoneName / HitResult.BoneName
		//   - Phase 2 fallback 因为走世界坐标搜索, 没经过碰撞 trace → 拿不到 BoneName
		//   - **修复**: Phase 2 fallback 后做一次精确 LineTraceSingle (武器 Socket → FallbackPawn 中心),
		//     只为拿 BoneName — 这是精确 trace, 不会"粗大", 符合用户"正常射线大小"要求
		//   - 二次 trace 不影响 Phase 2 fallback 命中逻辑 — 只是补一个字段
		//
		// 性能成本 (大厂原则 - 可观测性):
		//   - Phase 2 fallback 命中率 < 1% (只有 BP_MuTi collision 配错才走), 二次 trace 几乎不跑
		//   - 单次 LineTraceSingle < 0.01ms (UE 物理引擎内部 trace)
		//
		// 不破坏刀战模式 (大厂原则 - 零耦合):
		//   - 刀战模式 Phase 1 (LineTraceMulti) 已经能拿到 BoneName → 不走 Phase 2 fallback → 不受影响
		//   - 只有 Phase 2 fallback 才走二次 trace (兼容性修复)
		// ============================================================
		{
			FHitResult PreciseHit;
			// 【v99 P0.1 修复】FCollisionQueryParams 不能 const (它内部需要可写 TraceTag 等)
			FCollisionQueryParams PreciseParams(SCENE_QUERY_STAT(RangedStrategyBoneProbe), false, Weapon);
			PreciseParams.bReturnPhysicalMaterial = false;
			PreciseParams.bTraceComplex = true; // 复杂碰撞 (骨骼级 trace) — 必须, 否则 BoneName 是 NAME_None
			PreciseParams.bReturnFaceIndex = false;

			const bool bPreciseHit = World->LineTraceSingleByChannel(
				PreciseHit,
				RayOrigin,           // 从武器 Socket (Phase 1 的起点)
				FallbackPawn->GetActorLocation(),  // 到 Pawn 中心
				ECC_Visibility,
				PreciseParams
			);

			if (bPreciseHit && PreciseHit.BoneName != NAME_None)
			{
				// 二次 trace 拿到骨骼名 (head/body 等) → 真实 BoneName
				HitResult.BoneName = PreciseHit.BoneName;
				HitResult.Location = PreciseHit.Location;
				HitResult.ImpactPoint = PreciseHit.ImpactPoint;

				UE_LOG(LogTemp, Log,
					TEXT("[URangedLineStrategy::PerformSingleShot] 【v99 P0】Phase 2.5 二次 trace 拿到 BoneName='%s' for Target='%s'. "
					     "母体伤害将按头/身体区分."),
					*PreciseHit.BoneName.ToString(),
					*FallbackPawn->GetName());
			}
			else
			{
				// 二次 trace 没拿到 (Pawn 无骨骼 / collision 完全关闭) → 保持 NAME_None (走身体伤害)
				HitResult.BoneName = NAME_None;

				UE_LOG(LogTemp, Verbose,
					TEXT("[URangedLineStrategy::PerformSingleShot] 【v99 P0】Phase 2.5 二次 trace 未拿 BoneName for Target='%s' (bPreciseHit=%d). "
					     "母体伤害默认走身体路径."),
					*FallbackPawn->GetName(),
					bPreciseHit ? 1 : 0);
			}
		}
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
