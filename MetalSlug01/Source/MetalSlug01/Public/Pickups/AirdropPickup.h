// ==========================================
// 【v117 大厂架构新增】AAirdropPickup
//
// ==========================================
// 【生化模式】空投 Actor — 静态网格体 + 单一自治 RPC 链路
// ==========================================
//
// 业务规则 (用户 2026.08.03 明确):
//   - 空投是静态网格体 (StaticMesh), 由策划在 BP_AirdropPickup 蓝图挂 StaticMesh 资产
//   - 唯一作用: 被人类吃掉时, 主武器所有弹药恢复全满
//   - 母体无效 — 走过去无视 (业务层判定, 不污染 CollisionProfile)
//   - 被吃掉后立即 Destroy() + 通知 Subsystem 从账本移除
//
// 大厂原则 — 职责单一 (与 v24 WeaponDissolveComponent 同源):
//   - 空投自己拥有 StaticMesh + SphereComponent + 碰撞响应
//   - 业务判定 (人类/母体/死亡) 在 C++ 内, 不依赖蓝图
//   - 弹药恢复走 WeaponFireComponent (单一真理源)
//   - 账本自维护: 销毁时回调 Subsystem 移除账本
//
// 大厂原则 — 零兜底:
//   - Cast<ABaseCharacter> 失败 → Log Verbose + 跳过 (非 Pawn 不响应, 是正常过滤)
//   - 母体 (bIsMother=true) → Log Verbose + 跳过 (业务规则, 不算 bug)
//   - 死亡人类 → Log Verbose + 跳过 (死人不能吃)
//   - 没人拿主武器 → Log Error + return (配置错必须显式)
//   - 主武器没 WeaponFireComponent → Log Error + return (配置错必须显式)
//
// 大厂原则 — 不污染 CollisionProfile:
//   - 用户提"母体穿透"在 C++ 业务层判定, 不修改项目 CollisionSettings
//   - Sphere 通道: 对 Pawn = Overlap (让 C++ Handle_Overlap 跑), 其他通道 = Ignore
//   - StaticMesh (默认 BlockAll) 对地面 = Block, 对 Pawn = 默认 (UE 自动让 Capsule 阻挡 — 我们用 Sphere 接管 Pawn 响应, 不会冲突)
//
// 网络同步:
//   - bReplicates = true (默认) — 客户端必须看到空投外观
//   - 业务判定只在服务器 (HasAuthority 守卫)
//   - 不需要 Multicast_PlayPickupFX — 用户没要求视觉同步, 蓝图按需在 BP_Overlap 事件自定义
//
// 调用链 (服务器):
//   AAirdropPickup::Handle_OverlapBegin → 校验人类/非死亡/有武器 →
//     WeaponFireComponent::Server_RefillAmmo() → Destroy() →
//     URoomAirdropSubsystem::NotifyPickupDestroyed() → 账本移除
//
// ==========================================

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"

// 大厂注意 (UE 5.6 UHT 严格模式): .generated.h 必须用裸文件名
#include "AirdropPickup.generated.h"

// Engine forward
class UStaticMeshComponent;
class USphereComponent;
class URoomAirdropSubsystem;
class ABaseCharacter;
class ABaseWeapon;
class UWeaponFireComponent;
class USoundBase;

/**
 * @class AAirdropPickup
 * @brief 生化模式空投 Pickup — 服务器权威, 人类吃 = 主武器弹药全满
 *
 * 设计:
 *   - StaticMeshComponent + SphereComponent (BP 子类可挂, 也可 C++ 默认)
 *   - bReplicates=true, 但关键字段都不复制 (外观由 StaticMesh 资源同步)
 *   - OwningSubsystem 是反向引用 (由 Subsystem 在 SpawnActor 后注入)
 */
UCLASS(Blueprintable, ClassGroup = (MetalSlug))
class METALSLUG01_API AAirdropPickup : public AActor
{
	GENERATED_BODY()

public:
	AAirdropPickup();

	// UE 生命周期
	virtual void BeginPlay() override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

	/**
	 * 【v117 反向引用】空投所属的 AirdropSubsystem (服务器 SpawnActor 后注入)
	 *
	 * 大厂原则 — 反向依赖 (与 v40.6 ResolveOwner 同源):
	 *   - 不在 BeginPlay 内 GetSubsystem (时序不可靠, 与 World Subsystem 创建时机有关)
	 *   - Subsystem 在 SpawnActor 成功后立即写入此字段
	 *   - 客户端永远为 nullptr (Subsystem 仅服务器存在)
	 *   - 不复制 (UPROPERTY 不加 Replicated, 客户端不需要账本)
	 */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Airdrop")
	TObjectPtr<URoomAirdropSubsystem> OwningSubsystem;

	// ==========================================
	// 音效 — 空投生成时播放 (大厂模式, 镜像 BaseCharacter::Multicast_PlayEquipSoundForSlot)
	// 必须 public, 否则外部 Subsystem 调不到 Multicast_PlayDropSound
	// ==========================================

	/**
	 * 【v118 大厂架构新增】空投生成音效 (策划在 BP_AirdropPickup 蓝图挂 Sound 资产)
	 *
	 * 大厂原则 — 配置零代码:
	 *   - Sound 资产由策划在 BP_AirdropPickup → Class Defaults → Airdrop → Drop Sound 字段挂入
	 *   - 不在 C++ 硬编码 USoundBase 引用 (违反 DRY)
	 *
	 * 大厂原则 — 服务器权威 + Multicast 推送:
	 *   - 服务器 SpawnActor 成功 → 立即调 Multicast_PlayDropSound
	 *   - 所有客户端 (包括 ListenServer) 调 PlaySoundAtLocation
	 *   - 音频只在播放方进程触发 (服务器不播, 客户端各自播 — UE 标准模式)
	 *
	 * 大厂原则 — USoundBase* 跨 RPC 边界:
	 *   - UE 5.6 自动按 Soft Reference 解析 (客户端 BeginPlay 时按需加载)
	 *   - Sound 字段为空 → Multicast_PlayDropSound 立即返回 (服务器已校验过)
	 *   - 客户端 InSound 为 null → Log Error + return (强制修复 BP)
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Airdrop|Sound")
	TObjectPtr<USoundBase> DropSound;

	/**
	 * 【v118 大厂架构新增】空投音效音量倍率 (策划可调)
	 *
	 * 大厂原则 — 业务可调:
	 *   - 默认 1.0 (原音量)
	 *   - BP 子类可覆盖
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Airdrop|Sound", meta = (ClampMin = "0.0", ClampMax = "5.0"))
	float DropSoundVolumeMultiplier = 1.0f;

	/**
	 * 【v118 大厂架构新增】空投音效音高倍率 (策划可调)
	 *
	 * 大厂原则 — 业务可调:
	 *   - 默认 1.0 (原音高)
	 *   - BP 子类可覆盖
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Airdrop|Sound", meta = (ClampMin = "0.1", ClampMax = "3.0"))
	float DropSoundPitchMultiplier = 1.0f;

	/**
	 * 【v118 大厂架构新增】Multicast RPC — 服务器权威, 所有客户端播放空投生成音效
	 *
	 * 大厂原则 — RPC 单一入口 (镜像 BaseCharacter::Multicast_PlayEquipSoundForSlot):
	 *   - 服务器 SpawnActor 成功后立即调
	 *   - Implementation 内调 UGameplayStatics::PlaySoundAtLocation
	 *   - 不在服务器播 (服务器本身是 ListenServer 时已通过 Implementation 触发本地播放 — UE 标准)
	 *
	 * 大厂原则 — 零兜底:
	 *   - InSound 字段为空 → Log Error + return (强制修复 BP)
	 *   - World 无效 → Log Error + return
	 */
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_PlayDropSound(USoundBase* InSound, float VolumeMul, float PitchMul);

protected:
	// ==========================================
	// 组件 — 由 BP_AirdropPickup 蓝图添加, 这里只声明运行时查找
	// ==========================================

	/**
	 * 静态网格体组件 — 空投外观 (由 BP 子类挂 StaticMesh 资产)
	 *
	 * 大厂原则 — 不在 C++ 强制 CreateDefaultSubobject:
	 *   - 不同空投造型不同, 让策划在 BP 自由选
	 *   - 运行时用 FindComponentByClass 按需查找 (v40.6 不缓存原则)
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UStaticMeshComponent> StaticMeshComponent;

	/**
	 * 球形触发器 — 检测人类/母体 Overlap
	 *
	 * 默认半径 80cm (1 个 Capsule 距离), 蓝图可调
	 *
	 * 大厂原则 — 碰撞配置:
	 *   - BlockAllDynamic (默认) — 对地面 = Block (符合用户"对地面阻挡"需求)
	 *   - 对 Pawn = Overlap (让 C++ Handle_Overlap 跑判定)
	 *   - 其他通道 = Ignore
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<USphereComponent> SphereComponent;

	// ==========================================
	// 内部方法
	// ==========================================

	/**
	 * 【v117 业务核心】SphereComponent Overlap 回调 (服务器权威)
	 *
	 * 大厂原则 — 业务判定链 (零兜底, 显式失败):
	 *   1. HasAuthority() 必须为 true (防御, UE 默认只在服务器 fire)
	 *   2. Cast<ABaseCharacter> → nullptr: 非 Pawn (子弹 / 道具) → Verbose 跳过 (正常过滤)
	 *   3. bIsMother=true: 母体无视 → Verbose 跳过 (业务规则)
	 *   4. IsDead(): 死亡人类 → Verbose 跳过 (死人不能吃)
	 *   5. ResolveCurrentWeapon(): 没主武器 → Error + return (配置错)
	 *   6. Weapon->GetWeaponFireComponent(): 没 FireComp → Error + return (配置错)
	 *   7. FireComp->Server_RefillAmmo(): 弹药全满 (大厂原则, 真理源唯一)
	 *   8. Destroy() + Subsystem->NotifyPickupDestroyed()
	 *
	 * @param OverlappedComponent 触发源 (本 Actor 的 SphereComponent)
	 * @param OtherActor          进入的 Actor (期望是 ABaseCharacter)
	 * @param OtherComp           进入的 Component (期望是 CapsuleComponent)
	 */
	UFUNCTION()
	void Handle_OverlapBegin(UPrimitiveComponent* OverlappedComponent, AActor* OtherActor,
		UPrimitiveComponent* OtherComp, int32 OtherBodyIndex,
		bool bFromSweep, const FHitResult& SweepResult);

	/**
	 * 【v117 工具方法】解析触碰者拿的武器 (按需 lazy resolve, 镜像 v40.6)
	 *
	 * 大厂原则 — 不缓存:
	 *   - Character->GetCurrentWeapon() 永远按需查询
	 *   - Character 可能在中途切换武器, 缓存会失效
	 *   - 调一次约 0.001ms, 完全可接受
	 *
	 * @param OtherChar 触碰者 (已 Cast 为 ABaseCharacter, 非 nullptr)
	 * @return 武器指针 (无主武器时 nullptr)
	 */
	ABaseWeapon* ResolveTouchingWeapon(ABaseCharacter* OtherChar) const;
};