// ==========================================
// 脚步音效 Component 【2026-06-15 重构: 完整迁移 BaseCharacter 脚步逻辑】
// 目的: BaseCharacter 的脚步物理检测与音效播放全部下沉到本 Component
// 优势:
//   1. 音效资源自治: Crouch/Run/Walk 三种脚步声在 Component 内配置
//   2. 音量 CVar 绑定: 通过 g.FootstepVolume 控制全局缩放
//   3. 地面检测: LineTrace 获取物理材质，可扩展不同地面不同音效
//   4. 随机音高: 0.9~1.1 随机变化，更自然的脚步听感
// 调用链: AnimNotify → BaseCharacter::PlayFootstepSound(Location)
//                          → FootstepComponent->PlayFootstep(OwnerChar, Location)
// ==========================================
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "FootstepComponent.generated.h"

class USoundBase;
class ACharacter;

UCLASS(ClassGroup = (MetalSlug), meta = (BlueprintSpawnableComponent))
class METALSLUG01_API UFootstepComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UFootstepComponent();

	/**
	 * 播放一次脚步音效
	 * 由 BaseCharacter::PlayFootstepSound(Location) 委托调用
	 * @param OwnerChar Owner 角色引用（用于获取速度/下蹲状态）
	 * @param Location 脚步播放位置
	 */
	UFUNCTION(BlueprintCallable, Category = "Footstep")
	void PlayFootstep(ACharacter* OwnerChar, const FVector& Location);

protected:
	/** 地面检测射线向下长度（cm） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Footstep|Trace")
	float TraceDistance = 150.0f;

	/** 地面检测射线起始偏移（向上抬避免穿模） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Footstep|Trace")
	float TraceStartOffset = 100.0f;

	/** 地面检测碰撞通道 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Footstep|Trace")
	TEnumAsByte<ECollisionChannel> FootstepTraceChannel = ECC_Visibility;

public:
	/** 下蹲时的脚步声资源 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Footstep|Audio")
	TObjectPtr<USoundBase> CrouchFootstepSound = nullptr;

	/** 奔跑时的脚步声资源 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Footstep|Audio")
	TObjectPtr<USoundBase> RunFootstepSound = nullptr;

	/** 行走时的脚步声资源 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Footstep|Audio")
	TObjectPtr<USoundBase> WalkFootstepSound = nullptr;
};
