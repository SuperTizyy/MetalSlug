// 版权声明：在项目设置的描述页面填写您的版权信息。

#pragma once

// UE 引擎核心最小化头文件
#include "CoreMinimal.h"

// 引入 EKillMethod 击杀方式枚举
#include "Data/Enums/CombatEnums.h"

// IWeaponDamageStrategy 完整定义 (TScriptInterface UPROPERTY 需要完整接口类型)
#include "Weapons/WeaponDamageStrategy.h"

// 前向声明 (放在 .generated.h 之后)
class UWeaponDissolveComponent;
class UWeaponFireComponent;

// UE 自动生成的头文件 (.generated.h 必须最后)
#include "BaseWeapon.generated.h"

/**
 * @class ABaseWeapon
 * @brief 项目所有武器的 C++ 基类
 *
 * 职责说明:
 * - 武器静态模型（StaticMesh）
 * - 刀刃扫掠判定（每帧 SphereTrace 检测命中）
 * - 伤害计算（轻击/重击/爆头）
 * - 武器专属动画库（轻击连斩 + 重击）
 * - 网络同步（Server RPC 报告命中）
 *
 * 架构理念:
 * 1. 数据驱动：每把武器在蓝图中配置自己的伤害/范围/动画
 * 2. 服务器权威：所有伤害判定走 Server RPC
 * 3. 一刀多伤防护：使用 IgnoreActors 列表防止重复伤害
 * 4. 轻击连斩：每把武器可配置多个连击动画（左划/右划/下劈）
 */
UCLASS()
class METALSLUG01_API ABaseWeapon : public AActor
{
	GENERATED_BODY()

public:
	// ==========================================
	// 构造函数与生命周期
	// ==========================================

	/**
	 * 构造函数: 设置默认值
	 * 目的: 创建武器组件、初始化属性
	 */
	ABaseWeapon();

	/**
	 * UE 原生生命周期: 每帧调用
	 * 用途: 武器激活时进行刀刃扫掠判定
	 */
	virtual void Tick(float DeltaTime) override;

	/**
	 * 【v81 客户端武器可用性修复】网络复制字段注册
	 *
	 * 旧 (v80-): bReplicates=true 但 GetLifetimeReplicatedProps 不存在
	 *   → Actor 本身复制 (引用同步), 但 UPROPERTY 字段不复制
	 *   → 客户端的 Weapon.AttackRange / MeshType / WeaponRowName / MuzzleOffset 永远是 BP 默认值
	 *   → OnFirePressed 静默 return / RangedLineStrategy 射程错误 / Server_ReportHit 伤害计算异常
	 *
	 * 新 (v81): 注册 5 个核心字段 → UE 自动复制 → 客户端的武器数据跟服务器一致
	 */
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/**
	 * 【P0 2026.07.07 大厂架构】销毁兜底 — EndPlay 时清理通道状态
	 * 防止:
	 *   - 武器销毁时 AI 通道残留为 true, 影响其他复用 Character 的人
	 *   - 状态泄漏到下一关卡 (跨关卡 GC 抖动)
	 */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;


	// ==========================================
	// 武器激活接口
	// ==========================================

/**
 * 开启武器刀刃伤害判定（玩家路径，由 AnimNotify 调用）
 *
 * 【P0 2026.07.07】与 AI 通道互斥
 *   如果当前 bIsAIWeaponActive=true,本调用被忽略(避免双扣血)
 *   玩家挥刀期间如果 AI 通道被外部错误开启,本调用也设 false 防止冲突
 *
 * 【v35 AnimNotify 化】调用方: BP AnimNotify (ANS_MeleeTrace / ANS_HeavyTrace)
 *   - 美术在蒙太奇"挥刀中段"位置加 ANS_MeleeTrace (轻击) 或 ANS_HeavyTrace (重击)
 *   - ANS_MeleeTrace 内: Get Current Weapon → StartWeaponTrace(false)
 *   - ANS_HeavyTrace 内: Get Current Weapon → StartWeaponTrace(true)
 *   - 收刀位置加 ANS_MeleeTraceEnd → Get Current Weapon → StopWeaponTrace
 *
 * @param bIsHeavyAttack 是否为重击（决定伤害数值和动画）
 */
UFUNCTION(BlueprintCallable, Category = "Combat")
void StartWeaponTrace(bool bIsHeavyAttack);

/**
 * 关闭武器刀刃伤害判定（玩家路径）
 *
 * 【P0 2026.07.07】不会关闭 AI 通道,两个通道独立管理
 *
 * 【v35】调用方: BP AnimNotify (ANS_MeleeTraceEnd) 或蒙太奇自然结束 C++ 兜底清理
 */
UFUNCTION(BlueprintCallable, Category = "Combat")
void StopWeaponTrace();

	/**
	 * 【P0 2026.07.07 大厂架构重构 — DEPRECATED】开启 AI 伤害通道
	 *
	 * 历史背景 (现已废弃):
	 *   旧版 AI 攻击通过 Server_ReportHit(Damage=ConfigSO.Damage) 走 DamageOverride,
	 *   BP_Weapon_Knife 蓝图 AnimNotify 同时调 StartWeaponTrace 开了 trace,
	 *   导致一次挥刀扣两次血 (trace = LightDamageBody + AI = ConfigSO.Damage)。
	 *   旧修复: AI 通道与 trace 通道互斥, 用 bIsAIWeaponActive 屏蔽 trace。
	 *
	 * 新架构 (2026.07.07 重构后):
	 *   玩家和 AI **完全共用同一套 trace 路径**
	 *   - 不再需要"AI 屏蔽 trace"的互斥机制
	 *   - 伤害来源决策由 Server_ReportHit 读 Owner.bIsCurrentlyAttackerAI 决定
	 *   - AI 通道字段 bIsAIWeaponActive / IsAIWeaponActive() / SetAIWeaponActive() 全部删除
	 *
	 * 当前: 本函数变为 no-op, 保留仅为兼容遗留调用方
	 *       玩家/AI 都通过 startWeaponTrace → Tick BoxTrace → trace 命中 → 扣血
	 *
	 * 调用方: ABaseCharacter::OnAIRequestAttack_Simple (已不再调用, 删除中)
	 *
	 * @deprecated 2026.07.07 — 旧版 AI 通道与 trace 互斥机制已废弃
	 *                       玩家/AI 共用 trace, 由 Owner Character 的 bIsCurrentlyAttackerAI 决定伤害
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat|AI",
		meta = (DeprecatedProperty, DeprecationMessage = "已废弃 2026.07.07, 玩家/AI 共用 trace 路径, 用 BaseCharacter::SetAttackerIsAI 替代"))
	void SetAIWeaponActive(bool bActivate) { (void)bActivate; }

	/**
	 * 查询 AI 通道是否激活
	 *
	 * 【P0 2026.07.07 大厂架构重构 — DEPRECATED】已废弃
	 *   AI 通道字段已删除, 本接口为兼容保留, 永远返回 false
	 *
	 * @deprecated 2026.07.07 — AI 不再有独立通道, 全走 trace
	 */
	UFUNCTION(BlueprintPure, Category = "Combat|AI",
		meta = (DeprecatedProperty, DeprecationMessage = "已废弃 2026.07.07, 永远返回 false"))
	bool IsAIWeaponActive() const { return false; }


	// ==========================================
	// 基础属性
	// ==========================================

	/**
	 * 武器基础伤害（兜底用）
	 *
	 * 【P0 2026.07.06 重构】已废弃 — 不再被读
	 *
	 * 历史:
	 *   - 旧版设计: 武器有一个统一的"基础伤害"字段, 各种攻击都从这里派生
	 *   - 实际: 该字段从未被 C++ 读过, LightDamageBody/Head/Heavy 才是真正的伤害源
	 *
	 * 当前:
	 *   - AI 通道: 走 ABaseAIController::GetEffectiveAttackDamage() → ConfigSO.Damage
	 *   - 玩家通道: 走 BaseWeapon::Server_ReportHit → LightDamageBody/Head/Heavy
	 *   - BaseDamage 字段保留 (BlueprintReadWrite) 以免破坏现有 BP, 但不参与实际伤害计算
	 *
	 * 新设计: 武器的"统一伤害"概念被拆为 LightDamageBody/Head/Heavy 三个精确字段
	 *         AI 行为配置走 ConfigSO, 不再受武器"基础伤害"字段干扰
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat",
		meta = (DeprecatedProperty, DeprecationMessage = "已废弃, AI 通道走 ConfigSO.Damage, 玩家通道走 LightDamageBody/Head/Heavy"))
	float BaseDamage = 20.0f;

	/**
	 * 轻击时允许移动吗？
	 * 例: 短剑 = true（轻击快）, 大锤 = false（重击慢）
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat Config")
	bool bCanMoveWhileLightAttack = true;

	/**
	 * 重击时允许移动吗？
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat Config")
	bool bCanMoveWhileHeavyAttack = false;

	/**
	 * 下蹲时允许攻击吗？(统管轻重击)
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat Config")
	bool bCanAttackWhileCrouched = true;

protected:
	// 【v75 架构清理】删除旧版 trace 状态字段
	//   - bIsWeaponActive / bIsCurrentAttackHeavy / IgnoreActors / LastFrameStartLoc / LastFrameEndLoc
	//   - 全部由 DamageStrategy (UMeleeSwStrategy / URangedLineStrategy) 内部持有
	//   - 真理源唯一 — 不再 BaseWeapon / Strategy 双状态

	// 【P0 2026.07.07 大厂架构重构 — 已删除】 bIsAIWeaponActive 字段
	//
	// 历史: 旧版 AI 通道与 trace 互斥机制的标志, Tick 用它跳过 trace
	// 当前: 玩家/AI 共用同一 trace 路径, 不再需要互斥
	// 替代: 由 ABaseCharacter::bIsCurrentlyAttackerAI 替代 (基于攻击者状态而非武器状态)

public:
	/**
	 * 获取最后造成的击杀方式（供 HUD 显示用）
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat")
	EKillMethod GetLastKillMethod() const { return LastKillMethod; }

	/**
	 * 报告命中（Server RPC）
	 *
	 * 【P0 2026.07.07 大厂架构重构】新增 bIsAIDriven 参数
	 *   - bIsAIDriven=false (玩家路径,默认): 走武器字段重算 (LightDamageBody/Head/Heavy)
	 *   - bIsAIDriven=true  (AI 路径):     强制走 DamageOverride, 不读武器字段
	 *     这是修复"AI 一次攻击造成两种伤害"的核心:
	 *       旧版 AI 调用时 Damage=AIDamage, 但代码仍会兜底 if (bIsHeavy) else, 走武器字段
	 *       → 若 BP 把 LightDamageBody 改了, AI 伤害被覆盖
	 *       → 加上 bIsAIDriven 后, AI 通道完全独立, ConfigSO.Damage 单一真值源
	 *
	 * 流程: 客户端检测到命中后调用，服务器执行伤害
	 *
	 * @param HitActor    受击目标 (服务器校验: 必须是 ABaseCharacter 且未死)
	 * @param Damage      伤害值
	 *                      - 玩家路径: 传 0.0f, 服务器按部位 + bIsHeavy 重算
	 *                      - AI 路径:   传 ConfigSO.Damage (已难度缩放), 服务器直接用
	 * @param HitLocation 命中世界坐标 (服务器校验 NaN)
	 * @param HitNormal   命中法线 (服务器校验单位向量)
	 * @param BoneName    骨骼名 (玩家路径用于判定爆头)
	 * @param bIsHeavy    是否重击
	 * @param bIsAIDriven 是否 AI 通道 (强制走 DamageOverride, 默认 false 玩家路径)
	 */
	UFUNCTION(Server, Reliable, WithValidation)
	void Server_ReportHit(AActor* HitActor, float Damage, FVector HitLocation, FVector HitNormal, FName BoneName, bool bIsHeavy, bool bIsAIDriven = false);

protected:
	/**
	 * 最后造成的击杀方式（由 Tick 中判定并存储）
	 * 供 HUD 调用显示击杀图标
	 */
	UPROPERTY()
	EKillMethod LastKillMethod = EKillMethod::MeleeWeapon;


	// ==========================================
	// 1. 武器核心组件 (由 BP 手动添加)
	// ==========================================
	// 
	// 【v61.3 重构】不再声明 WeaponMesh 字段 — 由 BP Components 面板手动添加
	// GetMeshComponent() 通过 FindComponentByClass 运行时查找,不依赖 C++ 字段
	// 支持:
	//   - StaticMeshComponent (近战武器)
	//   - SkeletalMeshComponent (枪械)


	// ==========================================
	// 2. 武器核心战斗数值（蓝图里可以针对不同武器随便改）
	// ==========================================
public:
	/**
	 * 轻击打身体的伤害
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Weapon Stats")
	float LightDamageBody;

	/**
	 * 轻击爆头伤害
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Weapon Stats")
	float LightDamageHead;

	/**
	 * 重击伤害 (一击毙命)
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Weapon Stats")
	float HeavyDamage;

	/**
	 * 攻击判定距离（例如尼泊尔比小刀长）
	 *
	 * 【v81 客户端武器可用性修复】Replicated:
	 *   旧 (v80-): 仅服务器 SpawnAndEquipWeapon → InitializeWeaponFireConfigFromClass 写入
	 *   → 客户端的 Weapon.AttackRange 永远是 BP 默认值 (150.0f)
	 *   → 客户端 URangedLineStrategy::PerformSingleShot 用错误射程 → 看不到伤害
	 *   新: 服务器写入 → UE 自动复制 → 客户端可用正确的策划配置射程
	 */
	UPROPERTY(Replicated, EditDefaultsOnly, BlueprintReadWrite, Category = "Weapon Stats")
	float AttackRange;

	/**
	 * 攻击判定范围的粗细（比如铁锹的横扫范围比小刀大）
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Weapon Stats")
	float AttackRadius;


	// ==========================================
	// 3. 武器专属动画库（角色挥刀时，会找武器借这些动画播）
	// ==========================================
protected:
	/**
	 * 【核心升级】: 轻击连斩动画数组！
	 * 你可以往里面塞 左划、右划、下劈 3个不同的动画
	 */
	UPROPERTY(Replicated, EditDefaultsOnly, BlueprintReadWrite, Category = "Weapon Animations")
	TArray<class UAnimMontage*> LightAttackMontages;

	/**
	 * 重击通常只有一个动画
	 */
	UPROPERTY(Replicated, EditDefaultsOnly, BlueprintReadWrite, Category = "Weapon Animations")
	class UAnimMontage* HeavyAttackMontage;


	// ==========================================
	// 4. 核心功能接口（供拿着它的 Character 调用）
	// ==========================================
public:
	/**
	 * 根据角色的连击进度（ComboIndex），返回对应的那一段挥刀动画
	 * @param bIsHeavy 是否为重击
	 * @param ComboIndex 连击段数（轻击使用）
	 * @return 对应的 AnimMontage
	 */
	UAnimMontage* GetAttackMontage(bool bIsHeavy, int32 ComboIndex = 0) const;


	// ==========================================
	// 【v83.1 大厂架构】Montage 数量 / 重击查询 getter (公开)
	//
	// 背景: v83 PlayerComboComponent 直接访问 protected LightAttackMontages.Num()
	//   → 编译错 C2248 无法访问 protected 成员
	//
	// 大厂原则 (封装 + 公开 API):
	//   - protected 字段不能被外部直接读, 必须通过 getter
	//   - getter 是单一真理源 (PlayerComboComponent 不需要知道内部是 TArray 还是 TMap)
	//   - inline 零开销
	//   - const 方法, 纯查询
	//
	// 调用方: PlayerComboComponent::ExecuteComboSequence (v83.1 可观测性 log)
	// ==========================================
	/**
	 * 返回轻击蒙太奇数量 (供客户端可观测性 log + 强制 trace 兜底判定)
	 */
	FORCEINLINE int32 GetLightAttackMontageCount() const
	{
		return LightAttackMontages.Num();
	}

	/**
	 * 返回重击蒙太奇指针 (供客户端可观测性 log 报告)
	 */
	FORCEINLINE UAnimMontage* GetHeavyAttackMontagePtr() const
	{
		return HeavyAttackMontage;
	}


	// ==========================================
	// 5. 溶解特效（2026.07.10 大厂 P0 重构 — 职责对等）
	// ==========================================
public:
	/**
	 * 【大厂 P0 2026.07.10】武器死亡溶解入口 — 公开唯一接口
	 *
	 * 死亡流程调用: ABaseCharacter::DropAndFadeWeapon
	 *
	 * 设计原则 (类比身体溶解):
	 *   - 身体溶解: ABaseCharacter 持有 UDissolveComponent → 驱动自己的 Mesh
	 *   - 武器溶解: ABaseWeapon 持有 UWeaponDissolveComponent → 驱动自己的 Mesh
	 *   - 职责对等: 武器管自己的溶解, 角色不再穿透调用
	 *   - 零跨边界: 武器 Detach 后完全自治, 不依赖角色任何状态
	 *
	 * 调用时序 (大厂规范):
	 *   1. 武器必须已 Detach (Mesh 不再跟随角色骨骼)
	 *   2. 武器必须已启用物理模拟 (SetSimulatePhysics(true))
	 *   3. 调本方法 → UWeaponDissolveComponent 收集自己 Mesh 的 MID
	 *   4. Tick 累加 DissolveAmount → 材质完成溶解动画
	 *   5. 角色 SetLifeSpan(3.0) → UE 引擎在 3 秒后自动 Destroy
	 *
	 * 协议 (与身体一致 — 零兜底):
	 *   - 武器材质蓝图必须调用 MF_Dissolve 节点 (有 DissolveAmount 参数)
	 *   - 协议不满足时, 组件 Log(Warning) 报警 (在 CollectDynamicMaterials 阶段)
	 */
	UFUNCTION(BlueprintCallable, Category = "Weapon|Dissolve")
	void StartDissolve();

	/**
	 * 查询武器是否正在溶解中
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Weapon|Dissolve")
	bool IsDissolving() const;

	/**
	 * 武器溶解组件 — 自治组件, 拥有自己的生命周期
	 *
	 * 设计:
	 *   - 在 ABaseWeapon 构造函数中 CreateDefaultSubobject 创建
	 *   - 武器 Actor 销毁时, Component 随 Actor 自动销毁
	 *   - 武器 Detach 后, Component 继续工作 (Actor 还在, 组件还在)
	 *   - 角色不再穿透调用, 零跨边界引用
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<UWeaponDissolveComponent> WeaponDissolveComponent;

	// ==========================================
	// v70 缺失方法补充 (保守修复 — BaseWeapon.cpp 被还原后调用的遗留方法)
	// ==========================================

	/**
	 * GetMeshType — 查询武器 Mesh 类型 (EWeaponMeshType)
	 *
	 * 调用方: WeaponAttachmentComponent / BaseAnimInstance / BaseCharacter / MeleeSwStrategy
	 *
	 * v61.2 重构: MeshType 字段从 DT_WeaponInfo 写入 Weapon->MeshType.
	 * GetMeshType() 直接返回字段值 (单一真理源).
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Weapon|Config")
	EWeaponMeshType GetMeshType() const;

	/**
	 * GetMeshComponent — 获取武器 Mesh 组件 (UMeshComponent)
	 *
	 * 调用方: WeaponAttachmentComponent / CombatDeathComponent / MeleeSwStrategy
	 *
	 * 【v61.3 重构】运行时按需查找, 不再依赖构造函数创建的固定字段
	 * 允许 BP 手动挂载不同类型 (StaticMesh/SkeletalMesh).
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Weapon|Components")
	UMeshComponent* GetMeshComponent() const;

	/**
	 * BeginPlay — UE 生命周期 (大厂架构 v83+ 客户端武器可用性)
	 *
	 * 调用方: UE 引擎 (Actor 初始化完成后)
	 *
	 * 【核心职责】强制禁用武器 Mesh 的物理碰撞
	 *   - 服务器: 调用 SpawnAndEquipWeapon / Server_SpawnAllWeapons / OnRep_CurrentWeaponSlot 时已设 NoCollision
	 *   - 客户端: 客户端进程不执行这些 Server 函数 → 武器 Mesh 仍保留 BP 默认 BlockAllDynamic 碰撞
	 *     → 当玩家拿着近战武器 (StaticMesh 70cm 长) 在场景里跑, 武器 Mesh 阻挡自身角色 Capsule 移动
	 *     → 客户端画面"一直抖动" + SpringArm (ProbeSize=12) 被武器 Mesh 阻挡 → 镜头缩到角色身上
	 *
	 * 大厂原则 (零兜底 + 单一真理源):
	 *   - 服务器端 BeginPlay 设 NoCollision 是双保险 (与 SpawnAndEquipWeapon 等路径对称)
	 *   - 客户端端 BeginPlay 设 NoCollision 是必须的 (Server 函数不跑客户端)
	 *   - 这是"物理正确性" 不是"业务兜底": 武器 mesh 不应该阻挡任何其他 actor (武器只负责装饰和伤害)
	 *
	 * 大厂原则 (Decoupled Architecture):
	 *   - 不用反射 / 不用事件
	 *   - 直接调 GetMeshComponent() (单一真理源), 设 NoCollision
	 *   - 找不到 Mesh → Log Warning (不影响游戏, 因为玩家拿着的武器一定有 Mesh)
	 */
	virtual void BeginPlay() override;

	/**
	 * StartDamageTrace — 启动伤害追踪 (v73 大厂架构)
	 *
	 * 调用方:
	 *   - PlayerComboComponent::EnableComboWindow (玩家挥刀中段开启 trace)
	 *   - AIAttackComponent (AI 攻击蒙太奇中段开启 trace)
	 *
	 * 替代旧版"依赖 BP AnimNotify 调用 PerformDamageTrace" 的反模式:
	 *   - 旧版 BP 配错 / 美术忘配 → 玩家/AI 永远没有伤害检测 (用户看不到错, 只看到"打不出伤害")
	 *   - v73: C++ 在状态机确定的时机 (挥刀中段) 强制调用, 不依赖 BP 配置
	 *
	 * @param bIsHeavy  是否重击 (近战区分轻击/重击伤害)
	 */
	UFUNCTION(BlueprintCallable, Category = "Weapon|Combat")
	void StartDamageTrace(bool bIsHeavy);

	/**
	 * StopDamageTrace — 停止伤害追踪 (玩家路径兼容)
	 *
	 * 调用方: PlayerComboComponent / AIAttackComponent / CombatDeathComponent / MeleeSwStrategy
	 *
	 * 【v73 大厂架构修复】真正转发到 DamageStrategy, 不再是 no-op stub
	 */
	UFUNCTION(BlueprintCallable, Category = "Weapon|Combat")
	void StopDamageTrace();

	/**
	 * Multicast_PlayFireMontage — 多播播放开火蒙太奇
	 *
	 * 调用方: WeaponFireComponent::MulticastPlayFireMontage
	 *
	 * v70 保守修复: 旧版是 Weapon RPC, 新版 Strategy 内.
	 * 这里做 no-op stub (蒙太奇播放由 WeaponFireComponent::PerformSingleShot 内部处理).
	 */
	UFUNCTION(NetMulticast, Reliable, Category = "Weapon|Animation")
	void Multicast_PlayFireMontage();

	/**
	 * Multicast_PlayReloadMontage — 多播播放换弹蒙太奇
	 *
	 * 调用方: WeaponFireComponent::MulticastPlayReloadMontage
	 *
	 * v70 保守修复: 同上.
	 */
	UFUNCTION(NetMulticast, Reliable, Category = "Weapon|Animation")
	void Multicast_PlayReloadMontage();

	/**
	 * Multicast_PlayFireTraceVisual — 客户端同步显示服务器 trace 视觉
	 *
	 * 调用方: URangedLineStrategy::PerformSingleShot (服务器权威 trace 完后)
	 *
	 * 架构:
	 *   - 服务器权威 trace (单次权威判定)
	 *   - 服务器 trace 完后调 Multicast → 所有客户端画 DrawDebugLine (红线/绿线, 与服务器一致)
	 *   - 大厂原则: 服务器权威 trace → 客户端视觉同步, 不重复 trace
	 *
	 * @param StartLoc trace 起点
	 * @param EndLoc   trace 终点
	 * @param bHit     是否命中 (决定颜色: 命中绿, 未命中红)
	 * @param HitLoc   命中点 (bHit=true 时有效)
	 */
	UFUNCTION(NetMulticast, Reliable, Category = "Weapon|Debug")
	void Multicast_PlayFireTraceVisual(FVector StartLoc, FVector EndLoc, bool bHit, FVector HitLoc);

	/**
	 * Server_StartFire — 服务器开始开火 (枪械)
	 *
	 * 调用方: BaseCharacter::OnFirePressed (玩家路径)
	 *
	 * v70 保守修复: 旧版是 Weapon RPC, 新版 WeaponFireComponent 内.
	 * 这里 stub 做 no-op (真实实现在 WeaponFireComponent::Server_StartFire).
	 *
	 * v82 大厂架构修复 — 客户端射线参数:
	 *   - 客户端玩家在 OnFirePressed 用 HUD Crosshair 算出射线 → RPC 传给服务器
	 *   - 服务器 WeaponFireComponent::StartFire 缓存射线, 全自动 Tick 复用
	 *   - AI 路径 (BT): 传零向量 = Strategy 内部 fallback BaseAimRotation
	 *
	 * @param ClientRayOrigin     客户端射线起点 (ZeroVector = AI 路径)
	 * @param ClientRayDirection  客户端射线方向 (ForwardVector = AI 路径)
	 */
	UFUNCTION(Server, Reliable, Category = "Weapon|Combat")
	void Server_StartFire(const FVector_NetQuantize& ClientRayOrigin, const FVector_NetQuantizeNormal& ClientRayDirection);

	/** Server_StartFire_Implementation — 服务器接收射线参数后透传到 WeaponFireComponent */
	void Server_StartFire_Implementation(const FVector_NetQuantize& ClientRayOrigin, const FVector_NetQuantizeNormal& ClientRayDirection);

	/** Server_StartFire_Validate — 验证函数 */
	bool Server_StartFire_Validate();

	/**
	 * Server_StopFire — 服务器停止开火
	 *
	 * 调用方: BaseCharacter::OnFireReleased
	 */
	UFUNCTION(Server, Reliable, Category = "Weapon|Combat")
	void Server_StopFire();

	/** Server_StopFire_Validate — 验证函数 */
	bool Server_StopFire_Validate();

	/**
	 * Server_StartReload — 服务器开始换弹
	 *
	 * 调用方: BaseCharacter::OnReloadPressed
	 */
	UFUNCTION(Server, Reliable, Category = "Weapon|Combat")
	void Server_StartReload();

	/** Server_StartReload_Validate — 验证函数 */
	bool Server_StartReload_Validate();

	// ==========================================
	// v70 缺失方法: 弹匣相关 (枪械 AnimNotify 调用)
	// ==========================================

	/**
	 * ResolveMagazineSkeletalMesh — 获取弹匣 SkeletalMesh 组件
	 *
	 * 调用方: AnimNotify_AttachMagazine / AnimNotify_DetachMagazine
	 *
	 * v70 保守修复: 枪械弹匣 AnimNotify 需要此方法.
	 * 这里返回 nullptr (近战武器没有弹匣, AnimNotify 会走 nullptr 分支).
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Weapon|Magazine")
	class USkeletalMeshComponent* ResolveMagazineSkeletalMesh() const;

	/**
	 * RestoreMagazineToWeapon — 将弹匣重新挂回武器
	 *
	 * 调用方: AnimNotify_AttachMagazine
	 *
	 * v70 保守修复: 枪械 AnimNotify 需要此方法.
	 * 这里做 no-op stub (近战武器没有弹匣, 无需操作).
	 */
	UFUNCTION(BlueprintCallable, Category = "Weapon|Magazine")
	void RestoreMagazineToWeapon();

	// ==========================================
	// v70 缺失字段补充 (被其他文件引用但 BaseWeapon 旧版没有)
	// ==========================================

	/**
	 * MuzzleOffset — 枪口偏移量 (厘米)
	 *
	 * 调用方: RangedLineStrategy::PerformSingleShot
	 *
	 * v70 保守修复: 旧版有此字段 (默认 0.0f).
	 *
	 * 【v81 客户端武器可用性修复】Replicated:
	 *   客户端 RangedLineStrategy::PerformSingleShot 必须读这个值
	 *   → 客户端 MuzzleOffset=0 → 射线起点偏到角色中心 → 客户端射偏
	 */
	UPROPERTY(Replicated, EditAnywhere, BlueprintReadWrite, Category = "Weapon Stats")
	float MuzzleOffset = 0.0f;

	/**
	 * WeaponRowName — 武器行名 (DT_WeaponInfo 查表用)
	 *
	 * 调用方: WeaponAttachmentComponent::InitializeWeaponFireConfigFromClass
	 *
	 * v70 保守修复: 旧版有此字段.
	 *
	 * 【v81 客户端武器可用性修复】Replicated:
	 *   客户端的 WeaponRowName 必须跟服务器一致 (RangedLineStrategy::PerformSingleShot 不直接读这个字段,
	 *   但 AnimNotify_AttachMagazine / AnimNotify_DetachMagazine / Server_ReportHit 等多处读)
	 *   → 客户端为空 → 武器名解析失败
	 */
	UPROPERTY(Replicated, EditAnywhere, BlueprintReadWrite, Category = "Weapon Config")
	FName WeaponRowName = NAME_None;

	/**
	 * 【v61.2 单一真理源】武器模型类型
	 *
	 * 真理源: DT_WeaponInfo.MeshType 由 WeaponAttachmentComponent 写入此处.
	 * GetMeshType() 返回此字段值, 不再依赖 BP 蓝图层的 MeshType 字段.
	 *
	 * 默认 EWeaponMeshType::Melee (C++ 层保证有值, DT 配错时在 InitializeWeaponFireConfigFromClass 报错).
	 *
	 * 【v81 客户端武器可用性修复】Replicated:
	 *   客户端的 MeshType 必须跟服务器一致 (OnFirePressed → GetCurrentWeapon() → GetMeshType()
	 *   决定走 LightAttack_Pressed 还是 Server_StartFire)
	 *   → 客户端 MeshType=None (BP 默认值) → 客户端无法分类武器类型 → 按左键没反应
	 */
	UPROPERTY(Replicated, EditAnywhere, BlueprintReadWrite, Category = "Weapon Config")
	EWeaponMeshType MeshType = EWeaponMeshType::Melee;

	/**
	 * WeaponFireComponent — 武器开火组件 (枪械特有)
	 *
	 * 调用方: WeaponAttachmentComponent::InitializeWeaponFireConfigFromClass / MeleeSwStrategy
	 *
	 * v70 保守修复: 旧版有此字段, 新版 Strategy 在组件内.
	 * 这里声明以便其他文件引用.
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TObjectPtr<class UWeaponFireComponent> WeaponFireComponent;

	/**
	 * DamageStrategy — 武器伤害策略 (RangedLineStrategy / MeleeSwStrategy)
	 *
	 * 调用方: WeaponFireComponent::PerformSingleShot (服务器) / ANS_MeleeTraceState (客户端)
	 *
	 * 【v71 大厂架构修复】WeaponFireComponent::PerformSingleShot 调 Weapon->GetDamageStrategy()
	 * 但原版 GetDamageStrategy() 返回空 TScriptInterface，导致枪械永远无法开火。
	 * 真实存储移到这里（Weapon 是 Strategy 的 Outer，与 FireComponent 并列）。
	 *
	 * 【v82 客户端武器可用性修复】Replicated:
	 *   - 旧版 (v70-v81) 注释错误: "DamageStrategy (TScriptInterface — UE 不直接复制,
	 *            但持有 UObject 引用, 由 UE 的 Object 复制系统处理)"
	 *   - 实际: TScriptInterface<UObject*> 也需要 UPROPERTY(Replicated) + DOREPLIFETIME
	 *           UE 不会"自动"复制 UObject 引用, 必须显式标记
	 *   - 客户端 ANS_MeleeTraceState::NotifyBegin 调 Weapon->StartDamageTrace → GetDamageStrategy() → nullptr
	 *   - → 客户端近战 trace 完全跑不起来 (服务器跑 Ranged 没事, 因为 HasAuthority 守卫)
	 *   - 新版 (v82): DamageStrategy Replicated → 服务器写入 → 客户端自动同步
	 */
	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	TScriptInterface<IWeaponDamageStrategy> DamageStrategy;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Weapon|Config")
	TScriptInterface<IWeaponDamageStrategy> GetDamageStrategy() const
	{
		return DamageStrategy;
	}

	UFUNCTION(BlueprintCallable, Category = "Weapon|Config")
	void SetDamageStrategy(TScriptInterface<IWeaponDamageStrategy> InStrategy)
	{
		DamageStrategy = InStrategy;
	}
};
