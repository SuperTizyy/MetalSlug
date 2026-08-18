// 通用出生音效 Component 实现【v201.6 大厂架构新增】
#include "Components/SpawnSoundComponent.h"
#include "Characters/BaseCharacter.h"
#include "Kismet/GameplayStatics.h"


USpawnSoundComponent::USpawnSoundComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	SetIsReplicatedByDefault(false);
}


ABaseCharacter* USpawnSoundComponent::ResolveOwnerCharacter() const
{
	if (!GetOwner())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[SpawnSound] ResolveOwnerCharacter: GetOwner() 返回 null. Owner=%s"),
			*GetName());
		return nullptr;
	}

	ABaseCharacter* OwnerChar = Cast<ABaseCharacter>(GetOwner());
	if (!OwnerChar)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[SpawnSound] ResolveOwnerCharacter: Owner '%s' 不是 ABaseCharacter 派生."),
			*GetOwner()->GetName());
		return nullptr;
	}

	return OwnerChar;
}


/**
 * @brief 播放出生音效 — Multicast 路径调用, 客户端/服务器通用
 * @return true=成功播放, false=Owner 失效或未配置 SpawnSound
 *
 * 大厂原则 (v201.6):
 *   - 单一真理源: SpawnSound 在 BP 配置, C++ 不做兜底
 *   - 未配置资产 = 静默跳过 (可选功能, 不报错)
 *   - 客户端/服务器均可调 (音效本地播放, 不走网络)
 */
bool USpawnSoundComponent::PlaySpawnSound()
{
	ABaseCharacter* OwnerChar = ResolveOwnerCharacter();
	if (!OwnerChar)
	{
		return false;
	}

	// 可选：没配置资产 = 静默跳过
	if (!SpawnSound)
	{
		// 不报错，这是可选功能
		return false;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[SpawnSound] PlaySpawnSound: World 无效. Owner=%s."),
			*GetName());
		return false;
	}

	// 在角色位置播放音效 (3D 衰减)
	UGameplayStatics::PlaySoundAtLocation(
		this,
		SpawnSound,
		OwnerChar->GetActorLocation(),
		VolumeMultiplier,
		PitchMultiplier);

	UE_LOG(LogTemp, Display,
		TEXT("[SpawnSound] 出生音效已播放. Owner=%s, Sound=%s, Vol=%.2f, Pitch=%.2f."),
		*GetName(),
		*GetNameSafe(SpawnSound),
		VolumeMultiplier,
		PitchMultiplier);

	return true;
}
