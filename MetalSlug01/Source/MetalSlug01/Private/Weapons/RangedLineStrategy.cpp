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


URangedLineStrategy::URangedLineStrategy()
{
	bIsCurrentHeavy = false;
}


// ===========================================
// 1. StartTrace — v60.3 立即执行一发射线
// ===========================================

bool URangedLineStrategy::StartTrace(ABaseWeapon* Weapon, bool bIsHeavy)
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
		TEXT("[URangedLineStrategy::StartTrace] 立即执行枪械射线 — Weapon=%s Type=%d Heavy=%d"),
		*Weapon->GetName(),
		static_cast<int32>(Weapon->GetMeshType()),
		bIsHeavy ? 1 : 0);

	// 立即打一发 (不再延后)
	const bool bExecuted = PerformSingleShot(Weapon);
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

bool URangedLineStrategy::PerformSingleShot(ABaseWeapon* Weapon)
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
	// 步骤 3: 获取相机位置和方向 (v60.16 核心)
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
	// 步骤 4: 获取 TargetArmLength (相机到角色中心的距离)
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
	// CameraBoom 是角色 Mesh 的子组件，连接到 RootComponent
	USpringArmComponent* CameraBoom = Cast<USpringArmComponent>(
		CharacterOwner->GetComponentByClass(USpringArmComponent::StaticClass()));

	float TargetArmLength = 300.0f; // 默认值

	if (CameraBoom)
	{
		TargetArmLength = CameraBoom->TargetArmLength;
	}
	else
	{
		// CameraBoom 不存在时，用默认值
		UE_LOG(LogTemp, Warning,
			TEXT("[URangedLineStrategy::PerformSingleShot] Weapon=%s 找不到 CameraBoom，用默认值 TAL=300cm."),
			*Weapon->GetName());
	}

	// ===========================================
	// 步骤 5: 计算枪口偏移后的射线起点 (v60.16 核心公式)
	// ===========================================
	// 第三人称视角布局: 枪口 → 角色 → 相机 (从前往后)
	// CameraForward 指向相机方向（从枪口指向相机的方向）
	// 枪口在相机前方（目标方向），所以射线起点 = 相机位置 + CameraForward × (TAL + MuzzleOffset)
	const float MuzzleOffset = Weapon->MuzzleOffset;
	const float TotalOffset = TargetArmLength + MuzzleOffset;
	const FVector RayOrigin = CameraLocation + CameraForward * TotalOffset;

	// ===========================================
	// 步骤 6: 获取射击方向 (准星屏幕坐标 → 世界射线)
	// ===========================================
	int32 ViewportSizeX, ViewportSizeY;
	PC->GetViewportSize(ViewportSizeX, ViewportSizeY);

	const FVector2D CrosshairScreenPos = FVector2D(
		ViewportSizeX * 0.5f,
		ViewportSizeY * 0.5f
	);

	FVector WorldOrigin;
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

	// ===========================================
	// 步骤 7: 计算射线终点
	// ===========================================
	const float Range = Weapon->AttackRange;
	const FVector EndLoc = RayOrigin + WorldDirection * Range;

	// ===========================================
	// 步骤 8: 命中过滤 (自己 + Owner)
	// ===========================================
	TArray<AActor*> IgnoreActors;
	IgnoreActors.Add(Weapon);
	if (AActor* Owner = Weapon->GetOwner())
	{
		IgnoreActors.Add(Owner);
	}

	// ===========================================
	// 步骤 9: 执行 LineTraceSingle 射线检测
	// ===========================================
	FHitResult HitResult;
	const bool bHit = UKismetSystemLibrary::LineTraceSingle(
		Weapon,
		RayOrigin,     // 起点: 枪口位置 (相机 - 偏移)
		EndLoc,        // 终点: 枪口 + 方向 * 射程
		UEngineTypes::ConvertToTraceType(ECC_Visibility),
		false,
		IgnoreActors,
		EDrawDebugTrace::ForDuration,
		HitResult,
		true,
		FLinearColor::Red,       // TraceColor: 未命中
		FLinearColor::Green,     // TraceHitColor: 命中
		IWeaponDamageStrategy::kDebugTraceLifeTimeSeconds
	);

	// ===========================================
	// 步骤 10: 命中处理
	// ===========================================
	if (bHit && HitResult.GetActor())
	{
		// 玩家/AI 判定
		bool bAIDriven = false;
		if (const ABaseCharacter* OwnerChar = Cast<ABaseCharacter>(Weapon->GetOwner()))
		{
			bAIDriven = OwnerChar->IsAttackerAI();
		}

		// 调 Server_ReportHit (RPC, 服务器验算伤害)
		Weapon->Server_ReportHit(
			HitResult.GetActor(),
			0.0f, // 伤害值由服务器重算
			HitResult.ImpactPoint,
			HitResult.ImpactNormal,
			HitResult.BoneName,
			false, // 枪械暂不分轻重
			bAIDriven
		);

		UE_LOG(LogTemp, Log,
			TEXT("[URangedLineStrategy::PerformSingleShot] ★命中★ — Weapon=%s Target=%s Bone=%s Origin=%s End=%s (TAL=%.0f+MO=%.0f=%.0fcm)"),
			*Weapon->GetName(),
			*HitResult.GetActor()->GetName(),
			*HitResult.BoneName.ToString(),
			*RayOrigin.ToCompactString(),
			*EndLoc.ToCompactString(),
			TargetArmLength, MuzzleOffset, TotalOffset);
	}
	else
	{
		UE_LOG(LogTemp, Log,
			TEXT("[URangedLineStrategy::PerformSingleShot] 未命中 — Weapon=%s Origin=%s Dir=%s Range=%.1fcm (TAL=%.0f+MO=%.0f=%.0fcm)"),
			*Weapon->GetName(),
			*RayOrigin.ToCompactString(),
			*WorldDirection.ToCompactString(),
			Range,
			TargetArmLength, MuzzleOffset, TotalOffset);
	}

	return true;
}
