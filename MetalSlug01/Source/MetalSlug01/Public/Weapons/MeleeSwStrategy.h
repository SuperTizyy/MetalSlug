// ==========================================
// 近战扫掠检测策略 (大厂架构 v60.3 — Strategy 模式实现 #1)
//
// 【设计动机】
//   旧 BaseWeapon.cpp Tick() 内联 BoxTrace → 不能复用到枪械
//   抽出后 BaseWeapon.Tick() 仅委托 Strategy, 自身 0 算法
//
// 【v60.3 接口语义修订 — 与 Ranged 对称】
//   - StartTrace 返回 bool (true=成功启动, false=配置错/状态错)
//   - StartTrace 不立即执行 (Melee 跨帧缝合需要等待下一帧 TickDetection)
//   - 加 IsActive() 供 BaseWeapon::Tick 查询 (替代重复的 bIsWeaponActive)
//
// 【大厂原则 — 完整迁移 v1-v50 逻辑】
//   - BoxTraceSingle 缝合上一帧/当前帧 (核心黑科技)
//   - LastFrameStartLoc/EndLoc 缓存 (跨帧状态)
//   - IgnoreActors 防一刀多伤
//   - 重击/轻击伤害字段从 Weapon 读 (LightDamageBody/Head/Heavy — 走 Server_ReportHit 算)
//   - 这里只负责命中检测, 不算伤害 (单一职责)
//
// 【零兜底】
//   - WeaponMesh 失效 → StartTrace 返回 false + Log Error
//   - TraceStart/TraceEnd Socket 不存在 → StartTrace 返回 false + Log Error (配置错, 让美术加 Socket)
//   - 缝合距离为 0 → 直接跳过本帧 (防 NaN)
//
// 【Socket 协议 — 武器 Mesh 必须有以下 Socket】
//   - TraceStart: 刀刃起点
//   - TraceEnd:   刀刃终点
//   BP 美术未配 → 启动时 Log Error + 强制修复
// ==========================================
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "Weapons/WeaponDamageStrategy.h"
#include "MeleeSwStrategy.generated.h"

class ABaseWeapon;
class AActor;

/**
 * @class UMeleeSwStrategy
 * @brief 近战武器 BoxTrace 检测策略 (v60.3 — 跨帧 Tick 缝合)
 *
 * 适用武器: 刀/铲/斧/任何近战 (MeshType=Melee)
 *
 * 数据流:
 *   ABaseWeapon::Tick (IsActive=true) → TickDetection → BoxTrace 缝合 → Server_ReportHit RPC
 *
 * 与 Ranged 的关键区别:
 *   - Melee 需要跨帧状态 (LastFrameStartLoc/EndLoc), 必须 Tick 持续做事
 *   - Ranged 单次独立, 不需要 Tick
 *   - 接口语义统一 (StartTrace 返回 bool), 实现可不同
 */
UCLASS()
class METALSLUG01_API UMeleeSwStrategy : public UObject, public IWeaponDamageStrategy
{
	GENERATED_BODY()

public:
	UMeleeSwStrategy();

	// ==========================================
	// IWeaponDamageStrategy 实现
	// ==========================================

	/**
	 * 启动近战检测 (v60.3 — 立即记录激活态, 不立即执行)
	 *
	 * Melee 与 Ranged 的关键区别:
	 *   - Melee: StartTrace 初始化跨帧状态, 实际 BoxTrace 在 TickDetection 做
	 *   - Ranged: StartTrace 立即打一发 (v60.3 重构)
	 *
	 * 【协议】
	 *   Weapon 必须有 Mesh (UMeshComponent), Mesh 上有 TraceStart / TraceEnd 两个 Socket
	 *
	 * @return true=成功启动, false=配置错/状态错
	 */
	virtual bool StartTrace(ABaseWeapon* Weapon, bool bIsHeavy) override;

	/**
	 * 停止近战检测 — 清 IgnoreActors, 重置 LastFrame 缓存, bIsActive=false
	 *
	 * 大厂原则: 必须调 (否则 Tick 持续做事, 漏伤 + 性能浪费)
	 */
	virtual void StopTrace(ABaseWeapon* Weapon) override;

	/**
	 * 每帧 BoxTrace 缝合检测
	 *
	 * 核心算法 (大厂黑科技 — 完整保留 v1-v50 逻辑):
	 *   1. 读 TraceStart / TraceEnd 当前世界坐标
	 *   2. 计算上一帧中心 / 当前帧中心
	 *   3. 算刀身长度
	 *   4. BoxTraceSingle 从 LastMid → CurrentMid (缝合)
	 *   5. 命中 → 加 IgnoreActors + 调 Weapon->Server_ReportHit
	 *   6. 更新 LastFrame 缓存
	 */
	virtual void TickDetection(ABaseWeapon* Weapon, float DeltaTime) override;

	/**
	 * 查询激活态 (v60.3 新增 — 单一真理源)
	 *
	 * 真理源: bIsActive 字段
	 * 调用方: ABaseWeapon::Tick 决策是否委托 TickDetection
	 *
	 * 大厂原则: 替代 BaseWeapon::bIsWeaponActive 重复字段, BaseWeapon::Tick 通过 IsActive() 查询
	 */
	virtual bool IsActive() const override { return bIsActive; }

protected:
	/**
	 * 一刀多伤防护: 已经命中过的 Actor 列表 (武器 Actor 销毁时自动清)
	 *
	 * 大厂原则:
	 *   - 不存在则 Trace 全部失败 (打架时空挥)
	 *   - 旧 v1-v50 在 BaseWeapon.cpp 里持有 — 现在迁到 Strategy 内部, 跟随 Strategy 生命周期
	 */
	UPROPERTY()
	TArray<TWeakObjectPtr<AActor>> IgnoreActors;

	/**
	 * 上一帧刀刃起点 (用于缝合检测)
	 *
	 * 大厂原则:
	 *   - 跨帧状态必须存在 (防止挥刀速度过快时漏判)
	 *   - 由 StartTrace 第一帧写入, 之后每帧 TickDetection 更新
	 */
	FVector LastFrameStartLoc = FVector::ZeroVector;

	/**
	 * 上一帧刀刃终点
	 */
	FVector LastFrameEndLoc = FVector::ZeroVector;

	/**
	 * 当前激活态 (v60.3 — 唯一真理源, BaseWeapon::Tick 通过 IsActive() 查)
	 *
	 * 写入: StartTrace 设 true, StopTrace 设 false
	 * 读取: IsActive() 接口, TickDetection 内部防御
	 */
	bool bIsActive = false;

	/**
	 * 当前激活的 Weapon 指针 (回调 Server_ReportHit 时用)
	 *
	 * 大厂原则 - 不缓存:
	 *   - 仅 StartTrace 时记录, StopTrace 时清空
	 *   - 不缓存 Actor 引用 (BP archetype 会 nullify)
	 *   - TickDetection 内只读不写, StopTrace 后必为 nullptr
	 */
	UPROPERTY()
	TWeakObjectPtr<ABaseWeapon> ActiveWeapon;

	/**
	 * 当前是重击还是轻击 (TickDetection 命中后传给 Server_ReportHit)
	 */
	bool bIsCurrentHeavy = false;
};
