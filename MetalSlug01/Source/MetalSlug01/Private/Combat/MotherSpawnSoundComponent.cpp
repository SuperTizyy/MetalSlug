// ==========================================
// 母体出生音效 Component 实现【v99.3 大厂架构 — 零等待 / 零兜底】(2026.07.26)
//
// 大厂架构 — 与 UMotherSpawnParticleComponent / UInvincibilityFlickerComponent 同款模式(本地播放,不复制):
//   - 组件不复制(音效本地播放,服务器 / 客户端各自独立)
//   - 服务器 / 客户端各自本地订阅 Multicast_PlayMutationFX → 各自播放一次
//   - 双发保证: 服务器本地 RPC 实现也跑一次,客户端 RPC 到达后再跑一次
//
// 【v99.3 零等待重大重构】
//   - 不运行期校验 GameState.CurrentMatchMode (真理源在 Server 侧入口 MutatePawnToMother)
//   - 旧版误用 IsAllowedByCurrentMatchMode 模式 = 兜底 = 反模式
//   - 与 v99.2 粒子组件完全对称架构
//
// 【音效 3D 衰减】
//   - UGameplayStatics::PlaySoundAtLocation 自带 3D 衰减
//   - 玩家能在远处听到母体出生方向感
//   - 与粒子"看母体在哪"完全对称
//
// 大厂原则 — 零兜底:
//   - 缺资产 / World / Owner → Log Error + 拒绝播放
//   - 不在 Owner 位置取不到时 fallback 到世界原点 (必须由调用方保证 Owner 位置有效)
// ==========================================
#include "Combat/MotherSpawnSoundComponent.h"

// UE 引擎 API
#include "Engine/World.h"               // GetWorld
#include "Kismet/GameplayStatics.h"     // PlaySoundAtLocation
#include "Sound/SoundBase.h"            // USoundBase (RPC 序列化需要完整类型)

// 项目内
#include "Characters/BaseCharacter.h"   // Owner + GetActorLocation


// ==========================================
// 1. 构造函数
// ==========================================
/**
 * UMotherSpawnSoundComponent 构造函数
 *
 * 大厂原则 - 零 Tick:
 *   - 音效一次性播放,不依赖 Component Tick
 *   - 没有生命周期 Timer (与粒子组件不同 — 粒子附着 5 秒,音效只是一瞬间)
 */
UMotherSpawnSoundComponent::UMotherSpawnSoundComponent()
{
	PrimaryComponentTick.bCanEverTick = false;
	PrimaryComponentTick.bStartWithTickEnabled = false;

	// 不需要 SetIsReplicatedByDefault:
	//   - 音效本地播放,服务器 / 客户端各自独立
	//   - 双发保证靠 Multicast RPC 跨网络同步(组件不参与复制)
}


// ==========================================
// 2. 公共 API — 播放
// ==========================================
/**
 * PlaySpawnSound — 在母体 Pawn 当前位置播放一次音效
 *
 * 触发方: ABaseCharacter::Multicast_PlayMutationFX_Implementation(单一入口)
 *
 * 【v99.3 零等待重构】校验链精简:
 *   1. Owner(ABaseCharacter)有效
 *   2. World 有效
 *   3. MotherSpawnSound 资产已配置
 *
 * v99.3 删除的校验:
 *   - IsAllowedByCurrentMatchMode: 真理源在 Server 侧 MutatePawnToMother,客户端重复校验 = 兜底
 *
 * 全部通过 → UGameplayStatics::PlaySoundAtLocation(3D 衰减,本地播放)
 * 任一失败 → Log Error + 拒绝播放(不抛异常,不影响 Pawn)
 */
void UMotherSpawnSoundComponent::PlaySpawnSound()
{
	// ===== 校验层 1: Owner =====
	ABaseCharacter* OwnerChar = ResolveOwnerCharacter();
	if (!OwnerChar)
	{
		// ResolveOwnerCharacter 内部已 Log Error 解释根因
		return;
	}

	// ===== 校验层 2: World =====
	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[MotherSpawnSound] PlaySpawnSound: World 无效. Owner=%s. 拒绝播放音效."),
			*OwnerChar->GetName());
		return;
	}

	// ===== 校验层 3: 音效资产 =====
	if (!MotherSpawnSound)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[MotherSpawnSound] PlaySpawnSound: MotherSpawnSound 未配置. "
				 "Owner=%s. "
				 "【修复】在 BP_MuTi 的 MotherSpawnSound 组件 Class Defaults → MotherSpawnSound 字段配 USoundBase (SoundCue/SoundWave/MetaSoundSource). "
				 "刀战 BP 不需要配置 — 不会收到该 RPC."),
			*OwnerChar->GetName());
		return;
	}

	// ===== 执行层: 3D 衰减播放 =====
	//
	// 大厂原则 - 3D 位置而非 2D:
	//   - 母体音效应该带空间感 (玩家能听到母体从远处走来)
	//   - PlaySoundAtLocation 自动 3D 衰减
	//   - 与粒子"看母体在哪"完全对称
	//
	// 大厂原则 - 显式优于隐式:
	//   - Owner->GetActorLocation() 一次性取坐标
	//   - 不缓存 Location (与 RPC 同步链独立 — 客户端取本地坐标,服务器取服务器坐标)
	//   - VolumeMultiplier / PitchMultiplier 来自 BP 配置,不在 C++ 硬编码
	UGameplayStatics::PlaySoundAtLocation(
		World,
		MotherSpawnSound,
		OwnerChar->GetActorLocation(),
		VolumeMultiplier,
		PitchMultiplier);

	UE_LOG(LogTemp, Display,
		TEXT("[MotherSpawnSound] PlaySpawnSound: 音效已本地播放. Owner=%s, Sound=%s, Vol=%.2f, Pitch=%.2f."),
		*OwnerChar->GetName(),
		*GetNameSafe(MotherSpawnSound),
		VolumeMultiplier,
		PitchMultiplier);
}


// ==========================================
// 3. 私有辅助 — Lazy Resolve Owner
// ==========================================
/**
 * ResolveOwnerCharacter — 按需 lazy 解析 Owner,避开 BeginPlay 缓存陷阱
 *
 * 大厂原则(同 v40.6 / v40.7 AIAttackComponent / PlayerComboComponent / v99.2 MotherSpawnParticleComponent):
 *   - 不缓存 raw pointer: BP archetype 会覆写 C++ 默认字段
 *   - 每次 GetOwner() + Cast<T>: 真理源 = UE 标准 API
 *   - Cast 失败 / GetOwner 无效 → Log Error + return nullptr
 */
ABaseCharacter* UMotherSpawnSoundComponent::ResolveOwnerCharacter() const
{
	const AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[MotherSpawnSound] ResolveOwnerCharacter: GetOwner() 返回 null. "
				 "Component 必须挂在 Actor 上."));
		return nullptr;
	}

	const ABaseCharacter* OwnerChar = Cast<ABaseCharacter>(OwnerActor);
	if (!OwnerChar)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[MotherSpawnSound] ResolveOwnerCharacter: Owner '%s' 不是 ABaseCharacter 派生. "
				 "本组件必须挂在 BP_*.uasset (继承 ABaseCharacter) 上. "
				 "实际 OwnerClass=%s."),
			*OwnerActor->GetName(),
			*OwnerActor->GetClass()->GetName());
		return nullptr;
	}

	// Cast 在 const 上下文返回 const,工程上不修改字段,这里做一次解包
	return const_cast<ABaseCharacter*>(OwnerChar);
}
