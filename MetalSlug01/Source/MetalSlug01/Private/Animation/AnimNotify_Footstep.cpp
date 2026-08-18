// 版权声明：在项目设置的描述页面填写您的版权信息。

// ==========================================
// UAnimNotify_Footstep 实现 (脚步声通知)
// ==========================================
//
// 【大厂原则 — 职责单一】
//   本类是一个 AnimNotify — 仅在动画播放到指定帧时触发一次
//   职责: 在脚部落地帧播放脚步声 + 调用蓝图钩子 OnFootstep
//
// 【职责链】
//   美术在 Run/Walk/Crouch 等蒙太奇时间轴上挂此通知 → 脚落地瞬间播放音效
//   1. 防崩保护 (MeshComp/Animation/OwningCharacter)
//   2. 计算脚部位置 (foot_l/foot_r 中点)
//   3. 安全获取 World (从 Owner 拿, 避免 UE 版本兼容问题)
//   4. 应用随机音高变化 (避免重复感)
//   5. 调用 UGameplayStatics::PlaySoundAtLocation
//   6. 调用蓝图钩子 OnFootstep_Implementation (子类可覆盖触发尘土粒子等)
//
// 【零兜底原则】
//   - MeshComp/Animation/OwningCharacter 任一为空 → 静默返回 (编辑器预览常见)
//   - World/FootstepSound 任一为空 → 静默返回 (编辑器配置缺失不刷错)
// ==========================================

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
 * @brief  返回通知在编辑器中的显示名称, 便于美术在蒙太奇时间轴上识别
 * @return FString 固定返回 "Footstep"
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
 * @brief  计算脚步声的播放位置 — 优先取 foot_l/foot_r 插槽中点 (更精准反映脚的位置)
 * @param  MeshComp  当前播放动画的骨骼网格组件
 * @return FVector   播放位置 (脚部中点 或 Mesh 位置兜底)
 * @note   若两个 foot 插槽都存在 → 取中点 + 强制 Z 用 Mesh 高度 (防止脚部 Z 抖动)
 * @note   若插槽缺失 → 兜底用 Mesh 组件位置 (零兜底 — 但不会刷错, 因为只是位置不准)
 */
FVector UAnimNotify_Footstep::GetFootstepLocation(const USkeletalMeshComponent* MeshComp) const
{
	// 防崩保护
	if (!MeshComp)
	{
		return FVector::ZeroVector;
	}

	// 默认位置: Mesh 组件世界坐标 (兜底)
	FVector Location = MeshComp->GetComponentLocation();

	// 尝试获取 foot_l/foot_r 插槽世界坐标
	FVector foot_l = MeshComp->GetSocketLocation(FName("foot_l"));
	FVector foot_r = MeshComp->GetSocketLocation(FName("foot_r"));

	// 两个插槽都存在时 → 取中点 (脚部中心更精准)
	if (!foot_l.IsZero() && !foot_r.IsZero())
	{
		Location = (foot_l + foot_r) * 0.5f;
		Location.Z = MeshComp->GetComponentLocation().Z; // Z 用 Mesh 高度, 避免脚部 Z 抖动
	}

	return Location;
}


// ==========================================
// 3. 核心回调：动画播放到此帧时自动调用
// ==========================================

/**
 * Notify
 *
 * @brief  AnimNotify 基类核心回调 — 蒙太奇播放到此帧时引擎自动调用
 * @param  MeshComp         当前播放动画的骨骼网格组件
 * @param  Animation        触发该通知的动画序列
 * @param  EventReference   通知事件引用 (UE 5 新 API)
 * @note   1. 防崩保护 → 2. 获取播放位置 → 3. 应用随机音高 → 4. 播放音效 → 5. 调蓝图钩子
 */
void UAnimNotify_Footstep::Notify(
	USkeletalMeshComponent* MeshComp,
	UAnimSequenceBase* Animation,
	const FAnimNotifyEventReference& EventReference)
{
	// 1. 防崩保护: 任一为空直接返回 (编辑器预览常见情况, 静默合理)
	if (!MeshComp || !Animation)
	{
		return;
	}

	// 2. 取 Owner 角色 — 静默 return (编辑器预览常见)
	ACharacter* OwningCharacter = Cast<ACharacter>(MeshComp->GetOwner());
	if (!OwningCharacter)
	{
		return;
	}

	// 3. 计算播放位置 (foot_l/foot_r 中点 或 Mesh 兜底)
	FVector FootLocation = GetFootstepLocation(MeshComp);

	// 【P2 修复】安全获取 World：优先从 MeshComp 获取
	// FAnimNotifyEventReference 在部分 UE5 版本中不含 GetWorld()，
	// 改用 MeshComp->GetOwner()->GetWorld()，比直接用 MeshComp->GetWorld() 更可靠
	UWorld* World = nullptr;
	if (AActor* Owner = MeshComp->GetOwner())
	{
		World = Owner->GetWorld();
	}
	// 兜底: Owner 没拿到 World 时用 MeshComp 自己的 World
	if (!World)
	{
		World = MeshComp->GetWorld();
	}
	// World 缺失 或 FootstepSound 未配置 → 静默返回 (编辑器配置错不刷错)
	if (!World || !FootstepSound)
	{
		return;
	}

	// 4. 计算随机音高 (PitchVariation > 0 时启用, 避免每次脚步声完全一样)
	float FinalPitch = 1.0f;
	if (PitchVariation > 0.0f)
	{
		FinalPitch = UKismetMathLibrary::RandomFloatInRange(1.0f - PitchVariation, 1.0f + PitchVariation);
	}

	// 5. 在脚部位置播放音效 (UGameplayStatics 标准调用)
	UGameplayStatics::PlaySoundAtLocation(
		MeshComp,
		FootstepSound,
		FootLocation,
		FRotator::ZeroRotator,
		VolumeMultiplier,
		FinalPitch
	);

	// 6. 调用蓝图钩子 — 子类可覆盖此逻辑 (例如触发地面尘土粒子)
	OnFootstep(MeshComp, FootLocation);
}


// ==========================================
// 4. 蓝图钩子实现
// ==========================================

/**
 * OnFootstep_Implementation
 *
 * @brief  虚函数实现, 蓝图子类可覆盖此逻辑 (例如触发地面尘土粒子特效)
 * @param  MeshComp  当前播放动画的骨骼网格组件
 * @param  Location  脚步声播放位置
 * @note   默认空实现 — 蓝图子类覆盖后可在角色脚下播放尘土/水花等粒子
 */
void UAnimNotify_Footstep::OnFootstep_Implementation(const USkeletalMeshComponent* MeshComp, FVector Location) const
{
	// 虚函数实现，蓝图子类可覆盖此逻辑
	// 例如在此处触发地面尘土粒子特效
}
