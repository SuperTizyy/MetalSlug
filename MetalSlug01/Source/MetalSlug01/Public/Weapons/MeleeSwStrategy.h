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

	// ==========================================================
	// 【v93.2 大厂架构 — 母体复用 Melee 缝合算法】接口签名扩展
	//
	// 复用动机:
	//   - 母体无武器, 不能复用 BaseWeapon::StartDamageTrace
	//   - 但 BoxTrace 缝合算法 + IgnoreActors 防一刀多伤 = Melee 通用算法, 应该复用
	//   - 不重写 Strategy 类 — 加 bUseOwnerMesh 标志, 让 Strategy 走 Owner Mesh 路径
	//   - Socket 名改 TraceStart_Mother / TraceEnd_Mother (母体 Mesh 上, 武器 Mesh 上仍用 TraceStart/TraceEnd)
	//   - 命中 RPC 改 Owner->Server_ReportMotherAttackHit 而不是 Weapon->Server_ReportHit
	//
	// 大厂原则 — 不重复架构:
	//   - 复用现有 BoxTrace 缝合算法 (零改动)
	//   - 复用现有 IgnoreActors / LastFrame 缓存 (零改动)
	//   - 复用现有 TraceState 状态机 (零改动)
	//   - 只在 StartTrace/TickDetection 加分支 (单一路径)
	// ==========================================================
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
	 * 【v82 客户端射线参数 — Melee 不使用】
	 *   - Melee trace 起点/终点来自 Mesh Socket, 不需要客户端射线
	 *   - 保留参数接口一致性 (默认 ZeroVector = 不使用客户端射线)
	 *
	 * 【v93.2 母体复用】
	 *   - bUseOwnerMesh=false (默认): Weapon Mesh 路径 (刀战玩家/AI)
	 *   - bUseOwnerMesh=true: Weapon->GetOwner() Mesh 路径 (生化母体)
	 *   - bUseOwnerMesh=true 时, Socket 名用 SocketName_MotherTraceStart / SocketName_MotherTraceEnd
	 *
	 * @return true=成功启动, false=配置错/状态错
	 */
	virtual bool StartTrace(ABaseWeapon* Weapon, bool bIsHeavy,
		const FVector& ClientRayOrigin = FVector::ZeroVector,
		const FVector& ClientRayDirection = FVector::ForwardVector) override;

	/**
	 * 【v93.2 母体复用 — 简化入口】启动母体 trace (供 ANS_MeleeTraceState bIsMother 分支直接调)
	 *
	 * 与 StartTrace(Weapon, bIsHeavy) 的区别:
	 *   - 内部固定 bUseOwnerMesh=true
	 *   - 内部固定 Socket 名 = SocketName_MotherTraceStart / SocketName_MotherTraceEnd
	 *   - 调用方只需传 Owner Character + bIsHeavy
	 *
	 * 调用方:
	 *   - UANS_MeleeTraceState::NotifyBegin (bIsMother 分支)
	 *
	 * @return true=成功启动, false=配置错/状态错
	 */
	bool StartMotherTrace(class ABaseCharacter* OwnerChar, bool bIsHeavy);

	/**
	 * 【v93.2 母体复用 — 简化入口】停止母体 trace
	 *
	 * 幂等: 多次调用 no-op
	 */
	void StopMotherTrace(class ABaseCharacter* OwnerChar);

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
	 *
	 * 【v93.2 母体复用】
	 *   - bUseOwnerMesh=false: Mesh 来源 = Weapon->GetMeshComponent(), RPC = Weapon->Server_ReportHit
	 *   - bUseOwnerMesh=true:  Mesh 来源 = Owner->GetMesh(),          RPC = Owner->Server_ReportMotherAttackHit
	 *   - 大厂原则: 算法零重复, 路径决策用 if 分支
	 */
	virtual void TickDetection(ABaseWeapon* Weapon, float DeltaTime) override;

	/**
	 * 查询激活态 (v60.3 新增 — 单一真理源)
	 *
	 * 真理源: TraceState 字段 (v74 — 替代 bIsActive bool)
	 * 调用方: ABaseWeapon::Tick 决策是否委托 TickDetection
	 *
	 * 大厂原则: 替代 BaseWeapon::bIsWeaponActive 重复字段, BaseWeapon::Tick 通过 IsActive() 查询
	 */
	virtual bool IsActive() const override { return TraceState != EWeaponTraceState::Idle; }

	/**
	 * 获取当前检测状态标签 (v74)
	 */
	virtual EWeaponTraceState GetTraceState() const override { return TraceState; }

	/**
	 * 获取状态变化广播委托 (v74)
	 */
	virtual FOnWeaponTraceStateChanged& OnTraceStateChanged() override { return TraceStateChanged; }

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
	 * 当前检测状态标签 (v74 — 替代 bIsActive bool)
	 *
	 * 真理源: ANS_MeleeTraceState::NotifyBegin 写入 Tracing, NotifyEnd 写回 Idle
	 *         TickDetection 命中时设 Hit 后立即回 Tracing
	 *
	 * 大厂原则 - 单一真理源:
	 *   - 蒙太奇时间轴驱动 = 美术可控 (按挥刀节奏)
	 *   - 不再有"start/stop 时机 C++ 说了算"
	 *   - Strategy 内部状态字段, 调用方通过 GetTraceState() / OnTraceStateChanged() 读
	 */
	EWeaponTraceState TraceState = EWeaponTraceState::Idle;

	/**
	 * 状态变化广播委托 (v74 — 事件驱动)
	 *
	 * 订阅方: HUD / 音效 / 命中反馈
	 * 触发: StartTrace/StopTrace/TickDetection 命中时广播
	 */
	UPROPERTY(BlueprintAssignable, Category = "Weapon|Trace")
	FOnWeaponTraceStateChanged TraceStateChanged;

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

	// ==========================================
	// 【v75 单一真理源】Socket 名称常量声明
	// ==========================================
	// 武器 Mesh 必须有以下 Socket (美术在 Mesh 编辑器加):
	//   - TraceStart: 刀刃起点
	//   - TraceEnd:   刀刃终点
	// 大厂原则 - DRY: 唯一真理源在头文件声明 + cpp 定义, StartTrace / TickDetection 共享同一常量
	//   (UCLASS 内不能用 `= FName(...)` 初始化静态成员, UE 反射系统限制, 必须 cpp 内定义)
	static const FName SocketName_TraceStart;
	static const FName SocketName_TraceEnd;

	// ==================================
	// 【v93.2 母体复用】母体 Socket 名称常量
	// ==================================
	// 母体 Pawn Mesh 必须有以下 Socket (美术在 BP_MuTi Mesh 编辑器加):
	//   - TraceStart_Mother: 母体爪击起点
	//   - TraceEnd_Mother:   母体爪击终点
	// 大厂原则 - DRY: 与武器 Socket 名并列, 单一真理源在头文件声明
	static const FName SocketName_MotherTraceStart;
	static const FName SocketName_MotherTraceEnd;

	// ==================================
	// 【v93.2 母体复用】bUseOwnerMesh 路径标志
	// ==================================
	// false (默认): StartTrace 走 Weapon Mesh (刀战路径, 武器 Socket 名)
	// true:         StartTrace 走 Owner Mesh (生化母体路径, 母体 Socket 名)
	// 大厂原则 - 路径明确: 一个 Strategy 实例同时支持两条路径, 通过 bUseOwnerMesh 决策
	bool bUseOwnerMesh = false;

	// ==================================
	// 【v93.2 母体复用】Active Owner (母体路径专用)
	// ==================================
	// 母体路径下, ActiveWeapon 是 nullptr, 命中 RPC 走 Owner->Server_ReportMotherAttackHit
	// 刀战路径下, ActiveOwner 是 nullptr, 命中 RPC 走 Weapon->Server_ReportHit
	// 大厂原则 - 单一真理源: 谁拥有当前 trace, 谁负责报命中
	TWeakObjectPtr<class ABaseCharacter> ActiveOwner;

	// 【v93.2 母体复用】bAIDriven 缓存 (TickDetection 读)
	// 母体路径下, 玩家/AI 判定从 ActiveOwner 读 bIsCurrentlyAttackerAI
	bool bIsMotherAIDriven = false;
};
