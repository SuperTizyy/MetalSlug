// ==========================================
// UFootstepComponent 实现
// 职责: 播放带距离衰减的脚步音效. 单一真理源 (FootstepSound + FootstepAttenuation) 在 BP 配置.
// 这是 UE 5.6 标准做法 — 大厂项目 (Lyra): 距离衰减配置在 SoundAttenuation 资产, 不在 C++ 兜底.
// ==========================================
#include "Components/FootstepComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Logs/MetalSlugLogChannels.h"
#include "PhysicalMaterials/PhysicalMaterial.h"
#include "Engine/World.h"
#include "Sound/SoundAttenuation.h"

UFootstepComponent::UFootstepComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
}

/**
 * @brief 播放脚步声 — 单次触发, 带距离衰减
 * @param OwnerChar 脚步声所属的 ACharacter (用于 Log Error 时显示角色名)
 * @param Location  脚步触发的世界位置 (UE AnimNotify 传入)
 *
 * 大厂原则 (v241):
 *   - 距离衰减完全由 FootstepAttenuation (BP 配置 SoundAttenuation 资产) 决定, C++ 不做兜底
 *   - 缺失 FootstepSound / FootstepAttenuation → Log Error + 拒绝播放 (零兜底, 强制 BP 修复)
 *   - 随机音高 (0.9~1.1) 提升听感自然度
 */
void UFootstepComponent::PlayFootstep(ACharacter* OwnerChar, const FVector& Location)
{
	// 【v241 零兜底】Owner 链校验
	UWorld* World = OwnerChar ? OwnerChar->GetWorld() : GetWorld();
	if (!World || !OwnerChar)
	{
		return;
	}

	// 【v241 零兜底】FootstepSound 缺失 = BP 没配 — Log Error + 拒绝播放
	if (!FootstepSound)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[FootstepComponent][v241] PlayFootstep: FootstepSound 未配置! "
			     "Owner=%s. 【零兜底】必须修复 BP Components.Footstep.FootstepSound."),
			*OwnerChar->GetName());
		return;
	}

	// 【v241 零兜底】FootstepAttenuation 缺失 = BP 没配 — Log Error + 拒绝播放
	// 这是缺失距离衰减的根因 — 修复路径: BP 里配 SA_Footstep / SA_MotherFootstep 资产
	if (!FootstepAttenuation)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[FootstepComponent][v241] PlayFootstep: FootstepAttenuation 未配置! "
			     "Owner=%s. 【零兜底】必须修复 BP Components.Footstep.FootstepAttenuation. "
			     "【缺失距离衰减的根因】没有这个资产, 脚步声无视距离, 任何距离都一样响. "
			     "【修复】UE 内容浏览器 → 右键 → Sounds → Sound Attenuation → 创建 SA_Footstep 配置 Falloff Distance. "
			     "然后拖到 BP Components.Footstep.FootstepAttenuation 字段."),
			*OwnerChar->GetName());
		return;
	}

	// 【v241 移除】CVarFootstepVolume 兜底 — 距离衰减由 BP 配置的 SoundAttenuation 资产决定
	// 旧 CVar 是"运行时调全局音量" 的反模式 — 真正的"音量衰减"是 BP 资产的责任

	// 【v241 移除】Crouch/Run/Walk 分支判断 — 旧三选一是反模式 (走/Walk 状态本质都是同一类声音)
	// 脚步声音量衰减由 SoundAttenuation 决定, 移动状态由骨骼动画节奏决定 (AnimNotify 频率)

	// 【v241 移除】地面 LineTrace 物理材质查询 — 旧逻辑只 Log Verbose, 没有任何业务用途
	// 如果未来需要"不同地面不同音效", 应该在 BP 加 MetaSounds Switch 节点, 而不是 C++ 模板化

	// 随机音高变化 (0.9~1.1) — 听感更自然, 与距离衰减无关
	const float PitchMultiplier = FMath::FRandRange(0.9f, 1.1f);

	// 【v241 核心修复】带距离衰减的播放
	// PlaySoundAtLocation 的"含 FRotator" 重载:
	//   (World, Sound, Location, Rotation, VolumeMul, PitchMul, StartTime, Attenuation, Concurrency, Owner, InitialParams)
	// FootstepAttenuation 是 BP 配置的资产, 距离衰减曲线由它决定
	const float FinalVolume = FootstepVolumeMultiplier;
	UGameplayStatics::PlaySoundAtLocation(
		World,
		FootstepSound,
		Location,
		FRotator::ZeroRotator,
		FinalVolume,
		PitchMultiplier,
		/*StartTime=*/0.0f,
		/*AttenuationSettings=*/FootstepAttenuation,
		/*ConcurrencySettings=*/nullptr,
		/*OwningActor=*/nullptr,
		/*InitialParams=*/nullptr
	);

	const FString AttenuationName = FootstepAttenuation->GetName();
	UE_LOG(LogTemp, Verbose,
		TEXT("[FootstepComponent][v241] 播放脚步声: Loc=(%.1f, %.1f, %.1f), Crouch=%d, VolMul=%.2f, Attenuation=%s"),
		Location.X, Location.Y, Location.Z,
		OwnerChar->bIsCrouched ? 1 : 0,
		FinalVolume,
		*AttenuationName);
}
