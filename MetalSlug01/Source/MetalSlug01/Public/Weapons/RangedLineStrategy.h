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
	 * 流程 (v60.16 相机起点 + 枪口偏移):
	 *   1. 获取相机位置 + 方向
	 *   2. 获取 TargetArmLength (相机到角色中心)
	 *   3. 计算射线起点 = 相机位置 - 相机方向 × (TargetArmLength + MuzzleOffset)
	 *   4. 准星屏幕坐标 → Deproject → 世界方向
	 *   5. 终点 = 起点 + 方向 × AttackRange
	 *   6. LineTraceSingle → 命中调 Server_ReportHit
	 *
	 * @return true=成功执行射线, false=配置错/状态错
	 */
	virtual bool StartTrace(ABaseWeapon* Weapon, bool bIsHeavy) override;

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
	 */
	virtual bool IsActive() const override { return false; }

protected:
	/**
	 * 单次射线检测 (内部使用, StartTrace 内部调用)
	 *
	 * 流程 (v60.16 相机起点 + 枪口偏移):
	 *   1. 相机位置 - 相机方向 × (TargetArmLength + MuzzleOffset) = 枪口位置
	 *   2. 准星屏幕中心 → Deproject → 世界方向
	 *   3. 终点 = 枪口位置 + 方向 × AttackRange
	 *   4. LineTraceSingle → 命中调 Server_ReportHit
	 *
	 * @return true=成功执行, false=配置错
	 */
	bool PerformSingleShot(ABaseWeapon* Weapon);

	/**
	 * 当前激活的 Weapon (保留以便后续 DropOff/后坐力扩展)
	 */
	UPROPERTY()
	TWeakObjectPtr<ABaseWeapon> ActiveWeapon;

	/**
	 * 当前是否重击 (枪械暂未用)
	 */
	bool bIsCurrentHeavy = false;
};
