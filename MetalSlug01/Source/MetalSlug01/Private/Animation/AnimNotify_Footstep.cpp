// 版权声明：在项目设置的描述页面填写您的版权信息。

// ==========================================
// 头文件包含区
// ==========================================
// 引入本类头文件
#include "Animation/AnimNotify_Footstep.h"

// 引入骨骼网格组件
#include "Components/SkeletalMeshComponent.h"

// 引入角色基类（用于类型转换）
#include "GameFramework/Character.h"

// 引入播放声音静态函数
#include "Kismet/GameplayStatics.h"

// 引入数学函数（随机音高）
#include "Kismet/KismetMathLibrary.h"


// ==========================================
// 1. 通知名称（调试用）
// ==========================================

/**
 * GetNotifyName_Implementation
 *
 * 返回通知的名称（调试时可出现在日志中）
 */
FString UAnimNotify_Footstep::GetNotifyName_Implementation() const
{
	return TEXT("Footstep");
}


// ==========================================
// 2. 获取播放位置
// ==========================================

/**
 * GetFootstepLocation
 *
 * 获取播放位置
 * 默认: 优先取 foot_l 和 foot_r 插槽的中点（更精准反映脚的位置）
 * 兜底: 若插槽不存在则返回 Mesh 组件位置
 */
FVector UAnimNotify_Footstep::GetFootstepLocation(const USkeletalMeshComponent* MeshComp) const
{
	if (!MeshComp)
	{
		return FVector::ZeroVector;
	}

	FVector Location = MeshComp->GetComponentLocation();

	FVector foot_l = MeshComp->GetSocketLocation(FName("foot_l"));
	FVector foot_r = MeshComp->GetSocketLocation(FName("foot_r"));

	if (!foot_l.IsZero() && !foot_r.IsZero())
	{
		Location = (foot_l + foot_r) * 0.5f;
		Location.Z = MeshComp->GetComponentLocation().Z;
	}

	return Location;
}


// ==========================================
// 3. 核心回调：动画播放到此帧时自动调用
// ==========================================

/**
 * Notify
 *
 * AnimNotify 基类核心回调
 * 1. 防崩保护
 * 2. 获取播放位置
 * 3. 随机音高变化（更自然）
 * 4. 在指定位置播放音效
 * 5. 调用 OnFootstep 蓝图钩子
 */
void UAnimNotify_Footstep::Notify(
	USkeletalMeshComponent* MeshComp,
	UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	if (!MeshComp || !Animation)
	{
		return;
	}

	ACharacter* OwningCharacter = Cast<ACharacter>(MeshComp->GetOwner());
	if (!OwningCharacter)
	{
		return;
	}

	FVector FootLocation = GetFootstepLocation(MeshComp);

	// 编辑器预览时 MeshComp->GetWorld() 可能无效，直接跳过音效播放
	UWorld* World = MeshComp->GetWorld();
	if (!World || !FootstepSound)
	{
		return;
	}

	float FinalPitch = 1.0f;
	if (PitchVariation > 0.0f)
	{
		FinalPitch = UKismetMathLibrary::RandomFloatInRange(1.0f - PitchVariation, 1.0f + PitchVariation);
	}

	UGameplayStatics::PlaySoundAtLocation(
		this,
		FootstepSound,
		FootLocation,
		FRotator::ZeroRotator,
		VolumeMultiplier,
		FinalPitch
	);

	// 调用 OnFootstep 蓝图钩子
	OnFootstep(MeshComp, FootLocation);
}


// ==========================================
// 4. 蓝图钩子实现
// ==========================================

/**
 * OnFootstep_Implementation
 *
 * 虚函数实现，蓝图子类可覆盖此逻辑
 * 例如在此处触发地面尘土粒子特效
 */
void UAnimNotify_Footstep::OnFootstep_Implementation(const USkeletalMeshComponent* MeshComp, FVector Location) const
{
	// 虚函数实现，蓝图子类可覆盖此逻辑
	// 例如在此处触发地面尘土粒子特效
}
