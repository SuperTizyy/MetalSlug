// 版权声明: 在项目设置的描述页面填写您的版权信息。
//
// 文件: AIAttackComponent.cpp
// 作用: AI 攻击子系统组件实现 — 从 ABaseCharacter 拆出 (Phase 2.2 重构)
//
// 创建日期: 2026.07.12
// 关联 Phase: Phase 2 — BaseCharacter.cpp 巨型文件拆分
// 最近重构: 2026.07.13 — v40.4 大厂架构重构 (原子化 + 单一节流点)
//
// 拆分来源: BaseCharacter.cpp 第 1568-1801 行 (OnAIRequestAttack_Simple) +
//           第 1825-1933 行 (OnAIAttackMontageEnded) +
//           第 1950-1962 行 (Server_PlayAttackAnim_Implementation) +
//           第 1971-1989 行 (Multicast_PlayAttackAnim_Implementation) +
//           第 2010-2122 行 (Server_ReportAIAttackHit_Implementation + _Validate)
//           + 字段 CachedAIMontage / bIsWaitingForAIMontageCallback
//
// 大厂原则 (Phase 2.2 + v40.4 落地):
//   - 事件驱动: 不依赖 BT 距离检查决定动画生命周期, 蒙太奇自然结束才解锁
//   - 单一真理源: 攻击者身份 (AI/玩家) 在 Owner Character 上, 本组件只做委托
//   - 单一节流点 (v40.4): BTDecorator_CooldownReady 唯一, C++ 不再做节流
//   - 零兜底: 死亡/无武器/蒙太奇解析失败 → 显式 Log + return false
//   - 职责对等: 与 PlayerComboComponent 完全解耦, 互不干扰
//
// 【v40.4 关键变更】OnAIRequestAttack_Simple 不再做本地节流:
//   - 历史 (v22-v40.3): 双重节流 (BT Decorator + C++ LastAIAttackTimeSeconds)
//   - v40.4 修复: 删除 C++ 层节流, BT 配错应立即暴露 (Failed), 不允许 C++ 兜底
//   - 删字段: LastAIAttackTimeSeconds

// ==========================================
// 头文件包含区
// ==========================================
// 引入本组件头文件
#include "Combat/AIAttackComponent.h"

// 引入 Owner Character (访问 bIsCrouched / bIsMovementLocked / GetCurrentWeapon / Multicast_PlayAttackAnim / PlayAnimMontage / bIsCurrentlyAttackerAI)
#include "Characters/BaseCharacter.h"

// 引入 AI 控制器 (访问 GetEffectiveAttackInterval / SetCurrentlyAttacking / SetInAttackCooldown)
#include "Systems/BaseAIController.h"

// 引入 AIController (UE 原生, 用于 Cast 第一层)
#include "AIController.h"

// 引入武器 (访问 CurrentWeapon->StartWeaponTrace / StopWeaponTrace)
#include "Weapons/BaseWeapon.h"

// 引入 AI 攻击蒙太奇解析器 (职责链, v32 零兜底)
#include "Combat/AIAttackMontageResolver.h"

// 引入 SkeletalMeshComponent / UAnimInstance (绑 OnMontageEnded)
#include "Components/SkeletalMeshComponent.h"
#include "Animation/AnimInstance.h"

// 引入 UGameplayStatics::ApplyPointDamage
#include "Kismet/GameplayStatics.h"

// 引入 DamageType
#include "GameFramework/DamageType.h"

// 引入 CharacterMovementComponent (锁速)
#include "GameFramework/CharacterMovementComponent.h"

// 引入阵营守卫 (FFactionTags::CanDamage)
#include "Data/Faction/FactionTags.h"

// 引入 HealthComponent (无敌期检测)
#include "Components/HealthComponent.h"

// 引入 BlackboardComponent (v40.4 P0 关键修复 — 写 BB.CooldownEndTime)
#include "BehaviorTree/BlackboardComponent.h"

// 引入 AIRuntimeConfigComponent (v42 P0 修复 — 恢复 AI 速度)
#include "Systems/AI/AIRuntimeConfigComponent.h"

// UE 引擎日志
#include "Engine/Engine.h"


// ==========================================
// 1. 构造函数
// ==========================================

/**
 * UAIAttackComponent 构造函数
 *
 * 目的: 启用 Tick (本组件不需要 Tick, 蒙太奇结束由事件回调驱动);
 *       初始化状态字段 (UE 反射默认值)。
 *
 * 设计要点:
 *   - PrimaryComponentTick.bCanEverTick = false (蒙太奇回调是事件驱动, 不需要每帧检查)
 *   - CachedAIMontage = nullptr (没有缓存)
 *   - bIsWaitingForAIMontageCallback = false (不在等回调)
 *   - 【v40.4 删除】LastAIAttackTimeSeconds = 0.0 (C++ 层节流已废除, 统一走 BT)
 */
UAIAttackComponent::UAIAttackComponent()
{
	// 组件本身不 Tick — 蒙太奇结束由 UAnimInstance::OnMontageEnded 事件回调驱动
	PrimaryComponentTick.bCanEverTick = false;
}


// ==========================================
// 2. 生命周期
// ==========================================

/**
 * UAIAttackComponent::BeginPlay
 *
 * 【v40.6 P0 修复】空实现 — 不再缓存 OwnerCharacter
 *
 * 历史 (v22-v40.5) 反模式 — BeginPlay 缓存 OwnerCharacter:
 *   - 假设: 组件 BeginPlay 时 Owner 必然有效
 *   - 真实 (BP archetype 问题): v36 已发现 — BP 子类可能让组件 BeginPlay 不按顺序/不运行
 *   - 结果: OwnerCharacter 永久 null → AIAttack 永远失败
 *
 * v40.6 大厂原则:
 *   - 真理源 = GetOwner(), 不缓存
 *   - 每次需要时调 ResolveOwnerCharacter() lazy resolve
 *   - 这是大厂 UE 组件设计标准 (Lyra / Paragon 都用这种模式)
 */
void UAIAttackComponent::BeginPlay()
{
	Super::BeginPlay();
	// 【v40.6 P0】不再缓存 OwnerCharacter — 删除 OwnerCharacter 字段
	// 改用 ResolveOwnerCharacter() 按需 GetOwner() + Cast
}

/**
 * UAIAttackComponent::ResolveOwnerCharacter — 按需解析 Owner
 *
 * v40.6 新增:
 *   - 真理源 = GetOwner() (UE 标准 API)
 *   - 不缓存 (BP archetype 可能让 BeginPlay 不运行)
 *   - 失败 → Log Error + return nullptr (零兜底)
 */
ABaseCharacter* UAIAttackComponent::ResolveOwnerCharacter() const
{
	AActor* Owner = GetOwner();
	if (!Owner)
	{
		// 这种情况: 组件本身挂了但 Owner 已销毁
		// UE 5.6 应该不会到这里 (销毁前会先调 EndPlay)
		// 0 兜底: 直接报错, 让调用方决定如何处理
		UE_LOG(LogTemp, Error,
			TEXT("[AIAttackComponent] ResolveOwnerCharacter: GetOwner() 返回 null. "
			     "组件可能处于销毁中. 组件类=%s, Owner=%s"),
			*GetClass()->GetName(),
			*GetNameSafe(Owner));
		return nullptr;
	}

	ABaseCharacter* BaseChar = Cast<ABaseCharacter>(Owner);
	if (!BaseChar)
	{
		// 配置错: 组件挂到非 ABaseCharacter 的 Actor 上
		// 0 兜底: 直接报错
		UE_LOG(LogTemp, Error,
			TEXT("[AIAttackComponent] ResolveOwnerCharacter: Owner '%s' 不是 ABaseCharacter. "
			     "本组件必须挂在 ABaseCharacter 子类上. 当前 Owner 类型=%s"),
			*Owner->GetName(), *Owner->GetClass()->GetName());
		return nullptr;
	}

	return BaseChar;
}


/**
 * UAIAttackComponent::EndPlay
 *
 * 显式解绑 UAnimInstance::OnMontageEnded 回调, 防残留
 * (大厂原则 - 工业规范: 显式清理优于依赖 UE 隐式行为)
 *
 * 注意: UAnimInstance 可能在 Owner 销毁前被销毁 (动画资源卸载时序),
 *       所以解绑前必须检查 AnimInst 是否有效
 */
void UAIAttackComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 显式解绑蒙太奇结束回调 (如果 Owner 还有 AnimInst)
	ABaseCharacter* OwnerChar = ResolveOwnerCharacter();
	if (OwnerChar)
	{
		if (USkeletalMeshComponent* CharMesh = OwnerChar->GetMesh())
		{
			if (UAnimInstance* AnimInst = CharMesh->GetAnimInstance())
			{
				// RemoveDynamic 是幂等的 — 不存在的 binding 静默跳过
				AnimInst->OnMontageEnded.RemoveDynamic(this, &UAIAttackComponent::OnAIAttackMontageEnded);
			}
		}
	}

	// 清理状态字段
	CachedAIMontage = nullptr;
	bIsWaitingForAIMontageCallback = false;
	// 【v40.4 删除】LastAIAttackTimeSeconds 字段 — 见头文件注释
	// 【v40.6 删除】OwnerCharacter 字段 — 见头文件注释

	Super::EndPlay(EndPlayReason);
}


// ==========================================
// 3. AI 攻击入口 (OnAIRequestAttack_Simple)
// ==========================================

/**
 * OnAIRequestAttack_Simple — AI 专用轻攻击入口 (v40.4 原子化版本)
 *
 * 对应原 BaseCharacter.cpp line 1568-1801 完整实现
 *
 * 完整流程 (v40.4 — 5 个阶段):
 *   阶段 1: 死亡/无武器 检查 (零兜底)
 *   阶段 2: 走 AIAttackMontageResolver 职责链解析蒙太奇 (Level 0 唯一路径)
 *   阶段 3: 播放蒙太奇 + 绑 OnMontageEnded (RemoveDynamic + IsAlreadyBound + AddDynamic)
 *   阶段 4: 同步网络 (Server_PlayAttackAnim) + 攻击扣血入口 (SetAttackerIsAI)
 *   阶段 5: 【v40.4 关键】写 BB.CooldownEndTime (单一真理源 - 历史从 BTTask 收回)
 *
 * 【v40.4 P0 删除】阶段 0 (历史): 本地时间戳节流
 *   - 历史 (v22-v40.3): LastAIAttackTimeSeconds + AttackInterval + SafeInterval 兜底
 *   - v40.4 删除原因: 重复架构, BTDecorator_CooldownReady 已做实时冷却
 *   - 0 兜底原则: BT 配错应立即暴露 (ExecuteTask 返回 Failed), 不允许 C++ 兜底
 *
 * 大厂原则 (v40.4 落地):
 *   - 单一节流点: BT 是唯一节流决策点, C++ 不做防御性兜底
 *
 * 大厂原则 (v35 — AnimNotify 化):
 *   - 任何阶段失败都 return false + Log, 不静默"再试一次"
 *   - 拆分量检查 + 蒙太奇解析, 两层防御 (v40.4 删节流)
 */
bool UAIAttackComponent::OnAIRequestAttack_Simple(ABaseCharacter* InOwnerCharacter)
{
	// ================================================
	// 【v40.7 P0 关键修复】验证传入的 Owner — 不再调用 GetOwner()
	// ================================================
	if (!InOwnerCharacter)
	{
		// 调用方 (BTTask / BaseCharacter 转发壳) 必须传入有效 Pawn
		// 这是大厂修复 CDO 问题的标准手法: 调用方持有真实引用, 显式传入
		UE_LOG(LogTemp, Error,
			TEXT("[AIAttackComponent] OnAIRequestAttack_Simple: InOwnerCharacter 为空! "
			     "调用方必须传入真实 Pawn 实例, 不能传 CDO/Archetype。"));
		return false;
	}

	// 【v40.7 调试日志】确认拿到的是真实 Pawn
	UE_LOG(LogTemp, Display,
		TEXT("[AIAttackComponent] OnAIRequestAttack_Simple ENTER. Owner=%s (Valid=%d)"),
		*InOwnerCharacter->GetName(),
		InOwnerCharacter->IsValidLowLevel() ? 1 : 0);

	// 用传入的 InOwnerCharacter, 不再调用 GetOwner()
	ABaseCharacter* OwnerCharacter = InOwnerCharacter;

	// ============================================================
	// 【v40.4 P0 删除】本地时间戳节流 (防御性兜底)
	// ============================================================
	//
	// 历史 (v22-v40.3) 反模式 (重复架构):
	//   - BTTask 层 (BB.CooldownEndTime + BTDecorator_CooldownReady) 已做实时冷却
	//   - C++ 又加 LastAIAttackTimeSeconds + SafeInterval 节流
	//   - 两个节流叠加, BT 配错时被 C++ 节流"再撑一道墙"
	//   - 根因永远不可见 (用户原问题"AI 持续连击"就是因为这层兜底)
	//
	// 大厂原则:
	//   - 单一节流点: BTDecorator_CooldownReady (实时, 0 延迟, BT 编辑器 100% 可见)
	//   - 0 兜底: BT 配错应立即暴露 (ExecuteTask 返回 Failed), 不允许 C++ 节流掩盖
	//   - 单一职责: OnAIRequestAttack_Simple 只负责"播放蒙太奇", 不做节流决策
	//
	// v40.4 修复: 阶段 1 整段删除, OnAIRequestAttack_Simple 只剩:
	//   - 死/武器检查
	//   - 蒙太奇解析 (Resolver Log Error)
	//   - PlayAnimMontage
	//   - 绑 OnMontageEnded 回调
	//   - 写 BB.CooldownEndTime (单一真理源, OnAIRequestAttack_Simple 内部)
	//   - 设攻击标志 (对称)
	// ============================================================

	// ============================================================
	// 阶段 1: 死了不能打 / 没武器不能打
	// ============================================================
	if (OwnerCharacter->IsDead())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[AIAttackComponent] OnAIRequestAttack_Simple: AI=%s 已死亡, 跳过"),
			*OwnerCharacter->GetName());
		return false;
	}

	if (!OwnerCharacter->GetCurrentWeapon())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[AIAttackComponent] OnAIRequestAttack_Simple: AI=%s 无 CurrentWeapon, 跳过"),
			*OwnerCharacter->GetName());
		return false;
	}

	// ============================================================
	// 阶段 2: 走了大厂 Resolver 职责链 (Level 0 唯一路径 — v32 零兜底)
	// ============================================================
	// 设计要点:
	//   - ComboIndex=1 对应玩家 Combo1 段 (玩家连击序列的第 1 段)
	//   - 如果 BP 没配 Combo1, 解析器会 Log Error 拒绝兜底 (v32 零兜底)
	// ============================================================
	ABaseWeapon* CurrentWeapon = OwnerCharacter->GetCurrentWeapon();
	const FAIAttackMontageResult ResolveResult =
		UAIAttackMontageResolver::ResolveLightAttackMontage(CurrentWeapon, /*ComboIndex=*/1);

	if (!ResolveResult.Montage)
	{
		// 【v32 零兜底】Resolver 内部已 Log Error (组合索引越界/没配), 这里不再重复报警
		UE_LOG(LogTemp, Log,
			TEXT("[AIAttackComponent][OnAIRequestAttack_Simple] AI=%s 攻击放弃 (Level=%d, ComboIndex=%d). "
			     "Resolver 已 Log Error 说明 BP 配置路径修复方法."),
			*OwnerCharacter->GetName(), ResolveResult.FallbackLevel, ResolveResult.ResolvedIndex);
		return false;
	}

	// ============================================================
	// 阶段 3: 播放蒙太奇 + 绑 OnMontageEnded (事件驱动 Cooldown)
	// ============================================================
	// 设计 (v32 大厂架构):
	//   - 用 UAnimInstance::OnMontageEnded (multicast dynamic delegate)
	//   - Combo1 自然播完 → 引擎回调 → OnAIAttackMontageEnded(false)
	//   - OnAIAttackMontageEnded 通知 AIController 攻击结束 → BT 进入冷却
	//   - 即使玩家跑远, 蒙太奇也完整播完 (用户明确要求!)
	//
	// 安全 (P0 2026.07.07 修复 Ensure 崩溃):
	//   - UE TMulticastScriptDelegate::AddInternal 有 ensure: 不能重复 AddDynamic
	//   - 修复: RemoveDynamic 先解绑 + IsAlreadyBound 检查 (双保险)
	// ============================================================
	UAnimMontage* AttackMontage = ResolveResult.Montage;
	const float MontageLen = OwnerCharacter->PlayAnimMontage(AttackMontage, 1.0f, FName("Combo1"));
	if (MontageLen <= 0.0f)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[AIAttackComponent] OnAIRequestAttack_Simple: PlayAnimMontage 失败 (返回 %.2f), "
			     "AI=%s, 武器=%s"),
			MontageLen, *OwnerCharacter->GetName(), *CurrentWeapon->GetName());
		return false;
	}

	// 绑 UAnimInstance::OnMontageEnded 回调
	if (USkeletalMeshComponent* CharMesh = OwnerCharacter->GetMesh())
	{
		if (UAnimInstance* AnimInst = CharMesh->GetAnimInstance())
		{
			// 步骤 1: 先解绑 (RemoveDynamic 是幂等的 — 不存在的 binding 静默跳过)
			AnimInst->OnMontageEnded.RemoveDynamic(this, &UAIAttackComponent::OnAIAttackMontageEnded);

			// 步骤 2: 再绑定 (IsAlreadyBound 检查 — 双保险, 防止 Inst 内部状态异常)
			if (!AnimInst->OnMontageEnded.IsAlreadyBound(this, &UAIAttackComponent::OnAIAttackMontageEnded))
			{
				AnimInst->OnMontageEnded.AddDynamic(this, &UAIAttackComponent::OnAIAttackMontageEnded);
			}

			// 缓存正在播放的攻击蒙太奇 — 用于回调时判断是不是"我触发的蒙太奇"
			// (multicast OnMontageEnded 会对所有结束的蒙太奇广播, 必须过滤)
			CachedAIMontage = AttackMontage;
			bIsWaitingForAIMontageCallback = true;
		}
	}

	// ============================================================
	// 阶段 4: 同步给服务器 + 攻击扣血入口
	// ============================================================
	// 4.1 同步给服务器 (AI 在服务器调用, Server RPC 在服务器本地直接走 _Implementation)
	// 注: 这里不传 Montage 指针, 沿用 ComboIndex 让服务器侧也走相同 fallback
	// 【2026.07.12 P0 重构】RPC 转发壳已上移到 ABaseCharacter, 但 AI 在服务器调用 Server RPC
	//   会触发 UE 5.6 "本地直接 _Implementation" 行为, 等价于直接调 Component 的 _Implementation
	if (OwnerCharacter)
	{
		Server_PlayAttackAnim_Implementation(false, 1);
	}

	// 4.2 攻击扣血入口 — 让 BaseWeapon::Tick 在 trace 命中时知道这是 AI 攻击
	// (蒙太奇播完后在 OnAIAttackMontageEnded 复位 false)
	if (OwnerCharacter)
	{
		OwnerCharacter->SetAttackerIsAI(true);
	}

	// ============================================================
	// 【v40.4 P0 关键修复】写 BB.CooldownEndTime — 单一真理源 (历史从 BTTask 收回)
	// ============================================================
	//
	// 历史 (v22-v40.3) 架构错误链:
	//   - BTTask_PlayAttackMontage 写 BB.CooldownEndTime (硬编码 Key 名 + 重复架构)
	//   - v40.4 重构删了 BTTask 中的写入 → 但**没有补回** OnAIRequestAttack_Simple
	//   - 结果: BB.CooldownEndTime 永远 = 0 → BTDecorator_CooldownReady 看到
	//     CooldownEndTime <= 0 → 永远返回 true → 冷却检查**失效** → AI 一直能攻击
	//
	// v40.4 终极修复 (大厂原则 - 单一真理源 + 单一节流点):
	//   - 唯一入口: OnAIRequestAttack_Simple 成功播蒙太奇后, 写 BB.CooldownEndTime
	//   - 硬编码 Key 名 = AIBlackboardKeyNames::CooldownEndTime = 唯一真理源
	//   - BTDecorator_CooldownReady 实时读 World.Time vs BB.CooldownEndTime, 0 延迟决策
	//   - 不写入 C++ 字段 bIsInAttackCooldown (v15 残留, 由 BT 决策不再用)
	//
	// 为什么不在 BTTask 里写 BB (大厂 BTTask 惯例的破例):
	//   - BTTask 节点标准做法: 用 FBlackboardKeySelector 让策划在 BP 编辑器配 Key 名
	//   - C++ 组件直接调硬编码 Key 名 — 因为 Component 是单一入口, Key 名是真理源
	//   - 但 BTTask_PlayAttackMontage 是"调用 Component 的原子壳", 不该再写副作用
	//   - 副作用全部下沉到 Component (单一职责: Component 知道所有写入路径)
	// ============================================================
	if (UWorld* World = OwnerCharacter->GetWorld())
	{
		float AttackInterval = 1.2f;  // 默认 1.2s 冷却

		// 从 AIController 拿最终攻击间隔 (策划配在 Profile 里)
		if (AAIController* AIC = Cast<AAIController>(OwnerCharacter->GetController()))
		{
			if (const ABaseAIController* BaseAIC = Cast<ABaseAIController>(AIC))
			{
				const float EffectiveInterval = BaseAIC->GetEffectiveAttackInterval();
				if (EffectiveInterval > 0.f)
				{
					AttackInterval = EffectiveInterval;
				}
			}
		}

		// 写 BB.CooldownEndTime (AIBlackboardKeyNames::CooldownEndTime = "CooldownEndTime")
		// 【UE 5.6 const 保护】AAIController::GetBlackboardComponent() 返回 const
		//                  必须在 ABaseAIController 非 const 方法内获取非 const 视图
		if (AAIController* AIC = Cast<AAIController>(OwnerCharacter->GetController()))
		{
			if (UBlackboardComponent* BB = AIC->GetBlackboardComponent())
			{
				const float CurrentTime = World->GetTimeSeconds();
				const float CooldownEndTime = CurrentTime + AttackInterval;
				BB->SetValueAsFloat(FName(AIBlackboardKeyNames::CooldownEndTime), CooldownEndTime);

				// 【v40.5 P0 升级 Display 级 — 让冷却写入可见】
				UE_LOG(LogTemp, Display,
					TEXT("[AIAttackComponent] OnAIRequestAttack_Simple: 写 BB.CooldownEndTime=%.2f (Now=%.2f + AttackInterval=%.2f)"),
					CooldownEndTime, CurrentTime, AttackInterval);
			}
			else
			{
				UE_LOG(LogTemp, Warning,
					TEXT("[AIAttackComponent] OnAIRequestAttack_Simple: AI=%s 拿不到 BlackboardComponent! BT 冷却决策可能失效. "
					     "【v40.4 修复后】BB.CooldownEndTime 必须由本函数写入, 没有 BB 就无法写入."),
					*OwnerCharacter->GetName());
			}
		}
	}

	// ============================================================
	// 【v35 撤回】trace 启动交给 BP AnimNotify (跟玩家路径对称)
	//
	// 旧版 (v32-v34): C++ 主动开 trace
	//   - 问题: trace 窗口与蒙太奇挥刀动作不同步 (蒙太奇第一帧就开)
	//   - 根因: 美术想在"挥刀中段"开, "收刀"关, C++ 硬编码做不到
	//
	// 新版 (v35): BP AnimNotify 控制
	//   - 美术在 UE 编辑器打开 AI 蒙太奇 (跟武器共用同一个)
	//   - 在"挥刀中段"位置加 ANS_MeleeTrace → StartWeaponTrace
	//   - 在"收刀"位置加 ANS_MeleeTraceEnd → StopWeaponTrace
	//   - BP AnimNotify 拿武器: WeaponAttach->GetCurrentWeapon() (蓝图纯函数节点)
	//
	// 大厂原则 - 职责对等:
	//   - 启动 trace = 美术/策划的责任 (蒙太奇节奏)
	//   - 结束 trace = C++ 防泄漏清理 (蒙太奇自然结束时强制 StopWeaponTrace)
	//   - 这是"清理逻辑"不是"业务兜底" — 蒙太奇必然结束, 防 BP 通知漏触发造成 trace 永远开启
	// ============================================================

	UE_LOG(LogTemp, Log,
		TEXT("[AIAttackComponent] OnAIRequestAttack_Simple: AI=%s 成功播放攻击动画 "
		     "(Level=%d, Index=%d, Montage=%s, Length=%.2fs, 扣血等待 trace 命中)"),
		*OwnerCharacter->GetName(),
		ResolveResult.FallbackLevel,
		ResolveResult.ResolvedIndex,
		*AttackMontage->GetName(),
		MontageLen);
	return true;
}


// ==========================================
// 4. AI 攻击蒙太奇结束回调
// ==========================================

/**
 * OnAIAttackMontageEnded — AI 攻击蒙太奇自然/被打断结束时由引擎回调
 *
 * 对应原 BaseCharacter.cpp line 1825-1933 完整实现
 *
 * 完整流程 (5 个步骤, v40.4 收敛):
 *   步骤 1: Montage 参数验证 — 过滤掉非本组件触发的蒙太奇结束
 *   步骤 2: bIsWaitingForAIMontageCallback 标志验证 — 防二次回调
 *   步骤 3: 通知 AIController 攻击阶段结束 (SetCurrentlyAttacking(false))
 *   步骤 4: 清理缓存 (CachedAIMontage / bIsWaitingForAIMontageCallback)
 *   步骤 5: 对称关闭 trace + 还原攻击者标志 (SetAttackerIsAI(false))
 *   - 历史 (v10-v15): bIsInAttackCooldown 由 BTTask 状态 2 SetInAttackCooldown(true)
 *   - v22 架构重构后: BTTask 不再设 bIsInAttackCooldown(true), 此字段形同虚设
 *   - v40.4 修复后: 冷却决策由 BT 全权 (BB.CooldownEndTime + Decorator_CooldownReady)
 *   - 打断分支仍保留 SetInAttackCooldown(false) 是**对称清理**, 防止历史残留干扰
 *     v15 旧 BT 实例可能还持有 SetInAttackCooldown(true) 状态
 *
 * 大厂原则 (v40.4 落地):
 *   - 单一节流点: BTDecorator_CooldownReady 实时读 BB.CooldownEndTime vs World.Time
 *   - 本回调只管"通知 AIController 解锁 + 关闭 trace", 不写 BB、不设冷却
 *   - 打断分支 (bInterrupted=true) → SetInAttackCooldown(false) 清理残留状态 (历史兼容)
 *
 * 历史注释澄清 (v10 时代):
 *   - 旧版说"Cooldown 在蒙太奇自然结束时不主动清零, 由 BTTask::ConsumeAttackToken Timer 负责"
 *   - v22 架构重构已删除 bHasAttackToken + ConsumeAttackToken Timer
 *   - v40.4 重构后冷却 = BTDecorator_CooldownReady 实时读 BB.CooldownEndTime
 *   - 但 bIsInAttackCooldown (BaseAIController 字段) 仍然存在 — 仅作 TickChaseFallback
 *     兼容标记, 不参与 v40.4 冷却决策 (决策由 BT 全权负责)
 */
void UAIAttackComponent::OnAIAttackMontageEnded(UAnimMontage* Montage, bool bInterrupted)
{
	// ============================================================
	// 【v40.7】用传入的 Owner (OnAIRequestAttack_Simple 已在同一调用链传入)
	// 由于 OnAIAttackMontageEnded 是 UAnimInstance 回调, 无法传参
	// 改用 GetOwner() 的 InPlacePawn 版本 (见下面)
	// ============================================================
	ABaseCharacter* OwnerCharacter = nullptr;
	if (AActor* Owner = GetOwner())
	{
		OwnerCharacter = Cast<ABaseCharacter>(Owner);
	}
	if (!OwnerCharacter)
	{
		// Owner 已销毁或组件被错误挂载 — 蒙太奇回调是 UAnimInstance 触发,
		// 即使 Owner Destroy, AnimInstance 可能仍残留着回调. 静默 return 不报错 (兼容销毁路径)
		return;
	}

	// ============================================================
	// 步骤 1: Montage 参数验证 — 忽略不匹配的蒙太奇 (防误触发)
	// ============================================================
	// UAnimInstance::OnMontageEnded 是 multicast, 会对所有结束的蒙太奇广播
	// 我们只处理自己 CachedAIMontage (本次攻击触发的) 的结束事件
	if (Montage != CachedAIMontage)
	{
		return;  // 不是我们这次触发的, 忽略 (例如: 切换武器/受伤/死亡导致别的蒙太奇结束)
	}

	// ============================================================
	// 步骤 2: 等待回调标志验证 — 防二次回调
	// ============================================================
	if (!bIsWaitingForAIMontageCallback)
	{
		// 不在等回调 (防御: 收到二次回调, 第一次已处理)
		return;
	}

	// ============================================================
	// 步骤 3: 通知 AIController 攻击阶段结束
	// ============================================================
	AAIController* AIC = Cast<AAIController>(OwnerCharacter->GetController());
	ABaseAIController* BaseAIC = Cast<ABaseAIController>(AIC);

	if (!BaseAIC)
	{
		// 没 AIController (可能是玩家), 不处理 AI 状态
		return;
	}

	// 核心: 通知 AIController 攻击阶段结束
	//   1. C++ 层: bIsCurrentlyAttacking=false
	//      BT 装饰器 DistanceCheck 看到 false → 重新选 Sequence
	//   2. BT 层: Cooldown 机制自行处理
	//      自然结束 (bInterrupted=false): Cooldown 在 BT 端计时
	//      被打断 (bInterrupted=true): 立即解锁, BT 重新评估
	BaseAIC->SetCurrentlyAttacking(false);

	// 【v10 大厂架构重构】Cooldown 在蒙太奇结束时**不**清零!
	// 自然结束 → 由 BTTask::ConsumeAttackToken Timer 负责清零
	// 打断 (bInterrupted=true) → 必须立即清, 否则 AI 永远卡在冷却中
	const bool bExpectCooldownEnd = !bInterrupted;

	if (bInterrupted)
	{
		BaseAIC->SetInAttackCooldown(false);
	}

	UE_LOG(LogTemp, Log,
		TEXT("[AIAttackComponent] OnAIAttackMontageEnded: AI=%s 蒙太奇结束 (Interrupted=%s, ExpectCooldownEnd=%s), "
		     "SetCurrentlyAttacking=false; Cooldown %s"),
		*OwnerCharacter->GetName(),
		bInterrupted ? TEXT("true") : TEXT("false"),
		bExpectCooldownEnd ? TEXT("true") : TEXT("false"),
		bInterrupted ? TEXT("已立即清 (打断分支)") : TEXT("保留, 由 Timer 自然清 (正常分支)"));

	// ============================================================
	// 步骤 5: 清理缓存 (避免下次攻击时复用旧状态)
	// ============================================================
	bIsWaitingForAIMontageCallback = false;
	CachedAIMontage = nullptr;

	// ============================================================
	// 步骤 6: 对称关闭 trace + 还原攻击者标志
	// ============================================================
	// 这是 OnAIRequestAttack_Simple SetAttackerIsAI(true) 的对称关闭
	//
	// v35 后: StartWeaponTrace 由 BP AnimNotify 触发 (玩家路径一致), C++ 不再主动开启
	// 但这里 StopWeaponTrace 必须保留 —— 蒙太奇自然结束 = 生命周期的必然事件,
	// 防止 BP 通知漏触发 → bIsWeaponActive 永远残留为 true → 下次挥刀全员扣血
	//
	// 关闭条件: 蒙太奇自然结束 / 被打断 / AI 死亡, 否则:
	//   1. bIsCurrentlyAttackerAI 残留为 true
	//      → 玩家路径的轻击 trace 命中时也走 AI 通道 (读 ConfigSO.Damage 而非 LightDamageBody)
	//   2. bIsWeaponActive 残留为 true → trace 永远激活, 任何人接近都扣血
	//
	// 单一职责: 攻击者标志由攻击发起方管理, 武器 Tick 只读不写
	OwnerCharacter->SetAttackerIsAI(false);

	if (OwnerCharacter->HasAuthority() && OwnerCharacter->GetCurrentWeapon())
	{
		// 对称关闭 trace — 这是"生命周期清理"不是"业务兜底"
		// (即便 BP 蓝图 AnimNotify 也会调 StopWeaponTrace, 这里再调一次幂等无害)
		OwnerCharacter->GetCurrentWeapon()->StopWeaponTrace();
	}

	UE_LOG(LogTemp, Verbose,
		TEXT("[AIAttackComponent] OnAIAttackMontageEnded: 已还原攻击者标志 (Player 路径) + 关闭 trace, AI=%s"),
		*OwnerCharacter->GetName());

	// ============================================================
	// 【v42 P0 修复】恢复 AI 移动速度
	// ============================================================
	// 根因: Server_PlayAttackAnim_Implementation 中设 MaxWalkSpeed=0,
	//        但攻击结束后没有恢复速度 → AI 物理引擎 fallback 到默认值(玩家速度 400)
	// 修复: 攻击蒙太奇结束后调用 RestoreMaxWalkSpeedFromConfig 恢复配置表速度
	// 大厂原则 - 职责对等: 与玩家路径 PlayerComboComponent::EndAttackState 对称
	RestoreMaxWalkSpeedFromConfig(OwnerCharacter);
}


// ==========================================
// 5. AI 速度恢复 (v42 P0 修复)
// ==========================================

/**
 * RestoreMaxWalkSpeedFromConfig — 从 AIRuntimeConfigComponent 恢复 MaxWalkSpeed
 *
 * 根因: Server_PlayAttackAnim_Implementation 中设 MaxWalkSpeed=0,
 *        但 OnAIAttackMontageEnded 中没有恢复速度
 *
 * 大厂原则 - 职责对等:
 *   - 玩家路径: PlayerComboComponent::EndAttackState 恢复速度
 *   - AI 路径: 本函数恢复速度 (与玩家路径对称)
 *
 * @param OwnerCharacter 有效 Pawn (调用方保证非空)
 */
void UAIAttackComponent::RestoreMaxWalkSpeedFromConfig(ABaseCharacter* OwnerCharacter)
{
	if (!OwnerCharacter)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[AIAttackComponent] RestoreMaxWalkSpeedFromConfig: OwnerCharacter 为空, 跳过速度恢复"));
		return;
	}

	UCharacterMovementComponent* MoveComp = OwnerCharacter->GetCharacterMovement();
	if (!MoveComp)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[AIAttackComponent] RestoreMaxWalkSpeedFromConfig: MoveComp 为空, Pawn=%s"),
			*OwnerCharacter->GetName());
		return;
	}

	// 1. 尝试从 AIController 的 RuntimeConfig 读配置表速度
	AAIController* AIC = Cast<AAIController>(OwnerCharacter->GetController());
	ABaseAIController* BaseAIC = Cast<ABaseAIController>(AIC);

	if (!BaseAIC)
	{
		// 【零兜底】Controller 不是 ABaseAIController → 显式报错, 不静默用默认值
		UE_LOG(LogTemp, Error,
			TEXT("[AIAttackComponent] RestoreMaxWalkSpeedFromConfig: AI=%s 的 Controller 不是 ABaseAIController, "
				 "无法获取 AIRuntimeConfigComponent. AI 将保持 MaxWalkSpeed=0 (无法移动). "
				 "【修复】确保 AI 使用 AMeleeAIController 或 ABaseAIController"),
			*OwnerCharacter->GetName());
		return;
	}

	UAIRuntimeConfigComponent* RuntimeConfig = BaseAIC->GetRuntimeConfig();
	if (!RuntimeConfig)
	{
		// 【零兜底】RuntimeConfig 为空 → 显式报错, 不静默用默认值
		UE_LOG(LogTemp, Error,
			TEXT("[AIAttackComponent] RestoreMaxWalkSpeedFromConfig: AI=%s 的 AIController RuntimeConfig 为空, "
				 "AI 没有移动速度配置. AI 将保持 MaxWalkSpeed=0 (无法移动). "
				 "【v54 修复】检查 DA_AIBehaviorConfig_XXX 是否正确分配给 AIController 的 RuntimeConfig 字段 (DA_AIProfile_XXX 已删除)"),
			*OwnerCharacter->GetName());
		return;
	}

	const FAIMovementParams MoveParams = RuntimeConfig->GetScaledMovement();
	if (MoveParams.WalkSpeed <= 0.f)
	{
		// 【零兜底】配置表 WalkSpeed <= 0 → 显式报错, 不静默用默认值
		UE_LOG(LogTemp, Error,
			TEXT("[AIAttackComponent] RestoreMaxWalkSpeedFromConfig: AI=%s 的配置表 WalkSpeed=%.0f <= 0, "
				 "AI 将保持 MaxWalkSpeed=0 (无法移动). "
				 "【修复】DA_AIBehaviorConfig_XXX → Movement → WalkSpeed 设置 > 0 (例如 250)"),
			*OwnerCharacter->GetName(), MoveParams.WalkSpeed);
		return;
	}

	// 速度恢复成功
	MoveComp->MaxWalkSpeed = MoveParams.WalkSpeed;
	UE_LOG(LogTemp, Log,
		TEXT("[AIAttackComponent] RestoreMaxWalkSpeedFromConfig: AI=%s 速度恢复为 WalkSpeed=%.0f (配置表)"),
		*OwnerCharacter->GetName(), MoveParams.WalkSpeed);
}


// ==========================================
// 5. 网络同步 RPC 实现
// ==========================================

/**
 * Server_PlayAttackAnim_Implementation
 *
 * 对应原 BaseCharacter.cpp line 1950-1962 完整实现
 *
 * Server RPC 实现: 客户端向服务器请求挥刀
 *   1. 服务器端同步锁速 (bIsMovementLocked + MaxWalkSpeed=0)
 *      这样服务器和客户端的物理推演完全一致, 再也不会发生"强行拽人"的瞬移
 *   2. 服务器收到请求后, 直接向全频道广播
 *
 * 注意: 本组件的 RPC 只被 AI 路径触发 (OnAIRequestAttack_Simple 调用了 Server_PlayAttackAnim)
 *       玩家路径仍走 BaseCharacter 自己的 Server_PlayAttackAnim (PlayerComboComponent 调用)
 *       两条路径在 Multicast_PlayAttackAnim 层汇合
 */
void UAIAttackComponent::Server_PlayAttackAnim_Implementation(bool bIsHeavy, int32 InComboIndex)
{
	// 【v40.6 P0】按需解析 Owner — 不依赖 BeginPlay 缓存
	ABaseCharacter* OwnerCharacter = ResolveOwnerCharacter();
	if (!OwnerCharacter)
	{
		return;
	}

	// 服务器端也必须同步锁死速度
	// 这样服务器和客户端的物理推演就完全一致了, 再也不会发生"强行拽人"的瞬移
	ABaseWeapon* CurrentWeapon = OwnerCharacter->GetCurrentWeapon();
	if (CurrentWeapon && !CurrentWeapon->bCanMoveWhileLightAttack)
	{
		OwnerCharacter->bIsMovementLocked = true;
		OwnerCharacter->GetCharacterMovement()->MaxWalkSpeed = 0.0f;
	}

	// 服务器收到请求后, 直接向全频道广播
	// 注: Multicast_PlayAttackAnim 仍在 BaseCharacter 上 (玩家/AI 共用)
	OwnerCharacter->Multicast_PlayAttackAnim(bIsHeavy, InComboIndex);
}


/**
 * Server_PlayAttackAnim_Validate
 *
 * 对应原 BaseCharacter.cpp line 2492-2495 校验逻辑
 *
 * 校验: InComboIndex 在合法范围 (0~10)
 * 返回 false 服务器会断开连接 (反作弊)
 */
bool UAIAttackComponent::Server_PlayAttackAnim_Validate(bool bIsHeavy, int32 InComboIndex)
{
	return InComboIndex >= 0 && InComboIndex <= 10;
}


/**
 * Multicast_PlayAttackAnim_Implementation
 *
 * 对应原 BaseCharacter.cpp line 1971-1989 完整实现
 *
 * NetMulticast 实现: 服务器向所有客户端广播
 *   1. 发起攻击的本地玩家自己已经播过动画了, 防鬼畜直接跳过
 *   2. 取第 0 个蒙太奇 (所有轻击招式都在这里面)
 *   3. 根据服务器传来的 InComboIndex, 智能推断该播哪个片段 (Combo1/Combo2)
 *
 * 注意: Multicast 在 UFUNCTION 中只能发到 Server -> All Clients (跨进程), 不会回到发送者
 *       所以本地控制者已经在 OwnerCharacter->Multicast_PlayAttackAnim 调用前的 PlayAnimMontage 播过
 *       (对 AI 而言, AI Controller 在服务器, OnAIRequestAttack_Simple 也在服务器调, 所以 AI 路径不会"鬼畜")
 *       但安全起见仍保留 IsLocallyControlled 跳过 — 与原 BaseCharacter 行为一致
 */
void UAIAttackComponent::Multicast_PlayAttackAnim_Implementation(bool bIsHeavy, int32 InComboIndex)
{
	// 【v40.6 P0】按需解析 Owner — 不依赖 BeginPlay 缓存
	ABaseCharacter* OwnerCharacter = ResolveOwnerCharacter();
	if (!OwnerCharacter)
	{
		return;
	}

	// 发起攻击的本地玩家自己已经播过动画了, 防鬼畜直接跳过
	if (OwnerCharacter->IsLocallyControlled())
	{
		return;
	}

	ABaseWeapon* CurrentWeapon = OwnerCharacter->GetCurrentWeapon();
	if (CurrentWeapon)
	{
		// 工业级做法: 直接拿第 0 个蒙太奇 (因为所有轻击招式都在这里面)
		UAnimMontage* MontageToPlay = CurrentWeapon->GetAttackMontage(bIsHeavy, 0);
		if (MontageToPlay)
		{
			// 根据服务器传来的 InComboIndex, 智能推断该播哪个片段
			const FName SectionName = (InComboIndex == 1) ? FName("Combo1") : FName("Combo2");

			// 让其他玩家屏幕上的你, 也精准跳到对应的连招片段
			OwnerCharacter->PlayAnimMontage(MontageToPlay, 1.0f, SectionName);
		}
	}
}


// ==========================================
// 6. AI 攻击伤害上报 (备用通道, 服务器权威)
// ==========================================

/**
 * Server_ReportAIAttackHit_Implementation
 *
 * 对应原 BaseCharacter.cpp line 2010-2100 完整实现
 *
 * Server RPC 实现: AI 攻击伤害上报 (备用通道, 当前 AI 攻击走 BaseWeapon::Server_ReportHit 路径)
 *
 * 大厂原则 (零兜底):
 *   - HitActor 空 → Log Warning + return
 *   - 非 ABaseCharacter → Log Warning + return
 *   - 目标已死 → return (防重复扣血)
 *   - 自伤 → return (AI 不会打自己)
 *   - 友军 (FFactionTags::CanDamage) → return (单一真理源守卫)
 *   - 无敌期 (HealthComponent::IsInvincible) → Layer 0 已被拦截, 这里仅 Verbose 日志
 *
 * 防御层级:
 *   Layer 0 (HealthComponent::ApplyDamage): bIsInvincible=true → return 0 (单一拦截点)
 *   Layer 1 (本函数): 阵营检查 + 自伤检查 + 友军检查 + Verbose 日志
 */
void UAIAttackComponent::Server_ReportAIAttackHit_Implementation(AActor* HitActor, float Damage)
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
			TEXT("[AIAttackComponent] Server_ReportAIAttackHit: AI=%s 收到空 HitActor, 忽略"),
			*OwnerCharacter->GetName());
		return;
	}

	// 防御 2: 目标必须是 ABaseCharacter
	ABaseCharacter* Victim = Cast<ABaseCharacter>(HitActor);
	if (!Victim)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[AIAttackComponent] Server_ReportAIAttackHit: AI=%s -> HitActor=%s 不是 ABaseCharacter, 忽略"),
			*OwnerCharacter->GetName(), *HitActor->GetName());
		return;
	}

	// 防御 3: 目标已死则不扣 (避免重复扣血)
	if (Victim->IsDead())
	{
		return;
	}

	// 防御 4: 自伤防御 (AI 不会打自己)
	if (Victim == OwnerCharacter)
	{
		return;
	}

	// 防御 5: 【2026.07.11 P0 大厂架构】友军伤害守卫 (Friendly Fire Guard)
	// 单一真理源: 走 FFactionTags::CanDamage 集中校验
	//   - 同阵营 → 拒绝扣血 + Log Warning (用户需求: 队友之间不能有队伤)
	//   - 任一阵营无效 → 拒绝扣血 + Log Error (强制修复 Pawn.FactionTag 同步链路)
	//   - 异阵营 → 通过
	if (!FFactionTags::CanDamage(
			OwnerCharacter->GetFactionTag(),
			Victim->GetFactionTag(),
			TEXT("UAIAttackComponent::Server_ReportAIAttackHit"),
			OwnerCharacter->GetName(),
			Victim->GetName()))
	{
		// CanDamage 内部已 Log 错误原因, 这里直接静默 return
		return;
	}

	// 防御 6: 【2026.07.11 P0 大厂架构】复活无敌期早期观察点 (与 BaseWeapon::Server_ReportHit 同构)
	// 单一真理源: HealthComponent->bIsInvincible (Layer 0 ApplyDamage 已拦截)
	// 这里仅 Verbose 日志, 不重复拦截 (大厂原则 - 零重复)
	// [v40 P0 修复] 必须用 ResolveHealthComponent() 而非裸字段 — BP archetype 会 nullify
	if (UHealthComponent* VictimHC = Victim->ResolveHealthComponent())
	{
		if (VictimHC->IsInvincible())
		{
			UE_LOG(LogTemp, Verbose,
				TEXT("[AIAttackComponent::Server_ReportAIAttackHit] Victim=%s 在无敌期, 已被 HealthComponent Layer 0 拦截, 剩余=%.2fs"),
				*Victim->GetName(),
				VictimHC->GetInvincibilityRemainingSeconds());
		}
	}

	const float FinalDamage = FMath::Max(Damage, 0.0f);

	// 走 UGameplayStatics::ApplyPointDamage 走标准伤害事件链
	// 注意: 用 Victim->GetActorLocation() 当 HitLocation 是简化, 实际应该用武器刀刃 Trace
	//       现阶段 AI 攻击没有 Trace 接入, 用"AI 当前世界坐标"近似, 视觉效果无差 (伤害无距离衰减)
	FHitResult HitInfo(Victim, Victim->GetMesh(), OwnerCharacter->GetActorLocation(), -OwnerCharacter->GetActorForwardVector());

	UGameplayStatics::ApplyPointDamage(
		Victim,
		FinalDamage,
		-OwnerCharacter->GetActorForwardVector(),  // 攻击来向 (从 AI 指向玩家)
		HitInfo,
		OwnerCharacter->GetController(),            // Instigator 是 AI 的 Controller
		OwnerCharacter,                             // DamageCauser 是 AI Character
		UDamageType::StaticClass()
	);

	UE_LOG(LogTemp, Log,
		TEXT("[AIAttackComponent] Server_ReportAIAttackHit: AI=%s -> Victim=%s, Damage=%.1f (由 ConfigSO.Damage 决定)"),
		*OwnerCharacter->GetName(), *Victim->GetName(), FinalDamage);
}


/**
 * Server_ReportAIAttackHit_Validate
 *
 * 对应原 BaseCharacter.cpp line 2107-2122 完整实现
 *
 * 校验: HitActor 非空, Damage 在合法范围 (0~10000)
 */
bool UAIAttackComponent::Server_ReportAIAttackHit_Validate(AActor* HitActor, float Damage)
{
	// 1. HitActor 非空
	if (!HitActor)
	{
		return true;  // HitActor 空不算作弊, 实施时拒绝即可
	}

	// 2. Damage 在合法范围
	if (Damage < 0.0f || Damage > 10000.0f)
	{
		return false;  // 范围异常, 视为客户端作弊
	}

	return true;
}