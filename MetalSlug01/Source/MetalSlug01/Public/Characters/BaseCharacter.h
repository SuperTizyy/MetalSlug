// 版权声明：在项目设置的描述页面填写您的版权信息。

#pragma once

// ==========================================
// 头文件包含说明
// ==========================================
// UE 引擎核心最小化头文件
#include "CoreMinimal.h"

// 引入 UE 原生 ACharacter 类（基类）
#include "GameFramework/Character.h"

// 增强输入系统所需的 FInputActionValue 结构体
#include "InputActionValue.h"

// 引入房间相关枚举（EKillMethod 等）
// 改造: 改为精确子表头, 不再 include 整个 432 行 StaticTable.h
#include "Data/Enums/CombatEnums.h"

// 引入新增的 4 个 Component (健康/能量/溶解/脚步)
#include "Components/HealthComponent.h"
#include "Components/EnergyComponent.h"
#include "Components/DissolveComponent.h"
#include "Components/FootstepComponent.h"
#include "Components/CharacterEvents.h"
#include "Components/HealthRegenComponent.h"

// 引入 DataTable 行结构体（引擎 FindRow 模板需要）
#include "Data/Tables/WeaponTableRow.h"
#include "Data/Tables/CharacterTableRow.h"

// 【P0 2026.07.06】引入 FOnMontageEnded 委托类型 (AI 攻击蒙太奇结束回调需要)
//
// 【关键】FOnMontageEnded 实际定义在 Animation/AnimInstance.h 中 (不是 AnimMontage.h),
//        它是 UAnimInstance 的 Montage_SetEndDelegate 接口使用的动态委托类型
// 注: 我们用 UAnimInstance::OnMontageEnded.AddDynamic(...) 绑定回调,
//     所以 BaseCharacter.h 不需要暴露 FOnMontageEnded 成员 (UHT 只需看到函数指针)
#include "Animation/AnimInstance.h"

// 【Phase 1】 阵营 GameplayTag (为后续 IGenericTeamAgentInterface 升级预留)
#include "GameplayTagContainer.h"

// 【Phase 1】AI 数据驱动 Profile (前置声明, 头文件不依赖 AIProfileAsset.h 加快编译)
class UAIProfileAsset;

// 【Phase 1 重构】 IG_TeamAttitude (UE5 官方阵营协议) — 由 ABaseCharacter 实现
#include "GenericTeamAgentInterface.h"

// UE 自动生成的头文件（必须放在最后）
#include "BaseCharacter.generated.h"


// ==========================================
// 前置声明（加快编译，避免循环包含）
// ==========================================
class UInputMappingContext;       // Enhanced Input 映射上下文
class USpringArmComponent;        // 弹簧臂组件
class UInputAction;               // Enhanced Input 输入动作
class UCameraComponent;           // 摄像机组件
class USkeletalMeshComponent;     // 骨骼网格组件
class ABaseWeapon;                // 武器基类
class UGameHUDWidget;             // 游戏 HUD 容器


/**
 * @class ABaseCharacter
 * @brief 项目所有角色的 C++ 基类
 *
 * 职责说明:
 * - 封装第三人称视角（弹簧臂 + 跟随摄像机）
 * - 战斗属性：血量/能量/AC/ACE 系统（含网络同步）
 * - 武器系统：装备/连斩/挂载（按 DataTable 差异化配置）
 * - 脚步声系统（按物理材质播放不同音效）
 * - 助攻追踪系统（基于时间窗的助攻判定）
 * - 生命/能量回复系统（停止移动后自动回血回蓝）
 * - 溶解特效系统（死亡后模型渐隐）
 * - 增强输入系统（移动/跳跃/下蹲/轻击/重击/技能）
 *
 * 架构理念:
 * 1. 数据驱动：通过 DataTable 配置角色+武器的挂载点、偏移等
 * 2. 网络同步：所有关键属性使用 Replicated/ReplicatedUsing
 * 3. 服务器权威：所有伤害/死亡/装备操作均通过 RPC 集中处理
 * 4. 蓝图友好：所有关键参数都暴露给蓝图可配置
 */
UCLASS()
class METALSLUG01_API ABaseCharacter : public ACharacter, public IGenericTeamAgentInterface
{
	GENERATED_BODY()

public:
	/**
	 * 构造函数: 在角色被加载时调用
	 * 目的: 创建组件、初始化属性、设置默认值
	 */
	ABaseCharacter();

	/**
	 * 必须重写此函数以注册需要网络同步的 UPROPERTY
	 * 同步内容: 血量/能量/AC/ACE/死亡状态/武器等
	 */
	virtual void GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const override;

	/**
	 * 当前攻击动作是否锁死移动
	 * 目的: 挥刀期间禁止玩家移动（动画驱动）
	 */
	UPROPERTY(BlueprintReadOnly, Category = "Combat")
	bool bIsMovementLocked = false;

	/**
	 * 【2026-06-15 重构】: 暴露给外部或蓝图调用的生死状态获取接口
	 * @return 是否已死亡
	 * 实现委托给 HealthComponent (数据权威在 Component)
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Stats")
	bool IsDead() const { return HealthComponent ? HealthComponent->IsDead() : false; }

	/**
	 * 兼容旧调用方 (RoomGameMode/RoomGameState 等已用此名)
	 * 新代码请使用 IsDead()
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Stats", meta = (DeprecatedFunction, DeprecationMessage = "请改用 IsDead()"))
	bool GetIsDead() const { return IsDead(); }

	// ==========================================
	// 阵营系统 — 【Phase 1 重构】走 IGenericTeamAgentInterface (UE5 官方阵营协议)
	// ==========================================

	/**
	 * 【Phase 1 重构】
	 * 原 uint8 TeamID 字段已废弃。
	 * 新体系: 实现 IGenericTeamAgentInterface::GetGenericTeamId(), AIPerception 直接读取
	 * 原 TeamID 字段保留为派生属性 (uint8 GetTeamID() 从内部 FactionTag 派生),
	 * 仅供旧代码 (RoomGameMode/SpawnTable) 兼容用, 不再是数据源。
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat|Team",
        meta = (Categories = "Faction"))
	FGameplayTag FactionTag;

	/** 阵营对外只读视图 — 由 Build.cs 中的 RoomGameMode/BaseAIController 兼容调用 */
	uint8 GetTeamID() const;

	/** 【Phase 1 新增】IGenericTeamAgentInterface — UE5 官方阵营协议入口 */
	virtual void SetGenericTeamId(const FGenericTeamId& NewTeamID) override;
	virtual FGenericTeamId GetGenericTeamId() const override;
	virtual ETeamAttitude::Type GetTeamAttitudeTowards(const AActor& Other) const override;

	/**
	 * 【Phase 1】阵营的 GameplayTag 形式 (统一入口)
	 * 设计: 全工程只有这里一处负责 FactionTag ↔ FGenericTeamId 互转
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat|Team")
	FGameplayTag GetFactionTag() const { return FactionTag; }

	/** Faction -> FGenericTeamId 静态映射 (0=Player, 1=Zombie, 2=Friendly, 3=Neutral, 255=Hostile) */
	static FGenericTeamId ResolveGenericTeamIdFromTag(FGameplayTag InTag);

	/**
	 * 【Phase 1 新增】AI Profile 资产引用
	 * 用途: 让编辑器里直接摆的 AI Pawn 也能拿到 Profile (无需走 RoomGameMode.AddAIToRoom)
	 * 用法:
	 *   1. 在 BP_GruntAI Details → Default Melee Profile 拖入 DA_MeleeGrunt
	 *   2. BP_MeleeAIController::BeginPlay → Cast to Pawn → Get "Melee Profile" → SetupMeleeAI
	 *
	 * 也可以走代码注入: AGameMode::AddAIToRoom 调用 AIC->SetupMeleeAI(Profile) 时无需此字段
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "AI")
	TObjectPtr<UAIProfileAsset> MeleeProfile;

	/**
	 * 【P0 大厂架构 2026.07.06】Profile 直接设置武器/角色入口
	 *
	 * 设计: SetSpawnLoadout 空实现导致 AI 无法拿武器.
	 *       改为: 存一份 Profile 引用, 后续 PossessedBy 直接从 Profile 读 WeaponID/CharacterID.
	 *
	 * 调用方: AMeleeAIController::SetupMeleeAI (关卡预放 AI 自举)
	 *
	 * 为什么不改 SetSpawnLoadout:
	 *   - 已有代码依赖其签名, 改实现可能破坏玩家路径
	 *   - Profile 存成员变量更直观, Debug 时一眼看清来源
	 */
	UFUNCTION(BlueprintCallable, Category = "AI|Profile")
	void SetMeleeProfile(UAIProfileAsset* InProfile);

	/**
	 * 【P0 大厂架构 2026.07.06】从缓存的 Profile 同步武器/角色数据
	 *
	 * 调用方: PossessedBy (在读取 SpawnWeaponID 之前先调用本方法)
	 *
	 * 逻辑:
	 *   1. 检查 MeleeProfile 是否有效
	 *   2. 如果 SpawnWeaponID 为空, 从 Profile.WeaponID 填充
	 *   3. 如果 CharacterID 为空, 从 Profile.CharacterRowName 填充
	 *   4. 输出诊断日志
	 */
	void SyncWeaponFromProfile();

protected:
	/**
	 * UE 原生生命周期: 在 Actor 首次被初始化时调用
	 * 用途: 启动回复定时器、初始化 HUD
	 */
	virtual void BeginPlay() override;

	/**
	 * UE 原生生命周期: 在 Actor 被销毁/离开世界前调用
	 * 用途: 【工业规范】显式清理所有由自身注册的 Timer
	 * 背景: UE 内部虽会通过 OnDestroyed 自动清理成员绑定的 Timer,
	 *       但显式清理可避免: (1) 边角崩溃 (2) 死亡流程中未触发的 timer 残留
	 */
	virtual void EndPlay(const EEndPlayReason::Type EndPlayReason) override;

public:
	/**
	 * UE 原生生命周期: 每帧调用
	 * 用途: 处理溶解特效、回复系统
	 */
	virtual void Tick(float DeltaTime) override;

	/**
	 * UE 原生生命周期: 设置玩家输入组件
	 * 用途: 绑定 Enhanced Input 动作
	 */
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

	/**
	 * UE 原生函数: 处理伤害
	 * @return 实际应用的伤害值
	 */
	virtual float TakeDamage(float DamageAmount, struct FDamageEvent const& DamageEvent, class AController* EventInstigator, AActor* DamageCauser) override;


	// ==========================================
	// 1. 核心组件 (第三人称视角)
	// ==========================================
public:
	// ===== 改造: 业务组件（健康/能量/溶解/脚步）已抽离为独立 Component =====
	// 设计: BaseCharacter 创建并持有 4 个业务 Component, 后续业务代码可逐步迁移
	// 行为兼容: 当前 BaseCharacter 内部仍维护同名字段, 两者并存; 后续批次将字段删除

	/** 健康管理 (血量/受伤/死亡事件) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components|Health")
	UHealthComponent* HealthComponent;

	/** 能量管理 (消耗/回复/百分比) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components|Energy")
	UEnergyComponent* EnergyComponent;

	/**
	 * 【2026-07-01 新增】角色事件总线组件
	 * 用途: BaseCharacter 通过它向 UI 层广播 7 个状态事件 (头像/血量/能量/AC/ACE/武器)
	 * 订阅方: UGameHUDWidget (在 NativeConstruct 中订阅)
	 * 优势: 消除 BaseCharacter → GameHUDWidget 的直接依赖, 实现依赖倒置
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components|CharacterEvents")
	UCharacterEvents* CharacterEvents;

	/** 溶解特效 (死亡后材质渐隐) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components|Dissolve")
	UDissolveComponent* DissolveComponent;

	/** 脚步音效 (地面检测 + 音效播放) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components|Footstep")
	UFootstepComponent* FootstepComponent;

	/**
	 * 【2026-07-01 新增】生命回复组件 (自治)
	 * 用途: 把原本散落在 BaseCharacter::Tick() 里的回血回蓝逻辑独立化
	 * 默认 bEnableAutoRegen=false (格斗游戏设计: 被攻击后血量保持不变)
	 * 蓝图可配置开启 (例如 Boss 战的护盾回复)
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components|Regen")
	UHealthRegenComponent* HealthRegenComponent;

	/**
	 * 第三人称专属的"自拍杆" (弹簧臂)
	 * 作用: 防止相机穿墙，自动跟随角色
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components|Camera")
	USpringArmComponent* CameraBoom;

	/**
	 * 挂在自拍杆后面的跟随摄像机
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components|Camera")
	UCameraComponent* FollowCamera;


	// ==========================================
	// 2. 战斗属性 (网络同步)
	// ==========================================
protected:
	// ==========================================
	// 【2026-06-15 重构】: 血量数据已下沉到 HealthComponent
	// BaseCharacter 不再持有 MaxHealth/CurrentHealth/bIsDead 字段
	// 访问通过 HealthComponent 委托方法
	// 旧 GetCurrentHealth/GetMaxHealth 委托给 Component,外部调用方无感
	// ==========================================

	/**
	 * HealthComponent->OnHealthChanged 事件回调
	 * 用途: 血量变化时刷新 HUD (替代原 OnRep_Health 路径)
	 * 调用时机:
	 *   - 服务器: HealthComponent::ApplyDamage / Heal 内部 Broadcast
	 *   - 客户端: HealthComponent::OnRep_CurrentHealth 内部 Broadcast
	 */
	UFUNCTION()
	void OnHealthChanged_Callback(float NewHealth);

	/**
	 * 服务器主动通知所属客户端刷新血量 (已废弃)
	 * 【2026-06-15 废弃】: 改用 HealthComponent->OnHealthChanged 事件, 由 OnHealthChanged_Callback 处理
	 * 保留声明仅为避免 UHT 报错
	 */
	UFUNCTION(Client, Reliable)
	void Client_UpdateHealthDisplay(float Current, float Max);

	/**
	 * 服务器主动通知所属客户端刷新能量条 (已废弃)
	 * 【2026-06-15 废弃】: 改用 EnergyComponent->OnEnergyChanged 事件
	 * 保留声明仅为避免 UHT 报错
	 */
	UFUNCTION(Client, Reliable)
	void Client_UpdateEnergyDisplay(float Current, float Max);


	// ==========================================
	// 能量系统 (网络同步)
	// 【2026-06-15 重构】: 能量数据已下沉到 EnergyComponent
	// BaseCharacter 不再持有 MaxEnergy/CurrentEnergy 字段
	// 访问通过 EnergyComponent 委托方法
	// ==========================================

	/**
	 * 消耗能量（释放技能时调用）
	 * 【2026-06-15 重构】: 委托给 EnergyComponent
	 * @param Amount 要消耗的能量
	 * @return 是否消耗成功
	 */
	UFUNCTION(BlueprintCallable, Category = "Stats|Energy")
	bool ConsumeEnergy(float Amount);

public:
	/**
	 * 获取当前血量 (供 UI 直接访问)
	 * 【2026-06-15 重构】: 委托给 HealthComponent
	 */
	UFUNCTION(BlueprintCallable, Category = "Stats")
	float GetCurrentHealth() const { return HealthComponent ? HealthComponent->GetCurrent() : 0.0f; }

	/**
	 * 获取最大血量 (供 UI 直接访问)
	 * 【2026-06-15 重构】: 委托给 HealthComponent
	 */
	UFUNCTION(BlueprintCallable, Category = "Stats")
	float GetMaxHealth() const { return HealthComponent ? HealthComponent->GetMax() : 0.0f; }

	/**
	 * 获取当前能量（供 UI 直接访问）
	 * 【2026-06-15 重构】: 委托给 EnergyComponent
	 */
	UFUNCTION(BlueprintCallable, Category = "Stats|Energy")
	float GetCurrentEnergy() const { return EnergyComponent ? EnergyComponent->GetCurrent() : 0.0f; }

	/**
	 * 获取最大能量（供 UI 直接访问）
	 * 【2026-06-15 重构】: 委托给 EnergyComponent
	 */
	UFUNCTION(BlueprintCallable, Category = "Stats|Energy")
	float GetMaxEnergy() const { return EnergyComponent ? EnergyComponent->GetMax() : 0.0f; }

protected:

	// ==========================================
	// 脚步声系统 (Footstep)
	// 【2026-06-15 重构】: 脚步音效已下沉到 FootstepComponent
	// AnimNotify 或其他系统调用本方法，实际逻辑委托给 Component
	// ==========================================
public:
	/**
	 * 播放脚步声（供 AnimNotify 或其他系统调用）
	 * 【2026-06-15 重构】: 逻辑已迁移到 FootstepComponent::PlayFootstep
	 * @param Location 播放位置
	 */
	UFUNCTION(BlueprintCallable, Category = "Audio")
	void PlayFootstepSound(FVector Location);

protected:

	// ==========================================
	// 助攻追踪系统
	// ==========================================
public:
	/**
	 * 助攻判定时间窗口（秒）
	 * 玩家在 N 秒内造成过伤害但在击杀前死亡，可以算作助攻
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stats|Assist")
	float AssistTimeWindow = 5.0f;

	/**
	 * 最后一次攻击目标的时间戳（用于判定助攻）
	 * Key = 目标 Actor 弱引用, Value = 时间戳
	 */
	TMap<TWeakObjectPtr<AActor>, float> LastHitTimestamps;

	/**
	 * 检查是否可以给予助攻
	 * @param PotentialAssistant 潜在助攻者
	 * @return 是否符合助攻条件
	 */
	bool CanGrantAssist(AActor* PotentialAssistant) const;

	/**
	 * 授予符合条件的玩家助攻得分
	 * @param Victim 受害者
	 * @param Killer 击杀者
	 */
	static void GrantAssistsToEligiblePlayers(ABaseCharacter* Victim, ABaseCharacter* Killer);

	/**
	 * 当受到伤害时，通知可能的助攻者（供武器系统调用）
	 * @param Victim 受害者
	 */
	UFUNCTION(BlueprintCallable, Category = "Stats|Assist")
	void NotifyDamageDealtTo(AActor* Victim);

protected:

	// ==========================================
	// 生命/能量回复系统
	// ==========================================
protected:
	/**
	 * 【2026-07-01 P0 重构】停止移动后开始回复的时间（秒）
	 *
	 * 架构决策: 本游戏的战斗模式不允许自动回血 (格斗游戏设计原则)
	 *   - 被攻击后血量应该保持不变, 直到下次治疗或死亡
	 *   - 能量 (AC/ACE) 也不自动回复, 由连击/技能系统产生
	 *
	 * 保留此接口的原因:
	 *   - 蓝图子类可能扩展 (例如 Boss 战的护盾回复)
	 *   - 测试模式可能临时开启 (GameMode 钩子)
	 *
	 * 当前默认值: 关闭 (HealthRegenRate=0 / EnergyRegenRate=0)
	 *   如果未来需要开启, 直接修改默认值即可
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stats|Regeneration")
	float RegenerationDelay = 2.0f;

	/**
	 * 【2026-07-01 P0 重构】生命回复速度（每秒）
	 *
	 * 默认值 0.0: 关闭自动回血 (格斗游戏设计)
	 * 设为 > 0 才开启自动回血
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stats|Regeneration", meta = (ClampMin = "0.0", ClampMax = "100.0"))
	float HealthRegenRate = 0.0f;

	/**
	 * 【2026-07-01 P0 重构】能量回复速度（每秒）
	 *
	 * 默认值 0.0: 关闭自动回能量
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stats|Regeneration", meta = (ClampMin = "0.0", ClampMax = "100.0"))
	float EnergyRegenRate = 0.0f;

	// 注: LastMoveTime / bIsRegenerating / StartRegeneration / StopRegeneration 已于 2026-07-01
	//     抽离到 UHealthRegenComponent, 此处不再保留字段/方法, 避免双轨不一致
	//     访问请通过 HealthRegenComponent 组件

	/**
	 * 【2026-06-15 重构】: 死亡状态锁 (防止鞭尸) 委托给 HealthComponent
	 * 原字段已删除,这里保留声明仅为兼容(无字段后置)
	 * 实际语义: HealthComponent->bIsDead 仍会 Replicated
	 */
	// 注: 原 UPROPERTY(Replicated) bool bIsDead 已下沉到 HealthComponent

	/**
	 * 记录最后的击杀方式（用于 HUD 击杀信息显示）
	 */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Stats")
	EKillMethod LastKillMethod = EKillMethod::MeleeWeapon;

	/**
	 * 死亡动画蒙太奇
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat")
	UAnimMontage* DeathMontage;

	/**
	 * 死亡逻辑（服务器端）
	 * 触发播放死亡动画 + 启动布娃娃 + 启动溶解
	 */
	void Die();

	/**
	 * 【2026-07-01 新增】HealthComponent 死亡事件回调
	 * 服务器: 走击杀结算 + Multicast_Die + StartRespawnTimer (经由 Die())
	 * 客户端: 本地执行死亡流程 (经由 ExecuteDeathLocal())
	 */
	UFUNCTION()
	void OnHealthComponentDeath();

	/**
	 * 【2026-07-01 新增】本地执行死亡流程
	 * 客户端通过 HealthComponent->OnRep_bIsDead 触发, 无需 Multicast RPC
	 * 服务器 Die() 内部也复用此方法
	 */
	void ExecuteDeathLocal();

	/**
	 * 【2026-07-01 新增】死亡序列幂等标志
	 * 服务器和客户端可能通过多个路径触发死亡流程:
	 *   - 服务器: HealthComponent::ApplyDamage → OnDeath.Broadcast → OnHealthComponentDeath → Die() → Multicast_Die (服务器自己)
	 *   - 客户端: HealthComponent::OnRep_bIsDead → OnDeath.Broadcast → OnHealthComponentDeath → ExecuteDeathLocal()
	 *            + Multicast_Die RPC → ExecuteDeathLocal()
	 * 用 bDeathSequenceStarted 保证 ExecuteDeathLocal 核心步骤只执行一次
	 */
	UPROPERTY(Transient)
	bool bDeathSequenceStarted = false;

	/**
	 * 启用布娃娃物理（角色真实死亡时调用）
	 */
	void EnableRagdoll();

	/**
	 * 【2026-07-01 新增】武器死亡处理（统一入口）
	 * @param Weapon  要处理的武器 (调用前会拷贝 CurrentWeapon, 内部置空已由调用方处理)
	 * 职责:
	 *   - Detach 武器与角色骨骼
	 *   - 激活武器 Mesh 物理模拟
	 *   - 调用 DissolveComponent 收集武器材质 (修复 FindComponentByClass 找不到的 bug)
	 *   - 仅服务器调用 Weapon->SetLifeSpan() 实现单一销毁权威
	 */
	void DropAndFadeWeapon(ABaseWeapon* Weapon);

	/**
	 * 布娃娃启用定时器句柄
	 */
	FTimerHandle RagdollTimerHandle;

	/**
	 * 角色图标刷新延迟重试定时器
	 */
	FTimerHandle CharacterIconRefreshTimerHandle;

	/**
	 * 【P0 修复 2026-06-29】角色图标重试计数器 (与 HUD 重试计数器配对, 防止日志洪水)
	 */
	int32 CurrentIconRetryCount = 0;

	/**
	 * HUD 刷新延迟重试定时器
	 */
	FTimerHandle HUDRefreshTimerHandle;

	/**
	 * 【P0 修复 2026-06-29】HUD 重试计数器
	 * 用途: 防止 HUD 永远拿不到时 RetryRefreshHUD 进入死循环刷屏
	 * 上限: 20 次 (10 秒后停止)
	 * 重置: HUD 成功获取 / EndPlay / 主动 ClearTimer 时清零
	 */
	int32 CurrentHUDRetryCount = 0;

	/**
	 * 【2026-07-01 重构】: 武器延迟销毁时间 (秒)
	 * 原 WeaponDestroyTimerHandle 是用于服务器/客户端各自手动调度 Destroy 的,
	 *   导致重复销毁语义混乱。现改为 SetLifeSpan, 由 UE 单一权威自动销毁。
	 * 默认 3.0 秒: 玩家能看到武器掉地物理动画 + 溶解特效, 同时小于 RespawnDelaySeconds 保证重生前清理完毕
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Death", meta = (ClampMin = "0.5", ClampMax = "30.0"))
	float WeaponDestroyDelaySeconds = 3.0f;

	/**
	 * 缓存角色 ID，延迟刷新时使用（避免循环触发服务器 RPC）
	 */
	FString CachedCharacterIDForIcon;

	/**
	 * 网络多播死亡（让所有玩家都看到你变成布娃娃）
	 */
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_Die();

	/**
	 * 复活延迟时间（秒），死亡时传递给 PlayerController 启动定时器
	 *
	 * 【2026-07-01 P0 调整】必须大于角色溶解完成时间
	 * 溶解时长 = 1.0 / DissolveSpeed 秒 (DissolveSpeed=1.5 → ~0.67s)
	 * 默认 3.0s: 给溶解 + 死亡动画 + 武器掉地留足视觉时间
	 * 如果 DissolveSpeed 调小 (溶解变慢), 需同步调大 RespawnDelaySeconds
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Respawn", meta = (ClampMin = "1.0", ClampMax = "30.0"))
	float RespawnDelaySeconds = 3.0f;


	// ==========================================
	// 击杀奖励系统
	// ==========================================
public:
	/**
	 * 击杀奖励接口
	 * @param KilledCharacter 被击杀的角色
	 */
	UFUNCTION(BlueprintCallable, Category = "Stats")
	void OnKill(ABaseCharacter* KilledCharacter);

	/**
	 * 击杀增加的血量
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stats|KillReward")
	float HealthRewardPerKill = 10.0f;

	/**
	 * 击杀增加的能量
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stats|KillReward")
	float EnergyRewardPerKill = 25.0f;


	// ==========================================
	// UI连接 【2026-07-01 重构 v2: 事件总线模式】
	// ==========================================
	// 历史 (2026-06-15): BaseCharacter → GameHUDWidget 直接 Push
	// 现状 (2026-07-01): BaseCharacter → CharacterEvents → GameHUDWidget (依赖倒置)
	// 说明:
	//   - GameHUDWidget / TryResolveHUDWidget / RefreshHUDFromCurrentState
	//     保留仅用于内部重试/向后兼容，不再被新代码使用
	//   - 所有新事件通过 CharacterEvents 组件广播，由 GameHUDWidget 订阅
	// ==========================================
protected:
	/**
	 * 【内部使用】游戏 HUD 引用
	 * 用途: 仅用于 RetryRefreshHUD / RetryRefreshCharacterIcon 等重试逻辑
	 * 新代码请勿直接访问，通过 CharacterEvents 事件订阅机制
	 */
	UPROPERTY()
	UGameHUDWidget* GameHUDWidget;

	/**
	 * 【仅内部使用】从 PlayerController 安全获取 GameHUDWidget 引用
	 * @note 仅用于延迟刷新重试 (RetryRefreshHUD / RetryRefreshCharacterIcon)
	 *       新代码通过 CharacterEvents 事件总线订阅，不应调用此方法
	 */
	bool TryResolveHUDWidget(bool bLogWarning = true);

	/**
	 * 【仅内部使用】一次性推送所有角色状态到 HUD (向后兼容)
	 * @note 仅用于向后兼容，不再被新代码调用
	 */
	void RefreshHUDFromCurrentState();

	// ==========================================
	// 事件广播方法 (替代直接 HUD Push)
	// ==========================================
	/**
	 * 【2026-07-01 新增】广播头像加载事件
	 * 调用时机: GetCharacterAvatarFromTable 加载完毕后
	 * 注意: 内部已包含 IsLocallyControlled() 检查，只在本地玩家调用
	 */
	void BroadcastCharacterIconReady(const FString& CharID, UTexture2D* Icon);

	/**
	 * 【2026-07-01 新增】广播 AC 值变化事件
	 * 调用时机: OnRep_ACValue 中
	 */
	void BroadcastACValueChanged(int32 NewAC);

	/**
	 * 【2026-07-01 新增】广播 ACE 值变化事件 (含排名)
	 * 调用时机: RefreshACEWithRank 中
	 */
	void BroadcastACEWithRankChanged(int32 NewACE, EACERankType RankType);

	/**
	 * 【2026-07-01 新增】广播武器图标加载完毕事件
	 * 调用时机: RefreshWeaponIconOnHUD 中
	 */
	void BroadcastWeaponIconReady(const FString& WeaponID, UTexture2D* Icon);


	// ==========================================
	// 3. 增强输入系统 (UE5 标配)
	// ==========================================
protected:
	/**
	 * 默认输入映射上下文
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	UInputMappingContext* DefaultMappingContext;

	/**
	 * 输入动作: 移动 (WASD)
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	UInputAction* MoveAction;

	/**
	 * 输入动作: 视角 (鼠标)
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	UInputAction* LookAction;

	/**
	 * 输入动作: 跳跃 (Space)
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	UInputAction* JumpAction;

	/**
	 * 输入动作: 下蹲
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	UInputAction* CrouchAction;

	/**
	 * 输入动作: 轻击
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	UInputAction* LightAttackAction;

	/**
	 * 输入动作: 重击
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	UInputAction* HeavyAttackAction;

	/**
	 * 输入动作: 释放技能
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	UInputAction* UseSkillAction;

	/**
	 * 基础输入回调: 移动
	 */
	void Move(const FInputActionValue& Value);

	/**
	 * 基础输入回调: 视角
	 */
	void Look(const FInputActionValue& Value);

	/**
	 * 下蹲回调: 开始下蹲
	 */
	void StartCrouch();

	/**
	 * 下蹲回调: 停止下蹲
	 */
	void StopCrouch();


	// ==========================================
	// 4. 武器与自动连斩系统核心变量
	// ==========================================
protected:
	/**
	 * 角色手里当前拿着的武器
	 * Replicated: 武器指针会通过网络复制到所有客户端
	 * ReplicateUsing: 客户端需要用 OnRep_CurrentWeapon 同步生成武器
	 */
	UPROPERTY(Replicated, VisibleAnywhere, BlueprintReadOnly, Category = "Combat", meta = (ReplicateUsing = "OnRep_CurrentWeapon"))
	class ABaseWeapon* CurrentWeapon;

	/**
	 * 武器指针改变时的回调（网络复制通知，客户端需要用这个来同步生成武器）
	 */
	UFUNCTION()
	void OnRep_CurrentWeapon(class ABaseWeapon* OldWeapon);

	/**
	 * 武器挂载配置数据表
	 * 用途: 配置不同角色+武器的挂载点、偏移等
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Attachment")
	class UDataTable* WeaponAttachmentDataTable;

	/**
	 * 当前角色 ID（用于查找挂载配置）
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat|Attachment")
	FString CharacterID;

	/**
	 * 【P0 大厂架构 2026.07.03 19:35】Spawn 时刻预定的武器 ID
	 *
	 * 设计: AI 跟玩家走完全相同的武器 Spawn 链路 (SpawnAndEquipWeapon),
	 *       但 AI 没有 PlayerState, 不能像玩家那样从 PS 读 SelectionWeapon1.
	 *
	 * 写入时机:
	 *   - 服务器 SpawnAIInternal 后 (Possess 之前):
	 *       AIPawn->SpawnWeaponID = Profile.WeaponID (从数据驱动配置来)
	 *   - 已存在的玩家不变 (玩家依然从 PS 读)
	 *
	 * 读取时机:
	 *   - PossessedBy 中, 优先读 SpawnWeaponID, 找不到再走 PS 路径
	 *
	 * 安全: 玩家流程不碰这字段, 互不干扰
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Combat|Attachment")
	FString SpawnWeaponID;

public:
	/**
	 * 【P0 大厂封装 2026.07.03】统一的武器/角色预填入口
	 *
	 * 为什么用 setter 而不是直接写 public 字段:
	 *   - 字段访问控制: protected 让外部需要 friend 或 public 函数
	 *   - setter 可以在内部加日志/校验 (例如非空检查/格式规范化)
	 *   - 蓝图也能调, 编辑器友好
	 *
	 * 调用方:
	 *   - ARoomGameMode::SpawnAIInternal (AI 路径)
	 *   - AMeleeAIController::SetupMeleeAI (关卡预放 AI 路径)
	 *
	 * 【P0 大厂架构重构 2026.07.06】:
	 *   - 原实现: 仅在 !InXXXID.IsEmpty() 时写入, 导致 Profile.WeaponID=""
	 *     时直接跳过, SpawnWeaponID 永远为空
	 *   - 新实现: 无论是否为空都写入, 确保外部覆盖意图被尊重
	 *   - 大厂原则: 显式赋值优于隐式跳过, 日志清晰可追溯
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat|Attachment")
	void SetSpawnLoadout(const FString& InCharacterID, const FString& InWeaponID)
	{
		UE_LOG(LogTemp, Log,
			TEXT("[BaseCharacter] SetSpawnLoadout: InCharID='%s', InWeaponID='%s', Old CharID='%s', Old WeaponID='%s'"),
			*InCharacterID, *InWeaponID, *CharacterID, *SpawnWeaponID);

		// 【P0 修复 2026.07.06】: 无论是否为空都写入, 不再跳过空字符串
		// 原因: Profile.WeaponID="" 时应显式清空, 而非保留旧值
		CharacterID = InCharacterID;
		SpawnWeaponID = InWeaponID;
	}

	/** 只读 getter, 给 AI Controller 用 */
	UFUNCTION(BlueprintPure, Category = "Combat|Attachment")
	const FString& GetSpawnWeaponID() const { return SpawnWeaponID; }

protected:

	/**
	 * 全局攻击锁: 正在挥刀吗？
	 */
	bool bIsAttacking;

	/**
	 * 绿灯亮起: 现在处于输入区间内吗？（连招窗口）
	 */
	bool bCanReceiveInput;

	/**
	 * 缓存区: 玩家在这个区间内点击过鼠标吗？
	 */
	bool bSaveAttack;

	/**
	 * 按住状态: 玩家是一直死死按着左键没松吗？
	 */
	bool bIsHoldingLightAttack;

	/**
	 * 当前连招段数 (1 或 2)
	 */
	int32 ComboIndex;

	/**
	 * 根据角色 ID 和武器 ID 查找挂载配置
	 */
	struct FWeaponAttachmentConfig* FindWeaponAttachmentConfig(const FString& InCharacterID, const FString& InWeaponID) const;

	/**
	 * 连招核心执行逻辑
	 */
	void ExecuteComboSequence();

	/**
	 * 轻击按下事件
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat")
	void LightAttack_Pressed();

	/**
	 * 轻击松开事件
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat")
	void LightAttack_Released();

	/**
	 * 重击事件（单击）
	 */
	void HeavyAttack();

	/**
	 * 释放技能事件
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat")
	void UseSkill();


	// ==========================================
	// 5. 动画通知 (Anim Notify) 专用接口
	// ==========================================
public:
	/**
	 * 供动画蓝图区间 (Anim Notify State) 调用的接口
	 * 区间开始时触发（开启连招窗口）
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat")
	void EnableComboWindow();

	/**
	 * 区间结束时触发（检查缓存区是否有缓存攻击）
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat")
	void CheckCombo();

	/**
	 * 动画彻底结束时触发
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat")
	void EndAttackState();

	/**
	 * 【P0 大厂架构修复 2026.07.06】AI 专用轻攻击 — 不复用玩家连击状态机
	 *
	 * 根因分析:
	 *   旧路径 OnAIRequestAttack -> LightAttack_Pressed -> 设置 bIsMovementLocked + MaxWalkSpeed=0
	 *   -> EndAttackState (由动画蓝图 NotifyEnd 调用) -> 清除锁
	 *   问题: AI 攻击不走动画蓝图的 NotifyEnd 回调链, EndAttackState 从未被调用
	 *   结果: bIsMovementLocked 永远为 true, AI 永远无法移动 (用户原话: "原地攻击")
	 *
	 * 修复方案:
	 *   AI 专用攻击路径, 完全独立于玩家连击状态机:
	 *   - 不设置 bIsMovementLocked (AI 攻击时不锁定移动)
	 *   - 不设置 bIsAttacking (不触发连击状态)
	 *   - 直接播放攻击动画 + Server RPC (与玩家攻击视觉一致)
	 *   - AI 持续追逐 + 持续尝试攻击, 攻击动画播放期间也能移动
	 *
	 * 架构优势:
	 *   - 与玩家逻辑完全解耦: 玩家改连击系统不影响 AI
	 *   - 攻击动画播放期间 AI 仍能移动: 不会出现"原地挥刀"
	 *   - 代码最小变更: 只加一个函数, 不改现有玩家逻辑
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat|AI")
	bool OnAIRequestAttack_Simple();


	// ==========================================
	// 6. 战斗 RPC (网络动作同步)
	// ==========================================
protected:
	/**
	 * 客户端向服务器请求挥刀（带着连击序号，告诉大家播第几个动作）
	 */
	UFUNCTION(Server, Reliable, WithValidation)
	void Server_PlayAttackAnim(bool bIsHeavy, int32 InComboIndex);

	/**
	 * 服务器向所有客户端广播: "大家都播放这个人的挥刀动画！"
	 */
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_PlayAttackAnim(bool bIsHeavy, int32 InComboIndex);

	/**
	 * 【P0 大厂架构 2026.07.06 重构】AI 攻击伤害上报（服务器权威）
	 *
	 * 背景: 旧版 AI 攻击只播动画 (OnAIRequestAttack_Simple) → 引擎回调
	 *       → 玩家/AI 都没扣血, 因为:
	 *         (a) AI 攻击路径没调 StartWeaponTrace (没人触发武器刀刃扫掠)
	 *         (b) ConfigSO.Damage 字段从未被读 (死数据)
	 *         (c) BaseWeapon.LightDamageBody 也未生效 (Trace 没开)
	 *
	 * 修复: AI 攻击时, 由 BaseCharacter 自己向服务器上报命中
	 *       伤害值 = AIConfig.Damage (带难度缩放)
	 *       流程: OnAIRequestAttack_Simple → PlayAnimMontage → 等待蒙太奇 NotifyBegin
	 *           → Server_ReportAIAttackHit (服务器) → ApplyPointDamage → 扣血
	 *
	 * 为什么不复用 BaseWeapon::Server_ReportHit?
	 *   - 武器 Server_ReportHit 接收 bIsHeavy + BoneName 后用武器字段自己算伤害
	 *   - AI 通道要的是 ConfigSO 控制 (不是武器控制), 走武器会被 LightDamageBody 覆盖
	 *   - 两条通道独立: 玩家按武器, AI 按 Config, 互不干扰
	 *
	 * @param HitActor  受击目标 (服务器校验: 必须是 ABaseCharacter 且未死)
	 * @param Damage    伤害值 (服务器还会再校验范围, 防作弊)
	 */
	UFUNCTION(Server, Reliable, WithValidation)
	void Server_ReportAIAttackHit(AActor* HitActor, float Damage);


	// ==========================================
	// 【P0 2026.07.06 大厂架构重构】AI 攻击蒙太奇事件驱动
	// ==========================================
public:
	/**
	 * AI 攻击蒙太奇结束回调（事件驱动, 不依赖 BT 距离检查）
	 *
	 * 【大厂架构 - 事件驱动 vs 轮询驱动】
	 *   旧设计 (轮询):
	 *     - BT TickTask 每帧检查距离, 距离 > AttackRange 就 StopMontage 打断
	 *     - 玩家在蒙太奇播放期间走开 → AI 被打断 → Combo1 永远播不完
	 *     - 严重违反"用户期望: 攻击必须播放完整 Combo1 段"
	 *
	 *   新设计 (事件驱动):
	 *     - OnAIRequestAttack_Simple 启动蒙太奇时, 绑定 UAnimInstance::OnMontageEnded
	 *     - Combo1 自然播放完成 → 引擎回调 → OnAIAttackMontageEnded
	 *     - 收到回调后再通知 AIController 攻击真正结束 → BT 进入冷却
	 *
	 * 优势:
	 *   - 保证 Combo1 段完整播放 (符合用户要求)
	 *   - 不再依赖 BT 距离检查决定动画生命周期
	 *   - 即使玩家跑远, 蒙太奇也播完 (AI 攻击看起来"认真", 不是立刻放弃)
	 *   - 蒙太奇结束后才进入冷却, 状态机清晰
	 *
	 * @param Montage        结束的蒙太奇指针 (UAnimInstance::OnMontageEnded 必须的签名参数)
	 * @param bInterrupted   true = 被其他动画/StopMontage 打断; false = 自然播完
	 *
	 * 【关键】必须是 UFUNCTION + 签名 (UAnimMontage*, bool)
	 *        因为我们要绑定到 UAnimInstance::OnMontageEnded (DECLARE_DYNAMIC_MULTICAST_DELEGATE)
	 *        该委托要求 UFUNCTION 才能 AddDynamic
	 */
	UFUNCTION()
	void OnAIAttackMontageEnded(UAnimMontage* Montage, bool bInterrupted);

protected:
	/**
	 * 【P0 大厂架构 2026.07.06 19:25】上次 AI 攻击的时间戳 (秒, FPlatformTime::Seconds)
	 *
	 * 用途: OnAIRequestAttack_Simple 入口的本地节流兜底
	 *       即使 BTTask 的 bHasAttackToken + Cooldown Timer 全部失效,
	 *       Character 自己手里还有这道墙, 防止 AI 持续连击到玩家
	 *
	 * 设计要点:
	 *   - 默认 0.0 = 从未攻击过 (第一次永远放行)
	 *   - 用 C++ 成员而不是 BB Key: 不依赖 BT/BP, 跨 BT 重启保持
	 *   - 跟 bIsCurrentlyAttacking 互补: 一个管"动作状态", 一个管"节流时间"
	 */
	double LastAIAttackTimeSeconds = 0.0;

	/**
	 * 上次 AI 攻击请求时播放的蒙太奇指针
	 * 用于在 OnMontageEnded 回调时验证 Montage 是否"我们自己触发的"
	 * (UAnimInstance::OnMontageEnded 是 multicast, 所有蒙太奇结束都会广播一次)
	 * 由 UPROPERTY 持有, 防止 GC 回收导致野指针
	 */
	UPROPERTY()
	TObjectPtr<UAnimMontage> CachedAIMontage = nullptr;

	/**
	 * 是否在等待 AI 攻击蒙太奇回调
	 * 设计: 每次启动新攻击时设 true, 收到正确 Montage 的回调后 false
	 *       超时机制: TickChaseFallback 通过 SetCurrentlyAttacking 5s 兜底
	 * 用 bool 比再创建 FName 标识简单 — 单 AI 单任务
	 */
	UPROPERTY()
	bool bIsWaitingForAIMontageCallback = false;


	// ==========================================
	// 【P0 2026.07.07 大厂架构重构】AI/玩家攻击伤害来源标识
	// ==========================================
	// 背景: AI 攻击要走"trace 命中"而不是"动画开始瞬间扣血",
	//       这是用户原话 — "AI 攻击扣血时机应该像玩家一样用武器射线"
	//
	// 大厂设计:
	//   玩家路径 (LightAttack_Pressed → 蒙太奇 → AnimNotify → StartWeaponTrace):
	//     - 不设此标志, 默认 false
	//     - 武器 Tick 命中 → Server_ReportHit → 用 LightDamageBody/Head (玩家字段)
	//
	//   AI 路径 (OnAIRequestAttack_Simple → 蒙太奇 → 同样的 AnimNotify → StartWeaponTrace):
	//     - 攻击发起时 SetAttackerIsAI(true)
	//     - 武器 Tick 命中 → Server_ReportHit → 用 ConfigSO.Damage (AI 字段)
	//     - 蒙太奇结束 → SetAttackerIsAI(false)
	//
	// 关键: 玩家和 AI **完全共用同一套 AnimNotify + trace + Server_ReportHit 代码路径**
	//       只在伤害来源决策上根据 bIsCurrentlyAttackerAI 切换, 单一职责
	//
	// 为什么不放在 BaseWeapon 上:
	//   - BaseWeapon 是无状态组件 (给玩家/AI 都装同一把刀)
	//   - "这把刀正在被 AI 用还是玩家用" 不是武器的属性, 是攻击者的属性
	//   - 因此属于 ABaseCharacter 的状态, 武器 Tick 通过 GetOwner()->bIsCurrentlyAttackerAI 读
	// ============================================================
public:
	/**
	 * 【P0 2026.07.07 大厂架构重构】设置攻击者是否 AI
	 *
	 * 调用方:
	 *   - 玩家 LightAttack_Pressed: 不需要调, 默认 false
	 *   - AI OnAIRequestAttack_Simple: 攻击发起时设 true, 蒙太奇结束回调 false
	 *
	 * 用途: 让 BaseWeapon::Tick 在 trace 命中时知道应该用 ConfigSO.Damage 还是 LightDamageBody
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat|AI")
	void SetAttackerIsAI(bool bIsAI) { bIsCurrentlyAttackerAI = bIsAI; }

	/**
	 * 攻击者是否为 AI (供 BaseWeapon::Tick 读取)
	 */
	UFUNCTION(BlueprintPure, Category = "Combat|AI")
	bool IsAttackerAI() const { return bIsCurrentlyAttackerAI; }

	/** 【P0 2026.07.07】攻击者是否 AI (公开读, 武器 Tick 用) */
	UPROPERTY(VisibleInstanceOnly, BlueprintReadOnly, Category = "Combat|AI")
	bool bIsCurrentlyAttackerAI = false;


	// ==========================================
	// 供上帝 (GameMode) 调用的装备武器接口
	// ==========================================
public:
	/**
	 * 装备武器（GameMode 调用）
	 * @param WeaponClassToEquip 要装备的武器蓝图类
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat")
	void EquipWeapon(TSubclassOf<ABaseWeapon> WeaponClassToEquip);


	/**
	 * 【P0 大厂架构 2026.07.06】武器生成请求入口（对外暴露）
	 *
	 * 设计: SpawnAndEquipWeapon 是 protected (仅内部逻辑使用).
	 *       但关卡预放 AI 需要在 Controller 侧主动触发武器生成.
	 *       所以对外暴露一个 public 包装, 让 AMeleeAIController::SetupMeleeAI 可以调用.
	 *
	 * 调用方: AMeleeAIController::SetupMeleeAI (OnPossess 末尾手动触发武器生成)
	 *
	 * 内部直接调 SpawnAndEquipWeapon, 复用完整链路 (查表/挂载/HUD).
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat|AI")
	void RequestWeaponSpawn(FString WeaponID);


	// ==========================================
	// 公开获取当前武器的接口，给动画系统用
	// ==========================================
	/**
	 * 获取当前装备的武器
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat")
	class ABaseWeapon* GetCurrentWeapon() const { return CurrentWeapon; }

protected:
	// ==========================================
	// 3A 级摄像机减震系统
	// ==========================================

	/**
	 * 重写虚幻原生自带的下蹲回调（用于平滑相机高度）
	 */
	virtual void OnStartCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust) override;

	/**
	 * 重写虚幻原生自带的起立回调
	 */
	virtual void OnEndCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust) override;

	/**
	 * 重写 PossessedBy（AI/Player 控制器附身时调用）
	 */
	void PossessedBy(AController* NewController);

	/**
	 * 根据 WeaponID 查表并生成+装备武器
	 */
	void SpawnAndEquipWeapon(FString WeaponID);

	/**
	 * 摄像机平滑过渡的速度 (越大越快，越小越像慢动作)
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Camera")
	float CrouchCameraSmoothSpeed = 12.0f;


	// ==========================================
	// AC/ACE系统 (类似CS中的得分系统)
	// ==========================================
public:
	/**
	 * 最大 AC 值（蓝图可配置，默认100）
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stats|AC", meta = (ClampMin = "1"))
	int32 MaxAC = 100;

	/**
	 * 当前 AC 值（网络同步）
	 */
	UPROPERTY(ReplicatedUsing = OnRep_ACValue, BlueprintReadOnly, Category = "Stats|AC")
	int32 ACValue;

	/**
	 * ACE 值（连续获胜回合数，网络同步）
	 */
	UPROPERTY(ReplicatedUsing = OnRep_ACEValue, BlueprintReadOnly, Category = "Stats|AC")
	int32 ACEValue;

	/**
	 * 增加 AC 值
	 * @param Amount 要增加的 AC
	 */
	UFUNCTION(BlueprintCallable, Category = "Stats|AC")
	void AddAC(int32 Amount);

	/**
	 * 扣除 AC 值（用于结算时扣分）
	 * @param Amount 要扣除的 AC
	 */
	UFUNCTION(BlueprintCallable, Category = "Stats|AC")
	void TakeAC(int32 Amount);

	/**
	 * 重置 AC 到 0（同时重置 ACE）
	 */
	UFUNCTION(BlueprintCallable, Category = "Stats|AC")
	void ResetAC();

	/**
	 * 获取当前 AC 值
	 */
	UFUNCTION(BlueprintCallable, Category = "Stats|AC")
	int32 GetAC() const { return ACValue; }

	/**
	 * 获取最大 AC 值
	 */
	UFUNCTION(BlueprintCallable, Category = "Stats|AC")
	int32 GetMaxAC() const { return MaxAC; }

	/**
	 * 获取 AC 百分比 (0.0~1.0)
	 */
	UFUNCTION(BlueprintCallable, Category = "Stats|AC")
	float GetACPercent() const { return MaxAC > 0 ? static_cast<float>(ACValue) / static_cast<float>(MaxAC) : 0.0f; }

	/**
	 * 获取 ACE 值
	 */
	UFUNCTION(BlueprintCallable, Category = "Stats|AC")
	int32 GetACE() const { return ACEValue; }

	/**
	 * 根据当前 PlayerState 的 SelectedCharacterID，从 CharacterDataTable 查出头像并刷新 HUD
	 */
	UFUNCTION(BlueprintCallable, Category = "PlayerStatus")
	void RefreshCharacterIcon();

	/**
	 * 查询当前 ACE 排名并刷新 HUD 上的 ACE 文字颜色
	 */
	UFUNCTION(BlueprintCallable, Category = "PlayerStatus")
	void RefreshACEWithRank();


	// ==========================================
	// 角色图标网络同步 (Client RPC)
	// ==========================================
protected:
	/**
	 * 服务器调用，通知所属客户端刷新自己的头像图标
	 * @param InCharacterID 角色 ID
	 * @param Avatar        头像贴图（服务器查表后直接传递，解决客户端 GetAuthGameMode 为 nullptr 的问题）
	 */
	UFUNCTION(Client, Reliable)
	void Client_RefreshCharacterIcon(const FString& InCharacterID, class UTexture2D* Avatar);

	/**
	 * HUD 未就绪时的延迟重试回调
	 */
	void RetryRefreshCharacterIcon();

	/**
	 * 刷新武器图标到 HUD（从 PlayerSpawnDataCache 读取 WeaponID）
	 */
	void RefreshWeaponIconOnHUD();


	// ==========================================
	// 计分板系统 (Scoreboard)
	// ==========================================
public:
	/**
	 * 服务器广播击杀信息给所有客户端（用于更新计分板）
	 */
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_NotifyKill(AActor* VictimActor, AActor* AssistantActor);

	/**
	 * 获取击杀者 PlayerState
	 */
	UFUNCTION(BlueprintCallable, Category = "Scoreboard")
	class ARoomPlayerState* GetRoomPlayerState() const;

private:
	/**
	 * HUD 还未就绪时延迟刷新的辅助方法
	 */
	void RetryRefreshHUD();

protected:
	/**
	 * 从 CharacterDataTable 查指定角色 ID 的头像贴图
	 */
	class UTexture2D* GetCharacterAvatarFromTable(const FString& CharID);

protected:
	/**
	 * AC 值改变时的客户端回调
	 */
	UFUNCTION()
	void OnRep_ACValue();

	/**
	 * ACE 值改变时的客户端回调
	 */
	UFUNCTION()
	void OnRep_ACEValue();
};
