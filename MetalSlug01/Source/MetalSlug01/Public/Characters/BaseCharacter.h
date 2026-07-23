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

// 【v93 大厂架构新增】UAnimMontage 完整类型 (MotherAttackMontage UPROPERTY 字段必须)
#include "Animation/AnimMontage.h"

// 【v93.2 大厂架构新增】UMeleeSwStrategy 完整类型 (MotherTraceStrategy UPROPERTY 字段必须)
class UMeleeSwStrategy;

// 【2026.07.12 P0 大厂重构 Phase 2】引入新增的 5 个业务 Component
// 拆分来源:
//   - PlayerComboComponent   ← BaseCharacter 玩家连击状态机
//   - AIAttackComponent       ← BaseCharacter AI 攻击子系统
//   - CombatDeathComponent    ← BaseCharacter 战斗死亡/无敌期
//   - WeaponAttachmentComponent ← BaseCharacter 武器装备/挂载
//   - CharacterIconComponent  ← BaseCharacter 角色头像/武器图标刷新
// 注: 这 5 个组件由 BaseCharacter 在构造函数中 CreateDefaultSubobject 创建,
//     BaseCharacter 上保留同名转发壳方法, BP/外部调用方无感知
//
// 【2026.07.12 P0 修复】避免循环 include:
//   旧版: BaseCharacter.h include Component.h → Component.h 不能 include BaseCharacter.h →
//         Component 调 BaseCharacter protected 方法报 C2248 (不完整类型 friend 无效)
//   新版: BaseCharacter.h 用前置声明, Component.h include BaseCharacter.h (完整定义)
//
// 大厂原则 - 友元完整定义:
//   friend class ABaseCharacter 只在 ABaseCharacter 是完整类型时才能生效,
//   所以 Component.h 必须看到 ABaseCharacter 的完整类定义
class UPlayerComboComponent;
class UAIAttackComponent;
class UCombatDeathComponent;
class UWeaponAttachmentComponent;
class UCharacterIconComponent;
// 【v39 新增】6 个老组件的前向声明 (字段声明需要)
class UHealthComponent;
class UEnergyComponent;
class UCharacterEvents;
class UDissolveComponent;
class UFootstepComponent;
class UHealthRegenComponent;
class UInvincibilityFlickerComponent;  // 【v40.8 新增】无敌期视觉闪烁

// 【v38 修复】模板函数 ResolveComponent<T> 调用 FindComponentByClass<T>(), 需要完整类型
// UE 的 FindComponentByClass 是模板, 内部涉及 StaticClass(), 必须看到完整类型定义
// 注: 这 5 个 .h 互相已经 friend, include 顺序不会形成循环
#include "Combat/PlayerComboComponent.h"
#include "Combat/AIAttackComponent.h"
#include "Combat/CombatDeathComponent.h"
#include "Combat/WeaponAttachmentComponent.h"
#include "Combat/CharacterIconComponent.h"

// 【v39 修复】新增 6 个老组件的完整 include (供 ResolveComponent<T> 模板调用 FindComponentByClass)
//   - 老的 6 个组件 (Health/Energy/CharacterEvents/Dissolve/Footstep/HealthRegen) v38 未保护
//   - v39 统一走 Resolve 模式, 必须看到完整类型
#include "Components/HealthComponent.h"
#include "Components/EnergyComponent.h"
#include "Components/CharacterEvents.h"
#include "Components/DissolveComponent.h"
#include "Components/FootstepComponent.h"
#include "Components/HealthRegenComponent.h"
#include "Combat/InvincibilityFlickerComponent.h"  // 【v40.8 新增】

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

// 【v54 大厂架构重构】UAIProfileAsset 已删除, 改用 UAIBehaviorConfigSO
class UAIBehaviorConfigSO;

// 【2026.07.11 P0】 SyncFactionTagFromController 需要 Cast<ABaseAIController>
//  前向声明避免 BaseCharacter.h 依赖 BaseAIController.h (减少编译依赖)
class ABaseAIController;
class ARoomPlayerState;

// 【v51 大厂架构 — 生化模式】武器槽位类型前置声明 (完整定义见 Weapons/WeaponSlotType.h)
// enum class EWeaponSlotType : uint8;
// 【v76 大厂架构 — 武器切换音效】UFUNCTION 参数需要完整 enum 定义, UHT 反射要求
//   旧版用前向声明, 但 UFUNCTION(NetMulticast) 反射枚举参数需要完整定义
//   → 改为直接 include (v76 已验证)
#include "Weapons/WeaponSlotType.h"

// 【v76 大厂架构 — 武器切换音效】USoundBase 前向声明 (Multicast RPC 签名需要)
// class USoundBase;

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

// 【2026.07.12 P0 重构】Component 持有 BaseCharacter 上的转发壳 (TryResolveHUDWidget / Server_PlayAttackAnim 等),
// 但 RPC/转发壳必须保留在 Actor 上 (UE 硬约束), 所以 Component 需要访问 BaseCharacter 的 protected 成员.
//
// 大厂原则 - 友元精确授权:
//   - friend 写在 被访问类 (这里是 ABaseCharacter) 里, 声明 谁 (这里是 UPlayerComboComponent 等) 可以访问 protected
//   - 比把所有方法改 public 更精确 (只对 5 个 Component 开放访问, 不对全 BP 暴露)
//   - 反向依赖安全: BaseCharacter.h 已改为前置声明 Component (不循环 include)
friend class UPlayerComboComponent;
friend class UAIAttackComponent;
friend class UCombatDeathComponent;
friend class UWeaponAttachmentComponent;
friend class UCharacterIconComponent;
// 【v93.2 大厂架构新增】母体 trace 命中 RPC 转发 — MeleeSwStrategy::TickDetection 命中时
//   调 OwnerChar->Server_ReportMotherAttackHit (protected RPC). 与 5 个业务 Component friend 对称.
friend class UMeleeSwStrategy;

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
	 * 【2026.07.10 P0 重构】阵营 GameplayTag — 单一真理源
	 *
	 * 大厂原则:
	 *   - 全工程阵营表达只用这一个 FGameplayTag 字段
	 *   - 有效值: Faction.Offense / Faction.Defense (由 FFactionTags 集中定义)
	 *   - 阵营是**通用身份**: 客户端玩家和 AI 都可以是 Offense 或 Defense
	 *     (阵营 ≠ 角色类型, 玩家/AI 由控制器决定, 与阵营正交)
	 *   - 模式决定哪方是攻/守: 走 ARoomGameMode::ModeRulesByMode[Mode].AttackTeamFaction
	 *   - 旧 Tag 校验: 旧 Faction.Player/Zombie/Enemy 标记 DEPRECATED,
	 *                  FFactionTags::ValidateFactionOrReportError 检测后 Log Error
	 *                  (大厂原则 - 零兜底, 不自动迁移)
	 *
	 * 删除历史 (2026.07.10):
	 *   - uint8 TeamID 字段 (冗余)
	 *   - uint8 GetTeamID() 派生接口 (冗余)
	 *   - ResolveGenericTeamIdFromTag 内部查表函数 (冗余, FactionTags.cpp 替代)
	 *   - MigrateFromLegacy 静默迁移 (大厂禁止 - 改为 ValidateFactionOrReportError)
	 *
	 * 数据流:
	 *   配置层: DA_AIBehaviorConfig_XXX.FactionTag (策划在 DataAsset 配)
	 *   运行时: ABaseCharacter::FactionTag (Controller 通过 SetGenericTeamId 写入)
	 *   判定层: FFactionTags::AttitudeBetween (AIPerception 自动调用)
	 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, ReplicatedUsing = OnRep_FactionTag,
	         Category = "Combat|Team", meta = (Categories = "Faction"))
	FGameplayTag FactionTag;

	/**
	 * 【2026.07.11 P0 修复】FactionTag 网络同步回调
	 *
	 * 触发场景: 服务器修改 Pawn.FactionTag 后, 客户端 OnRep_FactionTag 收到
	 * 为什么需要:
	 *   - UE AIPerception 调 GetTeamAttitudeTowards → 读 Pawn.FactionTag
	 *   - 如果没 Replicated, 客户端 Pawn 永远 FactionTag=空 → AI 看不到玩家 → 玩家永远不在 BT 视野里
	 *   - 旧版 v25 完全没处理这个同步, 客户端看到的 FactionTag 是 UE 反射初始化的默认值 (FGameplayTag::EmptyTag)
	 *
	 * 大厂原则: 真理源在 Pawn.FactionTag, Client 必须看到 Server 的真相
	 *
	 * 调用方: 引擎自动触发
	 */
	UFUNCTION()
	void OnRep_FactionTag();

	/**
	 * 【2026.07.11 P0 修复】FactionTag 从 PlayerState 同步
	 *
	 * Server-only (HasAuthority 检查由 PossessedBy 调用方负责)
	 *
	 * 角色:
	 *   - Player Controller 路径: PlayerState.CurrentFactionTag → Pawn.FactionTag (单一真理源同步)
	 *   - AI Controller 路径: 跳过 (AI 真理源在 AIController.CachedFactionTag)
	 *
	 * 大厂原则 - 零兜底: 缺 PlayerState → Error; 阵营为空 → Error
	 * 调用方: ABaseCharacter::PossessedBy (Server)
	 */
	void SyncFactionTagFromController(AController* InController);

	// 【P0 2026.07.10 大厂阵营重构】删除 GetTeamID()/ResolveGenericTeamIdFromTag()
	//   理由:
	//     1. uint8 GetTeamID() 是冗余派生 — FactionTag 已是源, 取整型 ID 无任何业务价值
	//     2. FGenericTeamId 是 UE 历史协议, 在 FGameplayTag 阵营体系下完全冗余
	//     3. 阵营判定统一走 FFactionTags::AttitudeBetween (大厂原则: 单一真理源)
	//   若旧 BP/DA 引用 GetTeamID, 编译期就会暴露 → 修复迁移即可, 不要回退

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

	// 【P0 2026.07.10】删除 ResolveGenericTeamIdFromTag — 详见上方注释

	// ==========================================
	// 【v93.1 大厂架构新增】母体变异状态 — 生化模式独有
	// ==========================================
	//
	// 大厂原则 — 单一真理源:
	//   - 真理源 = 服务器 URoomMotherMutationSubsystem::MutateCharacterToMother 写入
	//   - 客户端 OnRep_bIsMother 收到 → Broadcast 让 BT / UI 知道该角色已变异为母体
	//   - 镜像 v27 FactionTag: 没有 Replicated = 客户端永远是默认值 → 业务失效
	//
	// 大厂原则 — 双向标记 (互斥语义):
	//   - bIsMother = true  时, bIsHuman = false (强制一致, MutateCharacterToMother 写入时同步)
	//   - bIsHuman  便于 BP 蓝图分支 (策划可读)
	//   - 默认值: bIsHuman = true, bIsMother = false (出生时是正常人类)
	//
	// 业务层使用:
	//   - BT Decorator (母体不攻击人类 / 不需要识别玩家)
	//   - HUD (玩家知道自己已变异)
	//   - UI Scoreboard (母体显示特殊图标)
	//
	// 跨模式安全:
	//   - 刀战模式: bIsMother 永远是 false, 这两字段对刀战模式 0 影响
	//   - 生化模式: 服务器变异时写入, 客户端 OnRep 触发

	/**
	 * 是否为生化母体 — 母体变异后由服务器写入 true, 客户端 OnRep 收到
	 * @note 仅在 ERoomMatchMode::Zombie 模式下才有意义
	 */
	UPROPERTY(ReplicatedUsing = OnRep_bIsMother, BlueprintReadOnly, Category = "Combat|Mother")
	bool bIsMother = false;

	/**
	 * 是否为人类 — 镜像字段, 母体变异时由服务器同步设为 false
	 * @note 互斥于 bIsMother (MutateCharacterToMother 写入时双字段同步)
	 */
	UPROPERTY(Replicated, BlueprintReadOnly, Category = "Combat|Mother")
	bool bIsHuman = true;

	/**
	 * bIsMother 复制回调 (客户端) — 触发 OnMotherStatusChanged 广播
	 * 大厂原则 — UE 5.6 OnRep: 必须用 UFUNCTION() 标记, 引擎自动调用
	 */
	UFUNCTION()
	void OnRep_bIsMother();

	/**
	 * 【v93 大厂架构新增】母体左键攻击蒙太奇 — 真理源 (母体无武器也能攻击)
	 *
	 * 业务背景:
	 *   生化模式母体变异后, 玩家按左键应播放母体专属攻击动画 (无武器)
	 *   - 由 BP_MuTi.uasset 在 ClassDefaults → Combat|Mother → Mother Attack Montage 配
	 *   - 必填, 零兜底 (未配 → PlayerComboComponent::LightAttack_Pressed 拒绝攻击 + Log Error)
	 *   - 仅 bIsMother=true 时使用, 刀战模式零影响 (bIsMother 永远是 false)
	 *
	 * 镜像对称:
	 *   - 玩家武器轻击: BaseWeapon::LightAttackMontages[ComboIndex] (武器驱动)
	 *   - 玩家母体轻击: ABaseCharacter::MotherAttackMontage        (角色驱动, 单一蒙太奇无连击)
	 *
	 * 大厂原则 — 不重复架构:
	 *   - 复用现有 Multicast_PlayAttackAnim RPC (单一真理源), 不新增 RPC
	 *   - 客户端 Implementation 根据 bIsMother 选不同 Montage 来源 (武器 vs 母体字段)
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Combat|Mother")
	TObjectPtr<UAnimMontage> MotherAttackMontage = nullptr;

	/**
	 * 【v93.2 大厂架构 — 母体复用 Melee 缝合算法】母体攻击 Strategy 实例
	 *
	 * 设计动机:
	 *   - 母体没有武器 → 不能复用 BaseWeapon::DamageStrategy
	 *   - 母体有自己的 BoxTrace 算法 → 不能用 BP 蓝图 Tick (性能/抗抖动)
	 *   - 复用 MeleeSwStrategy 算法 (零重复架构)
	 *
	 * 大厂原则 — 单一真理源:
	 *   - 所有母体 Pawn 共享同一份 Strategy 实例? 不! 每个 Pawn 一份
	 *   - Strategy 内部有 IgnoreActors / LastFrameStartLoc 等跨帧状态, 不能共享
	 *   - 这是"per-instance state" 不是 "shared singleton"
	 *
	 * 零兜底:
	 *   - Strategy null → StartMotherTrace 拒绝 + Log Error (强制修复)
	 *   - 构造函数创建, BeginPlay 不再缓存 (v40.6 大厂原则 — 不缓存 Actor 引用)
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Combat|Mother")
	TObjectPtr<UMeleeSwStrategy> MotherTraceStrategy = nullptr;

	/**
	 * 母体变异事件广播 (客户端被动接收)
	 * - 服务器调 Target->Multicast_PlayMutationFX 通知所有客户端
	 * - 客户端 Implementation 触发本委托
	 * - HUD / 蓝图可订阅播变身动画 + 粒子 + 音效
	 */
	DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnMotherStatusChanged, const FString&, TargetName);

	UPROPERTY(BlueprintAssignable, Category = "Combat|Mother")
	FOnMotherStatusChanged OnMotherStatusChanged;

	/**
	 * 【v93.1 大厂架构】Multicast RPC — 播放"母体变异"视觉特效到所有客户端
	 *
	 * 大厂原则 — RPC 边界纯数据化 (v31.6):
	 *   - 参数是 FString (TargetName), 不传 Actor* (UE 网络边界序列化要求)
	 *   - 业务方: URoomMotherMutationSubsystem::MutateCharacterToMother 服务器触发
	 *
	 * 大厂原则 — UE 5.6 NetMulticast 行为:
	 *   - 服务器自己调用时, NetMulticast **会** 在服务器本地执行 Implementation
	 *   - 远端客户端收到时也执行 Implementation
	 *   - 服务器自己可能也播一次 (ListenServer 模式)
	 *
	 * @param TargetName 被变异角色的名称 (服务器本地读 Target->GetName())
	 */
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_PlayMutationFX(const FString& TargetName);
	//   迁移: 旧调用点 ARoomGameMode::SpawnAIInternal 改用 FFactionTags::AttitudeBetween / IsSameSide

	/**
	 * 【v54 大厂架构重构 — UAIProfileAsset 已删除】MeleeProfile 字段整个删除
	 *
	 * 历史 (Phase 1 - v53):
	 *   - 让编辑器里直接摆的 AI Pawn 也能拿到 Profile (无需走 RoomGameMode.AddAIToRoom)
	 *   - 在 BP_GruntAI Details → Default Melee Profile 拖入 DA_MeleeGrunt
	 *
	 * v54 重构 (用户决策 2026.07.16):
	 *   - UAIProfileAsset 整个类已删除, 此字段整个删除
	 *   - ConfigSO 现在由 AMeleeAIController.DefaultMeleeConfig 持有 (单 BP 配置一次, 所有 AI 共享)
	 *   - 关卡预放 AI: AIController::SetupMeleeAI(DefaultMeleeConfig) → SetMeleeConfig 链路
	 *   - 大厅入队 AI: RoomGameMode::SpawnAIInternal 末尾 → AIController::InitializeFromConfig
	 */

	/**
	 * 【P0 大厂架构 2026.07.06 → v54 重命名】ConfigSO 直接设置武器入口
	 *
	 * 旧名: SetMeleeProfile(UAIProfileAsset*) — UAIProfileAsset 已删除
	 * 新名: SetMeleeConfig(UAIBehaviorConfigSO*) — v54 替代
	 *
	 * 设计: SetSpawnLoadout 空实现导致 AI 无法拿武器.
	 *       现在通过 SetMeleeConfig 直接传 ConfigSO, 后续 PossessedBy 不再需要缓存字段
	 *
	 * 调用方: AMeleeAIController::SetupMeleeAI 末尾 (关卡预放 AI 自举)
	 *
	 * 【v54 大厂架构重构】转发壳 — 实际逻辑在 WeaponAttachmentComponent::SetMeleeConfig
	 */
	UFUNCTION(BlueprintCallable, Category = "AI|Config")
	void SetMeleeConfig(UAIBehaviorConfigSO* InConfig);

	/**
	 * 【P0 大厂架构 2026.07.06 → v54 重命名】ConfigSO 同步武器数据
	 *
	 * 旧名: SyncWeaponFromProfile() — 从已删除的 MeleeProfile 字段读
	 * 新名: SyncWeaponFromConfig() — 直接接 ConfigSO, 不再依赖已删除字段
	 *
	 * 调用方: 应该在 SpawnAndEquipWeapon 之前调用 (PossessedBy 中)
	 *
	 * 【v54 大厂架构重构】转发壳 — 实际逻辑在 WeaponAttachmentComponent::SyncWeaponFromConfig
	 */
	void SyncWeaponFromConfig();

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
	 *
	 * 【2026.07.12 P0 重构】转发壳 — 实际逻辑在 CombatDeathComponent::TakeDamage
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

	/**
	 * 【v40.8 P0 大厂架构】复活无敌期视觉闪烁
	 *
	 * 职责:
	 *   - 订阅 HealthComponent->OnInvincibilityChanged
	 *   - 准备身体 Mesh 的 MID (含 FlickerAmount 参数验证)
	 *   - 派发 OnFlickerStarted/OnFlickerStopped 到 BP 子类 (Timeline 驱动)
	 *
	 * 大厂原则 - 数据/视觉分离:
	 *   - HealthComponent 只管数据 (bIsInvincible)
	 *   - 本组件只管视觉 (MID 操作)
	 *   - BP 子类只管动画 (Timeline)
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components|Flicker")
	UInvincibilityFlickerComponent* FlickerComponent;

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
	 * 【2026.07.12 P0 大厂重构 Phase 2】玩家连击状态机组件 (Phase 2.1)
	 * 职责: 玩家轻击/重击/技能/连击窗口 等状态机逻辑
	 * 大厂原则 - 单一真理源: 状态机字段 (bIsAttacking/ComboIndex 等) 全部在本组件
	 * BaseCharacter 上同名方法改为转发壳
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components|Combat")
	UPlayerComboComponent* PlayerCombo;

	/**
	 * 【2026.07.12 P0 大厂重构 Phase 2】AI 攻击子系统组件 (Phase 2.2)
	 * 职责: AI 专用轻击入口 + 蒙太奇生命周期 + 攻击节流 + 伤害上报
	 * 大厂原则 - 职责分离: 与 PlayerComboComponent 完全解耦, AI 改攻击不影响玩家
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components|Combat")
	UAIAttackComponent* AIAttack;

	/**
	 * 【2026.07.12 P0 大厂重构 Phase 2】战斗死亡/无敌期组件 (Phase 2.3)
	 * 职责: TakeDamage/Die/ExecuteDeathLocal/EnableRagdoll/Multicast_Die/DropAndFadeWeapon
	 *       + ActivateSpawnInvincibility/DeactivateSpawnInvincibility
	 * 大厂原则 - 单一真理源: 死亡字段 bDeathSequenceStarted 在本组件
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components|Combat")
	UCombatDeathComponent* CombatDeath;

	/**
	 * 【2026.07.12 P0 大厂重构 Phase 2】武器装备/挂载组件 (Phase 2.4)
	 * 职责: OnRep_CurrentWeapon/EquipWeapon/RequestWeaponSpawn/SpawnAndEquipWeapon
	 *       + SetSpawnLoadout/GetSpawnWeaponID/GetCurrentWeapon/FindWeaponAttachmentConfig
	 * 大厂原则 - 单一真理源: CurrentWeapon 字段在本组件
	 *
	 * 【v36 修复】字段被 BP 子类覆盖可能为 nullptr, 提供 ResolveWeaponAttach() lazy 查找
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components|Combat")
	UWeaponAttachmentComponent* WeaponAttach;

	/**
	 * 【v36 新增】Lazy resolve WeaponAttachmentComponent — 单一真理源 = GetComponentsByClass
	 *
	 * 根因 (Session1.log):
	 *   构造函数 EXIT 时 WeaponAttach=0x...257DE70140 (有效)
	 *   但 SetSpawnLoadout ENTER 时 WeaponAttach=0x0000000000000000 (null!)
	 *   → BP 子类在某种初始化阶段清空了 C++ 默认字段
	 *
	 * 大厂原则:
	 *   - 不要依赖 raw pointer 字段缓存 (字段可能被 BP 子类覆盖)
	 *   - 真理源永远是 GetComponentByClass (UE 标准 API, 永远能找到)
	 *   - 每次访问都重新 resolve, 不缓存 (缓存失效 = 隐性兜底)
	 *   - 找不到 → Log Error + return nullptr (零兜底, 强制修复 BP 配置)
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat")
	UWeaponAttachmentComponent* ResolveWeaponAttach()
	{
		return ResolveComponent<UWeaponAttachmentComponent>(WeaponAttach, TEXT("WeaponAttach"));
	}

	// ============================================================
	// 【v38 大厂重构】统一组件 Resolver — 解决 BP 子类覆写 C++ 默认子对象的根因
	// ============================================================
	//
	// 根因 (2026.07.12 Session1.log):
	//   - 构造函数 EXIT 时 WeaponAttach=有效, PlayerCombo=有效
	//   - SetupPlayerInputComponent 时 PlayerCombo=null (裸字段访问!)
	//   - 同样的根因影响 PlayerCombo / AIAttack / CombatDeath / CharacterIcon
	//     (WeaponAttach 已有 v36 lazy resolver, 但其他组件没有)
	//
	// 大厂原则 — 零重复 (DRY):
	//   - 之前: 每个组件需要单独写 lazy resolver (v36 给 WeaponAttach 写了一份)
	//   - v38: 统一模板 + 5 个零开销包装, 一次解决
	//   - 真理源 = GetComponentByClass (UE 标准 API, 永远能找到)
	//   - 优先用字段 (避免每次 FindComponent 开销)
	//   - 找不到 → Log Error + return nullptr (零兜底)
	// ============================================================

	/**
	 * 通用组件 Resolver 模板
	 *   1. 优先用 CachedField (fast path)
	 *   2. 字段为 null → FindComponentByClass<T>() (UE 标准 API)
	 *   3. 找到 → 写回字段, 后续走 fast path
	 *   4. 找不到 → Log Error + return nullptr (零兜底)
	 */
	template<typename T>
	T* ResolveComponent(T*& CachedField, const TCHAR* ComponentName)
	{
		if (CachedField)
		{
			return CachedField;
		}

		T* Found = FindComponentByClass<T>();
		if (Found)
		{
			CachedField = Found;
			UE_LOG(LogTemp, Warning,
				TEXT("[BaseCharacter] %s 字段为 null, 通过 FindComponentByClass 找到组件. "
					 "Pawn=%s. "
					 "【警告】C++ 默认字段在某种初始化阶段被清空, 已修复字段. "
					 "【修复】BP 蓝图 Components 面板的 '%s' 子对象可能未连接 (Disconnected) 或被 BP 子类重命名."),
				ComponentName, *GetName(), ComponentName);
			return Found;
		}

		UE_LOG(LogTemp, Error,
			TEXT("[BaseCharacter] %s 组件未挂载! Pawn=%s, Class=%s. "
				 "【v38 零兜底】拒绝返回 nullptr 让调用方猜. "
				 "【修复】UE 编辑器 → BP 蓝图 → Components 面板 → 添加 %s 子对象. "
				 "C++ 构造函数 CreateDefaultSubobject 应该已自动添加, 但 BP 子类可能删除了它."),
			ComponentName, *GetName(), *GetClass()->GetName(), ComponentName);
		return nullptr;
	}

	/** v38 — 玩家连击状态机 (通用 resolver 入口) */
	UFUNCTION(BlueprintCallable, Category = "Combat")
	UPlayerComboComponent* ResolvePlayerCombo()
	{
		return ResolveComponent<UPlayerComboComponent>(PlayerCombo, TEXT("PlayerCombo"));
	}

	/** v38 — AI 攻击子系统 (通用 resolver 入口) */
	UFUNCTION(BlueprintCallable, Category = "Combat")
	UAIAttackComponent* ResolveAIAttack()
	{
		return ResolveComponent<UAIAttackComponent>(AIAttack, TEXT("AIAttack"));
	}

	/** v38 — 战斗死亡/无敌期 (通用 resolver 入口) */
	UFUNCTION(BlueprintCallable, Category = "Combat")
	UCombatDeathComponent* ResolveCombatDeath()
	{
		return ResolveComponent<UCombatDeathComponent>(CombatDeath, TEXT("CombatDeath"));
	}

	/** v38 — 角色头像/武器图标 (通用 resolver 入口) */
	UFUNCTION(BlueprintCallable, Category = "Combat")
	UCharacterIconComponent* ResolveCharacterIcon()
	{
		return ResolveComponent<UCharacterIconComponent>(CharacterIcon, TEXT("CharacterIcon"));
	}

	// ============================================================
	// 【v39 大厂重构】6 个新增 Resolver — 覆盖原 6 个未受 v38 保护的字段
	//
	// 根因 (2026.07.12 Session2.log):
	//   - BP 子类 archetype 在某种初始化阶段会清空 C++ 默认子对象字段
	//   - v38 只保护了 PlayerCombo/AIAttack/CombatDeath/WeaponAttach/CharacterIcon 5 个
	//   - 老的 6 个组件 (Health/Energy/CharacterEvents/Dissolve/Footstep/HealthRegen) 仍是裸字段
	//   - 结果: HUD 订阅 CharacterEvents 失败, 血量/头像永远不更新
	//
	// 大厂原则 — 零重复 (DRY):
	//   - 复用 v38 ResolveComponent<T> 模板, 不写新逻辑
	//   - 6 个零开销 inline 包装, 一次调用 ResolveComponent
	//
	// 调用方迁移规则:
	//   - BaseCharacter.cpp 内部: 所有裸字段访问改为 Resolve 函数
	//   - 外部 (HUD/BP): 不直接访问字段, 走 Resolve 函数
	// ============================================================

	/** v39 — 健康组件 (血量/受伤/死亡事件) - 通用 resolver 入口 */
	UFUNCTION(BlueprintCallable, Category = "Combat")
	UHealthComponent* ResolveHealthComponent()
	{
		return ResolveComponent<UHealthComponent>(HealthComponent, TEXT("HealthComponent"));
	}

	/** v39 — 能量组件 (消耗/回复/百分比) - 通用 resolver 入口 */
	UFUNCTION(BlueprintCallable, Category = "Combat")
	UEnergyComponent* ResolveEnergyComponent()
	{
		return ResolveComponent<UEnergyComponent>(EnergyComponent, TEXT("EnergyComponent"));
	}

	/** v39 — 角色事件总线组件 (头像/血量/能量/AC/ACE/武器) - 通用 resolver 入口
	 *
	 * 【关键修复 Bug 2/5】HUD 订阅 CharacterEvents 时如果字段为 null → 永远不订阅成功
	 *   - 修复前: `Character->CharacterEvents` 直接访问, 字段 null 时跳过订阅
	 *   - 修复后: 调 ResolveCharacterEvents() 永远拿到有效指针 (lazily find by class)
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat")
	UCharacterEvents* ResolveCharacterEvents()
	{
		return ResolveComponent<UCharacterEvents>(CharacterEvents, TEXT("CharacterEvents"));
	}

	/** v39 — 溶解组件 (死亡后材质渐隐) - 通用 resolver 入口 */
	UFUNCTION(BlueprintCallable, Category = "Combat")
	UDissolveComponent* ResolveDissolveComponent()
	{
		return ResolveComponent<UDissolveComponent>(DissolveComponent, TEXT("DissolveComponent"));
	}

	/** v39 — 脚步音效组件 (地面检测 + 音效播放) - 通用 resolver 入口 */
	UFUNCTION(BlueprintCallable, Category = "Combat")
	UFootstepComponent* ResolveFootstepComponent()
	{
		return ResolveComponent<UFootstepComponent>(FootstepComponent, TEXT("FootstepComponent"));
	}

	/** v39 — 生命回复组件 - 通用 resolver 入口 */
	UFUNCTION(BlueprintCallable, Category = "Combat")
	UHealthRegenComponent* ResolveHealthRegenComponent()
	{
		return ResolveComponent<UHealthRegenComponent>(HealthRegenComponent, TEXT("HealthRegenComponent"));
	}

	/**
	 * 【v40.8 新增】无敌期视觉闪烁组件 - 通用 resolver 入口
	 *
	 * 走与 ResolveDissolveComponent 完全相同的模板路径
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat")
	UInvincibilityFlickerComponent* ResolveFlickerComponent()
	{
		return ResolveComponent<UInvincibilityFlickerComponent>(FlickerComponent, TEXT("FlickerComponent"));
	}

	/**
	 * 【2026.07.12 P0 大厂重构 Phase 2】角色头像/武器图标刷新组件 (Phase 2.5)
	 * 职责: RefreshCharacterIcon/Client_RefreshCharacterIcon_Implementation/RetryRefreshCharacterIcon
	 *       + RefreshWeaponIconOnHUD/GetCharacterAvatarFromTable
	 * 大厂原则 - 单一真理源: CachedCharacterIDForIcon/CurrentIconRetryCount/CharacterIconRefreshTimerHandle 在本组件
	 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components|Combat")
	UCharacterIconComponent* CharacterIcon;

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

	/**
	 * 【v51 大厂架构 — 生化模式】服务器权威切武器槽位 (Server RPC)
	 *
	 * 调用方:
	 *   - Q 键输入 → OnSwitchWeaponPressed → 本 RPC → 服务器执行
	 *   - 自动在 Primary ↔ Melee 之间循环切换
	 *
	 * 大厂原则 (服务器权威 + 单一真理源):
	 *   - 客户端不能直接切槽位 (防作弊)
	 *   - 服务器调 WeaponAttachmentComponent::Server_SwitchToWeaponSlot
	 *   - 切换完成后 UE 自动同步 CurrentWeaponSlot (Replicated) + OnRep 客户端
	 *
	 * @param TargetSlot 目标槽位 (Primary / Melee)
	 */
	UFUNCTION(Server, Reliable, WithValidation)
	void Server_SwitchWeaponSlot(EWeaponSlotType TargetSlot);

	/**
	 * 【v51 大厂架构 — 生化模式】客户端处理 Q 键切换武器槽位
	 *
	 * 调用方: SetupPlayerInputComponent 绑定 SwitchWeaponAction → Enhanced Input Triggered → 本回调
	 *
	 * 行为:
	 *   - 调 Server_SwitchWeaponSlot RPC (服务器权威)
	 *   - 服务器基于当前槽位自动计算目标槽位 (Primary↔Melee 循环)
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat|Weapons|Slot")
	void OnSwitchWeaponPressed();

	/**
	 * 【v58 大厂架构 — FPS枪战】客户端切换到主武器（按键1）
	 *
	 * 调用方: SetupPlayerInputComponent 绑定 SwitchToPrimaryAction → Enhanced Input Triggered → 本回调
	 *
	 * 行为:
	 *   - 调 Server_RequestSwitchToSlot RPC → 服务器切换到主槽位
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat|Weapons|Slot")
	void OnSwitchToPrimaryPressed();

	/**
	 * 【v58 大厂架构 — FPS枪战】客户端切换到副武器（按键2）
	 *
	 * 调用方: SetupPlayerInputComponent 绑定 SwitchToSecondaryAction → Enhanced Input Triggered → 本回调
	 *
	 * 行为:
	 *   - 调 Server_RequestSwitchToSlot RPC → 服务器切换到副槽位
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat|Weapons|Slot")
	void OnSwitchToSecondaryPressed();

	/**
	 * 【v58 大厂架构 — FPS枪战】客户端切换到近战武器（按键3）
	 *
	 * 调用方: SetupPlayerInputComponent 绑定 SwitchToMeleeAction → Enhanced Input Triggered → 本回调
	 *
	 * 行为:
	 *   - 调 Server_RequestSwitchToSlot RPC → 服务器切换到近战槽位
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat|Weapons|Slot")
	void OnSwitchToMeleePressed();

	/**
	 * 【v58 大厂架构 — FPS枪战】服务器执行显式槽位切换（1/2/3键）
	 *
	 * 大厂原则 (服务器权威):
	 *   - 客户端发送目标槽位 → 服务器校验槽位是否有武器 → 执行切换
	 *   - 服务器检查目标槽位是否有武器，无武器则拒绝切换
	 *
	 * @param TargetSlot 目标槽位
	 */
	UFUNCTION(Server, Reliable, WithValidation)
	void Server_RequestSwitchToSlot(EWeaponSlotType TargetSlot);


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
	 * 授予符合条件的玩家助攻得分 (服务器纯函数, 单一职责)
	 * @param Victim         受害者 (读取 LastHitTimestamps)
	 * @param Killer        击杀者 (排除)
	 * @param KillMethod    击杀方式 (纯数据传给 Multicast_NotifyKill)
	 * @param bIsKillerPlayer Killer 是否为玩家控制的角色 (决定是否显示 KillFeed 图标)
	 *
	 * 大厂原则 (v31.6):
	 *   - 不再负责 AddKillScore / AddDeath (这些已在 TakeDamage 末尾集中处理)
	 *   - KillMethod 作为参数显式传入, 不依赖 Character.LastKillMethod 字段
	 */
	static void GrantAssistsToEligiblePlayers(ABaseCharacter* Victim, ABaseCharacter* Killer,
	                                          EKillMethod KillMethod, bool bIsKillerPlayer);

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

	// 【v31.6 大厂重构】删除 LastKillMethod 字段 — 真理源唯一 = Weapon::LastKillMethod
	//
	// 历史 (v22-v31.5):
	//   - Weapon 自己持有 LastKillMethod (写入: Server_ReportHit 根据部位设值)
	//   - Character 也持有 LastKillMethod (写入: TakeDamage 时从 Weapon 拷贝, 用于 Multicast_NotifyKill)
	//   - 两个字段 + DOREPLIFETIME 复制 = 重复架构
	//
	// 大厂原则 (v31.6):
	//   - 真理源唯一: Weapon.LastKillMethod (被击杀方式由"哪把武器杀的"决定, 武器天然知道)
	//   - RPC 边界纯数据化: Multicast_NotifyKill 直接传 EKillMethod, 不再读 Character 字段
	//   - 删除 DOREPLIFETIME (line 526), 删除字段声明 (line 540)

	/**
	 * 死亡动画蒙太奇
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Combat")
	UAnimMontage* DeathMontage;

	/**
	 * 死亡逻辑（服务器端）
	 * 触发播放死亡动画 + 启动布娃娃 + 启动溶解
	 *
	 * 【2026.07.12 P0 重构】转发壳 — 实际逻辑在 CombatDeathComponent::Die()
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
	 * 【2026.07.14 新增】HealthComponent 无敌期状态变化回调
	 * 用途: 订阅 HealthComponent->OnInvincibilityChanged，广播到 CharacterEvents->OnInvincibilityChanged
	 *       让 UI 层 (GameHUDWidget) 统一订阅显示复活进度条
	 *
	 * 调用时机:
	 *   - 服务器激活无敌: HealthComponent::ActivateInvincibility → Broadcast(true)
	 *   - 服务器到期: HealthComponent::ExpireInvincibility_Internal → Broadcast(false)
	 *   - 客户端激活: HealthComponent::OnRep_InvincibilityChanged → Broadcast(true)
	 *   - 客户端到期: HealthComponent::OnRep_InvincibilityChanged → Broadcast(false)
	 */
	UFUNCTION()
	void OnHealthComponentInvincibilityChanged(bool bIsNowInvincible);

	/**
	 * 【2026-07-01 新增】本地执行死亡流程
	 * 客户端通过 HealthComponent->OnRep_bIsDead 触发, 无需 Multicast RPC
	 * 服务器 Die() 内部也复用此方法
	 *
	 * 【2026.07.12 P0 重构】转发壳 — 实际逻辑在 CombatDeathComponent::ExecuteDeathLocal()
	 */
	void ExecuteDeathLocal();

	/**
	 * 【2026.07.12 P0 重构】删除 bDeathSequenceStarted 字段 — 已迁移到 CombatDeathComponent
	 *
	 * 历史 (v22-v31.5):
	 *   - BaseCharacter::bDeathSequenceStarted 是死亡序列幂等标志
	 *   - 服务器/客户端可能通过多个路径触发死亡流程
	 *
	 * 大厂原则 (Phase 2.3 落地):
	 *   - 真理源迁移到 CombatDeathComponent::bDeathSequenceStarted
	 *   - BaseCharacter 不再持有副本, 完全委托给 CombatDeath
	 *   - 字段已彻底删除 (cPP 中已无引用)
	 */

	/**
	 * 启用布娃娃物理（角色真实死亡时调用）
	 *
	 * 【2026.07.12 P0 重构】转发壳 — 实际逻辑在 CombatDeathComponent::EnableRagdoll()
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
	 *
	 * 【2026.07.12 P0 重构】转发壳 — 实际逻辑在 CombatDeathComponent::DropAndFadeWeapon()
	 */
	void DropAndFadeWeapon(ABaseWeapon* Weapon);

	/**
	 * 【2026.07.12 P0 重构】删除 RagdollTimerHandle 字段 — 已迁移到 CombatDeathComponent
	 */

	/**
	 * 【2026.07.12 P0 重构】删除 CharacterIconRefreshTimerHandle 字段 — 已迁移到 CharacterIconComponent
	 */

	/**
	 * 【2026.07.12 P0 重构】删除 CurrentIconRetryCount 字段 — 已迁移到 CharacterIconComponent
	 */

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
	 * 【2026.07.12 P0 重构】删除 CachedCharacterIDForIcon 字段 — 已迁移到 CharacterIconComponent
	 */

	/**
	 * 网络多播死亡（让所有玩家都看到你变成布娃娃）
	 *
	 * 【2026.07.12 P0 重构】RPC 仍保留在 BaseCharacter 上 (RPC 必须在 Actor 上),
	 *                      但实现转发到 CombatDeathComponent
	 */
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_Die();

	/**
	 * 【v76 大厂架构】Multicast RPC — 播放"拿起武器"音效到所有客户端
	 *
	 * 触发场景:
	 *   - 服务器 Server_SwitchToWeaponSlot → UWeaponAttachmentComponent::PlayEquipSoundForSlot
	 *   - PlayEquipSoundForSlot 拿到 Sound 数据 → 调本 RPC
	 *   - 所有客户端 (包括服务器自己的 ListenClient 实例) 收到 RPC → PlaySoundAtLocation
	 *
	 * 大厂原则 — RPC 纯数据化:
	 *   - 参数是 USoundBase* / float — 不传 Actor / Component 引用 (UE 网络边界序列化要求)
	 *   - 调用方负责查出 Sound 数据后再传, RPC 内部不查 (避免分布式 RPC 里反查 GM)
	 *
	 * 大厂原则 — UE 5.6 NetMulticast 行为:
	 *   - 服务器自己调用时, NetMulticast **会** 在服务器本地执行 Implementation
	 *   - 远端客户端收到时也执行 Implementation
	 *   - 服务器 Local 实例播本地音效 (如果它有可视窗口)
	 *   - Dedicated Server 上服务器实例没声音 (合理 — 服务器不渲染)
	 *
	 * @param InSound       要播放的 SoundBase (SoundCue / SoundWave / MetaSoundSource)
	 * @param VolumeMul     音量倍数 (DataAsset.VolumeMultiplier)
	 * @param PitchMul      音高倍数 (DataAsset.PitchMultiplier)
	 */
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_PlayEquipSoundForSlot(EWeaponSlotType NewSlot, USoundBase* InSound, float VolumeMul, float PitchMul);

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

	/**
	 * 【2026.07.11 P0 大厂架构】复活无敌期（秒）
	 *
	 * 时序:
	 *   - 死亡 → RespawnDelaySeconds 后生成新 Pawn (玩家路径 via PC->StartRespawnTimer)
	 *   - AI 路径: 直接生成新 Pawn (无延迟)
	 *   - Pawn Spawn 完毕 → PossessedBy → 立即激活无敌期 DefaultSpawnInvincibilitySeconds 秒
	 *   - 无敌期内: HealthComponent::ApplyDamage 拦截所有伤害 (Layer 0 单一真理源)
	 *   - 到期: Server Timer 自动清字段 → 客户端 OnRep_InvincibilityChanged 广播
	 *
	 * 用户决策 (2026.07.11 AskQuestion 选项 A):
	 *   - 默认 3 秒
	 *   - 配置 <= 0 → HealthComponent::ActivateInvincibility 静默跳过 (零兜底, 不强制默认值)
	 *   - 玩家路径 BP BP_BaseCharacter 直接配 (此字段就是给玩家用的)
	 *   - AI 路径在 DA_AIBehaviorConfig_XXX.SpawnInvincibilitySeconds 配 (覆盖此默认值)
	 *
	 * 为什么不把 RespawnDelaySeconds 和 SpawnInvincibilitySeconds 合并:
	 *   - 语义不同: RespawnDelaySeconds 是"死后等多久重生", SpawnInvincibilitySeconds 是"重生后无敌多久"
	 *   - 实战调参: 改 spawn invincibility 不应该影响死亡到重生的时间间隔
	 *   - 大厂原则: 配置项语义单一, 一个值改一件事
	 */
	UPROPERTY(EditDefaultsOnly, BlueprintReadWrite, Category = "Combat|Respawn", meta = (ClampMin = "0.0", ClampMax = "30.0"))
	float DefaultSpawnInvincibilitySeconds = 2.0f;


	// ==========================================
	// 击杀奖励系统
	// ==========================================
public:
	/**
	 * 获取默认复活无敌期时长（秒）
	 *
	 * 大厂原则 - 封装: 外部类不直接读 protected 字段, 通过 getter 访问
	 * DurationOverride=-1.0 时, ActivateSpawnInvincibility 用此值
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat|Respawn")
	float GetDefaultSpawnInvincibilitySeconds() const { return DefaultSpawnInvincibilitySeconds; }

	/**
	 * 【v41 大厂架构】设置复活延迟秒数
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat|Respawn")
	void SetRespawnDelaySeconds(float InDelay) { RespawnDelaySeconds = InDelay; }

	/**
	 * 【v41 大厂架构】设置武器销毁延迟秒数
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat|Death")
	void SetWeaponDestroyDelaySeconds(float InDelay) { WeaponDestroyDelaySeconds = InDelay; }

	/**
	 * 【v41 大厂架构】设置复活无敌期秒数
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat|Respawn")
	void SetDefaultSpawnInvincibilitySeconds(float InSeconds) { DefaultSpawnInvincibilitySeconds = InSeconds; }

	/**
	 * 【v41 大厂架构】设置击杀奖励血量
	 */
	UFUNCTION(BlueprintCallable, Category = "Stats|KillReward")
	void SetHealthRewardPerKill(float InReward) { HealthRewardPerKill = InReward; }

	/**
	 * 【v41 大厂架构】设置击杀奖励能量
	 */
	UFUNCTION(BlueprintCallable, Category = "Stats|KillReward")
	void SetEnergyRewardPerKill(float InReward) { EnergyRewardPerKill = InReward; }

	/**
	 * 【v41 大厂架构】设置助攻判定时间窗口
	 */
	UFUNCTION(BlueprintCallable, Category = "Stats|Assist")
	void SetAssistTimeWindow(float InWindow) { AssistTimeWindow = InWindow; }

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


	// ============================================================
	// 【2026.07.11 P0 大厂架构】复活无敌期 — public 入口 (集中调度层)
	// ============================================================
	//
	// 调用方 (大厂原则 - 集中调度):
	//   - ARoomGameMode::HandlePlayerRequestSpawn (玩家路径末尾)
	//   - ARoomGameMode::RequestRespawn AI 分支末尾 (Possess + SetupMeleeAI 之后)
	//   - ARoomGameMode::SpawnAIInternal (战斗开局 AI 生成末尾)
	//
	// 设计原则 (单一真理源 + 零兜底):
	//   - 仅 HealthComponent 写字段, Actor 只编排行为 (跟 bIsDead 同构)
	//   - HealthComponent 已做完整 server-only 守卫, 这里简化
	//   - DurationOverride < 0 → 使用 DefaultSpawnInvincibilitySeconds (BP 配置)
	//   - DefaultSpawnInvincibilitySeconds <= 0 → 用户决策 A: 静默跳过激活
	// ============================================================

	/**
	 * 激活复活无敌期
	 *
	 * @param DurationOverride  < 0 (默认): 用 DefaultSpawnInvincibilitySeconds
	 *                           > 0: 强制用这个值 (例如 AI ConfigSO 配的业务值)
	 *                           == 0: 静默跳过激活
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat|Respawn")
	void ActivateSpawnInvincibility(float DurationOverride = -1.0f);

	/**
	 * 取消复活无敌期 (死亡时强制调用, 防止 Timer 残留)
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat|Respawn")
	void DeactivateSpawnInvincibility();


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
	 * 【v51 大厂架构 — 生化模式】输入动作: 切换武器槽位 (Q 键)
	 *
	 * 调用方: ABaseCharacter::SetupPlayerInputComponent 绑定 Enhanced Input
	 * 触发: Server_SwitchWeaponSlot (RPC → 服务器权威切槽)
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	UInputAction* SwitchWeaponAction;

	/**
	 * 【v58 大厂架构 — FPS枪战】切换到主武器（按键1）
	 *
	 * 调用方: ABaseCharacter::SetupPlayerInputComponent 绑定 Enhanced Input
	 * 触发: Server_RequestSwitchToSlot RPC → 服务器切换到主槽位
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	UInputAction* SwitchToPrimaryAction;

	/**
	 * 【v58 大厂架构 — FPS枪战】切换到副武器（按键2）
	 *
	 * 调用方: ABaseCharacter::SetupPlayerInputComponent 绑定 Enhanced Input
	 * 触发: Server_RequestSwitchToSlot RPC → 服务器切换到副槽位
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	UInputAction* SwitchToSecondaryAction;

	/**
	 * 【v58 大厂架构 — FPS枪战】切换到近战武器（按键3）
	 *
	 * 调用方: ABaseCharacter::SetupPlayerInputComponent 绑定 Enhanced Input
	 * 触发: Server_RequestSwitchToSlot RPC → 服务器切换到近战槽位
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	UInputAction* SwitchToMeleeAction;

	/**
	 * 开火输入 (左键, Triggered 事件)
	 * v60 — 玩家枪械射击
	 * 玩家按 → Triggered → OnFirePressed (一直按住一直触发)
	 * 玩家松 → Completed → OnFireReleased
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	UInputAction* FireAction;

	/**
	 * 换弹输入 (R 键, Started 事件)
	 * v60 — 玩家换弹
	 */
	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Input")
	UInputAction* ReloadAction;

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

	/**
	 * v60 枪械射击输入回调
	 *
	 * - Triggered (按下+按住): 每帧触发, 路由到 CurrentWeapon->Server_StartFire
	 *   - 半自动: Component 内部节流保护, 只打一发
	 *   - 全自动: Component 内部 Tick 节流, 按射速持续打
	 * - Completed (松开): 路由到 CurrentWeapon->Server_StopFire
	 *
	 * 大厂原则:
	 *   - 服务器权威: 这里只发 Server RPC, 真逻辑在 WeaponFireComponent
	 *   - MeshType 校验: 当前武器是 Melee 时拒绝转发 (刀按左键走 LightAttack_Pressed)
	 */
	void OnFirePressed(const FInputActionValue& Value);
	void OnFireReleased(const FInputActionValue& Value);

	/**
	 * v60 换弹输入回调
	 *
	 * - Started (按下瞬间): 路由到 CurrentWeapon->Server_StartReload
	 */
	void OnReloadPressed(const FInputActionValue& Value);


	// ==========================================
	// 4. 武器与自动连斩系统核心变量
	// ==========================================
protected:
	/**
	 * 【2026.07.12 P0 重构】删除 CurrentWeapon 字段 — 已迁移到 WeaponAttachmentComponent
	 *
	 * 历史 (v22-v31.5):
	 *   - CurrentWeapon 是 BaseCharacter 上的 Replicated 武器指针
	 *   - OnRep_CurrentWeapon 是网络同步回调, 客户端用这个同步生成武器
	 *
	 * 大厂原则 (Phase 2.4 落地):
	 *   - 真理源迁移到 WeaponAttachmentComponent::CurrentWeapon
	 *   - BaseCharacter 不再持有副本, 完全委托给 WeaponAttach
	 *   - OnRep_CurrentWeapon 仍在 BaseCharacter 上声明 (UPROPERTY 必须在 Actor 上),
	 *     但实现转发到 WeaponAttachmentComponent
	 *   - 字段已彻底删除 (cpp 中已无引用)
	 */

	/**
	 * 武器指针改变时的回调（网络复制通知，客户端需要用这个来同步生成武器）
	 *
	 * 【2026.07.12 P0 重构】UFUNCTION 仍保留在 BaseCharacter 上 (UPROPERTY 必须在 Actor 上),
	 *                      实现转发到 WeaponAttachmentComponent
	 */
	UFUNCTION()
	void OnRep_CurrentWeapon(class ABaseWeapon* OldWeapon);

	/**
	 * 【2026.07.12 P0 重构】删除 WeaponAttachmentDataTable / CharacterID / SpawnWeaponID 字段
	 *                      — 已迁移到 WeaponAttachmentComponent
	 *
	 * 历史 (v22-v31.5):
	 *   - WeaponAttachmentDataTable: 武器挂载配置数据表
	 *   - CharacterID: 当前角色 ID (用于查找挂载配置)
	 *   - SpawnWeaponID: Spawn 时刻预定的武器 ID (AI 路径用)
	 *
	 * 大厂原则 (Phase 2.4 落地):
	 *   - 真理源全部迁移到 WeaponAttachmentComponent
	 *   - BaseCharacter 不再持有副本, 完全委托给 WeaponAttach
	 *   - 字段已彻底删除 (cpp 中已无引用)
	 *   - SetSpawnLoadout / GetSpawnWeaponID 仍是 BaseCharacter 转发壳
	 */

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
	 *   - 原实现: 仅在 !InXXXID.IsEmpty() 时写入, 导致 Profile.DefaultWeaponRowName=""
	 *     (原 Profile.WeaponID="") 时直接跳过, SpawnWeaponID 永远为空
	 *   - 新实现: 无论是否为空都写入, 确保外部覆盖意图被尊重
	 *   - 大厂原则: 显式赋值优于隐式跳过, 日志清晰可追溯
	 *
	 * 【2026.07.12 P0 重构】转发壳 — 实际逻辑在 WeaponAttachmentComponent::SetSpawnLoadout
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat|Attachment")
	void SetSpawnLoadout(const FString& InCharacterID, const FString& InWeaponID);

	/** 只读 getter, 给 AI Controller 用
	 *
	 * 【2026.07.12 P0 重构】转发壳 — 实际逻辑在 WeaponAttachmentComponent::GetSpawnWeaponID
	 */
	UFUNCTION(BlueprintPure, Category = "Combat|Attachment")
	const FString& GetSpawnWeaponID() const;

protected:

	/**
	 * 【2026.07.12 P0 重构】删除 Combo 状态机字段 — 已迁移到 PlayerComboComponent
	 *
	 * 历史 (v22-v31.5):
	 *   - bIsAttacking / bCanReceiveInput / bSaveAttack / bIsHoldingLightAttack / ComboIndex
	 *     都是 BaseCharacter 上的状态机字段
	 *
	 * 大厂原则 (Phase 2.1 落地):
	 *   - 真理源迁移到 PlayerComboComponent
	 *   - BaseCharacter 不再持有副本, 完全委托给 PlayerCombo
	 *   - 字段已彻底删除 (cpp 中已无引用)
	 */

	/**
	 * 【v49 大厂架构重构 — 单一真理源】根据 Class 查找挂载配置
	 *
	 * 转发壳 — 实际逻辑在 WeaponAttachmentComponent::FindWeaponAttachmentConfig
	 *
	 * @param InPawnClass   角色 BP 类 (例如 BP_GruntAI_C)
	 * @param InWeaponClass 武器 BP 类 (例如 BP_Weapon_Knife_C)
	 */
	struct FWeaponAttachmentConfig* FindWeaponAttachmentConfig(
		TSubclassOf<ABaseCharacter> InPawnClass,
		TSubclassOf<ABaseWeapon> InWeaponClass) const;

	/**
	 * 连招核心执行逻辑
	 *
	 * 【2026.07.12 P0 重构】转发壳 — 实际逻辑在 PlayerComboComponent::ExecuteComboSequence
	 */
	void ExecuteComboSequence();

	/**
	 * 轻击按下事件
	 *
	 * 【2026.07.12 P0 重构】转发壳 — 实际逻辑在 PlayerComboComponent::LightAttack_Pressed
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat")
	void LightAttack_Pressed();

	/**
	 * 轻击松开事件
	 *
	 * 【2026.07.12 P0 重构】转发壳 — 实际逻辑在 PlayerComboComponent::LightAttack_Released
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat")
	void LightAttack_Released();

	/**
	 * 重击事件（单击）
	 *
	 * 【2026.07.12 P0 重构】转发壳 — 实际逻辑在 PlayerComboComponent::HeavyAttack
	 */
	void HeavyAttack();

	/**
	 * 释放技能事件
	 *
	 * 【2026.07.12 P0 重构】转发壳 — 实际逻辑在 PlayerComboComponent::UseSkill
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
	 *
	 * 【2026.07.12 P0 重构】转发壳 — 实际逻辑在 PlayerComboComponent::EnableComboWindow
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat")
	void EnableComboWindow();

	/**
	 * 区间结束时触发（检查缓存区是否有缓存攻击）
	 *
	 * 【2026.07.12 P0 重构】转发壳 — 实际逻辑在 PlayerComboComponent::CheckCombo
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat")
	void CheckCombo();

	/**
	 * 动画彻底结束时触发
	 *
	 * 【2026.07.12 P0 重构】转发壳 — 实际逻辑在 PlayerComboComponent::EndAttackState
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
	 *
	 * 【2026.07.12 P0 重构】转发壳 — 实际逻辑在 AIAttackComponent::OnAIRequestAttack_Simple
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat|AI")
	bool OnAIRequestAttack_Simple();


	// ==========================================
	// 6. 战斗 RPC (网络动作同步)
	// ==========================================
protected:
	/**
	 * 客户端向服务器请求挥刀（带着连击序号，告诉大家播第几个动作）
	 *
	 * 【2026.07.12 P0 重构】UFUNCTION(Server, Reliable, WithValidation) 仍保留在 BaseCharacter 上
	 *                      (RPC 必须在 Actor 上), 实现转发到 AIAttackComponent::Server_PlayAttackAnim_Implementation
	 */
	UFUNCTION(Server, Reliable, WithValidation)
	void Server_PlayAttackAnim(bool bIsHeavy, int32 InComboIndex);

	/**
	 * 服务器向所有客户端广播: "大家都播放这个人的挥刀动画！"
	 *
	 * 【2026.07.12 P0 重构】UFUNCTION(NetMulticast, Reliable) 仍保留在 BaseCharacter 上
	 *                      (RPC 必须在 Actor 上), 实现转发到 AIAttackComponent::Multicast_PlayAttackAnim_Implementation
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
	 * 【2026.07.12 P0 重构】UFUNCTION(Server, Reliable, WithValidation) 仍保留在 BaseCharacter 上
	 *                      (RPC 必须在 Actor 上), 实现转发到 AIAttackComponent::Server_ReportAIAttackHit_Implementation
	 *
	 * @param HitActor  受击目标 (服务器校验: 必须是 ABaseCharacter 且未死)
	 * @param Damage    伤害值 (服务器还会再校验范围, 防作弊)
	 */
	UFUNCTION(Server, Reliable, WithValidation)
	void Server_ReportAIAttackHit(AActor* HitActor, float Damage);


	// ==========================================
	// 【v93.2 大厂架构 — 母体复用 Melee 缝合算法】母体攻击命中上报
	// ==========================================
	// 背景: 生化模式母体复用 ANS_MeleeTraceState (大厂复用原则), 但母体无武器
	//       → 命中 RPC 不能走 Weapon->Server_ReportHit (Weapon 必非空)
	//       → 母体必须有自己的 Server RPC, 但 RPC 必须声明在 Actor 上 (UE 5.6 硬约束)
	//
	// 复用动机 (用户需求 2026.07.25):
	//   - 复用 UANS_MeleeTraceState 类 (美术不学新标签)
	//   - 复用 MeleeSwStrategy::TickDetection BoxTrace 缝合算法 (零重复)
	//   - 母体路径: Mesh 来自 Owner (BP_MuTi), Socket 名 _Mother 后缀
	//   - 命中 RPC: 走 Server_ReportMotherAttackHit (本函数)
	//
	// 实现 (大厂原则 — 转发壳模式):
	//   - UFUNCTION(Server, Reliable, WithValidation) 必须在 BaseCharacter (Actor)
	//   - _Implementation 转发到 PlayerComboComponent 或 AIAttackComponent (账本归属)
	//   - 玩家母体 → PlayerComboComponent::Server_ReportMotherAttackHit_Implementation
	//   - AI 母体   → AIAttackComponent::Server_ReportMotherAttackHit_Implementation
	//   - 内部统一调 RoomMotherMutationSubsystem::MutateCharacterToMother (大厂复用)
	//
	// 大厂原则 — 不重复架构:
	//   - 防御层 (IsDead / 自伤 / 友军 / 无敌期) 与 Server_ReportAIAttackHit 同构
	//   - 仅"命中后行为"不同: AIAttack 走 ApplyDamage, MotherAttack 走 MutateCharacterToMother
	//
	// @param HitActor  受击目标 (服务器校验: 必须是 ABaseCharacter 且 bIsHuman=true 且未死)
	UFUNCTION(Server, Reliable, WithValidation)
	void Server_ReportMotherAttackHit(AActor* HitActor);


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
	 *
	 * 【2026.07.12 P0 重构】UFUNCTION 仍保留在 BaseCharacter 上 (UAnimInstance::OnMontageEnded 要求 UFUNCTION),
	 *                      实现转发到 AIAttackComponent::OnAIAttackMontageEnded
	 */
	UFUNCTION()
	void OnAIAttackMontageEnded(UAnimMontage* Montage, bool bInterrupted);

protected:
	/**
	 * 【2026.07.12 P0 重构 + v40.4 原子化】删除 AI 攻击节流字段 — 已迁移到 AIAttackComponent
	 *
	 * 历史 (v22-v40.3):
	 *   - LastAIAttackTimeSeconds (v40.4 删除 - 重复架构, 改 BT 决策)
	 *   - CachedAIMontage / bIsWaitingForAIMontageCallback
	 *     都是 BaseCharacter 上的 AI 攻击状态字段
	 *
	 * 大厂原则 (Phase 2.2 + v40.4 落地):
	 *   - 真理源迁移到 AIAttackComponent
	 *   - BaseCharacter 不再持有副本, 完全委托给 AIAttack
	 *   - v40.4: AIAttackComponent 内也不再有 LastAIAttackTimeSeconds — 单一节流点 = BT
	 *   - 字段已彻底删除 (cpp 中已无引用)
	 */


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
	// 武器装备/生成接口 (单一真理源 v36)
	// ==========================================
public:
	/**
	 * 【v36 大厂重构】武器生成请求 — 唯一公开入口
	/**
	 * 【v54.3 大厂架构重构 — Class 强类型, 跳过 DT_WeaponInfo】武器生成请求入口
	 *
	 * 签名变更 (v54.3):
	 *   - 旧 (v54.2): RequestWeaponSpawn(FString WeaponID) — 字符串 + DT_WeaponInfo 反查
	 *   - 新 (v54.3): RequestWeaponSpawn(TSubclassOf<ABaseWeapon> WeaponClass) — 强类型
	 *
	 * 用户原话 2026.07.16:
	 *   "DefaultWeaponRowName 属性为什么不直接引用 DT_WeaponInfo 表的 WeaponBlueprint 内容啊"
	 *
	 * 调用方:
	 *   - AMeleeAIController::SetupMeleeAI (关卡预放 AI, 读 Config.LevelPlacedWeaponClass.LoadSynchronous())
	 *   - URoomSpawnSubsystem::SpawnAIInternal (大厅入队 AI 战斗 Spawn, 拿 Request.WeaponID 查 DT 拿 Class)
	 *   - URoomSpawnSubsystem::HandlePlayerRequestSpawn (玩家 Spawn)
	 *
	 * 内部走 WeaponAttachmentComponent::RequestWeaponSpawn 复用完整链路:
	 *   1. 查挂载配置 (DT_WeaponAttachmentConfig: PawnClass + WeaponClass → Socket/Location/Rotation)
	 *   2. SpawnActor + AttachToComponent + Replicated
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat|AI")
	void RequestWeaponSpawn(TSubclassOf<ABaseWeapon> WeaponClass);


	// ==========================================
	// 公开获取当前武器的接口，给动画系统用
	// ==========================================
	/**
	 * 获取当前装备的武器
	 *
	 * 【2026.07.12 P0 重构】转发壳 — 实际逻辑在 WeaponAttachmentComponent::GetCurrentWeapon
	 *
	 * BP 调用方式 (3 选 1):
	 *   方式 1 (推荐): 在 BP 节点图的 self 引脚上拖出 "Get Current Weapon" (此 BlueprintPure 转发壳)
	 *   方式 2: self → Get Components By Class(WeaponAttachmentComponent) → Get Current Weapon
	 *   方式 3: 缓存 WeaponAttach 引用 → GetCurrentWeapon 组件函数 (最高效)
	 *
	 * 注: BlueprintPure 让 BP 中可以直接拖成纯函数节点 (绿色), 不需要 exec 引脚
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Combat")
	class ABaseWeapon* GetCurrentWeapon() const;

	/**
	 * 【v93 大厂架构】获取母体攻击蒙太奇 (Pawn 真理源, 母体左键攻击使用)
	 *
	 * 大厂原则 — 单一真理源:
	 *   - 直接读 this->MotherAttackMontage 字段 (蓝图配置)
	 *   - 不从 WeaponAttachment 派生 (母体无武器, 武器驱动路径不适用)
	 *   - 不走 Get/Resolve component 路径 (字段在 Actor 上, 不在 Component)
	 *
	 * 调用方:
	 *   - UPlayerComboComponent::LightAttack_Pressed (母体分支)
	 *   - UAIAttackComponent::Multicast_PlayAttackAnim_Implementation (母体分支)
	 *
	 * 返回值语义:
	 *   - 非 nullptr: BP_MuTi 已配 MotherAttackMontage
	 *   - nullptr:    BP_MuTi 未配 → PlayerComboComponent 会拒绝攻击 + Log Error (零兜底)
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Combat|Mother")
	UAnimMontage* GetMotherAttackMontage() const;

	/**
	 * 【v93.2 大厂架构 — 母体复用 Melee 缝合算法】启动母体攻击 trace
	 *
	 * 复用动机 (用户需求 2026.07.25):
	 *   - 复用 ANS_MeleeTraceState 类 (美术不学新标签)
	 *   - 复用 MeleeSwStrategy::TickDetection BoxTrace 缝合算法 (零重复)
	 *   - 母体没有武器 → 不能走 Weapon->StartDamageTrace
	 *   - 母体路径: Mesh = GetMesh(), Socket = TraceStart_Mother / TraceEnd_Mother
	 *   - 命中: 调 Server_ReportMotherAttackHit (走变母体逻辑)
	 *
	 * 职责:
	 *   - 创建/持有独立的 MeleeSwStrategy 实例 (Strategy 模式 — 不在 BP_MuTi 持字段)
	 *   - Tick 时调 Strategy.TickDetection (Strategy 内部驱动 BoxTrace)
	 *
	 * 调用方:
	 *   - UANS_MeleeTraceState::NotifyBegin (EnterState=Tracing + bIsMother=true 分支)
	 *
	 * @param bIsHeavy 是否重击 (策划/美术在蒙太奇时间轴上配 ANS 的 bIsHeavy 字段)
	 * @return true=成功启动, false=配置错/状态错
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat|Mother")
	bool StartMotherTrace(bool bIsHeavy);

	/**
	 * 【v93.2 大厂架构】停止母体攻击 trace (与 StartMotherTrace 对称)
	 *
	 * 幂等: 多次调用 no-op
	 *
	 * 调用方:
	 *   - UANS_MeleeTraceState::NotifyEnd (bIsMother=true 分支)
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat|Mother")
	void StopMotherTrace();

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
	 * 【v54.3 DEPRECATED — 完全删除】SpawnAndEquipWeapon 重复接口
	 *
	 * 历史:
	 *   - v36: 标记 DEPRECATED, 转发到 RequestWeaponSpawn
	 *   - v54.3: 完全删除 (重构后签名变成 Class, 旧 FString 版本无人调用, 强类型签名是单一真理源)
	 *
	 * 旧调用方若有残留编译错, 改为调 RequestWeaponSpawn(TSubclassOf<ABaseWeapon>)
	 */

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
	 *
	 * 【2026.07.12 P0 重构】转发壳 — 实际逻辑在 CharacterIconComponent::RefreshCharacterIcon
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
	 *
	 * 【2026.07.12 P0 重构】UFUNCTION(Client, Reliable) 仍保留在 BaseCharacter 上 (RPC 必须在 Actor 上),
	 *                      实现转发到 CharacterIconComponent::Client_RefreshCharacterIcon_Implementation
	 */
	UFUNCTION(Client, Reliable)
	void Client_RefreshCharacterIcon(const FString& InCharacterID, class UTexture2D* Avatar);

	/**
	 * 客户端 RPC — 接收武器图标数据 (服务器查好 Icon 直接 RPC 推给客户端)
	 *
	 * 【v40.1 P0 新增】镜像 Client_RefreshCharacterIcon
	 *
	 * 服务器调 RefreshWeaponIconOnHUD → 查表 → Owner->Client_RefreshWeaponIcon(WeaponID, Icon)
	 * 客户端收到 → 转发到 CharacterIconComponent::Client_RefreshWeaponIcon_Implementation
	 *
	 * 大厂原则 - RPC 必须在 Actor 上:
	 *   - UFUNCTION(Client, Reliable) 必须在 Actor 上, 不能在 Component 上
	 *   - 服务器本地 (ListenServer) 自动走本地 Implementation 调用, 不走 RPC 序列化
	 *   - 远端客户端的 Pawn 走 RPC 序列化, 收到后调 Implementation
	 */
	UFUNCTION(Client, Reliable)
	void Client_RefreshWeaponIcon(const FString& InWeaponID, class UTexture2D* Icon);

	/**
	 * 客户端 RPC — 接收武器弹药数据 (服务器查好 CurrentAmmo/MagazineSize/ReserveAmmo 直接 RPC 推给客户端)
	 *
	 * 【v85.3 P0 新增】镜像 Client_RefreshWeaponIcon
	 *
	 * 服务器调 (Server_SpawnAllWeapons / Server_SwitchToWeaponSlot 末尾)
	 *   → Owner->Client_RefreshWeaponAmmo(CurrentAmmo, MagazineSize, ReserveAmmo)
	 * 客户端收到 → 转发到 CharacterIconComponent::Client_RefreshWeaponAmmo_Implementation
	 *
	 * 大厂原则 - RPC 必须在 Actor 上:
	 *   - UFUNCTION(Client, Reliable) 必须在 Actor 上, 不能在 Component 上
	 *   - 服务器本地 (ListenServer) 自动走本地 Implementation 调用, 不走 RPC 序列化
	 *   - 远端客户端的 Pawn 走 RPC 序列化, 收到后调 Implementation
	 *
	 * 镜像对称原则 (v40.1/v40.2 已落地, v85.3 弹药链路必须同样走 RPC):
	 *   - 武器图标真理源 = 服务器查 DT_WeaponInfo, RPC 推 Icon
	 *   - 武器弹药真理源 = 服务器读 WeaponFireComponent.CurrentAmmo/MagazineSize/ReserveAmmo, RPC 推数据
	 *   - 客户端不"自己读武器组件再广播" (因为 CurrentWeapon 还没复制过来时, 读到默认值 1/1, HUD 显示错误)
	 */
	UFUNCTION(Client, Reliable)
	void Client_RefreshWeaponAmmo(int32 CurrentAmmo, int32 MagazineSize, int32 ReserveAmmo);

	/**
	 * HUD 未就绪时的延迟重试回调
	 *
	 * 【2026.07.12 P0 重构】转发壳 — 实际逻辑在 CharacterIconComponent::RetryRefreshCharacterIcon
	 */
	void RetryRefreshCharacterIcon();

	/**
	 * 刷新武器图标到 HUD（从 PlayerSpawnDataCache 读取 WeaponID）
	 *
	 * 【2026.07.12 P0 重构】转发壳 — 实际逻辑在 CharacterIconComponent::RefreshWeaponIconOnHUD
	 */
	void RefreshWeaponIconOnHUD();


	// ==========================================
	// 计分板系统 (Scoreboard)
	// ==========================================
public:
	/**
	 * 服务器广播击杀信息给所有客户端（用于更新 HUD 击杀信息栏）
	 *
	 * 【v31.6 大厂架构重构】从 AActor* 引用改为纯数据
	 *
	 * 历史痛点 (v22 及之前):
	 *   - 原签名 Multicast_NotifyKill(AActor* Victim, AActor* Assistant)
	 *   - 客户端收到 RPC 时, Victim 可能已被 Destroy (Server 端 TakeDamage → Die() → Destroy 同步链路)
	 *   - 客户端 Implementation 第 3248 行 VictimActor->GetName() 解 nullptr 崩溃
	 *   - EXCEPTION_ACCESS_VIOLATION 0x18 (结构体内 3rd 指针成员偏移, 完全可重现)
	 *
	 * 大厂原则 (v31.6) — RPC 边界纯数据化:
	 *   - RPC 参数应该是 POHOs (Plain Old Data), 序列化时不存在生命周期问题
	 *   - 任何 Actor 引用跨 RPC 边界都需 IsValid 校验, 否则埋雷
	 *   - 不在 RPC 边界"先传指针再解析字段", 而是"服务器本地读好后直接传字符串"
	 *
	 * @param KillerName  击杀者姓名 (服务器本地读 PlayerState->GetPlayerName())
	 * @param VictimName  被击杀者姓名
	 * @param KillMethod  击杀方式 (服务器本地读 Weapon->GetLastKillMethod())
	 * @param bIsAssist   true=这是助攻消息, false=普通击杀
	 */
	UFUNCTION(NetMulticast, Reliable)
	void Multicast_NotifyKill(const FString& KillerName, const FString& VictimName,
	                          EKillMethod KillMethod, bool bIsAssist, bool bIsKillerPlayer);

	/**
	 * 获取击杀者 PlayerState
	 */
	UFUNCTION(BlueprintCallable, Category = "Scoreboard")
	class ARoomPlayerState* GetRoomPlayerState() const;

	// ============================================================
	// 【v60.11 大厂架构】武器射线方向服务 — 单一真理源
	// ============================================================

	/**
	 * GetAimRayFromCrosshairOrEyes — 武器射线检测的方向入口
	 *
	 * 大厂原则 — Camera-based 路由策略 (v60.12):
	 *   - 本地玩家: 不走本函数 (v60.13 改为 RangedLineStrategy 直接用 PlayerCameraManager)
	 *   - AI: 走 Controller 准星方向 (GetBaseAimRotation, BT 控制的旋转)
	 *     → BT 的 FaceTarget 让 AIController 旋转, BaseAimRotation 反映准星方向
	 *
	 * 调用方:
	 *   - AI: RangedLineStrategy (v60.12 已改用 BaseAimRotation)
	 *   - 玩家: 不走本函数, Strategy 直接用 PlayerCameraManager
	 *
	 * @param OutRayOrigin     输出: 射线起点 (cm, 世界坐标) — 不输出, 由调用方决定
	 * @param OutRayDirection  输出: 射线方向 (单位向量, 已归一化)
	 * @return true=成功, false=获取方向失败 (Log Error)
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat|Aim")
	bool GetAimRayFromCrosshairOrEyes(FVector& OutRayOrigin, FVector& OutRayDirection);

	/**
	 * GetCrosshairWorldDirection_Const — 纯 const 查询 (仅供真正 const 上下文调用)
	 *
	 * 大厂原则 — const 正确性:
	 *   - 当调用方处于 const 上下文 (e.g. inline 渲染函数 / 多播 RPC 实现),
	 *     用本查询版 — 不缓存字段, 每次走完整解析链
	 *   - 性能开销: 0.001ms/次 (PlayerController->GetHUD 链是 UE 缓存, cheap)
	 *
	 * @return true=成功 (OutWorldDirection 已写入), false=HUD/Crosshair 未就绪
	 */
	UFUNCTION(BlueprintCallable, Category = "Combat|Aim")
	bool GetCrosshairWorldDirection_Const(FVector& OutWorldDirection) const;

private:
	/**
	 * HUD 还未就绪时延迟刷新的辅助方法
	 */
	void RetryRefreshHUD();

protected:
	/**
	 * 从 CharacterDataTable 查指定角色 ID 的头像贴图
	 *
	 * 【2026.07.12 P0 重构】转发壳 — 实际逻辑在 CharacterIconComponent::GetCharacterAvatarFromTable
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
