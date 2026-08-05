// ===========================================
// 远程射线检测策略 (大厂架构 v60.3 + v60.16)
// 相机起点 + 枪口偏移 (所见即所射)
//
// 【v60.16 射线起点公式重构 — CS:GO / Apex / PUBG 标准】
//   - 射线起点 = 相机位置 + 相机方向 × (TAL + MuzzleOffset)
//   - 射线方向 = 准星屏幕坐标 → DeprojectScreenPositionToWorld
//   - 效果: 子弹从枪口位置射出，方向对准星，所见即所射
//
// 【核心公式 v60.16】
//   射线起点 = CameraLocation + CameraForward × (TargetArmLength + MuzzleOffset)
//   (第三人称: 枪口 → 角色 → 相机)
//
// 【大厂标准对照】
//   CS:GO / Apex / PUBG / Fortnite: 子弹从枪口射出，方向对准星
//   你的 v60.16: 完全一致
//
// 【vXXX P0 关键修复 — 玩家路径适用所有端玩家】
//   - 旧版 v109 用 IsLocalController() 判定 → 只有 ListenServer 自己玩家走公式
//   - 远端客户端玩家(普通客户端玩家)被错判到 "AI 路径" → 起点 = 相机位置 ≠ 武器位置
//   - 新版 vXXX 用 Cast<APlayerController> 类型判定 → 任何端玩家都走公式
//   - 与 v110 GetAimRayFromCrosshairOrEyes 的"类型判定 SSOT" 完全一致
//
// 【设计动机 — 生化模式枪械核心】
//   旧 BaseWeapon.cpp Tick() 写死 BoxTrace → 枪械需要 LineTrace
//   抽出后所有"用枪的算法"集中本类，BaseWeapon 0 算法
//
// 【v60.3 关键重构 — 解决"按左键没射线"bug】
//   - StartTrace 立即 PerformSingleShot 一次 → 完成
//   - 无 1 帧延迟，按下立即打
//
// 【零兜底】
//   - CameraBoom/TargetArmLength 获取失败 → Log Error
//   - Deproject 失败 → Log Error
// ===========================================
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Weapons/WeaponDamageStrategy.h"
#include "RangedLineStrategy.generated.h"

class ABaseWeapon;
class AActor;

/**
 * @class URangedLineStrategy
 * @brief 远程武器 LineTrace 检测策略 (v60.3 + v60.16 — 相机起点 + 枪口偏移)
 *
 * 适用武器: 枪械 (MeshType=Primary / Secondary)
 *
 * 大厂标准 (v60.16):
 *   - 射线起点: CameraLocation + CameraForward × (TargetArmLength + MuzzleOffset)
 *   - 射线方向: 准星屏幕坐标 → DeprojectScreenPositionToWorld → 世界射线
 *   - 效果: 子弹从枪口射出，方向对准星 (CS:GO / Apex / PUBG / Fortnite)
 *
 * 数据流:
 *   WeaponFireComponent 开火 → 调本 Strategy->StartTrace → 立即射线 → 完成
 *   - 半自动: FireComponent.StartFire → StartTrace 一次 → 完成
 *   - 全自动: FireComponent.Tick 节流 → 每 TimeBetweenShotsSeconds 秒调 StartTrace 一次
 */
UCLASS()
class METALSLUG01_API URangedLineStrategy : public UObject, public IWeaponDamageStrategy
{
	GENERATED_BODY()

public:
	URangedLineStrategy();

	// ===========================================
	// IWeaponDamageStrategy 实现
	// ===========================================

/**
 * 启动一次伤害检测 — v60.3 立即执行一发射线
 *
 * 调用方: WeaponFireComponent::PerformSingleShot / ABaseWeapon::PerformDamageTrace
 *
 * 流程 (v82 修复 — 客户端射线参数):
 *   1. 入参校验 (Weapon 非空)
 *   2. 缓存状态 (ActiveWeapon + bIsCurrentHeavy)
 *   3. 调 PerformSingleShot(ClientRayOrigin, ClientRayDirection) — 传入客户端射线参数
 *
 * v82 大厂架构修复:
 *   - 旧 (v70-v81) 反模式: PerformSingleShot 内部用 PC->GetViewportSize + DeprojectScreenPositionToWorld
 *     → 服务器进程对远端玩家 PC 调用 GetViewportSize 返回 0,0 → Deproject 失败 → return false
 *     → 远端玩家在服务器上 trace 永远失败 (用户报告 "玩家客户端攻击没射线检测")
 *   - 新 (v82): ClientRayOrigin/Direction 由客户端在 OnFirePressed 用 HUD Crosshair 算出 → RPC 传给服务器
 *     → 服务器用客户端射线做权威 trace (玩家射线 = 玩家准星)
 *     → 兼容 AI 路径: ClientRayOrigin = ZeroVector → AI fallback 用 BaseAimRotation
 *
 * @return true=成功执行射线, false=配置错/状态错
 */
virtual bool StartTrace(ABaseWeapon* Weapon, bool bIsHeavy,
	const FVector& ClientRayOrigin = FVector::ZeroVector,
	const FVector& ClientRayDirection = FVector::ForwardVector) override;

	/**
	 * 停止检测 — v60.3 对 Ranged 是 no-op
	 */
	virtual void StopTrace(ABaseWeapon* Weapon) override;

	/**
	 * Tick 检测 — v60.3 对 Ranged 永远 no-op
	 */
	virtual void TickDetection(ABaseWeapon* Weapon, float DeltaTime) override;

	/**
	 * 查询激活态 — v60.3 Ranged 永远返回 false
	 *
	 * v74 注: Ranged 也支持状态标签 (单帧 Tracing 后回 Idle), 但 IsActive 仅 Tracing 中为 true
	 */
	virtual bool IsActive() const override { return TraceState != EWeaponTraceState::Idle; }

	/**
	 * 获取当前检测状态标签 (v74 — Ranged 单帧 Tracing)
	 */
	virtual EWeaponTraceState GetTraceState() const override { return TraceState; }

	/**
	 * 获取状态变化广播委托 (v74)
	 */
	virtual FOnWeaponTraceStateChanged& OnTraceStateChanged() override { return TraceStateChanged; }

protected:
	/**
	 * 单次射线检测 (内部使用, StartTrace 内部调用)
	 *
	 * v82 大厂架构修复 — 客户端射线参数:
	 *   - 旧: PerformSingleShot(Weapon) 内部用 PC->GetViewportSize + Deproject
	 *     → 服务器进程失败
	 *   - 新: PerformSingleShot(Weapon, ClientRayOrigin, ClientRayDirection)
	 *     → 客户端传射线, 服务器做权威 trace
	 *     → ClientRayOrigin = ZeroVector 时, AI fallback 用 BaseAimRotation
	 *
	 * @return true=成功执行, false=配置错
	 */
	bool PerformSingleShot(ABaseWeapon* Weapon,
		const FVector& ClientRayOrigin = FVector::ZeroVector,
		const FVector& ClientRayDirection = FVector::ForwardVector);

	/**
	 * 当前激活的 Weapon (保留以便后续 DropOff/后坐力扩展)
	 */
	UPROPERTY()
	TWeakObjectPtr<ABaseWeapon> ActiveWeapon;

	/**
	 * 当前是否重击 (枪械暂未用)
	 */
	bool bIsCurrentHeavy = false;

	/**
	 * 当前检测状态标签 (v74)
	 */
	EWeaponTraceState TraceState = EWeaponTraceState::Idle;

	/**
	 * 状态变化广播委托 (v74)
	 */
	UPROPERTY(BlueprintAssignable, Category = "Weapon|Trace")
	FOnWeaponTraceStateChanged TraceStateChanged;
};
