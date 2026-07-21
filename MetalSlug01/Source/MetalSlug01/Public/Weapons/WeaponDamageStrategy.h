// ==========================================
// 武器伤害检测策略接口 (Strategy 模式 — 大厂原则 v51)
//
// 【设计动机】
//   旧 BaseWeapon.cpp Tick() 写死 BoxTrace → 枪械需要 LineTrace 时无法换
//   不同武器类型 (Melee / Primary / Secondary) 的检测算法不同:
//     - Melee (近战): BoxTrace 缝合上一帧/当前帧 (TraceStart/TraceEnd Socket)
//     - Ranged (枪械): LineTrace 从 Muzzle Socket 射出, 含 DropOff 衰减 + 后坐力
//
// 【大厂原则 — 开闭原则 + 依赖倒置】
//   - BaseWeapon 不依赖任何具体 Strategy
//   - 通过 UInterface (TScriptInterface) 抽象, 运行时按 MeshType 注入
//   - 加新武器类型 = 加新 Strategy (不改 BaseWeapon / 不改现有 Strategy)
//
// 【调用方 (唯一入口)】
//   ABaseWeapon::PerformDamageTrace(bIsHeavy)         ← 启动检测
//   ABaseWeapon::Tick()                                ← 每帧调 Strategy.TickDetection (仅当需要持续)
//   ABaseWeapon::StopDamageTrace()                     ← 停止检测
//   ABaseWeapon::SetDamageStrategy()                   ← 运行时注入
//
// 【v60.3 接口语义修订 — 一次调用 = 一次检测, 不再延后到 Tick】
//   - 旧 (v51-v60.2) 反模式: StartTrace 只设 bIsActive=true, 等下一帧 TickDetection 才真正打
//     → 半自动按下 = 1 帧延迟, 全自动 StopTrace 时序错位
//   - 新 (v60.3): StartTrace 立即执行一次检测
//     → Ranged (枪械): StartTrace = 立即打一发射线 + 不需要持续 Tick
//     → Melee (近战): StartTrace = 初始化跨帧状态 + 标记需要 Tick 缝合
//   - 调用方如果不想持续 Tick, 不用调 StopTrace; 持续 Tick 模式 (近战挥刀中段) 才调 StopTrace
//
// 【零兜底】
//   - Strategy 未注入 → 拒绝检测 + Log Error (强制注入)
//   - WeaponMesh 失效 → 拒绝检测 + Log Error (强制修复)
//   - 武器类型与 Strategy 不匹配 → Server_ReportHit 拒绝扣血 + Log Error
//
// 【生命周期】
//   - Strategy 通常为 UObject (TStrategyImpl), 由 BaseWeapon 在 Spawn 后由外部注入
//   - 也支持 Native (非 UObject) 模式 — 接口不强求 UObject
// ==========================================
#pragma once

#include "CoreMinimal.h"
#include "UObject/Interface.h"
#include "WeaponDamageStrategy.generated.h"

class ABaseWeapon;

/**
 * EWeaponTraceState — 武器检测状态标签 (v74 大厂架构统一)
 *
 * 【设计动机 — 用户需求 2026.07.22】
 *   美术想在 UE 编辑器蒙太奇里"拖时间轴"打检测状态标签 (Idle/Tracing/Hit),
 *   让动画驱动检测生命周期. 旧版依赖 BP ANS_MeleeTrace 一刀切调用 StartTrace/StopTrace,
 *   策划/美术无法按"挥刀中段/收刀"分段控制.
 *
 * 【v74 统一状态标签 — 大厂原则】
 *   - 单一真理源: ANS_MeleeTraceState (UAnimNotifyState 子类) 在蒙太奇时间轴上设置标签
 *   - C++ 端: AnimNotifyState::NotifyBegin/NotifyEnd 把标签写入 Strategy 内部字段
 *   - 各 Strategy (Melee/Ranged) 读统一字段, 不再有散落的 bIsActive
 *
 * 状态机 (蒙太奇时间轴驱动):
 *   Idle (蒙太奇起播) → AnimNotifyState::NotifyBegin(Tracing) → 检测中
 *   Tracing → AnimNotifyState::NotifyEnd(Idle) → 检测关闭
 *   Hit 是 TickDetection 命中时设置, 立即回 Tracing
 */
UENUM(BlueprintType)
enum class EWeaponTraceState : uint8
{
	/** 未激活 — 默认状态 */
	Idle UMETA(DisplayName = "Idle"),
	/** 检测中 — AnimNotifyState::NotifyBegin 设置, NotifyEnd 清除 */
	Tracing UMETA(DisplayName = "Tracing"),
	/** 本帧命中 — TickDetection 命中时设置, 立即回 Tracing */
	Hit UMETA(DisplayName = "Hit")
};

/**
 * 武器检测状态变化委托 (v74)
 *
 * @param State      新状态
 * @param HitActor   命中目标 (State=Hit 时有效, 其他为空)
 * @param HitLoc     命中世界坐标 (State=Hit 时有效)
 * @param HitBone    命中骨骼名 (State=Hit 时有效, Headshot 判定关键)
 */
DECLARE_DYNAMIC_MULTICAST_DELEGATE_FourParams(
	FOnWeaponTraceStateChanged,
	EWeaponTraceState, State,
	AActor*, HitActor,
	FVector, HitLoc,
	FName, HitBone
);

UINTERFACE(MinimalAPI, BlueprintType, meta = (CannotImplementInterfaceInBlueprint))
class UWeaponDamageStrategy : public UInterface
{
	GENERATED_BODY()
};

/**
 * 武器伤害检测策略接口
 *
 * 实现者:
 *   - UMeleeSwStrategy (近战 BoxTrace, 需要跨帧 Tick 缝合)
 *   - URangedLineStrategy (枪械 LineTrace, 单次独立, 无 Tick 需求)
 *   - 未来可扩展: URangedProjectileStrategy (投掷物), UMeleeThrowStrategy 等
 */
class METALSLUG01_API IWeaponDamageStrategy
{
	GENERATED_BODY()

public:
	// ============================================================
	// 【v60.10 单一真理源】Debug Trace 可视化时长
	//   - 旧 (v1-v60.9): 每个 Strategy cpp 各自写默认 5 秒 (EDrawDebugTrace::ForDuration 默认值)
	//   - 新 (v60.10): 统一 5 分钟 (300 秒) — 玩家调试/查错需要足够时间看 Trace
	//   - 大厂原则: 改时长只改这一个常量, 所有 Trace 自动生效
	//   - 不允许在 Strategy cpp 写死时长 (避免重复架构)
	// ============================================================
	static constexpr float kDebugTraceLifeTimeSeconds = 300.0f; // 5 分钟
	/**
	 * 启动一次伤害检测 (v60.3 语义修订 — 立即执行)
	 *
	 * 调用方:
	 *   - ABaseWeapon::PerformDamageTrace (BP AnimNotify / 玩家 Input / AI BT)
	 *   - UWeaponFireComponent::PerformSingleShot (枪械节奏调用, 每次开火 = 一次)
	 *
	 * 【新语义 — 大厂原则: 一次调用 = 一次检测】
	 *   - Ranged (枪械): 立即执行单次 LineTrace, 不依赖 Tick
	 *   - Melee (近战): 初始化跨帧状态 + 启动 TickDetection 做 BoxTrace 缝合
	 *     * 调用方需要后续 StopTrace 关闭 (否则 Tick 会一直做事)
	 *
	 * 【v82 大厂架构修复 — 客户端射线参数】:
	 *   - 旧 (v70-v81) 反模式: Ranged 内部用 PC->GetViewportSize + DeprojectScreenPositionToWorld
	 *     → 服务器进程对远端玩家 PC 调用 GetViewportSize 返回 0,0 → Deproject 失败 → return false
	 *     → 远端玩家在服务器上 trace 永远失败 → 玩家开火"无射线" (用户报告)
	 *   - 新 (v82): 客户端玩家路径在 OnFirePressed 用 HUD Crosshair 算出射线 → RPC 传给服务器
	 *     → 服务器用客户端传来的射线做权威 trace (防作弊: 限制射线长度/方向)
	 *   - AI 路径: 传零向量 → Strategy 内部 fallback 用 BaseAimRotation (BT 控制的旋转)
	 *   - 兼容旧调用: 不传参数 = AI 路径, 向前兼容
	 *
	 * @param Weapon     武器 Actor 指针 (实现可读 MeshType / Mesh / Owner)
	 * @param bIsHeavy   是否重击 (近战区分轻击/重击伤害, 枪械区分半自动/全自动)
	 * @param ClientRayOrigin     客户端射线起点 (FVector::ZeroVector = AI 路径, Strategy 内部 fallback)
	 * @param ClientRayDirection  客户端射线方向 (FVector::ForwardVector = AI 路径)
	 *
	 * @return 是否成功启动 (false = 配置错/状态错, 调用方应停止后续逻辑)
	 */
	virtual bool StartTrace(ABaseWeapon* Weapon, bool bIsHeavy,
		const FVector& ClientRayOrigin = FVector::ZeroVector,
		const FVector& ClientRayDirection = FVector::ForwardVector) = 0;

	/**
	 * 停止伤害检测
	 *
	 * 调用方:
	 *   - ABaseWeapon::StopDamageTrace (玩家收刀 / AI 蒙太奇结束)
	 *   - ABaseWeapon::OnWeaponChanged (玩家切换武器槽位 → 强制停止当前检测)
	 *   - ABaseWeapon::EndPlay (销毁兜底, 防状态泄漏)
	 *
	 * 零兜底:
	 *   - 幂等: 多次调用 no-op
	 *   - Melee 必须调 (否则 Tick 持续做事); Ranged 可不调 (本就不需要 Tick)
	 */
	virtual void StopTrace(ABaseWeapon* Weapon) = 0;

	/**
	 * 每帧 Tick 检测
	 *
	 * 调用方: ABaseWeapon::Tick (仅当 bIsWeaponActive=true 时调, 由 Strategy 内部判断)
	 *
	 * 大厂原则: Strategy 内部应自带"激活态"判断, 若未激活则 no-op
	 *           Tick 内不读 Strategy 状态 = 强制每帧做事 (性能浪费)
	 *
	 * @note 仅 Melee 缝合算法需要; Ranged 永远在 StartTrace 内做完, Tick no-op
	 */
	virtual void TickDetection(ABaseWeapon* Weapon, float DeltaTime) = 0;

	/**
	 * 查询 Strategy 当前是否激活 (v60.3 新增 — 避免 BaseWeapon 与 Strategy 双状态)
	 *
	 * 真理源: Strategy 内部 bIsActive
	 *
	 * 调用方: ABaseWeapon::Tick 决策是否委托 TickDetection (消除 BaseWeapon::bIsWeaponActive 重复)
	 */
	virtual bool IsActive() const = 0;

	/**
	 * 获取当前检测状态标签 (v74 — 蒙太奇时间轴驱动)
	 *
	 * 真理源: ANS_MeleeTraceState 写入 / TickDetection 命中时设 Hit
	 *
	 * 调用方: HUD / 音效 / 命中反馈 / 调试可视化
	 *
	 * 大厂原则: 替代 IsActive() bool 字段 — 状态标签包含 Idle/Tracing/Hit 三态
	 *           订阅方拿到语义明确的状态, 不再二值猜"是否在 trace"
	 */
	virtual EWeaponTraceState GetTraceState() const = 0;

	/**
	 * 获取状态变化广播委托 (v74 — 事件驱动替代轮询)
	 *
	 * 调用方: HUD / 音效 / 命中反馈 Subscribe 订阅
	 *
	 * 大厂原则: 替代"每帧 Tick 查 IsActive()" 反模式
	 *           委托由各 Strategy 实现持有, 暴露 const 引用防止外部重绑
	 */
	virtual FOnWeaponTraceStateChanged& OnTraceStateChanged() = 0;
};
