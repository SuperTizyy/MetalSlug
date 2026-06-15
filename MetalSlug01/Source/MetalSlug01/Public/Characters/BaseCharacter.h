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
class METALSLUG01_API ABaseCharacter : public ACharacter
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
	// 阵营系统 (0=人类, 1=丧尸)
	// ==========================================

	/**
	 * 阵营 ID
	 * 0 = 攻方/人类
	 * 1 = 守方/丧尸
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Combat|Team")
	uint8 TeamID = 0; // 默认是人类

	/**
	 * 获取阵营 ID（供 AI 和 GameMode 调用）
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat|Team")
	uint8 GetTeamID() const { return TeamID; }

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
protected:
	// ===== 改造: 业务组件（健康/能量/溶解/脚步）已抽离为独立 Component =====
	// 设计: BaseCharacter 创建并持有 4 个业务 Component, 后续业务代码可逐步迁移
	// 行为兼容: 当前 BaseCharacter 内部仍维护同名字段, 两者并存; 后续批次将字段删除

	/** 健康管理 (血量/受伤/死亡事件) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components|Health")
	UHealthComponent* HealthComponent;

	/** 能量管理 (消耗/回复/百分比) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components|Energy")
	UEnergyComponent* EnergyComponent;

	/** 溶解特效 (死亡后材质渐隐) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components|Dissolve")
	UDissolveComponent* DissolveComponent;

	/** 脚步音效 (地面检测 + 音效播放) */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components|Footstep")
	UFootstepComponent* FootstepComponent;

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
	 * 停止移动后开始回复的时间（秒）
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stats|Regeneration")
	float RegenerationDelay = 2.0f;

	/**
	 * 生命回复速度（每秒）
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stats|Regeneration")
	float HealthRegenRate = 5.0f;

	/**
	 * 能量回复速度（每秒）
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Stats|Regeneration")
	float EnergyRegenRate = 10.0f;

	/**
	 * 最后移动时间
	 */
	float LastMoveTime;

	/**
	 * 是否正在回复
	 */
	bool bIsRegenerating;

	/**
	 * 开始回复（由 Tick 或定时器调用）
	 */
	void StartRegeneration();

	/**
	 * 停止回复（角色开始移动时调用）
	 */
	void StopRegeneration();

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
	 * 启用布娃娃物理（角色真实死亡时调用）
	 */
	void EnableRagdoll();

	/**
	 * 布娃娃启用定时器句柄
	 */
	FTimerHandle RagdollTimerHandle;

	/**
	 * 角色图标刷新延迟重试定时器
	 */
	FTimerHandle CharacterIconRefreshTimerHandle;

	/**
	 * HUD 刷新延迟重试定时器
	 */
	FTimerHandle HUDRefreshTimerHandle;

	/**
	 * 【2026-06-15 重构】: 死亡流程 - 武器延迟销毁定时器句柄
	 * 原代码为局部变量 (BaseCharacter.cpp L1457),会在栈帧结束时丢失句柄
	 * 改为成员变量,允许 EndPlay 或新一轮死亡时主动 ClearTimer
	 */
	FTimerHandle WeaponDestroyTimerHandle;

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
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Respawn")
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
	// UI连接
	// ==========================================
	// 【2026-06-15 重构】: 跨层依赖集中化
	// 历史: BaseCharacter.cpp 中曾有 50+ 处直接访问 GameHUDWidget / MyGameHUD
	// 现状: 收敛到 2 个 helper 函数 (RefreshHUDFromCurrentState / TryResolveHUDWidget)
	//      集中点位于 cpp 文件底部 "UI Bridge (跨层访问点)" 区段
	// 待优化: 长期应改为 UI 订阅 HealthComponent->OnHealthChanged 事件
	//        实现真正的依赖倒置 (UI→Core 单向), 当前阶段先收敛
	// ==========================================
protected:
	/**
	 * 游戏 HUD 引用
	 * 用途: 通过它刷新血条/能量/武器图标
	 * @note 外部代码请勿直接访问, 走 helper 函数
	 */
	UPROPERTY()
	UGameHUDWidget* GameHUDWidget;

	// === UI Bridge Helper (2026-06-15 新增) ===
	/**
	 * 【UI Bridge】从 PlayerController 安全获取 GameHUDWidget 引用
	 * @param bLogWarning 失败时是否打 Warning 日志 (默认 true)
	 * @return 是否成功解析 (true 表示 GameHUDWidget 非空)
	 * @note 替代散落在 30+ 处的 HUD 获取样板代码
	 */
	bool TryResolveHUDWidget(bool bLogWarning = true);

	/**
	 * 【UI Bridge】一次性推送所有角色状态到 HUD
	 * 推送内容: 血量/能量/AC/ACE
	 * @note 替代散落在 10+ 处的 UpdateXxx 调用
	 */
	void RefreshHUDFromCurrentState();


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
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat|Attachment")
	FString CharacterID;

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


	// ==========================================
	// 6. 战斗 RPC (网络动作同步)
	// ==========================================
protected:
	/**
	 * 客户端向服务器请求挥刀（带着连击序号，告诉大家播第几个动作）
	 */
	UFUNCTION(Server, Reliable)
	void Server_PlayAttackAnim(bool bIsHeavy, int32 InComboIndex);

	/**
	 * 服务器向所有客户端广播: "大家都播放这个人的挥刀动画！"
	 */
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_PlayAttackAnim(bool bIsHeavy, int32 InComboIndex);


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
	 */
	UFUNCTION(Client, Reliable)
	void Client_RefreshCharacterIcon(const FString& InCharacterID);

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
