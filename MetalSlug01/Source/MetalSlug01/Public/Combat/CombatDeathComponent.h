// ==========================================
// 版权声明：在项目设置的描述页面填写您的版权信息。
// ==========================================
//
// @file CombatDeathComponent.h
// @brief 战斗死亡/无敌期 子系统 — 从 ABaseCharacter 抽离
//
// 【架构定位 — 2026.07.12 P0 大厂重构】
//   BaseCharacter.cpp 已 3957 行, 单文件维护成本爆炸.
//   本文件把 BaseCharacter 中"伤害+死亡+无敌期"相关逻辑抽出到独立 ActorComponent.
//
// 【职责 (What)】
//   1. TakeDamage 入口 (UE 标准扣血接口)
//   2. Die() 服务器权威死亡
//   3. ExecuteDeathLocal 本地死亡流程 (武器掉落/布娃娃/溶解/胶囊体)
//   4. EnableRagdoll 布娃娃启用 (UE 标准 5 步)
//   5. Multicast_Die RPC 实现
//   6. DropAndFadeWeapon 武器死亡处理 (含溶解委派)
//   7. ActivateSpawnInvincibility / DeactivateSpawnInvincibility 无敌期入口
//   8. PerformKillSettlement 击杀结算 (服务器集中, v31.6 重构)
//
// 【不负责 (What NOT)】
//   - 血量数据本身 (在 UHealthComponent)
//   - 武器生成/挂载 (在 WeaponAttachmentComponent)
//   - 输入/移动 (在 BaseCharacter 本身)
//   - AI BT 决策 (在 Behavior Tree)
//
// 【大厂原则 - 单一真理源】
//   - 死亡字段 bDeathSequenceStarted 在本组件, 不再在 BaseCharacter
//   - 无敌期数据在 UHealthComponent, 本组件只编排 (委托 Activate/Deactivate)
//   - LastKillMethod 数据在 ABaseWeapon, 本组件读取但不持有
//
// 【大厂原则 - 零兜底】
//   - 所有 Log Error 都明确指出根因 + 修复路径
//   - 入参校验: nullptr → Log Error + early return
//   - 配置缺失 → Log Error (而非 Warning)
//
// 【调用链路 (大厂原则 - 集中调度)】
//   TakeDamage (UE 入口) → PerformKillSettlement (结算)
//                              ↓ IsDead 分支
//                           OnHealthComponentDeath 事件
//                              ↓
//                           Die() (服务器) → Multicast_Die → 客户端也走 ExecuteDeathLocal
//
// 【迁移路径】
//   1. 本次只创建 4 个文件, 不动 BaseCharacter.h / .cpp
//   2. 后续 batch: BaseCharacter 字段/方法迁移到本组件, BaseCharacter 调用委托
//   3. 字段迁移完后再删除 BaseCharacter 中重复字段
//
// 【UE 5.6 注意事项】
//   .generated.h 必须用**裸文件名** (UE 5.6 UHT 字面 OrdinalIgnoreCase 比对, 不处理路径前缀)
// ==========================================
#pragma once

// ==========================================
// 头文件包含区
// ==========================================
#include "CoreMinimal.h"                          // UE 引擎核心最小化
#include "Components/ActorComponent.h"            // UActorComponent 基类
#include "Data/Enums/CombatEnums.h"              // EKillMethod 击杀方式枚举

// 转发: UE 反射系统需要 EKillMethod 类型在编译期可见

// UE 自动生成的头文件 (必须最后, 且用裸文件名 — UE 5.6 严格模式)
#include "CombatDeathComponent.generated.h"

// 前置声明 (加快编译)
class ABaseCharacter;                            // 拥有者 (Owner)
class ABaseWeapon;                                // 武器
class AController;                                // UE 原生控制器
class UHealthComponent;                          // 血量组件 (数据权威)
class UCharacterMovementComponent;                // 移动组件
class UDamageType;                                // UE 伤害类型
class USkeletalMeshComponent;                    // 骨骼网格
class UAnimMontage;                               // 动画蒙太奇
class ARoomPlayerState;                          // 玩家状态


/**
 * @class UCombatDeathComponent
 * @brief 战斗死亡/无敌期子系统 — BaseCharacter 的"战斗编排层"
 *
 * 设计模式: ActorComponent 自治子系统
 *   - Owner 暴露: 通过 Owner->HealthComponent / Owner->GetMesh() 等访问
 *   - 不跨边界: 武器溶解由武器自己的 WeaponDissolveComponent 处理
 *   - 职责对等: 身体溶解由 DissolveComponent 处理, 武器溶解由 WeaponDissolveComponent 处理
 *
 * 使用方法:
 *   1. BaseCharacter 构造函数中 CreateDefaultSubobject<UCombatDeathComponent>
 *   2. TakeDamage 重写转发: return DeathComponent->TakeDamage(...)
 *   3. Die() 重写转发: DeathComponent->Die()
 *   4. ActivateSpawnInvincibility 转发: DeathComponent->ActivateSpawnInvincibility(...)
 *
 * 后续迁移:
 *   - BaseCharacter::TakeDamage → 改为转发到本组件
 *   - BaseCharacter::Die → 改为转发到本组件
 *   - BaseCharacter::Multicast_Die → 改为转发到本组件
 *   - BaseCharacter::ExecuteDeathLocal → 改为转发到本组件
 */
UCLASS(ClassGroup = (MetalSlug), meta = (BlueprintSpawnableComponent))
class METALSLUG01_API UCombatDeathComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	// ==========================================
	// 构造函数
	// ==========================================

	/**
	 * 构造函数: 设置默认值
	 * 目的: 不创建子组件 (本组件没有子组件), 仅初始化字段默认值
	 */
	UCombatDeathComponent();

	// 【2026.07.12 P0 重构】Friend 双向授权:
	//   - ABaseCharacter 调本组件 protected 方法 (ExecuteDeathLocal / TakeDamage 等)
	friend class ABaseCharacter;

	// ==========================================
	// UE 原生伤害入口 — 转发自 ABaseCharacter::TakeDamage
	// ==========================================

	/**
	 * UE 原生伤害入口 — 完整死亡检测链路
	 *
	 * 【2026.07.12 P0 重构】从 BaseCharacter 抽离:
	 *   - BaseCharacter::TakeDamage 改为转发本方法
	 *   - 内部完整保留 v31.6 重构后的纯数据 RPC 路径
	 *
	 * 流程:
	 *   1. 已死/无权限 → return 0
	 *   2. 友军伤害守卫 (FFactionTags::CanDamage 三层防御之一)
	 *   3. Super::TakeDamage (走 UE 引擎原生伤害处理)
	 *   4. HealthComponent->ApplyDamage (Layer 0 单一真理源)
	 *   5. HealthRegenComponent->NotifyDamageTaken (重置回血计时)
	 *   6. IsDead → PerformKillSettlement (击杀结算集中)
	 *
	 * 单一真理源 (大厂原则):
	 *   - bIsDead 在 UHealthComponent
	 *   - LastKillMethod 在 ABaseWeapon
	 *   - KillMethod 提取: DamageCauser->IsA<ABaseWeapon>() → Weapon->GetLastKillMethod()
	 *
	 * @param DamageAmount    UE 标准伤害值
	 * @param DamageEvent     UE 标准伤害事件 (含 DamageType/HitInfo)
	 * @param EventInstigator 攻击者 Controller (Player/AI/其他)
	 * @param DamageCauser    伤害源 Actor (Weapon/Character/陷阱)
	 * @return 实际应用的伤害值
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat|Death")
	float TakeDamage(float DamageAmount, FDamageEvent const& DamageEvent,
	                 AController* EventInstigator, AActor* DamageCauser);

	// ==========================================
	// 死亡入口 (服务器权威)
	// ==========================================

	/**
	 * 服务器权威死亡入口 — 统一死亡流程
	 *
	 * 调用方:
	 *   - HealthComponent::OnDeath 事件 → BaseCharacter 转发 → 本方法
	 *   - 直接调用: 测试/调试/GM 强制
	 *
	 * 时序 (大厂原则 - 先派发再销毁):
	 *   1. 武器清理: SetAttackerIsAI(false) + StopWeaponTrace
	 *   2. 先派发复活: GM->RequestRespawn (Pawn 销毁前调, 避免 Controller 失效)
	 *   3. ExecuteDeathLocal (本地死亡视觉)
	 *   4. Destroy (Pawn 销毁)
	 *
	 * 注: 复活逻辑完全委托给 ARoomGameMode::RequestRespawn
	 */
	void Die();

	// ==========================================
	// 本地死亡流程 (服务器/客户端共用)
	// ==========================================

	/**
	 * 本地执行死亡流程 (客户端用, 服务器也可用)
	 *
	 * 与 Multicast_Die_Implementation 的区别:
	 *   - 不调 StartRespawnTimer (复活只在服务器调)
	 *   - 不调 ResetAC (仅服务器重置)
	 *   - 其他 (武器掉落/溶解/胶囊体/动画/布娃娃) 完全一致
	 *
	 * 幂等保证 (大厂原则):
	 *   - bDeathSequenceStarted 标志保证核心步骤只首次执行
	 *   - 服务器 Die() + Multicast_Die + 客户端 OnRep_bIsDead 可能重复触发
	 *   - 重复调用仅 ResetAC + StartRespawnTimer
	 *
	 * 时序 (t=0 死亡瞬间):
	 *   - t=0: 武器立即溶解 + 角色立即溶解 + 胶囊体透明 + 禁用移动 + 死亡动画
	 *   - t=0.7*DeathMontageDuration: 启动 Ragdoll
	 *   - t=DissolveDuration: 角色和武器溶解完毕
	 *   - t=RespawnDelaySeconds: 复活定时器到期, 销毁旧角色
	 *
	 * 必须满足时序约束:
	 *   RespawnDelaySeconds > DissolveDuration + DeathMontageDuration
	 *   否则身体溶解到一半就被销毁, 视觉效果断裂
	 */
	void ExecuteDeathLocal();

	/**
	 * 启用布娃娃物理 — UE 标准 5 步
	 *
	 * 之前的 bug: 仅有 StopAnimMontage + SetCollisionProfileName + SetSimulatePhysics,
	 *   缺了 3 个关键步骤, 物理模拟被 CharacterMovement 强制覆盖, Mesh 不会真的倒下.
	 *
	 * UE 官方标准流程:
	 *   1. 打断所有动画 (避免动画蒙太奇继续驱动骨骼, 与物理冲突)
	 *   2. 禁用 CharacterMovement (MOVE_None, 让 Movement Component 不再强制控制 Mesh)
	 *   3. 设置 Mesh 碰撞为 Ragdoll profile (项目设置里要预定义)
	 *   4. SetAllBodiesSimulatePhysics(true) + bBlendPhysics=true
	 *      (关键: 让 Mesh 的 root 与胶囊体解耦)
	 *   5. WakeAllRigidBodies() (强制所有物理骨骼激活, 否则可能因 sleep 而不响应)
	 *
	 * @note 物理资产 PhysicsAsset 必须在 BP/资产侧配置正确, 否则骨骼没有碰撞体
	 * @note 调用前必须确保胶囊体碰撞已设为 Ignore Pawn (ExecuteDeathLocal 已做)
	 */
	void EnableRagdoll();

	/**
	 * DiagnoseMeshRenderingSetup — Mesh 渲染配置诊断 (v60.9)
	 *
	 * 大厂原则 — 错误尽早暴露: BeginPlay 时立即检查角色 Mesh 配置
	 *   - 缺 Skeletal Mesh 资产 → 角色看不见
	 *   - 缺 AnimClass → T-pose (用户最常见问题)
	 *   - 缺 PhysicsAsset → 死亡 Ragdoll 不工作
	 *
	 * 每项缺失都会 Log Error 列出精确修复路径 (UE 编辑器面板名 + 资产名),
	 * 让玩家一看日志就知道怎么修 BP, 不再瞎猜.
	 *
	 * 调用方: BeginPlay() 末尾 (OwnerCharacter 缓存后)
	 */
	void DiagnoseMeshRenderingSetup() const;

	// ==========================================
	// RPC (NetMulticast Reliable)
	// ==========================================

	/**
	 * 网络多播死亡 — 让所有玩家都看到布娃娃效果
	 *
	 * 实现见 cpp — 因为 UFUNCTION 不能在 cpp 中拆分声明
	 * 本方法声明保持简单, RPC 修饰符在 cpp 中标识
	 *
	 * 调用方:
	 *   - 服务器 Die() → Multicast_Die() (服务器自己也会收到, 用于服务器本地兜底)
	 *   - 客户端不主动调用, 仅服务器广播
	 */
	void Multicast_Die_Implementation();

	// ==========================================
	// 武器死亡处理
	// ==========================================

	/**
	 * 武器死亡处理 (统一入口)
	 *
	 * 【大厂 P0 2026.07.10 重构 — 职责对等】武器自治 + 零跨边界
	 *
	 * 调用方: ExecuteDeathLocal 内部 (武器掉落前)
	 *
	 * 时序 (大厂规范):
	 *   1. 武器必须已 Detach (Mesh 不再跟随角色骨骼)
	 *   2. 武器必须已启用物理模拟 (SetSimulatePhysics(true))
	 *   3. 调 Weapon->StartDissolve() → 武器自治溶解
	 *   4. 仅服务器调用 Weapon->SetLifeSpan(3.0) → UE 引擎自动 Destroy
	 *
	 * 协议 (与身体一致 — 零兜底):
	 *   - 武器材质蓝图必须调用 MF_Dissolve 节点 (有 DissolveAmount 参数)
	 *   - 协议不满足时, WeaponDissolveComponent 内部 Log(Warning) 报警
	 *
	 * @param Weapon 要处理的武器 (调用前会拷贝, 内部置空由调用方处理)
	 */
	void DropAndFadeWeapon(ABaseWeapon* Weapon);

	// ==========================================
	// 复活无敌期 — 委托 HealthComponent (单一真理源)
	// ==========================================

	/**
	 * 激活复活无敌期
	 *
	 * 【2026.07.12 P0 重构】从 BaseCharacter 抽离, 内部委托 HealthComponent
	 *
	 * @param DurationOverride
	 *   - < 0 (默认 -1.0): 用 Owner->DefaultSpawnInvincibilitySeconds
	 *   - > 0: 强制用这个值 (例如 AI Profile 配的业务值)
	 *   - == 0: 跳过激活 (调用方明确说要禁用)
	 *
	 * 大厂原则 - 零兜底:
	 *   - 无 HealthComponent → Log Error + 不激活
	 *   - 委托 HealthComponent (单一真理源)
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat|Respawn")
	void ActivateSpawnInvincibility(float DurationOverride = -1.0f);

	/**
	 * 取消复活无敌期
	 *
	 * 调用方:
	 *   - ExecuteDeathLocal (死亡时强制取消 — 防止边缘 case 残留)
	 *   - 调试/反作弊强制清零
	 *
	 * 注: HealthComponent 内部已做幂等保护, 这里安全调用
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat|Respawn")
	void DeactivateSpawnInvincibility();

	/**
	 * 【v39 修复 P0】释放占用的出生点
	 *
	 * 调用方:
	 *   - ExecuteDeathLocal (死亡时强制释放 — 防止出生点泄漏)
	 *
	 * 大厂原则:
	 *   - 集中调度: 死亡链路唯一释放入口, 不依赖调用方记得释放
	 *   - 零兜底: 没有占用 → Log Verbose, 不视为错误 (Map.Remove 重复安全)
	 *   - 服务器专属: 客户端没有 SpawnSubsystem 写权限, HasAuthority() 守卫已确保只在服务器执行
	 *
	 * 根因 (2026.07.12 Session1.log):
	 *   - 旧版 ReleaseSpawnPoint 整个项目 0 调用
	 *   - 玩家首次 Spawn → OccupiedSpawnPoints.Add (5 个出生点都被占 1 个)
	 *   - 玩家死 → 没有 ReleaseSpawnPoint → 出生点永久占用
	 *   - 第二次复活 → GetAvailableSpawnPointForFaction: 全部 5 个出生点都被占用 → 返回 nullptr
	 *   - HandlePlayerRequestSpawn: 拒绝 Spawn → 玩家不复活
	 *   - 反复 5 次后 → 5 个出生点全部永久占用 → 所有玩家不复活
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat|Respawn")
	void ReleaseOccupiedSpawnPoint();

	/**
	 * ReleaseHuntingTarget — 释放仇恨账本 (v40.6 P0 反扎堆)
	 *
	 * 【大厂原则 - 集中调度】死亡时强制清理, 与 ReleaseOccupiedSpawnPoint 对等模式
	 *
	 * 根因: AI 死后账本 AIHuntingMap 残留 → 其他 AI 反扎堆评分把 "已死 AI 锁定的目标" 算进 LockedEnemies
	 *       → 死目标永远 "被锁定" → 其他 AI 不选它 → 反扎堆账本腐化
	 * 修复: 在 ExecuteDeathLocal 中调 ReleaseTarget (集中调度, 唯一释放入口)
	 *
	 * 服务器专属: HasAuthority() 守卫 — 客户端不允许写账本 (UE 网络分层)
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat|Death")
	void ReleaseHuntingTarget();

protected:
	// ==========================================
	// 内部辅助方法 (服务器集中结算)
	// ==========================================

	/**
	 * 击杀结算 (服务器集中, v31.6 重构)
	 *
	 * 职责:
	 *   1. 提取 KillMethod (Weapon->GetLastKillMethod)
	 *   2. 读取 Killer/Victim 姓名 (本地读 PlayerState)
	 *   3. AddKillScore + AddDeath (服务器侧, 不再藏在 RPC Implementation)
	 *   4. GrantAssistsToEligiblePlayers (查找助攻者)
	 *   5. Multicast_NotifyKill (纯数据 RPC)
	 *
	 * 大厂原则 (v31.6):
	 *   - RPC 纯数据化: FString + EKillMethod, 不传 Actor*
	 *   - 单一真理源: Weapon.LastKillMethod 决定 KillMethod
	 *   - 集中调度: 整个击杀结算在一个函数里, 不散落在 RPC Implementation
	 *
	 * @param DamageAmount    UE 标准伤害值 (未使用, 留作扩展)
	 * @param EventInstigator 攻击者 Controller
	 * @param DamageCauser    伤害源 Actor (Weapon)
	 */
	void PerformKillSettlement(float DamageAmount, AController* EventInstigator, AActor* DamageCauser);

	/**
	 * Owner Character 缓存 (避免每次访问 Owner 时 Cast)
	 *
	 * 大厂原则 - 缓存友好:
	 *   - BeginPlay 缓存一次, 后续访问 O(1)
	 *   - EndPlay 清空 (Owner 销毁时)
	 *
	 * 不需要 UPROPERTY 修饰 — Owner 通过 GetOwner() 保证生命周期
	 */
	UPROPERTY(Transient)
	TWeakObjectPtr<ABaseCharacter> OwnerCharacter;

	// ==========================================
	// 字段 (全部 private - 单一真理源)
	// ==========================================
private:
	/**
	 * 死亡序列幂等标志 — 单一真理源
	 *
	 * 服务器和客户端可能通过多个路径触发死亡流程:
	 *   - 服务器: HealthComponent::ApplyDamage → OnDeath.Broadcast → OnHealthComponentDeath → Die() → Multicast_Die (服务器自己)
	 *   - 客户端: HealthComponent::OnRep_bIsDead → OnDeath.Broadcast → OnHealthComponentDeath → ExecuteDeathLocal()
	 *            + Multicast_Die RPC → ExecuteDeathLocal()
	 *
	 * 用 bDeathSequenceStarted 保证 ExecuteDeathLocal 核心步骤只执行一次
	 *
	 * 大厂原则 - 单一真理源: 字段在本组件, 不再在 BaseCharacter
	 *
	 * 【v207 大厂架构修复】语义变更: 此标志仅由 ExecuteDeathLocal() 设置
	 *   - 历史 bug: Die() 提前设此标志为 true, 导致 Die() 末尾的 ExecuteDeathLocal() 调用被幂等跳过
	 *             → 服务器武器不掉落/不溶解, 客户端正常
	 *   - 修复: Die() 用独立的 bDieStarted 幂等标志 (自身语义), bDeathSequenceStarted 保留给 ExecuteDeathLocal
	 *   - 不破坏客户端: 客户端走 OnHealthComponentDeath → ExecuteDeathLocal (首次设置 bDeathSequenceStarted), 不受影响
	 */
	UPROPERTY(Transient)
	bool bDeathSequenceStarted = false;

	/**
	 * Die() 自身的幂等标志 — 【v207 新增】单一真理源分离
	 *
	 * 历史 bug (Session1.txt 2026.08.09):
	 *   - Die() 在 line 547 设置 bDeathSequenceStarted=true
	 *   - Die() 在 line 669 调 ExecuteDeathLocal() → 被 bDeathSequenceStarted 幂等跳过
	 *   - 后果: 服务器武器不掉落/不溶解
	 *
	 * 大厂原则 - 单一职责:
	 *   - bDieStarted: Die() 自己的幂等 (防止 Die() 被多次调用)
	 *   - bDeathSequenceStarted: ExecuteDeathLocal() 自己的幂等 (防止 ExecuteDeathLocal 被多次调用)
	 *   - 两者互不干扰
	 */
	UPROPERTY(Transient)
	bool bDieStarted = false;

	/**
	 * 布娃娃定时器句柄 — ExecuteDeathLocal 设置, EnableRagdoll 回调
	 */
	FTimerHandle RagdollTimerHandle;

	/**
	 * 布娃娃启动延迟 (秒) — ExecuteDeathLocal 计算后传入 SetTimer
	 *
	 * 默认为 0.0 — 实际由 ExecuteDeathLocal 根据 AnimDuration * 0.7 计算
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Combat|Death")
	float RagdollDurationSeconds = 5.0f;

	/**
	 * 武器延迟销毁时间 (秒) — DropAndFadeWeapon 内 SetLifeSpan 使用
	 *
	 * 大厂原则 - 单一权威: 仅服务器调用 SetLifeSpan, 客户端通过 OnDestroyed 自动同步
	 *
	 * 默认 3.0s: 玩家能看到武器掉地物理动画 + 溶解特效
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Combat|Death", meta = (ClampMin = "0.5", ClampMax = "30.0"))
	float WeaponDestroyDelaySeconds = 3.0f;

	/**
	 * 复活延迟时间 (秒) — Multicast_Die 末尾传递给 PlayerController
	 *
	 * 必须大于 DissolveDuration + DeathMontageDuration
	 * 默认 3.0s: 给溶解 + 死亡动画 + 武器掉地留足视觉时间
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Combat|Respawn", meta = (ClampMin = "1.0", ClampMax = "30.0"))
	float RespawnDelaySeconds = 3.0f;

	/**
	 * 默认复活无敌期 (秒) — ActivateSpawnInvincibility 在 DurationOverride<0 时用
	 *
	 * 大厂原则 - 封装: 用户决策 A: 配错 (≤0) → 静默跳过, 不强制默认值
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Combat|Respawn", meta = (ClampMin = "0.0", ClampMax = "30.0"))
	float DefaultSpawnInvincibilitySeconds = 2.0f;

	/**
	 * Owner Character 的死亡动画蒙太奇 — ExecuteDeathLocal 播放
	 *
	 * 通过 Owner->GetMesh()->GetAnimInstance() 播放
	 */
	UPROPERTY(EditDefaultsOnly, Category = "Combat|Death")
	TObjectPtr<UAnimMontage> DeathMontage = nullptr;

	// ==========================================
	// UE 生命周期
	// ==========================================
public:
	/**
	 * UE 组件生命周期: 组件被注册时调用 (类似 BeginPlay)
	 * 用途: 缓存 Owner Character 引用
	 */
	virtual void BeginPlay() override;

	/**
	 * UE 组件生命周期: 组件被注销时调用 (类似 EndPlay)
	 * 用途: 清空 RagdollTimerHandle, 清空 Owner 引用, 清空死亡标志
	 */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;
};