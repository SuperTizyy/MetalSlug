// ==========================================
// ANS_MeleeTraceState — 近战武器检测状态标签 (v74 大厂架构)
//
// 【设计动机 — 用户需求 2026.07.22】
//   美术想在 UE 编辑器蒙太奇时间轴上"拖动标签"控制检测生命周期:
//     - "挥刀中段"开始时: 拖入 ANS_MeleeTraceState(Tracing) — 启动 trace
//     - "收刀"位置:     拖入 ANS_MeleeTraceState(Idle)   — 关闭 trace
//   旧版 BP ANS_MeleeTrace 一刀切, 策划/美术无法分段控制.
//
// 【大厂架构 — AnimNotifyState 标准用法 (UE5.6)】
//   - NotifyBegin (进入标签区间): 调 Weapon->StartDamageTrace()
//   - NotifyEnd   (离开标签区间): 调 Weapon->StopDamageTrace()
//   - NotifyTick  (区间内每帧):  无需做事 (MeleeSwStrategy 内部 Tick 已驱动)
//
// 【协议 — 与 MeleeSwStrategy 对齐】
//   - 仅在近战武器 (MeshType=Melee) 上生效
//   - 武器 Mesh 必须有 TraceStart / TraceEnd 两个 Socket
//   - Socket 名称由 v60+ 起固定, 不允许再改
//
// 【与 BP 旧版 ANS_MeleeTrace 的关系】
//   旧 ANS_MeleeTrace 在蒙太奇时间轴上是单帧 (Notify), 而新版是区间 (NotifyState).
//   新版 NotifyState 可以拖动时长, 完全控制 trace 窗口长度 (例如挥刀中段 0.15s).
//   大厂原则: NotifyState > Notify (区间控制 > 单帧触发).
//
// 【零兜底】
//   - Mesh 无效 / Owner 无效 / 没找到武器 → NotifyBegin Log Error + 不做事
//   - bIsHeavy 配置错 (非 bool 字段) → 编辑器约束 (UPROPERTY meta)
// ==========================================
#pragma once

#include "CoreMinimal.h"
#include "Animation/AnimNotifies/AnimNotifyState.h"
#include "Weapons/WeaponDamageStrategy.h"  // EWeaponTraceState
#include "ANS_MeleeTraceState.generated.h"

class ABaseWeapon;

/**
 * @class UANS_MeleeTraceState
 * @brief 近战武器检测状态 AnimNotifyState (区间型 — 蒙太奇时间轴驱动)
 *
 * 美术使用方法 (UE 编辑器):
 *   1. 打开近战攻击蒙太奇 (例如 Anim_LightAttack01_Montage)
 *   2. 在时间轴上"挥刀中段"位置右键 → Add Notify State → Melee Trace State
 *   3. 拖动时间轴区间长度 (0.1~0.3s, 取决于刀速)
 *   4. 在右侧 Details 面板配置:
 *      - Trace State = Tracing (此区间内开启检测)
 *      - bIs Heavy   = false  (轻击, 重击蒙太奇设为 true)
 *   5. 重复 1-4 给所有 Combo 段 (Combo1 / Combo2 / Combo3)
 *
 * 大厂原则 — 单一真理源:
 *   蒙太奇时间轴 = 美术对"什么时候检测"的唯一决定权
 *   C++ 不再硬编码 StartTrace/StopTrace 时机 (旧 v73 那样)
 */
UCLASS(EditInlineNew, Blueprintable, meta = (DisplayName = "Melee Trace State"))
class METALSLUG01_API UANS_MeleeTraceState : public UAnimNotifyState
{
	GENERATED_BODY()

public:
	UANS_MeleeTraceState();

	// ============================================================
	// 蓝图可配置参数 (美术/策划在蒙太奇时间轴上编辑)
	// ============================================================

	/**
	 * 进入标签区间时写入的检测状态
	 *
	 * 常用配置:
	 *   - Tracing: 开启 trace (挥刀中段)
	 *   - Idle:    强制关闭 trace (异常结束 / 提前收刀)
	 *
	 * 大厂原则: NotifyState 配 Idle 是"主动关闭", 与蒙太奇自然结束 (NotifyEnd 自动关闭) 等价
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee Trace")
	EWeaponTraceState EnterState = EWeaponTraceState::Tracing;

	/**
	 * 是否重击 (影响 Server_ReportHit 伤害值)
	 *
	 * 与 EWeaponTraceState 解耦:
	 *   - State 控制 trace 启停 (美术)
	 *   - bIsHeavy 控制伤害计算 (策划)
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Melee Trace")
	bool bIsHeavy = false;

	// ============================================================
	// UAnimNotifyState 接口实现
	// ============================================================

	virtual void NotifyBegin(
		USkeletalMeshComponent* MeshComp,
		UAnimSequenceBase* Animation,
		float TotalDuration,
		const FAnimNotifyEventReference& EventReference) override;

	/**
	 * @brief 启动蒙太奇区间结束时关闭 trace (调 Weapon->StopDamageTrace)
	 *
	 * 与 NotifyBegin 配对使用 — 美术在蒙太奇时间轴上配置 AnimNotifyState 区间
	 * 区间结束自动调用本方法,关闭武器检测并清理临时状态
	 *
	 * 零兜底: Mesh/Owner 无效 → Log Error + return,不抛异常
	 */
	virtual void NotifyEnd(
		USkeletalMeshComponent* MeshComp,
		UAnimSequenceBase* Animation,
		const FAnimNotifyEventReference& EventReference) override;

	/** 显示名称 (蒙太奇编辑器右键菜单用) */
	virtual FString GetNotifyName_Implementation() const override;
};
