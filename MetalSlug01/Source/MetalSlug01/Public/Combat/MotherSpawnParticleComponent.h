// ==========================================
// 母体出生粒子特效 Component【v99.2 大厂架构 — 零等待】(2026.07.26)
//
// @brief 母体 Pawn 生成 / 复活时,在身体 Mesh 上附着 Cascade 粒子 5 秒
//
// 【架构定位 — SRP 关注点分离】
//   - ABaseCharacter: 持有本组件,经 Resolver 暴露给其他调用方
//   - 本组件: 只管本地 Cascade 组件的创建 / 附着 / 5 秒后销毁
//   - ARoomSpawnSubsystem::MutatePawnToMother: 统一母体生成入口,负责调用 Multicast_PlayMutationFX
//   - ABaseCharacter::Multicast_PlayMutationFX_Implementation: 经 Resolver 调本组件,触发本地播放
//
// 【为什么走 Multicast_PlayMutationFX 而不新增 Client RPC】
//   - 现成 Actor 层 RPC,统一了"首次变异 + 母体复活"的视觉入口(本项目 v90 收敛结果)
//   - 不新建第二套 RPC 避免双发(老问题已修)
//   - 复用同一 NetMulticast,服务器本地与远端客户端都走同一路径,链路透明
//
// 【为什么不在 OnRep_bIsMother 中启动粒子】
//   - 旧方案同时调 OnRep 与 Multicast,会双发粒子 (5 秒效果跑两遍)
//   - 大厂原则 - 关注点分离: OnRep 只负责状态校验 + 状态事件广播,Multicast 负责视觉一次性触发
//
// 【为什么走 GetMesh() 附着而不是世界坐标 SpawnEmitter】
//   - 母体生成 / 复活后要随母体 Pawn 移动
//   - SpawnEmitterAttached 会跟随附着组件的 Transform(骨骼或 Mesh)
//   - 用 EAttachLocation::SnapToTarget 锁定到 Mesh 组件原点
//
// 【v99.2 大厂重构 — 零等待 / 删兜底】
//   - 旧版运行期模式校验 IsAllowedByCurrentMatchMode + ResolveGameState 是"防止上游错误"的双重校验
//   - 客户端 GameState.CurrentMatchMode 是 Replicated — 等同步 = 1 帧以上延时 = 出生瞬间看不到粒子
//   - 真理源已经是 Server 侧 URoomSpawnSubsystem::MutatePawnToMother 唯一入口(Mode = Zombie 才会进)
//   - 客户端校模式 = 兜底 = 反模式 — v99.2 删除
//   - 客户端 RPC 触发 = "Server 已决定母体存在" 信号,模式必定是 Biochemical(否则 Server 根本不会调到这个 RPC)
//
// 【零兜底 — 严禁】
//   - 禁止: 缺资产时用默认粒子
//   - 禁止: 缺 Mesh 时静默跳过
//   - 禁止: 缺 World 时容错播放
//   - 禁止: 客户端运行期再校验 GameMode (真理源在 Server, 校验过 = 永远过)
//   - 禁止: 5 秒计时器超时前再次调用时延长时长(幂等拒绝延长,避免"半路覆盖")
// ==========================================
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "MotherSpawnParticleComponent.generated.h"

class UParticleSystem;
class UParticleSystemComponent;
class USkeletalMeshComponent;
class ABaseCharacter;

/**
 * @class UMotherSpawnParticleComponent
 * @brief 母体出生粒子特效组件
 *
 * 工作流:
 *   1. BaseCharacter 构造函数中 CreateDefaultSubobject<UMotherSpawnParticleComponent>
 *   2. 在 BP_MuTi 上配 MotherSpawnParticleSystem 字段(必须 Cascade ParticleSystem,不允许空)
 *   3. 服务器 ABaseCharacter::Multicast_PlayMutationFX 触发 → 每台机器本地启动本组件
 *   4. 组件 SpawnEmitterAttached 到 Body Mesh → 5 秒后自动销毁粒子组件
 *
 * 大厂原则 — 单一真理源:
 *   - 组件不复制(视觉不需要同步): 服务器 / 客户端各自本地播放
 *   - 触发信号通过 ABaseCharacter::Multicast_PlayMutationFX 统一分发
 *   - 幂等保护: 重复 PlaySpawnParticle 调用不重复生成,不延长时长
 */
UCLASS(ClassGroup = (MetalSlug), meta = (BlueprintSpawnableComponent))
class METALSLUG01_API UMotherSpawnParticleComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UMotherSpawnParticleComponent();

	/**
	 * 在母体 Pawn 的 Body Mesh 上附着粒子特效,持续 5 秒后自动销毁
	 *
	 * 触发方: ABaseCharacter::Multicast_PlayMutationFX_Implementation(单一网络入口)
	 *
	 * 大厂原则 - 幂等:
	 *   - 已存在活动粒子 → 拒绝重复生成(不延长时长)
	 *   - 调用者重复触发是上游 bug,本函数仅负责一次正确的本地播放
	 *
	 * v99.2 零等待:
	 *   - 仅校验: Owner / World / ParticleSystem / Mesh / 幂等
	 *   - 不再运行期校验 GameState.CurrentMatchMode (真理源在 Server 侧 MutatePawnToMother)
	 *   - 模式校验在 Server 侧入口 URoomSpawnSubsystem::MutatePawnToMother 已做(玩家/AI 路径都有)
	 *   - 客户端 RPC 到达 = Server 已决定母体存在 = 模式必然对
	 *
	 * 零兜底:
	 *   - 缺资产 / 缺 Mesh / 缺 World 时 → Log Error + 直接拒绝(不抛异常)
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat|Mother")
	void PlaySpawnParticle();

	/**
	 * 立即停止并销毁活动粒子(若存在)
	 *
	 * 调用方:
	 *   - EndPlay: 防御性清理
	 *   - 上游决定"母体立即下线"时(本项目目前无该路径,留作防御 API)
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat|Mother")
	void StopSpawnParticle();

	// ==========================================
	// 配置项(必须由 BP_MuTi 配置,刀战 BP 不配)
	// ==========================================

	/**
	 * 母体出生粒子资产 — 必须配置
	 *
	 * 必须为 Cascade UParticleSystem*,不允许为空
	 * 资产本身不需要自带 Lifetime,5 秒由组件 Timer 统一控制
	 *
	 * 不允许:
	 *   - 在 ABaseCharacter / BP_SWAT / BP_GruntAI 上配置(普通角色收不到 RPC)
	 *   - 用空指针 / 默认粒子兜底
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mother|Protocol")
	TObjectPtr<UParticleSystem> MotherSpawnParticleSystem;

	/**
	 * 粒子存活时长(秒),默认 5 秒
	 *
	 * 大厂原则 - 配置驱动:
	 *   - 策划可在 BP 默认值调
	 *   - 组件内部 Timer 用此值,到期自动 Stop + Destroy 粒子组件
	 *   - 不依赖粒子资产自带的循环 / 寿命设置
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mother|Protocol", meta = (ClampMin = "0.1", ClampMax = "30.0"))
	float ParticleLifetimeSeconds = 5.0f;

	/**
	 * 粒子附着 Socket 名(可选)
	 *
	 * 默认 NAME_None 表示附着到 Mesh 组件原点
	 * 若 BP_MuTi 骨架上挂了专用 Socket(如 "MotherFX"),可填此名
	 *
	 * 大厂原则 - 零兜底:
	 *   - 填了 Socket 但骨架上不存在 → Log Error,改用 Mesh 原点(显式失败,不让视觉错位)
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Mother|Protocol")
	FName AttachSocketName = NAME_None;

protected:
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

private:
	/**
	 * Timer 到期回调 — 5 秒后停止并销毁活动粒子组件
	 *
	 * 必须经 TimerHandle 调度,不允许在 PlaySpawnParticle 同步链中直接等待
	 */
	UFUNCTION()
	void HandleLifetimeExpired();

	/**
	 * Lazy resolve Owner — 每次按需查询,避开 BeginPlay 缓存被 BP archetype 覆写的陷阱
	 *
	 * 返回 nullptr 时已 Log Error,调用方直接 return
	 */
	ABaseCharacter* ResolveOwnerCharacter() const;

	/**
	 * 当前正在播放的粒子组件 — UPROPERTY 防 GC
	 *
	 * 唯一真相源: 本组件持有一个活动的 UParticleSystemComponent
	 * 同一时间最多一个(5 秒生命周期内不允许多个并存)
	 */
	UPROPERTY(Transient)
	TObjectPtr<UParticleSystemComponent> ActiveParticleComponent;

	/**
	 * 5 秒生命周期 Timer 句柄 — 独立存储用于清理
	 */
	FTimerHandle LifetimeTimerHandle;

	/**
	 * 幂等守卫 — 同一时间最多一个粒子活动
	 *
	 * true = 已有活动粒子,拒绝再次播放
	 */
	bool bIsParticleActive = false;
};
