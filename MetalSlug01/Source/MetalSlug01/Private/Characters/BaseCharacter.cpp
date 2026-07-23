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
#include "Kismet/GameplayStatics.h"
#include "Sound/SoundBase.h"           // v76 — 武器切换音效 (USoundBase 完整定义)
#include "Weapons/WeaponSlotType.h"    // v76 — EWeaponSlotType (LexToString)
#include "Data/Tables/WeaponTableRow.h"
#include "Data/Tables/CharacterTableRow.h"
#include "Data/Faction/FactionTags.h" // 【2026.07.10 P0 重构】阵营集中定义

// 【2026.07.12 P0 大厂架构 Phase 2】5 个新 ActorComponent 头文件 (转发壳依赖)
// 注意: 头文件本身由别的 agent 编写 (可能在 PIE 同时编译), 这里只前向声明 + 假设依赖
// 如果某个 Component 头未到位, 注释掉对应 CreateDefaultSubobject 行即可编译
#include "Combat/PlayerComboComponent.h"   // 玩家连击
#include "Combat/AIAttackComponent.h"      // AI 攻击
#include "Combat/CombatDeathComponent.h"   // 战斗死亡
#include "Combat/WeaponAttachmentComponent.h" // 武器装备
#include "Combat/CharacterIconComponent.h"  // 头像/武器图标

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

	UE_LOG(LogTemp, Log, TEXT("[ABaseCharacter] 构造函数 EXIT: this=%p Class=%s WeaponAttach=%p PlayerCombo=%p AIAttack=%p CombatDeath=%p CharacterIcon=%p HealthComp=%p"),
		this, *GetClass()->GetName(), WeaponAttach, PlayerCombo, AIAttack, CombatDeath, CharacterIcon, HealthComponent);

	// ==========================================
	// 角色模型设置
	// ==========================================
	// 【关键修改】: 允许玩家自己看到自己的全身模型
	GetMesh()->SetOwnerNoSee(false);
	GetMesh()->SetRelativeLocation(FVector(0.f, 0.f, -90.f)); // 往下挪一点，对齐胶囊体底盘
	GetMesh()->SetRelativeRotation(FRotator(0.f, -90.f, 0.f)); // 把脸转到正前方

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
void ABaseCharacter::Client_RefreshCharacterIcon_Implementation(const FString& InCharacterID, UTexture2D* Avatar)
{
	if (UCharacterIconComponent* Icon = ResolveCharacterIcon())
	{
		Icon->Client_RefreshCharacterIcon_Implementation(InCharacterID, Avatar);
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
			Combo->SetInputActions(LightAttackAction, HeavyAttackAction, UseSkillAction);
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
 * 大厂原则 — 路由策略:
 *   - 本地玩家 → HUD 拿 Crosshair 世界射线 (玩家射线 = 玩家准星)
 *   - AI / 远端 → 攻击者眼睛视图方向 (AI 没 HUD)
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
	// 路径 A: 本地玩家 → HUD 拿 Crosshair 世界射线
	// ===========================================
	if (IsLocallyControlled())
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

	OutRayDirection = BaseAim.Vector();
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
                                                       EKillMethod KillMethod, bool bIsAssist, bool bIsKillerPlayer)
{
	UE_LOG(LogTemp, Log,
		TEXT("[BaseCharacter] Multicast_NotifyKill: Killer=%s, Victim=%s, Method=%d, IsAssist=%d, bIsKillerPlayer=%d, HasAuthority=%d"),
		*KillerName, *VictimName, static_cast<int32>(KillMethod), bIsAssist ? 1 : 0,
		bIsKillerPlayer ? 1 : 0, HasAuthority() ? 1 : 0);

	// 大厂原则 (v31.6 零兜底): 字符串字段允许为空 (即没读到 PS 名字), 不允许静默崩溃
	// 击杀者加分逻辑: 已在服务器 TakeDamage 流程中本地加, 不依赖 RPC 边界

	// 【v40.9 P0 大厂架构】只有 Killer 是玩家时才推送 KillFeed 图标
	// 业务规则: "击杀图标只用于玩家击杀其他玩家或者AI时才显示，被击杀不应该显示别人的击杀图标"
	// - 玩家击杀玩家 → bIsKillerPlayer=true → KillFeed 显示 ✅
	// - 玩家击杀 AI → bIsKillerPlayer=true → KillFeed 显示 ✅
	// - AI 击杀玩家 → bIsKillerPlayer=false → KillFeed 不显示 ✅
	// - AI 击杀 AI → bIsKillerPlayer=false → KillFeed 不显示 ✅

	if (!bIsKillerPlayer)
	{
		// AI 击杀: KillFeed 不显示，直接 return
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
		const FString AssistantName = AssistantPS->GetPlayerName();
		AssistantChar->Multicast_NotifyKill(AssistantName, VictimName, KillMethod, /*bIsAssist=*/true, bIsKillerPlayer);
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
		TEXT("[BaseCharacter::OnFirePressed] Pawn=%s 客户端射线: Origin=%s Direction=%s → 调 Server_StartFire."),
		*GetName(),
		*ClientRayOrigin.ToCompactString(),
		*ClientRayDirection.ToCompactString());

	// ============================================================
	// 【v85.1 大厂架构 — 主武器射线视觉】
	//
	// 完整流程:
	//   OnFirePressed → Server_StartFire RPC (携带客户端射线参数)
	//     → URangedLineStrategy::PerformSingleShot (服务器权威 trace)
	//       → 服务器本地: EDrawDebugTrace::ForDuration (红/绿线, 5分钟持久)
	//       → Multicast_PlayFireTraceVisual → 所有客户端画红/绿线 (0.5s 短留)
	//
	// 颜色规则:
	//   - 命中 (Hit) = 绿色
	//   - 未命中 (Miss) = 红色
	// ============================================================
	CurrentWeapon->Server_StartFire(FVector_NetQuantize(ClientRayOrigin), FVector_NetQuantizeNormal(ClientRayDirection));
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
	CurrentWeapon->Server_StopFire();
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
	CurrentWeapon->Server_StartReload();
}
