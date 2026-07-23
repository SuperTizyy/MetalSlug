// 版权声明: 在项目设置的描述页面填写您的版权信息。
//
// 文件: PlayerComboComponent.cpp
// 作用: 玩家连击状态机子组件实现 — 从 ABaseCharacter 拆出 (Phase 2.1 重构)
//
// 创建日期: 2026.07.12
// 关联 Phase: Phase 2 — BaseCharacter.cpp 巨型文件拆分
//
// 拆分来源: BaseCharacter.cpp 第 1331-1524 行 (LightAttack_Pressed / LightAttack_Released /
//           ExecuteComboSequence / HeavyAttack / UseSkill / EnableComboWindow /
//           CheckCombo / EndAttackState) + 相关字段 (bIsAttacking / bCanReceiveInput /
//           bSaveAttack / bIsHoldingLightAttack / ComboIndex)
//
// 大厂原则 (Phase 2.1 落地):
//   - 单一真理源: 状态机字段只在组件内, BaseCharacter 同步删除 (Phase 2.2)
//   - 零兜底: 死亡/无武器/下蹲 权限检查显式 return, 不静默跳过
//   - 职责对等: 玩家路径独立组件, 与 AIAttackComponent 完全解耦

// ==========================================
// 头文件包含区
// ==========================================
// 引入本组件头文件
#include "Combat/PlayerComboComponent.h"

// 引入 Owner Character
#include "Characters/BaseCharacter.h"

// 引入武器 (访问 CurrentWeapon / GetAttackMontage / bCanMoveWhileLightAttack / bCanAttackWhileCrouched)
#include "Weapons/BaseWeapon.h"

// 引入移动组件 (访问 MaxWalkSpeed, UCharacterMovementComponent 前向声明不够, 必须 include 完整定义)
#include "GameFramework/CharacterMovementComponent.h"

// 引入动画实例 (用于 OnMontageEnded 绑定 — 2026.07.12 P0 修复, 替代 BP ANS_MeleeTrace)
#include "Animation/AnimInstance.h"

// 【v93.2 大厂架构新增】母体命中变母体调用 — RoomMotherMutationSubsystem::MutateCharacterToMother
#include "Systems/Mother/RoomMotherMutationSubsystem.h"
// 【v93.2 大厂架构新增】模式校验 — ARoomGameState::CurrentMatchMode (真理源)
#include "Systems/RoomGameState.h"
// 【v93.2 大厂架构新增】友军伤害守卫 (复用 FFactionTags::CanDamage)
#include "Data/Faction/FactionTags.h"
// 引入动画蒙太奇 (CachedPlayerMontage 类型需要)
#include "Animation/AnimMontage.h"

// 引入增强输入组件 (用于 BindInputActions)
#include "EnhancedInputComponent.h"

// 引入增强输入子系统 (用于触发事件 ETriggerEvent)
#include "InputAction.h"

// UE 引擎核心日志
#include "Engine/Engine.h"

// 【v56.5 大厂架构新增】玩家配置资产 (访问 MaxWalkSpeed)
#include "Data/Config/PlayerConfigAsset.h"

// 【v56.5 大厂架构新增】房间 GameMode (访问 PlayerConfigAsset)
#include "Systems/RoomGameMode.h"


// ==========================================
// 1. 构造函数
// ==========================================

/**
 * UPlayerComboComponent 构造函数
 *
 * 目的: 启用 Tick (本组件不需要 Tick, 但保留扩展位);
 *       初始化状态机字段 (UE 反射默认值)。
 *
 * 设计要点:
 *   - PrimaryComponentTick.bCanEverTick = false (连击状态机不需要 Tick, 输入驱动)
 *   - 所有字段在头文件已给默认值, 构造函数不必重复赋值
 *   - bIsAttacking = false / bCanReceiveInput = false / bSaveAttack = false
 *   - bIsHoldingLightAttack = false / ComboIndex = 1 (符合"第一刀起手"语义)
 */
UPlayerComboComponent::UPlayerComboComponent()
{
	// 组件本身不 Tick — 连击状态机由输入和 Anim Notify 事件驱动, 不需要每帧检查
	PrimaryComponentTick.bCanEverTick = false;
}


// ==========================================
// 2. 生命周期
// ==========================================

/**
 * UPlayerComboComponent::BeginPlay
 *
 * 缓存 Owner Character, 避免后续每次 GetOwner() + Cast 的开销
 *
 * 大厂原则:
 *   - Owner 必须存在 (UE 反射保证: 组件能 BeginPlay 说明 Owner 有效)
 *   - 缓存失败 → Log Warning (不中断流程, 由后续访问点的 nullptr 守卫兜底)
 */
void UPlayerComboComponent::BeginPlay()
{
	Super::BeginPlay();
	// 【v40.6 P0】不再缓存 OwnerCharacter — 改用 ResolveOwnerCharacter() 按需 lazy resolve
}

/**
 * UPlayerComboComponent::ResolveOwnerCharacter — 按需解析 Owner
 *
 * v40.6 新增:
 *   - 真理源 = GetOwner() (UE 标准 API)
 *   - 不缓存 (BP archetype 可能让 BeginPlay 不运行)
 *   - 失败 → Log Error + return nullptr (零兜底)
 */
ABaseCharacter* UPlayerComboComponent::ResolveOwnerCharacter() const
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[PlayerComboComponent] ResolveOwnerCharacter: GetOwner() 返回 null. "
			     "组件可能处于销毁中. 组件类=%s, Owner=%s"),
			*GetClass()->GetName(),
			*GetNameSafe(Owner));
		return nullptr;
	}

	ABaseCharacter* BaseChar = Cast<ABaseCharacter>(Owner);
	if (!BaseChar)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[PlayerComboComponent] ResolveOwnerCharacter: Owner '%s' 不是 ABaseCharacter. "
			     "本组件必须挂在 ABaseCharacter 子类上. 当前 Owner 类型=%s"),
			*Owner->GetName(), *Owner->GetClass()->GetName());
		return nullptr;
	}

	return BaseChar;
}


/**
 * UPlayerComboComponent::EndPlay
 *
 * 显式清理状态机字段, 防止边缘 case 残留
 * (大厂原则 - 工业规范: 显式清理优于依赖 UE 隐式行为)
 */
void UPlayerComboComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 清理状态机 — 防残留导致新关卡/重连时状态错乱
	bIsAttacking = false;
	bCanReceiveInput = false;
	bSaveAttack = false;
	bIsHoldingLightAttack = false;
	ComboIndex = 1;

	// 【v40.6 P0】删除 OwnerCharacter 清空语句 — 字段已删除 (改用 ResolveOwnerCharacter() 按需解析)

	// 清空 Input Action 引用 (避免 transient 引用残留)
	LightAttackAction = nullptr;
	HeavyAttackAction = nullptr;
	UseSkillAction = nullptr;

	Super::EndPlay(EndPlayReason);
}


// ==========================================
// 3. Input Action 注入接口
// ==========================================

/**
 * 设置 Input Action 引用 — 由 ABaseCharacter::SetupPlayerInputComponent 调用
 *
 * 大厂原则 - 拆分约束:
 *   - Input Action 资产本身仍在 ABaseCharacter 上 (BP 配置位置)
 *   - 本组件不创建/复制资产, 只持有引用
 *   - nullptr 容忍: 某个 Action 可能没配, 调用方 BindInputActions 内部检查
 */
void UPlayerComboComponent::SetInputActions(UInputAction* InLightAttackAction,
                                            UInputAction* InHeavyAttackAction,
                                            UInputAction* InUseSkillAction)
{
	LightAttackAction = InLightAttackAction;
	HeavyAttackAction = InHeavyAttackAction;
	UseSkillAction = InUseSkillAction;
}


/**
 * 绑定输入到 Enhanced Input Component — 由 ABaseCharacter::SetupPlayerInputComponent 调用
 *
 * 大厂原则 - 对应原 BaseCharacter::SetupPlayerInputComponent 中的绑定逻辑
 *   原代码 (BaseCharacter.cpp line 1252-1260):
 *     EnhancedInputComponent->BindAction(LightAttackAction, ETriggerEvent::Started,   this, &LightAttack_Pressed);
 *     EnhancedInputComponent->BindAction(LightAttackAction, ETriggerEvent::Completed, this, &LightAttack_Released);
 *     EnhancedInputComponent->BindAction(HeavyAttackAction, ETriggerEvent::Started,   this, &HeavyAttack);
 *     EnhancedInputComponent->BindAction(UseSkillAction,    ETriggerEvent::Started,   this, &UseSkill);
 *
 *   新代码 (本方法): 把 this 改为 this (组件本身), 行为完全一致
 *
 * nullptr 守卫:
 *   - 每个 Action 都可能 nullptr (Character 没配), 单独检查而非整体跳过
 *   - 这样策划只配了 LightAttackAction 也能正常绑定, 其他不绑定不报错
 */
void UPlayerComboComponent::BindInputActions(UEnhancedInputComponent* EnhancedInputComponent)
{
	// 防御: Enhanced Input Component 必须有效
	if (!EnhancedInputComponent)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[PlayerComboComponent] BindInputActions: EnhancedInputComponent 为空, 跳过绑定"));
		return;
	}

	// 防御: Owner Character 必须有效
	ABaseCharacter* OwnerCharacter = ResolveOwnerCharacter();
	if (!OwnerCharacter)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[PlayerComboComponent] BindInputActions: ResolveOwnerCharacter 失败, 跳过绑定"));
		return;
	}

	// ===========================================
	// 【v60.x 大厂架构 — 武器类型分离】
	// 同一个物理键（左键）在 UE IMC 中被同时绑定到 FireAction 和 LightAttackAction
	// 导致按左键时两个回调都被触发
	//
	// 解决方案: 在 PlayerComboComponent 层根据武器类型决定是否处理 LightAttack
	//   - 近战武器 (Melee): 处理 LightAttack
	//   - 枪械 (Primary/Secondary): 不处理（由 OnFirePressed 处理）
	//   - 无武器: 不处理
	// ===========================================

	// 绑定轻击: Started → LightAttack_Pressed, Completed → LightAttack_Released
	// 武器类型检查在 LightAttack_Pressed / LightAttack_Released 内部进行
	if (LightAttackAction)
	{
		EnhancedInputComponent->BindAction(LightAttackAction, ETriggerEvent::Started,
			this, &UPlayerComboComponent::LightAttack_Pressed);
		EnhancedInputComponent->BindAction(LightAttackAction, ETriggerEvent::Completed,
			this, &UPlayerComboComponent::LightAttack_Released);
	}

	// 绑定重击: Started → HeavyAttack (单击)
	if (HeavyAttackAction)
	{
		EnhancedInputComponent->BindAction(HeavyAttackAction, ETriggerEvent::Started,
			this, &UPlayerComboComponent::HeavyAttack);
	}

	// 绑定技能: Started → UseSkill
	if (UseSkillAction)
	{
		EnhancedInputComponent->BindAction(UseSkillAction, ETriggerEvent::Started,
			this, &UPlayerComboComponent::UseSkill);
	}
}


// ==========================================
// 4. 玩家输入入口回调
// ==========================================

/**
 * LightAttack_Pressed — 轻击按下事件
 *
 * 完整流程 (对应原 BaseCharacter.cpp line 1341-1380):
 *   1. 死亡/无武器检查 → 显式 return (零兜底)
 *   2. 下蹲攻击权限拦截 (如果武器不允许下蹲攻击)
 *   3. 记录 bIsHoldingLightAttack = true
 *   4. 根据武器 bCanMoveWhileLightAttack 决定是否锁步
 *   5. 第一刀起手 (bIsAttacking=false): 初始化状态机 → 调 ExecuteComboSequence
 *   6. 连击区间 (bCanReceiveInput=true): 记录 bSaveAttack = true
 *
 * 大厂原则 - 零兜底:
 *   - 任何前置检查不通过都 return, 不静默"先执行再说"
 *   - 所有访问 OwnerCharacter->xxx 之前都先 nullptr 守卫 (拆分后必须显式检查)
 */
void UPlayerComboComponent::LightAttack_Pressed()
{

	// ==================================================================
	// 【v40.6 P0】按需解析 Owner — 不依赖 BeginPlay 缓存
	// ===================================================================
	ABaseCharacter* OwnerCharacter = ResolveOwnerCharacter();
	if (!OwnerCharacter)
	{
		return;
	}

	// 死人不能攻击
	if (OwnerCharacter->IsDead())
	{
		return;
	}

	// ============================================================
	// 【v93 大厂架构】母体分支 — 母体无武器也能攻击
	// ============================================================
	//
	// 大厂原则 — 分支最早做:
	//   母体变异后武器已销毁 (MutatePawnToMother v93 修复), CurrentWeapon == nullptr
	//   武器分支(line 289-301)会 return, 永远走不到攻击流程
	//   必须在武器检查之前分流, 避免母体左键失效
	//
	// 复用现有 Server_PlayAttackAnim/Multicast_PlayAttackAnim RPC (单一真理源):
	//   - ComboIndex = 0 表示"母体专属蒙太奇" (InComboIndex 不是 1/2 走 Combo1/Combo2)
	//   - 客户端 Multicast_PlayAttackAnim_Implementation 根据 bIsMother 选不同 Montage 来源
	if (OwnerCharacter->bIsMother)
	{
		ExecuteMotherAttackSequence();
		return;
	}

	// ============================================================
	// 【v60.x 大厂架构 — 武器类型检查】
	// 同一个物理键（左键）同时绑定了 FireAction 和 LightAttackAction
	// 按左键时两个回调都被触发
	// 修复: LightAttack 只处理近战武器 (Melee)，枪械走 OnFirePressed
	// ============================================================
	ABaseWeapon* CurrentWeapon = OwnerCharacter->GetCurrentWeapon();
	if (!CurrentWeapon)
	{
		// 没武器 — 业务正常, 不报错
		return;
	}

	// 只处理近战武器 (Melee)，枪械走 OnFirePressed
	if (CurrentWeapon->GetMeshType() != EWeaponMeshType::Melee)
	{
		// 是枪械 — 不处理 LightAttack, 让 OnFirePressed 处理
		return;
	}

	// 下蹲攻击权限拦截
	// 如果你正蹲着, 并且这把武器禁止下蹲攻击, 直接 return, 无视玩家按键
	if (OwnerCharacter->bIsCrouched && !CurrentWeapon->bCanAttackWhileCrouched)
	{
		return;
	}

	// 记录按住状态
	bIsHoldingLightAttack = true;

	// 根据当前武器的配置, 决定要不要锁步
	// 大厂原则 - 委托 BaseCharacter 的字段: bIsMovementLocked 在 BaseCharacter 上,
	// 这是为了保持 BaseWeapon::Tick 仍然能读到这个标志 (武器 Tick 是另一条独立路径)
	OwnerCharacter->bIsMovementLocked = !CurrentWeapon->bCanMoveWhileLightAttack;

	if (OwnerCharacter->bIsMovementLocked)
	{
		// 物理刹车: MaxWalkSpeed=0 让角色立即停下
		OwnerCharacter->GetCharacterMovement()->MaxWalkSpeed = 0.0f;
	}

	if (!bIsAttacking)
	{
		// 1. 第一刀起手: 上锁, 初始化状态
		bIsAttacking = true;
		ComboIndex = 1;
		bSaveAttack = false;
		bCanReceiveInput = false;
		ExecuteComboSequence();
	}
	else if (bCanReceiveInput)
	{
		// 2. 如果正在挥刀, 且【绿色输入区间】开启了
		// 记录玩家点过鼠标了 (不管点几次, 只记为 true)
		bSaveAttack = true;
	}
}


/**
 * LightAttack_Released — 轻击松开事件
 *
 * 流程: 仅设 bIsHoldingLightAttack = false (记录松手状态, 给 CheckCombo 终极结算用)
 *
 * 【v60.x 大厂架构】也检查武器类型，只处理近战武器
 */
void UPlayerComboComponent::LightAttack_Released()
{

	// ==================================================================
	// 【v40.6 P0】按需解析 Owner — 不依赖 BeginPlay 缓存
	// ===================================================================
	ABaseCharacter* OwnerCharacter = ResolveOwnerCharacter();
	if (!OwnerCharacter)
	{
		return;
	}

	// 【v60.x 大厂架构】检查武器类型，只处理近战武器
	ABaseWeapon* CurrentWeapon = OwnerCharacter->GetCurrentWeapon();
	if (!CurrentWeapon || CurrentWeapon->GetMeshType() != EWeaponMeshType::Melee)
	{
		// 没武器或不是近战武器 — 不处理
		return;
	}

	// 记录: 玩家松手了
	bIsHoldingLightAttack = false;
}


/**
 * HeavyAttack — 重击事件 (单击)
 *
 * 注意: 当前版本 HeavyAttack 实现被注释, 仅留下权限检查
 * 大厂原则 - 零兜底: 死/无武器/不允许下蹲攻击 → 显式 return
 */
void UPlayerComboComponent::HeavyAttack()
{

	// ==================================================================
	// 【v40.6 P0】按需解析 Owner — 不依赖 BeginPlay 缓存
	// ===================================================================
	ABaseCharacter* OwnerCharacter = ResolveOwnerCharacter();
	if (!OwnerCharacter)
	{
		return;
	}

	// 死人、武器未装备不能重击
	if (OwnerCharacter->IsDead() || !OwnerCharacter->GetCurrentWeapon())
	{
		return;
	}

	// 重击同样需要检查下蹲权限
	ABaseWeapon* CurrentWeapon = OwnerCharacter->GetCurrentWeapon();
	if (OwnerCharacter->bIsCrouched && !CurrentWeapon->bCanAttackWhileCrouched)
	{
		return;
	}

	// 注: 重击主体实现已注释 (保留扩展位)
}


/**
 * UseSkill — 释放技能事件
 *
 * TODO: 技能系统具体实现 (暂留空, 与原 BaseCharacter 行为一致)
 */
void UPlayerComboComponent::UseSkill()
{

	// ==================================================================
	// 【v40.6 P0】按需解析 Owner — 不依赖 BeginPlay 缓存
	// ===================================================================
	ABaseCharacter* OwnerCharacter = ResolveOwnerCharacter();
	if (!OwnerCharacter)
	{
		return;
	}

	// 死亡状态不能释放技能
	if (OwnerCharacter->IsDead())
	{
		return;
	}

	// TODO: 技能系统具体实现
	// 这里可以扩展为技能槽系统, 支持多个技能
}


// ==========================================
// 5. 动画通知 (Anim Notify) 专用接口
// ==========================================

/**
 * EnableComboWindow — 区间开始时触发 (开启连招窗口)
 *
 * 行为:
 *   - bCanReceiveInput = true (绿灯亮起, 开始接收输入)
 *   - bSaveAttack = false (清空上一轮的垃圾缓存)
 *   - 【v74 大厂架构】trace 检测由蒙太奇 ANS_MeleeTraceState 标签驱动
 *     * 旧版 (v73) C++ 在 EnableComboWindow 强制调 StartDamageTrace → 与美术时间轴标签冲突
 *     * 新版 (v74) 单一真理源 = ANS_MeleeTraceState (UAnimNotifyState 子类)
 *       - 美术在"挥刀中段"位置拖 ANS_MeleeTraceState(Tracing) → 启 trace
 *       - 在"收刀"位置 NotifyEnd → 关 trace
 *     * C++ 不再做硬编码启停 (职责单一 — 状态机只管连招窗口)
 */
void UPlayerComboComponent::EnableComboWindow()
{

	// ==================================================================
	// 【v40.6 P0】按需解析 Owner — 不依赖 BeginPlay 缓存
	// ===================================================================
	ABaseCharacter* OwnerCharacter = ResolveOwnerCharacter();
	if (!OwnerCharacter)
	{
		return;
	}

	bCanReceiveInput = true;  // 绿灯亮起, 开始接收输入
	bSaveAttack = false;      // 清空上一轮的垃圾缓存

	// 【v74 大厂架构 — 蒙太奇时间轴驱动检测】
	//   旧版 (v73) C++ 在 EnableComboWindow 强制调 StartDamageTrace
	//   → 与美术在蒙太奇时间轴上拖 ANS_MeleeTraceState 标签冲突, 双重启动
	//   新版 (v74) 单一真理源 = ANS_MeleeTraceState:
	//     - 美术在"挥刀中段"位置拖 ANS_MeleeTraceState(Tracing) → 启 trace
	//     - 在"收刀"位置 NotifyEnd → 关 trace
	//   C++ 不再做硬编码启停, 完全由蒙太奇时间轴决定
	//
	//   防御 (零兜底): 如果蒙太奇忘配 ANS_MeleeTraceState, trace 不会启动
	//   这是大厂原则 - 显式优于隐式: 美术配置错立即可见 (没伤害), 而不是 C++ 兜底
}


/**
 * CheckCombo — 区间结束时触发 (检查缓存区是否有缓存攻击)
 *
 * 终极结算: 如果有缓存的点击或玩家正死死按住左键, 则跳转下一段
 *
 * 工业级防逃课锁: 连击派生前, 检查当前是不是已经蹲下了
 *   (与原 BaseCharacter.cpp line 1491-1496 行为一致)
 */
void UPlayerComboComponent::CheckCombo()
{

	// ==================================================================
	// 【v40.6 P0】按需解析 Owner — 不依赖 BeginPlay 缓存
	// ===================================================================
	ABaseCharacter* OwnerCharacter = ResolveOwnerCharacter();
	if (!OwnerCharacter)
	{
		return;
	}

	bCanReceiveInput = false;  // 绿灯熄灭, 关闭窗口

	// 【终极结算】: 如果有缓存的点击, 或者玩家正死死按住左键
	if (bSaveAttack || bIsHoldingLightAttack)
	{
		// 【防逃课锁】: 连击派生前, 检查当前是不是已经蹲下了
		// 防御: Owner / Weapon 必须有效 (极端边缘 case: 区间开启到结束之间 Owner 被销毁)
		if (OwnerCharacter && OwnerCharacter->GetCurrentWeapon() &&
			OwnerCharacter->bIsCrouched &&
			!OwnerCharacter->GetCurrentWeapon()->bCanAttackWhileCrouched)
		{
			bSaveAttack = false;  // 清除缓存
			return;               // 强行中断后续连招
		}

		bSaveAttack = false;  // 消耗掉这次缓存

		// 核心需求: 1变2, 2变1, 无限循环
		ComboIndex = (ComboIndex == 1) ? 2 : 1;

		ExecuteComboSequence();  // 丝滑跳转下一刀
	}
}


/**
 * EndAttackState — 动画彻底结束时触发
 *
 * 彻底收招, 解开所有锁:
 *   - 状态机字段重置 (bIsAttacking / bSaveAttack / bCanReceiveInput / ComboIndex)
 *   - bIsMovementLocked = false (BaseCharacter 上的字段)
 *   - MaxWalkSpeed = 600.0f (恢复正常速度)
 */
void UPlayerComboComponent::EndAttackState()
{

	// ==================================================================
	// 【v40.6 P0】按需解析 Owner — 不依赖 BeginPlay 缓存
	// ===================================================================
	ABaseCharacter* OwnerCharacter = ResolveOwnerCharacter();
	if (!OwnerCharacter)
	{
		return;
	}

	// 状态机字段重置
	bIsAttacking = false;
	bSaveAttack = false;
	bCanReceiveInput = false;
	ComboIndex = 1;

	// 收刀时, 彻底解除移动锁定
	// 大厂原则 - 委托 BaseCharacter: bIsMovementLocked 在 BaseCharacter 上 (BaseWeapon::Tick 也要读)
	if (OwnerCharacter)
	{
		OwnerCharacter->bIsMovementLocked = false;
		if (UCharacterMovementComponent* MoveComp = OwnerCharacter->GetCharacterMovement())
		{
			// 【v56.5 大厂架构修复】从 PlayerConfigAsset 读取 MaxWalkSpeed，不再硬编码
			// 
			// 根因: 旧版 EndAttackState 硬编码 MaxWalkSpeed=400.0f
			//        当 DA_PlayerConfigAsset.MaxWalkSpeed 配成 600 时，攻击结束后速度变成 400
			//
			// 修复: 从 PlayerConfigAsset 读取配置的 MaxWalkSpeed，保持移动速度一致性
			float TargetWalkSpeed = 600.0f;  // 默认值兜底
			if (UPlayerConfigAsset* PlayerConfig = GetPlayerConfigAsset())
			{
				TargetWalkSpeed = PlayerConfig->MaxWalkSpeed;
			}
			MoveComp->MaxWalkSpeed = TargetWalkSpeed;
		}
	}
}


// ==========================================
// 6. 连招核心执行 (ExecuteComboSequence — 内部)
// ==========================================

/**
 * ExecuteComboSequence — 连招核心执行逻辑
 *
 * 工业级做法: 所有连招全在第 0 个蒙太奇里
 * 智能拼接要跳转的片段名字 (Combo1 或 Combo2, 由 ComboIndex 决定)
 *
 * 流程:
 *   1. 取 Owner.CurrentWeapon (无武器 → 直接 return)
 *   2. 取武器的 ComboMontage (第 0 个蒙太奇)
 *   3. 根据 ComboIndex 智能选择 Section 名 (Combo1 或 Combo2)
 *   4. PlayAnimMontage 在本机播
 *   5. 调 Owner.Server_PlayAttackAnim 走网络同步 (RPC)
 *
 * 大厂原则 (v35 — AnimNotify 化):
 *   - 启动 trace 时机: 不再硬编码 (旧版 line 499-502 "PlayAnimMontage 立刻就开" 太早)
 *   - 改为 BP AnimNotify 控制 (美术在蒙太奇"挥刀中段"位置放 ANS_MeleeTrace 触发)
 *   - BP 通知拿武器: WeaponAttach->GetCurrentWeapon() (蓝图纯函数节点)
 *   - 结束 trace 时机: 蒙太奇自然结束 (OnPlayerAttackMontageEnded 防 BP 通知漏触发)
 *   - 零兜底: BP 通知没触发 = trace 不开 = 蒙太奇"空挥" (这是策划的责任, 不再 C++ 兜底)
 *
 *   - 无武器 / 蒙太奇为空 → 直接 return, 不静默 "再播一次"
 *   - 网络同步失败由 RPC 内部处理 (UFUNCTION 标记已配 WithValidation)
 */
void UPlayerComboComponent::ExecuteComboSequence()
{

	// ==================================================================
	// 【v40.6 P0】按需解析 Owner — 不依赖 BeginPlay 缓存
	// ===================================================================
	ABaseCharacter* OwnerCharacter = ResolveOwnerCharacter();
	if (!OwnerCharacter)
	{
		return;
	}

	// 取武器 (无武器 → 直接 return)
	ABaseWeapon* CurrentWeapon = OwnerCharacter->GetCurrentWeapon();
	if (!CurrentWeapon)
	{
		return;
	}

	// 工业级做法: 所有连招全在第 0 个蒙太奇里
	UAnimMontage* ComboMontage = CurrentWeapon->GetAttackMontage(false, 0);
	if (ComboMontage)
	{
		// 【v83 可观测性】客户端/服务器都打 log, 确认 LightAttackMontages[0] 在两端都能拿到
		//   - 客户端 Local=1 Auth=0 时也打, 让客户端近战 trace 问题可定位
		//   - 若 Client Log 显示 "LightAttackMontages.Num()=0" → 蓝图没配 / DOREPLIFETIME 没生效
		const int32 LightMontageCount = CurrentWeapon->GetLightAttackMontageCount();
		UE_LOG(LogTemp, Log,
			TEXT("[PlayerCombo] ExecuteComboSequence ENTER. Owner=%s Local=%d Auth=%d LightAttackMontages.Num()=%d ComboIndex=%d"),
			*OwnerCharacter->GetName(),
			OwnerCharacter->IsLocallyControlled() ? 1 : 0,
			OwnerCharacter->HasAuthority() ? 1 : 0,
			LightMontageCount,
			ComboIndex);

		// 智能拼接要跳转的片段名字 (Combo1 或者是 Combo2)
		const FName SectionName = (ComboIndex == 1) ? FName("Combo1") : FName("Combo2");

		// 本机播放
		OwnerCharacter->PlayAnimMontage(ComboMontage, 1.0f, SectionName);

		// 【激活网络同步】: 告诉服务器我出的是第几刀
		// Server_PlayAttackAnim 是 BaseCharacter 上的 RPC (UFUNCTION Server Reliable WithValidation)
		// 拆分后通过 Owner 转发, 网络协议完全一致
		OwnerCharacter->Server_PlayAttackAnim(false, ComboIndex);

		// ============================================================
		// 【v85.3 大厂架构重构】trace 开关完全由动画通知控制
		//
		// 旧版 (v32-v85.2) 反模式:
		//   ExecuteComboSequence 直接调 StartDamageTrace → 动画还没开始 trace 就开了
		//   → 射线检测在挥刀动画开始前就运行了 (跟动画不同步)
		//   → 如果动画被打断/结束回调失败 → trace 永远开着
		//
		// 新版 (v85.3):
		//   ExecuteComboSequence 只负责播放动画 (单一职责)
		//   trace 的 Start/Stop 完全由 ANS_MeleeTraceState 控制 (动画时间轴)
		//   → 射线检测跟动画完全同步 (动画开始 → trace 开始, 动画结束 → trace 结束)
		//
		// 调用链 (大厂原则 - 责任链):
		//   1. ExecuteComboSequence → PlayAnimMontage + Server_PlayAttackAnim
		//   2. 动画播放到 ANS_MeleeTraceState NotifyBegin → StartDamageTrace (两端都调)
		//   3. 动画播放完/ANS NotifyEnd → StopDamageTrace (两端都调)
		//   4. OnPlayerAttackMontageEnded → 清理状态锁
		//
		// 大厂原则 - 零兜底:
		//   - 不在 ExecuteComboSequence 中调 StartDamageTrace (绕过动画通知 = 不同步)
		//   - 不在 OnPlayerAttackMontageEnded 中调 StopDamageTrace (ANS NotifyEnd 才是唯一入口)
		//   - Server_ReportHit 的 HasAuthority() 守卫是防重复扣血的唯一机制
		// ============================================================

		// 绑蒙太奇结束回调 (只清理状态锁, 不调 StopDamageTrace)
		BindMontageEndCallback(ComboMontage);
	}
	else
	{
		// ============================================================
		// 【v83 大厂架构 — 客户端近战 trace 兜底】当 LightAttackMontages[0] 为空
		//
		// 用户报告: "客户端近战武器攻击还是没有射线检测"
		// 根因链 (大厂原则 - 可观测性优先):
		//   1. Client 端 BP_Weapon_Xxx 的 LightAttackMontages 通过 DOREPLIFETIME 复制
		//   2. 若 BP 没配 Montage 或 DOREPLIFETIME 没生效 → Client 端 ComboMontage=null
		//   3. Client 端 ExecuteComboSequence 内 if (ComboMontage) 块整个跳过
		//   4. Client 不发 Server_PlayAttackAnim RPC → 服务器不知道 → 服务器不 Multicast
		//   5. Client 不调 PlayAnimMontage → 蒙太奇不播 → ANS_MeleeTraceState 不触发 → 没 trace
		//
		// 大厂原则 (强制可观测性 — 不是兜底):
		//   - 不静默 return: 必须 Log Error 告知根因 + 强制 trace 让用户立即看到问题
		//   - 这是"防御型可视性": 即使配置错误, 客户端也能看到 trace 效果 (而不是黑屏)
		//   - 服务器侧会通过 Server RPC 链路 + ANS_MeleeTraceState 正常 trace, 不受客户端这条路径影响
		//
		// 大厂原则 (单一真理源):
		//   - 不在这里硬编码"用 Combo2 Section" 或"用重击 montage" 等静默兜底
		//   - 直接 Log Error 告知: BP 没配 Montage, 必须配置 BP_Weapon_Xxx 的 LightAttackMontages
		//   - 强制调 StartDamageTrace 让客户端立即看到效果 (绕开蒙太奇配置)
		//   - Timer 0.5s 后强制 StopDamageTrace (模拟"挥刀中段 + 收刀"的动画节奏)
		// ============================================================
		const int32 LightMontageCount = CurrentWeapon->GetLightAttackMontageCount();

		UE_LOG(LogTemp, Error,
			TEXT("[PlayerCombo] ExecuteComboSequence: LightAttackMontages 为空! Owner=%s Local=%d Auth=%d "
			     "LightAttackMontages.Num()=%d HeavyAttackMontage=%s. "
			     "【v83 零兜底】客户端近战 trace 启动失败 — 客户端 PlayAnimMontage 没东西可播 → "
			     "蒙太奇不播 → ANS_MeleeTraceState 不触发 → 屏幕看不到 trace. "
			     "【强制】C++ 立即调 StartDamageTrace 让客户端看到 trace 效果 (防御型可视性, Timer 0.5s 后强制关). "
			     "【修复】打开 BP_Weapon_%s → 找到 LightAttackMontages 数组, 添加蒙太奇引用 (e.g. AM_Combo_Knife). "
			     "BP 字段名: 'Weapon Animations → Light Attack Montages', 'Heavy Attack Montage'."),
			*OwnerCharacter->GetName(),
			OwnerCharacter->IsLocallyControlled() ? 1 : 0,
			OwnerCharacter->HasAuthority() ? 1 : 0,
			LightMontageCount,
			(CurrentWeapon->GetHeavyAttackMontagePtr() != nullptr) ? TEXT("有") : TEXT("空"),
			*CurrentWeapon->GetClass()->GetName());

		// 强制开 trace (绕开蒙太奇配置依赖, 客户端立即看到效果)
		CurrentWeapon->StartDamageTrace(false);

		// Timer 0.5s 后强制 StopDamageTrace (模拟"挥刀中段 → 收刀" 节奏, 防 trace 永远开着)
		//   - 玩家挥刀实际节奏: 挥刀中段 ≈ 0.3-0.5s, 收刀 ≈ 0.2s
		//   - 这是"防御型兜底" — 没蒙太奇 = 没 OnMontageEnded, 必须 C++ 用 Timer 关
		if (UWorld* World = OwnerCharacter->GetWorld())
		{
			FTimerHandle TraceTimerHandle;
			World->GetTimerManager().SetTimer(
				TraceTimerHandle,
				FTimerDelegate::CreateWeakLambda(OwnerCharacter, [CurrentWeapon]()
				{
					if (CurrentWeapon && IsValid(CurrentWeapon))
					{
						CurrentWeapon->StopDamageTrace();
					}
				}),
				0.5f,  // 0.5s 后强制关
				false  // 不循环
			);
		}

		// 客户端进程触发不了 Server_PlayAttackAnim (PlayAnimMontage 没东西可播), 服务器侧 trace 由 ANS_MeleeTraceState 处理
		// 这里我们也告诉服务器一声 (虽然服务器已经会通过自己的 ANS_MeleeTraceState 触发, 但兜底再发一次 RPC)
		OwnerCharacter->Server_PlayAttackAnim(false, ComboIndex);
	}
}


// ==========================================
// 【v93 大厂架构新增】ExecuteMotherAttackSequence — 母体左键攻击序列
// ==========================================
//
// 镜像 ExecuteComboSequence 结构, 但:
//   - 不依赖武器 (母体无武器)
//   - 单一蒙太奇 (无 Combo 切换)
//   - 复用现有 Server_PlayAttackAnim RPC, ComboIndex=0 表示"母体专属"
//   - 客户端 Multicast_PlayAttackAnim_Implementation 根据 bIsMother 选 MotherAttackMontage
//
// 大厂原则 — RPC 边界:
//   - Server_PlayAttackAnim RPC 必须从 BaseCharacter 调 (UE 5.6 RPC 必须在 Actor 上)
//   - 这里 Owner->Server_PlayAttackAnim(...) 是转发壳调用, 网络协议与玩家路径完全一致
//
// 大厂原则 — 零兜底:
//   - MotherAttackMontage 未配 → Log Error + return (BP_MuTi 必填)
//   - HasAuthority() 守卫 (客户端调 Server RPC 是 UE 标准做法, 但 ExecuteMotherAttackSequence 本身只判断一次)

void UPlayerComboComponent::ExecuteMotherAttackSequence()
{
	// ==================================================================
	// 【v40.6 P0】按需解析 Owner — 不依赖 BeginPlay 缓存
	// ===================================================================
	ABaseCharacter* OwnerCharacter = ResolveOwnerCharacter();
	if (!OwnerCharacter)
	{
		return;
	}

	// 大厂原则 — 零兜底: MotherAttackMontage 必须配 (BP_MuTi 必填字段)
	UAnimMontage* MotherMontage = OwnerCharacter->GetMotherAttackMontage();
	if (!MotherMontage)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[PlayerCombo] ExecuteMotherAttackSequence: MotherAttackMontage 为空. "
			     "Pawn=%s (Class=%s). "
			     "【v93 零兜底】拒绝攻击. "
			     "【修复路径】打开 BP_MuTi.uasset → ClassDefaults → Combat|Mother → Mother Attack Montage 必填 (e.g. AM_MotherAttack). "
			     "刀战模式零影响 — bIsMother 永远是 false, 走不到这里."),
			*OwnerCharacter->GetName(),
			*OwnerCharacter->GetClass()->GetName());
		return;
	}

	// 防御性校验: bIsMother 必须为 true (本函数调用方已保证, 但显式检查防御代码漂移)
	if (!OwnerCharacter->bIsMother)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[PlayerCombo] ExecuteMotherAttackSequence: bIsMother=false, 拒绝执行母体攻击. "
			     "Pawn=%s. 【v93 零兜底】本函数仅供母体调用, 非母体走 ExecuteComboSequence."),
			*OwnerCharacter->GetName());
		return;
	}

	UE_LOG(LogTemp, Display,
		TEXT("[PlayerCombo] ExecuteMotherAttackSequence ENTER. Owner=%s Local=%d Auth=%d MotherMontage=%s"),
		*OwnerCharacter->GetName(),
		OwnerCharacter->IsLocallyControlled() ? 1 : 0,
		OwnerCharacter->HasAuthority() ? 1 : 0,
		*MotherMontage->GetName());

	// 本机播放 (ListenServer 也走本机播放, Multicast RPC 在远端客户端播)
	OwnerCharacter->PlayAnimMontage(MotherMontage, 1.0f);

	// 【复用现有 RPC — 不新增】调 Server_PlayAttackAnim 走网络同步
	//   - bIsHeavy=false (左键是轻击语义)
	//   - InComboIndex=0 (母体专属蒙太奇, 客户端 Implementation 用 bIsMother 判定走 MotherAttackMontage)
	//   - 这与 ExecuteComboSequence 调 Server_PlayAttackAnim(false, ComboIndex) 协议完全一致
	OwnerCharacter->Server_PlayAttackAnim(false, 0);

	// 大厂原则 — 镜像 ExecuteComboSequence:
	//   - 蒙太奇结束回调绑到 OnPlayerAttackMontageEnded 清理状态锁
	//   - 母体没有武器, 所以不调 StartDamageTrace (ANS_MeleeTraceState 不适用)
	//   - 母体走 BP_MuTi Pawn 上的蓝图蒙太奇通知 (如果有需要可由美术在 BP_MuTi 蓝图 AnimBP 加自定义 Notify)
	BindMontageEndCallback(MotherMontage);

	// 大厂原则 — 状态机:
	//   - 母体没连击系统, 只设 bIsAttacking=true 防止重复触发
	//   - 不设 bSaveAttack / bCanReceiveInput (无连击逻辑)
	bIsAttacking = true;
}


/**
 * 【2026.07.12 P0 新增】蒙太奇结束回调 (替代 BP ANS_MeleeTrace NotifyEnd)
 *
 * 流程:
 *   1. Montage 参数验证 (过滤非我触发的蒙太奇)
 *   2. 调 EndAttackState 解状态锁 + 解移动锁
 *   3. 清空 CachedPlayerMontage (防内存残留)
 *
 * 【v85.3 大厂架构重构】StopDamageTrace 不在这里调
 *   - ANS_MeleeTraceState::NotifyEnd 才是 trace 停止的唯一入口
 *   - 这里只负责清理状态锁和缓存
 *
 * 大厂原则 - 零兜底:
 *   - Montage 不匹配 (不是我触发的) → 静默 return
 *   - Owner/Weapon 为空 → 跳过 trace, 只清理状态
 *   - 不静默吞错, 所有异常路径都有日志
 */
void UPlayerComboComponent::OnPlayerAttackMontageEnded(UAnimMontage* Montage, bool bInterrupted)
{

	// ==================================================================
	// 【v40.6 P0】按需解析 Owner — 不依赖 BeginPlay 缓存
	// ===================================================================
	ABaseCharacter* OwnerCharacter = ResolveOwnerCharacter();
	if (!OwnerCharacter)
	{
		return;
	}

	// 【v42 P0 修复】防御: CachedPlayerMontage 为空时直接返回
	// 根因: AI 攻击不走 PlayerComboComponent 路径, CachedPlayerMontage 永远是 nullptr
	//        导致 Montage != CachedPlayerMontage 比较 (nullptr != AI_Montage) 为 true → 错误执行 EndAttackState
	//        → 速度被重置为 400 而不是从配置表恢复
	// 修复: CachedPlayerMontage 为空说明没有绑定过玩家攻击蒙太奇, 直接 return
	if (!CachedPlayerMontage)
	{
		// 【v42.1】改用 Log Warning 确保日志可见 (用户需要看到这个来确认修复生效)
		UE_LOG(LogTemp, Warning,
			TEXT("[PlayerCombo] OnPlayerAttackMontageEnded: CachedPlayerMontage 为空 (AI 蒙太奇或未绑定), 跳过处理. Montage=%s Owner=%s"),
			*GetNameSafe(Montage),
			*GetNameSafe(GetOwner()));
		return;
	}

	// 【v40.6 P0】ResolveOwnerCharacter() 已处理 null, 这里不再重复

	// 防御 2: Montage 过滤 (multicast 会对所有结束的蒙太奇广播, 必须过滤)
	if (Montage && Montage != CachedPlayerMontage)
	{
		// 不是我触发的蒙太奇, 静默忽略
		return;
	}

	// 【v85.3 删除】StopDamageTrace 不在这里调 — ANS_MeleeTraceState::NotifyEnd 才是唯一入口
	// 步骤 1: 解状态锁 + 解移动锁
	EndAttackState();

	// 步骤 2: 清空缓存 (防下次攻击误判)
	CachedPlayerMontage = nullptr;

	UE_LOG(LogTemp, Verbose,
		TEXT("[PlayerCombo] OnPlayerAttackMontageEnded: Pawn=%s, Interrupted=%d, 已 EndAttackState"),
		*OwnerCharacter->GetName(), bInterrupted ? 1 : 0);
}


/**
 * 【2026.07.12 P0 新增】双绑 + 缓存蒙太奇 (大厂原则)
 *
 * 安全:
 *   - UE TMulticastScriptDelegate::AddInternal 有 ensure: 不能重复 AddDynamic
 *   - 修复: RemoveDynamic 先解绑 + IsAlreadyBound 检查 (双保险)
 *   - CachedPlayerMontage 记录本次蒙太奇 (过滤 multicast 干扰)
 */
void UPlayerComboComponent::BindMontageEndCallback(UAnimMontage* InMontage)
{

	// ==================================================================
	// 【v40.6 P0】按需解析 Owner — 不依赖 BeginPlay 缓存
	// ===================================================================
	ABaseCharacter* OwnerCharacter = ResolveOwnerCharacter();
	if (!OwnerCharacter)
	{
		return;
	}

	if (!OwnerCharacter || !InMontage)
	{
		return;
	}

	USkeletalMeshComponent* CharMesh = OwnerCharacter->GetMesh();
	if (!CharMesh)
	{
		return;
	}

	UAnimInstance* AnimInst = CharMesh->GetAnimInstance();
	if (!AnimInst)
	{
		return;
	}

	// 步骤 1: 先解绑 (RemoveDynamic 是幂等的 — 不存在的 binding 静默跳过)
	AnimInst->OnMontageEnded.RemoveDynamic(this, &UPlayerComboComponent::OnPlayerAttackMontageEnded);

	// 步骤 2: 再绑定 (IsAlreadyBound 检查 — 双保险)
	if (!AnimInst->OnMontageEnded.IsAlreadyBound(this, &UPlayerComboComponent::OnPlayerAttackMontageEnded))
	{
		AnimInst->OnMontageEnded.AddDynamic(this, &UPlayerComboComponent::OnPlayerAttackMontageEnded);
	}

	// 缓存正在播放的攻击蒙太奇 — 用于回调时判断是不是"我触发的蒙太奇"
	CachedPlayerMontage = InMontage;
}


// ==========================================
// 7. 攻击者身份转发
// ==========================================

/**
 * SetAttackerIsAI — 转发到 BaseCharacter
 *
 * 大厂原则 - 单一真理源:
 *   - bIsCurrentlyAttackerAI 在 BaseCharacter 上 (BaseWeapon::Tick 要读)
 *   - 本组件不持有副本, 只做委托调用
 *   - 玩家路径默认 false, AIAttackComponent 设 true
 */
void UPlayerComboComponent::SetAttackerIsAI(bool bIsAI)
{

	// ==================================================================
	// 【v40.6 P0】按需解析 Owner — 不依赖 BeginPlay 缓存
	// ===================================================================
	ABaseCharacter* OwnerCharacter = ResolveOwnerCharacter();
	if (!OwnerCharacter)
	{
		return;
	}

	if (OwnerCharacter)
	{
		OwnerCharacter->SetAttackerIsAI(bIsAI);
	}
}


/**
 * IsAttackerAI — 转发到 BaseCharacter
 */
bool UPlayerComboComponent::IsAttackerAI() const
{
	// 【v40.6 P0】按需解析 Owner — 不依赖 BeginPlay 缓存
	ABaseCharacter* OwnerCharacter = ResolveOwnerCharacter();
	if (!OwnerCharacter)
	{
		return false;
	}

	return OwnerCharacter->IsAttackerAI();
}


// ==========================================
// 8. 工具接口
// ==========================================

/**
 * GetCurrentWeapon — 转发到 BaseCharacter
 *
 * 大厂原则 - 零跨边界:
 *   - 不暴露 BaseCharacter 的 protected CurrentWeapon 字段
 *   - 通过 BaseCharacter::GetCurrentWeapon() 公开 getter 访问
 */
ABaseWeapon* UPlayerComboComponent::GetCurrentWeapon() const
{
	// 【v40.6 P0】按需解析 Owner — 不依赖 BeginPlay 缓存
	ABaseCharacter* OwnerCharacter = ResolveOwnerCharacter();
	if (!OwnerCharacter)
	{
		return nullptr;
	}

	return OwnerCharacter->GetCurrentWeapon();
}


// ==========================================
// 辅助函数
// ==========================================

/**
 * GetPlayerConfigAsset — 获取玩家配置资产
 *
 * 路径: RoomGameMode->PlayerConfigAsset
 *
 * 大厂原则 - 单一真理源:
 *   - 玩家移动速度从 DA_PlayerConfigAsset.MaxWalkSpeed 读取
 *   - 不在组件内硬编码
 */
UPlayerConfigAsset* UPlayerComboComponent::GetPlayerConfigAsset() const
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return nullptr;
	}

	AGameModeBase* GameMode = World->GetAuthGameMode();
	ARoomGameMode* RoomGM = Cast<ARoomGameMode>(GameMode);
	if (!RoomGM)
	{
		return nullptr;
	}

	// 【v56.5】PlayerConfigAsset 是 TSoftObjectPtr，需要 LoadSynchronous 获取实际指针
	return RoomGM->PlayerConfigAsset.LoadSynchronous();
}


// ============================================================
// 【v93.2 大厂架构 — 母体复用 Melee 缝合算法】玩家母体攻击命中上报
// ============================================================
// 复用动机 (用户需求 2026.07.25):
//   - 母体复用 ANS_MeleeTraceState + MeleeSwStrategy (零重复)
//   - 命中 RPC 走 Owner->Server_ReportMotherAttackHit → BaseCharacter 转发壳 → 本函数 (玩家路径)
//   - 命中后行为: 调 RoomMotherMutationSubsystem::MutateCharacterToMother (大厂复用)
//
// 大厂原则 — 与 AIAttackComponent 镜像对称:
//   - 防御 1-7 与 Server_ReportMotherAttackHit_Implementation (AI) 完全同构
//   - 唯一区别: 调用方 (BaseCharacter 转发壳) 已用 IsAttackerAI() 区分玩家/AI, 本函数无须再分

void UPlayerComboComponent::Server_ReportMotherAttackHit_Implementation(AActor* HitActor)
{
	// 【v40.6 P0】按需解析 Owner — 不依赖 BeginPlay 缓存
	ABaseCharacter* OwnerCharacter = ResolveOwnerCharacter();
	if (!OwnerCharacter)
	{
		return;
	}

	// 防御 1: HitActor 必须有效
	if (!HitActor)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[PlayerComboComponent] Server_ReportMotherAttackHit: 玩家母体=%s 收到空 HitActor, 忽略"),
			*OwnerCharacter->GetName());
		return;
	}

	// 防御 2: 目标必须是 ABaseCharacter
	ABaseCharacter* Victim = Cast<ABaseCharacter>(HitActor);
	if (!Victim)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[PlayerComboComponent] Server_ReportMotherAttackHit: 玩家母体=%s -> HitActor=%s 不是 ABaseCharacter, 忽略"),
			*OwnerCharacter->GetName(), *HitActor->GetName());
		return;
	}

	// 防御 3: 目标已死则不变 (避免重复变母体)
	if (Victim->IsDead())
	{
		UE_LOG(LogTemp, Verbose,
			TEXT("[PlayerComboComponent] Server_ReportMotherAttackHit: 玩家母体=%s -> Victim=%s 已死, 跳过变母体"),
			*OwnerCharacter->GetName(), *Victim->GetName());
		return;
	}

	// 防御 4: 自伤防御 (母体不打自己)
	if (Victim == OwnerCharacter)
	{
		return;
	}

	// 防御 5: 友军伤害守卫 (复用 FFactionTags::CanDamage)
	if (!FFactionTags::CanDamage(
			OwnerCharacter->GetFactionTag(),
			Victim->GetFactionTag(),
			TEXT("UPlayerComboComponent::Server_ReportMotherAttackHit"),
			OwnerCharacter->GetName(),
			Victim->GetName()))
	{
		return;
	}

	// 防御 6: 目标已是母体 → 拒绝 (二次变母体无意义)
	if (Victim->bIsMother)
	{
		UE_LOG(LogTemp, Verbose,
			TEXT("[PlayerComboComponent] Server_ReportMotherAttackHit: 玩家母体=%s -> Victim=%s 已是母体, 跳过"),
			*OwnerCharacter->GetName(), *Victim->GetName());
		return;
	}

	// 防御 7: 模式校验 — 大厂原则: 母体攻击只在生化模式触发
	const AGameStateBase* GameStateBase = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	if (const ARoomGameState* RoomGS = Cast<ARoomGameState>(GameStateBase))
	{
		if (RoomGS->CurrentMatchMode != ERoomMatchMode::Zombie)
		{
			UE_LOG(LogTemp, Error,
				TEXT("[PlayerComboComponent] Server_ReportMotherAttackHit: 当前模式=%d 不是 Zombie — 拒绝变母体. "
				     "【v93.2 零兜底】母体攻击只能在生化模式触发. Owner=%s Victim=%s"),
				static_cast<int32>(RoomGS->CurrentMatchMode),
				*OwnerCharacter->GetName(), *Victim->GetName());
			return;
		}
	}

	// ============================================================
	// 终极行为 — 大厂复用 (零重复架构)
	// ============================================================
	if (URoomMotherMutationSubsystem* MutSys = URoomMotherMutationSubsystem::Get(this))
	{
		MutSys->MutateCharacterToMother(Victim);
	}
	else
	{
		UE_LOG(LogTemp, Error,
			TEXT("[PlayerComboComponent] Server_ReportMotherAttackHit: 找不到 RoomMotherMutationSubsystem — 拒绝变母体. "
			     "【v93.2 零兜底】检查 ARoomGameMode::InjectSubsystemConfigs 是否调用."));
	}
}