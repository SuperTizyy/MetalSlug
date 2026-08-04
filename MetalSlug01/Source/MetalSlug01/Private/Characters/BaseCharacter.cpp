// 版权声明：在项目设置的描述页面填写您的版权信息。

// ==========================================
// 头文件包含区
// ==========================================
// 引入本类头文件
#include "Characters/BaseCharacter.h"

#include "GenericTeamAgentInterface.h"

// 引入 DrawDebug 函数 (v82+ 客户端预测线)
#include "DrawDebugHelpers.h"

// 引入摄像机组件
#include "Camera/CameraComponent.h"

// 引入胶囊体组件
#include "Components/CapsuleComponent.h"

// 引入 PhysicsAsset (UE_LOG 中需要 GetPhysicsAsset()->GetName(), 需要完整类型)
#include "PhysicsEngine/PhysicsAsset.h"

// 引入 Net/UnrealNetwork.h（DOREPLIFETIME 宏的来源）
#include "Net/UnrealNetwork.h"

// 引入增强输入组件
#include "EnhancedInputComponent.h"

// 引入增强输入本地玩家子系统
#include "EnhancedInputSubsystems.h"

// 引入弹簧臂组件
#include "GameFramework/SpringArmComponent.h"
// 引入武器基类，以便调用武器的接口
#include "GameFramework/CharacterMovementComponent.h"
#include "TimerManager.h" // 为了使用定时器
#include "Weapons/BaseWeapon.h"
// 【v93.2 大厂架构新增】母体复用 Melee 缝合算法 (MeleeSwStrategy::StartMotherTrace/StopMotherTrace)
#include "Weapons/MeleeSwStrategy.h"
// 【v93.2 大厂架构新增】模式校验 — ARoomGameState::CurrentMatchMode (真理源)
#include "Systems/RoomGameState.h"
// 【P0 2026.07.06 大厂架构重构】引入 AI 攻击蒙太奇解析器 (职责链兜底)
// 背景: BP 配置错误 (LightAttackMontages 数组越界) 直接导致 GetAttackMontage 返回 null
//       旧版硬路径 → AI 不攻击 → 玩家看到"AI 跟着我不打"
// 新版: 走解析器职责链, 多层兜底 + 报警日志
#include "Combat/AIAttackMontageResolver.h"
// 【P0 2026.07.06】OnAIAttackMontageEnded 需要通知 AIController 解锁
// 引入 BaseAIController.h 以便 Cast<ABaseAIController>
#include "Systems/BaseAIController.h"
#include "UI/MyGameHUD.h"
#include "UI/Game/GameHUDWidget.h"
#include "Systems/Core/RoomPlayerState.h"
#include "Systems/RoomGameState.h"
#include "Systems/RoomGameMode.h"
#include "Systems/RoomPlayerController.h"
#include "Systems/Spawn/RoomSpawnSubsystem.h" // 【v133.5.1】HandleSlowStateChanged 读 PlayerConfigAsset 直指针
#include "Data/Config/PlayerConfigAsset.h"     // 【v133.5.1】玩家路径真理源 (MotherSlowSpeed / MaxWalkSpeed / MotherMaxWalkSpeed)
#include "Data/AI/AIBehaviorConfigSO.h"        // 【v133.5.1】AI 路径真理源 (MotherSlowSpeed)
// 【v110 P0 修复】引入 Controller 完整定义以做 IsA<APlayerController>() 类型判定
//   旧版用 IsLocallyControlled() 误判 ListenServer 上 AI Pawn 为本地玩家
#include "GameFramework/PlayerController.h"
#include "GameFramework/Controller.h"
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"           // v76 — 武器切换音效 (USoundBase 完整定义)
#include "Components/AudioComponent.h"  // v100.1 — 回血音组件 (UAudioComponent 完整定义)
#include "Weapons/WeaponSlotType.h"    // v76 — EWeaponSlotType (LexToString)
#include "Data/Tables/WeaponTableRow.h"
#include "Data/Tables/CharacterTableRow.h"
#include "Data/Faction/FactionTags.h" // 【2026.07.10 P0 重构】阵营集中定义

// 【v132 2026.08.02 新增】 IAISightTargetInterface 实现 (UE 官方 AI 视野协议)
//
// 业界共识 — UE 官方 + Lyra 推荐:
//   - 自定义视线检测必须 override `IAISightTargetInterface::CanBeSeenFrom`
//   - AI 视野系统每次检测时自动调用此 override
//   - Trace 必须命中目标才能 Visible (业界共识方案: trace 多个 Pawn 关键点)
#include "Perception/AISightTargetInterface.h"  // IAISightTargetInterface / FCanBeSeenFromContext
#include "Perception/AISense_Sight.h"            // UAISense_Sight::EVisibilityResult
#include "Engine/World.h"                        // LineTraceSingleByChannel

// 【2026.07.12 P0 大厂架构 Phase 2】5 个新 ActorComponent 头文件 (转发壳依赖)
// 注意: 头文件本身由别的 agent 编写 (可能在 PIE 同时编译), 这里只前向声明 + 假设依赖
// 如果某个 Component 头未到位, 注释掉对应 CreateDefaultSubobject 行即可编译
#include "Combat/PlayerComboComponent.h"   // 玩家连击
#include "Combat/AIAttackComponent.h"      // AI 攻击
#include "Combat/CombatDeathComponent.h"   // 战斗死亡
#include "Combat/WeaponAttachmentComponent.h" // 武器装备
#include "Combat/CharacterIconComponent.h"  // 头像/武器图标
#include "Combat/MotherSpawnParticleComponent.h"  // 【v99.1 新增】母体出生粒子
#include "Combat/MotherLowHealthFlickerComponent.h"  // 【v101 新增】母体残血虚弱闪烁 (与 InvincibilityFlickerComponent 完全对称)
#include "Combat/MotherSkillComponent.h"    // 【v119 新增】母体加速技能

// ==========================================
// 1. 构造函数
// ==========================================

/**
 * ABaseCharacter 构造函数
 *
 * 目的: 创建组件、初始化属性、设置默认值
 * 关键:
 * 1. 开启网络同步
 * 2. 搭建第三人称摄像机（弹簧臂 + 跟随相机）
 * 3. 调整骨骼网格对齐胶囊体
 * 4. 初始化战斗属性（血量/能量/AC/ACE）
 * 5. 初始化回复系统
 * 6. 初始化连击系统状态
 * 7. 开启 NavAgent 下蹲权限
 * 8. 设置下蹲时走路速度
 */
ABaseCharacter::ABaseCharacter()
{
	UE_LOG(LogTemp, Log, TEXT("[ABaseCharacter] 构造函数 ENTER: this=%p Class=%s"), this, *GetClass()->GetName());

	PrimaryActorTick.bCanEverTick = true;

	// 极其关键: 开启角色的网络同步
	bReplicates = true;

	// ==========================================
	// 第三人称摄像机系统搭建
	// ==========================================
	// 1. 创建弹簧臂（自拍杆）
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->TargetArmLength = 300.0f; // 摄像机距离角色的距离（可以随时在蓝图里调）
	CameraBoom->bUsePawnControlRotation = true; // 弹簧臂跟随鼠标转动

	// 【v85.x 删除相机阻尼】让鼠标控制更跟手，零延迟

	// 2. 创建跟随摄像机
	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName); // 把相机挂在弹簧臂的末端
	FollowCamera->bUsePawnControlRotation = false; // 相机本身不转，跟着弹簧臂转就行

	// ==========================================
	// 3. 业务组件 (改造: 从内联字段抽离为 ActorComponent)
	// ==========================================
	// 优势: 业务逻辑独立测试, 可被多个角色类复用, 单元可被替换
	HealthComponent   = CreateDefaultSubobject<UHealthComponent>(TEXT("HealthComponent"));
	EnergyComponent   = CreateDefaultSubobject<UEnergyComponent>(TEXT("EnergyComponent"));
	DissolveComponent = CreateDefaultSubobject<UDissolveComponent>(TEXT("DissolveComponent"));
	FootstepComponent = CreateDefaultSubobject<UFootstepComponent>(TEXT("FootstepComponent"));
	CharacterEvents  = CreateDefaultSubobject<UCharacterEvents>(TEXT("CharacterEvents"));
	HealthRegenComponent = CreateDefaultSubobject<UHealthRegenComponent>(TEXT("HealthRegenComponent"));

	// 【v40.8 P0 大厂架构】复活无敌期视觉闪烁组件
	//   - 数据/视觉分离: HealthComponent 持数据, 本组件持视觉 (MID 操作)
	//   - 协议: 身体材质蓝图必须有 "FlickerAmount" 标量参数
	//   - 启动时: 订阅 HealthComponent->OnInvincibilityChanged
	//   - 激活时: 派发 OnFlickerStarted → BP 子类 Timeline 驱动
	FlickerComponent = CreateDefaultSubobject<UInvincibilityFlickerComponent>(TEXT("FlickerComponent"));

	// 【v101 大厂架构新增】母体残血虚弱闪烁组件 — 与 FlickerComponent 完全对称
	//   - 业务规则 (用户 2026.07.27): 母体被打到残血 (≤ 100) → 启动虚弱闪烁; 回血 (> 100) → 停止
	//   - 触发源: HealthComponent->OnHealthChanged 订阅 + 阈值检测
	//   - 数据/视觉分离: HealthComp 持数据, 本组件持视觉 (MID 操作)
	//   - 协议: 身体材质蓝图必须有 "FlickerAmount" 标量参数 (与 FlickerComponent 共享)
	//   - 不破坏刀战模式: 刀战 BP 阈值场景不触发, 母体 BP 默认 100.0 触发
	//   - 零重复: 复用 InvincibilityFlickerComponent 的协议 / PrepareMaterials / Slot 过滤
	MotherLowHealthFlicker = CreateDefaultSubobject<UMotherLowHealthFlickerComponent>(TEXT("MotherLowHealthFlicker"));

	// 【v133.5 大厂架构新增】母体被主武器击中后降速组件 — 与 InvincibilityFlickerComponent 完全对称
	//   - 业务规则 (用户 2026.08.02): 主武器击中母体 → 降速到 MotherSlowSpeed (默认 100) → 2 秒后恢复
	//   - 触发源: CombatDeathComponent::TakeDamage 末尾 (主武器 + 母体 判定)
	//   - 数据/状态分离: Component 持状态 (bIsSlowed + SlowExpiresAtWorldTime), BaseCharacter 持表现 (MaxWalkSpeed)
	//   - 状态字段 Replicated (镜像 Invincibility): 服务器写 + 客户端 OnRep 同步
	//   - 派发 OnSlowStateChanged → BaseCharacter 订阅 → 改 MaxWalkSpeed
	//   - 不破坏刀战模式: 刀战没母体, 永远不触发
	MotherSlowComponent = CreateDefaultSubobject<UMotherSlowComponent>(TEXT("MotherSlowComponent"));

	// 【v121 大厂架构新增】母体加速技能组件
	//   - 所有角色都挂载 (零架构分支), 非母体静默 no-op
	//   - 职责: 技能状态数据权威 (bIsSkillActive, SkillCooldownEndTime) + 事件广播
	//   - 触发: 玩家按 IA_MotherSkill → UseSkill → ActivateSkill
	//   - 不破坏刀战模式: 刀战 AI 挂载但无 InputAction 绑定 → UseSkill 静默 no-op
	MotherSkillComponent = CreateDefaultSubobject<UMotherSkillComponent>(TEXT("MotherSkillComponent"));

	// ==========================================
	// 【2026.07.12 P0 大厂重构 Phase 2】5 个业务组件 (BaseCharacter 拆分)
	// ==========================================
	// 拆分动机: BaseCharacter.cpp 已 3957 行, 单文件维护成本爆炸.
	//          按业务职责拆分到独立 ActorComponent, BaseCharacter 改为转发壳.
	//
	// 拆分说明:
	//   PlayerCombo   ← 玩家连击状态机 (Phase 2.1)
	//   AIAttack      ← AI 攻击子系统 (Phase 2.2)
	//   CombatDeath   ← 战斗死亡/无敌期 (Phase 2.3)
	//   WeaponAttach  ← 武器装备/挂载 (Phase 2.4)
	//   CharacterIcon ← 角色头像/武器图标刷新 (Phase 2.5)
	PlayerCombo     = CreateDefaultSubobject<UPlayerComboComponent>(TEXT("PlayerCombo"));
	AIAttack        = CreateDefaultSubobject<UAIAttackComponent>(TEXT("AIAttack"));
	CombatDeath     = CreateDefaultSubobject<UCombatDeathComponent>(TEXT("CombatDeath"));
	WeaponAttach    = CreateDefaultSubobject<UWeaponAttachmentComponent>(TEXT("WeaponAttach"));
	CharacterIcon   = CreateDefaultSubobject<UCharacterIconComponent>(TEXT("CharacterIcon"));

	// 【v99.1 大厂架构新增】母体出生粒子特效组件
	//   - 所有角色都挂载(零架构分支), 刀战 BP 不配 MotherSpawnParticleSystem → 收到 RPC 时组件内部拒绝
	//   - 实际触发由 Multicast_PlayMutationFX 统一分发, 不走 OnRep
	MotherSpawnParticle = CreateDefaultSubobject<UMotherSpawnParticleComponent>(TEXT("MotherSpawnParticle"));

	// 【v99.3 大厂架构新增】母体出生音效组件
	//   - 与 MotherSpawnParticle 完全对称: 所有角色都挂载, 刀战 BP 不配 MotherSpawnSound → 收到 RPC 时组件内部拒绝
	//   - 实际触发由 Multicast_PlayMutationFX 统一分发, 不走 OnRep
	MotherSpawnSound = CreateDefaultSubobject<UMotherSpawnSoundComponent>(TEXT("MotherSpawnSound"));

	// 【v100 大厂架构新增】击杀音效组件 — 通用
	//   - 所有角色都挂载(零架构分支), 无论玩家 / AI / Mother / 普通角色
	//   - 触发由 Multicast_NotifyKill 统一分发 — 复用现有 RPC(零新建)
	//   - 数据驱动: GameMode.KillStreakIconDataTable 查表(与 HUD 连杀图标共享)
	//   - 不破坏刀战模式: 刀战也能正常播击杀音效
	KillSound = CreateDefaultSubobject<UKillSoundComponent>(TEXT("KillSound"));

	// 【v93.2 大厂架构 — 母体复用 Melee 缝合算法】母体攻击 Strategy
	//   - 复用 UMeleeSwStrategy 算法 (零重复架构), 但路径走 Owner Mesh (而非武器 Mesh)
	//   - 每个 Pawn 一份 Strategy (IgnoreActors / LastFrame 状态不能共享)
	//   - CreateDefaultSubobject 创建 — 默认所有角色都持有, 不论刀战/生化 (刀战路径不调用, 0 影响)
	//   - bUseOwnerMesh 默认 false (StartMotherTrace 内置 true)
	MotherTraceStrategy = CreateDefaultSubobject<UMeleeSwStrategy>(TEXT("MotherTraceStrategy"));

	UE_LOG(LogTemp, Log, TEXT("[ABaseCharacter] 构造函数 EXIT: this=%p Class=%s WeaponAttach=%p PlayerCombo=%p AIAttack=%p CombatDeath=%p CharacterIcon=%p HealthComp=%p MotherTraceStrategy=%p MotherSpawnParticle=%p MotherSpawnSound=%p KillSound=%p MotherLowHealthFlicker=%p MotherSlowComponent=%p MotherSkillComponent=%p"),
		this, *GetClass()->GetName(), WeaponAttach, PlayerCombo, AIAttack, CombatDeath, CharacterIcon, HealthComponent, MotherTraceStrategy.Get(), MotherSpawnParticle, MotherSpawnSound, KillSound, MotherLowHealthFlicker, MotherSlowComponent, MotherSkillComponent);

	// ==========================================
	// 角色模型设置
	// ==========================================
	// 【关键修改】: 允许玩家自己看到自己的全身模型
	GetMesh()->SetOwnerNoSee(false);
	GetMesh()->SetRelativeLocation(FVector(0.f, 0.f, -90.f)); // 往下挪一点，对齐胶囊体底盘
	GetMesh()->SetRelativeRotation(FRotator(0.f, -90.f, 0.f)); // 把脸转到正前方

	// ==========================================
	// 【v110 P0 终极修复】Collision — 强制所有 ABaseCharacter 派生 BP 都正确响应 ECC_Visibility
	// ==========================================
	//
	// 【真根因 — v99.3 修复不完整,v110 完整修复】
	//   v99.3: SetCollisionResponseToChannel(ECC_Visibility, ECR_Block) ✅
	//   v99.3 漏掉 2 个关键点 (本次 v110 补全):
	//     (a) 没设 SetCollisionEnabled(QueryAndPhysics)
	//         → UE 5.6 ACharacter Mesh 默认 QueryOnly (够),但 BP_MuTi 可能设成 NoCollision → 射线穿过
	//     (b) 没设 SetCollisionResponseToAllChannels(ECR_Block)
	//         → 默认 Channel 设置可能被 BP 单 Channel 覆盖 → 部分 Channel Ignore → 射线穿过
	//
	// 【注】USkeletalMeshComponent 没有 bUseComplexAsSimpleCollision 字段
	//   - 该字段属于 UStaticMeshComponent (静态网格才有此概念)
	//   - USkeletalMesh 默认就是 complex collision — 只要 CollisionEnabled != NoCollision
	//     + bTraceComplex=true → Mesh 三角面自动参与 trace
	//
	// 【用户反馈 (Session1.log 2026.07.30)】
	//   - 玩家打母体正常 (但这其实是用户的另一个测试场景,Session1.log 没体现)
	//   - AI 人类打玩家母体,射线穿过母体 → 命中身后墙 (LineTraceMulti bHit=true, 但 bHasPawnInTrace=false)
	//   - 用户明确: "AI 射线角度正确穿过了母体" — 不是方向问题,是 collision 问题
	//
	// 【大厂原则 — 真理源在 C++ 构造函数,不是 BP】
	//   - BP archetype 可能覆盖 C++ 默认 (v36/v38 反复踩坑)
	//   - 修复方案: 在构造函数**强制**设 CollisionEnabled + AllChannels Block
	//   - 所有 ABaseCharacter 派生 BP (BP_MuTi / BP_SWAT_C / BP_GruntAI) 自动受益
	//   - BP 蓝图即使把 Collision 设错,C++ 构造函数也强制覆盖回正确值
	//
	// 【为什么不是只设单 Channel Response】
	//   - 只设 ECC_Visibility Block → AllChannels 默认还是 1 Channel 1 Channel 设的状态
	//   - BP 子类可能在 BP 蓝图里单独设某个 Channel = Ignore → 射线绕过
	//   - 设 AllChannels=Block 后再单独 Ignore Camera → 简洁无歧义
	// ==========================================

	// (1) Mesh Collision — 全身三角面参与 trace (USkeletalMesh 默认 complex,无需 bUseComplexAsSimpleCollision)
	GetMesh()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);  // 【v110 P0】强制 Query 模式 (物理 Body + Mesh 三角面)
	GetMesh()->SetCollisionResponseToAllChannels(ECR_Block);             // 【v110 P0】所有通道默认 Block (再单独开放 Ignore)
	GetMesh()->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);  // 显式重申 (大厂零冗余 — 真理源明确)
	GetMesh()->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);    // 相机通道 Ignore (避免反射/穿透)
	// 注: USkeletalMeshComponent 没有 bUseComplexAsSimpleCollision 字段 (那是 UStaticMeshComponent 的)
	//     SkeletalMesh 默认就是 complex collision — CollisionEnabled=QueryAndPhysics 已足够 bTraceComplex=true 命中三角面

	// (2) Capsule Collision — 双保险 (Capsule 命中作为 fallback,即使 Mesh 三角面问题也不会穿过)
	GetCapsuleComponent()->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);  // 强制 Query 模式
	GetCapsuleComponent()->SetCollisionResponseToAllChannels(ECR_Block);             // 所有通道默认 Block
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block); // 显式重申
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);    // 相机通道 Ignore

	// 【v110 修复后预期效果】
	//   - 玩家射线 (bTraceComplex=true) → 命中母体 Mesh 三角面 (全身任何部位)
	//   - AI 射线 (bTraceComplex=true)    → 命中母体 Mesh 三角面 (全身任何部位)
	//   - 玩家/AI 走完全相同的 collision 路径 (大厂镜像 — 零差异)

	// 5. 【2026-06-15 重构】初始化战斗属性
	// 注: MaxHealth/CurrentHealth/bIsDead 已下沉到 HealthComponent
	//     MaxEnergy/CurrentEnergy 已下沉到 EnergyComponent
	// 业务组件初始化: 由各 Component 自治 (均已开启 Replicated)
	// HealthComponent 和 EnergyComponent 的初值在 CreateDefaultSubobject 时由 UPROPERTY 默认值决定
	// 如需自定义初值，可在蓝图子类中覆盖 UPROPERTY 的默认值

	// 【Phase 1 重构】阵营默认值: 默认人类阵营 (FGenericTeamId(0))
	// 外部旧代码可通过 SetFactionTag() 切换 (Human vs Zombie)
	// 注意: 不再初始化 TeamID 字段 (字段已废弃, 派生属性从 FactionTag 派生)

	// 初始化AC/ACE系统
	MaxAC = 100;
	ACValue = 0;
	ACEValue = 0;

	// 注: 回复系统状态已抽离到 HealthRegenComponent, 此处不再初始化字段

	// 6. 【2026.07.12 P0 重构】连击状态机字段已迁移到 PlayerComboComponent
	//    死亡字段已迁移到 CombatDeathComponent
	//    武器字段已迁移到 WeaponAttachmentComponent
	//    图标字段已迁移到 CharacterIconComponent
	//    这里不再初始化这些字段, 全部由 Component 自治

	// 【核心权限】: 告诉引擎底层的导航代理，这个角色可以下蹲
	// 如果不加这句，你按破键盘角色也不会蹲下
	GetCharacterMovement()->GetNavAgentPropertiesRef().bCanCrouch = true;

	// 设置下蹲时的走路速度 (比如 300)
	GetCharacterMovement()->MaxWalkSpeedCrouched = 300.0f;

	// 初始化脚步声默认音效（指向已有资源，后续可在蓝图覆盖）
}


// ==========================================
// 2. BeginPlay
// ==========================================

/**
 * ABaseCharacter::BeginPlay
 *
 * 在角色被初始化时调用
 * 【2026-06-15 重构】: 溶解材质初始化已迁移到 DissolveComponent::CollectDynamicMaterials
 */
void ABaseCharacter::BeginPlay()
{
	Super::BeginPlay();

	// 【v109 大厂架构】删除 v108 BeginPlay 的字符串匹配兜底
	//
	// 旧 (v108) 反模式 — 大厂原则被违反:
	//   if (GetClass()->GetName().Contains(TEXT("MuTi")) || GetClass()->GetName().Contains(TEXT("Mother")))
	//   {
	//       bIsMother = true;
	//       bIsHuman = false;
	//   }
	//
	// 为什么是反模式 (大厂原则):
	//   1. 用类名模糊匹配代替类型检查 (应该用 IsA<AZombieMotherBase> 之类)
	//   2. 类名改了就不工作 (脆弱, 不可维护)
	//   3. 掩盖根因: 关卡预放的 BP_MuTi 没走 MutatePawnToMother → 业务层跳过 → 状态字段缺失
	//   4. 与"单一真理源"原则冲突: bIsMother 真理源 = 业务层 MutatePawnToMother Step 5.7 写入,
	//      BeginPlay 不应该有自己的真理源写入
	//   5. **真正的兜底行为**: 如果 MutatePawnToMother 写漏了, BeginPlay 兜底 → 配置错误永远看不见
	//
	// 新 (v109) 零兜底 — 单一真理源:
	//   - 母体 Pawn 创建真理源 = URoomSpawnSubsystem::MutatePawnToMother Step 5.7 写 bIsMother
	//   - 关卡预放 BP_MuTi Pawn: 必须在 BP 蓝图细节面板**手动配** bIsMother=true (类型安全, 编译期可查)
	//   - 业务层调用 (MutateCharacterToMother 路径): MutatePawnToMother 自动写入 bIsMother=true
	//   - 复活链 (RequestRespawn → MutatePawnToMother): 同上, 自动写入
	//   - BeginPlay 不再干预 bIsMother (大厂原则 — 职责单一)
	//
	// 用户 UE 编辑器必做 (修复后, 关卡预放 BP_MuTi 需要):
	//   1. 打开 Content/Blueprints/Characters/BP_MuTi.uasset
	//   2. Class Defaults → Combat|Mother → 勾选 bIsMother (或者直接设 bIsMother=true)
	//   3. Compile + Save
	//   — 这是类型安全的配置, 不会因类改名失效, 不会掩盖业务层错误
	//
	// 不破坏刀战模式:
	//   - 刀战模式不调 MutatePawnToMother, 也没有 BP_MuTi, 这段代码对刀战模式 0 影响

	// 【2026-06-15 重构】: 订阅 HealthComponent 事件
	// 原因: 血量数据已下沉到 Component, BaseCharacter 通过事件转发来驱动 HUD 刷新
	// 服务器: Component->ApplyDamage/Heal 内部 Broadcast
	// 客户端: Component->OnRep_CurrentHealth 内部 Broadcast
	// 【v39 修复】用 Resolve 函数而非裸字段访问 (BP archetype null 字段防护)
	if (UHealthComponent* HC = ResolveHealthComponent())
	{
		HC->OnHealthChanged.AddUniqueDynamic(this, &ABaseCharacter::OnHealthChanged_Callback);

		// 【2026-07-01 新增】订阅死亡事件
		// 服务器: HealthComponent::ApplyDamage → OnDeath.Broadcast → OnHealthComponentDeath → Die
		// 客户端: HealthComponent::OnRep_bIsDead → OnDeath.Broadcast → OnHealthComponentDeath → ExecuteDeathLocal
		HC->OnDeath.AddUniqueDynamic(this, &ABaseCharacter::OnHealthComponentDeath);

		// 【2026.07.14 新增】订阅无敌期变化事件
		// 用于驱动 UI 层显示复活进度条
		HC->OnInvincibilityChanged.AddUniqueDynamic(this, &ABaseCharacter::OnHealthComponentInvincibilityChanged);
	}

	// 【2026-07-01 新增】: CharacterEvents 初始化检查
	// 【v39 修复】用 Resolve 函数而非裸字段访问
	if (!ResolveCharacterEvents())
	{
		UE_LOG(LogTemp, Error, TEXT("[BaseCharacter] CharacterEvents 组件未挂载!"));
	}

	// 【v133.5 大厂架构新增】订阅 MotherSlowComponent.OnSlowStateChanged
	//   - 触发源: CombatDeathComponent::TakeDamage 末尾 (主武器击中母体) → ActivateSlow
	//   - 表现: BaseCharacter 改 MaxWalkSpeed (走真理源: MotherSlowSpeed / MotherMaxWalkSpeed)
	//   - 服务器主动 Broadcast + 客户端 OnRep Broadcast = 双发保证 (与 Invincibility 镜像)
	//
	// 大厂原则 — 职责分离:
	//   - Component 只管"是否慢速"状态, 不动 MaxWalkSpeed (跨边界)
	//   - BaseCharacter 订阅事件 → 改 MaxWalkSpeed (知道 Movement 真实作用)
	if (UMotherSlowComponent* SlowComponent = ResolveMotherSlowComponent())
	{
		SlowComponent->OnSlowStateChanged.AddUniqueDynamic(this, &ABaseCharacter::HandleSlowStateChanged);
	}

	// 【v119 大厂架构新增】订阅 MotherSkillComponent.OnSkillActiveChanged
	//   - 触发源: BTTask_ActivateMotherSpeedBoost::ExecuteTask → ActivateSkill
	//   - 表现: BaseCharacter 改 MaxWalkSpeed = CachedBaseMaxWalkSpeed × SpeedMultiplier
	//   - 服务器主动 Broadcast + 客户端 OnRep Broadcast = 双发保证 (与 MotherSlow 镜像)
	//   - 不破坏刀战模式: 刀战 AI 没 MotherSkillComponent, 订阅失败 → Log Error (无害)
	if (UMotherSkillComponent* SkillComp = ResolveMotherSkillComponent())
	{
		SkillComp->OnSkillActiveChanged.AddUniqueDynamic(this, &ABaseCharacter::HandleMotherSkillStateChanged);
	}

	// 【v100.1 大厂架构】订阅 HealthRegenComponent 回血状态变化
	// 触发场景 (业务规则 2026.07.26 母体): 不被打 + 不移动 5 秒后, 服务器 Tick 检测到开始回血
	// 服务器路径: HealthRegenComponent::OnRegenStateChanged(true).Broadcast
	//             → HandleRegenStateChanged → Client_PlayRegenSound RPC (OwnerOnly) — 仅母体本人听到
	// 客户端路径: 收到 RPC → Client_PlayRegenSound_Implementation 创建循环音组件
	if (UHealthRegenComponent* HRC = ResolveHealthRegenComponent())
	{
		HRC->OnRegenStateChanged.AddUniqueDynamic(this, &ABaseCharacter::HandleRegenStateChanged);
	}
}


/**
 * ABaseCharacter::OnHealthChanged_Callback
 *
 * 【2026-06-15 新增】: HealthComponent->OnHealthChanged 事件回调
 * 用途: 血量变化时刷新 HUD (替代原 OnRep_Health)
 * 调用时机:
 *   - 服务器: HealthComponent::ApplyDamage / Heal 内部 Broadcast
 *   - 客户端: HealthComponent::OnRep_CurrentHealth 内部 Broadcast
 *
 * 【v39 修复】用 Resolve 函数而非裸字段访问 (BP archetype null 字段防护)
 *   根因: 旧版 `if (!CharacterEvents || !HealthComponent) return;`
 *         BP archetype 让字段 null → 不广播 → HUD 血量永远不更新 (Bug 2)
 *   修复: 调 Resolve 函数 lazily 找到组件 (永不返回 null 除非真没挂载)
 */
void ABaseCharacter::OnHealthChanged_Callback(float NewHealth)
{
	// 【2026-07-01 重构 v2】: 改用 CharacterEvents 广播, 替代直接 GameHUDWidget Push
	// 优势: UI 层自主订阅, BaseCharacter 不再依赖 GameHUDWidget
	UCharacterEvents* Events = ResolveCharacterEvents();
	UHealthComponent* HC = ResolveHealthComponent();
	if (!Events || !HC)
	{
		return;
	}
	Events->OnHealthChangedDelegate.Broadcast(NewHealth, HC->GetMax());
}


/**
 * ABaseCharacter::OnHealthComponentDeath
 *
 * 【2026-07-01 新增】HealthComponent->OnDeath 事件回调
 *
 * 架构升级: 死亡入口统一化
 *   旧架构 (依赖 TakeDamage 主动调 Die):
 *     - 任何绕过 TakeDamage 的死亡 (脚本伤害 / 自杀 / 调试命令) 都不会触发 Die
 *     - 客户端死亡完全依赖 Multicast_Die RPC, 网络抖动会丢
 *   新架构 (HealthComponent->OnDeath 事件总线):
 *     - 服务器: HealthComponent::ApplyDamage 内部 Broadcast → 本回调 → 走击杀结算 + Multicast_Die
 *     - 客户端: HealthComponent::OnRep_bIsDead 内部 Broadcast → 本回调 → 本地死亡流程
 *     - 任何修改 HealthComponent->bIsDead 的路径都会自动触发
 */
void ABaseCharacter::OnHealthComponentDeath()
{
	// 【v39 修复】用 Resolve 函数而非裸字段访问
	UHealthComponent* HC = ResolveHealthComponent();

	UE_LOG(LogTemp, Error, // 使用 Error 让日志更显眼
		TEXT("[BaseCharacter][OnHealthComponentDeath] ★★★ 触发死亡 ★★★: Pawn=%s HasAuth=%d HealthComponent=%p"),
		*GetName(), HasAuthority() ? 1 : 0, HC);

	if (!HC)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[BaseCharacter][OnHealthComponentDeath] FATAL: HealthComponent is null! Cannot proceed with death."));
		return;
	}

	if (HasAuthority())
	{
		// 服务器: 走完整流程 (击杀结算 + Multicast_Die + 复活定时器)
		UE_LOG(LogTemp, Error, TEXT("[BaseCharacter][OnHealthComponentDeath] 调用 Die()..."));
		Die();
	}
	else
	{
		// 客户端: 本地直接执行死亡流程 (无需 Multicast)
		// 注: Multicast_Die 也可能会到达, ExecuteDeathLocal 是幂等的 (CurrentWeapon 置空后第二次会跳过)
		UE_LOG(LogTemp, Error, TEXT("[BaseCharacter][OnHealthComponentDeath] 调用 ExecuteDeathLocal()..."));
		ExecuteDeathLocal();
	}
}


/**
 * ABaseCharacter::OnHealthComponentInvincibilityChanged 【2026.07.14 新增】
 *
 * HealthComponent 无敌期状态变化回调
 * 职责: 订阅 HealthComponent->OnInvincibilityChanged，广播到 CharacterEvents->OnInvincibilityChanged
 *       让 UI 层 (GameHUDWidget) 统一订阅显示复活进度条
 *
 * 设计动机:
 *   - HealthComponent->OnInvincibilityChanged 是服务器/客户端各自广播的本地事件
 *   - 需要一个统一的 CharacterEvents 事件让 UI 层统一订阅
 *   - 与 OnHealthChanged_Callback / OnHealthComponentDeath 模式一致
 */
void ABaseCharacter::OnHealthComponentInvincibilityChanged(bool bIsNowInvincible)
{
	UCharacterEvents* Events = ResolveCharacterEvents();
	if (!Events)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[BaseCharacter][OnHealthComponentInvincibilityChanged] CharacterEvents 未挂载! Pawn=%s"),
			*GetName());
		return;
	}

	// 广播到 CharacterEvents，让 UI 层统一订阅
	Events->OnInvincibilityChanged.Broadcast(bIsNowInvincible);

	UE_LOG(LogTemp, Display,
		TEXT("[BaseCharacter][OnHealthComponentInvincibilityChanged] ★ 无敌期变化: Pawn=%s bIsNowInvincible=%d"),
		*GetName(), bIsNowInvincible ? 1 : 0);
}


void ABaseCharacter::HandleSlowStateChanged()
{
	// ==========================================
	// 【v133.5.3 大厂架构 — 修复永远不消退】母体被主武器击中后改 MaxWalkSpeed
	// ==========================================
	//
	// 旧版 (v133.5.1) 痛点:
	//   - "还原" 路径用真理源优先 (PlayerConfig / ConfigSO)
	//   - 真理源缺失 → 拒绝还原 + Log Error → MaxWalkSpeed 永远 = slow speed
	//   - 用户场景: 母体被主武器击中 → 2s 后 Slow Timer 触发 → DeactivateSlow → Broadcast
	//            → HandleSlowStateChanged 还原路径 → 真理源缺失 → 拒绝还原 → 永远 = 100
	//
	// 大厂原则 — 零兜底 (v133.5.3 修复):
	//   - 不要拒绝还原 — 必须给最可靠的值 (即使真理源缺失)
	//   - 还原优先级: 玩家真理源 > 缓存 > 退化默认值 (UE 默认 600)
	//   - 真理源缺失 → Log Error 但仍然还原 (不让用户卡在永远 100)
	//
	// 设计原则 (镜像 InvincibilityFlickerComponent):
	//   - Component 持状态 (bIsSlowed), BaseCharacter 改 MaxWalkSpeed
	//   - 跨边界: Component 不知道 MovementComponent 存在
	//   - 服务器/客户端都执行 (双发保证, 同一份视觉逻辑)
	//
	// 真理源决策 (v133.5.3 修复后):
	//   - 慢速速度: 玩家 = PlayerConfigAsset.MotherSlowSpeed, AI = ConfigSO.MotherSlowSpeed
	//   - 原速度还原: 玩家路径优先 PlayerConfig 真理源 → 退化缓存 → 退化默认值
	//                AI 路径用缓存 → 退化默认值
	//
	// 状态决策:
	//   - bIsSlowed=true  → 改 MotherSlowSpeed (强制, 真理源) — 真理源缺失拒绝, **不还原**
	//   - bIsSlowed=false → 还原 (真理源优先 > 缓存 > 退化默认值) — 配置错也还原, 不卡死

	UMotherSlowComponent* SlowComp = ResolveMotherSlowComponent();
	if (!SlowComp)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[BaseCharacter][HandleSlowStateChanged] MotherSlowComponent 解析失败. Pawn=%s"),
			*GetName());
		return;
	}

	UCharacterMovementComponent* MoveComp = GetCharacterMovement();
	if (!MoveComp)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[BaseCharacter][HandleSlowStateChanged] CharacterMovement 为空. Pawn=%s"),
			*GetName());
		return;
	}

	const bool bIsSlowedNow = SlowComp->IsSlowed();

	// 慢速速度真理源: 玩家读 PlayerConfig, AI 读 ConfigSO
	float MotherSlowSpeedValue = 0.0f;
	bool bIsPlayerPath = false;

	if (Cast<APlayerController>(GetController()) != nullptr)
	{
		// 玩家路径: 读 PlayerConfigAsset.Spawn->PlayerConfigAsset 直指针
		if (URoomSpawnSubsystem* SpawnSys = URoomSpawnSubsystem::Get(this))
		{
			if (UPlayerConfigAsset* PC = SpawnSys->GetPlayerConfigAsset())
			{
				MotherSlowSpeedValue = PC->MotherSlowSpeed;
			}
		}
		bIsPlayerPath = true;
	}
	else if (ABaseAIController* BaseAIC = Cast<ABaseAIController>(GetController()))
	{
		// AI 路径: 读 PlayerConfigAsset.Spawn->PlayerConfigAsset (v120 统一)
		if (URoomSpawnSubsystem* SpawnSys = URoomSpawnSubsystem::Get(this))
		{
			if (UPlayerConfigAsset* PC = SpawnSys->GetPlayerConfigAsset())
			{
				MotherSlowSpeedValue = PC->MotherSlowSpeed;
			}
		}
	}

	// ==========================================
	// 真理源决策 — 慢速 vs 还原
	// ==========================================
	// 【v133.5.3 修复】永远给一个值 — 配置错也用最可靠的退化值
	float TargetSpeed = 0.0f;
	const TCHAR* Reason = TEXT("");

	if (bIsSlowedNow)
	{
		// 慢速中 → 强制 MotherSlowSpeed (真理源)
		// 真理源缺失 → 拒绝激活 (零兜底, 但这种情况还没发生慢速, 不会卡死)
		if (MotherSlowSpeedValue <= 0.0f)
		{
			UE_LOG(LogTemp, Error,
				TEXT("[BaseCharacter][HandleSlowStateChanged] 慢速速度真理源未配 MotherSlowSpeed=%.1f "
				     "(Pawn=%s, 真理源=%s). 【v133.5.3 修复】检查 PlayerConfigAsset 或 ConfigSO 配置. 不强行设 MaxWalkSpeed."),
				MotherSlowSpeedValue, *GetName(),
				bIsPlayerPath ? TEXT("PlayerConfigAsset") : TEXT("ConfigSO"));
			return;
		}
		TargetSpeed = MotherSlowSpeedValue;
		Reason = TEXT("Slowed (MotherSlowSpeed)");
	}
	else
	{
		// ==========================================
		// 【v133.5.3 修复】还原路径 — 永远给一个值, 拒绝 "永远不还原"
		// ==========================================
		// 真根因 (v133.5.1): 还原路径要求两个真理源都拿到才还原, 任意一个缺失 → 拒绝 → 永远 = slow speed
		// 修复: 三级退化, 永远有值
		//   1. 玩家路径真理源 (PlayerConfig.MaxWalkSpeed / MotherMaxWalkSpeed)
		//   2. Component 缓存 (SlowComp->GetCachedBaseMaxWalkSpeed)
		//   3. 退化默认值 (UE 默认 600.0f) — 真理源全错时使用, 保证角色能动
		// 真理源缺失 → Log Error 报告, 但仍然还原 (不让角色永远卡死)
		float RestoredSpeed = 0.0f;

		if (bIsPlayerPath)
		{
			// 玩家路径: 用 PlayerConfig 真理源分流 (母体 → MotherMaxWalkSpeed, 人类 → MaxWalkSpeed)
			if (URoomSpawnSubsystem* SpawnSys = URoomSpawnSubsystem::Get(this))
			{
				if (UPlayerConfigAsset* PC = SpawnSys->GetPlayerConfigAsset())
				{
					RestoredSpeed = bIsMother
						? PC->MotherMaxWalkSpeed
						: PC->MaxWalkSpeed;
				}
			}
		}

		if (RestoredSpeed > 0.0f)
		{
			// 玩家路径真理源拿到 → 直接用
			TargetSpeed = RestoredSpeed;
			Reason = TEXT("PlayerPath-Restored (PlayerConfig truth source)");
		}
		else
		{
			// AI 路径 / 玩家路径配置缺失 → 用 Component 缓存
			const float CachedSpeed = SlowComp->GetCachedBaseMaxWalkSpeed();
			if (CachedSpeed > 0.0f)
			{
				TargetSpeed = CachedSpeed;
				Reason = TEXT("Cache-Restored (CachedBaseMaxWalkSpeed)");
			}
			else
			{
				// 【v133.5.3 最终修复】全部退化都没了 → 用 UE 默认 600
				// 绝不容忍 "永远不还原" — 角色必须能动
				UE_LOG(LogTemp, Error,
					TEXT("[BaseCharacter][HandleSlowStateChanged] 还原速度真理源全缺失: 玩家路径=%s, 缓存=%.1f "
					     "(Pawn=%s). 【v133.5.3 修复】检查 PlayerConfigAsset.MaxWalkSpeed / ConfigSO 配置. "
					     "用退化默认值 600.0 防止 '永远不还原' bug."),
					bIsPlayerPath ? TEXT("PlayerConfig") : TEXT("ConfigSO(N/A)"),
					CachedSpeed, *GetName());
				TargetSpeed = 600.0f; // UE ACharacter 默认 MaxWalkSpeed
				Reason = TEXT("Fallback-Default (UE Default 600)");
			}
		}
	}

	// 应用目标速度
	MoveComp->MaxWalkSpeed = TargetSpeed;
	MoveComp->MaxWalkSpeedCrouched = TargetSpeed * 0.5f;

	UE_LOG(LogTemp, Display,
		TEXT("[BaseCharacter][HandleSlowStateChanged] ★ 慢速状态变化: Pawn=%s bIsSlowed=%d → MaxWalkSpeed=%.1f "
		     "(真理源=%s, SlowSpeed=%.1f, Reason=%s)"),
		*GetName(), bIsSlowedNow ? 1 : 0, TargetSpeed,
		bIsPlayerPath ? TEXT("PlayerConfig") : TEXT("ConfigSO"),
		MotherSlowSpeedValue, Reason);
}


// ==========================================
// 【v119 大厂架构新增】母体加速技能 — MaxWalkSpeed 修改
// ==========================================

void ABaseCharacter::HandleMotherSkillStateChanged(bool bIsNowActive)
{
	// ==========================================
	// 【v119 大厂架构】母体加速技能激活/结束时改 MaxWalkSpeed
	// ==========================================
	//
	// 触发链:
	//   BTTask_ActivateMotherSpeedBoost → MotherSkillComponent::ActivateSkill
	//     → OnSkillStateChanged.Broadcast → 本函数 (服务器 + 客户端)
	//
	// 大厂原则 - 与 HandleSlowStateChanged 完全对称:
	//   - MotherSlowComponent: bIsSlowed=true → MaxWalkSpeed = SlowSpeed
	//   - MotherSkillComponent: bIsSkillActive=true → MaxWalkSpeed = CachedBaseMaxWalkSpeed × SpeedMultiplier
	//
	// 大厂原则 - 零跨边界:
	//   - MotherSkillComponent 只管"技能状态" (bIsSkillActive, SpeedMultiplier)
	//   - BaseCharacter 订阅事件后改 MaxWalkSpeed (Component 不知道 MovementComponent)
	//
	// 网络模型:
	//   - 服务器: OnSkillStateChanged.Broadcast → 本函数执行 → 修改成功
	//   - 客户端: OnRep_SkillActiveChanged.Broadcast → 本函数执行 → 同一份修改
	//   - 双发保证 (与 MotherSlow 完全一致)

	UMotherSkillComponent* SkillComp = ResolveMotherSkillComponent();
	if (!SkillComp)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[BaseCharacter][HandleMotherSkillStateChanged] MotherSkillComponent 解析失败. Pawn=%s"),
			*GetName());
		return;
	}

	UCharacterMovementComponent* MoveComp = GetCharacterMovement();
	if (!MoveComp)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[BaseCharacter][HandleMotherSkillStateChanged] CharacterMovement 为空. Pawn=%s"),
			*GetName());
		return;
	}

	const bool bIsSkillActiveNow = SkillComp->IsSkillActive();

	if (bIsSkillActiveNow)
	{
		// 加速激活: MaxWalkSpeed = 缓存原速度 × SpeedMultiplier
		const float BaseSpeed = SkillComp->GetCachedBaseMaxWalkSpeed();
		const float SpeedMul = SkillComp->GetCurrentSpeedMultiplier();
		if (BaseSpeed <= 0.0f)
		{
			UE_LOG(LogTemp, Error,
				TEXT("[BaseCharacter][HandleMotherSkillStateChanged] 加速激活但 CachedBaseMaxWalkSpeed=%.1f (≤0, 未缓存). Pawn=%s. "
				     "【v119 零兜底】检查 BTTask_ActivateMotherSpeedBoost 是否正确调用 ActivateSkill(CurrentMaxWalkSpeed)."),
				BaseSpeed, *GetName());
			return;
		}

		MoveComp->MaxWalkSpeed = BaseSpeed * SpeedMul;
		MoveComp->MaxWalkSpeedCrouched = BaseSpeed * SpeedMul * 0.5f;

		UE_LOG(LogTemp, Display,
			TEXT("[BaseCharacter][HandleMotherSkillStateChanged] ★ 加速激活: Pawn=%s MaxWalkSpeed=%.1f (Base=%.1f × Mul=%.2fx)"),
			*GetName(), MoveComp->MaxWalkSpeed, BaseSpeed, SpeedMul);
	}
	else
	{
		// 加速结束: 还原到缓存原速度 (由 MotherSkillComponent 在激活时缓存)
		const float BaseSpeed = SkillComp->GetCachedBaseMaxWalkSpeed();
		if (BaseSpeed > 0.0f)
		{
			MoveComp->MaxWalkSpeed = BaseSpeed;
			MoveComp->MaxWalkSpeedCrouched = BaseSpeed * 0.5f;

			UE_LOG(LogTemp, Display,
				TEXT("[BaseCharacter][HandleMotherSkillStateChanged] ★ 加速结束还原: Pawn=%s MaxWalkSpeed=%.1f (CachedBase)"),
				*GetName(), BaseSpeed);
		}
		else
		{
			// 缓存丢失 (可能是冷却到期自动清缓存) → 用 UE 默认值
			MoveComp->MaxWalkSpeed = 600.0f;
			MoveComp->MaxWalkSpeedCrouched = 300.0f;

			UE_LOG(LogTemp, Error,
				TEXT("[BaseCharacter][HandleMotherSkillStateChanged] 加速结束但 CachedBaseMaxWalkSpeed=%.1f. Pawn=%s. "
				     "用退化默认值 600.0. 【v119 防御型设计】"),
				BaseSpeed, *GetName());
		}
	}
}


/**
 * ABaseCharacter::ExecuteDeathLocal — 转发壳 【2026.07.12 P0 重构】
 *
 * 真实逻辑: CombatDeathComponent::ExecuteDeathLocal
 */
void ABaseCharacter::ExecuteDeathLocal()
{
	if (UCombatDeathComponent* Death = ResolveCombatDeath())
	{
		Death->ExecuteDeathLocal();
	}
}


// ==========================================
// 【2026.07.11 P0 大厂架构】复活无敌期 — 单一入口（转发壳）
// ==========================================
//
// 职责: 通过 BaseCharacter 暴露公开 API, 内部委托 CombatDeathComponent
// 调用方: RoomGameMode::RequestRespawn / SpawnAllPlayersIntoBattle 末尾
// 真实实现位于 CombatDeathComponent, BaseCharacter 这里只做转发
// ==========================================

/**
 * 激活复活无敌期 — 转发壳
 * 真实逻辑: CombatDeathComponent::ActivateSpawnInvincibility
 */
void ABaseCharacter::ActivateSpawnInvincibility(float DurationOverride)
{
	if (UCombatDeathComponent* Death = ResolveCombatDeath())
	{
		Death->ActivateSpawnInvincibility(DurationOverride);
	}
	// else: ResolveCombatDeath 已 Log Error
}


/**
 * 取消复活无敌期 — 转发壳
 * 真实逻辑: CombatDeathComponent::DeactivateSpawnInvincibility
 */
void ABaseCharacter::DeactivateSpawnInvincibility()
{
	if (UCombatDeathComponent* Death = ResolveCombatDeath())
	{
		Death->DeactivateSpawnInvincibility();
	}
}


/**
 * ABaseCharacter::EndPlay
 *
 * 【2026-06-15 新增】: 工业规范 - 显式清理所有由自身注册的 Timer
 *
 * 调用时机:
 * 1. 角色被销毁(死亡后服务器销毁旧角色、客户端主动离开)
 * 2. 角色离开世界(切关、玩家掉线)
 *
 * 为什么需要显式清理:
 * - UE 内部虽会通过 OnDestroyed 自动清理成员绑定的 Timer,但自动清理是"被动响应"
 * - 显式清理可以: (1) 立即释放 TimerManager 槽位 (2) 防止死亡流程未触发 timer 残留
 * - 极端情况下(快速死亡-重生-再死亡),残留 timer 会在新角色上误触发
 *
 * 防御性编程:
 * - 每个 ClearTimer 调用前先检查 World 有效性(EndPlay 时可能已无 World)
 * - 使用 IsTimerActive 判断避免无谓调用(可选,ClearTimer 本来就幂等)
 */
void ABaseCharacter::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 防御: EndPlay 时 World 可能已无效(切关时序问题)
	if (UWorld* World = GetWorld())
	{
		FTimerManager& TM = World->GetTimerManager();

		// 【2026.07.12 P0 重构】Timer 已迁移到 Component 自治:
		//   - RagdollTimerHandle → CombatDeathComponent (ExecuteDeathLocal 自清理)
		//   - CharacterIconRefreshTimerHandle → CharacterIconComponent (RetryRefresh 自清理)
		//   - HUDRefreshTimerHandle → CharacterIconComponent (RetryRefreshHUD 自清理)
		// UE 组件 EndPlay 时自动清理自身 Timer, 此处不需要再手动 ClearTimer

		// 防御: HUDRefreshTimerHandle 仍保留在 BaseCharacter 头部? 这里用泛化 ClearTimerAll 兜底
		//    实际已删除, 此行保留仅为编译期占位 (零运行时代价)
	}

	Super::EndPlay(EndPlayReason);
}


// ==========================================
// 3. Tick
// ==========================================

/**
 * ABaseCharacter::Tick
 *
 * 每帧调用
 * 关键:
 * 1. 摄像机阻尼减震回弹（下蹲/起立时镜头平滑）
 * 2. 【2026-07-01 重构】生命/能量回复已抽离到 UHealthRegenComponent
 * 3. 死亡溶解特效（值达到 1.1 时销毁）
 */
void ABaseCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// ==========================================
	// 摄像机阻尼减震回弹
	// ==========================================
	if (CameraBoom && !FMath::IsNearlyZero(CameraBoom->TargetOffset.Z, 0.01f))
	{
		// 利用 FInterpTo，每一帧让 TargetOffset.Z 平滑地向 0.0f 靠拢
		CameraBoom->TargetOffset.Z = FMath::FInterpTo(CameraBoom->TargetOffset.Z, 0.0f, DeltaTime, CrouchCameraSmoothSpeed);
	}

	// 注: 血量/能量回复已由 HealthRegenComponent 自治 (默认 bEnableAutoRegen=false, 格斗游戏设计)

	// 【P0 2026.07.08 调试用】AI 移动状态诊断, 每 5s 打印一次 (仅服务器, 仅 AI)
	// 排查 "AI 启用 BT 后闪烁但不移动" 问题
	// 【2026.07.12 P0 重构】改读 PlayerCombo->bIsAttacking / WeaponAttach->CurrentWeapon (真理源迁移)
	static double LastDiagnoseTime = 0.0;
	const double Now = GetWorld() ? GetWorld()->GetTimeSeconds() : 0.0;
	if (HasAuthority() && GetController() && GetController()->IsA<AAIController>() && Now - LastDiagnoseTime > 5.0)
	{
		LastDiagnoseTime = Now;
		UCharacterMovementComponent* Move = GetCharacterMovement();
		const FVector Vel = GetVelocity();
		UE_LOG(LogTemp, Warning,
			TEXT("[%s] AIDiag: MaxWalkSpeed=%.0f Vel=%.1f IsMovLocked=%d bIsAttacking=%d HasWeapon=%d MoveMode=%d"),
			*GetName(),
			Move ? Move->MaxWalkSpeed : 0.f,
			Vel.Size(),
			bIsMovementLocked ? 1 : 0,
			ResolvePlayerCombo() && ResolvePlayerCombo()->IsAttacking() ? 1 : 0,
			ResolveWeaponAttach() && ResolveWeaponAttach()->GetCurrentWeapon() ? 1 : 0,
			Move ? (int32)Move->MovementMode : -1);
	}

	// [REMOVED 2026.07.08 用户指令] 严禁 C++ 兜底移动, AI 100% BT 驱动

	// 【v93.2 大厂架构 — 母体复用 Melee 缝合算法】驱动母体 trace 的 BoxTrace 缝合
	//   - 大厂原则 — 单一真理源: Strategy 内部 TraceState 字段 (IsActive() 派生)
	//   - 调用方: ANS_MeleeTraceState::NotifyBegin 设 Active=true (经 StartMotherTrace)
	//   - 退出:   ANS_MeleeTraceState::NotifyEnd   设 Active=false (经 StopMotherTrace)
	//   - Tick 中: 委托 Strategy.TickDetection 做 BoxTrace (复用现有 Tick 系统)
	//
	// 【注意】TickDetection 内部已经处理 TraceState==Idle 的 no-op, 这里不需要再判断
	// 母体路径 TickDetection 走 Owner Mesh + 母体 Socket, 命中调 Server_ReportMotherAttackHit
	//
	// 【v98.1 重复扣血修复 — 母体路径 Tick 守卫】
	//   母体 trace 在两端都会触发 (UE AnimNotify 在 replicated Montage 上端点都跑):
	//     - 服务器端命中 → 调 Server_ReportMotherAttackHit → 服务器本地执行 Implementation → 1 次变母体
	//     - 客户端端命中 → 调 Server_ReportMotherAttackHit RPC → 服务器 Implementation → 又 1 次变母体 ❌
	//   守卫 (镜像 BaseWeapon::Tick):
	//     - 服务器端: HasAuthority=true 且 IsLocallyControlled=true (host 玩家/AI) → 跑
	//     - 客户端端: HasAuthority=false 且 IsLocallyControlled=true (本地玩家) → 跑
	//     - 远端 Pawn: IsLocallyControlled=false → 跳过 → 不调 RPC
	//   大厂原则: 单一入口数据源 (Tick 守卫保证同一 Pawn 不会两端同时跑)
	if (MotherTraceStrategy && (HasAuthority() || IsLocallyControlled()))
	{
		MotherTraceStrategy->TickDetection(nullptr, DeltaTime);
	}
}


// ==========================================
// 4. 网络同步注册
// ==========================================

/**
 * GetLifetimeReplicatedProps
 *
 * 注册需要网络同步的 UPROPERTY
 * 同步内容: 生命值/死亡状态/武器/能量/AC/ACE
 */
void ABaseCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// 【2026-06-15 重构】删除对 CurrentHealth/bIsDead 的 DOREPLIFETIME
	// 这两个字段已下沉到 HealthComponent, 复制由 Component 自治
	// 【2026-06-15 重构】删除对 CurrentEnergy 的 DOREPLIFETIME
	// 已下沉到 EnergyComponent, 复制由 Component 自治
	// 【2026.07.12 P0 重构】删除对 CurrentWeapon 的 DOREPLIFETIME
	// 已下沉到 WeaponAttachmentComponent, 复制由 Component 自治 (ReplicatedUsing = OnRep_CurrentWeapon)
	// 同步AC/ACE值（注意: MaxAC 是配置常量，无需网络同步）
	DOREPLIFETIME(ABaseCharacter, ACValue);
	DOREPLIFETIME(ABaseCharacter, ACEValue);

	// 【v31.6 大厂重构】删除 LastKillMethod 的 DOREPLIFETIME
	//   真理源唯一 = Weapon::LastKillMethod (Weapon 已独立复制)
	//   Multicast_NotifyKill 改为 RPC 参数传 EKillMethod, 不再读 Character 字段

	// 【2026.07.11 P0 修复】FactionTag 网络同步
	// 根因: Pawn.FactionTag 是 UE AIPerception 的阵营敌我判定唯一数据源
	//       如果不同步, 客户端 Pawn.FactionTag == Empty → AI 看到玩家 = Neutral (默认) →
	//       bDetectEnemies=true 但 Neutral 被跳过 → 玩家永远不在 BT 视野 → AI 不会追玩家
	DOREPLIFETIME(ABaseCharacter, FactionTag);

	// 【v93.1 大厂架构新增】母体变异状态同步
	// 大厂原则 — 单一真理源 (v99.1 收敛):
	//   - 服务器: URoomSpawnSubsystem::MutatePawnToMother Step 5.7 写入 NewMotherPawn->bIsMother = true
	//           (统一母体生成入口, 首次变异 + 母体复活都走这里)
	//   - 客户端: OnRep_bIsMother 收到 → Broadcast 让 BT / UI 知道该角色已变异为母体
	// 为什么 Replicated:
	//   - AI BT 需要识别母体 (不走人类攻击逻辑)
	//   - 玩家 HUD 需要知道"我是母体了, 切母体视角"
	//   - 镜像 v27 FactionTag: 没有 Replicated = 客户端永远是默认值 → 业务失效
	DOREPLIFETIME(ABaseCharacter, bIsMother);
	DOREPLIFETIME(ABaseCharacter, bIsHuman);
}


// ==========================================
// 5. 客户端 OnRep 回调
// ==========================================


/**
 * Client_UpdateHealthDisplay_Implementation
 *
 * Client RPC: 服务器主动通知所属客户端刷新血量
 * 【2026-06-15 重构】: 改用 RefreshHUDFromCurrentState (全量刷新,包含能量/AC)
 * 解决远程玩家受伤血条不扣的问题
 */
void ABaseCharacter::Client_UpdateHealthDisplay_Implementation(float Current, float Max)
{
	// 【2026-06-15 废弃】: 形参 Current/Max 未使用(内部改用 RefreshHUDFromCurrentState)
	// 保留实现体仅为兼容旧蓝图调用方(如有)
	UE_LOG(LogTemp, Warning, TEXT("[Health] Client_UpdateHealthDisplay 已废弃,改用 HealthComponent 事件"));
	RefreshHUDFromCurrentState();
}


/**
 * Client_UpdateEnergyDisplay_Implementation
 *
 * Client RPC: 服务器主动通知所属客户端刷新能量条
 * 【2026-06-15 重构】: 改用 TryResolveHUDWidget
 */
void ABaseCharacter::Client_UpdateEnergyDisplay_Implementation(float Current, float Max)
{
	if (TryResolveHUDWidget(false))
	{
		GameHUDWidget->UpdateEnergy(Current, Max);
		GameHUDWidget->UpdateEnergyText(FMath::CeilToInt(Current), FMath::CeilToInt(Max));
	}
}


// ==========================================
// 6. 武器网络复制回调（客户端同步）
// ==========================================

/**
 * OnRep_CurrentWeapon — 转发壳 (UFUNCTION 在 Actor 上, 实现委托给组件)
 * 真实逻辑: WeaponAttachmentComponent::OnRep_CurrentWeapon
 *
 * 【v36】改用 ResolveWeaponAttach lazy 查找
 */
void ABaseCharacter::OnRep_CurrentWeapon(ABaseWeapon* OldWeapon)
{
	if (UWeaponAttachmentComponent* ResolvedAttach = ResolveWeaponAttach())
	{
		ResolvedAttach->OnRep_CurrentWeapon(OldWeapon);
	}
	// else: ResolveWeaponAttach 已 Log Error
}


/**
 * OnRep_ACValue
 *
 * AC 值改变时的客户端回调
 *
 * 【v39 修复】用 ResolveCharacterEvents() 而非裸字段访问 (BP archetype null 字段防护)
 */
void ABaseCharacter::OnRep_ACValue()
{
	// 【2026-07-01 重构 v2】: 改用 CharacterEvents 广播
	UCharacterEvents* Events = ResolveCharacterEvents();
	if (!Events)
	{
		return;
	}
	Events->OnACValueChanged.Broadcast(ACValue);

	// AC 变化时重新查询排名并刷新 ACE 颜色 (内部也改用事件)
	RefreshACEWithRank();
}


/**
 * OnRep_ACEValue
 *
 * ACE 值改变时的客户端回调
 *
 * 【v39 修复】用 ResolveCharacterEvents() 而非裸字段访问
 */
void ABaseCharacter::OnRep_ACEValue()
{
	// 【2026-07-01 重构 v2】: 改用 CharacterEvents 广播
	// OnRep_ACEValue 在所有客户端上触发, CharacterEvents 组件本身在本地,
	// 所以这里的广播天然只在本地执行 (不需要额外的 IsLocallyControlled 守卫)
	UCharacterEvents* Events = ResolveCharacterEvents();
	if (!Events)
	{
		return;
	}
	// 刷新 ACE 数值 (无排名, 用默认白色)
	Events->OnACEValueChanged.Broadcast(ACEValue);

	// ACE 变化时也需要查询排名 (因为其他玩家的 ACE 变化也会影响你的排名)
	RefreshACEWithRank();
}


// ==========================================
// 7. 角色图标/武器图标刷新
// ==========================================

/**
 * RefreshCharacterIcon — 转发壳
 * 真实逻辑: CharacterIconComponent::RefreshCharacterIcon
 */
void ABaseCharacter::RefreshCharacterIcon()
{
	if (UCharacterIconComponent* Icon = ResolveCharacterIcon())
	{
		Icon->RefreshCharacterIcon();
	}
}


/**
 * Client_RefreshCharacterIcon_Implementation — 转发壳 (UFUNCTION(Client) 在 Actor 上)
 * 真实逻辑: CharacterIconComponent::Client_RefreshCharacterIcon_Implementation
 */
void ABaseCharacter::Client_RefreshCharacterIcon_Implementation(const FString& InCharacterID, UTexture2D* Avatar, bool bInIsMother)
{
	if (UCharacterIconComponent* Icon = ResolveCharacterIcon())
	{
		Icon->Client_RefreshCharacterIcon_Implementation(InCharacterID, Avatar, bInIsMother);
	}
}


/**
 * Client_RefreshWeaponIcon_Implementation — 转发壳 (UFUNCTION(Client) 在 Actor 上)
 * 真实逻辑: CharacterIconComponent::Client_RefreshWeaponIcon_Implementation
 *
 * 【v40.1 P0 新增】镜像 Client_RefreshCharacterIcon_Implementation
 */
void ABaseCharacter::Client_RefreshWeaponIcon_Implementation(const FString& InWeaponID, UTexture2D* Icon)
{
	if (UCharacterIconComponent* IconComp = ResolveCharacterIcon())
	{
		IconComp->Client_RefreshWeaponIcon_Implementation(InWeaponID, Icon);
	}
}


/**
 * Client_RefreshWeaponAmmo_Implementation — 转发壳 (UFUNCTION(Client) 在 Actor 上)
 * 真实逻辑: CharacterIconComponent::Client_RefreshWeaponAmmo_Implementation
 *
 * 【v85.3 P0 新增】镜像 Client_RefreshWeaponIcon_Implementation
 */
void ABaseCharacter::Client_RefreshWeaponAmmo_Implementation(int32 CurrentAmmo, int32 MagazineSize, int32 ReserveAmmo)
{
	if (UCharacterIconComponent* IconComp = ResolveCharacterIcon())
	{
		IconComp->Client_RefreshWeaponAmmo_Implementation(CurrentAmmo, MagazineSize, ReserveAmmo);
	}
}


/**
 * RetryRefreshCharacterIcon — 转发壳
 * 真实逻辑: CharacterIconComponent::RetryRefreshCharacterIcon
 */
void ABaseCharacter::RetryRefreshCharacterIcon()
{
	if (UCharacterIconComponent* Icon = ResolveCharacterIcon())
	{
		Icon->RetryRefreshCharacterIcon();
	}
}


/**
 * RefreshWeaponIconOnHUD — 转发壳
 * 真实逻辑: CharacterIconComponent::RefreshWeaponIconOnHUD
 */
void ABaseCharacter::RefreshWeaponIconOnHUD()
{
	if (UCharacterIconComponent* Icon = ResolveCharacterIcon())
	{
		Icon->RefreshWeaponIconOnHUD();
	}
}


/**
 * GetCharacterAvatarFromTable — 转发壳 (BlueprintPure)
 * 真实逻辑: CharacterIconComponent::GetCharacterAvatarFromTable
 */
UTexture2D* ABaseCharacter::GetCharacterAvatarFromTable(const FString& CharID)
{
	if (UCharacterIconComponent* Icon = ResolveCharacterIcon())
	{
		return Icon->GetCharacterAvatarFromTable(CharID);
	}
	return nullptr;
}


/* 删除重复的 Client_RefreshCharacterIcon_Implementation — 已转发壳替代 */


/**
 * RefreshACEWithRank
 *
 * 查询当前 ACE 排名并刷新 HUD 上的 ACE 文字颜色
 * - 全场第一: 金色
 * - 队内第一: 白色
 * - 其他: 无
 *
 * 【v39 修复】用 ResolveCharacterEvents() 而非裸字段访问
 */
void ABaseCharacter::RefreshACEWithRank()
{
	// 【2026-07-01 重构 v2】: 改用 CharacterEvents 广播 ACE + 排名颜色事件
	// IsLocallyControlled 守卫: ACE 排名只对本机玩家有意义
	if (!IsLocallyControlled())
	{
		return;
	}

	UCharacterEvents* Events = ResolveCharacterEvents();
	if (!Events)
	{
		return;
	}

	// 1. 获取自己的 RoomPlayerState
	ARoomPlayerState* MyPS = nullptr;
	if (APlayerState* PS = GetPlayerState<APlayerState>())
	{
		MyPS = Cast<ARoomPlayerState>(PS);
	}
	if (!MyPS)
	{
		return;
	}

	// 2. 获取 GameState 查询排名
	ARoomGameState* GS = GetWorld()->GetGameState<ARoomGameState>();
	if (!GS)
	{
		return;
	}

	// 3. 获取我的阵营 (FGameplayTag)
	// 【2026.07.10 P0 重构】CurrentTeam (ERoomTeam) → CurrentFactionTag (FGameplayTag)
	const FGameplayTag MyFactionTag = MyPS->CurrentFactionTag;

	// 4. 查询阵营内 AC 最高者
	// 【2026.07.10 P0 重构】GetTeamTopACPlayer(ERoomTeam) → GetFactionTopACPlayer(FGameplayTag)
	ARoomPlayerState* TeamTop = GS->GetFactionTopACPlayer(MyFactionTag);

	// 5. 查询全场 AC 最高者
	ARoomPlayerState* OverallTop = GS->GetOverallTopACPlayer();

	// 6. 确定 ACE 排名类型
	EACERankType RankType = EACERankType::None;

	// 优先判断是否是全场第一（金色），因为"全场第一"优先级高于"队内第一"
	if (OverallTop && OverallTop == MyPS)
	{
		RankType = EACERankType::Gold;
	}
	else if (TeamTop && TeamTop == MyPS)
	{
		RankType = EACERankType::White;
	}

	// 7. 通过 CharacterEvents 广播 ACE + 排名颜色 (替代直接 GameHUDWidget Push)
	Events->OnACEWithRankChanged.Broadcast(ACEValue, RankType);
}


// ==========================================
// 8. 能量管理
// ==========================================

/**
 * ConsumeEnergy
 *
 * 消耗能量（释放技能时调用）
 * 【2026-06-15 重构】: 委托给 EnergyComponent
 * @param Amount 要消耗的能量
 * @return 是否消耗成功
 */
bool ABaseCharacter::ConsumeEnergy(float Amount)
{
	// 【v39 修复】用 ResolveEnergyComponent 而非裸字段访问
	UEnergyComponent* EC = ResolveEnergyComponent();
	if (!EC)
	{
		return false;
	}

	// 消耗能量后打断回复 (有动作表示玩家活跃)
	// 【v39 修复】HealthRegenComponent → ResolveHealthRegenComponent
	const bool bSuccess = EC->Consume(Amount);
	if (bSuccess)
	{
		if (UHealthRegenComponent* Regen = ResolveHealthRegenComponent())
		{
			Regen->NotifyDamageTaken();
		}
	}
	return bSuccess;
}


// ==========================================
// 9. 回复系统 【2026-07-01 抽离】
// ==========================================
// 注: 血量/能量回复已抽离到 UHealthRegenComponent (自治组件, 默认关闭)
// 旧代码 (StartRegeneration/StopRegeneration) 已废弃, 字段和方法已删除
// 访问请通过 HealthRegenComponent 组件


// ==========================================
// 10. 击杀奖励与 AC/ACE
// ==========================================

/**
 * OnKill
 *
 * 击杀奖励接口
 * - 增加血量（HealthRewardPerKill）
 * - 增加能量（EnergyRewardPerKill）
 * - 增加 AC（AddAC）
 */
void ABaseCharacter::OnKill(ABaseCharacter* KilledCharacter)
{
	if (!HasAuthority()) return;

	// 击杀奖励: 增加血量和能量
	// 【v39 修复】用 ResolveEnergyComponent / ResolveHealthComponent 而非裸字段访问
	if (UEnergyComponent* EC = ResolveEnergyComponent())
	{
		EC->Add(EnergyRewardPerKill);
	}
	// 【2026-06-15 重构】: 通过 HealthComponent 加血
	if (UHealthComponent* HC = ResolveHealthComponent())
	{
		HC->Heal(HealthRewardPerKill);
	}

	// 增加AC（AddAC 内部已 Clamp 到 MaxAC）
	AddAC(1);
}


/**
 * AddAC
 *
 * 增加 AC 值（仅服务器）
 * ACE 代表连续击杀，每次击杀都 +1
 */
void ABaseCharacter::AddAC(int32 Amount)
{
	if (HasAuthority())
	{
		ACValue = FMath::Clamp(ACValue + Amount, 0, MaxAC);
		// ACE 代表连续击杀，每次击杀都 +1
		ACEValue += Amount;
	}
}


/**
 * TakeAC
 *
 * 扣除 AC 值（用于结算时扣分）
 */
void ABaseCharacter::TakeAC(int32 Amount)
{
	if (HasAuthority())
	{
		ACValue = FMath::Clamp(ACValue - Amount, 0, MaxAC);
	}
}


/**
 * ResetAC
 *
 * 重置 AC 到 0（同时重置 ACE）
 */
void ABaseCharacter::ResetAC()
{
	if (HasAuthority())
	{
		ACValue = 0;
		ACEValue = 0;
	}
}


// ==========================================
// 11. 输入绑定与移动逻辑
// ==========================================

/**
 * SetupPlayerInputComponent
 *
 * 绑定增强输入上下文
 * 绑定基础移动视角/跳跃/下蹲/轻重击/技能
 */
void ABaseCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	// 绑定增强输入上下文
	if (APlayerController* PlayerController = Cast<APlayerController>(Controller))
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PlayerController->GetLocalPlayer()))
		{
			if (DefaultMappingContext)
			{
				Subsystem->AddMappingContext(DefaultMappingContext, 0);
			}
		}
	}

	if (UEnhancedInputComponent* EnhancedInputComponent = CastChecked<UEnhancedInputComponent>(PlayerInputComponent))
	{
		// 绑定基础移动视角
		if (MoveAction) EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Triggered, this, &ABaseCharacter::Move);
		if (LookAction) EnhancedInputComponent->BindAction(LookAction, ETriggerEvent::Triggered, this, &ABaseCharacter::Look);
		if (JumpAction) EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Triggered, this, &ACharacter::Jump);

		// 【2026.07.12 P0 重构】连击输入转发给 PlayerComboComponent
		// BaseCharacter 留作转发壳, 实际状态机逻辑在 PlayerCombo 内
		// 【v38】改用 ResolvePlayerCombo 通用 resolver (防 BP 子类覆写)
		if (UPlayerComboComponent* Combo = ResolvePlayerCombo())
		{
			Combo->SetInputActions(LightAttackAction, HeavyAttackAction, IA_MotherSkill);
			Combo->BindInputActions(EnhancedInputComponent);
		}
		else
		{
			// 【大厂原则 - 零兜底】PlayerCombo 必须挂载, 否则 Log Error
			// 已在 ResolvePlayerCombo 内部 Log Error, 此处不再重复
		}

		// 绑定下蹲 (按下时触发 StartCrouch，松开时触发 StopCrouch)
		if (CrouchAction)
		{
			EnhancedInputComponent->BindAction(CrouchAction, ETriggerEvent::Started, this, &ABaseCharacter::StartCrouch);
			EnhancedInputComponent->BindAction(CrouchAction, ETriggerEvent::Completed, this, &ABaseCharacter::StopCrouch);
		}

		// 【v51 大厂架构 — 生化模式】绑定切换武器槽位 (Q 键)
		//   - Triggered (按下瞬间) → 调 OnSwitchWeaponPressed → Server RPC
		//   - 服务器权威切槽 → 自动 Replicate 到所有客户端
		if (SwitchWeaponAction)
		{
			EnhancedInputComponent->BindAction(SwitchWeaponAction, ETriggerEvent::Triggered, this, &ABaseCharacter::OnSwitchWeaponPressed);
		}

		// 【v58 大厂架构 — FPS枪战】绑定 1/2/3 键切换武器槽位
		//   - 按键1 → 切换到主武器
		//   - 按键2 → 切换到副武器
		//   - 按键3 → 切换到近战武器
		if (SwitchToPrimaryAction)
		{
			EnhancedInputComponent->BindAction(SwitchToPrimaryAction, ETriggerEvent::Triggered, this, &ABaseCharacter::OnSwitchToPrimaryPressed);
		}
		if (SwitchToSecondaryAction)
		{
			EnhancedInputComponent->BindAction(SwitchToSecondaryAction, ETriggerEvent::Triggered, this, &ABaseCharacter::OnSwitchToSecondaryPressed);
		}
		if (SwitchToMeleeAction)
		{
			EnhancedInputComponent->BindAction(SwitchToMeleeAction, ETriggerEvent::Triggered, this, &ABaseCharacter::OnSwitchToMeleePressed);
		}
		if (DropWeaponAction)
		{
			EnhancedInputComponent->BindAction(DropWeaponAction, ETriggerEvent::Started, this, &ABaseCharacter::OnDropWeaponPressed);
		}

		// 【v60.x 大厂架构 — FPS枪战】绑定开火/换弹输入
		//   - FireAction (左键):
		//     - Triggered (按住期间) → OnFirePressed → CurrentWeapon->Server_StartFire (RPC)
		//     - Completed (松开) → OnFireReleased → CurrentWeapon->Server_StopFire (RPC)
		//   - ReloadAction (R):
		//     - Started (按下瞬间) → OnReloadPressed → CurrentWeapon->Server_StartReload (RPC)
		//
		// 【v60.x 大厂架构 — FPS枪战】绑定开火/换弹输入
		//   - FireAction (左键):
		//     - Started (按下瞬间) → OnFirePressed → CurrentWeapon->Server_StartFire (RPC)
		//       注意: 半自动用 Started（全自动用 Triggered）
		//     - Completed (松开) → OnFireReleased → CurrentWeapon->Server_StopFire (RPC)
		//   - ReloadAction (R):
		//     - Started (按下瞬间) → OnReloadPressed → CurrentWeapon->Server_StartReload (RPC)
		//
		// UE5 Enhanced Input 行为说明:
		//   - ETriggerEvent::Started: 按下瞬间只触发一次 (适合半自动)
		//   - ETriggerEvent::Triggered: 按住期间每帧触发 (适合全自动)
		//   - ETriggerEvent::Completed: 松开时触发一次
		//
		// 大厂原则:
		//   - 半自动: Started 触发一次 → 冷却检查 → 射击
		//   - 全自动: Triggered 每帧触发 → StartFire → TickComponent 节流
		//   - StopFire 在松开时触发，停止 TickComponent
		if (FireAction)
		{
		// 全自动扫射模式: 按住期间每帧触发（Triggered）
		//   - Triggered 每帧调用 OnFirePressed
		//   - WeaponFireComponent::StartFire 内部有冷却检查，冷却中启动 TickComponent 等待
		EnhancedInputComponent->BindAction(FireAction, ETriggerEvent::Triggered, this, &ABaseCharacter::OnFirePressed);
			// 停止开火: 任何模式都需要（防止 TickComponent 继续射击）
			EnhancedInputComponent->BindAction(FireAction, ETriggerEvent::Completed, this, &ABaseCharacter::OnFireReleased);
		}
		if (ReloadAction)
		{
			EnhancedInputComponent->BindAction(ReloadAction, ETriggerEvent::Started, this, &ABaseCharacter::OnReloadPressed);
		}
	}
}


/**
 * Move
 *
 * 基础输入回调: 移动
 * 死亡/暂停时不能移动
 * 按摄像机视角驱动角色移动
 */
void ABaseCharacter::Move(const FInputActionValue& Value)
{
	// 死亡状态不能移动
	// 【注意】暂停检查已移除: 暂停逻辑改由 PlayerController 通过 InputMode=UIOnly 阻塞,
	// 不再使用全局 SetGamePaused, 因此 BaseCharacter 无需再读全局暂停状态
	if (IsDead()) return;

	FVector2D MovementVector = Value.Get<FVector2D>();
	if (Controller != nullptr)
	{
		// 找出摄像机当前面向的方位
		const FRotator Rotation = Controller->GetControlRotation();
		const FRotator YawRotation(0, Rotation.Yaw, 0);

		// 获取摄像机视角下的"正前方"和"正右方"
		const FVector ForwardDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
		const FVector RightDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);

		// 按照摄像机的视角去驱动角色移动
		AddMovementInput(ForwardDirection, MovementVector.Y);
		AddMovementInput(RightDirection, MovementVector.X);

		// 注: LastMoveTime 已抽离到 HealthRegenComponent, 通过速度检测自动维护
	}
}


/**
 * Look
 *
 * 基础输入回调: 视角
 */
void ABaseCharacter::Look(const FInputActionValue& Value)
{
	// 死亡状态不能转动视角
	// 【注意】暂停检查已移除: 暂停逻辑改由 PlayerController 通过 InputMode=UIOnly 阻塞,
	// 不再使用全局 SetGamePaused, 因此 BaseCharacter 无需再读全局暂停状态
	if (IsDead()) return;

	FVector2D LookAxisVector = Value.Get<FVector2D>();
	if (Controller != nullptr)
	{
		AddControllerYawInput(LookAxisVector.X);
		AddControllerPitchInput(LookAxisVector.Y);
	}
}


// ==========================================
// 12. 【2026.07.12 P0 重构】自动连斩系统核心逻辑 — 全部转发到 PlayerComboComponent
// ==========================================
// 大厂原则 - 单一真理源:
//   状态机字段 (bIsAttacking / bCanReceiveInput / bSaveAttack / bIsHoldingLightAttack /
//               ComboIndex) 全部已迁移到 PlayerComboComponent
//   BaseCharacter 上同名方法改为转发壳, 真实逻辑在 PlayerCombo 内
//   这里只做一行委托, 不再持有副本数据

/**
 * LightAttack_Pressed — 转发壳
 * 真实逻辑: PlayerComboComponent::LightAttack_Pressed
 */
void ABaseCharacter::LightAttack_Pressed()
{
	if (UPlayerComboComponent* Combo = ResolvePlayerCombo())
	{
		Combo->LightAttack_Pressed();
	}
	// else: ResolvePlayerCombo 内部已 Log Error, 此处不再重复
}


/**
 * LightAttack_Released — 转发壳
 * 真实逻辑: PlayerComboComponent::LightAttack_Released
 */
void ABaseCharacter::LightAttack_Released()
{
	if (UPlayerComboComponent* Combo = ResolvePlayerCombo())
	{
		Combo->LightAttack_Released();
	}
}


/**
 * ExecuteComboSequence — 转发壳
 * 真实逻辑: PlayerComboComponent::ExecuteComboSequence
 */
void ABaseCharacter::ExecuteComboSequence()
{
	if (UPlayerComboComponent* Combo = ResolvePlayerCombo())
	{
		Combo->ExecuteComboSequence();
	}
}


/**
 * HeavyAttack — 转发壳
 * 真实逻辑: PlayerComboComponent::HeavyAttack
 */
void ABaseCharacter::HeavyAttack()
{
	if (UPlayerComboComponent* Combo = ResolvePlayerCombo())
	{
		Combo->HeavyAttack();
	}
}


/**
 * UseSkill — 转发壳
 * 真实逻辑: PlayerComboComponent::UseSkill
 */
void ABaseCharacter::UseSkill()
{
	if (UPlayerComboComponent* Combo = ResolvePlayerCombo())
	{
		Combo->UseSkill();
	}
}


// ==========================================
// 13. 【2026.07.12 P0 重构】动画通知 (Anim Notify) 回调接口 — 全部转发到 PlayerComboComponent
// ==========================================

/**
 * EnableComboWindow — 转发壳
 * 真实逻辑: PlayerComboComponent::EnableComboWindow
 */
void ABaseCharacter::EnableComboWindow()
{
	if (UPlayerComboComponent* Combo = ResolvePlayerCombo())
	{
		Combo->EnableComboWindow();
	}
}


/**
 * CheckCombo — 转发壳
 * 真实逻辑: PlayerComboComponent::CheckCombo
 */
void ABaseCharacter::CheckCombo()
{
	if (UPlayerComboComponent* Combo = ResolvePlayerCombo())
	{
		Combo->CheckCombo();
	}
}


/**
 * EndAttackState — 转发壳
 * 真实逻辑: PlayerComboComponent::EndAttackState
 */
void ABaseCharacter::EndAttackState()
{
	if (UPlayerComboComponent* Combo = ResolvePlayerCombo())
	{
		Combo->EndAttackState();
	}
}



/**
 * 【P0 大厂架构修复 2026.07.06】AI 专用轻攻击 — 不复用玩家连击状态机
 *
 * 根因:
 *   OnAIRequestAttack -> LightAttack_Pressed -> 设置 bIsMovementLocked=true + MaxWalkSpeed=0
 *   -> EndAttackState (由动画蓝图 NotifyEnd 调用) -> 清除锁
 *   问题: AI 攻击不走动画蓝图的 NotifyEnd 回调链, EndAttackState 从未被调用
 *   结果: bIsMovementLocked 永远为 true, AI 永远无法移动
 *
 * 修复:
 *   AI 专用攻击路径, 完全独立于玩家连击状态机:
 *   - 不设置 bIsMovementLocked (AI 攻击时不锁定移动)
 *   - 不设置 bIsAttacking (不触发连击状态)
 *   - 直接播放攻击动画 + Server RPC
 *   - AI 持续追逐 + 持续尝试攻击, 攻击动画播放期间也能移动
 *
 * 【P0 增强 2026.07.06】返回 bool: 让 BT 任务节点能判断成功失败
 *   之前 void 返回, BT 任务不知道是 "没武器" 还是 "PlayAnimMontage 失败" 还是 "成功"
 *   → 日志只能刷 "PerformAttack 失败" 一行, 排查困难
 *   → 改为 bool + 详细 UE_LOG 后, 失败原因立即一目了然
 */
// ==========================================
// 14. 【2026.07.12 P0 重构 + v40.4 原子化】AI 攻击子系统 — 全部转发到 AIAttackComponent
// ==========================================
// 大厂原则 - 单一真理源:
//   AI 攻击节流字段 (v40.4 删 LastAIAttackTimeSeconds / CachedAIMontage / bIsWaitingForAIMontageCallback)
//   全部已迁移到 AIAttackComponent
//   BaseCharacter 上同名方法改为转发壳, 真实逻辑在 AIAttack 内
//   注意: RPC 修饰符 (Server/NetMulticast) 仍保留在 BaseCharacter 上 (UE 强制要求 RPC 在 Actor 上),
//         Implementation 部分转发给 AIAttack

/**
 * OnAIRequestAttack_Simple — 转发壳
 * 真实逻辑: AIAttackComponent::OnAIRequestAttack_Simple
 */
bool ABaseCharacter::OnAIRequestAttack_Simple()
{
	if (UAIAttackComponent* AI = ResolveAIAttack())
	{
		return AI->OnAIRequestAttack_Simple(this);
	}
	// else: ResolveAIAttack 内部已 Log Error
	return false;
}

/**
 * 【v133 P0 大厂扩展】OnAIRequestAttack_WithOptions — 转发壳
 *
 * 大厂原则 — 与 Simple 对称:
 *   - BTTask_PlayAttackMontage (v133) 直接调本方法
 *   - 内部调 AIAttackComponent::OnAIRequestAttack_WithOptions (传入真实 Pawn)
 *   - 任何失败都已在 Component 内部 Log Error, 这里不重复报警
 */
bool ABaseCharacter::OnAIRequestAttack_WithOptions(
	EAIAttackType InAttackType, int32 InComboIndex, bool bInLockMovement)
{
	if (UAIAttackComponent* AI = ResolveAIAttack())
	{
		return AI->OnAIRequestAttack_WithOptions(this, InAttackType, InComboIndex, bInLockMovement);
	}
	// else: ResolveAIAttack 内部已 Log Error
	return false;
}

/**
 * 【v133.1 P0 大厂扩展】OnAIRequestAttack_ExplicitMontage — 转发壳
 *
 * 大厂原则 — 与 WithOptions 对称:
 *   - BTTask_PlayAttackMontage (v133.1) 在 ExplicitMontage!=nullptr 时调本方法
 *   - 内部调 AIAttackComponent::OnAIRequestAttack_ExplicitMontage (传入真实 Pawn)
 */
bool ABaseCharacter::OnAIRequestAttack_ExplicitMontage(
	UAnimMontage* InExplicitMontage, bool bInLockMovement)
{
	if (UAIAttackComponent* AI = ResolveAIAttack())
	{
		return AI->OnAIRequestAttack_ExplicitMontage(this, InExplicitMontage, bInLockMovement);
	}
	// else: ResolveAIAttack 内部已 Log Error
	return false;
}


/**
 * OnAIAttackMontageEnded — 转发壳 (UFUNCTION 必须在 BaseCharacter 上, UAnimInstance::OnMontageEnded 要求)
 * 真实逻辑: AIAttackComponent::OnAIAttackMontageEnded
 */
void ABaseCharacter::OnAIAttackMontageEnded(UAnimMontage* Montage, bool bInterrupted)
{
	if (UAIAttackComponent* AI = ResolveAIAttack())
	{
		AI->OnAIAttackMontageEnded(Montage, bInterrupted);
	}
}


/**
 * Server_PlayAttackAnim_Implementation — 转发壳 (RPC 必须在 Actor 上, 但实现转发给组件)
 * 真实逻辑: AIAttackComponent::Server_PlayAttackAnim_Implementation
 */
void ABaseCharacter::Server_PlayAttackAnim_Implementation(bool bIsHeavy, int32 InComboIndex)
{
	if (UAIAttackComponent* AI = ResolveAIAttack())
	{
		AI->Server_PlayAttackAnim_Implementation(bIsHeavy, InComboIndex);
	}
}


/**
 * Multicast_PlayAttackAnim_Implementation — 转发壳 (RPC 必须在 Actor 上)
 * 真实逻辑: AIAttackComponent::Multicast_PlayAttackAnim_Implementation
 */
void ABaseCharacter::Multicast_PlayAttackAnim_Implementation(bool bIsHeavy, int32 InComboIndex)
{
	if (UAIAttackComponent* AI = ResolveAIAttack())
	{
		AI->Multicast_PlayAttackAnim_Implementation(bIsHeavy, InComboIndex);
	}
}


/**
 * Server_ReportAIAttackHit_Implementation — 转发壳 (RPC 必须在 Actor 上)
 * 真实逻辑: AIAttackComponent::Server_ReportAIAttackHit_Implementation
 */
void ABaseCharacter::Server_ReportAIAttackHit_Implementation(AActor* HitActor, float Damage)
{
	if (UAIAttackComponent* AI = ResolveAIAttack())
	{
		AI->Server_ReportAIAttackHit_Implementation(HitActor, Damage);
	}
}


/**
 * Server_ReportAIAttackHit_Validate — 转发壳 (RPC Validate 必须在 Actor 上)
 * 真实逻辑: AIAttackComponent::Server_ReportAIAttackHit_Validate
 */
bool ABaseCharacter::Server_ReportAIAttackHit_Validate(AActor* HitActor, float Damage)
{
	if (UAIAttackComponent* AI = ResolveAIAttack())
	{
		return AI->Server_ReportAIAttackHit_Validate(HitActor, Damage);
	}
	// 找不到组件 → RPC 拒绝 (大厂原则 - 永不静默放行)
	return false;
}


// ==========================================
// 【v93.2 大厂架构 — 母体复用 Melee 缝合算法】母体攻击命中上报 RPC
// ==========================================
// 复用动机 (用户需求 2026.07.25):
//   - 母体无武器 → 不能走 Weapon->Server_ReportHit
//   - 但 BoxTrace 缝合算法复用 MeleeSwStrategy (零重复)
//   - 母体路径: 命中调 Owner->Server_ReportMotherAttackHit (本函数)
//   - 命中后行为: 玩家 → PlayerComboComponent, AI → AIAttackComponent
//   - 终极行为: 调 RoomMotherMutationSubsystem::MutateCharacterToMother (大厂复用, 不重复实现)
//
// 转发壳模式 (与 Server_ReportAIAttackHit 对称):
//   - RPC 必在 Actor (UE 5.6 硬约束), 实现转发到账本归属组件
//   - 玩家/AI 判定: 用 bIsCurrentlyAttackerAI (复用 Server_ReportAIAttackHit 真理源)

void ABaseCharacter::Server_ReportMotherAttackHit_Implementation(AActor* HitActor)
{
	// 零兜底 — HitActor 必非空
	if (!HitActor)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[BaseCharacter] Server_ReportMotherAttackHit: 收到空 HitActor, 忽略."));
		return;
	}

	// 大厂原则 — 账本归属: 玩家 → PlayerComboComponent, AI → AIAttackComponent
	// 决策: 复用 IsAttackerAI() (真理源, 与 Server_ReportAIAttackHit 同源)
	const bool bIsAI = IsAttackerAI();
	if (bIsAI)
	{
		if (UAIAttackComponent* AI = ResolveAIAttack())
		{
			AI->Server_ReportMotherAttackHit_Implementation(HitActor);
			return;
		}
		UE_LOG(LogTemp, Error,
			TEXT("[BaseCharacter] Server_ReportMotherAttackHit: AI 路径找不到 AIAttackComponent — 拒绝. "
			     "【v93.2 零兜底】检查 BP 子类 Components 面板."));
	}
	else
	{
		if (UPlayerComboComponent* Combo = ResolvePlayerCombo())
		{
			Combo->Server_ReportMotherAttackHit_Implementation(HitActor);
			return;
		}
		UE_LOG(LogTemp, Error,
			TEXT("[BaseCharacter] Server_ReportMotherAttackHit: 玩家路径找不到 PlayerComboComponent — 拒绝. "
			     "【v93.2 零兜底】检查 BP 子类 Components 面板."));
	}
}


bool ABaseCharacter::Server_ReportMotherAttackHit_Validate(AActor* HitActor)
{
	// 零兜底 — HitActor 必非空
	if (!HitActor)
	{
		return false; // RPC 拒绝 (大厂原则 - 永不静默放行)
	}
	return true;
}


// ==========================================
// 【v93.2 大厂架构】母体 trace 启动/停止 — ANS_MeleeTraceState 入口
// ==========================================
// 复用动机 (用户需求 2026.07.25):
//   - 母体复用 UANS_MeleeTraceState 类 (美术不学新标签)
//   - ANS_MeleeTraceState::NotifyBegin 内调 OwnerChar->StartMotherTrace(bIsHeavy)
//   - ANS_MeleeTraceState::NotifyEnd   内调 OwnerChar->StopMotherTrace()
//
// 大厂原则 — 不重复架构:
//   - 委托 MotherTraceStrategy (UMeleeSwStrategy 实例) 走 StartMotherTrace
//   - Strategy 内部处理 Mesh 校验 / Socket 校验 (零兜底)
//   - Tick 驱动 BoxTrace 缝合 (复用现有 Tick 系统)
//
// 零兜底:
//   - MotherTraceStrategy == nullptr → Log Error + return false (构造函数已创建, 找不到即配置错)

bool ABaseCharacter::StartMotherTrace(bool bIsHeavy)
{
	// 零兜底 — Strategy 必非空 (构造函数 CreateDefaultSubobject 已创建)
	if (!MotherTraceStrategy)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[BaseCharacter] StartMotherTrace: MotherTraceStrategy 为空 — 拒绝启动. "
			     "【v93.2 零兜底】构造函数应已创建该字段. 检查 BP 子类是否覆盖了 C++ 默认值."));
		return false;
	}

	// 模式校验 — 大厂原则: 母体 trace 只在生化模式有效, 刀战模式不调用 (但防御性 Log 仍保留)
	const AGameStateBase* GameStateBase = GetWorld() ? GetWorld()->GetGameState() : nullptr;
	if (const ARoomGameState* RoomGS = Cast<ARoomGameState>(GameStateBase))
	{
		if (RoomGS->CurrentMatchMode != ERoomMatchMode::Zombie)
		{
			UE_LOG(LogTemp, Error,
				TEXT("[BaseCharacter] StartMotherTrace: 当前模式=%d 不是 Zombie — 拒绝启动母体 trace. "
				     "【v93.2 零兜底】母体攻击只能在生化模式触发."),
				static_cast<int32>(RoomGS->CurrentMatchMode));
			return false;
		}
	}

	// 委托 Strategy 处理 (Strategy 内部 Socket 校验 / 跨帧状态初始化)
	// 注: StartMotherTrace 内部设 bUseOwnerMesh=true, 走 Owner Mesh 路径
	return MotherTraceStrategy->StartMotherTrace(this, bIsHeavy);
}


void ABaseCharacter::StopMotherTrace()
{
	// 零兜底 — Strategy 必非空
	if (!MotherTraceStrategy)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[BaseCharacter] StopMotherTrace: MotherTraceStrategy 为空 — 拒绝停止. "
			     "【v93.2 零兜底】构造函数应已创建该字段."));
		return;
	}

	// 委托 Strategy 处理 (幂等 — 内部 TraceState=Idle 时 no-op)
	MotherTraceStrategy->StopMotherTrace(this);
}


/* 下面是旧版实现 (Phase 2 重构前), 保留作参考, 实际不再被调用 — 已被转发壳替代 */


// ==========================================
// 15. 【2026.07.12 P0 重构】装备武器 — 转发到 WeaponAttachmentComponent
// ==========================================

/**
 /**
 * RequestWeaponSpawn — 转发壳
 * 真实逻辑: WeaponAttachmentComponent::RequestWeaponSpawn
 *
 * 【v36】改用 ResolveWeaponAttach lazy 查找
 */
void ABaseCharacter::RequestWeaponSpawn(TSubclassOf<ABaseWeapon> WeaponClass)
{
	if (UWeaponAttachmentComponent* ResolvedAttach = ResolveWeaponAttach())
	{
		ResolvedAttach->RequestWeaponSpawn(WeaponClass);
	}
	// else: ResolveWeaponAttach 已 Log Error
}


/* 下面是旧版 TakeDamage/Die/ExecuteDeathLocal/Multicast_Die_Implementation/EnableRagdoll/DropAndFadeWeapon
   等死亡链路方法 (Phase 2 重构前), 保留作参考, 实际不再被调用 — 已被转发壳替代
   新的转发壳在文件后面 — 见后续的 CombatDeathComponent 转发壳段 */


/* 下面是旧版 TakeDamage/Die/ExecuteDeathLocal/Multicast_Die_Implementation/EnableRagdoll/DropAndFadeWeapon
   等死亡链路方法 (Phase 2 重构前), 保留作参考, 实际不再被调用 — 已被转发壳替代
   新的转发壳在文件后面 — 见后续的 CombatDeathComponent 转发壳段 */

// ==========================================
// 16. 【2026.07.12 P0 重构】受伤与死亡 — 转发到 CombatDeathComponent
// ==========================================
// 大厂原则 - 单一真理源:
//   死亡链路方法 (TakeDamage / Die / ExecuteDeathLocal / Multicast_Die_Implementation /
//                  EnableRagdoll / DropAndFadeWeapon / ActivateSpawnInvincibility /
//                  DeactivateSpawnInvincibility)
//   全部已迁移到 CombatDeathComponent
//   BaseCharacter 上同名方法改为转发壳
//   注意: Multicast_Die 仍保留 UFUNCTION(NetMulticast, Reliable) 在 BaseCharacter 上 (RPC 必须在 Actor 上),
//         Implementation 部分转发给 CombatDeath

/**
 * TakeDamage — 转发壳 (UE 原生伤害入口)
 * 真实逻辑: CombatDeathComponent::TakeDamage
 */
float ABaseCharacter::TakeDamage(float DamageAmount, struct FDamageEvent const& DamageEvent, class AController* EventInstigator, AActor* DamageCauser)
{
	if (UCombatDeathComponent* Death = ResolveCombatDeath())
	{
		return Death->TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
	}
	// 找不到组件 → 0 伤害 (UE 原生接口需要 float 返回, 大厂原则 - 永不静默吞)
	// ResolveCombatDeath 已 Log Error, 拒绝伤害也是正确行为 (伤害去哪了必须有日志)
	return 0.0f;
}


/**
 * Die — 转发壳
 * 真实逻辑: CombatDeathComponent::Die
 */
void ABaseCharacter::Die()
{
	if (UCombatDeathComponent* Death = ResolveCombatDeath())
	{
		Death->Die();
	}
}


/**
 * ExecuteDeathLocal — 转发壳 (Phase 2 重构已新增, 此处已删除重复实现)
 */
/**
 * Multicast_Die_Implementation — 转发壳 (RPC 必须在 Actor 上, 但实现转发给组件)
 * 真实逻辑: CombatDeathComponent::Multicast_Die_Implementation
 */
void ABaseCharacter::Multicast_Die_Implementation()
{
	if (UCombatDeathComponent* Death = ResolveCombatDeath())
	{
		Death->Multicast_Die_Implementation();
	}
}


/**
 * Multicast_PlayEquipSoundForSlot_Implementation — 播放拿起武器音效
 *
 * 【v76 大厂架构】RPC 真身实现 (UE 5.6 NetMulticast, 服务器 + 所有客户端都触发)
 *
 * 为什么不在 Component 而在 Actor:
 *   - UE 5.6 RPC 必须声明在 Actor 上 (Component RPC 受限, 不可靠)
 *   - Multicast_PlayEquipSoundForSlot 声明在 BaseCharacter, 实现也在这里
 *   - 大厂原则: RPC Implementation 自包含, 不再转发 Component (单一职责)
 *   - 与 Multicast_Die 不同 (后者转发 Component) 是因为 Die 业务复杂,
 *     PlayEquip 是单行调用 UGameplayStatics, 转不转发无所谓
 *
 * 大厂原则 - 零兜底:
 *   - InSound 字段为空 → Log Error + 拒绝播放 (强制修复 DA)
 *   - World 无效 → Log Error + 拒绝播放
 *   - Owner 坐标失效 → 用 ZeroVector fallback (视觉位置影响小,声音是非定向)
 *
 * 大厂原则 - 网络:
 *   - 服务器本地调用 → 服务器 Implementation 触发 (PlaySoundAtLocation)
 *   - 远端客户端收到 → Implementation 触发 (PlaySoundAtLocation)
 *   - 服务器 Local 实例 (ListenServer 客户端) 同样触发 (UE NetMulticast 行为)
 *   - Dedicated Server 上服务器实例也触发 (但服务器没声卡 → 无声播放是引擎忽略)
 *
 * @param NewSlot 槽位 (Primary / Secondary / Melee — 用于日志)
 * @param InSound 音效资产 (从 GM->DA_WeaponSoundMap 查出, RPC 边界序列化纯指针)
 * @param VolumeMul 音量倍数 (从 DataAsset.VolumeMultiplier)
 * @param PitchMul 音高倍数 (从 DataAsset.PitchMultiplier)
 */
void ABaseCharacter::Multicast_PlayEquipSoundForSlot_Implementation(EWeaponSlotType NewSlot, USoundBase* InSound, float VolumeMul, float PitchMul)
{
	// 零兜底: Sound 字段为空 → 强制修复 DA (服务端已查,这里防御二次)
	if (!InSound)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[ABaseCharacter::Multicast_PlayEquipSoundForSlot] InSound 为空 — 拒绝播放. Slot=%s. ")
			TEXT("【v76 大厂原则 — 零兜底】服务器查 DA 时 Sound 字段为空, 强制修复 DA_WeaponSoundMap.uasset."),
			LexToString(NewSlot));
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[ABaseCharacter::Multicast_PlayEquipSoundForSlot] World 无效 — 拒绝播放. Slot=%s."),
			LexToString(NewSlot));
		return;
	}

	// 优先级: Owner 位置 (玩家) > ZeroVector (兜底)
	const FVector PlayLocation = GetActorLocation();

	// UE 5 标准 API: PlaySoundAtLocation (无 Multiplayer 行为, 任何调用方都本地播放)
	UGameplayStatics::PlaySoundAtLocation(
		this,
		InSound,
		PlayLocation,
		VolumeMul,
		PitchMul
	);

	UE_LOG(LogTemp, Display,
		TEXT("[ABaseCharacter::Multicast_PlayEquipSoundForSlot] Pawn=%s Slot=%s 播放 Equip 音效 — Sound=%s Loc=%s Vol=%.2f Pitch=%.2f World=%s"),
		*GetName(),
		LexToString(NewSlot),
		*InSound->GetName(),
		*PlayLocation.ToCompactString(),
		VolumeMul,
		PitchMul,
		*World->GetName());
}


/**
 * EnableRagdoll — 转发壳
 * 真实逻辑: CombatDeathComponent::EnableRagdoll
 */
void ABaseCharacter::EnableRagdoll()
{
	if (UCombatDeathComponent* Death = ResolveCombatDeath())
	{
		Death->EnableRagdoll();
	}
}


/**
 * DropAndFadeWeapon — 转发壳
 * 真实逻辑: CombatDeathComponent::DropAndFadeWeapon
 */
void ABaseCharacter::DropAndFadeWeapon(ABaseWeapon* Weapon)
{
	if (UCombatDeathComponent* Death = ResolveCombatDeath())
	{
		Death->DropAndFadeWeapon(Weapon);
	}
}


/* 下面是已废弃的旧版 EnableRagdoll — 删除 (与上方转发壳重复, 编译会报 redefinition error) */


// ==========================================
// 17. 下蹲系统
// ==========================================

/**
 * StartCrouch
 *
 * 实现下蹲函数
 * 只有没死的时候才能蹲
 * 【注意】暂停检查已移除: 暂停逻辑改由 PlayerController 通过 InputMode=UIOnly 阻塞,
 * 不再使用全局 SetGamePaused, 因此 BaseCharacter 无需再读全局暂停状态
 */
void ABaseCharacter::StartCrouch()
{
	if (!IsDead())
	{
		Crouch(); // 调用 UE ACharacter 自带的神级下蹲函数
	}
}


/**
 * StopCrouch
 *
 * 解除下蹲
 * 自带起立函数（它会自动检测头顶有没有障碍物，有的话会保持蹲着）
 * 【注意】暂停检查已移除: 暂停逻辑改由 PlayerController 通过 InputMode=UIOnly 阻塞,
 * 不再使用全局 SetGamePaused, 因此 BaseCharacter 无需再读全局暂停状态
 */
void ABaseCharacter::StopCrouch()
{
	if (!IsDead())
	{
		UnCrouch(); // 调用自带的起立函数 (它会自动检测头顶有没有障碍物，有的话会保持蹲着)
	}
}


/**
 * OnStartCrouch
 *
 * 当物理系统判定角色【真正蹲下】的瞬间触发（用于平滑相机高度）
 */
void ABaseCharacter::OnStartCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust)
{
	Super::OnStartCrouch(HalfHeightAdjust, ScaledHalfHeightAdjust);

	// 此时胶囊体瞬间矮了，摄像机跟着掉下去了
	// 我们给摄像机的 TargetOffset (目标偏移) 瞬间加上这段高度差
	// 这样摄像机在视觉上就仿佛停留在原处没动
	if (CameraBoom)
	{
		CameraBoom->TargetOffset.Z += HalfHeightAdjust;
	}
}


/**
 * OnEndCrouch
 *
 * 当物理系统判定角色【真正站起】的瞬间触发
 */
void ABaseCharacter::OnEndCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust)
{
	Super::OnEndCrouch(HalfHeightAdjust, ScaledHalfHeightAdjust);

	// 此时胶囊体瞬间长高了，摄像机被瞬间顶上去了
	// 我们把摄像机瞬间往下拉同样的距离，抵消瞬移
	if (CameraBoom)
	{
		CameraBoom->TargetOffset.Z -= HalfHeightAdjust;
	}
}


// ==========================================
// 18. 角色附身/出生
// ==========================================

/**
 * ABaseCharacter::SetMeleeConfig — 转发壳 【v54 P0 重构 — UAIProfileAsset 已删除】
 *
 * 旧: SetMeleeProfile(UAIProfileAsset*) — UAIProfileAsset 整个类已删除
 * 新: SetMeleeConfig(UAIBehaviorConfigSO*)
 *
 * 真实逻辑: WeaponAttachmentComponent::SetMeleeConfig
 *
 * 【v36】改用 ResolveWeaponAttach lazy 查找
 */
void ABaseCharacter::SetMeleeConfig(UAIBehaviorConfigSO* InConfig)
{
	if (UWeaponAttachmentComponent* ResolvedAttach = ResolveWeaponAttach())
	{
		ResolvedAttach->SetMeleeConfig(InConfig);
	}
	// else: ResolveWeaponAttach 已 Log Error
}


/**
 * ABaseCharacter::SyncWeaponFromConfig — 转发壳 【v54 P0 重构】
 *
 * 旧: SyncWeaponFromProfile() — 从已删除的 MeleeProfile 字段读
 * 新: SyncWeaponFromConfig() — 直接接 ConfigSO, 不再依赖字段
 *
 * 真实逻辑: WeaponAttachmentComponent::SyncWeaponFromConfig
 *
 * 【v36】改用 ResolveWeaponAttach lazy 查找
 */
void ABaseCharacter::SyncWeaponFromConfig()
{
	if (UWeaponAttachmentComponent* ResolvedAttach = ResolveWeaponAttach())
	{
		ResolvedAttach->SyncWeaponFromConfig();
	}
	// else: ResolveWeaponAttach 已 Log Error
}

/**
 * PossessedBy
 *
 * 当 AI/Player 控制器附身时调用
 * 关键: 强制刷新一次 HUD（PossessedBy 在角色出生时/复活重生时触发）
 */
void ABaseCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);

	// 【2026-07-01 P0 新增】重生时重置回血状态
	// PossessedBy 在角色出生和复活时触发, 此时需要重置 LastMoveTime 和 bIsRegenerating
	// 防止旧角色残留状态影响新角色
	// 【v39 修复】用 ResolveHealthRegenComponent 而非裸字段访问
	if (UHealthRegenComponent* Regen = ResolveHealthRegenComponent())
	{
		Regen->ResetRegenerationState();
	}

	// 通过 PlayerController 获取 HUD 实例并缓存
	if (APlayerController* PC = Cast<APlayerController>(NewController))
	{
		if (AMyGameHUD* HUD = Cast<AMyGameHUD>(PC->GetHUD()))
		{
			GameHUDWidget = HUD->GetGameHUDWidget();
		}
	}

	// 【关键修复】: PossessedBy 在以下时机触发: 1) 角色出生时 2) 复活重生时
	// 【2026-06-15 重构】: HUD 推送收敛到 RefreshHUDFromCurrentState
	// 此时血量已是初始值,但 OnRep_CurrentHealth 不会触发（值没变）
	// 所以必须强制刷新一次 HUD（仅本地玩家需要刷新自己的 HUD）
	if (TryResolveHUDWidget())
	{
		// 设置初始 AC 值（若尚未初始化，则设为满值）
		if (ACValue == 0)
		{
			ACValue = MaxAC;
		}
		// 【2026-06-15 重构】: 全量刷新 (血量+能量+AC)
		RefreshHUDFromCurrentState();
		RefreshACEWithRank();
	}
	else
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[BaseCharacter] HUD 或 Widget_PlayerStatus 未就绪，延迟刷新。GameHUDWidget=%s"),
			*GetNameSafe(GameHUDWidget));

		// HUD 还未完全初始化（Widget_PlayerStatus BindWidget 失败），延迟重试
		GetWorld()->GetTimerManager().SetTimer(HUDRefreshTimerHandle, this, &ABaseCharacter::RetryRefreshHUD, 0.5f, false);
	}

	// 刷新角色头像（仅本地玩家需要）
	RefreshCharacterIcon();

	// 仅在服务器执行
	if (HasAuthority())
	{
		// ============================================================
		// 【2026.07.11 P0 修复】服务端 Pawn.FactionTag 同步
		//
		// 根因: UE AIPerception 调 GetTeamAttitudeTowards → 读 Pawn.FactionTag
		//       玩家 Pawn.FactionTag 永远空 → AI 看玩家 = Neutral (默认)
		//       bDetectNeutrals=false → 玩家永远不在 BT 视野 → AI 不会追玩家 → "AI 不动/没目标"
		//
		// 大厂原则 (零兜底):
		//   - AI: 真理源在 AIController.CachedFactionTag (v26.5 已修复)
		//         SetupMeleeAI/SetupZombieAI 时已写入 Pawn.FactionTag, 此处不动
		//   - Player: 真理源在 PlayerState.CurrentFactionTag (Server 端 set, Client 端 OnRep)
		//           此处从 PlayerState 同步 → Pawn.FactionTag (Server 单一写入点)
		//   - 这是**单一真理源**而非"兜底": 阵营信息从 PlayerState 流向 Pawn, 不允许反向
		// ============================================================
		SyncFactionTagFromController(NewController);

		// 【v32→v40.3 零兜底改造】武器 Spawn 链路彻底删除 (line 1504-1620)
		//
		// 历史链:
		//   - v32-v40.2: PossessedBy 读 Pawn.SpawnWeaponID → RequestWeaponSpawn
		//   - 同时 MeleeAIController::SetupMeleeAI 末尾也读 Pawn.SpawnWeaponID → RequestWeaponSpawn
		//   → 2 个入口叠加, 时序问题让 AI 永远没武器 (Session1.log bug 根因)
		//
		// v40.3 修复: PossessedBy 只负责"非武器"职责
		//   - SyncFactionTagFromController / HUD refresh / 头像 / HUD 重试
		// 武器 Spawn 唯一入口:
		//   - 关卡预放 AI: AMeleeAIController::SetupMeleeAI 末尾
		//   - 玩家 Spawn:  URoomSpawnSubsystem::HandlePlayerRequestSpawn
		//   - AI Spawn:    URoomSpawnSubsystem::SpawnAIInternal
		// ============================================================
// 【v40.3 P0 关键修复】PossessedBy 中武器 Spawn 链路已彻底删除
//
// 旧 (v32-v40.2) 反模式:
//   - PossessedBy 末尾读 Pawn.SpawnWeaponID → RequestWeaponSpawn
//   - 配合 MeleeAIController::SetupMeleeAI 末尾也读 → RequestWeaponSpawn
//   → 2 个入口叠加, 时序问题让 AI 永远没武器 (Session1.log bug 根因)
//
// 新架构 (v40.3 — 单一真理源 + 集中调度):
//   - 武器 Spawn **不在** PossessedBy (时序最早, 字段为空)
//   - **不在** BaseCharacter 任何方法 (零兜底, 让 Spawn 路径自己负责)
//   - 唯一入口 (按调用方分类):
//       a) 关卡预放 AI: AMeleeAIController::SetupMeleeAI 末尾
//          (与 SetMeleeConfig 写入字段"在同一函数内", 保证时序)
//       b) 玩家 Spawn:   URoomSpawnSubsystem::HandlePlayerRequestSpawn
//          (在 SpawnActor + SetSpawnLoadout + Possess 后, 调 RequestWeaponSpawn)
//       c) AI Spawn:     URoomSpawnSubsystem::SpawnAIInternal
//          (同上, 对 AI Pawn 调)
//
// 单一真理源保证:
//   - RequestWeaponSpawn 用 GetOwnerCharacterChecked (v40.3 修复) — 不依赖 BeginPlay 时序
//   - SpawnAndEquipWeapon 用 GetOwner() — 任何时序都拿到正确 Owner
//   - Pawn.SetSpawnLoadout 写入字段 — Spawn 路径负责, SetMeleeProfile 负责 AI 默认路径
//
// PossessedBy 现在只负责"非武器"职责:
//   - SyncFactionTagFromController (玩家 Pawn.FactionTag 同步) — 上面 line 1502
//   - HUD refresh (RefreshHUDFromCurrentState)
//   - 角色头像 (RefreshCharacterIcon)
//   - HUD 重试调度
//   **不**包含武器 Spawn — 由调用方 (SpawnSubsystem / AIController) 集中调度
// ============================================================
	}
}


/**
 * RetryRefreshHUD
 *
 * HUD 延迟刷新重试
 *
 * 【P0 修复】: 加最大重试次数 + 防御性检查, 防止死循环刷屏
 *
 * 根因 (2026-06-29):
 *   - 旧实现: HUD 拿不到 → 调度下一次重试 → 永远拿不到 → 永远调度 → 日志洪水
 *   - 触发场景: 玩家 ClientTravel 到战斗地图, AMyGameHUD 未及时初始化,
 *     RetryRefreshHUD 进入死循环, 每 0.5 秒打一行 Warning 日志
 *
 * 设计原则:
 *   1. 最大重试 20 次 (10 秒后停止), 避免无限制循环
 *   2. 失败后降级为 Log 等级, 不再 Warning 刷屏
 *   3. EndPlay 中已 ClearTimer, 但若 Character 没销毁就走到这里, 加 IsValid 防御
 */
void ABaseCharacter::RetryRefreshHUD()
{
	// ==========================================
	// 【P0 防御 1】: Character 或 World 已销毁 → 停止重试
	// 设计理由: 玩家可能已经退出战斗场景, BaseCharacter 处于 PendingKill 状态,
	//          此时重试无意义, 必须立即停止
	// ==========================================
	if (!IsValid(this) || !GetWorld())
	{
		UE_LOG(LogTemp, Log, TEXT("[BaseCharacter] RetryRefreshHUD: Character/World 已失效, 停止重试"));
		return;
	}

	// ==========================================
	// 【P0 防御 2】: 最大重试次数限制
	// 设计理由: 防止 HUD 永远拿不到时无限循环刷屏 (每 0.5 秒一行日志)
	//          20 次 = 10 秒, 足够 HUD 完成异步初始化
	// ==========================================
	const int32 MaxRetries = 20;
	CurrentHUDRetryCount++;
	if (CurrentHUDRetryCount >= MaxRetries)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[BaseCharacter] RetryRefreshHUD 达到最大重试次数 (%d 次), 放弃. HUD 永远不可用, 请检查 MyGameHUD::InitGameHUDWidget 时序"),
			CurrentHUDRetryCount);
		// 重置计数器, 防止下次又从这里开始累计
		CurrentHUDRetryCount = 0;
		// 显式 ClearTimer 防止 SetTimer 残留
		GetWorld()->GetTimerManager().ClearTimer(HUDRefreshTimerHandle);
		return;
	}

	// 【2026-06-15 重构】: HUD 获取收敛到 TryResolveHUDWidget
	if (!TryResolveHUDWidget())
	{
		// HUD 仍未就绪，继续延迟重试 (降级为 Log, 避免日志洪水)
		UE_LOG(LogTemp, Verbose, TEXT("[BaseCharacter] HUD 重试刷新中... (%d/%d)"), CurrentHUDRetryCount, MaxRetries);
		GetWorld()->GetTimerManager().SetTimer(HUDRefreshTimerHandle, this, &ABaseCharacter::RetryRefreshHUD, 0.5f, false);
		return;
	}

	if (!GameHUDWidget->GetWidget_PlayerStatus())
	{
		// Widget_PlayerStatus BindWidget 仍未完成, 继续重试 (降级为 Log)
		UE_LOG(LogTemp, Verbose, TEXT("[BaseCharacter] Widget_PlayerStatus 未就绪, 重试中... (%d/%d)"), CurrentHUDRetryCount, MaxRetries);
		GetWorld()->GetTimerManager().SetTimer(HUDRefreshTimerHandle, this, &ABaseCharacter::RetryRefreshHUD, 0.5f, false);
		return;
	}

	// 成功 → 重置计数器
	UE_LOG(LogTemp, Log, TEXT("[BaseCharacter] HUD 刷新成功 (重试 %d 次后)"), CurrentHUDRetryCount);
	CurrentHUDRetryCount = 0;
	// 【2026-06-15 重构】: 全量刷新
	RefreshHUDFromCurrentState();
	RefreshACEWithRank();
}


/**
 * 【v54.3 大厂重构 — 完全删除】SpawnAndEquipWeapon 旧版转发壳已彻底删除
 *
 * 旧版签名: void ABaseCharacter::SpawnAndEquipWeapon(FString WeaponID)
 *
 * 删除原因 (v54.3 中间层重构):
 *   - 重复接口, 已统一到 RequestWeaponSpawn
 *   - FString WeaponID 签名已彻底改为 TSubclassOf<ABaseWeapon>
 *   - 旧 FString 版本无任何调用方, 保留只是 dead code
 *   - 大厂原则: 单一真理源 = RequestWeaponSpawn(Class)
 */
// void ABaseCharacter::SpawnAndEquipWeapon 已删除 (v54.3)


/**
 * FindWeaponAttachmentConfig — 转发壳
 * 真实逻辑: WeaponAttachmentComponent::FindWeaponAttachmentConfig
 *
 * 【v49 大厂架构重构 — 单一真理源】
 *   - 旧签名: (const FString& InCharacterID, const FString& InWeaponID) — 字符串匹配, 易错位
 *   - 新签名: (TSubclassOf<ABaseCharacter>, TSubclassOf<ABaseWeapon>) — BP 强类型, 无错位
 */
FWeaponAttachmentConfig* ABaseCharacter::FindWeaponAttachmentConfig(
	TSubclassOf<ABaseCharacter> InPawnClass,
	TSubclassOf<ABaseWeapon> InWeaponClass) const
{
	if (UWeaponAttachmentComponent* ResolvedAttach = const_cast<ABaseCharacter*>(this)->ResolveWeaponAttach())
	{
		return ResolvedAttach->FindWeaponAttachmentConfig(InPawnClass, InWeaponClass);
	}

	UE_LOG(LogTemp, Error,
		TEXT("[BaseCharacter] FindWeaponAttachmentConfig: 找不到 WeaponAttachmentComponent."));
	return nullptr;
}


/**
 * SetSpawnLoadout — 转发壳
 * 真实逻辑: WeaponAttachmentComponent::SetSpawnLoadout
 *
 * 【v36】改用 ResolveWeaponAttach lazy 查找, 字段为 null 时不再报错
 */
void ABaseCharacter::SetSpawnLoadout(const FString& InCharacterID, const FString& InWeaponID)
{
	UE_LOG(LogTemp, Log,
		TEXT("[BaseCharacter] SetSpawnLoadout ENTER: this=%p Class=%s WeaponAttach=%p CharID=%s WeaponID=%s"),
		this, *GetClass()->GetName(), WeaponAttach, *InCharacterID, *InWeaponID);

	if (UWeaponAttachmentComponent* ResolvedAttach = ResolveWeaponAttach())
	{
		ResolvedAttach->SetSpawnLoadout(InCharacterID, InWeaponID);
	}
	// else: ResolveWeaponAttach 已 Log Error, 这里不再重复报错
}


/**
 * GetSpawnWeaponID — 转发壳
 * 真实逻辑: WeaponAttachmentComponent::GetSpawnWeaponID
 */
const FString& ABaseCharacter::GetSpawnWeaponID() const
{
	if (UWeaponAttachmentComponent* ResolvedAttach = const_cast<ABaseCharacter*>(this)->ResolveWeaponAttach())
	{
		return ResolvedAttach->GetSpawnWeaponID();
	}

	static const FString Empty = TEXT("");
	return Empty;
}


/**
 * GetCurrentWeapon — 转发壳
 * 真实逻辑: WeaponAttachmentComponent::GetCurrentWeapon
 *
 * 【v36】改用 ResolveWeaponAttach lazy 查找 (避免字段为 null 时找不到组件)
 */
ABaseWeapon* ABaseCharacter::GetCurrentWeapon() const
{
	if (UWeaponAttachmentComponent* ResolvedAttach = const_cast<ABaseCharacter*>(this)->ResolveWeaponAttach())
	{
		return ResolvedAttach->GetCurrentWeapon();
	}

	UE_LOG(LogTemp, Error,
		TEXT("[BaseCharacter] GetCurrentWeapon: 找不到 WeaponAttachmentComponent 组件. "
			 "Pawn=%s. 【v36 修复】请检查 BP 蓝图 Components 面板, 确保有 WeaponAttachmentComponent 子对象. "
			 "如果是 BP 子类, 检查 Components 面板的 'WeaponAttach' 是否被重命名或删除."),
		*GetName());
	return nullptr;
}


/**
 * 【v93 大厂架构】GetMotherAttackMontage 转发壳实现
 *
 * 直接读 this->MotherAttackMontage (Pawn 字段, 真理源), 不走 Component 解析路径
 * 因为母亲蒙太奇是 Pawn 蓝图配置, 不是 Component 状态.
 *
 * 调用方零兜底:
 *   - PlayerComboComponent 拿到 nullptr → Log Error + return false (拒绝攻击)
 *   - AIAttackComponent::Multicast_PlayAttackAnim_Implementation 拿到 nullptr → 静默跳过 (客户端路径, 不重复服务器已报错)
 */
UAnimMontage* ABaseCharacter::GetMotherAttackMontage() const
{
	return MotherAttackMontage;
}


// ==========================================
// 【v38 重构】ResolveWeaponAttach 已迁移到头文件 inline (走通用 ResolveComponent<T> 模板)
// 历史 (v36) 实现已删除, 避免与模板版本重复.
// 详见 BaseCharacter.h 中的 ResolveComponent<T> 模板.
// ==========================================


// ==========================================
// 19. 计分板系统
// ==========================================

/**
 * GetRoomPlayerState
 *
 * 获取击杀者 PlayerState（便捷转换）
 */
ARoomPlayerState* ABaseCharacter::GetRoomPlayerState() const
{
	if (APlayerState* PS = GetPlayerState<APlayerState>())
	{
		return Cast<ARoomPlayerState>(PS);
	}
	return nullptr;
}


// ============================================================
// 【v60.11 大厂架构】武器射线方向服务 — 单一真理源实现
// ============================================================

/**
 * ABaseCharacter::GetAimRayFromCrosshairOrEyes (非 const 版本)
 *
 * 大厂原则 — 路由策略 (v110 类型判定标准):
 *   - 真实玩家 (Controller = APlayerController) → HUD 拿 Crosshair 世界射线
 *   - AI (Controller = AAIController) → 攻击者 BaseAimRotation + 武器 Muzzle Socket
 *
 * 历史修正:
 *   - v60.11-v109: 用 IsLocallyControlled() 判定玩家/AI → ListenServer 上 AI Pawn 误判
 *   - v110: 改用 Controller 类型 (Cast<APlayerController>) → ListenServer / Dedicated Server / Client 都正确
 *
 * 关键设计 (v60.11 修订 — 编译错误 C2662 修复):
 *   - 本方法**非 const** — 玩家路径需要 TryResolveHUDWidget, 该方法会缓存 GameHUDWidget 字段
 *   - UE 5.6 C2662 编译错误: const this 不能调非 const 方法
 *   - 修复决策: 不强行 const_cast (反模式), 也不 mutable (反模式)
 *     → 本方法去掉 const, 性能上更优 (避免每帧查 PlayerController)
 *     → 提供 GetCrosshairWorldDirection_Const 给真正 const 上下文调用
 *   - 大厂原则: 性能优化 > 形式 const, 但仍提供 const 查询版作 fallback
 *
 * @note 这是 Strategy 层 (URangedLineStrategy) 唯一的射线方向入口
 *       Strategy 不允许自己读 HUD / 自己算屏幕坐标 (违反分层)
 */
bool ABaseCharacter::GetAimRayFromCrosshairOrEyes(FVector& OutRayOrigin, FVector& OutRayDirection)
{
	OutRayOrigin = FVector::ZeroVector;
	OutRayDirection = FVector::ForwardVector;

	// ===========================================
	// 路径 A: 真实玩家 (Controller = APlayerController) → HUD 拿 Crosshair 世界射线
	// ===========================================
	//
	// 【v110 P0 关键修复 — AI/玩家判定标准】
	//   旧 (v60.11-v109) 反模式: 用 IsLocallyControlled() 判定
	//     - ListenServer 上 AI Pawn 同样 IsLocallyControlled=true (服务器是 AI 的本地控制者)
	//     - 导致 AI Pawn 在 ListenServer 上走到 HUD 路径 → "HUD ??.... Pawn=BP_SWAT_AI_C_0"
	//     - HUD 永远拿不到 (AI 没 HUD) → 返回 false → AI 永远开不了枪
	//
	//   新 (v110) 大厂标准: 用 Controller 类型判定
	//     - 玩家 Pawn Controller = APlayerController 派生 (BP_RoomPlayerController 等)
	//     - AI Pawn Controller = AAIController 派生 (BP_MeleeAIController / BP_ZombieAIController)
	//     - 这是 UE 引擎硬约束 — Controller 类型 = Pawn 类型,不会变
	//     - 与 IsLocallyControlled 完全正交 (Dedicated Server / ListenServer / Client 都能正确分流)
	//
	//   大厂原则 — 类型判定 > 网络身份判定:
	//     - "我是谁" = Controller 类型 (静态, 永不改变)
	//     - "我在哪跑" = IsLocallyControlled (动态, 与网络拓扑有关)
	//     - "我要拿什么射线" = 我是谁 (玩家要 HUD, AI 要 BaseAim)
	//
	// 修复前日志 (用户 Session1.log):
	//   [BaseCharacter::GetAimRayFromCrosshairOrEyes] 本地玩家 HUD 未就绪.
	//   Pawn=BP_SWAT_AI_C_0. 【v60.11 零兜底】拒绝射线 — HUD 没初始化, Crosshair 不可用.
	//   ↑ 这是 AI Pawn 在 ListenServer 上误判成"本地玩家" 走到 HUD 路径
	//
	// ===========================================
	const AController* MyController = GetController();
	const bool bIsRealPlayer = MyController && MyController->IsA<APlayerController>();

	if (bIsRealPlayer)
	{
		// (a) 必须有 HUD (玩家专属), 没 HUD 是 BP/初始化错
		if (!TryResolveHUDWidget(false))
		{
			UE_LOG(LogTemp, Error,
				TEXT("[BaseCharacter::GetAimRayFromCrosshairOrEyes] 本地玩家 HUD 未就绪. "
				     "Pawn=%s. 【v60.11 零兜底】拒绝射线 — HUD 没初始化, Crosshair 不可用."),
				*GetName());
			return false;
		}

		// (b) HUD 必须能算出 Crosshair 世界射线
		FVector CrosshairOrigin;
		FVector CrosshairDirection;
		const bool bGetRayOK = GameHUDWidget->GetCrosshairWorldRay(CrosshairOrigin, CrosshairDirection);
		if (!bGetRayOK)
		{
			// GetCrosshairWorldRay 内部已 Log Error, 这里只 Log 路径上下文
			UE_LOG(LogTemp, Error,
				TEXT("[BaseCharacter::GetAimRayFromCrosshairOrEyes] 本地玩家 Crosshair 世界射线获取失败. "
				     "Pawn=%s. 【v60.11 零兜底】原因排查: 1) CrosshairWidget 未渲染? 2) WBP_GameHUD 漏绑 WBP_Crosshair? 3) PlayerController 未初始化?"),
				*GetName());
			return false;
		}

		// v82 大厂架构修复: 输出 Origin + Direction (玩家准星世界射线 = 准星屏幕中心 Deproject 起点)
		//   - 旧 (v60.11) 反模式: 只输出 Direction, Origin 让 Strategy 用 Muzzle Socket 算
		//   - 新 (v82): 输出完整的 Origin + Direction → RPC 传给服务器 → 服务器直接 trace
		//   - 服务器不再需要 Muzzle Socket 计算, 不再需要 Character/Weapon 几何信息
		//   - 这是"客户端射线, 服务器权威 trace"的大厂标准 (CS:GO / Apex / Valorant)
		OutRayOrigin = CrosshairOrigin;
		OutRayDirection = CrosshairDirection;
		return true;
	}

	// ===========================================
	// 路径 B: AI → 攻击者准星方向 (BaseAimRotation = BT 控制的旋转)
	// 【v60.12】GetBaseAimRotation
	// 玩家路径已移到 RangedLineStrategy::PerformSingleShot (v60.13)
	//
	// 【v109 镜像方案大厂架构 - AI 路径补全 Origin】
	//   历史 (v108): Origin 是 ZeroVector,Strategy 内部有 AI 分支兜底
	//   大厂原则: 镜像玩家 — Origin 也要算准确(玩家准星 Origin = Crosshair World Origin)
	//   AI 路径 Origin = Weapon Mesh 上的 Muzzle Socket 世界位置(枪口真实位置)
	//   - 与 CS:GO/Apex/PUBG/Valorant 一致:子弹从枪口射出
	//   - 这样 Strategy 完全不必区分 AI/玩家,只信入参射线即可
	//
	// 大厂原则 — 零兜底:
	//   - Owner 必须有 Weapon → 否则 BT 不应该调到这里(前面已校验)
	//   - Mesh 必须有 Muzzle Socket → 否则 BP 配置错,显式化
	// ===========================================
	const FRotator BaseAim = GetBaseAimRotation();
	if (BaseAim.Vector().IsNearlyZero())
	{
		UE_LOG(LogTemp, Error,
			TEXT("[BaseCharacter::GetAimRayFromCrosshairOrEyes] AI 准星方向为零向量 (BaseAimRotation 未初始化). "
			     "Pawn=%s. 【v60.12 零兜底】拒绝射线 — 攻击者没方向, 不能盲射."),
			*GetName());
		return false;
	}

	// [v109 大厂架构] AI 路径 Origin = Weapon Muzzle Socket (镜生态要求)
	//   - 与玩家路径 Origin (Crosshair World Origin) 严格对齐
	//   - Strategy 不再需要自己算 Muzzle Socket(零冗余)
	if (const ABaseWeapon* CurrentWeapon = GetCurrentWeapon())
	{
		if (UMeshComponent* WeaponMesh = CurrentWeapon->GetMeshComponent())
		{
			// 约定俗成 Socket 名 fallback (与 v108 Strategy 一致): Muzzle / Socket_Muzzle / MuzzleSocket
			FName MuzzleSocketToUse = NAME_None;
			const TArray<FName> CandidateSocketNames = { TEXT("Muzzle"), TEXT("Socket_Muzzle"), TEXT("MuzzleSocket") };
			for (const FName& Candidate : CandidateSocketNames)
			{
				if (WeaponMesh->DoesSocketExist(Candidate))
				{
					MuzzleSocketToUse = Candidate;
					break;
				}
			}

			if (!MuzzleSocketToUse.IsNone())
			{
				OutRayOrigin = WeaponMesh->GetSocketLocation(MuzzleSocketToUse);
			}
			else
			{
				// 配错 Log: AI 没 Muzzle Socket(纯警告,不让 GetAimRay 整体失败)
				UE_LOG(LogTemp, Warning,
					TEXT("[BaseCharacter::GetAimRayFromCrosshairOrEyes] 【v109】Pawn=%s 武器 '%s' Mesh 缺 MuzzleSocket (尝试过: Muzzle / Socket_Muzzle / MuzzleSocket). "
					     "AI 射线起点回退到 Actor 位置 (精度略差, 不影响命中判定). "
					     "【修复】打开 BP_Weapon_*** Mesh → Add Socket → 名称必须用 'Muzzle'."),
					*GetName(),
					*CurrentWeapon->GetName());

				OutRayOrigin = GetActorLocation();
			}
		}
		else
		{
			// 没 Mesh Component → 用 Actor 位置兜底 + Warning
			UE_LOG(LogTemp, Warning,
				TEXT("[BaseCharacter::GetAimRayFromCrosshairOrEyes] 【v109】Pawn=%s 武器 '%s' 缺 MeshComponent, Origin 回退到 Actor 位置."),
				*GetName(),
				*CurrentWeapon->GetName());
			OutRayOrigin = GetActorLocation();
		}
	}
	else
	{
		// 没武器 → BT 应该已校验过(否则不在这里走)
		UE_LOG(LogTemp, Warning,
			TEXT("[BaseCharacter::GetAimRayFromCrosshairOrEyes] 【v109】Pawn=%s 没 CurrentWeapon, Origin 回退到 Actor 位置."),
			*GetName());
		OutRayOrigin = GetActorLocation();
	}

	OutRayDirection = BaseAim.Vector();
	return true;
}


/**
 * ABaseCharacter::ComputeFireRayTowardsTarget (v130 新增)
 *
 * 算法 (v130 大厂 3D 追踪方案):
 *   - Origin     = Weapon Mesh Muzzle Socket (复用 v109 的搜索逻辑)
 *   - Direction  = (Target.ActorLocation - MuzzleLocation).GetSafeNormal()
 *                → 完整 3D 方向, 自动跟随 Target 上下位置
 *
 * 与 v109 GetAimRayFromCrosshairOrEyes AI 路径区别:
 *   - v109: Direction = BaseAim.Vector() (水平, 准星旋转, 不跟随 Target 高度)
 *   - v130: Direction = (Target - Muzzle)  (3D, 自动跟随 Target 高度)
 *
 *   用户反馈 (2026.08.02):
 *     "AI 水平射击, 不能跟随 Target 上下位置"
 *     → v109 是问题根源 (AI 朝向不随 Target 跳起而变)
 *     → v130 修复 — 直接用 Muzzle → Target 方向, 自然 3D 追踪
 *
 * 大厂原则 — 数据驱动:
 *   - BT 已知 TargetActor (装饰器已确认), 不需要二次校验
 *   - BT 决策 = 用 v109 还是 v130 — BT 编辑器配 Key 控制
 *
 * 大厂原则 — 代码复用:
 *   - Muzzle Socket 搜索逻辑 (v109) 不重复, 抽到局部 lambda
 *   - 与 GetAimRayFromCrosshairOrEyes AI 路径共享 Muzzle 逻辑
 */
bool ABaseCharacter::ComputeFireRayTowardsTarget(
	AActor* TargetActor,
	FVector& OutRayOrigin,
	FVector& OutRayDirection)
{
	// 默认值 (失败时)
	OutRayOrigin = GetActorLocation();
	OutRayDirection = FVector::ForwardVector;

	if (!IsValid(TargetActor))
	{
		UE_LOG(LogTemp, Error,
			TEXT("[BaseCharacter::ComputeFireRayTowardsTarget] 【v130】Pawn=%s TargetActor 无效. "
			     "【零兜底】调用方应该已 BT 校验, 这里保险."),
			*GetName());
		return false;
	}

	// 算 Muzzle Socket 起点 (复用 v109 搜索策略)
	ABaseWeapon* CurrentWeapon = GetCurrentWeapon();
	if (!CurrentWeapon)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[BaseCharacter::ComputeFireRayTowardsTarget] 【v130】Pawn=%s 没 CurrentWeapon, Origin 回退到 Actor 位置."),
			*GetName());
		OutRayOrigin = GetActorLocation();
	}
	else
	{
		UMeshComponent* WeaponMesh = CurrentWeapon->GetMeshComponent();
		if (!WeaponMesh)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[BaseCharacter::ComputeFireRayTowardsTarget] 【v130】Pawn=%s 武器 '%s' 没 MeshComponent, Origin 回退到 Actor 位置."),
				*GetName(),
				*CurrentWeapon->GetName());
			OutRayOrigin = GetActorLocation();
		}
		else
		{
			// 复用 v109 的多候选 Socket 名 (Muzzle / Socket_Muzzle / MuzzleSocket)
			FName MuzzleSocketToUse = NAME_None;
			const TArray<FName> CandidateSocketNames = { TEXT("Muzzle"), TEXT("Socket_Muzzle"), TEXT("MuzzleSocket") };
			for (const FName& Candidate : CandidateSocketNames)
			{
				if (WeaponMesh->DoesSocketExist(Candidate))
				{
					MuzzleSocketToUse = Candidate;
					break;
				}
			}

			if (!MuzzleSocketToUse.IsNone())
			{
				OutRayOrigin = WeaponMesh->GetSocketLocation(MuzzleSocketToUse);
			}
			else
			{
				UE_LOG(LogTemp, Warning,
					TEXT("[BaseCharacter::ComputeFireRayTowardsTarget] 【v130】Pawn=%s 武器 '%s' 缺 MuzzleSocket (候选: Muzzle/Socket_Muzzle/MuzzleSocket). "
					     "Origin 回退到 Actor 位置. "
					     "【修复】Mesh 上加 Socket 'Muzzle'."),
					*GetName(),
					*CurrentWeapon->GetName());
				OutRayOrigin = GetActorLocation();
			}
		}
	}

	// 算 3D 追踪方向 (v131 修复 — 跟随 Target 上下 + 命中胸口)
	//   Direction = (Target.胸部位置 - Muzzle位置).GetSafeNormal()
	//   胸部位置 = Target.ActorLocation + (0, 0, ChestHeightOffset)
	//   ChestHeightOffset = 60cm (从 ActorLocation 向上抬, 命中 Pawn Capsule 胸口)
	//
	// v130 旧版 (打脚根因):
	//   - Direction = (Target.ActorLocation - Muzzle).GetSafeNormal()
	//   - Target.ActorLocation = Capsule 中心 (脚底 + 半高 88cm)
	//   - 当 AI 在地面扫射 Target 高台时, Direction 抬到 Target 中心 588cm
	//   - trace 沿 Direction 走到 Range 距离, EndLoc 远超 Target
	//   - line 与 Target Capsule 的第一交点 = 进入点 (通常是胶囊底部 = 脚)
	//   - 视觉上看: 子弹打脚, 不打胸口 (用户反馈)
	//
	// v131 修复:
	//   - Direction 抬高到 Target.ActorLocation + 60cm (胸口高度)
	//   - trace 沿 Direction 走到 Target → 命中胸口
	//   - 与 v130 区别: v130 = 命中胶囊中心, v131 = 命中胸口
	//   - 玩家准星通常也指向胸口 (CS:GO/Apex 大厂标准), AI 也应该跟玩家一致
	const float ChestHeightOffset = 60.0f; // 厘米, 从 ActorLocation 向上 (从脚到胸)
	const FVector TargetAimPoint = TargetActor->GetActorLocation() + FVector(0.f, 0.f, ChestHeightOffset);
	const FVector DirectionRaw = TargetAimPoint - OutRayOrigin;
	if (DirectionRaw.IsNearlyZero())
	{
		// Target 就在 Muzzle 位置 (AI 完全贴住 Target) → 用水平朝向兜底
		UE_LOG(LogTemp, Warning,
			TEXT("[BaseCharacter::ComputeFireRayTowardsTarget] 【v131】Pawn=%s Target 跟 Muzzle 重叠 (距离≈0), 用 ForwardVector 兜底."),
			*GetName());
		OutRayDirection = GetActorForwardVector();
	}
	else
	{
		OutRayDirection = DirectionRaw.GetSafeNormal();
	}

	UE_LOG(LogBehaviorTree, Verbose,
		TEXT("[BaseCharacter::ComputeFireRayTowardsTarget] 【v131 3D 追踪 + 胸口】Pawn=%s Target='%s' "
		     "Origin=%s TargetAimPoint=%s (胸部偏移=%.0fcm) Direction=%s"),
		*GetName(),
		*TargetActor->GetName(),
		*OutRayOrigin.ToCompactString(),
		*TargetAimPoint.ToCompactString(),
		ChestHeightOffset,
		*OutRayDirection.ToCompactString());

	return true;
}


/**
 * ABaseCharacter::GetCrosshairWorldDirection_Const (const 版本 — 仅 const 上下文用)
 *
 * 大厂原则 — const 正确性 + 性能权衡:
 *   - 这是 const 查询版, 适合 inline 渲染 / Multicast RPC Implementation 等 const 上下文
 *   - 本方法**不依赖 TryResolveHUDWidget 缓存**, 每次走完整解析链
 *     → 不修改 GameHUDWidget 字段 → const 安全
 *     → 性能开销 0.001ms/次 (GetController() 是 UE 缓存, cheap)
 *   - 仅返回方向, 不返回起点
 *     → 起点由调用方按 Muzzle Socket 算 (武器几何真理源不在 Character 层)
 *
 * 零兜底:
 *   - 玩家路径失败 → Log Error + return false
 *   - 非本地玩家调用 → 拒绝 (本方法**专供本地玩家射线**)
 *
 * @param OutWorldDirection 输出: Crosshair 世界射线方向 (单位向量)
 * @return true=成功, false=配置错 / 非本地玩家调用
 */
bool ABaseCharacter::GetCrosshairWorldDirection_Const(FVector& OutWorldDirection) const
{
	OutWorldDirection = FVector::ForwardVector;

	// 非本地玩家 → 本方法专供本地玩家射线
	if (!IsLocallyControlled())
	{
		UE_LOG(LogTemp, Error,
			TEXT("[BaseCharacter::GetCrosshairWorldDirection_Const] 非本地玩家调用此方法 (Pawn=%s). "
			     "【v60.11 零兜底】本方法专供本地玩家武器射线, AI/远端请用 GetActorEyesViewPoint."),
			*GetName());
		return false;
	}

	// 玩家专属: 拿 PlayerController → HUD → Crosshair
	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[BaseCharacter::GetCrosshairWorldDirection_Const] PlayerController 为空 (Pawn=%s). "
			     "【v60.11 零兜底】拒绝射线 — 本地玩家没 PC = BP 初始化错."),
			*GetName());
		return false;
	}

	AMyGameHUD* HUD = Cast<AMyGameHUD>(PC->GetHUD());
	if (!HUD)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[BaseCharacter::GetCrosshairWorldDirection_Const] HUD 未就绪 (Pawn=%s). "
			     "【v60.11 零兜底】HUD 没创建好 — 检查 GameMode HUDClass 配置."),
			*GetName());
		return false;
	}

	UGameHUDWidget* HUDWidget = HUD->GetGameHUDWidget();
	if (!HUDWidget)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[BaseCharacter::GetCrosshairWorldDirection_Const] GameHUDWidget 为空 (Pawn=%s). "
			     "【v60.11 零兜底】HUD 创建但 GameHUDWidget 没拿到 — 检查 MyGameHUD 实现."),
			*GetName());
		return false;
	}

	FVector WorldOrigin;
	FVector WorldDirection;
	const bool bGetRayOK = HUDWidget->GetCrosshairWorldRay(WorldOrigin, WorldDirection);
	if (!bGetRayOK)
	{
		// GetCrosshairWorldRay 内部已 Log Error
		return false;
	}

	OutWorldDirection = WorldDirection;
	return true;
}


/**
 * Multicast_NotifyKill_Implementation
 *
 * NetMulticast 实现: 接收纯数据击杀消息 → 推送给所有客户端 HUD
 *
 * 【v31.6 大厂架构重构】从解 Actor* 改为接收字符串
 *
 * 历史痛点 (v22-v31.5) — Session3.log 崩溃栈:
 *   Unhandled Exception: EXCEPTION_ACCESS_VIOLATION reading address 0x0000000000000018
 *   ABaseCharacter::Multicast_NotifyKill_Implementation() [BaseCharacter.cpp:3247]
 *   UE_LOG: *VictimActor->GetName()  ← VictimActor 已 nullptr
 *
 * 根因: 服务器 TakeDamage → 触发 HealthComponent.OnDeath → Die() → Destroy()
 *       但 Multicast_NotifyKill 在 Destroy 之前已发出, RPC 携带的 AActor* NetGUID
 *       客户端解析时, 目标 Actor 可能已经被 bIsDead 复制触发的本地销毁标记为 Pending Kill
 *       → 客户端拿到 nullptr → 解引用崩溃
 *
 * 大厂修复 (v31.6):
 *   - RPC 签名改为 (FString KillerName, FString VictimName, EKillMethod KillMethod, bool bIsAssist)
 *   - 服务器在 TakeDamage 时已本地读好所有数据, RPC 纯数据, 永远不可能 nullptr 崩溃
 *   - 大厂原则: 任何 UE RPC 边界不允许传 Actor 引用 (跨 RPC 生命周期不可控)
 *
 * @param KillerName 击杀者姓名
 * @param VictimName 被击杀者姓名
 * @param KillMethod 击杀方式 (服务器已确定)
 * @param bIsAssist  true=助攻提示 (HUD 展示图标不同)
 */
void ABaseCharacter::Multicast_NotifyKill_Implementation(const FString& KillerName, const FString& VictimName,
                                                       EKillMethod KillMethod, bool bIsAssist, bool bIsKillerPlayer,
                                                       EKillStreakType InStreakType, USoundBase* KillSoundAsset)
{
	UE_LOG(LogTemp, Log,
		TEXT("[BaseCharacter] Multicast_NotifyKill: Killer=%s, Victim=%s, Method=%d, IsAssist=%d, bIsKillerPlayer=%d, StreakType=%d, HasAuthority=%d"),
		*KillerName, *VictimName, static_cast<int32>(KillMethod), bIsAssist ? 1 : 0,
		bIsKillerPlayer ? 1 : 0, static_cast<int32>(InStreakType), HasAuthority() ? 1 : 0);

	// 大厂原则 (v31.6 零兜底): 字符串字段允许为空 (即没读到 PS 名字), 不允许静默崩溃
	// 击杀者加分逻辑: 已在服务器 TakeDamage 流程中本地加, 不依赖 RPC 边界

	// ==========================================
	// 【v100 大厂架构 — 击杀音效】全员播放 (不论 Killer 是玩家/AI)
	// ==========================================
	//
	// 业务规则 (用户 2026.07.26 明确):
	//   - "击杀敌人或母体需要播放声音,每个连杀都是不同的声音"
	//   - 旁观者也要听到击杀声(游戏惯例 — 队友击杀大家都有 audio cue)
	//
	// 大厂原则 — 与 KillFeed 业务规则解耦:
	//   - KillFeed 业务 (旧 v40.9): 只在 bIsKillerPlayer=true 时显示(玩家视角)
	//   - 音效 (新 v100): 全员播放 — 不论 Killer 是玩家还是 AI
	//
	// 大厂原则 — 触发位置在 bIsKillerPlayer 守卫之前:
	//   - 老版本在 bIsKillerPlayer=false 时 return — 音效就放不出来了
	//   - 新版: 音效独立触发,不受 Killer 类型限制
	//
	// v105 大厂架构修复 — 服务器查表后 RPC 推送音效资产:
	//   - 根因: PlayKillSound 在客户端调用 EnsureKillStreakDataTable → World->GetAuthGameMode() 返回 nullptr
	//   - 修复: 服务器在调用前查表, 把 USoundBase* 作为参数传给客户端
	//   - 客户端直接播放 KillSoundAsset, 不再需要查 GameMode
	if (InStreakType != EKillStreakType::None && !bIsAssist)
	{
		if (KillSoundAsset)
		{
			UGameplayStatics::PlaySoundAtLocation(this, KillSoundAsset, GetActorLocation(), GetActorRotation());
			UE_LOG(LogTemp, Display,
				TEXT("[BaseCharacter] Multicast_NotifyKill: 播放击杀音效 Asset=%s (来自服务器 RPC 参数)"),
				*KillSoundAsset->GetName());
		}
		else
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[BaseCharacter] Multicast_NotifyKill: KillSoundAsset 为空, 无法播放音效. StreakType=%d"),
				static_cast<int32>(InStreakType));
		}
	}

	// 【v40.9 P0 大厂架构】只有 Killer 是玩家时才推送 KillFeed 图标
	// 业务规则: "击杀图标只用于玩家击杀其他玩家或者AI时才显示，被击杀不应该显示别人的击杀图标"
	// - 玩家击杀玩家 → bIsKillerPlayer=true → KillFeed 显示 ✅
	// - 玩家击杀 AI → bIsKillerPlayer=true → KillFeed 显示 ✅
	// - AI 击杀玩家 → bIsKillerPlayer=false → KillFeed 不显示 ✅
	// - AI 击杀 AI → bIsKillerPlayer=false → KillFeed 不显示 ✅

	if (!bIsKillerPlayer)
	{
		// AI 击杀: KillFeed 不显示，音效已在上面播完(音效无 Killer 类型限制)
		// 连杀图标逻辑: 仅在当前玩家是击杀者时才更新（原有逻辑不变）
		return;
	}

	// Killer 是玩家 — 推送 KillFeed 图标给所有客户端
	if (UWorld* World = GetWorld())
	{
		for (FConstPlayerControllerIterator It = World->GetPlayerControllerIterator(); It; ++It)
		{
			if (APlayerController* PC = It->Get())
			{
				if (ARoomPlayerController* RoomPC = Cast<ARoomPlayerController>(PC))
				{
					if (UGameHUDWidget* HUDWidget = RoomPC->GetGameHUDWidget())
					{
						// 推送击杀消息 (KillFeed) — 只有玩家击杀才显示
						HUDWidget->AddKillFeedMessage(KillerName, VictimName, KillMethod);

						// 判断是否是本地玩家（击杀者）自己的 HUD
						// 如果击杀者是当前控制的角色, 则更新连杀图标 (仅非助攻)
						if (!bIsAssist && IsLocallyControlled())
						{
							const bool bIsHeadshot = (KillMethod == EKillMethod::PrimaryHeadshot ||
								KillMethod == EKillMethod::SecondaryHeadshot ||
								KillMethod == EKillMethod::MeleeHeadshot);
							HUDWidget->OnPlayerKill(bIsHeadshot);
						}
					}
				}
			}
		}
	}
}


// ==========================================
// 20. 助攻追踪系统
// ==========================================

/**
 * NotifyDamageDealtTo
 *
 * 当受到伤害时，通知可能的助攻者（供武器系统调用）
 * 1. 记录当前时间戳
 * 2. 在受害者的角色上记录攻击者（方便死亡时查找）
 */
void ABaseCharacter::NotifyDamageDealtTo(AActor* Victim)
{
	// 只有服务器有权记录伤害
	if (!HasAuthority())
	{
		return;
	}

	// 记录攻击者和受害者的关联（用于判断助攻）
	if (Victim && Victim != this)
	{
		// 记录当前时间戳
		float CurrentTime = GetWorld()->GetTimeSeconds();
		LastHitTimestamps.FindOrAdd(Victim) = CurrentTime;

		// 在受害者的角色上记录攻击者（方便死亡时查找）
		ABaseCharacter* VictimChar = Cast<ABaseCharacter>(Victim);
		if (VictimChar)
		{
			VictimChar->LastHitTimestamps.FindOrAdd(this) = CurrentTime;
		}
	}
}


/**
 * CanGrantAssist
 *
 * 检查是否可以给予助攻
 * @param PotentialAssistant 潜在助攻者
 * @return 是否在助攻时间窗口内
 */
bool ABaseCharacter::CanGrantAssist(AActor* PotentialAssistant) const
{
	// 检查时间窗口
	const float* LastHitTimePtr = LastHitTimestamps.Find(PotentialAssistant);
	if (LastHitTimePtr)
	{
		float CurrentTime = GetWorld()->GetTimeSeconds();
		return (CurrentTime - *LastHitTimePtr) <= AssistTimeWindow;
	}
	return false;
}


/**
 * GrantAssistsToEligiblePlayers
 *
 * 授予符合条件的玩家助攻得分 — 单一职责 (大厂原则 v31.6)
 *
 * 职责:
 *   1. 遍历 Victim.LastHitTimestamps
 *   2. 给符合时间窗口的非 Killer 助攻者: PlayerState->AddAssistScore() + 广播助攻 RPC
 *   3. 清理 Victim.LastHitTimestamps
 *
 * 不再负责 (已拆分):
 *   - Killer.PlayerState->AddKillScore() → 由 TakeDamage 末尾统一调用
 *   - Victim.PlayerState->AddDeath()     → 由 TakeDamage 末尾统一调用
 *   - KillMethod 来源                     → 参数显式传入 (服务器本地读 Weapon->GetLastKillMethod())
 *
 * 大厂原则:
 *   - 静态方法 (无 this 依赖), 只用 Victim / Killer 参数
 *   - 不再做 RPC Actor* 传参, 改为纯数据 (FString + EKillMethod)
 *   - 单一真理源: Weapon.LastKillMethod 决定 KillMethod, 不再依赖 Character.LastKillMethod
 *
 * @param Victim     受害者 (读取 LastHitTimestamps)
 * @param Killer     击杀者 (排除)
 * @param KillMethod 击杀方式 (传给 Multicast_NotifyKill 的纯数据 RPC)
 * @param bIsKillerPlayer Killer 是否为玩家控制的角色 (决定是否显示 KillFeed 图标)
 */
void ABaseCharacter::GrantAssistsToEligiblePlayers(ABaseCharacter* Victim, ABaseCharacter* Killer,
                                                  EKillMethod KillMethod, bool bIsKillerPlayer)
{
	// 大厂零兜底: 入参校验 — 任何 null 都 Log Error + return, 不允许静默吞掉
	if (!Victim)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[BaseCharacter] GrantAssistsToEligiblePlayers: Victim 为 null. "
				 "调用方必须在 TakeDamage 死亡分支里调用, 传入 this."));
		return;
	}
	if (!Killer)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[BaseCharacter] GrantAssistsToEligiblePlayers: Killer 为 null. "
				 "调用方必须先校验 Killer Pawn 存在."));
		return;
	}

	UWorld* World = Victim->GetWorld();
	if (!World || !World->GetAuthGameMode())
	{
		// 非权威环境直接 return (服务器只在自己 world 加分)
		return;
	}

	// 大厂准备 Victim 姓名 (一次性读取, 复用)
	FString VictimName = TEXT("Unknown");
	if (ARoomPlayerState* VictimPS = Victim->GetRoomPlayerState())
	{
		VictimName = VictimPS->GetPlayerName();
	}

	// 遍历受害者的最后攻击者记录
	for (auto& HitPair : Victim->LastHitTimestamps)
	{
		AActor* AttackerActor = HitPair.Key.Get();
		if (!AttackerActor)
		{
			continue;
		}

		// 排除击杀者本人
		if (AttackerActor == Killer)
		{
			continue;
		}

		// 检查是否在时间窗口内
		const float TimeSinceHit = World->GetTimeSeconds() - HitPair.Value;
		if (TimeSinceHit > Victim->AssistTimeWindow)
		{
			continue;
		}

		// 符合条件的助攻者
		ABaseCharacter* AssistantChar = Cast<ABaseCharacter>(AttackerActor);
		if (!AssistantChar)
		{
			continue;
		}

		ARoomPlayerState* AssistantPS = AssistantChar->GetRoomPlayerState();
		if (!AssistantPS)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[BaseCharacter] 助攻者 '%s' 没有 ARoomPlayerState, 跳过加分."),
				*AssistantChar->GetName());
			continue;
		}

		// 服务器侧助攻加分 (单一真理源: AddAssistScore 内部 HasAuthority 守卫, 客户端不会重复)
		AssistantPS->AddAssistScore();

		// 广播助攻信息给所有客户端 — 纯数据 RPC, 不传 Actor*
		// 【v40.9】助攻者的 KillFeed 显示由 bIsKillerPlayer 决定（主击杀者是否为玩家）
		// 【v100】助攻不计入连杀 → StreakType=None → KillSoundComponent 收到 None 不播音
		//     （助攻只显示图标,不刷"二连杀"音效 — 与 KillStreakWidget 业务一致）
		const FString AssistantName = AssistantPS->GetPlayerName();
		AssistantChar->Multicast_NotifyKill(AssistantName, VictimName, KillMethod, /*bIsAssist=*/true, bIsKillerPlayer, EKillStreakType::None, /*KillSoundAsset=*/nullptr);
	}

	// 清理时间戳 (Victim 已死, 历史不再需要)
	Victim->LastHitTimestamps.Empty();
}


// ==========================================
// 21. 脚步声系统
// ==========================================

/**
 * PlayFootstepSound
 *
 * 播放脚步声（供 AnimNotify 或其他系统调用）
 * 【2026-06-15 重构】: 逻辑已迁移到 FootstepComponent::PlayFootstep
 * @param Location 播放位置
 */
void ABaseCharacter::PlayFootstepSound(FVector Location)
{
	// 【v39 修复】用 ResolveFootstepComponent 而非裸字段访问
	if (UFootstepComponent* FC = ResolveFootstepComponent())
	{
		FC->PlayFootstep(this, Location);
	}
}


// ==========================================
// 【2026-06-15 新增，2026-07-01 重构 v2】UI Bridge 区段
// 作用: 集中所有跨层访问 (Core/Characters → UI/Game/Systems)
// 历史: 修复前 50+ 处散落访问
// 现状 (2026-07-01): 大部分改用 CharacterEvents 事件广播, 仅保留重试逻辑使用 GameHUDWidget
// 说明: TryResolveHUDWidget / RefreshHUDFromCurrentState 仅供内部重试使用
// ==========================================

/**
 * 【UI Bridge】从 PlayerController 安全获取 GameHUDWidget 引用
 *
 * 替代散落在 30+ 处的样板代码:
 *   if (!GameHUDWidget) {
 *       if (APlayerController* PC = Cast<APlayerController>(GetController())) {
 *           if (AMyGameHUD* HUD = Cast<AMyGameHUD>(PC->GetHUD())) {
 *               GameHUDWidget = HUD->GetGameHUDWidget();
 *           }
 *       }
 *   }
 */
bool ABaseCharacter::TryResolveHUDWidget(bool bLogWarning)
{
	// 已解析则直接返回
	if (GameHUDWidget)
	{
		return true;
	}

	// 仅本地玩家需要 HUD
	if (!IsLocallyControlled())
	{
		return false;
	}

	APlayerController* PC = Cast<APlayerController>(GetController());
	if (!PC)
	{
		if (bLogWarning)
		{
			UE_LOG(LogTemp, Verbose, TEXT("[UI Bridge] TryResolveHUDWidget: 无 PlayerController (Role=%d)"), (int32)GetLocalRole());
		}
		return false;
	}

	AMyGameHUD* HUD = Cast<AMyGameHUD>(PC->GetHUD());
	if (!HUD)
	{
		if (bLogWarning)
		{
			UE_LOG(LogTemp, Warning, TEXT("[UI Bridge] TryResolveHUDWidget: HUD 未就绪 (可能 PlayerController 初始化中)"));
		}
		return false;
	}

	GameHUDWidget = HUD->GetGameHUDWidget();
	if (!GameHUDWidget && bLogWarning)
	{
		UE_LOG(LogTemp, Warning, TEXT("[UI Bridge] TryResolveHUDWidget: HUD 存在但 GameHUDWidget 为空"));
	}
	return GameHUDWidget != nullptr;
}


/**
 * 【UI Bridge】一次性推送所有角色状态到 HUD
 *
 * 替代散落在 10+ 处的分散更新:
 *   GameHUDWidget->UpdateHealth(Current, Max);
 *   GameHUDWidget->UpdateHealthText(...);
 *   GameHUDWidget->UpdateEnergy(CurrentEnergy, MaxEnergy);
 *   GameHUDWidget->UpdateEnergyText(...);
 *   GameHUDWidget->UpdateACValue(ACValue);
 *   GameHUDWidget->UpdateACEValue(ACEValue);
 */
void ABaseCharacter::RefreshHUDFromCurrentState()
{
	if (!TryResolveHUDWidget(false))
	{
		return;
	}

	// 血量 - 【v39 修复】用 ResolveHealthComponent 而非裸字段访问
	if (UHealthComponent* HC = ResolveHealthComponent())
	{
		const float Current = HC->GetCurrent();
		const float Max = HC->GetMax();
		GameHUDWidget->UpdateHealth(Current, Max);
		GameHUDWidget->UpdateHealthText(FMath::CeilToInt(Current), FMath::CeilToInt(Max));
	}

	// 能量 - 【2026-06-15 重构】【v39 修复】用 ResolveEnergyComponent 而非裸字段访问
	if (UEnergyComponent* EC = ResolveEnergyComponent())
	{
		const float ECurrent = EC->GetCurrent();
		const float EMax = EC->GetMax();
		GameHUDWidget->UpdateEnergy(ECurrent, EMax);
		GameHUDWidget->UpdateEnergyText(FMath::CeilToInt(ECurrent), FMath::CeilToInt(EMax));
	}

	// AC / ACE
	GameHUDWidget->UpdateACValue(ACValue);
	GameHUDWidget->UpdateACEValue(ACEValue);
}


// ==========================================
// 4. 事件广播方法 (2026-07-01 新增)
// 替代直接 GameHUDWidget Push, 实现依赖倒置
// ==========================================

/**
 * BroadcastCharacterIconReady
 *
 * 广播角色头像加载完毕事件
 * 调用链: Client_RefreshCharacterIcon_Implementation → BroadcastCharacterIconReady
 */
void ABaseCharacter::BroadcastCharacterIconReady(const FString& CharID, UTexture2D* Icon)
{
	// 【v39 修复】用 ResolveCharacterEvents 而非裸字段访问
	UCharacterEvents* Events = ResolveCharacterEvents();
	if (!Events)
	{
		return;
	}

	// 【2026-07-01 P0 修复】"事件 + 缓存"双轨制
	// 先写缓存, 再 Broadcast, 保证订阅者订阅时能从缓存拿到最新值
	Events->SetCachedCharacterIcon(CharID, Icon);
	Events->OnCharacterIconReady.Broadcast(CharID, Icon);
}


/**
 * BroadcastACValueChanged
 *
 * 广播 AC 防护服值变化事件
 * 调用链: OnRep_ACValue → BroadcastACValueChanged
 */
void ABaseCharacter::BroadcastACValueChanged(int32 NewAC)
{
	// 【v39 修复】用 ResolveCharacterEvents 而非裸字段访问
	UCharacterEvents* Events = ResolveCharacterEvents();
	if (!Events)
	{
		return;
	}

	// 【2026-07-01 P0 修复】事件 + 缓存双轨
	Events->SetCachedACValue(NewAC);
	Events->OnACValueChanged.Broadcast(NewAC);
}


/**
 * BroadcastACEWithRankChanged
 *
 * 广播 ACE 击杀数 + 排名颜色变化事件
 * 调用链: RefreshACEWithRank → BroadcastACEWithRankChanged
 */
void ABaseCharacter::BroadcastACEWithRankChanged(int32 NewACE, EACERankType RankType)
{
	// 【v39 修复】用 ResolveCharacterEvents 而非裸字段访问
	UCharacterEvents* Events = ResolveCharacterEvents();
	if (!Events)
	{
		return;
	}

	// 【2026-07-01 P0 修复】事件 + 缓存双轨
	Events->SetCachedACEState(NewACE, RankType);
	Events->OnACEWithRankChanged.Broadcast(NewACE, RankType);
}


/**
 * BroadcastWeaponIconReady
 *
 * 广播武器图标加载完毕事件
 * 调用链: RefreshWeaponIconOnHUD → BroadcastWeaponIconReady
 */
void ABaseCharacter::BroadcastWeaponIconReady(const FString& WeaponID, UTexture2D* Icon)
{
	// 【v39 修复】用 ResolveCharacterEvents 而非裸字段访问
	UCharacterEvents* Events = ResolveCharacterEvents();
	if (!Events)
	{
		return;
	}
	Events->OnWeaponIconReady.Broadcast(WeaponID, Icon);
}


// ==========================================
// 【2026.07.10 大厂阵营重构 v1】 IGenericTeamAgentInterface 实现 (走 FFactionTags)
//
// 单一真理源: FactionTag (FGameplayTag) — Offense/Defense 双阵营
// 阵营协议层 (FGenericTeamId) 仅是 UE AIPerception 接口要求的"历史包袱",
// 实现细节走内部映射, 不暴露给业务代码
// 业务代码 (RoomGameMode/BaseAIController/UI) 统一用 FFactionTags::IsSameSide 等静态 API
// ==========================================

// 【2026.07.11 P0 修复】OnRep_FactionTag — 客户端响应 Server 阵营变更
//
// 触发场景:
//   - 服务器 SetGenericTeamId() / FactionTag = ... → 复制到客户端 → 客户端 OnRep 触发
//   - 服务器 PossessedBy 从 PlayerState 同步 → 复制 → 客户端 OnRep
//
// 责任:
//   1. 验证 Tag 是否有效, 无效 → Error 日志 (上层默认空是大厂禁止行为)
//   2. UE AIPerception 的阵营表 (TArray<FPerceptionTeamID>) 内置缓存
//      客户端阵营值变化时必须主动调 SetGenericTeamId 让引擎感知, 否则 SightSense 仍用旧值
//   3. 广播 OnFactionTagChanged (后续 BT/UI 可订阅)
//
// 大厂原则: 真理源在 Pawn.FactionTag, Client 必须精确反映 Server 的当前值
void ABaseCharacter::OnRep_FactionTag()
{
	const FName TagName = FactionTag.GetTagName();

	if (!FFactionTags::IsValidFaction(FactionTag))
	{
		// 客户端拿到无效 Tag 也是错误 (Server 配错的客户端表现)
		// 注意区分: Server 故意设了 Deprecated 旧 Tag (违规但合法), vs Server 没设 (None)
		// 两者都让 bDetectNeutrals=false 的玩家永远不被 AI 看到 — 必须报错
		if (FactionTag.IsValid())
		{
			UE_LOG(LogTemp, Error,
				TEXT("[%s] OnRep_FactionTag: 收到非有效阵营 '%s' — Server 配错了, AI 永远不会识别此 Pawn 的敌我."
				     " 修复: Server 端 Pawn.FactionTag 必须为 Faction.Offense 或 Faction.Defense"),
				*GetName(), *TagName.ToString());
		}
		else
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[%s] OnRep_FactionTag: Server 端未配 Pawn.FactionTag (空). AI 将此 Pawn 视为中立 (Neutral). "
				     " 修复: Server 端 PossessedBy 时同步 PlayerState.CurrentFactionTag → Pawn.FactionTag"),
				*GetName());
		}
	}

	// 【可选扩展点】若后续 BT/UI 需要响应阵营变化, 这里 broadcast 委托
	// OnFactionTagChanged.Broadcast();
}

void ABaseCharacter::SetGenericTeamId(const FGenericTeamId& NewTeamID)
{
	// 【2026.07.10 v25】FGenericTeamId → FGameplayTag 统一走 FFactionTags::FromGenericTeamId
	// (单一真理源: 所有转换都在 FFactionTags 内, ABaseCharacter 不再各自实现)
	// 大厂原则: 非法 ID 在 FromGenericTeamId 内部已 Log Error, 此处不再重复
	FactionTag = FFactionTags::FromGenericTeamId(NewTeamID);
}

FGenericTeamId ABaseCharacter::GetGenericTeamId() const
{
	// 【2026.07.10 v25】FGameplayTag → FGenericTeamId 走 FFactionTags::ToGenericTeamId
	// 大厂原则: FactionTag 为空 → NoTeam 是 UE 协议层的合法状态, 不是兜底
	if (FactionTag.IsValid())
	{
		return FFactionTags::ToGenericTeamId(FactionTag);
	}
	return FGenericTeamId::NoTeam;
}

ETeamAttitude::Type ABaseCharacter::GetTeamAttitudeTowards(const AActor& Other) const
{
	// 【2026.07.10 P0】走 FFactionTags::AttitudeBetween — 单一真理源
	// 旧版"按 ID 奇偶判定阵营"启发式完全删除, 现在直接比 FGameplayTag
	if (const IGenericTeamAgentInterface* OtherTeamAgent =
		Cast<IGenericTeamAgentInterface>(&Other))
	{
		// 【2026.07.10 v25】IGenericTeamAgentInterface 不提供 GetFactionTag()
		//   阵营 Tag 存在 Character 自己的字段里 (FactionTag), 需要从 Other 拿 Actor 再 Cast 到 ABaseCharacter
		//   旧版 OtherTeamAgent->GetFactionTag() 编译失败, 因为 UE 接口根本没这方法
		const ABaseCharacter* OtherChar = Cast<ABaseCharacter>(&Other);
		const FGameplayTag OtherTag = OtherChar ? OtherChar->GetFactionTag() : FGameplayTag::EmptyTag;
		return FFactionTags::AttitudeBetween(FactionTag, OtherTag);
	}
	return ETeamAttitude::Neutral;
}

// ==========================================
// 【v132 2026.08.02 新增】IAISightTargetInterface::CanBeSeenFrom — UE 官方 AI 视野协议
// ==========================================
//
// 业务背景 (用户 2026.08.02 第五轮反馈):
//   "还是老样子, 为什么不用官方推荐的解决方案呢?"
//
// 之前 (v129 - v131.5) 我们自建 BTDecorator_HasLineOfSight, 自创多 trace 方案
//   - 业界共识: UE 自带 LineOfSightTo / UAISense_Sight 只 trace capsule center (已知限制)
//   - 业界共识: 自定义视线检测必须 override IAISightTargetInterface::CanBeSeenFrom
//   - 我之前嫌它要 override Target Actor "麻烦", 偷懒走了自创方案 — 用户指出错误
//   - v132 修正: 用 UE 官方推荐方案
//
// 业界共识方案 (UE 5.6 + Lyra + Epic Developer Community):
//   - 实现 IAISightTargetInterface, override CanBeSeenFrom (UE 5.2 新签名, 用 FCanBeSeenFromContext)
//   - 在 CanBeSeenFrom 内做自定义视线检测 (业界共识: trace Pawn 多个关键点)
//   - AI Perception 用 UAISenseConfig_Sight 配置 (项目中已存在 BP_MeleeAIController 等)
//   - BT 不再需要 LoS 装饰器 (AI 视野系统自动调用, 写入 BB.HasLineOfSight 等)
//
// v132 实现 — trace Pawn 关键点 (业界共识 — UE 5.3 forum 推荐):
//   - Phase 1: trace ObserverLocation → Pawn "head" socket (Mesh 上)
//               AI 仰视主视角, 掩体挡下半身时头仍可见
//   - Phase 2: trace ObserverLocation → Pawn Capsule 中心
//               标准视线检测点
//   - Phase 3: trace ObserverLocation → Pawn Capsule 底部
//               AI 俯视, 掩体挡上半身时脚仍可见
//   任一命中 Pawn 或其子组件 → Visible
//
// 用户场景验证 (AI 高地 Z=1463, Target 低地 Z=1016, 中间地面凸起):
//   - 旧 BTDecorator (v131.5): Phase 1 trace 头 (高) → 撞墙 → 仍 PASS → AI 误开火
//   - 新 IAISightTargetInterface (v132): 头撞墙 (Phase 1 FAIL) → 胸可能也撞墙 (Phase 2 FAIL) → 脚可能也撞墙 (Phase 3 FAIL) → NotVisible → AI 不开火 ✓
//
// 大厂原则 — UE 官方推荐 > 自创方案:
//   - 删 BTDecorator_HasLineOfSight 整个文件 (v129 - v131.5)
//   - 用 IAISightTargetInterface (UE 官方接口)
//   - BT 只负责距离/HP/状态决策, 不负责 LoS (LoS 归 AI Perception + IAISightTargetInterface, 单一真理源)
//   - 与 IGenericTeamAgentInterface 对称 — 都是 UE 官方协议接口, ABaseCharacter 是唯一实现者
UAISense_Sight::EVisibilityResult ABaseCharacter::CanBeSeenFrom(
	const FCanBeSeenFromContext& Context,
	FVector& OutSeenLocation,
	int32& OutNumberOfLoSChecksPerformed,
	int32& OutNumberOfAsyncLosCheckRequested,
	float& OutSightStrength,
	int32* UserData,
	const FOnPendingVisibilityQueryProcessedDelegate* Delegate)
{
	OutNumberOfLoSChecksPerformed = 0;
	OutNumberOfAsyncLosCheckRequested = 0;
	OutSightStrength = 0.f;
	OutSeenLocation = GetActorLocation();

	// 第 1 步: 基本校验 (大厂原则 — 零兜底)
	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[BaseCharacter::CanBeSeenFrom] '%s' World 为空, 视为 NotVisible."),
			*GetName());
		return UAISense_Sight::EVisibilityResult::NotVisible;
	}

	const FVector ObserverLocation = Context.ObserverLocation;

	// 第 2 步: 退化防御 — 距离太近 < 30cm (避免 trace 自身)
	const float DistToObserver = FVector::Dist(ObserverLocation, GetActorLocation());
	if (DistToObserver < 30.f)
	{
		OutNumberOfLoSChecksPerformed = 1;
		OutSightStrength = 1.f;
		OutSeenLocation = GetActorLocation();
		return UAISense_Sight::EVisibilityResult::Visible;
	}

	// 第 3 步: 配置 trace 参数
	//   - 不 ignore Target 自己 (UE 官方示例也是这样: Context.IgnoreActor 是 Observer)
	//   - 业界共识: 让 trace 真检测碰撞, 命中 Pawn 子组件也算 Visible
	const ECollisionChannel SightChannel = ECC_Visibility;
	FCollisionQueryParams Params(SCENE_QUERY_STAT(BaseChar_CanBeSeenFrom), /*bTraceComplex=*/true);
	// 注意: 不要 AddIgnoredActor(this) — UE 官方示例也没 ignore Target,
	//       因为 trace 命中 Target 或其子组件是 Visible 的有效路径
	Params.AddIgnoredActor(Context.IgnoreActor);

	// 第 4 步: Pawn 关键点 (业界共识 — 头/胸/脚三选一命中即可)
	//   - 头: 用 Mesh 上的 "head" socket (业界共识: UE 5.3 forum 推荐)
	//   - 胸: Pawn 中心 (Capsule Component 中心)
	//   - 脚: Pawn 底部 (Capsule Component 底部)
	TArray<FVector> KeyPoints;

	// 头 (Mesh "head" socket) — 优先用 socket, 没有就用 Capsule 顶部
	FVector HeadLoc = FVector::ZeroVector;
	bool bHasHeadSocket = false;
	if (USkeletalMeshComponent* MeshComp = GetMesh())
	{
		if (MeshComp->DoesSocketExist(TEXT("head")))
		{
			HeadLoc = MeshComp->GetSocketLocation(TEXT("head"));
			bHasHeadSocket = true;
		}
	}
	if (bHasHeadSocket)
	{
		KeyPoints.Add(HeadLoc);
	}
	else
	{
		// 退化: 用 Capsule 顶部 (比头低一点, 但够用)
		if (UCapsuleComponent* Capsule = GetCapsuleComponent())
		{
			KeyPoints.Add(GetActorLocation() + FVector(0.f, 0.f, Capsule->GetScaledCapsuleHalfHeight() * 0.7f));
		}
	}

	// 胸 (Capsule 中心)
	KeyPoints.Add(GetActorLocation());

	// 脚 (Capsule 底部)
	if (UCapsuleComponent* Capsule = GetCapsuleComponent())
	{
		KeyPoints.Add(GetActorLocation() + FVector(0.f, 0.f, -Capsule->GetScaledCapsuleHalfHeight() * 0.5f));
	}

	// 第 5 步: 关键点 trace — 任一命中 Target → Visible
	int32 KeyIndex = 0;
	for (const FVector& EndPt : KeyPoints)
	{
		FHitResult HitResult;
		const bool bHit = World->LineTraceSingleByChannel(
			HitResult, ObserverLocation, EndPt, SightChannel, Params);
		++OutNumberOfLoSChecksPerformed;
		++KeyIndex;

		// 3 case 显式 if-else (大厂原则 — 不简化):
		bool bVisible = false;
		if (!bHit)
		{
			// 情况 1: 路径干净 (无阻挡) → Visible
			bVisible = true;
		}
		else if (HitResult.GetActor() == this ||
		         (HitResult.GetActor() && HitResult.GetActor()->IsOwnedBy(this)))
		{
			// 情况 2: 路径到达 this 或 this 的子组件 (Mesh 等) → Visible
			bVisible = true;
		}
		else
		{
			// 情况 3: 路径撞墙 (非 Target) → NotVisible
			bVisible = false;
		}

		if (bVisible)
		{
			OutSightStrength = 1.f;
			OutSeenLocation = bHit ? HitResult.ImpactPoint : EndPt;
			UE_LOG(LogBehaviorTree, Display,
				TEXT("[CanBeSeenFrom] '%s' Visible via KeyPt #%d (EndPt=%s Hit=%s). Observer=%s"),
				*GetName(), KeyIndex, *EndPt.ToCompactString(),
				bHit ? *HitResult.GetActor()->GetName() : TEXT("None"),
				*ObserverLocation.ToCompactString());
			return UAISense_Sight::EVisibilityResult::Visible;
		}
	}

	// 第 6 步: 全部关键点都撞墙 → NotVisible (物理短路)
	UE_LOG(LogBehaviorTree, Display,
		TEXT("[CanBeSeenFrom] '%s' NotVisible (3 KeyPts 全部撞墙). Observer=%s SelfLoc=%s"),
		*GetName(), *ObserverLocation.ToCompactString(), *GetActorLocation().ToCompactString());
	return UAISense_Sight::EVisibilityResult::NotVisible;
}


// ==========================================
// 【2026.07.11 P0 修复】SyncFactionTagFromController
//
// 责任:
//   - Server-only, 在 PossessedBy 头部调用 (复活 / 关卡预放 / 玩家加入 均触发)
//   - AI Controller: 跳过 — AI 阵营真理源在 AIController.CachedFactionTag, 由 SetupMeleeAI 写入
//     (若用户配置了 Pawn.FactionTag 默认值, 则尊重该值 — 这是 AI 的关卡预放约定)
//   - Player Controller: 从 PlayerState.CurrentFactionTag 同步到 Pawn.FactionTag
//     这才是"大厂原则 - 单一真理源":
//       PlayerState.CurrentFactionTag (服务端权威, 已 Replicated) → Pawn.FactionTag (Pawn 本地镜像)
//     Pawn.FactionTag 也 Replicated, 客户端 OnRep_FactionTag 同步收到 → AI 在 Client 端也能正确判定
//
// 大厂原则 - 零兜底:
//   - Player Controller 没找到 PlayerState → 显式 Error, 不允许 Pawn 保持空 FactionTag
//   - PlayerState.CurrentFactionTag 为空 → 显式 Error, 提示上层 GameMode (PostLogin) 漏初始化阵营
//   - AI Controller 跳过同步 → 假设 SetupMeleeAI/SetupZombieAI 已写过 (关卡预放路径已 fix v26.5)
//
// 调用方:
//   - ABaseCharacter::PossessedBy (Server)
//
// 验证:
//   守护原则: 调用前 Pawn.FactionTag 必须是 Server 的真理 (即使没改也要走)
//   防止 player 路径在 RoundStart 后切换阵营时不同步
// ==========================================
void ABaseCharacter::SyncFactionTagFromController(AController* InController)
{
	// Guard 0: 防御 — 重复调用保护
	if (!InController)
	{
		return;
	}

	// ============ AI Controller 路径 — 跳过 ============
	// AI 阵营真理源在 AIController.CachedFactionTag (v26.5 运行时真理)
	// ABaseAIController::OnPossess + SetupMeleeAI/SetupZombieAI 都已经写过 Pawn.FactionTag (C++ 端)
	// 关卡预放的 BP_GruntAI 由用户在 BP defaults 手动设的 Pawn.FactionTag 也保持原值
	//
	// 大厂原则: 不重复写, 避免"双源冲突"
	//   若 AIC 路径有 bug, 反映在 AI 看玩家失败 — 但这是另一个问题, 不在 Player 路径修复
	if (const ABaseAIController* AIC = Cast<ABaseAIController>(InController))
	{
		return;
	}

	// ============ Player Controller 路径 — 真理源同步 ============
	ARoomPlayerState* PS = InController->GetPlayerState<ARoomPlayerState>();
	if (!PS)
	{
		// 大厂原则 - 零兜底: Player Controller 应该有 PlayerState, 没有就是配置错误
		UE_LOG(LogTemp, Error,
			TEXT("[%s] SyncFactionTagFromController: Player Controller '%s' 没有 ARoomPlayerState! "
			     "这会让 AI 永远看不到此玩家 (Pawn.FactionTag 保持空 → AI 视玩家为 Neutral). "
			     "修复: 检查 ARoomGameMode::PostLogin 是否正确分配了 PlayerState"),
			*GetName(), *InController->GetName());
		return;
	}

	const FGameplayTag PlayerFactionTag = PS->CurrentFactionTag;
	if (!PlayerFactionTag.IsValid())
	{
		// 大厂原则 - 零兜底: GameMode 启动时必须给玩家分阵营
		UE_LOG(LogTemp, Error,
			TEXT("[%s] SyncFactionTagFromController: PlayerState->CurrentFactionTag 为空! "
			     "AI 视此玩家为中立 (Neutral), 永远不会追击. "
			     "修复: 检查 ARoomGameMode::AssignPlayerFaction 或 PostLogin 是否有阵营初始化逻辑"),
			*GetName());
		return;
	}

	// 验证 Tag 合法性
	if (!FFactionTags::IsValidFaction(PlayerFactionTag))
	{
		UE_LOG(LogTemp, Error,
			TEXT("[%s] SyncFactionTagFromController: PlayerState->CurrentFactionTag='%s' 非 Faction.Offense/Defense. "
			     "AI 永远看不到此玩家 (Pawn.FactionTag 同步失败)."),
			*GetName(), *PlayerFactionTag.ToString());
		return;
	}

	// 写入真理源 — DOREPLIFETIME(FactionTag) 让客户端自动 OnRep
	FactionTag = PlayerFactionTag;

	UE_LOG(LogTemp, Log,
		TEXT("[%s] SyncFactionTagFromController: PlayerState.CurrentFactionTag='%s' → Pawn.FactionTag (Replicated)"),
		*GetName(), *FactionTag.ToString());
}


// ==========================================
// 【P0】WithValidation 验证实现 - 防止客户端伪造攻击参数
// ==========================================

/**
 * Server_PlayAttackAnim_Validate
 * 校验: 连击序号在合法范围 (0~10 涵盖连击表), bIsHeavy 是 bool
 */
bool ABaseCharacter::Server_PlayAttackAnim_Validate(bool bIsHeavy, int32 InComboIndex)
{
	return InComboIndex >= 0 && InComboIndex <= 10;
}


// ==========================================
// 【v51 大厂架构 — 生化模式】武器槽位切换 (Q 键)
// ==========================================

/**
 * OnSwitchWeaponPressed — 客户端按 Q 键处理
 *
 * 大厂原则 (服务器权威):
 *   - 客户端不能直接切槽位 (防作弊, 防客户端伪造)
 *   - 客户端只发送 Server RPC + 服务器决定 TargetSlot
 *   - 服务器基于当前 CurrentWeaponSlot 自动计算目标槽位 (Primary↔Melee 循环)
 *
 * 调用方: SetupPlayerInputComponent 中 SwitchWeaponAction Enhanced Input Triggered
 */
void ABaseCharacter::OnSwitchWeaponPressed()
{
	// 死亡状态不能切武器
	if (IsDead())
	{
		return;
	}

	// 【v51 编译修复 — C4458】局部变量不能与类成员同名, 改名避免隐藏 ABaseCharacter::WeaponAttach
	UWeaponAttachmentComponent* WeaponAttachComp = ResolveWeaponAttach();
	if (!WeaponAttachComp)
	{
		// ResolveWeaponAttach 内部已 Log Error, 强制修复 BP 组件挂载
		return;
	}

	// 仅玩家 (非 AI) 才允许客户端切槽位
	// AI 路径: AI 不接收 PlayerController 输入, 不会被本回调触发
	// 这里显式守卫, 防止某人手动调
	if (!IsPlayerControlled())
	{
		return;
	}

	// 服务器权威: 客户端发 RPC, 服务器内读取当前槽位 + 算目标槽位
	// 注: 这里不发目标槽位到 RPC, 让服务器自主决定 (避免客户端篡改)
	Server_SwitchWeaponSlot(EWeaponSlotType::Primary); // 占位, 服务器内忽略这个值, 自己算
}


/**
 * Server_SwitchWeaponSlot_Implementation — 服务器执行切换
 *
 * 大厂原则 (服务器权威 + 单一真理源):
 *   - 不信任客户端传来的 TargetSlot 参数, 服务器自己读当前状态算目标槽位
 *   - 服务器检查双武器是否 Spawn (WeaponsInSlot.Num() >= 2)
 *   - 服务器调 WeaponAttachmentComponent::Server_SwitchToWeaponSlot 真正执行切换
 *
 * 循环逻辑:
 *   Primary → Melee
 *   Melee   → Primary
 *   其它 (None / Secondary) → Primary (默认 fallback)
 */
void ABaseCharacter::Server_SwitchWeaponSlot_Implementation(EWeaponSlotType /*TargetSlot*/)
{
	// 【v51 编译修复 — C4458】局部变量不能与类成员同名
	UWeaponAttachmentComponent* WeaponAttachComp = ResolveWeaponAttach();
	if (!WeaponAttachComp)
	{
		return; // ResolveWeaponAttach 内部已 Log Error
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[BaseCharacter] Server_SwitchWeaponSlot: World 无效 — 拒绝切换. Pawn=%s"),
			*GetName());
		return;
	}

	// 零兜底: 必须在服务器上执行
	if (!HasAuthority())
	{
		UE_LOG(LogTemp, Error,
			TEXT("[BaseCharacter] Server_SwitchWeaponSlot: 非服务器执行 — RPC 链路有误. Pawn=%s"),
			*GetName());
		return;
	}

	// 服务器权威计算目标槽位 (不信任客户端)
	// 【v52 P0】3 槽位循环: Primary → Secondary → Melee → Primary
	//   - 跳过空槽位 (玩家没选副武器 / 近战武器时)
	//   - 循环找第一个非空槽位作为目标
	const EWeaponSlotType CurrentSlot = WeaponAttachComp->GetCurrentWeaponSlot();
	const TArray<ABaseWeapon*>& Weapons = WeaponAttachComp->GetWeaponsInSlotArray();
	EWeaponSlotType NextSlot = EWeaponSlotType::Primary;

	// 大厂原则 (零兜底): 循环查下一个非空槽位
	//   从 CurrentSlot + 1 开始, 顺时针循环 (Primary→Secondary→Melee→Primary)
	//   遇到非空槽位就停下, 找不到 → 留在原槽位 (不切换)
	const TArray<EWeaponSlotType> CycleOrder = {
		EWeaponSlotType::Primary,
		EWeaponSlotType::Secondary,
		EWeaponSlotType::Melee
	};

	// 找当前槽位在 CycleOrder 的索引
	int32 CurrentIdx = CycleOrder.IndexOfByKey(CurrentSlot);
	if (CurrentIdx == INDEX_NONE)
	{
		CurrentIdx = 0; // 容错: 未知槽位 → 从 Primary 开始
	}

	// 最多循环 3 次找下一个非空槽位
	for (int32 Step = 1; Step <= 3; ++Step)
	{
		const int32 ProbeIdx = (CurrentIdx + Step) % 3;
		const EWeaponSlotType Probe = CycleOrder[ProbeIdx];
		if (ProbeIdx < Weapons.Num() && Weapons[ProbeIdx] != nullptr)
		{
			NextSlot = Probe;
			break;
		}
	}
	// 找不到非空槽位 → NextSlot 仍为 Primary (向后兼容), 但 Server_SwitchToWeaponSlot 会拒绝切到空槽位

	UE_LOG(LogTemp, Log,
		TEXT("[BaseCharacter] Server_SwitchWeaponSlot: Q键触发 — Pawn=%s 当前槽位=%s → 目标槽位=%s"),
		*GetName(),
		LexToString(CurrentSlot),
		LexToString(NextSlot));

	// 调真实执行器 (大厂原则: 上层调度 + 下层执行分离)
	WeaponAttachComp->Server_SwitchToWeaponSlot(NextSlot);
}


/**
 * Server_SwitchWeaponSlot_Validate
 * 校验: EWeaponSlotType 必须是合法枚举值 (UE enum class 编译期已保证)
 */
bool ABaseCharacter::Server_SwitchWeaponSlot_Validate(EWeaponSlotType /*TargetSlot*/)
{
	return true; // enum class 编译期已保证
}


// ==========================================
// v58 FPS枪战 — 1/2/3 键切换武器槽位
// ==========================================

/**
 * OnSwitchToPrimaryPressed — 切换到主武器（按键1）
 */
void ABaseCharacter::OnSwitchToPrimaryPressed()
{
	// 死亡状态不能切武器
	if (IsDead())
	{
		return;
	}

	UWeaponAttachmentComponent* WeaponAttachComp = ResolveWeaponAttach();
	if (!WeaponAttachComp)
	{
		return;
	}

	if (!IsPlayerControlled())
	{
		return;
	}

	Server_RequestSwitchToSlot(EWeaponSlotType::Primary);
}

/**
 * OnSwitchToSecondaryPressed — 切换到副武器（按键2）
 */
void ABaseCharacter::OnSwitchToSecondaryPressed()
{
	if (IsDead())
	{
		return;
	}

	UWeaponAttachmentComponent* WeaponAttachComp = ResolveWeaponAttach();
	if (!WeaponAttachComp)
	{
		return;
	}

	if (!IsPlayerControlled())
	{
		return;
	}

	Server_RequestSwitchToSlot(EWeaponSlotType::Secondary);
}

/**
 * OnSwitchToMeleePressed — 切换到近战武器（按键3）
 */
void ABaseCharacter::OnSwitchToMeleePressed()
{
	if (IsDead())
	{
		return;
	}

	UWeaponAttachmentComponent* WeaponAttachComp = ResolveWeaponAttach();
	if (!WeaponAttachComp)
	{
		return;
	}

	if (!IsPlayerControlled())
	{
		return;
	}

	Server_RequestSwitchToSlot(EWeaponSlotType::Melee);
}

/**
 * Server_RequestSwitchToSlot_Implementation — 服务器执行显式槽位切换
 */
void ABaseCharacter::Server_RequestSwitchToSlot_Implementation(EWeaponSlotType TargetSlot)
{
	UWeaponAttachmentComponent* WeaponAttachComp = ResolveWeaponAttach();
	if (!WeaponAttachComp)
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[BaseCharacter] Server_RequestSwitchToSlot: World 无效 — 拒绝切换. Pawn=%s"),
			*GetName());
		return;
	}

	// 服务器权威校验
	if (!HasAuthority())
	{
		UE_LOG(LogTemp, Error,
			TEXT("[BaseCharacter] Server_RequestSwitchToSlot: 非服务器执行 — RPC 链路有误. Pawn=%s"),
			*GetName());
		return;
	}

	// 零兜底: 目标槽位不能是 None
	if (TargetSlot == EWeaponSlotType::None)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[BaseCharacter] Server_RequestSwitchToSlot: 目标槽位=None — 拒绝切换. Pawn=%s"),
			*GetName());
		return;
	}

	// 零兜底: 目标槽位必须有武器
	if (!WeaponAttachComp->HasWeaponInSlot(TargetSlot))
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[BaseCharacter] Server_RequestSwitchToSlot: 目标槽位 %s 没有武器 — 拒绝切换. Pawn=%s"),
			LexToString(TargetSlot), *GetName());
		return;
	}

	UE_LOG(LogTemp, Log,
		TEXT("[BaseCharacter] Server_RequestSwitchToSlot: 按键触发 — Pawn=%s 目标槽位=%s"),
		*GetName(),
		LexToString(TargetSlot));

	// 调真实执行器
	WeaponAttachComp->Server_SwitchToWeaponSlot(TargetSlot);
}

/**
 * Server_RequestSwitchToSlot_Validate — 校验显式槽位切换参数
 */
bool ABaseCharacter::Server_RequestSwitchToSlot_Validate(EWeaponSlotType TargetSlot)
{
	return TargetSlot != EWeaponSlotType::None;
}


// ==========================================
// v60 枪械射击/换弹输入回调 — 转发壳 (大厂原则)
// ==========================================
//
// 【设计原则 — 转发壳模式】
//   - BaseCharacter 留作 Input → RPC 的薄壳
//   - 真实业务在 UWeaponFireComponent (武器自治, 与 v24 Dissolve 对称)
//   - 服务器权威: 这里只发 Server RPC, 拒绝本地直调
//
// 【MeshType 校验】
//   - 当前武器不是枪 (Melee) → 拒绝转发, 让玩家按左键走 LightAttack_Pressed
//   - 当前没武器 → 拒绝 (业务正常)
//   - 死亡状态 → 拒绝 (业务正常)

void ABaseCharacter::OnFirePressed(const FInputActionValue& /*Value*/)
{
	// 【v82 诊断增强】入口 Display Log — 用户能确认按左键是否触发此函数
	//   旧版 (v70-v81) 只有错误情况 Log, 按下时静默, 难诊断"按了左键没反应"
	UE_LOG(LogTemp, Display,
		TEXT("[BaseCharacter::OnFirePressed] ENTER. Pawn=%s Local=%d Auth=%d"),
		*GetName(),
		IsLocallyControlled() ? 1 : 0,
		HasAuthority() ? 1 : 0);

	// 死亡不能开火
	if (IsDead())
	{
		UE_LOG(LogTemp, Display,
			TEXT("[BaseCharacter::OnFirePressed] Pawn=%s 已死亡, 拒绝开火."),
			*GetName());
		return;
	}

	// 仅玩家 (AI 不走 PlayerController 输入, 这里防御性守卫)
	if (!IsPlayerControlled())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[BaseCharacter::OnFirePressed] Pawn=%s 非玩家控制, 拒绝开火 (AI 应走 OnAIRequestAttack_Simple)."),
			*GetName());
		return;
	}

	UWeaponAttachmentComponent* WeaponAttachComp = ResolveWeaponAttach();
	if (!WeaponAttachComp)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[BaseCharacter::OnFirePressed] Pawn=%s 找不到 WeaponAttachmentComponent."),
			*GetName());
		return;
	}

	ABaseWeapon* CurrentWeapon = WeaponAttachComp->GetCurrentWeapon();
	if (!CurrentWeapon)
	{
		// 没武器 — 业务正常, 不报错
		UE_LOG(LogTemp, Display,
			TEXT("[BaseCharacter::OnFirePressed] Pawn=%s 没武器, 拒绝开火."),
			*GetName());
		return;
	}

	UE_LOG(LogTemp, Display,
		TEXT("[BaseCharacter::OnFirePressed] Pawn=%s CurrentWeapon=%s MeshType=%d (0=None 1=Melee 2=Primary 3=Secondary). 决策路径..."),
		*GetName(),
		*CurrentWeapon->GetName(),
		static_cast<int32>(CurrentWeapon->GetMeshType()));

	// 仅枪械 (Primary/Secondary) 才走开火路径
	//   - Melee 武器按左键 → 调 PlayerComboComponent->LightAttack_Pressed (连斩)
	if (CurrentWeapon->GetMeshType() == EWeaponMeshType::Melee)
	{
		UE_LOG(LogTemp, Display,
			TEXT("[BaseCharacter::OnFirePressed] Pawn=%s MeshType=Melee → 走 LightAttack_Pressed (连斩)."),
			*GetName());
		// 走近战连击路径
		if (UPlayerComboComponent* Combo = ResolvePlayerCombo())
		{
			Combo->LightAttack_Pressed();
		}
		// else: ResolvePlayerCombo 内部已 Log Error
		return;
	}

	// Primary / Secondary 走枪械开火路径
	if (CurrentWeapon->GetMeshType() != EWeaponMeshType::Primary &&
	    CurrentWeapon->GetMeshType() != EWeaponMeshType::Secondary)
	{
		// 不是枪也不是刀, 静默拒绝 (业务正常)
		UE_LOG(LogTemp, Warning,
			TEXT("[BaseCharacter::OnFirePressed] Pawn=%s MeshType=%d 既不是枪也不是刀, 拒绝开火. 【v82 修复】检查 DT_WeaponInfo.BQ001.MeshType 是否配 Primary."),
			*GetName(),
			static_cast<int32>(CurrentWeapon->GetMeshType()));
		return;
	}

	UE_LOG(LogTemp, Display,
		TEXT("[BaseCharacter::OnFirePressed] Pawn=%s MeshType=Primary/Secondary → 计算客户端射线 + 调 Server_StartFire() (RPC 转发到服务器)."),
		*GetName());

	// v82 大厂架构修复: 客户端计算 HUD Crosshair 射线 → RPC 传给服务器做权威 trace
	//   - 旧 (v70-v81) 反模式: 不传射线参数, 服务器 WeaponFireComponent 内部用 PC->GetViewportSize
	//     → 服务器对远端玩家 PC 调 GetViewportSize 返回 0,0 → Deproject 失败 → trace 永远失败
	//   - 新 (v82): 客户端玩家路径在 OnFirePressed 用 HUD Crosshair 算出射线 → RPC 传给服务器
	//   - 玩家射线 = 玩家准星: 服务器用客户端射线做权威 trace (所见即所射, 防作弊)
	FVector ClientRayOrigin = FVector::ZeroVector;
	FVector ClientRayDirection = FVector::ForwardVector;

	// 调 BaseCharacter::GetAimRayFromCrosshairOrEyes 拿方向 (路径 A 本地玩家 → HUD Crosshair)
	const bool bGotRay = GetAimRayFromCrosshairOrEyes(ClientRayOrigin, ClientRayDirection);
	if (!bGotRay)
	{
		// HUD 未就绪 / Crosshair 不可用 — 零兜底: 拒绝开火 + Log Error (不允许"没射线也开火")
		//   旧版会静默继续, 服务器再 fail — 双重静默 = 用户根本看不到错误
		UE_LOG(LogTemp, Error,
			TEXT("[BaseCharacter::OnFirePressed] Pawn=%s 获取客户端射线失败 (HUD 未就绪 / Crosshair 不可用). 拒绝开火, 强制修复 HUD 配置. "
			     "【v82 修复】检查 WBP_GameHUD 是否绑定了 WBP_Crosshair 子控件."),
			*GetName());
		return;
	}

	UE_LOG(LogTemp, Display,
		TEXT("[BaseCharacter::OnFirePressed] Pawn=%s 客户端射线: Origin=%s Direction=%s → 调 Server_StartFire (Character RPC — Owning Connection 修复)."),
		*GetName(),
		*ClientRayOrigin.ToCompactString(),
		*ClientRayDirection.ToCompactString());

	// 【v200.3.3 P0 修复】调 Character->Server_StartFire, 不是 Weapon->Server_StartFire
	//   - 旧版: Weapon->Server_StartFire RPC
	//     → Weapon 是"别人的枪" → "No owning connection" → RPC 丢弃
	//   - 新版: Character->Server_StartFire RPC
	//     → Character 有 owning connection (玩家自己的 Pawn) → RPC 成功
	Server_StartFire(FVector_NetQuantize(ClientRayOrigin), FVector_NetQuantizeNormal(ClientRayDirection));
}


void ABaseCharacter::OnFireReleased(const FInputActionValue& /*Value*/)
{
	if (IsDead()) return;
	if (!IsPlayerControlled()) return;

	UWeaponAttachmentComponent* WeaponAttachComp = ResolveWeaponAttach();
	if (!WeaponAttachComp) return;

	ABaseWeapon* CurrentWeapon = WeaponAttachComp->GetCurrentWeapon();
	if (!CurrentWeapon) return;

	// 近战武器: 转发到 PlayerComboComponent->LightAttack_Released
	if (CurrentWeapon->GetMeshType() == EWeaponMeshType::Melee)
	{
		if (UPlayerComboComponent* Combo = ResolvePlayerCombo())
		{
			Combo->LightAttack_Released();
		}
		return;
	}

	// 枪械路径
	if (CurrentWeapon->GetMeshType() != EWeaponMeshType::Primary &&
	    CurrentWeapon->GetMeshType() != EWeaponMeshType::Secondary)
	{
		return;
	}

	// 转发 (全自动模式下, 服务器会停 Tick 节流)
	// 【v200.3.3 P0 修复】调 Character->Server_StopFire, 不是 Weapon->Server_StopFire
	Server_StopFire();
}


// ============================================================
// 【v200.3.3 Server RPC 实现 — 修复捡枪射击问题】
//
// 根因: 旧版调 CurrentWeapon->Server_StartFire
//   - Weapon 是"别人的枪" (捡的别人的枪)
//   - UE NetDriver: "No owning connection for actor" → RPC 被丢弃
//   - 解决: 调 Character->Server_StartFire (Character 有 owning connection)
//
// 大厂原则:
//   - Character 持有 owning connection, Weapon 不持有
//   - 射击逻辑仍在 WeaponFireComponent::StartFire (保持单一职责)
// ============================================================

void ABaseCharacter::Server_StartFire_Implementation(
	const FVector_NetQuantize& ClientRayOrigin,
	const FVector_NetQuantizeNormal& ClientRayDirection)
{
	if (!HasAuthority())
	{
		UE_LOG(LogTemp, Error,
			TEXT("[BaseCharacter::Server_StartFire] 非服务器调用! Pawn=%s."),
			*GetName());
		return;
	}

	UWeaponAttachmentComponent* WeaponAttachComp = ResolveWeaponAttach();
	if (!WeaponAttachComp)
	{
		return;
	}

	ABaseWeapon* CurrentWeapon = WeaponAttachComp->GetCurrentWeapon();
	if (!CurrentWeapon)
	{
		return;
	}

	UE_LOG(LogTemp, Display,
		TEXT("[BaseCharacter::Server_StartFire] Pawn=%s Weapon=%s Origin=%s Dir=%s."),
		*GetName(),
		*CurrentWeapon->GetName(),
		*ClientRayOrigin.ToCompactString(),
		*ClientRayDirection.ToCompactString());

	CurrentWeapon->Server_StartFire(ClientRayOrigin, ClientRayDirection);
}

bool ABaseCharacter::Server_StartFire_Validate(
	const FVector_NetQuantize& /*ClientRayOrigin*/,
	const FVector_NetQuantizeNormal& /*ClientRayDirection*/)
{
	return true;
}

void ABaseCharacter::Server_StopFire_Implementation()
{
	if (!HasAuthority())
	{
		return;
	}

	UWeaponAttachmentComponent* WeaponAttachComp = ResolveWeaponAttach();
	if (!WeaponAttachComp)
	{
		return;
	}

	ABaseWeapon* CurrentWeapon = WeaponAttachComp->GetCurrentWeapon();
	if (!CurrentWeapon)
	{
		return;
	}

	UE_LOG(LogTemp, Display,
		TEXT("[BaseCharacter::Server_StopFire] Pawn=%s Weapon=%s."),
		*GetName(),
		*CurrentWeapon->GetName());

	CurrentWeapon->Server_StopFire();
}

bool ABaseCharacter::Server_StopFire_Validate()
{
	return true;
}

void ABaseCharacter::Server_StartReload_Implementation()
{
	if (!HasAuthority())
	{
		return;
	}

	UWeaponAttachmentComponent* WeaponAttachComp = ResolveWeaponAttach();
	if (!WeaponAttachComp)
	{
		return;
	}

	ABaseWeapon* CurrentWeapon = WeaponAttachComp->GetCurrentWeapon();
	if (!CurrentWeapon)
	{
		return;
	}

	UE_LOG(LogTemp, Display,
		TEXT("[BaseCharacter::Server_StartReload] Pawn=%s Weapon=%s."),
		*GetName(),
		*CurrentWeapon->GetName());

	CurrentWeapon->Server_StartReload();
}

bool ABaseCharacter::Server_StartReload_Validate()
{
	return true;
}


void ABaseCharacter::OnReloadPressed(const FInputActionValue& /*Value*/)
{
	if (IsDead()) return;
	if (!IsPlayerControlled()) return;

	UWeaponAttachmentComponent* WeaponAttachComp = ResolveWeaponAttach();
	if (!WeaponAttachComp) return;

	ABaseWeapon* CurrentWeapon = WeaponAttachComp->GetCurrentWeapon();
	if (!CurrentWeapon) return;

	if (CurrentWeapon->GetMeshType() != EWeaponMeshType::Primary &&
	    CurrentWeapon->GetMeshType() != EWeaponMeshType::Secondary)
	{
		// 刀没弹匣, 拒绝
		return;
	}

	// 转发 (服务器会校验弹药/换弹中, 拒绝 + Log Verbose)
	// 【v200.3.3 P0 修复】调 Character->Server_StartReload, 不是 Weapon->Server_StartReload
	Server_StartReload();
}


// ==========================================
// 【v93.1 大厂架构新增】母体变异 — OnRep + RPC 实现
// ==========================================

/**
 * OnRep_bIsMother — 客户端 bIsMother 字段变化时由引擎自动调用
 *
 * 大厂原则 — UE 5.6 OnRep:
 *   - 必须用 UFUNCTION() 标记 (头文件已声明)
 *   - 客户端独有, 服务器不调用
 *   - bIsMother 由 false→true 时触发变异广播
 *
 * 大厂原则 — 业务分离:
 *   - 本函数只负责: Broadcast OnMotherStatusChanged + 日志
 *   - 母体出生粒子 / 音效已由 Multicast_PlayMutationFX 统一触发 (v99.1 / v99.3),
 *     本函数不再重复触发 (避免双发)
 *   - BP 蓝图订阅做"动画/屏幕震动"等附加效果 (OnMotherStatusChanged 委托)
 */
void ABaseCharacter::OnRep_bIsMother()
{
	UE_LOG(LogTemp, Display,
		TEXT("[BaseCharacter] OnRep_bIsMother: '%s' bIsMother=%d bIsHuman=%d (HasAuthority=%d)"),
		*GetName(), bIsMother ? 1 : 0, bIsHuman ? 1 : 0, HasAuthority() ? 1 : 0);

	// ============================================================
	// 【v93.2 大厂架构 — 互斥校验延迟到下一帧】防 Replicated 时序误报
	// ============================================================
	//
	// 根因 (用户 2026.07.31 日志 line 113):
	//   - 服务器 MutatePawnToMother Step 5.7 写入顺序: bIsHuman=false → bIsMother=true
	//   - 同一帧同一 Bunch 推送到客户端, 但 UE Actor Channel 内属性推送按 DOREPLIFETIME 注册顺序,
	//     与代码写入顺序**无关**! OnRep_bIsMother 可能比 bIsHuman 的 Replicated 值**先**到达客户端
	//   - 旧版 OnRep 立即校验 → bIsHuman 仍是默认值 true → 误报"互斥语义违反"
	//
	// 修复 (零兜底, 不消除真错):
	//   - 推迟到下一帧 (SetTimerForNextTick) 让本帧所有 Replicated 属性都到达后再校验
	//   - 真错仍然会被捕获 (跨帧还存在 = 状态确实没同步)
	//   - 误报消失 (同一帧到达的属性差异自然消除)
	//
	// 不破坏刀战模式:
	//   - 刀战模式 bIsMother 永远 false → OnRep 内部 if 拒判 → 0 性能开销
	// ============================================================
	if (bIsMother)
	{
		if (UWorld* World = GetWorld())
		{
			TWeakObjectPtr<ABaseCharacter> WeakSelf = this;
			World->GetTimerManager().SetTimerForNextTick([WeakSelf]()
			{
				if (ABaseCharacter* Self = WeakSelf.Get())
				{
					if (Self->bIsMother && Self->bIsHuman)
					{
						UE_LOG(LogTemp, Error,
							TEXT("[BaseCharacter] OnRep_bIsMother(延迟校验): '%s' bIsMother=true 但 bIsHuman=true, 互斥语义真违反. "
							     "【根因】服务器 MutatePawnToMother Step 5.7 两个字段都写了, 但客户端 Actor Channel 推送顺序与代码写入顺序无关. "
							     "bIsHuman Replicated 值在 bIsMother OnRep 触发时还没到达 → 跨帧持续存在 = 真错. "
							     "修复: 检查 BaseCharacter::GetLifetimeReplicatedProps 中 bIsMother 和 bIsHuman 的注册顺序."),
							*Self->GetName());
					}
				}
			});
		}
	}

	// 广播委托 (HUD / BP 蓝图订阅播变身特效) — 立即广播, 不延迟 (业务层 UX 及时性优先)
	if (bIsMother)
	{
		OnMotherStatusChanged.Broadcast(GetName());
	}
}


/**
 * Multicast_PlayMutationFX_Implementation
 *
 * 服务器广播 / 客户端接收 — 播放母体变异视觉特效
 *
 * 大厂原则 — RPC 纯数据化 (v31.6):
 *   - 参数 TargetName 是 FString, 不传 Actor* (UE 网络边界序列化要求)
 *   - 业务方 (v99.1 收敛): URoomSpawnSubsystem::MutatePawnToMother Step 7 服务器唯一触发点
 *     首次变异 / 母体复活都走这一处, 不存在第二份调用
 *   - 客户端 Implementation: 触发 UMotherSpawnParticleComponent::PlaySpawnParticle (本地 5 秒粒子)
 *                            + Broadcast OnMotherStatusChanged (BP 蓝图订阅做音效 / 屏幕震动等)
 *   - OnRep_bIsMother 仅做状态广播, 不触发粒子 (避免双发)
 *
 * 大厂原则 — UE 5.6 NetMulticast 行为:
 *   - 服务器自己调用时也执行 Implementation (ListenServer)
 *   - 远端客户端收到时执行 Implementation
 *   - Dedicated Server 上服务器实例不渲染 (合理 — 服务器不渲染)
 *
 * @param TargetName 被变异角色的名称 (服务器本地读 Target->GetName())
 */
void ABaseCharacter::Multicast_PlayMutationFX_Implementation(const FString& TargetName)
{
	UE_LOG(LogTemp, Display,
		TEXT("[BaseCharacter] Multicast_PlayMutationFX: Target='%s', HasAuthority=%d"),
		*TargetName, HasAuthority() ? 1 : 0);

	// 大厂原则 — 关注点分离:
	//   - Multicast_PlayMutationFX = 唯一视觉入口(首次变异 + 母体复活)
	//   - OnRep_bIsMother = 状态复制, 不触发视觉(避免双发)
	//   - 委托 Broadcast 仍保留, BP 蓝图可订阅做"音效/动画/屏幕震动"等附加效果
	//
	// 大厂原则 — 视觉一刀切(粒子 + 音效)由同一 RPC 触发:
	//   - MotherSpawnParticle: 视觉粒子 (5 秒)
	//   - MotherSpawnSound: 听觉音效 (一次性)
	//   - 两者职责分离, 各为独立组件, 共同响应同一 RPC, 避免重复 RPC
	//   - 任一组件缺失(Resolver 找不到)→ Log Error + continue, 不影响另一个组件播放
	if (UMotherSpawnParticleComponent* Particle = ResolveMotherSpawnParticle())
	{
		Particle->PlaySpawnParticle();
	}
	else
	{
		UE_LOG(LogTemp, Error,
			TEXT("[BaseCharacter] Multicast_PlayMutationFX: 找不到 MotherSpawnParticle 组件(Owner=%s). "
				 "【零兜底】不允许静默跳过, 检查 BP 是否挂载 MotherSpawnParticle 子对象."),
			*GetName());
	}

	if (UMotherSpawnSoundComponent* Sound = ResolveMotherSpawnSound())
	{
		Sound->PlaySpawnSound();
	}
	else
	{
		UE_LOG(LogTemp, Error,
			TEXT("[BaseCharacter] Multicast_PlayMutationFX: 找不到 MotherSpawnSound 组件(Owner=%s). "
				 "【零兜底】不允许静默跳过, 检查 BP 是否挂载 MotherSpawnSound 子对象."),
			*GetName());
	}

	OnMotherStatusChanged.Broadcast(TargetName);
}


// ==========================================
// 【v100.1 大厂架构 — 母体待机回血 RPC + 事件桥】
// ==========================================

/**
 * ABaseCharacter::HandleRegenStateChanged
 *
 * 服务器端事件桥 (OnRegenStateChanged 委托 → RPC):
 *   - 触发方: HealthRegenComponent::SetRegeneratingState 内部 OnRegenStateChanged.Broadcast
 *   - 调用方: BaseCharacter::BeginPlay 末尾 AddUniqueDynamic 订阅
 *   - 业务: 把"回血状态变化"转成 NetMulticast RPC 推所有客户端
 *
 * 大厂原则 — 零兜底:
 *   - bIsNowRegenerating=true → 读 HealthRegenComponent->RegenSound
 *     - Sound != nullptr: Client_PlayRegenSound(Sound) (OwnerOnly)
 *     - Sound == nullptr: Log Warning + 不广播 (业务可禁用, 不是错误)
 *   - bIsNowRegenerating=false → Client_StopRegenSound (OwnerOnly)
 *
 * 【v133.11 P0 大厂架构 — 严格 OwnerOnly】
 *   - 玩家母体: IsPlayerControlled()=true → 调 Client RPC → 只发给该玩家
 *   - AI 母体: IsPlayerControlled()=false → 跳过 (业务决策 2026.08.02)
 *   - 大厂原则: 不能让场景中其他人听到, 必须 OwnerOnly
 *
 * 唯一调用方: 自己的 OnRegenStateChanged 委托 (服务器端)
 */
void ABaseCharacter::HandleRegenStateChanged(bool bIsNowRegenerating)
{
	// 服务器权威 — 服务器不响应自己的 OnRegenStateChanged (因为服务器自己 Multicast 也会本地执行)
	// 但服务器需要触发 Multicast 让远端客户端播放
	// 注: UE 5.6 NetMulticast Reliable 在 ListenServer 上服务器自己也会执行 Implementation
	//     但在 Dedicated Server 上服务器不渲染 — 这是标准行为, 不需特别处理
	if (!HasAuthority())
	{
		// 客户端不应收到 HealthRegenComponent 委托 (因为 TickComponent 客户端 return)
		// 防御: 万一有重复广播
		UE_LOG(LogTemp, Warning,
			TEXT("[BaseCharacter][v100.1] HandleRegenStateChanged: 客户端收到事件 (HasAuthority=false). "
				 "忽略 — 服务器权威. Owner=%s bIsNowRegenerating=%d"),
			*GetName(), bIsNowRegenerating ? 1 : 0);
		return;
	}

	if (bIsNowRegenerating)
	{
		UHealthRegenComponent* HRC = ResolveHealthRegenComponent();
		if (!HRC)
		{
			UE_LOG(LogTemp, Error,
				TEXT("[BaseCharacter][v100.1] HandleRegenStateChanged: ResolveHealthRegenComponent 失败. "
					 "【零兜底】理论上不应该 — HealthRegenComponent 是基础组件."));
			return;
		}

		USoundBase* Sound = HRC->RegenSound;
		if (!Sound)
		{
			// 业务可禁用 (RegenSound=nullptr), 不是错误 — 仅 Verbose
			UE_LOG(LogTemp, Verbose,
				TEXT("[BaseCharacter][v100.1] HandleRegenStateChanged: RegenSound=nullptr, 不广播. "
					 "Owner=%s. 如需启用声音, 在 BP HealthRegenComponent 字段配 Sound 资产."),
				*GetName());
			return;
		}

		UE_LOG(LogTemp, Display,
			TEXT("[BaseCharacter][v133.11] HandleRegenStateChanged: 触发 Client_PlayRegenSound (OwnerOnly). "
				 "Owner=%s Sound=%s IsPlayerControlled=%d"),
			*GetName(), *Sound->GetName(), IsPlayerControlled() ? 1 : 0);

		// 【v133.11 P0 大厂架构 — 严格 OwnerOnly】
		//   - 玩家母体: Client RPC 只发给该玩家 (IsPlayerControlled()=true → PC->ClientRPC → 只有该玩家执行)
		//   - AI 母体: 跳过 (AI 无 Owner 客户端, Client RPC 不会广播; 且按业务决定 — AI 母体不播回血音)
		//   - 大厂原则 — 零兜底: 严格按 IsPlayerControlled() 分流, 不允许模糊边界
		if (IsPlayerControlled())
		{
			Client_PlayRegenSound(Sound);
		}
		else
		{
			UE_LOG(LogTemp, Verbose,
				TEXT("[BaseCharacter][v133.11] HandleRegenStateChanged: AI 母体不播回血音 (业务决策 2026.08.02). "
					 "Owner=%s"),
				*GetName());
		}
	}
	else
	{
		UE_LOG(LogTemp, Display,
			TEXT("[BaseCharacter][v133.11] HandleRegenStateChanged: 触发 Client_StopRegenSound (OwnerOnly). "
				 "Owner=%s IsPlayerControlled=%d"),
			*GetName(), IsPlayerControlled() ? 1 : 0);

		// 【v133.11 P0 大厂架构 — 严格 OwnerOnly】配对 Client_PlayRegenSound
		if (IsPlayerControlled())
		{
			Client_StopRegenSound();
		}
		// AI 母体本就没播放音, 无需 Stop (IsPlayerControlled()=false 跳过)
	}
}


/**
 * ABaseCharacter::Client_PlayRegenSound_Implementation
 *
 * 客户端实现: 接收 Sound 资产引用 → 创建/缓存 UAudioComponent 循环播放
 *
 * 【v133.11 P0 大厂架构】严格 OwnerOnly (Client RPC)
 *   - 只发给 Owner (母体本人) → 只有该玩家客户端执行本函数 → 只有该玩家听到
 *   - 场景中其他玩家的客户端不调用本函数 → 完全听不到 (零串音)
 *
 * 大厂原则 — 单一真理源:
 *   - ActiveRegenAudioComponent 是本机唯一缓存 (避免重复 Spawn)
 *   - 已存在 → 先 Stop + Destroy 再 Spawn 新组件 (避免残留)
 *
 * 大厂原则 — 零兜底:
 *   - Sound=nullptr → Log Warning + return (业务禁用声音, 不算错)
 *   - GetMesh() 为空 → Log Error (BP 配错, 不允许)
 *   - SpawnSoundAttached 失败 → Log Error (引擎异常)
 */
void ABaseCharacter::Client_PlayRegenSound_Implementation(USoundBase* Sound)
{
	if (!Sound)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[BaseCharacter][v133.11] Client_PlayRegenSound: Sound=nullptr, 拒绝播放. "
				 "Owner=%s. (业务可能故意禁用声音)"),
			*GetName());
		return;
	}

	// 幂等: 已有活跃组件 → 先销毁 (避免多实例重叠)
	if (ActiveRegenAudioComponent)
	{
		ActiveRegenAudioComponent->Stop();
		ActiveRegenAudioComponent->DestroyComponent();
		ActiveRegenAudioComponent = nullptr;
	}

	// 必须挂到 Mesh 上 (3D 衰减 + 跟角色移动)
	USkeletalMeshComponent* BodyMesh = GetMesh();
	if (!BodyMesh)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[BaseCharacter][v133.11] Client_PlayRegenSound: GetMesh() 为空! "
				 "Owner=%s. 【零兜底】检查 BP 是否有 Mesh 组件."),
			*GetName());
		return;
	}

	// 附身到 Mesh, 循环播放, 不自动销毁
	ActiveRegenAudioComponent = UGameplayStatics::SpawnSoundAttached(
		Sound,
		BodyMesh,
		NAME_None, // attach to root socket
		FVector::ZeroVector,
		FRotator::ZeroRotator,
		EAttachLocation::KeepRelativeOffset,
		/*bStopWhenAttachedToDestroyed=*/true,
		/*VolumeMultiplier=*/1.0f,
		/*PitchMultiplier=*/1.0f,
		/*StartTime=*/0.0f,
		/*AttenuationSettings=*/nullptr,
		/*ConcurrencySettings=*/nullptr,
		/*bAutoDestroy=*/false // 关键: 不自动销毁, 我们手动控制
	);

	if (!ActiveRegenAudioComponent)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[BaseCharacter][v133.11] Client_PlayRegenSound: SpawnSoundAttached 失败. "
				 "Owner=%s, Sound=%s. 【零兜底】检查 Sound 资产是否损坏."),
			*GetName(), *Sound->GetName());
		return;
	}

	UE_LOG(LogTemp, Display,
		TEXT("[BaseCharacter][v133.11] Client_PlayRegenSound: 创建循环音组件成功 (OwnerOnly). "
			 "Owner=%s, Sound=%s"),
		*GetName(), *Sound->GetName());
}


/**
 * ABaseCharacter::Client_StopRegenSound_Implementation
 *
 * 客户端实现: 停止 + 销毁活跃音组件
 *
 * 【v133.11 P0 大厂架构】严格 OwnerOnly (Client RPC)
 *   - 配对 Client_PlayRegenSound, 也只发给 Owner
 *
 * 大厂原则 — 零兜底:
 *   - 没找到活跃组件 → Log Verbose (可能本机没开过音, 正常情况)
 *   - 找到 → Stop + DestroyComponent (下次激活时新建)
 */
void ABaseCharacter::Client_StopRegenSound_Implementation()
{
	if (!ActiveRegenAudioComponent)
	{
		UE_LOG(LogTemp, Verbose,
			TEXT("[BaseCharacter][v133.11] Client_StopRegenSound: ActiveRegenAudioComponent=nullptr. "
				 "Owner=%s. (本机可能没开过音, 正常情况)"),
			*GetName());
		return;
	}

	UE_LOG(LogTemp, Display,
		TEXT("[BaseCharacter][v133.11] Client_StopRegenSound: 停止 + 销毁音组件 (OwnerOnly). "
			 "Owner=%s, Sound=%s"),
		*GetName(),
		ActiveRegenAudioComponent->Sound ? *ActiveRegenAudioComponent->Sound->GetName() : TEXT("nullptr"));

	ActiveRegenAudioComponent->Stop();
	ActiveRegenAudioComponent->DestroyComponent();
	ActiveRegenAudioComponent = nullptr;
}

// ==========================================
// 【v200 大厂架构新增】丢弃主武器
// ==========================================

/**
 * OnDropWeaponPressed — 丢弃武器输入回调
 */
void ABaseCharacter::OnDropWeaponPressed(const FInputActionValue& Value)
{
	if (IsDead())
	{
		return;
	}

	if (!IsPlayerControlled())
	{
		return;
	}

	Server_DropPrimaryWeapon();
}

/**
 * Server_DropPrimaryWeapon_Implementation — 丢弃主武器 RPC 实现
 */
void ABaseCharacter::Server_DropPrimaryWeapon_Implementation()
{
	UE_LOG(LogTemp, Display,
		TEXT("[BaseCharacter] Server_DropPrimaryWeapon: ENTER. Pawn=%s"),
		*GetName());

	UWeaponAttachmentComponent* WeaponAttachComp = ResolveWeaponAttach();
	if (!WeaponAttachComp)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[BaseCharacter] Server_DropPrimaryWeapon: WeaponAttach 组件未找到. Pawn=%s"),
			*GetName());
		return;
	}

	// 检查当前武器槽位
	EWeaponSlotType CurrentSlot = WeaponAttachComp->GetCurrentWeaponSlot();
	ABaseWeapon* CurrentWeaponBefore = WeaponAttachComp->GetCurrentWeapon();
	UE_LOG(LogTemp, Display,
		TEXT("[BaseCharacter] Server_DropPrimaryWeapon: 当前槽位=%s, 当前武器=%s"),
		LexToString(CurrentSlot),
		CurrentWeaponBefore ? *CurrentWeaponBefore->GetName() : TEXT("None"));

	// 【v200 大厂架构】调用 WeaponAttachmentComponent 的丢弃方法
	// 注意: WeaponAttachmentComponent::Server_DropPrimaryWeapon 内部已经处理了:
	// 1. 先切换到近战武器 (Server_SwitchToWeaponSlot)
	// 2. 再丢弃主武器
	// 3. 清空 WeaponsInSlot[Primary]
	// 【v200.2.18 大厂架构重命名】WeaponAttachComp->HandleDropPrimaryWeapon (业务处理器, 非 RPC)
	//   旧版命名 Server_DropPrimaryWeapon 像 RPC 但实际不是, 命名误导
	const bool bDropped = WeaponAttachComp->HandleDropPrimaryWeapon();
	if (!bDropped)
	{
		// WeaponAttachmentComponent 内部已经有详细的错误日志
		UE_LOG(LogTemp, Warning,
			TEXT("[BaseCharacter] Server_DropPrimaryWeapon: WeaponAttachComp->Server_DropPrimaryWeapon 返回 false. Pawn=%s"),
			*GetName());
		return;
	}

	// 检查丢弃后的武器状态
	ABaseWeapon* CurrentWeaponAfter = WeaponAttachComp->GetCurrentWeapon();
	EWeaponSlotType CurrentSlotAfter = WeaponAttachComp->GetCurrentWeaponSlot();
	UE_LOG(LogTemp, Display,
		TEXT("[BaseCharacter] Server_DropPrimaryWeapon: 丢弃完成. 当前武器=%s, 当前槽位=%s"),
		CurrentWeaponAfter ? *CurrentWeaponAfter->GetName() : TEXT("None"),
		LexToString(CurrentSlotAfter));

	UE_LOG(LogTemp, Display,
		TEXT("[BaseCharacter] Server_DropPrimaryWeapon: 丢弃成功. Pawn=%s"),
		*GetName());
}

/**
 * Server_TryPickupWeapon_Implementation — 捡起武器 RPC 实现
 */
void ABaseCharacter::Server_TryPickupWeapon_Implementation(ABaseWeapon* WeaponToPickup)
{
	UWeaponAttachmentComponent* WeaponAttachComp = ResolveWeaponAttach();
	if (!WeaponAttachComp)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[BaseCharacter] Server_TryPickupWeapon: WeaponAttach 组件未找到. Pawn=%s"),
			*GetName());
		return;
	}

	// 【v200.2.18 大厂架构重命名】WeaponAttachComp->HandleTryPickupWeapon (业务处理器, 非 RPC)
	const bool bPickedUp = WeaponAttachComp->HandleTryPickupWeapon(WeaponToPickup);
	if (!bPickedUp)
	{
		// WeaponAttachmentComponent 内部已经有详细的错误日志
		return;
	}

	UE_LOG(LogTemp, Display,
		TEXT("[BaseCharacter] Server_TryPickupWeapon: 捡起成功. Pawn=%s, Weapon=%s"),
		*GetName(),
		WeaponToPickup ? *WeaponToPickup->GetName() : TEXT("None"));
}
