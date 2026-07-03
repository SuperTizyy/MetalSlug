// ==========================================
// UFootstepComponent 实现 【2026-06-15 重构: 完整迁移 BaseCharacter 脚步逻辑】
// ==========================================
#include "Components/FootstepComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Kismet/KismetMathLibrary.h"
#include "Logs/MetalSlugLogChannels.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "Engine/World.h"

/** 脚步声全局音量缩放 CVar（控制台命令: g.FootstepVolume） */
static TAutoConsoleVariable<float> CVarFootstepVolume(
	TEXT("g.FootstepVolume"),
	1.0f,
	TEXT("Footstep volume multiplier (0.0 to 2.0).\n")
	TEXT("Default: 1.0"),
	ECVF_Default
);

UFootstepComponent::UFootstepComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}


void UFootstepComponent::PlayFootstep(ACharacter* OwnerChar, const FVector& Location)
{
	// 【P1 修复】安全校验：优先用 OwnerChar 的 World，而非 Component 自己的 World
	// Component 的 World 在某些边缘情况下可能为空（如 AI 重生时）
	UWorld* World = OwnerChar ? OwnerChar->GetWorld() : GetWorld();
	if (!World || !OwnerChar)
	{
		return;
	}

	// 读取全局音量 CVar
	float CVarVolume = CVarFootstepVolume.GetValueOnGameThread();
	if (CVarVolume <= 0.0f)
	{
		return;
	}

	// 根据 Owner 当前状态选择脚步声资源
	USoundBase* SoundToPlay = nullptr;

	if (OwnerChar->bIsCrouched && CrouchFootstepSound)
	{
		SoundToPlay = CrouchFootstepSound;
	}
	else
	{
		// 根据速度判断行走/奔跑
		const float Speed = OwnerChar->GetVelocity().Length();
		const float MaxWalkSpeed = OwnerChar->GetCharacterMovement()
			? OwnerChar->GetCharacterMovement()->MaxWalkSpeed
			: 600.0f;

		if (Speed > MaxWalkSpeed && RunFootstepSound)
		{
			SoundToPlay = RunFootstepSound;
		}
		else if (WalkFootstepSound)
		{
			SoundToPlay = WalkFootstepSound;
		}
	}

	if (!SoundToPlay)
	{
		return;
	}

	// 地面检测: LineTrace 获取物理材质（可扩展为不同地面不同音效）
	FHitResult HitResult;
	FVector TraceStart = Location + FVector(0.0f, 0.0f, TraceStartOffset);
	FVector TraceEnd = Location - FVector(0.0f, 0.0f, TraceDistance);

	FCollisionQueryParams QueryParams;
	QueryParams.bReturnPhysicalMaterial = true;
	QueryParams.AddIgnoredActor(OwnerChar);

	// 【P2 修复】使用 TraceChannel 而非盲选通道，确保能命中地面
	// 若追踪失败（AI 在空中或地形特殊），SoundToPlay 已在上面确定，仍然播放音效
	if (World->LineTraceSingleByChannel(HitResult, TraceStart, TraceEnd, FootstepTraceChannel, QueryParams))
	{
		if (UPhysicalMaterial* PhysMat = HitResult.PhysMaterial.Get())
		{
			UE_LOG(LogTemp, VeryVerbose, TEXT("[FootstepComponent] Surface=%s"),
				*PhysMat->GetName());
		}
	}
	else
	{
		// 追踪失败时记录调试信息，但仍然播放脚步声（避免 AI 完全没声音）
		UE_LOG(LogTemp, VeryVerbose, TEXT("[FootstepComponent] 地面追踪失败 Loc=(%.1f, %.1f, %.1f), 仍播放脚步声"),
			Location.X, Location.Y, Location.Z);
	}

	// 随机音高变化（0.9~1.1，更自然的听感）
	const float PitchMultiplier = UKismetMathLibrary::RandomFloatInRange(0.9f, 1.1f);

	// 在指定位置播放音效
	UGameplayStatics::PlaySoundAtLocation(
		World,
		SoundToPlay,
		Location,
		FRotator::ZeroRotator,
		CVarVolume,
		PitchMultiplier
	);

	UE_LOG(LogTemp, Log, TEXT("[FootstepComponent] 播放脚步声: Loc=(%.1f, %.1f, %.1f), Crouch=%d"),
		Location.X, Location.Y, Location.Z, OwnerChar->bIsCrouched);
}
