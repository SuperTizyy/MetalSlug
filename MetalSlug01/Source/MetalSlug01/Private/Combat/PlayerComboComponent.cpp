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

	// 绑定轻击: Started → LightAttack_Pressed, Completed → LightAttack_Released
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

	// 死人、武器未装备不能攻击
	// 【注意】暂停检查已移除: 暂停逻辑改由 PlayerController 通过 InputMode=UIOnly 阻塞,
	// 不再使用全局 SetGamePaused, 因此 BaseCharacter 无需再读全局暂停状态
	if (OwnerCharacter->IsDead() || !OwnerCharacter->GetCurrentWeapon())
	{
		return;
	}

	// 下蹲攻击权限拦截
	// 如果你正蹲着, 并且这把武器禁止下蹲攻击, 直接 return, 无视玩家按键
	ABaseWeapon* CurrentWeapon = OwnerCharacter->GetCurrentWeapon();
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
		// 智能拼接要跳转的片段名字 (Combo1 或者是 Combo2)
		const FName SectionName = (ComboIndex == 1) ? FName("Combo1") : FName("Combo2");

		// 本机播放
		OwnerCharacter->PlayAnimMontage(ComboMontage, 1.0f, SectionName);

		// 【激活网络同步】: 告诉服务器我出的是第几刀
		// Server_PlayAttackAnim 是 BaseCharacter 上的 RPC (UFUNCTION Server Reliable WithValidation)
		// 拆分后通过 Owner 转发, 网络协议完全一致
		OwnerCharacter->Server_PlayAttackAnim(false, ComboIndex);

		// ============================================================
		// 【2026.07.12 P0 修复 → v35 撤回】trace 启动交给 BP AnimNotify
		//
		// 旧版 (v32-v34): C++ 在 PlayAnimMontage 后立刻调 StartWeaponTrace
		//   - 问题: trace 窗口与蒙太奇挥刀动作不同步 (按攻击键瞬间就开, 美术无控制权)
		//   - 根因: 美术想在"挥刀中段"开, "收刀"关, C++ 硬编码做不到
		//
		// 新版 (v35): BP AnimNotify 控制
		//   - 美术在 UE 编辑器打开蒙太奇 Combo1 / Combo2 段
		//   - 在"挥刀中段"位置加 ANS_MeleeTrace (BP AnimNotify) → 触发 StartWeaponTrace
		//   - 在"收刀"位置加 ANS_MeleeTraceEnd (BP AnimNotify) → 触发 StopWeaponTrace
		//   - BP AnimNotify 拿武器: WeaponAttach->GetCurrentWeapon() (蓝图纯函数节点)
		//
		// 大厂原则 - 职责对等:
		//   - 启动 trace = 美术/策划的责任 (蒙太奇节奏)
		//   - 结束 trace = C++ 防泄漏兜底 (蒙太奇自然结束时强制 StopWeaponTrace)
		//     * 跟 AIAttackComponent::OnAIAttackMontageEnded 对称
		//     * 这是"清理逻辑"不是"业务兜底" — 蒙太奇必然结束, 防 BP 通知漏触发造成 trace 永远开启
		// ============================================================

		// 绑蒙太奇结束回调 (客户端 + 服务器本地都绑, 谁播的谁管)
		// 作用: 蒙太奇自然结束时强制 StopWeaponTrace, 防 BP 通知漏触发导致 trace 永远开启
		BindMontageEndCallback(ComboMontage);
	}
}


/**
 * 【2026.07.12 P0 新增】蒙太奇结束回调 (替代 BP ANS_MeleeTrace NotifyEnd)
 *
 * 流程:
 *   1. Montage 参数验证 (过滤非我触发的蒙太奇)
 *   2. 服务器主动 StopWeaponTrace (跟 AIAttackComponent 对称)
 *   3. 调 EndAttackState 解状态锁 + 解移动锁
 *   4. 清空 CachedPlayerMontage (防内存残留)
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

	// 步骤 1: 服务器主动 StopWeaponTrace (跟 AIAttackComponent 对称)
	if (OwnerCharacter->HasAuthority())
	{
		if (ABaseWeapon* CurrentWeapon = OwnerCharacter->GetCurrentWeapon())
		{
			CurrentWeapon->StopWeaponTrace();
		}
	}

	// 步骤 2: 解状态锁 + 解移动锁
	EndAttackState();

	// 步骤 3: 清空缓存 (防下次攻击误判)
	CachedPlayerMontage = nullptr;

	UE_LOG(LogTemp, Verbose,
		TEXT("[PlayerCombo] OnPlayerAttackMontageEnded: Pawn=%s, Interrupted=%d, 已 StopTrace + EndAttackState"),
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