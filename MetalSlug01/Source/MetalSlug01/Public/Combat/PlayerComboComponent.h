// 版权声明: 在项目设置的描述页面填写您的版权信息。
//
// 文件: PlayerComboComponent.h
// 作用: 玩家连击状态机子组件 — 从 ABaseCharacter 拆出 (Phase 2.1 重构)
//
// 创建日期: 2026.07.12
// 负责人: <架构师>
// 关联 Phase: Phase 2 — BaseCharacter.cpp 巨型文件拆分

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
// 错误示例: #include "Combat/PlayerComboComponent.generated.h"
// 正确示例: #include "PlayerComboComponent.generated.h"
#include "PlayerComboComponent.generated.h"

// 前置声明 (加快编译, 避免循环包含)
class ABaseCharacter;
class ABaseWeapon;
class UInputAction;
class UAnimMontage;
class UPlayerConfigAsset;
class ARoomGameMode;


/**
 * @class UPlayerComboComponent
 * @brief 玩家连击状态机 — 从 ABaseCharacter 拆出的业务组件 (Phase 2.1 重构)
 *
 * 职责 (In Scope):
 *   - 玩家轻击/重击/技能输入的状态机 (bIsAttacking / bCanReceiveInput / bSaveAttack / bIsHoldingLightAttack / ComboIndex)
 *   - 连击序列的执行 (Combo1/Combo2 切换)
 *   - 攻击期间的移动锁 (bIsMovementLocked + MaxWalkSpeed)
 *   - 通过 Input Action 引用绑定 (由 BaseCharacter::SetupPlayerInputComponent 传入)
 *   - 攻击者 AI 标志的转发 (委托给 BaseCharacter)
 *
 * 不负责 (Out of Scope):
 *   - 网络同步 (Server_PlayAttackAnim / Multicast_PlayAttackAnim 仍属于 BaseCharacter)
 *   - AI 攻击 (OnAIRequestAttack_Simple → AIAttackComponent)
 *   - 武器切换 / 拾取 / 生成 (仍属于 BaseCharacter)
 *   - 死亡 / 复活 (仍属于 BaseCharacter)
 *
 * 架构 (大厂原则 v32 落地):
 *   - 单一真理源: 状态机字段 (bIsAttacking/ComboIndex 等) 全部位于本组件
 *   - 零兜底: 死亡/无武器/下蹲 等前置检查显式 return false, 不静默跳过
 *   - 职责对等: 玩家路径与 AIAttackComponent 完全解耦, 互不干扰
 *
 * 数据流:
 *   输入 (Enhanced Input) → SetupPlayerInputComponent 绑定 → LightAttack_Pressed / LightAttack_Released
 *   → ExecuteComboSequence → Server_PlayAttackAnim (RPC) → Multicast_PlayAttackAnim → 所有客户端播动画
 *   → Anim Notify State (Begin/End) → EnableComboWindow / CheckCombo → ExecuteComboSequence
 *   → Anim Notify End → EndAttackState (清理锁)
 */
UCLASS(ClassGroup = ("MetalSlug|Combat"), meta = (BlueprintSpawnableComponent),
       DisplayName = "Player Combo Component")
class METALSLUG01_API UPlayerComboComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	/**
	 * 构造函数: 创建默认子对象, 初始化状态字段
	 */
	UPlayerComboComponent();

	// ==========================================
	// 1. 生命周期
	// ==========================================

	// 【2026.07.12 P0 重构】Friend 双向授权 (大厂原则 - 精确授权):
	//   - 上面 friend class ABaseCharacter: ABaseCharacter 可访问本组件 protected 方法 (ExecuteComboSequence 等)
	//   - 这两个组件相互协作, 必须双向 friend (Actor 调 Component + Component 调 Actor)
	friend class ABaseCharacter;

	/**
	 * UE 原生生命周期: 组件被附加到 Actor 时调用
	 * 用途: 【v40.6 P0】空实现 — 不再缓存 OwnerCharacter (改用 ResolveOwnerCharacter() 按需解析)
	 */
	virtual void BeginPlay() override;

	/**
	 * UE 原生生命周期: 组件即将被销毁时调用
	 * 用途: 显式清理状态机字段, 防边缘 case 残留
	 * 大厂原则: 即便 UE 内部会自动清理, 显式 EndPlay 也是工业级规范
	 */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;


	// ==========================================
	// 2. Input Action 注入接口 (由 BaseCharacter 初始化时调用)
	// ==========================================

	/**
	 * 【Phase 2.1 拆分关键】设置 Input Action 引用
	 *
	 * 拆分背景: Input Action (LightAttackAction/HeavyAttackAction/IA_MotherSkill) 原是 ABaseCharacter 的 UPROPERTY
	 *          拆分后, 由 ABaseCharacter::SetupPlayerInputComponent 调本方法注入, 而不是组件自己持有 (职责单一)
	 *
	 * 大厂原则:
	 *   - Input Action 仍然是 Character 层的资产 (BP 配置在 Character 上)
	 *   - 组件只持有引用, 不创建/复制资产
	 *   - nullptr 容忍: Character 可能没配某个 Action, 调用方按需检查
	 *
	 * @param InLightAttackAction  轻击 Action (可为 nullptr)
	 * @param InHeavyAttackAction  重击 Action (可为 nullptr)
	 * @param InIA_MotherSkill     技能 Action (可为 nullptr)
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat|Player|Combo")
	void SetInputActions(UInputAction* InLightAttackAction,
	                     UInputAction* InHeavyAttackAction,
	                     UInputAction* InIA_MotherSkill);

	/**
	 * 【Phase 2.1 拆分关键】绑定输入到 Enhanced Input Component
	 *
	 * 调用方: ABaseCharacter::SetupPlayerInputComponent 末尾
	 *         (这是原绑定逻辑的搬迁点 — 由 Character 接收 EnhancedInputComponent 后委托给本组件)
	 *
	 * @param EnhancedInputComponent UE 提供的增强输入组件 (由 SetupPlayerInputComponent 传入)
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat|Player|Combo")
	void BindInputActions(UEnhancedInputComponent* EnhancedInputComponent);


	// ==========================================
	// 3. 玩家输入入口回调 (Enhanced Input → C++)
	// ==========================================

	/**
	 * 轻击按下事件 (Enhanced Input Triggered Started)
	 *
	 * 完整流程:
	 *   1. 死亡/无武器检查 → 显式 return (零兜底)
	 *   2. 下蹲攻击权限拦截 (如果武器不允许下蹲攻击)
	 *   3. 记录 bIsHoldingLightAttack = true
	 *   4. 根据武器 bCanMoveWhileLightAttack 决定是否锁步
	 *   5. 第一刀起手 (bIsAttacking=false): 初始化状态机 → 调 ExecuteComboSequence
	 *   6. 连击区间 (bCanReceiveInput=true): 记录 bSaveAttack = true
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat|Player|Combo")
	void LightAttack_Pressed();

	/**
	 * 轻击松开事件 (Enhanced Input Triggered Completed)
	 *
	 * 流程: 仅设 bIsHoldingLightAttack = false (记录松手状态, 给 CheckCombo 终极结算用)
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat|Player|Combo")
	void LightAttack_Released();

	/**
	 * 重击事件 (单击)
	 *
	 * 注意: 当前版本 HeavyAttack 实现被注释, 仅留下权限检查
	 * 大厂原则 - 零兜底: 死/无武器/不允许下蹲攻击 → 显式 return
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat|Player|Combo")
	void HeavyAttack();

	/**
	 * 释放技能事件
	 *
	 * TODO: 技能系统具体实现 (暂留空, 与原 BaseCharacter 行为一致)
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat|Player|Combo")
	void UseSkill();


	// ==========================================
	// 4. 动画通知 (Anim Notify) 专用接口
	// ==========================================

	/**
	 * 供动画蓝图区间 (Anim Notify State) 调用的接口
	 * 区间开始时触发 (开启连招窗口)
	 *   - bCanReceiveInput = true (绿灯亮起)
	 *   - bSaveAttack = false (清空上轮缓存)
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat|Player|Combo")
	void EnableComboWindow();

	/**
	 * 区间结束时触发 (检查缓存区是否有缓存攻击)
	 * 终极结算: 如果有缓存的点击或玩家正死死按住左键, 则跳转下一段
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat|Player|Combo")
	void CheckCombo();

	/**
	 * 动画彻底结束时触发
	 * 彻底收招, 解开所有锁 (状态机字段 + bIsMovementLocked + MaxWalkSpeed)
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat|Player|Combo")
	void EndAttackState();


	// ==========================================
	// 5. 攻击者身份转发 (单一真理源在 BaseCharacter, 本组件只做转发)
	// ==========================================

	/**
	 * 设置攻击者是否 AI (转发到 BaseCharacter::SetAttackerIsAI)
	 *
	 * 大厂原则 - 单一真理源:
	 *   - bIsCurrentlyAttackerAI 在 BaseCharacter 上 (供 BaseWeapon::Tick 读取)
	 *   - 本组件不持有副本, 只做委托调用
	 *   - 玩家路径默认 false, AI 路径在 AIAttackComponent 中设 true
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat|Player|Combo")
	void SetAttackerIsAI(bool bIsAI);

	/**
	 * 查询攻击者是否 AI (转发到 BaseCharacter::IsAttackerAI)
	 */
	UFUNCTION(BlueprintPure, Category = "Combat|Player|Combo")
	bool IsAttackerAI() const;


	// ==========================================
	// 6. 工具接口
	// ==========================================

	/**
	 * 获取当前武器 (转发到 BaseCharacter::GetCurrentWeapon)
	 *
	 * 大厂原则 - 零跨边界:
	 *   - 不暴露 BaseCharacter 的 protected CurrentWeapon 字段
	 *   - 通过 BaseCharacter::GetCurrentWeapon() 公开 getter 访问
	 *   - 单一权威入口 (其他人也能调这个 getter, 行为一致)
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Combat|Player|Combo")
	ABaseWeapon* GetCurrentWeapon() const;

	/**
	 * 【v56.5 大厂架构新增】获取玩家配置资产
	 *
	 * 路径: RoomGameMode->PlayerConfigAsset
	 *
	 * 大厂原则 - 单一真理源:
	 *   - 玩家移动速度从 DA_PlayerConfigAsset.MaxWalkSpeed 读取
	 *   - 不在组件内硬编码
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Combat|Player|Combo")
	UPlayerConfigAsset* GetPlayerConfigAsset() const;

	/**
	 * 【2026.07.12 P0 重构】状态机外部只读访问
	 * 给 BaseCharacter::Tick 诊断日志等场景用, 业务逻辑不应依赖此方法
	 *
	 * 大厂原则 - 显式查询优于字段穿透:
	 *   - 字段 bIsAttacking 是 protected, 外部不能直接读
	 *   - 提供 BlueprintPure getter, 编译期 + BP 双层保护
	 */
	UFUNCTION(BlueprintPure, Category = "Combat|Player|Combo")
	bool IsAttacking() const { return bIsAttacking; }


	// ==========================================
	// 7. 状态机执行核心 (ExecuteComboSequence — 内部使用)
	// ==========================================
protected:
	/**
	 * 连招核心执行逻辑
	 *
	 * 工业级做法: 所有连招全在第 0 个蒙太奇里
	 * 智能拼接要跳转的片段名字 (Combo1 或 Combo2, 由 ComboIndex 决定)
	 * 调用 Server_PlayAttackAnim 走网络同步 (BaseCharacter 的 RPC)
	 *
	 * 【2026.07.12 P0 修复】主动开 trace + 绑 OnMontageEnded (不再依赖 BP ANS_MeleeTrace)
	 *
	 * 注意: 不在头文件暴露 UFUNCTION (这是内部串联, 没必要 BP 化)
	 */
	void ExecuteComboSequence();

	/**
	 * 【v93 大厂架构新增】母体左键攻击序列 — 无武器也能攻击
	 *
	 * 调用链路 (玩家母体左键):
	 *   UPlayerComboComponent::LightAttack_Pressed (bIsMother 分支)
	 *   → ExecuteMotherAttackSequence (本函数)
	 *   → 调 Server_PlayAttackAnim RPC (复用现有 RPC, 不新增)
	 *   → Multicast_PlayAttackAnim (所有客户端播蒙太奇)
	 *
	 * 镜像对称:
	 *   - 玩家武器连击: ExecuteComboSequence (武器驱动, Combo1/Combo2 切换)
	 *   - 玩家母体攻击: ExecuteMotherAttackSequence (母体 Pawn 驱动, 单一蒙太奇, 无连击)
	 *
	 * 大厂原则 — 不重复架构:
	 *   - 复用现有 Server_PlayAttackAnim/Multicast_PlayAttackAnim RPC (单一真理源)
	 *   - 不新增 RPC, 不新增 Multicast 函数
	 *   - 客户端 Implementation 根据 bIsMother 选 Montage 来源 (武器 vs MotherAttackMontage)
	 *
	 * 零兜底:
	 *   - MotherAttackMontage 未配 → Log Error + return (BP 配置错必须修复)
	 *
	 * 状态机:
	 *   - 母体没有连击系统 (单一蒙太奇, 无 bSaveAttack / bCanReceiveInput)
	 *   - 只设 bIsAttacking=true → 蒙太奇结束 → EndAttackState 清理
	 */
	void ExecuteMotherAttackSequence();

	// ============================================================
	// 【v93.2 大厂架构 — 母体复用 Melee 缝合算法】玩家母体攻击命中上报
	// ============================================================
	// 复用动机 (用户需求 2026.07.25):
	//   - 母体复用 ANS_MeleeTraceState + MeleeSwStrategy (零重复)
	//   - 命中 RPC 走 Owner->Server_ReportMotherAttackHit → BaseCharacter 转发壳 → 本函数 (玩家路径)
	//   - 命中后行为: 调 RoomMotherMutationSubsystem::MutateCharacterToMother (大厂复用)
	//
	// 与 UAIAttackComponent::Server_ReportMotherAttackHit_Implementation 对称:
	//   - 防御层同构 (HitActor 有效 / 自伤 / 友军 / 已死 / 已母体)
	//   - 唯一区别: 玩家路径 IsAttackerAI()=false, AI 路径 = true (决策由 BaseCharacter 转发壳做)
	void Server_ReportMotherAttackHit_Implementation(AActor* HitActor);

	/**
	 * 【2026.07.12 P0 新增】蒙太奇结束回调 (替代 BP ANS_MeleeTrace NotifyEnd)
	 *
	 * 触发场景:
	 *   - 蒙太奇自然播完 → 引擎回调 → OnPlayerAttackMontageEnded(false)
	 *   - 蒙太奇被打断 (切换段数 / 死亡 / 退刀) → OnPlayerAttackMontageEnded(true)
	 *
	 * 行为:
	 *   - StopWeaponTrace (服务器权威)
	 *   - EndAttackState (解状态锁 + 解移动锁)
	 *   - 走 C++ 路径, 不再依赖 BP 蓝图编译成功
	 *
	 * UFUNCTION 必需 (Dynamic Delegate 要求)
	 */
	UFUNCTION()
	void OnPlayerAttackMontageEnded(UAnimMontage* Montage, bool bInterrupted);

	/**
	 * 【2026.07.12 P0 新增】双绑 + 缓存蒙太奇 (大厂原则 - 防 Inst 状态异常)
	 *
	 *   - RemoveDynamic 先解绑 (幂等, 不存在 binding 静默跳过)
	 *   - IsAlreadyBound 检查 (双保险, 防止 Inst 内部状态异常)
	 *   - CachedPlayerMontage 记录本次蒙太奇 (过滤 multicast OnMontageEnded 干扰)
	 */
	void BindMontageEndCallback(UAnimMontage* InMontage);

	/** 缓存正在播放的玩家攻击蒙太奇 — 用于回调时判断是不是"我触发的蒙太奇" */
	UPROPERTY(Transient)
	TObjectPtr<UAnimMontage> CachedPlayerMontage = nullptr;


	// ==========================================
	// 8. 内部状态机字段 (Phase 2.1 从 BaseCharacter 迁移)
	// ==========================================
private:
	/**
	 * 全局攻击锁: 正在挥刀吗？
	 * 写入: LightAttack_Pressed 第一刀起手设 true, EndAttackState 设 false
	 */
	bool bIsAttacking = false;

	/**
	 * 绿灯亮起: 现在处于输入区间内吗? (连招窗口)
	 * 写入: EnableComboWindow 设 true, CheckCombo / EndAttackState 设 false
	 */
	bool bCanReceiveInput = false;

	/**
	 * 缓存区: 玩家在这个区间内点击过鼠标吗？
	 * 写入: LightAttack_Pressed 连击区间设 true, EnableComboWindow / CheckCombo / EndAttackState 清 false
	 */
	bool bSaveAttack = false;

	/**
	 * 按住状态: 玩家是一直死死按着左键没松吗？
	 * 写入: LightAttack_Pressed 设 true, LightAttack_Released 设 false
	 * 用途: CheckCombo 终极结算时使用
	 */
	bool bIsHoldingLightAttack = false;

	/**
	 * 当前连招段数 (1 或 2, 默认 1)
	 * 写入: LightAttack_Pressed 第一刀起手设 1, CheckCombo 在 1/2 之间翻转
	 *        EndAttackState 重置为 1
	 */
	int32 ComboIndex = 1;


	// ==========================================
	// 9. Input Action 引用 (由 BaseCharacter 注入, 不在 BP 默认值创建)
	// ==========================================
private:
	/**
	 * 轻击 Input Action
	 * 由 SetInputActions 注入, 来自 ABaseCharacter::LightAttackAction
	 */
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> LightAttackAction = nullptr;

	/**
	 * 重击 Input Action
	 * 由 SetInputActions 注入, 来自 ABaseCharacter::HeavyAttackAction
	 */
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> HeavyAttackAction = nullptr;

	/**
	 * 技能 Input Action
	 * 由 SetInputActions 注入, 来自 ABaseCharacter::IA_MotherSkill
	 */
	UPROPERTY(Transient)
	TObjectPtr<UInputAction> IA_MotherSkill = nullptr;


	// ==========================================
	// 10. Owner 引用解析 (v40.6 — 按需 GetOwner() + Cast, 不缓存)
	// ==========================================
private:
	/**
	 * 【v40.6 P0 修复】按需解析 Owner Character (不再缓存)
	 *
	 * 根因 (Session1.log 2026.07.13):
	 *   - BP archetype 让某些 Component 的 BeginPlay 不运行
	 *     (CombatDeath/WeaponAttach 都跑了, AIAttack/PlayerCombo 没跑, 日志证据)
	 *   - 旧代码 BeginPlay 里缓存 OwnerCharacter → 永久 null → 业务永远失败
	 *
	 * v40.6 大厂原则:
	 *   - 真理源 = GetOwner(), 不缓存
	 *   - 每次需要时调 ResolveOwnerCharacter() lazy resolve
	 *   - 与 AIAttackComponent v40.6 同模式 (参考 .h 文件)
	 */
	ABaseCharacter* ResolveOwnerCharacter() const;
};