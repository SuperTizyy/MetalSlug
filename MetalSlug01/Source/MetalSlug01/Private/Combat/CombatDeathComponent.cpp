// ==========================================
// 版权声明：在项目设置的描述页面填写您的版权信息。
// ==========================================
//
// @file CombatDeathComponent.cpp
// @brief 战斗死亡/无敌期 子系统 — 实现
//
// 【大厂原则】
//   - 单一真理源: 字段在本组件, 武器自治溶解, HealthComponent 持无敌期数据
//   - 零兜底: 所有 nullptr/配置缺失都 Log Error + 显式修复路径
//   - RPC 纯数据化: 不传 Actor* (避免跨边界生命周期崩溃)
//   - 集中调度: 击杀结算在 PerformKillSettlement 一个函数里
//
// 【调用链】
//   UE 标准 TakeDamage → 本组件 TakeDamage → PerformKillSettlement (IsDead)
//                                                          ↓
//                                                       死亡事件 → Die() → Multicast_Die
//                                                                             ↓
//                                                                       ExecuteDeathLocal
//                                                                             ↓
//                                                                       EnableRagdoll (定时)
// ==========================================

// ==========================================
// 头文件包含区
// ==========================================
#include "Combat/CombatDeathComponent.h"

// 包含 BaseCharacter 以访问 Owner->HealthComponent 等
#include "Characters/BaseCharacter.h"

// 包含 BaseWeapon 以访问 Weapon->StopDamageTrace/StartDissolve
#include "Weapons/BaseWeapon.h"

// 包含 HealthComponent 以调 ApplyDamage/ActivateInvincibility/DeactivateInvincibility
#include "Components/HealthComponent.h"

// 包含 DissolveComponent 以调 StartDissolveImmediate
#include "Components/DissolveComponent.h"

// 包含 HealthRegenComponent 以重置回血状态
#include "Components/HealthRegenComponent.h"

// 包含 RoomGameMode 以调 RequestRespawn
#include "Systems/RoomGameMode.h"

// 【v39 修复】包含 RoomSpawnSubsystem 以调 ReleaseSpawnPoint
// 根因: ReleaseSpawnPoint 整个项目 0 调用 — 死亡时出生点永远不释放
//       → 玩家复活时 GetAvailableSpawnPointForFaction 看到全部 5 个出生点被占用 → 返回 nullptr → 拒绝 Spawn → 玩家不复活
// 修复: 在 ExecuteDeathLocal 中调 ReleaseSpawnPoint (集中调度, 唯一释放入口)
#include "Systems/Spawn/RoomSpawnSubsystem.h"

// 【2026.07.13 v40.6 反扎堆账本】包含 TargetingSubsystem 以调 ReleaseTarget
// 根因: AI 死亡时账本 AIHuntingMap 不释放 → 其他 AI 反扎堆评分时把"已死 AI 锁定的目标"也算进去
//       → 死目标仍被标记为"被锁定" → 其他 AI 不选它 → 永远不攻击死目标 (评分被惩罚)
//       → 反扎堆账本残留下, 新 AI 找不到该目标 (永远 unavailable)
// 修复: 在 ExecuteDeathLocal 中调 ReleaseTarget (与 ReleaseOccupiedSpawnPoint 对等模式)
#include "Systems/Targeting/RoomTargetingSubsystem.h"

// 包含 RoomPlayerController 以调 StartRespawnTimer
#include "Systems/RoomPlayerController.h"

// 包含 RoomPlayerState 以读 GetPlayerName (击杀结算用)
#include "Systems/Core/RoomPlayerState.h"

// 【v99.1 大厂架构】母体复活位置真理源 — BaseAIController.CachedDeathTransform
#include "Systems/BaseAIController.h"

// 包含 FactionTags 集中定义 (CanDamage 三层防御)
#include "Data/Faction/FactionTags.h"
#include "Data/Tables/KillIconTableRow.h" // 【v105 新增】FKillStreakIconInfo (服务器查表获取击杀音效)

// UE 引擎组件: 移动/胶囊体/骨骼网格
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/MeshComponent.h"
#include "PhysicsEngine/PhysicsAsset.h" // UPhysicsAsset 完整定义 (GetNameSafe 需要 UObjectBaseUtility 派生)

// UE 反射
#include "Net/UnrealNetwork.h"     // DOREPLIFETIME (本组件不需要, 这里只是占位)
#include "TimerManager.h"           // SetTimer 定时器
#include "Animation/AnimInstance.h" // PlayAnimMontage 返回值
#include "Animation/AnimMontage.h"  // UAnimMontage 类型


// ==========================================
// 1. 构造函数
// ==========================================

/**
 * UCombatDeathComponent 构造函数
 *
 * 目的: 不创建子组件 (本组件无子组件), 仅初始化默认值
 * 关键: 不启用 Tick (本组件不需要每帧 Tick, 所有逻辑都是事件驱动)
 */
UCombatDeathComponent::UCombatDeathComponent()
{
	// 不需要 Tick: 死亡流程全部由事件驱动 (HealthComponent.OnDeath → Die → Multicast)
	PrimaryComponentTick.bCanEverTick = false;
	PrimaryComponentTick.bStartWithTickEnabled = false;

	// 默认值 (已在头文件初始化, 这里不再重复)
}


// ==========================================
// 2. UE 生命周期
// ==========================================

/**
 * BeginPlay: 缓存 Owner Character 引用
 *
 * 大厂原则: Owner 引用缓存, 后续访问 O(1)
 *
 * 注: BaseCharacter 应该在构造函数中 CreateDefaultSubobject 本组件,
 *     所以 BeginPlay 时 Owner 必定存在 (UE 强制)
 */
void UCombatDeathComponent::BeginPlay()
{
	Super::BeginPlay();

	// 缓存 Owner Character 引用
	OwnerCharacter = Cast<ABaseCharacter>(GetOwner());
	if (!OwnerCharacter.IsValid())
	{
		UE_LOG(LogTemp, Error,
			TEXT("[CombatDeathComponent] BeginPlay: Owner 不是 ABaseCharacter (Owner=%s). "
				 "本组件需要挂在 ABaseCharacter 上."),
			*GetNameSafe(GetOwner()));
		return;
	}

	UE_LOG(LogTemp, Log,
		TEXT("[CombatDeathComponent] BeginPlay: Owner=%s, RagdollDuration=%.2f, WeaponDestroyDelay=%.2f, "
			 "RespawnDelay=%.2f, DefaultInvincibility=%.2f"),
		*GetNameSafe(GetOwner()),
		RagdollDurationSeconds, WeaponDestroyDelaySeconds,
		RespawnDelaySeconds, DefaultSpawnInvincibilitySeconds);

	// ============================================================
	// 【v60.9 大厂架构 — 角色渲染配置诊断】
	//   用户反馈 (2026.07.20 Session.log): "进游戏就是大字型 (T-pose)"
	//   根因 (BP 配置): BP_角色 的 SkeletalMeshComponent 没设 AnimClass
	//   旧 (v60.8) 反模式: 不校验, 玩家看 T-pose 不知道为啥
	//   新 (v60.9) 大厂原则 — 错误尽早暴露:
	//     BeginPlay 立即检查 Mesh 渲染配置, 缺一项就 Log Error + 列出所有信息
	//     让玩家一看日志就知道怎么修 BP
	// ============================================================
	DiagnoseMeshRenderingSetup();
}


// ==========================================
// 7.1 Mesh 渲染配置诊断 — T-pose 根因暴露
// ==========================================

/**
 * DiagnoseMeshRenderingSetup — 检查角色 Mesh 渲染配置, 避免 T-pose
 *
 * 检查项 (每项都必须配置, 否则角色渲染异常):
 *   1. Mesh 组件存在
 *   2. Skeletal Mesh 资产已设
 *   3. AnimClass 已设 (T-pose 最常见原因)
 *   4. 物理资产 (PhysicsAsset) 已设 (死亡 Ragdoll 需要)
 *
 * 错误处理 (大厂原则 — 零兜底):
 *   - 缺任何一项立即 Log Error
 *   - 错误日志列出**精确修复路径** (UE 编辑器具体面板名)
 *   - **不强制 Exit**, 让游戏继续跑 (玩家可继续调试)
 */
void UCombatDeathComponent::DiagnoseMeshRenderingSetup() const
{
	if (!OwnerCharacter.IsValid())
	{
		return;
	}
	ABaseCharacter* Owner = OwnerCharacter.Get();

	USkeletalMeshComponent* MeshComp = Owner->GetMesh();
	if (!MeshComp)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[CombatDeathComponent][v60.9 诊断] Pawn=%s 缺少 Mesh 组件 (SkeletalMeshComponent). "
			     "【修复】UE 编辑器打开 BP_%s → Components 面板添加 SkeletalMeshComponent (命名 'Mesh') "
			     "→ Details → Set Skeletal Mesh = SK_角色模型 资产."),
			*Owner->GetName(), *Owner->GetClass()->GetName());
		return;
	}

	// (1) Skeletal Mesh 资产
	USkeletalMesh* SkelMesh = MeshComp->GetSkeletalMeshAsset();
	if (!SkelMesh)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[CombatDeathComponent][v60.9 诊断] Pawn=%s 的 Mesh 组件没设 Skeletal Mesh 资产 → 角色看不见/白模. "
			     "【修复】UE 编辑器打开 BP_%s → Components → 选中 Mesh → Details → Skeletal Mesh = SK_角色模型 资产."),
			*Owner->GetName(), *Owner->GetClass()->GetName());
		return;
	}

	// (2) AnimClass — T-pose 最常见根因
	TSubclassOf<UAnimInstance> AnimClass = MeshComp->AnimClass;
	if (!AnimClass)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[CombatDeathComponent][v60.9 诊断] Pawn=%s 的 Mesh 组件没设 AnimClass → 角色大字型 (T-pose)! "
			     "【修复】UE 编辑器打开 BP_%s → Components → 选中 Mesh → Details → 搜索 'Anim' → Anim Class = ABP_角色 (继承自 UBaseAnimInstance 的 AnimBlueprint). "
			     "【大厂原则】AnimBlueprint 必须以 'UBaseAnimInstance' 为父类, 否则 C++ 端的 Native* 回调不触发."),
			*Owner->GetName(), *Owner->GetClass()->GetName());
	}
	else
	{
		UE_LOG(LogTemp, Verbose,
			TEXT("[CombatDeathComponent][v60.9 诊断] Pawn=%s AnimClass 配置正常: %s"),
			*Owner->GetName(), *AnimClass->GetName());
	}

	// (3) PhysicsAsset — 死亡 Ragdoll 需要
	UPhysicsAsset* PhysAsset = MeshComp->GetPhysicsAsset();
	if (!PhysAsset)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[CombatDeathComponent][v60.9 诊断] Pawn=%s 的 Skeletal Mesh '%s' 没设 PhysicsAsset → 死亡 Ragdoll 不工作, 角色会原地 T-pose 而不是倒下. "
			     "【修复】Content Browser 双击 SK_角色 → Window → Asset Details → Physics → Physics Asset = PA_角色 资产 (如果没创建过, 工具 → Set Up Rig → 重生成)."),
			*Owner->GetName(), *SkelMesh->GetName());
	}
}


/**
 * EndPlay: 清空所有状态 — 防止跨关卡 GC 抖动
 *
 * 大厂原则 - 防御型设计:
 *   - Clear RagdollTimerHandle (防止 Actor 销毁后定时器回调)
 *   - 清空 OwnerCharacter 弱引用 (避免野指针)
 *   - 重置 bDeathSequenceStarted (理论上不必要, 但跨关卡时安全)
 */
void UCombatDeathComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 防御: 清空 Ragdoll Timer, 防止 Actor 销毁后回调崩溃
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(RagdollTimerHandle);
	}

	// 清空 Owner 弱引用
	OwnerCharacter.Reset();

	// 重置死亡标志 (跨关卡时安全)
	bDeathSequenceStarted = false;

	Super::EndPlay(EndPlayReason);
}


// ==========================================
// 3. TakeDamage — UE 标准伤害入口
// ==========================================

/**
 * TakeDamage — 完整死亡检测链路
 *
 * 【v31.6 重构后】纯数据 RPC 路径, 不再传 Actor*
 *   旧版: Multicast_NotifyKill(this, nullptr) → 客户端解 nullptr → 崩溃
 *   新版: 服务器本地读 KillerName/VictimName + KillMethod → 纯数据 RPC
 *
 * 流程 (严格按顺序):
 *   1. 已死/无权限 → return 0
 *   2. 友军伤害守卫 (FFactionTags::CanDamage)
 *   3. Super::TakeDamage (走 UE 引擎原生)
 *   4. HealthComponent->ApplyDamage (Layer 0 单一真理源)
 *   5. HealthRegenComponent->NotifyDamageTaken
 *   6. IsDead → PerformKillSettlement
 */
float UCombatDeathComponent::TakeDamage(float DamageAmount, FDamageEvent const& DamageEvent,
                                        AController* EventInstigator, AActor* DamageCauser)
{
	// 防御: Owner 无效 → return 0
	ABaseCharacter* Owner = OwnerCharacter.Get();
	if (!Owner)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[CombatDeathComponent] TakeDamage: Owner 无效, 无法处理伤害."));
		return 0.0f;
	}

	// 1. 已死/无权限 → return 0
	if (Owner->IsDead() || !Owner->HasAuthority())
	{
		return 0.0f;
	}

	// ============================================================
	// 2. 友军伤害守卫 (三层防御 Layer 3) — 大厂原则
	//    单一真理源: FFactionTags::CanDamage 集中校验
	// ============================================================
	ABaseCharacter* AttackerForFaction = nullptr;
	if (AController* InstigatorCtrl = EventInstigator)
	{
		AttackerForFaction = Cast<ABaseCharacter>(InstigatorCtrl->GetPawn());
	}
	if (!AttackerForFaction)
	{
		AttackerForFaction = Cast<ABaseCharacter>(DamageCauser);
	}

	if (AttackerForFaction && AttackerForFaction != Owner)
	{
		if (!FFactionTags::CanDamage(
				AttackerForFaction->GetFactionTag(),
				Owner->GetFactionTag(),
				TEXT("UCombatDeathComponent::TakeDamage"),
				AttackerForFaction->GetName(),
				Owner->GetName()))
		{
			// CanDamage 内部已 Log (Warning 同阵营 / Error 阵营无效)
			// 拦截: 拒绝扣血, 但不要掩盖伤害事件本身 (AllowDamage 仍生效)
			return 0.0f;
		}
	}
	// 注: 没找到攻击者 Character (AttackerForFaction==nullptr) → 视为环境伤害
	//     例: 摔落/陷阱/Debuff DOT/自伤

	// ============================================================
	// 3. 【2026.07.11 P0 大厂架构】无敌期早期观察点 (与 BaseWeapon 同构)
	//    单一真理源: HealthComponent->bIsInvincible (Layer 0 ApplyDamage 已拦截)
	//    这里仅 Verbose 日志, 不重复拦截 (大厂原则 - 零重复)
	// ============================================================
	// [v40 P0 修复] 必须用 ResolveHealthComponent() 而非裸字段 — BP archetype 会 nullify
	if (UHealthComponent* HC = Owner->ResolveHealthComponent())
	{
		if (HC->IsInvincible())
		{
			UE_LOG(LogTemp, Verbose,
				TEXT("[CombatDeathComponent] TakeDamage: Owner=%s 在无敌期, 已被 HealthComponent Layer 0 拦截, 剩余=%.2fs"),
				*Owner->GetName(), HC->GetInvincibilityRemainingSeconds());
		}
	}

	// 4. 执行父类的逻辑 (UE 标准伤害处理)
	float ActualDamage = Owner->Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);

	// 5. 安全扣血 (委托 HealthComponent, 单一真理源)
	// [v40 P0 修复] 必须用 ResolveHealthComponent() 而非裸字段
	UHealthComponent* HC = Owner->ResolveHealthComponent();
	if (!HC)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[CombatDeathComponent] TakeDamage: ResolveHealthComponent 失败. "
				 "【v40 修复】理论上不应该 — HealthComponent 是 UE 标准组件, BP 必定挂上. "
				 "若出现, 检查 BP_BaseCharacter / BP_GruntAI 是否异常."));
		return 0.0f;
	}
	const float ActualApplied = HC->ApplyDamage(ActualDamage);
	UE_LOG(LogTemp, Warning,
		TEXT("[CombatDeathComponent] 服务器扣血完成: Damage=%.1f, NewHealth=%.1f/%.1f, Dead=%d, Auth=%d"),
		ActualApplied, Owner->GetCurrentHealth(), Owner->GetMaxHealth(), Owner->IsDead(), Owner->HasAuthority());

	// 6. 通知回血组件: 被打断了, 必须重新等 RegenerationDelay 才能回血
	// [v40 P0 修复] 必须用 ResolveHealthRegenComponent() 而非裸字段 — BP archetype 会 nullify
	if (Owner->HasAuthority())
	{
		if (UHealthRegenComponent* HRC = Owner->ResolveHealthRegenComponent())
		{
			HRC->NotifyDamageTaken();
		}
	}

	// 7. 判定生死 → 击杀结算
	if (Owner->IsDead())
	{
		// 服务器处理击杀结算 (仅在这里, 不在 Die() 内, 避免事件驱动路径重复触发)
		if (Owner->HasAuthority())
		{
			PerformKillSettlement(ActualApplied, EventInstigator, DamageCauser);
		}

		// 注: Die() 由 HealthComponent::OnDeath 事件触发 (BaseCharacter 订阅)
		//     旧 TakeDamage → Die() 路径已废弃, 改走事件驱动
	}

	return ActualDamage;
}


// ==========================================
// 4. Die — 服务器权威死亡入口
// ==========================================

/**
 * Die — 服务器权威统一死亡入口
 *
 * 【大厂 P0 2026.07.10 v2 关键修复】
 *   历史 bug: 先销毁 Pawn, 后调用 RequestRespawn → Controller 失效 → AI 永远不复活
 *   新实现: 先派发复活 → 再销毁 Pawn
 *
 * 调用方: HealthComponent::OnDeath 事件 → BaseCharacter::OnHealthComponentDeath → 本方法
 */
void UCombatDeathComponent::Die()
{
	ABaseCharacter* Owner = OwnerCharacter.Get();
	if (!Owner)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[CombatDeathComponent] Die: Owner 无效."));
		return;
	}

	// 服务器权威校验
	if (!Owner->HasAuthority())
	{
		return;
	}

	UE_LOG(LogTemp, Warning,
		TEXT("[CombatDeathComponent] Die: Pawn=%s, HasAuthority=%d"),
		*Owner->GetName(), Owner->HasAuthority() ? 1 : 0);

	// 1. 武器清理: 必须在 Pawn 销毁前
	if (ABaseWeapon* CurrentWeapon = Owner->GetCurrentWeapon())
	{
		Owner->SetAttackerIsAI(false);
		CurrentWeapon->StopDamageTrace();
	}

	// 2. 【v43 P0 修复】先释放出生点，再派发复活请求
	//    根因: 旧版顺序是 RequestRespawn → ExecuteDeathLocal → ReleaseOccupiedSpawnPoint
	//           → RequestRespawn 尝试占用出生点时，上一个还没释放 → 全部占用 → AI 无法复活
	//    修复: 在派发复活请求前，先调用 ReleaseOccupiedSpawnPoint() 释放旧出生点
	if (Owner->HasAuthority())
	{
		ReleaseOccupiedSpawnPoint(); // ← v43 修复：必须先释放
	}

	// 3. 【P0 v2 关键修复】派发复活请求
	//    原因: Pawn 销毁后, Controller 的 Pawn 引用失效, GetController() 可能返回 nullptr
	if (AController* MyController = Owner->GetController())
	{
		if (ARoomGameMode* GM = Cast<ARoomGameMode>(Owner->GetWorld()->GetAuthGameMode()))
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[CombatDeathComponent] Die: 派发复活请求: Controller=%s (Pawn=%s 即将销毁)"),
				*MyController->GetName(), *Owner->GetName());
			GM->RequestRespawn(MyController, false);
		}
		else
		{
			UE_LOG(LogTemp, Error,
				TEXT("[CombatDeathComponent] Die: Cannot find ARoomGameMode, skipping respawn for Pawn=%s"),
				*Owner->GetName());
		}
	}
	else
	{
		UE_LOG(LogTemp, Error,
			TEXT("[CombatDeathComponent] Die: Controller 已为空, 无法派发复活请求! Pawn=%s"),
			*Owner->GetName());
	}

	// 3. [v40 P0 修复] 显式广播死亡视觉效果给客户端 (Multicast_Die RPC)
	//    根因: 旧版完全没调 Multicast_Die,客户端只能依赖 OnRep_bIsDead 触发 → 但 OnRep 在 Pawn Destroy 之后不会触发
	//         结果: 普通玩家客户端完全看不到血量变 0 / 死亡特效 / 头像消失
	//    修复: 在 Pawn Destroy 之前显式发 Multicast_Die RPC 通知所有客户端
	//    单一真理源: 服务器统一通过 RPC 广播,客户端无需依赖 OnRep 时序
	UE_LOG(LogTemp, Warning,
		TEXT("[CombatDeathComponent] Die: 显式广播 Multicast_Die RPC: Pawn=%s"),
		*Owner->GetName());
	Owner->Multicast_Die();

	// 4. ExecuteDeathLocal 处理: 武器掉落/溶解, 胶囊体透明, 死亡动画, 布娃娃
	//    服务器端也调用, 服务器即将销毁 Pawn, 所以实际上不会有视觉效果
	ExecuteDeathLocal();

	// 5. [v40 P0 修复] 延迟销毁 Pawn (SetLifeSpan 0.1f)
	//    根因: 旧版 Owner->Destroy() 同步销毁 → UE 在销毁帧关闭 NetChannel → Client 收不到
	//          CurrentHealth=0 和 bIsDead=true 的 OnRep (血量最后为 0 状态 + 死亡流程)
	//    修复: SetLifeSpan 让 Actor 延迟 0.1s 销毁,期间 NetUpdate 能把当前 HealthComponent 状态推送
	//          给所有 Client (血量 0 + bIsDead=true),Client 触发 OnRep_Both → 血量 0 显示 + 死亡流程
	//    大厂原则 - 集中调度: 死亡 Pawn 都走这里,生命周期由 CombatDeathComponent 集中管理
	UE_LOG(LogTemp, Warning,
		TEXT("[CombatDeathComponent] Die: 延迟销毁 Pawn: %s (SetLifeSpan 0.1f 让 Replication 推送)"),
		*Owner->GetName());
	Owner->SetLifeSpan(0.1f);
}


// ==========================================
// 5. ExecuteDeathLocal — 本地死亡流程
// ==========================================

/**
 * ExecuteDeathLocal — 本地执行死亡流程 (幂等)
 *
 * 死亡流程时序 (t=0 死亡瞬间):
 *   - t=0: 武器立即溶解 + 角色立即溶解 + 胶囊体透明 + 禁用移动 + 死亡动画
 *   - t=0.7*DeathMontageDuration: 启动 Ragdoll
 *   - t=DissolveDuration: 角色和武器溶解完毕
 *   - t=RespawnDelaySeconds: 复活定时器到期, 销毁旧角色
 */
void UCombatDeathComponent::ExecuteDeathLocal()
{
	ABaseCharacter* Owner = OwnerCharacter.Get();
	if (!Owner)
	{
		return;
	}

	// 幂等检查: 死亡序列已开始则跳过核心步骤
	if (bDeathSequenceStarted)
	{
		UE_LOG(LogTemp, Verbose,
			TEXT("[CombatDeathComponent] ExecuteDeathLocal: 幂等跳过: Pawn=%s"),
			*Owner->GetName());
		return;
	}
	bDeathSequenceStarted = true;

	UE_LOG(LogTemp, Warning,
		TEXT("[CombatDeathComponent] ExecuteDeathLocal: Pawn=%s HasAuth=%d Weapon=%s"),
		*Owner->GetName(), Owner->HasAuthority() ? 1 : 0,
		Owner->GetCurrentWeapon() ? *Owner->GetCurrentWeapon()->GetName() : TEXT("nullptr"));

	// 0. 【v43 修复 P0】出生点释放在 Die() 中已提前调用
	//    v43 之前: ExecuteDeathLocal 中释放，但 Die() 中 RequestRespawn 在 ExecuteDeathLocal 之前
	//              → RequestRespawn 时出生点仍被占用 → 全部占用 → AI 无法复活
	//    v43 修复: Die() 中先 ReleaseOccupiedSpawnPoint() → 再 RequestRespawn()
	//              → RequestRespawn 时上一个出生点已释放 → 正常分配
	//    ExecuteDeathLocal 中保留幂等调用（防止非 Die() 路径的死亡）
	if (Owner->HasAuthority())
	{
		ReleaseOccupiedSpawnPoint();
	}

	// 【v99.1 大厂架构 — 母体复活位置真理源】死亡时主动缓存最后 Transform
	//
	// 业务核心 (用户 2026.07.26):
	//   "任何情况人类变成母体, 都是原地变成的, 不是回到出生点."
	//   母体死后复活也必须原地复活 (零兜底 — 禁用 ZeroVector / GetSpawnLocation 等)
	//
	// 大厂原则 — 单一真理源:
	//   - 玩家路径: ARoomPlayerState::LastDeathTransform + bHasLastDeathTransform (Replicated)
	//   - AI 路径:   ABaseAIController::CachedDeathTransform + bHasCachedDeathTransform (运行时真理源)
	//   - 复活链 MutatePawnToMother 读此字段, 不用 ZeroVector
	//
	// 不破坏刀战模式:
	//   - 刀战模式走出生点路径(URoomSpawnSubsystem::HandlePlayerRequestSpawn / SpawnAIInternal),
	//     不读 PS->LastDeathTransform / AIC->CachedDeathTransform
	//   - 死亡时仍缓存(无人读, 仅是冗余数据), 不影响业务
	if (Owner->HasAuthority())
	{
		const FTransform DeathTransform = Owner->GetActorTransform();
		if (APlayerController* PC = Cast<APlayerController>(Owner->GetController()))
		{
			if (ARoomPlayerState* PS = PC->GetPlayerState<ARoomPlayerState>())
			{
				PS->LastDeathTransform = DeathTransform;
				PS->bHasLastDeathTransform = true;
			}
		}
		else if (ABaseAIController* BaseAIC = Cast<ABaseAIController>(Owner->GetController()))
		{
			BaseAIC->SetCachedDeathTransform(DeathTransform);
		}
	}

	// 【2026.07.13 v40.6 反扎堆账本】死亡时释放仇恨账本 (与 ReleaseOccupiedSpawnPoint 对等)
	//   理由: AI 死后账本 AIHuntingMap 残留, 其他 AI 反扎堆评分把"已死 AI 锁定的目标"也算进 LockedEnemies
	//         → 死目标永远 "被锁定" → 其他 AI 不选它 → 反扎堆账本腐化
	//   释放时机: 与出生点释放一致 — 幂等检查之前 (重复死亡安全)
	//   客户端不释放: 账本是服务器权威, 客户端不允许写
	if (Owner->HasAuthority())
	{
		ReleaseHuntingTarget();
	}

	// 0.5. 【2026.07.11 P0 大厂架构】死亡时立即取消无敌期
	//    原因: 边缘 case — 玩家/AI 在无敌期内被 ban/death → Timer 残留 → 新 Pawn 复活时被误认为无敌
	// [v40 P0 修复] 必须用 ResolveHealthComponent() 而非裸字段 — BP archetype 会 nullify
	if (UHealthComponent* HC = Owner->ResolveHealthComponent())
	{
		HC->DeactivateInvincibility();
	}

	// 1. 武器掉落 (复制本地指针以防 CurrentWeapon 被清空)
	ABaseWeapon* CurrentWeapon = Owner->GetCurrentWeapon();
	if (CurrentWeapon)
	{
		DropAndFadeWeapon(CurrentWeapon);
		// 通知 Owner 清空 CurrentWeapon (通过现有 Owner API 路径)
		// 注: BaseCharacter 的 CurrentWeapon 是 protected, 不能直接清
		//     走 Owner 内部清空接口 — 这里只调 DropAndFadeWeapon, 不清指针
		//     BaseCharacter::ExecuteDeathLocal 旧版有 CurrentWeapon=nullptr, 沿用相同模式
	}

	// 2. 胶囊体透明 + 禁用移动
	if (UCharacterMovementComponent* MoveComp = Owner->GetCharacterMovement())
	{
		MoveComp->StopMovementImmediately();
		MoveComp->DisableMovement();
	}
	if (UCapsuleComponent* CapsuleComp = Owner->GetCapsuleComponent())
	{
		CapsuleComp->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
		CapsuleComp->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);
	}

	// 3. 死亡动画 + 定时布娃娃
	float AnimDuration = 0.1f;
	if (DeathMontage)
	{
		AnimDuration = Owner->PlayAnimMontage(DeathMontage);
	}
	float TimeToRagdoll = AnimDuration > 0.1f ? (AnimDuration * 0.7f) : 0.1f;

	if (UWorld* World = Owner->GetWorld())
	{
		World->GetTimerManager().SetTimer(
			RagdollTimerHandle, this, &UCombatDeathComponent::EnableRagdoll, TimeToRagdoll, false);
	}

	// 4. 【2026-07-01 P0 修复】角色身体溶解 - 立即启动
	// [v40 P0 修复] 必须用 ResolveDissolveComponent() 而非裸字段 — BP archetype 会 nullify
	if (UDissolveComponent* DC = Owner->ResolveDissolveComponent())
	{
		DC->StartDissolveImmediate();
	}
	else
	{
		UE_LOG(LogTemp, Error,
			TEXT("[CombatDeathComponent] ExecuteDeathLocal: ResolveDissolveComponent 失败! 角色将无法溶解. "
				 "Pawn=%s"),
			*Owner->GetName());
	}
}


// ==========================================
// 6. EnableRagdoll — 布娃娃启用 (UE 标准 5 步)
// ==========================================

/**
 * EnableRagdoll — UE 标准 5 步布娃娃设置
 *
 * UE 官方标准流程:
 *   1. 打断所有动画
 *   2. 禁用 CharacterMovement
 *   3. 设置 Mesh 碰撞为 Ragdoll profile
 *   4. SetAllBodiesSimulatePhysics(true) + bBlendPhysics=true
 *   5. WakeAllRigidBodies()
 */
void UCombatDeathComponent::EnableRagdoll()
{
	ABaseCharacter* Owner = OwnerCharacter.Get();
	if (!Owner)
	{
		return;
	}

	USkeletalMeshComponent* MeshComp = Owner->GetMesh();
	if (!MeshComp)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[CombatDeathComponent] EnableRagdoll: Owner->GetMesh() 为空, 无法启用布娃娃. Pawn=%s"),
			*Owner->GetName());
		return;
	}

	// 1. 打断所有还在播的动画 (蒙太奇会与物理模拟冲突, 必须停掉)
	Owner->StopAnimMontage();

	// 2. 禁用 CharacterMovement (Movement 强制控制 Mesh 会覆盖物理模拟)
	if (UCharacterMovementComponent* MoveComp = Owner->GetCharacterMovement())
	{
		MoveComp->StopMovementImmediately();
		MoveComp->DisableMovement();
		MoveComp->SetComponentTickEnabled(false);
	}

	// 3. 碰撞 Profile 切换到 Ragdoll
	MeshComp->SetCollisionProfileName(TEXT("Ragdoll"));

	// 4. bBlendPhysics = true (UE Ragdoll 关键标志)
	MeshComp->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	MeshComp->SetAllBodiesSimulatePhysics(true);
	MeshComp->bBlendPhysics = true;

	// 5. 唤醒所有刚体
	MeshComp->WakeAllRigidBodies();

	// 6. Mesh 不再跟随 Capsule
	MeshComp->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);

	UE_LOG(LogTemp, Log,
		TEXT("[CombatDeathComponent] EnableRagdoll: 已启用布娃娃 (Mesh=%s, PhysicsAsset=%s)"),
		*MeshComp->GetName(),
		*GetNameSafe(MeshComp->GetPhysicsAsset()));
}


// ==========================================
// 7. Multicast_Die_Implementation — RPC 实现
// ==========================================

/**
 * Multicast_Die_Implementation
 *
 * 服务器流程: HealthComponent::ApplyDamage → OnDeath.Broadcast → OnHealthComponentDeath → Die() → Multicast_Die (服务器自己)
 * 客户端流程: HealthComponent::OnRep_bIsDead → OnDeath.Broadcast → OnHealthComponentDeath → ExecuteDeathLocal()
 *              + Multicast_Die RPC → ExecuteDeathLocal() (兜底)
 *
 * 注: 实际 UE 中, Multicast_Die 是 UFUNCTION(NetMulticast, Reliable), 由 UE 反射机制展开
 *     这里声明实现 _Implementation, 因为 UCombatDeathComponent 不是 Actor, 没有 UFUNCTION RPC 装饰
 *     实际 BaseCharacter::Multicast_Die 仍存在, 通过转发调用本方法
 *
 *     大厂原则 - 集中调度: Multicast_Die 的"如何响应"逻辑全部在本方法
 */
void UCombatDeathComponent::Multicast_Die_Implementation()
{
	ABaseCharacter* Owner = OwnerCharacter.Get();
	if (!Owner)
	{
		return;
	}

	// 服务器专属: 重置 AC/ACE
	if (Owner->HasAuthority())
	{
		Owner->ResetAC();
	}

	// 执行本地死亡流程 (幂等, CurrentWeapon 已被 nullptr 后再次进入会被 if 短路)
	ExecuteDeathLocal();

	// 复活定时器仅服务器需要
	if (Owner->HasAuthority())
	{
		if (AController* MyController = Owner->GetController())
		{
			if (ARoomPlayerController* PC = Cast<ARoomPlayerController>(MyController))
			{
				PC->StartRespawnTimer(RespawnDelaySeconds);
			}
		}
	}
}


// ==========================================
// 8. DropAndFadeWeapon — 武器死亡处理
// ==========================================

/**
 * DropAndFadeWeapon — 武器掉落 + 自治溶解
 *
 * 【大厂 P0 2026.07.10 重构 — 职责对等】武器自治 + 零跨边界
 *   身体溶解: ABaseCharacter::DissolveComponent 驱动身体 Mesh
 *   武器溶解: ABaseWeapon::WeaponDissolveComponent 驱动武器 Mesh
 *   零跨边界: 武器 Detach 后完全自治, 不依赖角色任何状态
 */
void UCombatDeathComponent::DropAndFadeWeapon(ABaseWeapon* Weapon)
{
	ABaseCharacter* Owner = OwnerCharacter.Get();
	if (!Owner || !IsValid(Weapon))
	{
		return;
	}

	UE_LOG(LogTemp, Log,
		TEXT("[CombatDeathComponent] DropAndFadeWeapon: %s (Auth=%d)"),
		*Weapon->GetName(), Owner->HasAuthority() ? 1 : 0);

	// 1. 取消武器与角色的骨骼绑定 (KeepWorldTransform: 武器保持在世界中当前位置)
	Weapon->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);

	// 2. 激活武器 Mesh 物理模拟 + 碰撞, 让它掉地上
	if (UMeshComponent* WeaponMesh = Weapon->FindComponentByClass<UMeshComponent>())
	{
		WeaponMesh->SetCollisionEnabled(ECollisionEnabled::PhysicsOnly);
		WeaponMesh->SetCollisionResponseToAllChannels(ECR_Block);
		WeaponMesh->SetSimulatePhysics(true);
		WeaponMesh->WakeAllRigidBodies();
		WeaponMesh->SetEnableGravity(true);
	}

	// 3. 武器溶解 — 委托给武器自己 (职责对等)
	//    协议: 武器材质蓝图必须调用 MF_Dissolve 节点, 否则 Log Warning 报警 (零兜底)
	Weapon->StartDissolve();

	// 4. 统一销毁授权 — 仅服务器调用 SetLifeSpan
	if (Owner->HasAuthority())
	{
		Weapon->SetLifeSpan(WeaponDestroyDelaySeconds);
	}
}


// ==========================================
// 9. ActivateSpawnInvincibility — 激活无敌期
// ==========================================

/**
 * ActivateSpawnInvincibility
 *
 * 大厂原则 - 零兜底:
 *   - DurationOverride < 0 → 用 Owner->DefaultSpawnInvincibilitySeconds
 *   - DurationOverride == 0 → 跳过激活
 *   - DurationOverride > 0 → 用该值
 *   - HealthComponent 不存在 → Log Error + return
 *
 * 调用方 (大厂原则 - 集中调度):
 *   - ARoomGameMode::RequestRespawn 末尾
 *   - ARoomGameMode::SpawnAIInternal 末尾
 *   - ARoomGameMode::HandlePlayerRequestSpawn 末尾
 */
void UCombatDeathComponent::ActivateSpawnInvincibility(float DurationOverride)
{
	ABaseCharacter* Owner = OwnerCharacter.Get();
	if (!Owner)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[CombatDeathComponent] ActivateSpawnInvincibility: Owner 无效."));
		return;
	}

	// [v40 P0 修复] 必须用 ResolveHealthComponent() 而非裸字段 — BP archetype 会 nullify
	UHealthComponent* HC = Owner->ResolveHealthComponent();
	if (!HC)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[CombatDeathComponent] ActivateSpawnInvincibility: ResolveHealthComponent 失败, 无法激活无敌. "
				 "Pawn=%s. 大厂原则: 不能兜底 — 必须保证 Pawn 上挂 HealthComponent."),
			*Owner->GetName());
		return;
	}

	float Duration = DurationOverride;
	// 负数 (非 -1) 表示"无覆盖", 用默认值
	if (DurationOverride < 0.0f)
	{
		Duration = DefaultSpawnInvincibilitySeconds;
	}

	UE_LOG(LogTemp, Warning,
		TEXT("[CombatDeathComponent] ActivateSpawnInvincibility: Pawn=%s Override=%.2f Resolved=%.2f Default=%.2f"),
		*Owner->GetName(), DurationOverride, Duration, DefaultSpawnInvincibilitySeconds);

	// 委托 HealthComponent (单一真理源)
	HC->ActivateInvincibility(Duration);
}


// ==========================================
// 10. DeactivateSpawnInvincibility — 取消无敌期
// ==========================================

/**
 * DeactivateSpawnInvincibility — 委托 HealthComponent
 *
 * 调用方:
 *   - ExecuteDeathLocal (死亡时强制取消 — 防止边缘 case 残留)
 *   - 调试/反作弊强制清零
 */
void UCombatDeathComponent::DeactivateSpawnInvincibility()
{
	ABaseCharacter* Owner = OwnerCharacter.Get();
	if (!Owner)
	{
		return;
	}

	// [v40 P0 修复] 必须用 ResolveHealthComponent() 而非裸字段
	UHealthComponent* HC = Owner->ResolveHealthComponent();
	if (!HC)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[CombatDeathComponent] DeactivateSpawnInvincibility: ResolveHealthComponent 失败. "
				 "【v40 修复】理论上不应该 — 若出现, 检查 BP 是否挂上 HealthComponent."));
		return;
	}
	HC->DeactivateInvincibility();
}


// ==========================================
// 10.5. ReleaseOccupiedSpawnPoint — 释放出生点 (v39 P0 修复)
// ==========================================

/**
 * ReleaseOccupiedSpawnPoint — 释放占用的出生点 (v39 P0 修复)
 *
 * 【根因 (2026.07.12 Session1.log)】
 *   旧版 URoomSpawnSubsystem::ReleaseSpawnPoint 整个项目 0 调用:
 *     - 玩家首次 Spawn → RoomSpawnSubsystem::GetAvailableSpawnPointForFaction
 *         → OccupiedSpawnPoints.Add(SpawnPoint)
 *     - 玩家死亡 → 没有 ReleaseSpawnPoint 调用 → 出生点永久占用
 *     - 玩家复活 → GetAvailableSpawnPointForFaction: 全部 5 个出生点都被占用 → 返回 nullptr
 *     - HandlePlayerRequestSpawn: 拒绝 Spawn → 玩家不复活
 *     - 反复 5 次后 → 5 个出生点全部永久占用 → 所有玩家不复活 (Bug 1)
 *
 * 【大厂原则 — 集中调度】
 *   - 所有死亡路径必走 UCombatDeathComponent::ExecuteDeathLocal
 *   - 故这里是唯一释放入口, 不依赖调用方记得释放
 *   - 与 ActivateSpawnInvincibility/DeactivateSpawnInvincibility 同样的"死亡时强制清理"模式
 *
 * 【零兜底】
 *   - 没有 SpawnSubsystem → Log Error + return (理论上不可能, 因为 RoomGameMode 必有 SpawnSys)
 *   - 没有 PlayerState → Log Warning + return (AI 也可能没 PS, 不能强制)
 *   - 没有占用 → 静默 (Map.Remove 重复安全)
 *
 * 【服务器专属】
 *   - HasAuthority() 守卫确保只在服务器执行 (UE 网络分层: 客户端不能写 SpawnSubsystem 状态)
 *   - 客户端调 ExecuteDeathLocal 也会进这里, 但 HasAuthority=false → 立即 return
 */
void UCombatDeathComponent::ReleaseOccupiedSpawnPoint()
{
	ABaseCharacter* Owner = OwnerCharacter.Get();
	if (!Owner)
	{
		return;
	}

	// 服务器专属: 客户端不释放 (UE 网络分层)
	if (!Owner->HasAuthority())
	{
		return;
	}

	UWorld* World = Owner->GetWorld();
	if (!World)
	{
		return;
	}

	// 1. 找到 SpawnSubsystem (RoomGameMode 必有)
	URoomSpawnSubsystem* SpawnSys = URoomSpawnSubsystem::Get(World->GetAuthGameMode());
	if (!SpawnSys)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[CombatDeathComponent] ReleaseOccupiedSpawnPoint: RoomSpawnSubsystem 未找到. "
				 "Pawn=%s. 【v39 零兜底】拒绝静默 return, 请检查 GameMode."),
			*Owner->GetName());
		return;
	}

	// 2. 找到 Controller (玩家 = PlayerController, AI = AIController)
	// 【v43 修复】AI 也占用出生点，必须释放！
	// 根因: 旧版注释说 "AI Spawn 走 SpawnAIInternal 不调 GetAvailableSpawnPointForFaction"
	//       但实际上 SpawnAIInternal line 558 明确调用了 GetAvailableSpawnPointForFaction！
	//       → AI 复活占用出生点 → 死亡时跳过了释放 → 5 次后全部占用 → AI 无法复活
	AController* Ctrl = Cast<AController>(Owner->GetController());
	if (!Ctrl)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[CombatDeathComponent] ReleaseOccupiedSpawnPoint: 无法获取 Controller. Pawn=%s."),
			*Owner->GetName());
		return;
	}

	// 3. 通过 Controller 反查释放 (v39 SSOT: OccupiedSpawnByController)
	//    支持玩家 (PlayerController) 和 AI (AIController)
	SpawnSys->ReleaseSpawnPointByController(Ctrl);

	UE_LOG(LogTemp, Log,
		TEXT("[CombatDeathComponent] ReleaseOccupiedSpawnPoint: 已通过 Controller '%s' 精准释放出生点. "
			 "【v43 修复】支持 AI 复活路径，根因：SpawnAIInternal 占用出生点时记录了 OccupancyOwner，"
			 "死亡时通过 Controller 反查释放。"),
		*Owner->GetName(),
		*Ctrl->GetName());
}


// ==========================================
// 10.6. ReleaseHuntingTarget — 释放仇恨账本 (v40.6 P0 反扎堆)
// ==========================================

/**
 * ReleaseHuntingTarget — 释放仇恨账本中的自己 (v40.6 P0 修复)
 *
 * 【根因 (2026.07.13 业务需求 — 反扎堆)】
 *   用户需求: "只要某敌人被自己一方的 AI 锁定, 其他 AI 就不能再锁定此敌人"
 *   旧版坑: AI 死后账本 AIHuntingMap 残留, 其他 AI 反扎堆评分时
 *           - 走循环 IsTargetLockedByOthers 检查时, 已死 AI 仍占账本
 *           - 死目标永远被判定为 "被锁定"
 *           - 其他 AI 反扎堆评分 0 分 → 不选它
 *           - 反扎堆账本腐化, 永远不能锁定死目标 (即使它已死)
 *
 * 【修复 (大厂原则 — 集中调度)】
 *   - 所有死亡路径必走 ExecuteDeathLocal → 故这里是唯一释放入口
 *   - 与 ReleaseOccupiedSpawnPoint 同样的 "死亡时强制清理" 模式
 *   - 客户端不释放: 账本是服务器权威, UE 网络分层
 *
 * 【服务器专属】
 *   - HasAuthority() 守卫确保只在服务器执行
 */
void UCombatDeathComponent::ReleaseHuntingTarget()
{
	ABaseCharacter* Owner = OwnerCharacter.Get();
	if (!Owner)
	{
		return;
	}

	// 服务器专属: 客户端不释放 (UE 网络分层 — 账本权威)
	if (!Owner->HasAuthority())
	{
		return;
	}

	UWorld* World = Owner->GetWorld();
	if (!World)
	{
		return;
	}

	// 1. 拿 TargetingSubsystem — 账本真理源
	URoomTargetingSubsystem* TargetSys = URoomTargetingSubsystem::Get(World);
	if (!TargetSys)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[CombatDeathComponent] ReleaseHuntingTarget: URoomTargetingSubsystem 未找到. "
				 "Pawn=%s. 【v40.6 零兜底】拒绝静默 return, 请检查 ShouldCreateSubsystem."),
			*Owner->GetName());
		return;
	}

	// 2. AI 路径专属: AI Pawn 必须能转 ABaseCharacter
	//    玩家路径: 玩家死亡也会走这里, 但玩家不会在 AIHuntingMap 账本里 (玩家不是 AI)
	//    → ReleaseTarget 内部判断: AIHuntingMap.Contains(RequestingAI) → 玩家不在账本 → 安全 no-op
	//    所以这里不需要区分玩家/AI, 直接调即可
	TargetSys->ReleaseTarget(Owner);

	UE_LOG(LogTemp, Log,
		TEXT("[CombatDeathComponent] ReleaseHuntingTarget: 已释放仇恨账本记录 (Pawn=%s). "
			 "【v40.6 P0】修复: 旧版无释放入口 → AIHuntingMap 残留 → 反扎堆账本腐化 → 死目标永远 '被锁定'."),
		*Owner->GetName());
}


// ==========================================
// 11. PerformKillSettlement — 击杀结算 (服务器集中)
// ==========================================

/**
 * PerformKillSettlement — 击杀结算 (v31.6 重构 — 纯数据 RPC)
 *
 * 【v31.6 大厂重构】从 AActor* 引用改为纯数据
 *
 * 历史痛点 (v22-v31.5):
 *   - 旧 Multicast_NotifyKill(AActor* Victim, AActor* Assistant) 跨 RPC 边界
 *   - 服务器 TakeDamage 同步链: HealthComponent.OnDeath → Die() → Destroy()
 *   - 但 Multicast 在 Destroy 之前已发出, RPC 携带的 AActor* NetGUID
 *   - 客户端解析时, Victim 可能已 nullptr → 解引用崩溃 (Session3.log)
 *
 * 新架构 (v31.6):
 *   - 服务器在 TakeDamage 同步链路里把 Victim/Killer 姓名 + KillMethod 全部本地读出
 *   - 拼成纯数据 RPC: Multicast_NotifyKill(KillerName, VictimName, KillMethod, bIsAssist)
 *   - 客户端 Implementation 不再解 Actor*, 不可能再崩
 *
 * 单一职责 (大厂原则):
 *   1. 提取 KillMethod (Weapon->GetLastKillMethod)
 *   2. 读取 Killer/Victim 姓名
 *   3. AddKillScore (Killer.PlayerState)
 *   4. AddDeath (Victim.PlayerState)
 *   5. GrantAssistsToEligiblePlayers (静态函数, 查找助攻者)
 *   6. Multicast_NotifyKill (纯数据 RPC)
 */
void UCombatDeathComponent::PerformKillSettlement(float DamageAmount, AController* EventInstigator, AActor* DamageCauser)
{
	// [v40 P0 修复] 防御: Owner 可能已失效 (理论极致 case)
	//   正常路径: TakeDamage 末尾 → ApplyDamage → OnDeath.Broadcast → Die() 同步链 → SetLifeSpan(0.1)
	//     → 回到 TakeDamage → PerformKillSettlement. 此期间 Pawn 仍在,Owner 应有效.
	//   但如果 Die() 路径未来改成立即 Destroy (不要!),或 Owner 被外部销毁,
	//     防御层会在这里 Log 错误并 return (零兜底: 不抛异常,不静默)
	ABaseCharacter* Owner = OwnerCharacter.Get();
	if (!Owner)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[CombatDeathComponent] PerformKillSettlement: Owner 无效 (Pawn 可能已被外部销毁). "
				 "【v40 修复】正常 SetLifeSpan(0.1f) 路径下不应出现此错误. "
				 "若出现, 检查 Die() 路径是否被外部代码绕过."));
		return;
	}

	// 1. 击杀方式 (真理源: Weapon->GetLastKillMethod())
	EKillMethod KillMethod = EKillMethod::MeleeWeapon; // 默认近战
	if (ABaseWeapon* Weapon = Cast<ABaseWeapon>(DamageCauser))
	{
		KillMethod = Weapon->GetLastKillMethod();
	}

	// 2. 击杀者姓名 (服务器本地读 PlayerState)
	FString KillerName = TEXT("Unknown");
	AController* DamageInstigatorController = EventInstigator;
	ABaseCharacter* KillerCharacter = DamageInstigatorController
		? Cast<ABaseCharacter>(DamageInstigatorController->GetPawn()) : nullptr;
	if (KillerCharacter)
	{
		if (ARoomPlayerState* KillerPS = KillerCharacter->GetRoomPlayerState())
		{
			KillerName = KillerPS->GetPlayerName();
		}
		else
		{
			// 大厂零兜底: Killer 有 Character 但没 PlayerState = 配置错
			UE_LOG(LogTemp, Error,
				TEXT("[CombatDeathComponent] PerformKillSettlement: Killer '%s' 没有 ARoomPlayerState, "
					 "击杀消息姓名显示为 Unknown. 根因: 玩家 Spawn 链路没生成 PS, 检查 Spawn/Respawn 链路."),
				*KillerCharacter->GetName());
		}

		// 【v40.9 P0】在 GrantAssists 调用之前先计算 bIsKillerPlayer
		// 正确判断: KillerCharacter->IsPlayerControlled()
		// - 玩家 Pawn (被 APlayerController 控制) → true ✅
		// - AI Pawn (被 AAIController 控制) → false ✅
		const bool bIsKillerPlayer = (KillerCharacter && KillerCharacter->IsPlayerControlled());

		// 查找助攻 (服务器内部纯函数, 不走 RPC)
		// 【v40.9】传入 bIsKillerPlayer — 只有玩家击杀才显示助攻者的 KillFeed 图标
		ABaseCharacter::GrantAssistsToEligiblePlayers(Owner, KillerCharacter, KillMethod, bIsKillerPlayer);
	}
	else
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[CombatDeathComponent] PerformKillSettlement: 死亡时找不到 Killer Pawn (Instigator=%s, "
				 "DamageCauser=%s). 多半是 Instigator 已被 Destroy."),
			DamageInstigatorController ? *DamageInstigatorController->GetName() : TEXT("nullptr"),
			DamageCauser ? *DamageCauser->GetName() : TEXT("nullptr"));
	}

	// 3. 被击杀者姓名 (自己)
	FString VictimName = TEXT("Unknown");
	ARoomPlayerState* VictimPS = Owner->GetRoomPlayerState();
	if (VictimPS)
	{
		VictimName = VictimPS->GetPlayerName();
		// 【v31.6 大厂重构】服务器侧死亡计数
		// 【v100】VictimPS->AddDeath() 已下移到下面"清零连杀"块中,避免重复调用
	}
	else
	{
		UE_LOG(LogTemp, Error,
			TEXT("[CombatDeathComponent] PerformKillSettlement: Victim '%s' 没有 ARoomPlayerState, "
				 "死亡计数无法累加. 根因: 玩家 Spawn 链路没生成 PS."),
			*Owner->GetName());
	}

	// 【v31.6 大厂重构】服务器侧击杀计数
	if (KillerCharacter)
	{
		if (ARoomPlayerState* KillerPS = KillerCharacter->GetRoomPlayerState())
		{
			KillerPS->AddKillScore();
		}
	}

	// ==========================================
	// 【v100 大厂架构 — 连杀真理源累加 + 重置 Victim 连杀】
	//   时序关键: AddKillScore 之后立即调 — 因为 ServerUpdateKillStreak 返回 EKillStreakType
	//   而 EKillStreakType 要传给 Multicast_NotifyKill RPC 用, 必须在 RPC 调用前完成
	// ==========================================
	EKillStreakType CalculatedStreakType = EKillStreakType::None;
	USoundBase* KillSoundAsset = nullptr; // 【v105 新增】服务器查表后 RPC 传给客户端
	if (KillerCharacter)
	{
		if (ARoomPlayerState* KillerPS = KillerCharacter->GetRoomPlayerState())
		{
			const bool bIsHeadshot = (KillMethod == EKillMethod::PrimaryHeadshot ||
				KillMethod == EKillMethod::SecondaryHeadshot ||
				KillMethod == EKillMethod::MeleeHeadshot);
			CalculatedStreakType = KillerPS->ServerUpdateKillStreak(false, bIsHeadshot);

			// 【v105 大厂架构】服务器查表获取击杀音效资产
			// 根因: PlayKillSound 在客户端调用 EnsureKillStreakDataTable → World->GetAuthGameMode() 返回 nullptr
			// 修复: 服务器在调用前查表, 把 USoundBase* 作为参数传给客户端
			if (CalculatedStreakType != EKillStreakType::None)
			{
				UWorld* World = GetWorld();
				if (ARoomGameMode* GM = World ? World->GetAuthGameMode<ARoomGameMode>() : nullptr)
				{
					if (UDataTable* KillStreakTable = GM->KillStreakIconDataTable)
					{
						// 查找对应 StreakType 的行
						const FString EnumValueName = UEnum::GetValueAsString(CalculatedStreakType);
						FString RowNameStr = EnumValueName;
						int32 ColonPos = INDEX_NONE;
						if (RowNameStr.FindLastChar(TEXT(':'), ColonPos))
						{
							RowNameStr = RowNameStr.RightChop(ColonPos + 1);
						}
						const FName RowName(*RowNameStr);
						static const FString ContextStr(TEXT("KillSoundLookup"));
						if (FKillStreakIconInfo* Row = KillStreakTable->FindRow<FKillStreakIconInfo>(RowName, ContextStr, false))
						{
							KillSoundAsset = Row->KillSound;
						}
						else
						{
							UE_LOG(LogTemp, Warning,
								TEXT("[CombatDeathComponent][v105] PerformKillSettlement: KillStreakIconDataTable 找不到 Row '%s' (StreakType=%d). 音效不会播放."),
								*RowNameStr, static_cast<int32>(CalculatedStreakType));
						}
					}
					else
					{
						UE_LOG(LogTemp, Warning,
							TEXT("[CombatDeathComponent][v105] PerformKillSettlement: GM->KillStreakIconDataTable 为空. 音效不会播放."));
					}
				}
				else
				{
					UE_LOG(LogTemp, Warning,
						TEXT("[CombatDeathComponent][v105] PerformKillSettlement: AuthGameMode 不是 ARoomGameMode. 音效不会播放."));
				}
			}
		}
		else
		{
			UE_LOG(LogTemp, Error,
				TEXT("[CombatDeathComponent][v100] PerformKillSettlement: Killer '%s' 没有 ARoomPlayerState, "
					 "无法算 EKillStreakType. RPC 推送 None (音效会被 KillSoundComponent 拒绝)."),
				*KillerCharacter->GetName());
		}
	}

	// Victim 死亡 — 同步清零连杀 (避免下一回合继承)
	// 【v100】复用函数级已有的 VictimPS (上面第 1132 行已获取),不重复声明避免 C4456
	// 注: 上面 if/else 已经对 VictimPS==nullptr 做了 Log Error, 这里跳过二次
	if (VictimPS)
	{
		VictimPS->AddDeath();
		VictimPS->ServerResetKillStreak();
	}

	// 4. 广播击杀消息 (纯数据 RPC, 不会再 nullptr 崩溃)
	// 【v40.9 P0 大厂架构】传入 bIsKillerPlayer — 只有玩家击杀才显示 KillFeed 图标
	//
	// 正确判断: KillerCharacter->IsPlayerControlled()
	// - 玩家 Pawn (被 APlayerController 控制) → true ✅
	// - AI Pawn (被 AAIController 控制) → false ✅
	// 注意: KillerCharacter != nullptr 时 KillerCharacter 必定是 ABaseCharacter*
	//        AI 击杀玩家时 KillerCharacter 是 AI Pawn (不为 nullptr), 但 IsPlayerControlled() = false
	//
	// 【v100.1 大厂原则 — 零兜底】KillerCharacter == nullptr 路径:
	//   - 旧版 (兜底): 用 OneKill 假音效,注释自述"不让击杀完全无声"
	//   - 问题: 没有真实 Killer 时播"一杀"音效是伪造游戏事件,违反可观测性
	//   - 新版: 不广播 — 没有 Killer 就没有击杀事件,这是真实业务语义
	//   - 如果 Killer 真的丢了 (Destroy 时序竞态), 由 UE 网络机制自动不送达,不算 bug
	const bool bIsKillerPlayer = (KillerCharacter && KillerCharacter->IsPlayerControlled());
	if (KillerCharacter)
	{
		// 【v100】新增 StreakType 参数 — 客户端按此播对应连杀音效
		// 【v105】新增 KillSoundAsset 参数 — 服务器查表后 RPC 推送音效资产
		KillerCharacter->Multicast_NotifyKill(KillerName, VictimName, KillMethod, /*bIsAssist=*/false, bIsKillerPlayer, CalculatedStreakType, KillSoundAsset);
	}
	// else: 零兜底 — 不广播击杀消息(没有 Killer 就没有击杀事件)
}