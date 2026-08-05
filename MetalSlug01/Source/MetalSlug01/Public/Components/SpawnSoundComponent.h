// ==========================================
// 通用出生音效 Component【v201.6 大厂架构新增】
//
// @brief 所有角色（玩家/AI/母体）出生/复活时播放一次音效
//
// 【架构定位 — 与 MotherSpawnSoundComponent 对称】
//   - MotherSpawnSoundComponent: 仅母体专属音效
//   - SpawnSoundComponent: 通用出生音效 (所有角色都可用)
//
// 【工作流】
//   1. BaseCharacter 构造函数中 CreateDefaultSubobject<USpawnSoundComponent>
//   2. 在角色 BP 上配 SpawnSound 字段 (USoundBase)
//   3. 服务器在角色出生/复活时调用 Multicast_PlaySpawnSound RPC
//   4. 组件在每台机器本地播放一次音效
//
// 【大厂原则 — 单一职责】
//   - 本组件只管本地音效播放
//   - 触发时机由调用方 (RoomSpawnSubsystem) 决定
//
// 【不破坏刀战模式】
//   - 刀战角色不配 SpawnSound 字段 → 收到 RPC 时组件内部拒绝播放
// ==========================================
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "SpawnSoundComponent.generated.h"

class USoundBase;
class ABaseCharacter;

/**
 * @class USpawnSoundComponent
 * @brief 通用出生音效组件 — 一次本地播放
 *
 * 工作流:
 *   1. BaseCharacter 构造函数中 CreateDefaultSubobject<USpawnSoundComponent>
 *   2. 在角色 BP 上配 SpawnSound 字段 (USoundBase)
 *   3. 服务器调用 BaseCharacter::Multicast_PlaySpawnSound() → 每台机器本地播一次
 *   4. 组件 UGameplayStatics::PlaySoundAtLocation(无 Multiplayer,本地播放)
 *
 * 大厂原则 — 单一真理源:
 *   - 组件不复制(音效本地播放,服务器 / 客户端各自播放)
 *   - 触发信号通过 ABaseCharacter::Multicast_PlaySpawnSound 统一分发
 */
UCLASS(ClassGroup = (MetalSlug), meta = (BlueprintSpawnableComponent))
class METALSLUG01_API USpawnSoundComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	USpawnSoundComponent();

	/**
	 * 在角色 Pawn 当前位置播放一次音效
	 *
	 * 触发方: ABaseCharacter::Multicast_PlaySpawnSound_Implementation (单一网络入口)
	 *
	 * @return true=播放成功, false=资产未配置或播放失败
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat|Spawn")
	bool PlaySpawnSound();

	// ==========================================
	// 配置项 (由角色 BP 配置)
	// ==========================================

	/**
	 * 出生音效资产 — 可选配置
	 *
	 * 不配 = 静默跳过 (不报错,这是可选功能)
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawn|Protocol")
	TObjectPtr<USoundBase> SpawnSound;

	/**
	 * 音量倍数,默认 1.0
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawn|Protocol", meta = (ClampMin = "0.0", ClampMax = "4.0"))
	float VolumeMultiplier = 1.0f;

	/**
	 * 音高倍数,默认 1.0
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Spawn|Protocol", meta = (ClampMin = "0.1", ClampMax = "4.0"))
	float PitchMultiplier = 1.0f;

private:
	/**
	 * Lazy resolve Owner
	 */
	ABaseCharacter* ResolveOwnerCharacter() const;
};
