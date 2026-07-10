// 版权声明: 在项目设置的描述页面填写您的版权信息。
//
// 文件: AIAttackComponent.h
// 作用: AI 攻击子系统组件 — 从 ABaseCharacter 拆出 (Phase 2.2 重构)
//
// 创建日期: 2026.07.12
// 负责人: <架构师>
// 关联 Phase: Phase 2 — BaseCharacter.cpp 巨型文件拆分
//
// 拆分来源: BaseCharacter.cpp 第 1568-1801 行 (OnAIRequestAttack_Simple) +
//           第 1825-1933 行 (OnAIAttackMontageEnded) +
//           第 1950-1962 行 (Server_PlayAttackAnim_Implementation) +
//           第 1971-1989 行 (Multicast_PlayAttackAnim_Implementation) +
//           第 2010-2122 行 (Server_ReportAIAttackHit_Implementation + _Validate)
//           + 字段 LastAIAttackTimeSeconds / CachedAIMontage / bIsWaitingForAIMontageCallback

#pragma once

// ==========================================
// 头文件包含说明
// ==========================================
// UE 引擎核心最小化头文件
#include "CoreMinimal.h"

// 引入 UE 原生 UActorComponent 基类
#include "Components/ActorComponent.h"

// UE 自动生成的头文件 (必须放在最后)
// 【重要】UE 5.6 UHT 严格要求: bare filename, 不能带目录前缀
// 错误示例: #include "Combat/AIAttackComponent.generated.h"
// 正确示例: #include "AIAttackComponent.generated.h"
#include "AIAttackComponent.generated.h"

// 前置声明 (加快编译, 避免循环包含)
class ABaseCharacter;
class ABaseAIController;
class UAnimMontage;


/**
 * @class UAIAttackComponent
 * @brief AI 攻击子系统 — 从 ABaseCharacter 拆出的业务组件 (Phase 2.2 重构)
 *
 * 职责 (In Scope):
 *   - AI 专用轻攻击入口 (OnAIRequestAttack_Simple)
 *   - AI 攻击蒙太奇生命周期管理 (播放 + 结束回调 + 缓存验证)
 *   - 【v40.4 删除】AI 攻击节流 (LastAIAttackTimeSeconds 本地兜底) - 统一走 BT Decorator
 *   - AI 攻击伤害上报 (Server_ReportAIAttackHit — 备用通道)
 *   - 玩家/AI 共用的攻击动画网络同步 (Server_PlayAttackAnim / Multicast_PlayAttackAnim)
 *   - AI 通道的伤害来源切换 (SetAttackerIsAI 转发)
 *
 * 不负责 (Out of Scope):
 *   - 玩家连击状态机 (PlayerComboComponent 独立组件)
 *   - AI 死亡 / 复活 / 阵营管理 (仍属于 BaseCharacter / BaseAIController)
 *   - BT 决策 (BTTask 走 AIController, 不感知本组件)
 *   - 武器 Trace / Tick (BaseWeapon 自治)
 *
 * 架构 (大厂原则 v32 落地):
 *   - 事件驱动 (vs 旧版轮询): AI 不依赖 BT 距离检查决定动画生命周期
 *   - 单一真理源: 伤害来源决策在 Owner Character (BaseCharacter::bIsCurrentlyAttackerAI)
 *   - 零兜底: 死亡/无武器/无法解析蒙太奇 → 显式 Log + return false
 *   - 职责对等: 与 PlayerComboComponent 完全解耦, 玩家改连击不影响 AI
 *
 * 数据流 (v32 终版):
 *   BTTask 调用 → OnAIRequestAttack_Simple (本地时间戳节流)
 *     → AIAttackMontageResolver 解析蒙太奇 (Level 0 唯一路径, 零兜底)
 *     → PlayAnimMontage 本机播放 Combo1 段
 *     → 绑 UAnimInstance::OnMontageEnded (RemoveDynamic + IsAlreadyBound + AddDynamic)
 *     → CachedAIMontage + bIsWaitingForAIMontageCallback = true (缓存本次蒙太奇)
 *     → Server_PlayAttackAnim (RPC 走网络同步)
 *     → SetAttackerIsAI(true) + StartWeaponTrace (命中后走 Server_ReportHit 路径)
 *     → (蒙太奇自然结束) → OnAIAttackMontageEnded → 解锁 AIController + 关闭 trace
 */
UCLASS(ClassGroup = ("MetalSlug|Combat"), meta = (BlueprintSpawnableComponent),
       DisplayName = "AI Attack Component")
class METALSLUG01_API UAIAttackComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	/**
	 * 构造函数: 启用 Tick (本组件不需要 Tick, 但保留扩展位)
	 */
	UAIAttackComponent();

	// 【2026.07.12 P0 重构】Friend 双向授权:
	//   - ABaseCharacter 调本组件 protected 方法 (Server_PlayAttackAnim_Implementation 等)
	friend class ABaseCharacter;

	// ==========================================
	// 1. 生命周期
	// ==========================================

	/**
	 * UE 原生生命周期: 组件被附加到 Actor 时调用
	 * 用途: 【v40.6 P0】空实现 — 不再缓存 OwnerCharacter (改用 ResolveOwnerCharacter() 按需解析)
	 */
	virtual void BeginPlay() override;

	/**
	 * UE 原生生命周期: 组件即将被销毁时调用
	 * 用途: 显式解绑 UAnimInstance::OnMontageEnded 回调, 防残留
	 *       (UE 销毁组件会自动解绑, 但显式 EndPlay 是工业级规范)
	 */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;


	// ==========================================
	// 2. AI 攻击入口 (供 BTTask 调用)
	// ==========================================

	/**
	 * AI 专用轻攻击入口 (事件驱动版本 — 不复用玩家连击状态机)
	 *
	 * 根因 (旧设计问题 2026.07.06):
	 *   OnAIRequestAttack -> LightAttack_Pressed -> 设 bIsMovementLocked=true + MaxWalkSpeed=0
	 *   -> EndAttackState (由动画蓝图 NotifyEnd 调用) -> 清除锁
	 *   问题: AI 攻击不走动画蓝图的 NotifyEnd 回调链, EndAttackState 从未被调用
	 *   结果: bIsMovementLocked 永远为 true, AI 永远无法移动
	 *
	 * 大厂设计 (v40.4):
	 *   - 走 AIAttackMontageResolver 职责链 (Level 0 唯一路径, 零兜底)
	 *   - 绑 UAnimInstance::OnMontageEnded (事件驱动, 蒙太奇自然结束才解锁)
	 *   - SetAttackerIsAI(true) + StartWeaponTrace (玩家和 AI 共用 trace 扣血)
	 *
	 * 【v40.7 P0 修复】新增显式 Owner 参数:
	 *   - Session1.log 根因: BT 调用路径下 Component::GetOwner() 返回 CDO Archetype,
	 *     导致 Owner->GetCurrentWeapon() 永远是 BPGC_ARCH_FOR_CDO_BaseCharacter_1 的 null
	 *   - UE 5.6 已知行为: 部分 BT/C++ 调用上下文中, UActorComponent::GetOwner() 可能返回
	 *     Archetype 而非真实 Pawn, 这是引擎级限制, 不是我们代码 bug
	 *   - 大厂修复: 调用方 (BTTask / BaseCharacter 转发壳) 把真实 Pawn 显式传入,
	 *     内部不再调用 GetOwner(), 彻底绕过 CDO 问题
	 *
	 * @param InOwnerCharacter 真实 Pawn (必须是有效实例, 不能是 CDO/Archetype)
	 *                          由 BTTask_PlayAttackMontage / BaseCharacter::OnAIRequestAttack_Simple 传入
	 * @return true=攻击发起成功; false=任一前置检查失败 (死/无武器/蒙太奇解析失败)
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat|AI")
	bool OnAIRequestAttack_Simple(ABaseCharacter* InOwnerCharacter);


	// ==========================================
	// 3. AI 攻击蒙太奇结束回调 (UFUNCTION + Dynamic Delegate 必须的签名)
	// ==========================================

	/**
	 * AI 攻击蒙太奇结束回调 — 由 UAnimInstance::OnMontageEnded 触发
	 *
	 * 签名约束: 必须 (UAnimMontage*, bool), 因为要绑到
	 *           UAnimInstance::OnMontageEnded (DECLARE_DYNAMIC_MULTICAST_DELEGATE)
	 *           该委托要求 UFUNCTION 才能 AddDynamic
	 *
	 * 职责:
	 *   1. Montage 参数验证 (过滤掉非本组件触发的蒙太奇结束)
	 *   2. 通知 AIController 攻击阶段结束 (SetCurrentlyAttacking(false))
	 *   3. 打断分支 (bInterrupted=true): 立即 SetInAttackCooldown(false) 解锁
	 *   4. 正常分支 (bInterrupted=false): 由 BT Timer 自然清 cooldown
	 *   5. 清理缓存 (CachedAIMontage / bIsWaitingForAIMontageCallback)
	 *   6. 对称关闭 trace (StopWeaponTrace) + 还原攻击者标志 (SetAttackerIsAI(false))
	 *
	 * @param Montage        结束的蒙太奇指针
	 * @param bInterrupted   true=被其他动画/StopMontage 打断; false=自然播完
	 */
	UFUNCTION()
	void OnAIAttackMontageEnded(UAnimMontage* Montage, bool bInterrupted);


	// ==========================================
	// 4. 网络同步 RPC — _Implementation / _Validate (UE 5.6 硬约束: 必须与 UFUNCTION 同名声明)
	// ==========================================
	// 【2026.07.12 P0 重构】RPC 转发壳 (RPC 必须在 ABaseCharacter 上声明, 但实现委托给本组件)
	//   - Server_PlayAttackAnim / Multicast_PlayAttackAnim / Server_ReportAIAttackHit
	//     三个 UFUNCTION 标记的 RPC 已在 ABaseCharacter.h 声明 (line 1190/1199/1228)
	//   - 本类只声明同名同参的 _Implementation 和 _Validate 函数, ABaseCharacter 转发壳会调它们
	//   - 大厂原则: RPC 权威在 Actor, 业务逻辑在 Component (职责分离)

	/**
	 * Server_PlayAttackAnim 实现 (转发壳由 ABaseCharacter 调用)
	 * 服务器端同步锁速 + 广播 Multicast
	 */
	void Server_PlayAttackAnim_Implementation(bool bIsHeavy, int32 InComboIndex);

	/**
	 * Server_PlayAttackAnim 验证 (转发壳由 ABaseCharacter 调用)
	 * 返回 false 服务器会断开连接, 拒绝 RPC
	 */
	bool Server_PlayAttackAnim_Validate(bool bIsHeavy, int32 InComboIndex);

	/**
	 * Multicast_PlayAttackAnim 实现 (转发壳由 ABaseCharacter 调用)
	 * 全频道广播播放动画
	 */
	void Multicast_PlayAttackAnim_Implementation(bool bIsHeavy, int32 InComboIndex);

	/**
	 * Server_ReportAIAttackHit 实现 (转发壳由 ABaseCharacter 调用)
	 * AI 攻击伤害上报 (服务器权威)
	 */
	void Server_ReportAIAttackHit_Implementation(AActor* HitActor, float Damage);

	/**
	 * Server_ReportAIAttackHit 验证 (转发壳由 ABaseCharacter 调用)
	 * 服务器校验目标合法 + 伤害值合理
	 */
	bool Server_ReportAIAttackHit_Validate(AActor* HitActor, float Damage);


	// ==========================================
	// 6. 内部状态字段 (Phase 2.2 从 BaseCharacter 迁移)
	// ==========================================
private:
	/**
	 * 【v40.4 删除】上次 AI 攻击的时间戳 (秒, FPlatformTime::Seconds)
	 *
	 * 历史 (v22-v40.3) 用途: OnAIRequestAttack_Simple 入口的本地节流兜底
	 *   - 即使 BTTask 的 bHasAttackToken + Cooldown Timer 全部失效,
	 *     Character 自己手里还有这道墙, 防止 AI 持续连击到玩家
	 *
	 * v40.4 删除原因 (大厂原则 - 单一节流点):
	 *   - BTDecorator_CooldownReady 已做实时冷却 (World.Time vs BB.CooldownEndTime)
	 *   - 双层节流 = 重复架构, BT 配错时被 C++ 节流"再撑一道墙"
	 *   - 0 兜底原则: BT 配错应立即暴露 (ExecuteTask 返回 Failed), 不允许 C++ 节流掩盖
	 *
	 * 删除时机: v40.4 重构 OnAIRequestAttack_Simple 时一并删除
	 */

	/**
	 * 上次 AI 攻击请求时播放的蒙太奇指针
	 * 用于在 OnMontageEnded 回调时验证 Montage 是否"我们自己触发的"
	 * (UAnimInstance::OnMontageEnded 是 multicast, 所有蒙太奇结束都会广播一次)
	 *
	 * 由 UPROPERTY 持有, 防止 GC 回收导致野指针
	 */
	UPROPERTY()
	TObjectPtr<UAnimMontage> CachedAIMontage = nullptr;

	/**
	 * 是否在等待 AI 攻击蒙太奇回调
	 * 设计: 每次启动新攻击时设 true, 收到正确 Montage 的回调后 false
	 *       超时机制: TickChaseFallback 通过 SetCurrentlyAttacking 5s 兜底
	 *
	 * 用 bool 比再创建 FName 标识简单 — 单 AI 单任务
	 */
	UPROPERTY()
	bool bIsWaitingForAIMontageCallback = false;


	// ==========================================
	// 7. Owner 引用解析 (v40.6 — 按需 GetOwner() + Cast, 不缓存)
	// ==========================================
private:
	/**
	 * 【v40.6 P0 修复】删除 BeginPlay 缓存的 OwnerCharacter 字段
	 *
	 * 历史 (v22-v40.5) 反模式:
	 *   - BeginPlay 里: OwnerCharacter = Cast<ABaseCharacter>(GetOwner())
	 *   - 假设: 组件 BeginPlay 时 Owner 必然有效
	 *   - 真实 (BP archetype 问题): v36 已发现 — BP 子类可能让组件 BeginPlay 不按顺序/不运行
	 *   - 结果: OwnerCharacter 永久 null → AIAttack 永远失败
	 *   - 日志证据: BP_GruntAI_C 的 CombatDeath/WeaponAttach BeginPlay 都跑,
	 *     AIAttackComponent BeginPlay 一次都没跑过
	 *
	 * v40.6 修复 (大厂原则):
	 *   - 删除缓存字段 — 真理源 = GetOwner()
	 *   - 每次需要时 lazy resolve: ResolveOwnerCharacter() 调 GetOwner() + Cast
	 *   - 与 v38 BaseCharacter::ResolveComponent<T> 模板同模式
	 *   - 失败 → Log Error + return nullptr (零兜底)
	 */

	/**
	 * 按需解析 Owner Character (GetOwner() + Cast<ABaseCharacter>)
	 * @return 有效指针; nullptr 表示组件未挂载到 ABaseCharacter 上
	 */
	ABaseCharacter* ResolveOwnerCharacter() const;

	/**
	 * 【v42 P0 修复】从 AIRuntimeConfigComponent 恢复 MaxWalkSpeed
	 *
	 * 根因: Server_PlayAttackAnim_Implementation 中设 MaxWalkSpeed=0 锁速,
	 *        但 OnAIAttackMontageEnded 中没有恢复速度
	 *        → AI 攻击后 MaxWalkSpeed=0 → 物理引擎 fallback 到默认值(玩家速度)
	 *
	 * 大厂原则 - 职责对等:
	 *   - 玩家路径: PlayerComboComponent::EndAttackState 恢复速度
	 *   - AI 路径: 本函数恢复速度 (与玩家路径对称)
	 *
	 * @param OwnerCharacter 有效 Pawn
	 */
	void RestoreMaxWalkSpeedFromConfig(ABaseCharacter* OwnerCharacter);


};