// ==========================================
// 脚步音效 Component
// 职责单一:
//   本 Component 只负责"音效资源选择 + 距离衰减播放",
//   不做任何网络复制、网络状态管理 — 那是 BaseCharacter::Multicast_PlayFootstep 的事.
//
// 调用链 (重构后):
//   AnimNotify → BaseCharacter::PlayFootstepSound(Location)
//                → 服务器: 自己直接播放 (ListenServer / Dedicated)
//                → Multicast_PlayFootstep RPC → 所有客户端 Implementation
//                → FootstepComponent->PlayFootstep(SoundAttr, Location)
//                → UGameplayStatics::PlaySoundAtLocation 走 BP 配置的 Attenuation
//
// 大厂原则:
//   - 单一真理源: 距离衰减参数 (USoundAttenuation) 必须在 BP 配置, 不允许 CVar 兜底
//   - 零兜底: 没配 Attenuation → Log Error + 不播放
//   - 职责单一: Component 只管"放音", 不管"网络同步"
// ==========================================
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "FootstepComponent.generated.h"

class USoundBase;
class USoundAttenuation;
class ACharacter;

/**
 * @class UFootstepComponent
 * @brief 脚步音效组件 - 距离衰减播放封装
 *
 * 单一职责: 选择音效资源 + 应用距离衰减, 调用 UGameplayStatics::PlaySoundAtLocation
 *
 * 大厂架构中的角色:
 *   - 职责单一: 只管"放音", 不管"网络同步" (那是 BaseCharacter::Multicast_PlayFootstep 的事)
 *   - 单一真理源: FootstepSound + FootstepAttenuation 都在 BP 配置, 不允许 CVar/C++ 兜底
 *   - 零兜底: 任一字段缺失 → Log Error + 拒绝播放, 强制修复 BP 配置
 *
 * 调用链:
 *   AnimNotify → BaseCharacter::PlayFootstepSound → Multicast_PlayFootstep RPC
 *   → FootstepComponent->PlayFootstep → UGameplayStatics::PlaySoundAtLocation
 *
 * v241 重构后:
 *   - 删除 Crouch/Run/Walk 三选一反模式分支
 *   - 删除 CVarFootstepVolume 兜底
 *   - 删除地面 LineTrace 物理材质查询 (没有业务用途)
 */
UCLASS(ClassGroup = (MetalSlug), meta = (BlueprintSpawnableComponent))
class METALSLUG01_API UFootstepComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UFootstepComponent();

	/**
	 * 播放一次脚步音效（带距离衰减）
	 *
	 * @param OwnerChar  Owner 角色引用（用于获取速度/下蹲状态 + 决定是否播放）
	 * @param Location   脚步播放位置
	 *
	 * 大厂原则:
	 *   - 必有 FootstepAttenuation (BP 配置) — 否则 Log Error + 不播放
	 *   - 必有 FootstepSound (BP 配置) — 否则 Log Error + 不播放
	 */
	UFUNCTION(BlueprintCallable, Category = "Footstep")
	void PlayFootstep(ACharacter* OwnerChar, const FVector& Location);

protected:
	/** 地面检测射线向下长度（cm），过大值会穿到楼层下方 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Footstep|Trace", meta = (ClampMin = "5.0", ClampMax = "100.0"))
	float TraceDistance = 10.0f;

	/** 地面检测射线起始偏移（向上抬以避免穿模） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Footstep|Trace", meta = (ClampMin = "0.0", ClampMax = "50.0"))
	float TraceStartOffset = 5.0f;

	/** 地面检测碰撞通道 — 必须用能返回物理材质的通道 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Footstep|Trace")
	TEnumAsByte<ECollisionChannel> FootstepTraceChannel = ECC_PhysicsBody;

public:
	/**
	 * 脚步声音距离衰减资产 (单一真理源)
	 * 【v241 大厂架构】脚步声整体音量曲线由此资产决定
	 *   - 必须在 BP 配置 (BP_BaseCharacter / BP_GruntAI / BP_MuTi)
	 *   - 缺失 → Log Error + 不播放 (零兜底)
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Footstep|Audio")
	TObjectPtr<USoundAttenuation> FootstepAttenuation = nullptr;

	/**
	 * 脚步声音资源 (单一真理源)
	 * 【v241 大厂架构】脚步声 = 脚步声音资源 (一套) × 距离衰减曲线
	 *   - 必须在 BP 配置 (BP_BaseCharacter / BP_GruntAI / BP_MuTi)
	 *   - 缺失 → Log Error + 不播放 (零兜底)
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Footstep|Audio")
	TObjectPtr<USoundBase> FootstepSound = nullptr;

	/**
	 * 全局音量缩放 (BP 配置, 不是运行时 CVar)
	 * 【v241 修复】替换原 CVarFootstepVolume 兜底
	 *   - 默认 1.0
	 *   - 调整不会改动 BP
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Footstep|Audio", meta = (ClampMin = "0.0", ClampMax = "2.0"))
	float FootstepVolumeMultiplier = 1.0f;
};
