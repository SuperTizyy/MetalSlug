// Copyright (c) 2026.

#pragma once

// ==========================================
// 头文件包含说明
// ==========================================
#include "CoreMinimal.h"

#include "AIController.h"
#include "Perception/AIPerceptionTypes.h"
#include "GenericTeamAgentInterface.h"
#include "Data/AI/AIProfileAsset.h" // 【Phase 1】FOnAIBehaviorConfigLoaded
#include "Systems/AI/AIBehaviorTypes.h"
#include "BaseAIController.generated.h"

// 前置声明（加快编译）
class UAIPerceptionComponent;
class UBehaviorTree;
class UAIRuntimeConfigComponent;
class UBlackboardComponent;

/**
 * ABaseAIController
 * 项目所有 AI 控制器的 C++ 基类
 *
 * 【Phase 1 重构】一次性切换到 UE5 阵营协议:
 *   - 砍掉自造的 uint8 TeamID + Cast<ABaseCharacter> 阵营判定
 *   - 阵营判定走 IGenericTeamAgentInterface::GetTeamAttitudeTowards (UE5 原生)
 *   - AIPerception 检测到目标时, 引擎自己询问敌我, 自动决定是否回调 OnTargetPerceptionUpdated
 *   - 行为树参数 (250 / 1500 / 1800 / 90) 全部下沉到 UAIBehaviorConfigSO (RuntimeConfigComponent 提供)
 *   - 行为树启动走 UAIProfileAsset 异步加载; 老直启字段已废
 */
/**
 * 【UE 5.6 修复】 AAIController 已经继承 IGenericTeamAgentInterface
 * 我们只需 override, 不必重复声明接口冒. 重写 GetGenericTeamId / GetTeamAttitudeTowards 即可.
 */
UCLASS()
class METALSLUG01_API ABaseAIController : public AAIController
{
	GENERATED_BODY()

public:
	ABaseAIController();

/**
 * 【P0 2026.07.07 v11 大厂架构重构】NAV Arrival 决策结果 — 距离驱动停车 (迟滞版)
 *
 * 这是大厂"距离驱动停车"语义的最终落点, 决策算法从 TickChaseFallback 抽出到
 * ABaseAIController::ComputeArrivalDecision (纯函数, 无副作用, 可单测)。
 *
 * v11 重大修复 (用户 2026.07.07 18:34 反馈 — 删除 RetreatSpeed,改为冷却期实时距离维持):
 *   - 旧 v10: 冷却期距离 < AR → Chase(-RetreatSpeed=0.5) 半速后退,速度可配置
 *     - 问题: 半速过慢,玩家持续移动时 AI 跟不上维持距离
 *     - 问题: 距离接近 0 时 (典型 103cm),AI 半速后退被玩家继续前进抵消,看似不动
 *   - v11 修复: 冷却期距离 < AR-Hyst → Chase(-0.7) (硬编码 70% MaxWalkSpeed)
 *     - 用户明确需求: "不需要 RetreatSpeed"
 *     - 0.7 是大厂标准 (MGS / 死亡搁浅 / GTA5): 略慢于玩家冲刺 (PlayerMaxWalkSpeed=600),
 *       AI 后退能跟上玩家前进,维持 AR 距离
 *
 * v11 同步引入 Hysteresis (迟滞):
 *   - 旧 v10: D >= AR → Chase, D < AR → LockStop/Retreat (抖动边界)
 *   - v11:  D > AR+Hyst → Chase(1.0); D < AR-Hyst → 后退; 中间区间保留上一帧决策
 *
 * 状态机 v11 完整版 (优先级从高到低):
 *   [P1] bInCooldown=true                          → 距离维持 (含攻击蒙太奇期 + 冷却期)
 *          D > AR+Hyst   : Chase(1.0)
 *          D < AR-Hyst   : 面向敌人后退 Chase(-0.7)
 *          区间内        : LockStop (等待冷却结束)
 *   [P2] bAttacking=true && !bAllowMovementDuringAttack → LockStop (站桩型 AI 强制)
 *   [P3] bAttacking=true &&  bAllowMovementDuringAttack → 冲锋型: 距离维持 (同 P1)
 *   [P4] 非攻击非冷却: 距离维持 (同 P1, 持续维持 AR 距离, 不依赖冷却标志)
 *
 * 注意: v11 起 P1/P3/P4 都用同一套距离维持逻辑,核心差异只在 LockStop 是否
 *       由"bAttacking+!bAllowMovement"产生;其它状态 AI 都持续主动维持 AR 距离
 *
 * 这是**普通 C++ struct** (非 USTRUCT), 仅 C++ 内部用
 *       UHT 不接受 UFUNCTION 引用非反射类型, 所以本结构配套的函数
 *       ComputeArrivalDecision 不挂 UFUNCTION (普通 C++ static 即可)
 */
struct FArrivalDecision
{
	enum class EAction : uint8
	{
		Chase,        // 全速或减速追 (ScaleValue 可正可负, 负值=后退)
		LockStop,     // 硬停车 (MaxWalkSpeed=0 + StopMovementImmediately)
	};
	EAction Action = EAction::Chase;
	float   ChaseScale = 1.0f;  // 仅 Chase 时有意义: 正=追, 负=后退
	bool    bShouldLock = false; // 给 LockMovementForCooldown 用, 与 Action 同语义冗余
};

/**
 * 【P0 2026.07.07 v11 大厂架构重构】NAV Arrival 纯函数决策 — 距离驱动的停车语义 (Hysteresis 版)
 *
 * 注意: 普通 C++ 函数 (非 UFUNCTION), 给 C++ 调用方用即可, 不暴露 BP
 *
 * v11 关键变更 (用户 2026.07.07 18:34 反馈):
 *   1. 删除 RetreatSpeed 参数 (用户明确不要)
 *      → 后退速度硬编码 0.7 (大厂标准, 略慢于玩家冲刺, 维持距离)
 *   2. 引入 AttackRangeHysteresis (迟滞) 参数
 *      → 解决 v10 "AttackRange 边界抖动" 问题
 *      → 在 [AR-Hyst, AR+Hyst] 区间保持原决策, 玩家小幅接近时不会反复切换方向
 *   3. 所有状态 (P1/P3/P4) 都用同一套距离维持逻辑
 *      → v10 P4 "非攻击非冷却: 贴身 LockStop" 改为 "距离维持"
 *      → 真正做到用户原话 "只要进入攻击冷却(或任何状态), 实时监测敌人与 AI 距离,
 *        AI 保持 AttackRange 设定的距离, 直到攻击冷却结束" (推广到所有状态)
 *
 * 行为契约 (v11):
 *   - 玩家距离 > AR+Hyst: Chase(1.0) 全速追
 *   - 玩家距离 < AR-Hyst: Chase(-0.7) 面向敌人后退 (硬编码 70% MaxWalkSpeed)
 *   - 玩家距离 ∈ [AR-Hyst, AR+Hyst]: LockStop (迟滞区间, 防止抖动)
 *   - bAttacking + !bAllowMovementDuringAttack (站桩型): 全距离 LockStop (用户原意)
 *
 * @param Distance     AI 与目标距离 (cm), 已用 ComputeActorCenterDistance
 * @param AttackRange  攻击范围 (cm), 来自 GetEffectiveAttackRange
 * @param bAttacking  AI 是否在攻击蒙太奇中 (C++ 成员, 事件驱动)
 * @param bAllowMovementDuringAttack 冲锋型 AI 是否允许攻击中移动 (ConfigSO)
 * @param bInCooldown 冷却标志 (蒙太奇播放中 + 蒙太奇后冷却期, 由 OnMontageEnded/Timer 关闭)
 * @param Hysteresis  迟滞缓冲 (cm), 来自 GetAttackRangeHysteresis (默认 20cm)
 * @return 决策结构: { 动作, ChaseScale, bShouldLock }
 */
static FArrivalDecision ComputeArrivalDecision(
	float Distance, float AttackRange,
	bool bAttacking, bool bAllowMovementDuringAttack,
	bool bInCooldown, float Hysteresis = 20.f);

	// 【P0 大厂架构 2026.07.06 终极修复】C++ 与 BT 通信的攻击状态
	// 之前用 BB Key bIsAttackingNow: 需要改 BB_AI_Melee.uasset, 改资产不灵活
	// 现在用 C++ 成员: 不依赖 BB asset, 跨 BT 重启保持, 跨 AI 派生类复用
	//
	// 【P0 终极修复 2026.07.06】设为 true 时同时重置 TimeSinceAttackingStarted
	// 用于检测 bIsCurrentlyAttacking 是否卡死
	UFUNCTION(BlueprintCallable, Category = "AI|Combat")
	void SetCurrentlyAttacking(bool bAttacking);

	UFUNCTION(BlueprintPure, Category = "AI|Combat")
	bool IsCurrentlyAttacking() const { return bIsCurrentlyAttacking; }

	/**
	 * 【P0 2026.07.07 v10 大厂架构重构】AttackCooldown 冷却期间标志 — 窗口期语义
	 *
	 * v10 重大修复 — 生命周期语义重构:
	 *   bIsInAttackCooldown = "AI 处于攻击意图执行/等待阶段" 的统一标志
	 *   = 蒙太奇播放期间 (默认 LockStop)  +  蒙太奇后冷却期 (距离维持)
	 *
	 *   v11 状态切换 (与 BTTask 端 Timer 对齐) — 同 v10, 无变化:
	 *     - PerformAttack() 触发 (BTTask)  → SetInAttackCooldown(true)   [攻击蒙太奇开始]
	 *
	 *   v15 状态切换修复 (用户 2026.07.07 20:04 反馈 "AI 在攻击冷却时持续原地走动"):
	 *     - 根因 R-0: v14 把 SetInAttackCooldown(true) 放在 TickTask 状态 2,
	 *       但 ExecuteTask 入口 (距离够时) 直接调 PerformAttack → 绕过 TickTask 状态 2
	 *       → bIsInAttackCooldown=true 漏设 → v14 P2-New LockStop 永远进不去
	 *     - v15 修复: 职责下沉, PerformAttack 内部对称设 SetInAttackCooldown(true)
	 *       与 SetCurrentlyAttacking(true) 并列, 攻击触发的所有 C++ 状态由 PerformAttack 单点维护
	 *     - TickTask 状态 2 里的 SetInAttackCooldown(true) 保留为防御性兜底 (幂等)
	 *
	 *   v16 设计变更 (用户 2026.07.07 20:11 反馈 "玩家走 AI 不跟随, 要一会才跟随") — 重大变更:
	 *     - v14 引入的 "冷却期 LockStop" (P2-New) 被证明是错误修复方向
	 *     - 用户真正诉求链演变:
	 *         18:34 → "AI 不会后退" → 想要距离维持
	 *         19:50 → "AI 原地走动" → 想要冷却期 LockStop (v14 修复方向)
	 *         20:11 → "AI 不跟随" → 推翻 19:50, 5s LockStop 太久了
	 *     - v16 最终设计: 冷却期与完全空闲行为完全一致 → 距离维持 AttackRange
	 *     - bIsInAttackCooldown 仍然维护 (Timer 仍会清), 但**不再影响 ComputeArrivalDecision**
	 *     - 物理直觉: AI 攻击完恢复"主动战斗意识", 立刻按 AttackRange 调整距离
	 *
	 *   v17 抗抖动修复 (用户 2026.07.07 20:20 反馈 "现在都没问题了, 只有一个问题, 就是 ai 会原地走动") — 关键修补:
	 *     - 日志证据: AI 在 D=170/184/181/187/190 之间反复抖动, 触发 Chase(+1.0) ↔ Chase(-0.7) 高频翻转
	 *     - 根因: v13 删除 Hyst 字段 → 单点 AR=180 边界 → 玩家小幅抖动 (引擎 tick / 动画根位移 / NavMesh 投影)
	 *       直接跨过 180 → 每帧决策翻转 → 物理表现 "原地走动"
	 *     - v17 设计变更 (大厂"减熵 + 抗抖动兼顾"原则):
	 *         ✓ Hyst 字段保持删除 (用户 19:30 减熵诉求保留, ConfigSO 不暴露)
	 *         ✓ 在 C++ 中硬编码 kHysteresisForComputeArrivalDecision = 30.f
	 *           边界从单点 AR=180 变成 [AR-30, AR+30] = [150, 210] 区间
	 *           玩家在 180 附近 ±20cm 抖动 → 落入 LockStop 区间 → 不切换
	 *         ✓ TickChaseFallback Refractory Period 同时扩大: 任何决策切换都进 200ms 静默期
	 *           (不仅符号反转, LockStop↔Chase 也拦截), 但保留 "LockStop→Chase(-0.7) 直通"
	 *           (玩家突进必须立刻后退, 不能等 200ms)
	 *     - 不变量保持 (v16):
	 *         - 冷却期与完全空闲完全一致 → 距离维持 AttackRange (玩家贴身→后退)
	 *         - 蒙太奇中 (bAttacking=true): 站桩 LockStop / 冲锋 P3 距离维持
	 *
	 *   v16 设 false 路径 (对称闭环):
	 *     - OnAIAttackMontageEnded 回调       → 不清 (异常路径 bInterrupted=true 才清)
	 *     - BTTask Timer 回调 (Cooldown 秒)   → SetInAttackCooldown(false)  [真正冷却结束]
	 *     - AbortTask / bCooldownTimeout      → SetInAttackCooldown(false)  [异常路径兜底]
	 *
	 *   TickChaseFallback 行为 (v17 — 三态决策 + 抗抖动双兜底):
	 *     - bAttacking=true && !bAllowMovementDuringAttack → LockStop (P1 站桩)
	 *     - bAttacking=true &&  bAllowMovementDuringAttack → P3 距离维持 (冲锋型)
	 *     - bAttacking=false (不论 bInCooldown)            → P3 距离维持 (完全空闲 / 冷却期)
	 *     - + 30cm Hyst 区间 → 玩家小幅抖动落 LockStop
	 *     - + 200ms Refractory Period → 任何决策切换都进静默期 (仅保留 Chase(-0.7) 直通)
	 *
	 *   v11 修复 (用户 2026.07.07 18:34 反馈):
	 *     - 不再依赖 RetreatSpeed 可配置字段 (用户明确删除)
	 *     - Hysteresis 取代"严格边界", 解决 v10 在 AttackRange 边缘抖动
	 *     - 任何状态 (P1/P3/P4) 都持续维持 AR 距离, 不再有"非攻击就 LockStop"的死站场景
	 *
	 *   v10 修复 (用户 2026.07.07 18:25 反馈) — 仍成立:
	 *     - 蒙太奇时长 (9.97s) ≠ Cooldown 时长 (1.2s 配置)
	 *     - 旧 v9 在 OnAIAttackMontageEnded 立刻清 Cooldown → AI 进入 P4 → LockStop
	 *     - v10 修复: 以 Timer 为准, 蒙太奇结束保持 Cooldown, 直到 ConfigSO.AttackCooldown 秒后
	 *     - AI 持续距离维持, 直到真正冷却结束
	 *     - 蒙太奇时长 (9.97s) ≠ Cooldown 时长 (1.2s 配置)
	 *     - 旧 v9 在 OnAIAttackMontageEnded 立刻清 Cooldown → AI 进入 P4 → LockStop
	 *     - 用户感知: "AI 在下一次攻击开始时才判断 AttackRange 设定的距离移动"
	 *     - v10 修复: 以 Timer 为准, 蒙太奇结束保持 Cooldown, 直到 ConfigSO.AttackCooldown 秒后
	 *     - AI 持续距离维持, 直到真正冷却结束
	 *
	 *   旧实现 (v8) 的 bug:
	 *     - Timer 1.2s 后立即 SetInAttackCooldown(false)
	 *     - 蒙太奇 9.97s, 中间 8.7s 处于 "bAttacking=true, bInCooldown=false"
	 *     - 走普通 LockStop 分支, AI 完全不动 → 玩家贴身 68cm 持续 5+ 秒
	 *     - 用户原话: "AI 与敌人距离保持 AttackRange 设定的距离, 敌人离 ai 小于 AttackRange 设定的距离, 需要 ai 面向敌人往后退"
	 *
	 *   v9 也存在的隐含问题:
	 *     - 蒙太奇结束立刻清 Cooldown, 冷却窗口期"蒙太奇已结束但冷却未结束"期间 AI 不动
	 *     - 用户原话: "AI 在下一次攻击开始时才判断 AttackRange 移动"
	 *
	 * 设计哲学 (v10 终极):
	 *   Cooldown 标志严格对齐 ConfigSO.AttackCooldown 秒数 (唯一时间源)
	 *   与蒙太奇时长解耦: 蒙太奇播放期 / 蒙太奇后冷却期 / 蒙太奇先于冷却结束这三种
	 *   情况都由 bIsInAttackCooldown 一并覆盖
	 */
	UFUNCTION(BlueprintCallable, Category = "AI|Combat")
	void SetInAttackCooldown(bool bInCooldown);
	UFUNCTION(BlueprintPure, Category = "AI|Combat")
	bool IsInAttackCooldown() const { return bIsInAttackCooldown; }
	bool bIsInAttackCooldown = false;

	/**
	 * 入口: 由 GameMode/Spawner 在生成 AI 后调用
	 * 行为契约:
	 *   - Profile → 同步 LoadBehaviorConfigSync → ApplyConfig → async LoadBehaviorTree → RunBehaviorTree
	 *   - Profile 为空: 标记无 Profile 状态, 调用 RunLegacyIfPossible 走 fallback (Blueprint 默认 BT)
	 */
	UFUNCTION(BlueprintCallable, Category = "AI")
	void InitializeFromProfile(UAIProfileAsset* InProfile);

	/** 难度热注入 */
	UFUNCTION(BlueprintCallable, Category = "AI")
	void SetDifficultyTier(EAIDifficultyTier NewTier);

	/** 统一读 Config 入口 */
	UFUNCTION(BlueprintPure, Category = "AI")
	UAIRuntimeConfigComponent* GetRuntimeConfig() const { return RuntimeConfig; }

	/**
	 * 【Phase 2】公开读取当前 Profile — 让 GameMode 等外部系统不用 friend 也能拿到
	 * 设计: 当前生效的 AI Profile (可能为 nullptr, 表示走 Legacy)
	 */
	UFUNCTION(BlueprintPure, Category = "AI")
	UAIProfileAsset* GetCurrentProfile() const { return CurrentProfile; }

	/**
	 * 【P0 大厂架构 2026.07.06 19:25】AI 攻击间隔的最终值（Profile × 难度缩放）
	 *
	 * 用途: BTTask 一行调用, 不用关心 Profile/ConfigSO/难度表的回退链
	 *       内部走 GetCurrentProfile()->GetEffectiveAttackInterval() → GetScaledCombat().AttackCooldown
	 *
	 * 行为:
	 *   - 没 Profile: 返回 ConfigSO 默认值 (1.2s)
	 *   - 有 Profile 但 AttackInterval <= 0: 用 ConfigSO + 难度缩放
	 *   - 有 Profile 且 AttackInterval > 0: 优先生效, 难度缩放不叠加 (策划手动值)
	 *
	 * @return 最终攻击间隔（秒）; 0 表示禁止攻击
	 */
	UFUNCTION(BlueprintPure, Category = "AI|Combat")
	float GetEffectiveAttackInterval() const;

	/**
	 * 【P0 大厂架构 2026.07.06 重构】AI 攻击伤害的最终值（ConfigSO + 难度缩放）
	 *
	 * 用途: 修复 ConfigSO.Damage 从未被读的死代码 bug
	 *       OnAIRequestAttack_Simple 调一行 GetEffectiveAttackDamage() 拿到最终伤害
	 *
	 * 行为 (大厂设计):
	 *   - 单一数据源: ConfigSO.Combat.Damage (策划在 DA_AIBehavior_XXX 配置)
	 *   - 难度缩放: GetScaledCombat 内部完成 (Hard=1.3x, Easy=0.8x)
	 *   - < 0 = 禁用 AI 攻击伤害 (但仍播蒙太奇, 用于"只吓人不伤人"AI)
	 *   - = 0 = 0 伤害 (跟 < 0 行为一致, 但语义明确)
	 *
	 * @return 最终伤害值（已是难度缩放后的）
	 */
	UFUNCTION(BlueprintPure, Category = "AI|Combat")
	float GetEffectiveAttackDamage() const;

	/**
	 * 【P0 大厂架构 2026.07.06 重构】AI 当前的目标（来自 Blackboard）
	 *
	 * 用途: OnAIRequestAttack_Simple 需要知道 AI 在打谁, 用于 ApplyPointDamage
	 *       内部走 AIController->GetBlackboardComponent()->GetValueAsObject("TargetActor")
	 *       封装到这里避免 BaseCharacter 关心 BB Key 名字 + BB 头文件 include
	 *
	 * @return 目标 Actor (可能为 nullptr, 调用方做空检查)
	 */
	UFUNCTION(BlueprintPure, Category = "AI|Combat")
	AActor* GetCurrentTargetActor() const;

	/**
	 * 【P0 2026.07.07 大厂架构重构】AI 的最终攻击范围
	 *
	 * 与 BTTask_MeleeAttack::GetAttackRange 完全一致, 单一数据源:
	 *   RuntimeConfig->GetConfig()->GetScaledCombat().AttackRange (含难度缩放)
	 *
	 * 用途:
	 *   - TickChaseFallback 冷却状态机判断"玩家是否进入攻击范围"
	 *   - 防止 TickChaseFallback 和 BTTask_MeleeAttack 各自维护 AttackRange 导致数值漂移
	 *
	 * @return 最终攻击范围（厘米）; 没 Profile/Config 时返回默认 180
	 */
	UFUNCTION(BlueprintPure, Category = "AI|Combat")
	float GetEffectiveAttackRange() const;

	/**
	 * 【P0 2026.07.07 大厂架构】攻击时是否允许移动
	 *
	 * 用途: ComputeArrivalDecision 读取此值决定攻击中是否 LockStop
	 *
	 * 行为:
	 *   - true (默认): 攻击蒙太奇期间按距离正常决策 (可能渐进减速到位)
	 *   - false: 攻击蒙太奇期间强制 LockStop, 只有 bIsCurrentlyAttacking=false 后才恢复
	 *
	 * 数据源: ConfigSO.Combat.bAllowMovementDuringAttack (默认 true, 向后兼容)
	 *         不参与难度缩放 (这是行为开关, 不是数值倍率)
	 *
	 * @return true=攻击中允许移动, false=攻击中禁止移动
	 */
	UFUNCTION(BlueprintPure, Category = "AI|Combat")
	bool GetAllowMovementDuringAttack() const;

	/**
	 * 【P0 2026.07.07 v13 大厂减熵】AttackRangeHysteresis 已删除
	 *
	 * 历史 (v11-v12):
	 *   v11 引入 Hysteresis 字段 (迟滞缓冲) 解决 AttackRange 边界抖动.
	 *   v12 上调默认值 20 → 30cm + 数据访问层兜底.
	 *   v13 用户原话 (2026.07.07 19:30):
	 *     "AttackRangeHysteresis 感觉没什么用啊, 删掉吧"
	 *     "正常后退即可, 无需这个参数"
	 *
	 * 引擎层兜底 (v13 仍生效):
	 *   即便 Hyst 字段被删除, TickChaseFallback 内置 Refractory Period (200ms 决策静默期)
	 *   仍保留 — 这是抗抖动的核心机制, 不依赖任何数据源.
	 *   玩家在 AttackRange 边界抖动时, 静默期内锁定决策, 避免 "原地走动".
	 *
	 * 数据源: (无, 迟滞为 0, 边界是单点 AR)
	 *
	 * @return 恒为 0 (v13, 字段已删除)
	 *
	 * 已移除: BlueprintPure "float GetAttackRangeHysteresis()" UFUNCTION (v13 减熵)
	 */
	UFUNCTION(BlueprintPure, Category = "AI|Combat", meta = (DeprecatedFunction, DeprecationMessage = "v13 Hysteresis removed. Decision now uses Refractory Period in TickChaseFallback. Returns 0."))
	float GetAttackRangeHysteresis() const;

	/**
	 * 【P0 2026.07.07 v11 大厂架构重构】AI 与目标 Actor 中心点距离 (cm)
	 * 统一距离算法, 取代散落在 BaseAIController::TickChaseFallback / BTTask_MeleeAttack::TickTask
	 * 里各自手算的 FVector::Dist(...GetActorLocation())。
	 * 单一数据源: 胶囊体中心 / 包围盒中心, 避免脚底 88cm 误差。
	 */
	UFUNCTION(BlueprintPure, Category = "AI|Combat")
	static float ComputeActorCenterDistance(const AActor* A, const AActor* B);

	/**
	 * 【P0 2026.07.07 v9 大厂架构】距离决策函数单元自检
	 *
	 * 大厂实践: 纯函数 → 启动期自动跑全部状态机组合, 失败立即 Error 日志
	 * 调用方: GameMode::BeginPlay 或 GameInstance::Init 中执行一次
	 *
	 * 关键 case: 冷却期 + 贴身 → 后退 (用户 2026.07.07 18:11 反馈)
	 */
	static void SelfTestArrivalDecision();

	/**
	 * 【P0 2026.07.07 大厂架构重构】单个 Actor 的中心点 (世界坐标)
	 * 优先 CapsuleComponent 中心 (Character), 否则包围盒中心。
	 * 与 ComputeActorCenterDistance 共享一套语义, 是它的原子单元。
	 */
	UFUNCTION(BlueprintPure, Category = "AI|Combat")
	static FVector ComputeActorCenter(const AActor* A);

	/**
	 * 【P0 2026.07.07 大厂架构重构】冷却锁步的统一接口
	 *
	 * 用途: TickChaseFallback 冷却期间调用, 一站式管理:
	 *   - CharacterMovement->MaxWalkSpeed (锁 0 或恢复 600)
	 *   - BaseCharacter::bIsMovementLocked (true → AnimInstance 走 Idle 动画 → AnimNotify_Footstep 不触发)
	 *
	 * 设计意图: 修复问题三 "冷却时玩家原地不动, AI 原地不动但仍触发脚步声"
	 *
	 * 重要: bLock=true 时必须在 TickChaseFallback 中**也**停掉 AddMovementInput,
	 *       否则 Velocity 与 MaxWalkSpeed=0 矛盾,引擎会频繁强制刹车 (抖动)
	 *
	 * @param bLock       true=锁步 (MaxWalkSpeed=0, bIsMovementLocked=true)
	 *                    false=恢复 (MaxWalkSpeed=600, bIsMovementLocked=false)
	 */
	UFUNCTION(BlueprintCallable, Category = "AI|Combat")
	void LockMovementForCooldown(bool bLock);

	/**
	 * 【Phase 1 重构】AI 自身阵营
	 * 设计: 走 IGenericTeamAgentInterface (不再自造 uint8 TeamID)
	 * 默认 ID=255 (Hostile), 由 InitializeFromProfile 根据 Profile.FactionTag 切到具体阵营
	 */
	virtual void SetGenericTeamId(const FGenericTeamId& NewTeamID) override;
	virtual FGenericTeamId GetGenericTeamId() const override;
	virtual ETeamAttitude::Type GetTeamAttitudeTowards(const AActor& Other) const override;

	/**
	 * 【大厂架构 2026.07.06 公开给 BTService/外部使用】
	 * 不依赖 AIPerception 任何 API 的主动扫描
	 * 用 TActorIterator<ACharacter> + IGenericTeamAgentInterface 阵营判定找最近敌人
	 * @param MyCharacter  自身 Character (用于计算距离)
	 * @param ScanRange    扫描半径（<=0 时取默认 = SightRadius * 2）
	 * @return 最近的敌方 Character, 没找到返回 nullptr
	 */
	UFUNCTION(BlueprintCallable, Category = "AI")
	AActor* ScanForNearestEnemy(ACharacter* MyCharacter, float ScanRange = -1.f);

protected:
	virtual void BeginPlay() override;
	virtual void OnPossess(APawn* InPawn) override;
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
	virtual void Tick(float DeltaSeconds) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI")
	UAIPerceptionComponent* AIPerception;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI")
	TObjectPtr<UAIRuntimeConfigComponent> RuntimeConfig;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "AI")
	TObjectPtr<UAIProfileAsset> CurrentProfile;

	/**
	 * 【Phase 1 重构】感知触发的单一入口
	 * 设计:
	 *   - 引擎已通过 IGenericTeamAgentInterface 把敌我对错处理完毕, 这里不再做 Cast<ABaseCharacter>
	 *   - 极近距离时把目标覆盖写入 BB 的 ImmediateTarget Key (由 Subsystem 解析名)
	 *   - 距离阈值从 RuntimeConfig->GetScaledCombat().AttackRange 读取 (v5 已合并 OverrideBTDistance, 2026.07.07)
	 */
	UFUNCTION()
	virtual void OnTargetDetected(AActor* Actor, FAIStimulus Stimulus);

private:
	void OnProfileLoaded();
	void StartBehaviorTreeFromProfile();
	void RunLegacyBehaviorTree();

	/**
	 * 【P0 2026.07.06】延迟启动 BT 的统一入口
	 * 解决: AI 出生立即 RunBT, 玩家还在大厅就追玩家 (用户原话"还没点开始游戏 AI 就开始攻击人")
	 * 策略:
	 *   1. 检查 GameMode 是否已经 BattleInProgress
	 *      - 是 → 立即 StartBehaviorTreeFromProfile
	 *   2. 否 → 订阅 ARoomGameMode::OnBattleStarted, 收到回调时再启动
	 */
	void TryStartBehaviorTreeOrWaitForBattleStart();

	/**
	 * 【P0 2026.07.06】订阅 OnBattleStarted 后, 委托句柄 (EndPlay 时取消订阅)
	 */
	FDelegateHandle OnBattleStartedHandle;

	/** 【P0 2026.07.06】C++ ↔ BT 通信的攻击状态 (Setter/Getter 在 public) */
	bool bIsCurrentlyAttacking = false;

	/**
	 * 【Phase 2 共用层】按 Profile->Perception 配 AIPerception
	 * 设计: 把"刀战专属"的 ConfigurePerceptionFromConfig 收归 Base
	 *       所有 AI 派生类 (Melee/Zombie/Boss) 都不用再写一遍
	 * 调用时机: OnProfileLoaded 之后, BT 启动之前
	 */
	void ConfigurePerceptionFromConfig();

	/**
	 * 【P0 架构升级 2026.07.03】启动期一次性全景诊断
	 * 输出 4 项关键状态: Profile / 阵营 / 感知 / NavMesh
	 * 任何一项异常用 Error 级别, 方便一次扫描日志看清全局
	 * 调用时机: OnProfileLoaded 末尾 (配置完成后), BeginPlay (兜底)
	 */
	void DiagnoseAndLogBootStatus() const;

	// 【P0 2026.07.08 大厂架构重构 — BT 100% 接管】
	// 删除以下两个函数 (AI 决策 100% 由 BT 树驱动):
	//   - TickChaseFallback         (739 行): C++ 距离决策 + AddMovementInput 兜底
	//   - UpdateNearbyThreatByDistance (200 行): BB.NearbyThreat 派生
	// 替代方案 (BT 编辑器配置):
	//   - BTService_UpdateBlackboard (0.1s)  : 写 BB.DistanceToTarget / BB.HealthPercent
	//   - BTService_RefreshTarget    (0.3s)  : 失效目标扫描兜底
	//   - UE 原生 Compare BB Entries     : 距离区间决策 (Chase/Attack/Retreat 分支)
	//   - UE 原生 Move To               : 追击 (AcceptanceRadius=AR-10)
	//   - UE 原生 Move To               : 撤退 (AcceptanceRadius=AR+30)
	// 风险: BT 资产配置错误 → AI 静止 — 这是设计师责任, C++ 不救 (用户 v18 决策 "最纯 BT 架构")

	/** FOnAIBehaviorConfigLoaded 句柄, 跨 OnProfileLoaded 暂存 */
	FOnAIBehaviorConfigLoaded ProfileLoadedDelegate;

protected:
	/**
	 * 【Phase 1】派生类钩子入口
	 * 设计: 派生类 SetupMeleeAI 重写感知配置后, 必须调用本方法触发 BT 启动
	 * 行为:
	 *   - 已开始 BT (bBehaviorTreeStarted): 直接返回
	 *   - 未开始: 走 Config 同步+异步加载路径
	 */
	void StartBehaviorTreeFromConfig();

private:
	bool bBehaviorTreeStarted = false;

	/**
	 * 【P0 2026.07.07 大厂架构重构】冷却锁步状态 (与 LockMovementForCooldown 配对使用)
	 *
	 * 设计: 记录上一次锁步状态, 避免每帧重复设 MaxWalkSpeed/bIsMovementLocked
	 *       (每次设置都会触发 CharacterMovement 的内部状态重算, 微秒级开销但能省就省)
	 *
	 * 状态语义:
	 *   - bMovementLockedForCooldown=true: 当前处于"冷却锁步"状态 (MaxWalkSpeed=0 + bIsMovementLocked=true)
	 *   - bMovementLockedForCooldown=false: 正常移动状态
	 *
	 * 【P0 2026.07.08 大厂减熵】以下成员已删除 (TickChaseFallback 不再存在, 失去调用方):
	 *   - TimeSinceLastChaseCheck     (TickChaseFallback 节流)
	 *   - TimeSinceAttackingStarted   (Bailout 计时, TickChaseFallback 内用)
	 *   - CachedChaseTarget           (TickChaseFallback 目标缓存)
	 *   - BeginPlayTimeStamp          (TickChaseFallback 启动期报警)
	 *   - TimeSinceLastDecisionChange (Refractory Period 200ms)
	 *   - LastDecisionForRefractory   (Refractory Period 历史)
	 *   - bHasAnyPriorDecision        (Refractory Period 状态)
	 *
	 *   抗抖动 (原 Refractory Period 200ms) 改由 BT 编辑器层承担:
	 *     - UE 原生 Compare BB Entries 区间 (±10cm)   → Layer 3 死区迟滞
	 *     - BTService_UpdateDistance 0.1s 周期          → Layer 4 节流
	 *   不在 C++ 引擎层做 Refractory — 让决策层 (BT) 单点维护
	 */
	bool bMovementLockedForCooldown = false;
};
