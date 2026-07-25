// ==========================================
// 母体出生音效 Component【v99.3 大厂架构 — 零等待 / 零兜底】(2026.07.26)
//
// @brief 母体 Pawn 生成 / 复活时,在母体位置播放一次音效
//
// 【架构定位 — SRP 关注点分离】
//   - ABaseCharacter: 持有本组件,经 Resolver 暴露给其他调用方
//   - 本组件: 只管本地音效播放 — 一次 UGameplayStatics::PlaySoundAtLocation
//   - ARoomSpawnSubsystem::MutatePawnToMother: 统一母体生成入口,负责调用 Multicast_PlayMutationFX
//   - ABaseCharacter::Multicast_PlayMutationFX_Implementation: 经 Resolver 同时触发粒子组件 + 音效组件
//
// 【为什么单独成组件 — 与 MotherSpawnParticleComponent 对称】
//   - 粒子 vs 音效 = 两种不同表现层(视觉 / 听觉), 各自独立职责
//   - 单独组件 = 单独配置项 (BP_MuTi 上 MotherSpawnParticleSystem 字段 vs MotherSpawnSound 字段)
//   - 单独组件 = 单独未来扩展 (粒子能挂骨骼, 音效能改 3D 衰减)
//   - 共享同一条 RPC 触发 = 避免重复 RPC (项目大厂原则 — 单一入口)
//
// 【为什么不在 OnRep_bIsMother 中启动音效】
//   - 旧方案同时调 OnRep 与 Multicast,会双发 (5 秒效果跑两遍教训)
//   - 大厂原则 - 关注点分离: OnRep 只负责状态校验 + 状态事件广播,Multicast 负责"一次性"触发
//
// 【为什么不用 PlaySound2D】
//   - 母体音效应该带 3D 位置 (玩家能听到母体从远处走来的方向感)
//   - PlaySoundAtLocation 自动 3D 衰减,与粒子"看母体在哪"完全对称
//
// 【v99.3 零等待 — 与 v99.2 粒子组件同款架构】
//   - 旧版(假设)运行期模式校验(比如 IsAllowedByCurrentMatchMode)= 兜底 = 重复架构
//   - 真理源已经在 Server 侧 URoomSpawnSubsystem::MutatePawnToMother(只走 Zombie 路径)
//   - 客户端 RPC 触发 = Server 已决定母体存在 = 模式必然对 ⇒ 不再运行期校验
//   - 同步链少一跳 = 0 等待
//
// 【零兜底 — 严禁】
//   - 禁止: 缺资产时用默认音效
//   - 禁止: 缺 World 时容错播放
//   - 禁止: 重复触发时延长时长 (音效是一次性,无时长概念;重复触发 = 重复响)
//
// 【不破坏刀战模式】
//   - 刀战 BP 不配 MotherSpawnSound 字段 → 收到 RPC 时组件内部拒绝
//   - 刀战模式根本不调 Multicast_PlayMutationFX → 组件压根不会跑
//   - 双重保险: 即便 BP 误配,Multicast 也不会发
// ==========================================
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MotherSpawnSoundComponent.generated.h"

class USoundBase;
class ABaseCharacter;

/**
 * @class UMotherSpawnSoundComponent
 * @brief 母体出生音效组件 — 一次本地播放
 *
 * 工作流:
 *   1. BaseCharacter 构造函数中 CreateDefaultSubobject<UMotherSpawnSoundComponent>
 *   2. 在 BP_MuTi 上配 MotherSpawnSound 字段(必须 USoundBase)
 *   3. 服务器 ABaseCharacter::Multicast_PlayMutationFX 触发 → 每台机器本地播一次
 *   4. 组件 UGameplayStatics::PlaySoundAtLocation(无 Multiplayer,本地播放)
 *
 * 大厂原则 — 单一真理源:
 *   - 组件不复制(音效本地播放,服务器 / 客户端各自播放)
 *   - 触发信号通过 ABaseCharacter::Multicast_PlayMutationFX 统一分发
 *   - 重复 RPC = 重复播放 = 静态（音效一次性,组件不维护状态,不延长）
 */
UCLASS(ClassGroup = (MetalSlug), meta = (BlueprintSpawnableComponent))
class METALSLUG01_API UMotherSpawnSoundComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMotherSpawnSoundComponent();

	/**
	 * 在母体 Pawn 当前位置播放一次音效
	 *
	 * 触发方: ABaseCharacter::Multicast_PlayMutationFX_Implementation(单一网络入口)
	 *
	 * 大厂原则 — 零等待:
	 *   - 仅校验: Owner / World / 资产
	 *   - 不再运行期校验 GameState.CurrentMatchMode (真理源在 Server,校验过 = 永远过)
	 *
	 * 零兜底:
	 *   - 缺资产 / 缺 World / 缺 Owner → Log Error + 直接拒绝(不抛异常)
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat|Mother")
	void PlaySpawnSound();

	// ==========================================
	// 配置项(必须由 BP_MuTi 配置,刀战 BP 不配)
	// ==========================================

	/**
	 * 母体出生音效资产 — 必须配置
	 *
	 * 必须为 USoundBase* (兼容 SoundCue / SoundWave / MetaSoundSource),不允许为空
	 * PlaySoundAtLocation 自带 3D 衰减,本组件不再做空间化
	 *
	 * 不允许:
	 *   - 在 ABaseCharacter / BP_SWAT / BP_GruntAI 上配置(普通角色收不到 RPC)
	 *   - 用空指针 / 默认音效兜底
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mother|Protocol")
	TObjectPtr<USoundBase> MotherSpawnSound;

	/**
	 * 音量倍数,默认 1.0
	 *
	 * 大厂原则 - 配置驱动:
	 *   - 策划可在 BP 默认值调
	 *   - 不允许 C++ 硬编码 VolumeMultiplier
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mother|Protocol", meta = (ClampMin = "0.0", ClampMax = "4.0"))
	float VolumeMultiplier = 1.0f;

	/**
	 * 音高倍数,默认 1.0
	 *
	 * 大厂原则 - 配置驱动:
	 *   - 策划可在 BP 默认值调
	 *   - 不允许 C++ 硬编码 PitchMultiplier
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mother|Protocol", meta = (ClampMin = "0.1", ClampMax = "4.0"))
	float PitchMultiplier = 1.0f;

private:
	/**
	 * Lazy resolve Owner — 每次按需查询,避开 BeginPlay 缓存被 BP archetype 覆写的陷阱
	 *
	 * 大厂原则(同 v40.6 / v40.7 / v99.2 MotherSpawnParticleComponent):
	 *   - 不缓存 raw pointer: BP archetype 会覆写 C++ 默认字段
	 *   - 每次 GetOwner() + Cast<T>: 真理源 = UE 标准 API
	 *   - Cast 失败 / GetOwner 无效 → Log Error + return nullptr
	 */
	ABaseCharacter* ResolveOwnerCharacter() const;
};
