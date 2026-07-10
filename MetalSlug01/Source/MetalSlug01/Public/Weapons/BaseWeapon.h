// 版权声明：在项目设置的描述页面填写您的版权信息。

#pragma once

// ==========================================
// 头文件包含说明
// ==========================================
// UE 引擎核心最小化头文件
#include "CoreMinimal.h"

// 引入 UE 原生 AActor 类（基类）
#include "GameFramework/Actor.h"

// 引入 EKillMethod 击杀方式枚举
#include "Data/Enums/CombatEnums.h"

// UE 自动生成的头文件
#include "BaseWeapon.generated.h"

class UWeaponDissolveComponent;

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
	/**
	 * 内部标记: 武器当前是否激活（具有杀伤力）
	 */
	bool bIsWeaponActive = false;

	/**
	 * 内部标记: 当前这刀是轻击还是重击
	 */
	bool bIsCurrentAttackHeavy = false;

	// 【P0 2026.07.07 大厂架构重构 — 已删除】 bIsAIWeaponActive 字段
	//
	// 历史: 旧版 AI 通道与 trace 互斥机制的标志, Tick 用它跳过 trace
	// 当前: 玩家/AI 共用同一 trace 路径, 不再需要互斥
	// 替代: 由 ABaseCharacter::bIsCurrentlyAttackerAI 替代 (基于攻击者状态而非武器状态)

	/**
	 * 记录这一刀已经砍中过的人，防止一刀对同一个人造成多次伤害
	 */
	UPROPERTY()
	TArray<AActor*> IgnoreActors;

	/**
	 * 记录上一帧刀刃的起点和终点（用于刀刃扫掠追踪）
	 */
	FVector LastFrameStartLoc;
	FVector LastFrameEndLoc;

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
	// 1. 武器核心组件
	// ==========================================
protected:
	/**
	 * 武器的静态模型（比如一把小刀、斧头、尼泊尔）
	 * 注意: 如果需要武器有变形动画，可以换成 USkeletalMeshComponent
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
	class UStaticMeshComponent* WeaponMesh;


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
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Weapon Stats")
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
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon Animations")
	TArray<class UAnimMontage*> LightAttackMontages;

	/**
	 * 重击通常只有一个动画
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Weapon Animations")
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
};
