// 版权声明：在项目设置的描述页面填写您的版权信息。

// ==========================================
// 头文件包含区
// ==========================================
// 引入本类头文件
#include "Characters/BaseCharacter.h"

#include "GenericTeamAgentInterface.h"

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
#include "Data/Tables/WeaponTableRow.h"
#include "Data/Tables/CharacterTableRow.h"

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

	// 6. 初始化连击系统状态
	bIsHoldingLightAttack = false;
	bIsAttacking = false;
	CurrentWeapon = nullptr; // 初始状态手里没武器

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
	if (HealthComponent)
	{
		HealthComponent->OnHealthChanged.AddUniqueDynamic(this, &ABaseCharacter::OnHealthChanged_Callback);

		// 【2026-07-01 新增】订阅死亡事件
		// 服务器: HealthComponent::ApplyDamage → OnDeath.Broadcast → OnHealthComponentDeath → Die
		// 客户端: HealthComponent::OnRep_bIsDead → OnDeath.Broadcast → OnHealthComponentDeath → ExecuteDeathLocal
		HealthComponent->OnDeath.AddUniqueDynamic(this, &ABaseCharacter::OnHealthComponentDeath);
	}

	// 【2026-07-01 新增】: CharacterEvents 初始化检查
	// 如果 CharacterEvents 未正确挂载，打一条 Error，便于调试
	if (!CharacterEvents)
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
 */
void ABaseCharacter::OnHealthChanged_Callback(float NewHealth)
{
	// 【2026-07-01 重构 v2】: 改用 CharacterEvents 广播, 替代直接 GameHUDWidget Push
	// 优势: UI 层自主订阅, BaseCharacter 不再依赖 GameHUDWidget
	if (!CharacterEvents || !HealthComponent)
	{
		return;
	}
	CharacterEvents->OnHealthChangedDelegate.Broadcast(NewHealth, HealthComponent->GetMax());
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
	UE_LOG(LogTemp, Error, // 使用 Error 让日志更显眼
		TEXT("[BaseCharacter][OnHealthComponentDeath] ★★★ 触发死亡 ★★★: Pawn=%s HasAuth=%d HealthComponent=%p"),
		*GetName(), HasAuthority() ? 1 : 0, HealthComponent);

	if (!HealthComponent)
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
 * ABaseCharacter::ExecuteDeathLocal
 *
 * 【2026-07-01 新增】本地执行死亡流程 (客户端用, 服务器也可用)
 * 与 Multicast_Die_Implementation 的区别:
 *   - 不调 StartRespawnTimer (复活只在服务器调)
 *   - 不调 ResetAC (仅服务器重置)
 *   - 其他 (武器掉落/溶解/胶囊体/动画/布娃娃) 完全一致
 *
 * 幂等保证: 可能被多次调用 (服务器 Die() 走 Multicast_Die RPC 触发服务器, 客户端又通过 OnRep_bIsDead 触发)
 *   - bDeathSequenceStarted 标志保证只首次执行核心步骤
 *   - 重复调用仅 ResetAC + StartRespawnTimer
 *
 * 死亡流程时序 (t=0 死亡瞬间):
 *   - t=0: 武器立即溶解 + 角色立即溶解 + 胶囊体透明 + 禁用移动 + 死亡动画
 *   - t=0.7*DeathMontageDuration: 启动 Ragdoll
 *   - t=DissolveDuration: 角色和武器溶解完毕
 *   - t=RespawnDelaySeconds: 复活定时器到期, 销毁旧角色
 *
 * 时序约束 (大厂架构规范):
 *   RespawnDelaySeconds > DissolveDuration + DeathMontageDuration
 *   否则身体溶解到一半就被销毁, 视觉效果断裂
 */
void ABaseCharacter::ExecuteDeathLocal()
{
	// 幂等检查: 死亡序列已开始则跳过核心步骤
	if (bDeathSequenceStarted)
	{
		UE_LOG(LogTemp, Verbose,
			TEXT("[BaseCharacter][ExecuteDeathLocal] 幂等跳过: Pawn=%s"),
			*GetName());
		return;
	}
	bDeathSequenceStarted = true;

	UE_LOG(LogTemp, Warning,
		TEXT("[BaseCharacter][ExecuteDeathLocal] Pawn=%s HasAuth=%d Weapon=%s"),
		*GetName(), HasAuthority() ? 1 : 0,
		CurrentWeapon ? *CurrentWeapon->GetName() : TEXT("nullptr"));

	// 1. 武器掉落
	if (CurrentWeapon)
	{
		DropAndFadeWeapon(CurrentWeapon);
		CurrentWeapon = nullptr;
	}

	// 2. 胶囊体透明 + 禁用移动
	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		MoveComp->StopMovementImmediately();
		MoveComp->DisableMovement();
	}
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);

	// 3. 死亡动画 + 定时布娃娃
	float AnimDuration = 0.1f;
	if (DeathMontage)
	{
		AnimDuration = PlayAnimMontage(DeathMontage);
	}
	float TimeToRagdoll = AnimDuration > 0.1f ? (AnimDuration * 0.7f) : 0.1f;
	GetWorldTimerManager().SetTimer(RagdollTimerHandle, this, &ABaseCharacter::EnableRagdoll, TimeToRagdoll, false);

	// 4. 【2026-07-01 P0 修复】角色身体溶解 - 立即启动, 不再用 DissolveDelay 定时器
	// 旧架构 bug: DissolveComponent::OnOwnerDeath 用 DissolveDelay=5s 定时器
	//              复活定时器 3s 就触发销毁, 角色没机会溶解 → 身体"立马消失"
	// 新架构: 死亡编排器 (ExecuteDeathLocal) 显式控制溶解时序, 立即启动
	if (DissolveComponent)
	{
		DissolveComponent->StartDissolveImmediate();
	}
	else
	{
		UE_LOG(LogTemp, Error,
			TEXT("[BaseCharacter][ExecuteDeathLocal] DissolveComponent 未挂载! 角色将无法溶解"));
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

		// 清理死亡流程相关的 timer
		// 注: WeaponDestroy 已改为服务器 SetLifeSpan, 不再使用 TimerHandle
		// 注: DissolveTimerHandle 已迁移到 DissolveComponent, 由其自行清理
		TM.ClearTimer(RagdollTimerHandle);

		// 清理延迟重试 timer
		// 这两个 timer 通过 this 绑定,UE 内部会自动清理,但显式清理更安全
		TM.ClearTimer(CharacterIconRefreshTimerHandle);
		TM.ClearTimer(HUDRefreshTimerHandle);
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
			bIsAttacking ? 1 : 0,
			CurrentWeapon ? 1 : 0,
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
	// 同步武器指针, 让所有人都知道你拿了什么武器
	DOREPLIFETIME(ABaseCharacter, CurrentWeapon);
	// 同步AC/ACE值（注意: MaxAC 是配置常量，无需网络同步）
	DOREPLIFETIME(ABaseCharacter, ACValue);
	DOREPLIFETIME(ABaseCharacter, ACEValue);
	// 【修复 Q3】: LastKillMethod 声明了 UPROPERTY(Replicated), 必须加入 DOREPLIFETIME
	// 背景: Multicast_NotifyKill 在所有客户端读取 LastKillMethod, 若不复制则永远是默认值
	DOREPLIFETIME(ABaseCharacter, LastKillMethod);
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
 * OnRep_CurrentWeapon
 *
 * 武器指针改变时的回调(网络复制通知)
 *
 * 【Bug 3 根因分析 2026.07.10】
 *
 * 历史症状: "玩家复活后多出一把武器"
 * 根因链 (大厂排查链路):
 *   1. 服务器 PossessedBy → SpawnAndEquipWeapon → CurrentWeapon=新武器 → 复制到客户端
 *   2. 客户端 OnRep_CurrentWeapon 触发
 *   3. 客户端的 CharacterID 可能尚未从 PlayerState 复制到位(网络复制优先级)
 *   4. FindWeaponAttachmentConfig 返回 nullptr → 用默认 Socket "WeaponSocket_R"
 *   5. 挂载配置错误 → 武器视觉位置错乱, 看起来"多出一把"
 *
 * 大厂修复架构 (单一协议, 不兜底):
 *   - 只在 CharacterID 非空时才执行挂载 (防御: CharacterID 未复制到位时静默跳过)
 *   - 先脱挂旧武器 (防御: 复活场景 OldWeapon != null)
 *   - 服务器通过 PossessedBy -> SpawnAndEquipWeapon 权威挂载 (正确路径)
 *   - 客户端依赖网络复制 + 正确 CharacterID 后的挂载 (从属路径)
 *   - 日志显式化任何异常, 让美术/策划知道挂载配置缺失
 *
 * 【重要】只有 AI (非本地控制 + 非权威) 才走这个回调
 *         玩家角色: IsLocallyControlled()=true → 已在 PossessedBy 中跳过
 */
void ABaseCharacter::OnRep_CurrentWeapon(ABaseWeapon* OldWeapon)
{
	UE_LOG(LogTemp, Log, TEXT("[WeaponRep] OnRep_CurrentWeapon: Old=%s, New=%s, Local=%d, Auth=%d"),
		*GetNameSafe(OldWeapon), *GetNameSafe(CurrentWeapon), IsLocallyControlled(), HasAuthority());

	// 【Bug 3 修复】服务器和本地控制的角色已经在 PossessedBy 中处理了,跳过
	if (IsLocallyControlled() || HasAuthority())
	{
		return;
	}

	// 【Bug 3 修复 1】OldWeapon 脱挂 — 复活场景 OldWeapon != null
	// 防御: 如果 OldWeapon 已失效, 跳过脱挂 (安全 no-op)
	if (OldWeapon && IsValid(OldWeapon))
	{
		OldWeapon->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
		UE_LOG(LogTemp, Log, TEXT("[WeaponRep] 脱挂旧武器: %s"), *OldWeapon->GetName());
	}

	if (!CurrentWeapon)
	{
		// 新武器为空, 说明服务器清空了武器, 客户端也已脱挂 (见上方), 直接返回
		return;
	}

	// 【Bug 3 修复 2】CharacterID 未就绪时静默跳过
	// 防御: CharacterID 通过 PlayerState 网络复制, 优先级可能低于 CurrentWeapon
	//       如果 CharacterID 尚未到达, FindWeaponAttachmentConfig 会用错误的默认配置
	//       直接跳过挂载, 等下一次 OnRep (CharacterID 到位后) 或让 PossessedBy 兜底
	if (CharacterID.IsEmpty())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[WeaponRep] 跳过挂载: CharacterID 尚未复制到位 (可能 PlayerState 未同步). "
				 "Weapon=%s 将由 PossessedBy 或下次 OnRep 处理."),
			*CurrentWeapon->GetName());
		return;
	}

	// 从 WeaponDataTable 反查 WeaponID
	ARoomGameMode* GameMode = Cast<ARoomGameMode>(GetWorld()->GetAuthGameMode());
	FString WeaponIDToFind;

	if (GameMode && GameMode->WeaponDataTable)
	{
		static const FString WeaponContextString(TEXT("WeaponRepLookup"));
		for (const FName& RowName : GameMode->WeaponDataTable->GetRowNames())
		{
			if (FWeaponInfo* Info = GameMode->WeaponDataTable->FindRow<FWeaponInfo>(RowName, WeaponContextString))
			{
				// WeaponBlueprint 是 TSoftClassPtr, 需要 LoadSynchronous 拿到 UClass* 再 IsA
				if (!Info->WeaponBlueprint.IsNull())
				{
					UClass* WeaponClass = Info->WeaponBlueprint.LoadSynchronous();
					if (WeaponClass && CurrentWeapon->IsA(WeaponClass))
					{
						WeaponIDToFind = RowName.ToString();
						break;
					}
				}
			}
		}
	}

	// 【Bug 3 修复 3】WeaponID 反查失败时报警 (显式错误, 不兜底)
	if (WeaponIDToFind.IsEmpty())
	{
		UE_LOG(LogTemp, Error,
			TEXT("[WeaponRep] 无法反查 WeaponID: CharacterID=%s, Weapon=%s. "
				 "请检查 WeaponDataTable 中是否存在该武器的 Blueprint 配置. "
				 "武器将使用默认挂载 Socket (WeaponSocket_R)."),
			*CharacterID, *CurrentWeapon->GetName());
		// 不 return — 用默认配置继续挂载 (让武器至少能显示出来)
		// 这是"部分失败优雅降级", 不是"兜底行为"
	}

	// 查找挂载配置
	FWeaponAttachmentConfig* AttachmentConfig = FindWeaponAttachmentConfig(CharacterID, WeaponIDToFind);

	FName SocketName = TEXT("WeaponSocket_R");
	FVector RelativeLocation = FVector::ZeroVector;
	FRotator RelativeRotation = FRotator::ZeroRotator;

	if (AttachmentConfig)
	{
		SocketName = AttachmentConfig->SocketName;
		RelativeLocation = AttachmentConfig->RelativeLocation;
		RelativeRotation = AttachmentConfig->RelativeRotation;
	}
	else
	{
		// 【Bug 3 修复 4】挂载配置缺失时显式报警 (大厂原则: 显式错误 > 静默兼容)
		UE_LOG(LogTemp, Warning,
			TEXT("[WeaponRep] 挂载配置缺失: CharacterID=%s + WeaponID=%s 未找到 AttachmentConfig. "
				 "请检查 WeaponAttachmentDataTable 中是否配置了该组合. "
				 "使用默认 Socket=%s."),
			*CharacterID, *WeaponIDToFind, *SocketName.ToString());
	}

	// 将武器挂载到插槽
	FAttachmentTransformRules AttachmentRules = FAttachmentTransformRules::SnapToTargetNotIncludingScale;
	CurrentWeapon->AttachToComponent(GetMesh(), AttachmentRules, SocketName);

	if (!RelativeLocation.IsNearlyZero())
	{
		CurrentWeapon->SetActorRelativeLocation(RelativeLocation);
	}
	if (!RelativeRotation.IsNearlyZero())
	{
		CurrentWeapon->SetActorRelativeRotation(RelativeRotation);
	}

	UE_LOG(LogTemp, Log, TEXT("[WeaponRep] 挂载完成: Socket=%s, CharID=%s, WeaponID=%s"),
		*SocketName.ToString(), *CharacterID, *WeaponIDToFind);

	// 刷新战斗 HUD 上的武器图标（远程玩家也需要在自己客户端看到）
	if (IsLocallyControlled())
	{
		if (APlayerController* PC = GetController<APlayerController>())
		{
			if (AMyGameHUD* HUD = Cast<AMyGameHUD>(PC->GetHUD()))
			{
				if (UGameHUDWidget* GameHUD = HUD->GetGameHUDWidget())
				{
					GameHUD->UpdateWeaponIconFromID(WeaponIDToFind);
				}
			}
		}
	}
}


/**
 * OnRep_ACValue
 *
 * AC 值改变时的客户端回调
 */
void ABaseCharacter::OnRep_ACValue()
{
	// 【2026-07-01 重构 v2】: 改用 CharacterEvents 广播
	if (!CharacterEvents)
	{
		return;
	}
	CharacterEvents->OnACValueChanged.Broadcast(ACValue);

	// AC 变化时重新查询排名并刷新 ACE 颜色 (内部也改用事件)
	RefreshACEWithRank();
}


/**
 * OnRep_ACEValue
 *
 * ACE 值改变时的客户端回调
 */
void ABaseCharacter::OnRep_ACEValue()
{
	// 【2026-07-01 重构 v2】: 改用 CharacterEvents 广播
	// OnRep_ACEValue 在所有客户端上触发, CharacterEvents 组件本身在本地,
	// 所以这里的广播天然只在本地执行 (不需要额外的 IsLocallyControlled 守卫)
	if (!CharacterEvents)
	{
		return;
	}
	// 刷新 ACE 数值 (无排名, 用默认白色)
	CharacterEvents->OnACEValueChanged.Broadcast(ACEValue);

	// ACE 变化时也需要查询排名 (因为其他玩家的 ACE 变化也会影响你的排名)
	RefreshACEWithRank();
}


// ==========================================
// 7. 角色图标/武器图标刷新
// ==========================================

/**
 * RefreshCharacterIcon
 *
 * 核心修复: 本地角色直接在服务器侧走一遍 Client RPC 流程
 * 服务器先拿到 CharacterID，再通知所属客户端刷新头像
 * 这样远程玩家也能看到其他人的头像图标了
 */
void ABaseCharacter::RefreshCharacterIcon()
{
	FString CharID;
	if (APlayerState* PS = GetPlayerState<APlayerState>())
	{
		if (ARoomPlayerState* RoomPS = Cast<ARoomPlayerState>(PS))
		{
			CharID = RoomPS->GetSelectedCharacterID();
		}
	}

	if (CharID.IsEmpty())
	{
		CharID = TEXT("Warrior");
	}

	// 【2026-07-01 重构 v2】: 服务器直接查表获取头像贴图并传递给客户端
	// 原因: GetAuthGameMode 在客户端上可能为 nullptr, 导致客户端查表失败产生白板
	// 新架构: 服务器侧查表, 通过 RPC 传递 UTexture2D* (UE 支持 Client RPC 传 UObject*)
	UTexture2D* Avatar = GetCharacterAvatarFromTable(CharID);

	// 服务器通知所属客户端刷新头像 (Avatar 由服务器传递, 避免客户端查表失败)
	Client_RefreshCharacterIcon(CharID, Avatar);
}


/**
 * Client_RefreshCharacterIcon_Implementation
 *
 * Client RPC: 在所属客户端上刷新头像
 * 【2026-07-01 重构 v2】: 改用 CharacterEvents 广播事件
 *
 * 架构变更:
 *   - 旧: 服务器只传 CharacterID, 客户端自己查 DataTable (GetAuthGameMode 在客户端为 nullptr, 导致白板)
 *   - 新: 服务器直接传递 UTexture2D* Avatar, 客户端通过 CharacterEvents->OnCharacterIconReady 广播
 *
 * IsLocallyControlled 守卫说明:
 *   Client RPC 按 UE 语义只在所属客户端上执行,
 *   加 IsLocallyControlled() 是双保险, 防止未来 UE 语义变化或被意外 Multicast 时污染 HUD
 */
void ABaseCharacter::Client_RefreshCharacterIcon_Implementation(const FString& InCharacterID, UTexture2D* Avatar)
{
	// 【P0 双保险】: 必须是本机玩家才允许改本机 HUD
	if (!IsLocallyControlled())
	{
		return;
	}

	// 缓存 ID，延迟刷新时使用
	CachedCharacterIDForIcon = InCharacterID;

	// HUD 未就绪 → 延迟重试 (保留重试机制, CharacterEvents 组件本身也受 HUD 生命周期影响)
	if (!TryResolveHUDWidget())
	{
		UE_LOG(LogTemp, Warning, TEXT("[Icon] HUD 未就绪, 延迟刷新, InCharacterID=%s"), *InCharacterID);
		GetWorld()->GetTimerManager().SetTimer(CharacterIconRefreshTimerHandle, this, &ABaseCharacter::RetryRefreshCharacterIcon, 0.5f, false);
		return;
	}

	// 通过 CharacterEvents 广播头像事件 (替代直接 GameHUDWidget Push)
	BroadcastCharacterIconReady(InCharacterID, Avatar);

	// 刷新武器图标
	RefreshWeaponIconOnHUD();
}


/**
 * RetryRefreshCharacterIcon
 *
 * 延迟重试回调: 直接在客户端重新执行 HUD 刷新逻辑
 * 跳过服务器中转
 *
 * 【P0 修复 2026-06-29】: 加最大重试次数 + 防御性检查, 与 RetryRefreshHUD 保持一致
 */
void ABaseCharacter::RetryRefreshCharacterIcon()
{
	// ==========================================
	// 【P0 防御 1】: Character 或 World 已失效 → 停止重试
	// ==========================================
	if (!IsValid(this) || !GetWorld())
	{
		return;
	}

	// ==========================================
	// 【P0 防御 2】: 2026-07-01 新增 - 仅本机玩家需要刷新 HUD
	// 与 Client_RefreshCharacterIcon_Implementation 保持一致, 防止非本地玩家的
	// 定时器意外跑起来污染本机 HUD
	// ==========================================
	if (!IsLocallyControlled())
	{
		return;
	}

	// ==========================================
	// 【P0 防御 3】: 最大重试次数限制
	// ==========================================
	const int32 MaxRetries = 20;
	CurrentIconRetryCount++;
	if (CurrentIconRetryCount >= MaxRetries)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[BaseCharacter] RetryRefreshCharacterIcon 达到最大重试次数 (%d 次), 放弃"), CurrentIconRetryCount);
		CurrentIconRetryCount = 0;
		GetWorld()->GetTimerManager().ClearTimer(CharacterIconRefreshTimerHandle);
		return;
	}

	// 【2026-06-15 重构】: HUD 获取收敛到 TryResolveHUDWidget
	if (!TryResolveHUDWidget())
	{
		// HUD 仍未就绪, 继续重试 (降级为 Log)
		UE_LOG(LogTemp, Verbose, TEXT("[Icon] HUD 未就绪, 重试中... (%d/%d), CachedID=%s"),
			CurrentIconRetryCount, MaxRetries, *CachedCharacterIDForIcon);
		GetWorld()->GetTimerManager().SetTimer(CharacterIconRefreshTimerHandle, this, &ABaseCharacter::RetryRefreshCharacterIcon, 0.5f, false);
		return;
	}

	// HUD 已就绪，直接刷新（使用缓存的 ID）
	UE_LOG(LogTemp, Log, TEXT("[Icon] 延迟重试刷新角色图标和武器图标, CachedID=%s (重试 %d 次后成功)"),
		*CachedCharacterIDForIcon, CurrentIconRetryCount);
	CurrentIconRetryCount = 0;  // 成功 → 重置

	// ==========================================
	// 【2026-07-01 P0 修复】"事件 + 缓存"双轨制补发:
	//   - 优先从 CharacterEvents 缓存拉取 (服务器原始 Avatar)
	//   - 缓存为空时再尝试 GetCharacterAvatarFromTable (但客户端 GetAuthGameMode 为 nullptr, 必然失败)
	//   - 这样: 若 BaseCharacter 之前已 Broadcast 过头像 (即便没订阅者), 我们仍能从缓存拿到
	// ==========================================
	UTexture2D* Avatar = nullptr;
	FString CharIDToBroadcast = CachedCharacterIDForIcon;

	if (CharacterEvents)
	{
		// 1. 优先: 从缓存拉 (服务器已 Broadcast 过的 Avatar)
		FString CachedID;
		UTexture2D* CachedAvatar = nullptr;
		if (CharacterEvents->GetCachedCharacterIcon(CachedID, CachedAvatar))
		{
			Avatar = CachedAvatar;
			CharIDToBroadcast = CachedID;
			UE_LOG(LogTemp, Log,
				TEXT("[Icon] RetryRefreshCharacterIcon: 从 CharacterEvents 缓存取 Avatar, CachedID=%s, Avatar=%s"),
				*CachedID, Avatar ? *Avatar->GetName() : TEXT("nullptr"));
		}
		else
		{
			// 2. 兜底: 客户端查表 (但 GetAuthGameMode 在客户端为 nullptr, 几乎必然 nullptr)
			Avatar = GetCharacterAvatarFromTable(CachedCharacterIDForIcon);
			UE_LOG(LogTemp, Warning,
				TEXT("[Icon] RetryRefreshCharacterIcon: 缓存为空, 客户端查表失败 (预期), Avatar=%s"),
				Avatar ? *Avatar->GetName() : TEXT("nullptr"));
		}
	}

	BroadcastCharacterIconReady(CharIDToBroadcast, Avatar);
	RefreshWeaponIconOnHUD();
}


/**
 * RefreshWeaponIconOnHUD
 *
 * 刷新武器图标到 HUD
 * 优先从服务器缓存的武器 ID 读取（绕过 PlayerState 复制时序问题）
 * 【2026-07-01 重构 v2】: 改用 CharacterEvents 广播，替代直接 GameHUDWidget Push
 */
void ABaseCharacter::RefreshWeaponIconOnHUD()
{
	// 优先从服务器缓存的武器 ID 读取（绕过 PlayerState 复制时序问题）
	FString WeaponID;
	if (ARoomGameMode* GM = Cast<ARoomGameMode>(GetWorld()->GetAuthGameMode()))
	{
		FString CachedCharID;
		FString CachedWeaponID;
		if (GM->GetPlayerSpawnData(GetController()->GetUniqueID(), CachedCharID, CachedWeaponID))
		{
			WeaponID = CachedWeaponID;
		}
	}

	if (WeaponID.IsEmpty())
	{
		return;
	}

	// 【2026-07-01 重构 v2】: 通过 CharacterEvents 广播武器图标事件
	// 由 GameHUDWidget 订阅并异步加载
	if (CharacterEvents)
	{
		CharacterEvents->OnWeaponIconReady.Broadcast(WeaponID, nullptr);
	}
}


/**
 * GetCharacterAvatarFromTable
 *
 * 从 CharacterDataTable 查指定角色 ID 的头像贴图
 * @return 头像 UTexture2D（找不到返回 nullptr）
 */
UTexture2D* ABaseCharacter::GetCharacterAvatarFromTable(const FString& CharID)
{
	if (CharID.IsEmpty()) return nullptr;

	// 通过 GameMode 获取数据表
	ARoomGameMode* GM = Cast<ARoomGameMode>(GetWorld()->GetAuthGameMode());
	if (!GM || !GM->CharacterDataTable) return nullptr;

	static const FString ContextString(TEXT("CharacterAvatarLookup"));
	FCharacterInfo* Info = GM->CharacterDataTable->FindRow<FCharacterInfo>(FName(*CharID), ContextString);
	if (Info && Info->AvatarIcon)
	{
		return Info->AvatarIcon;
	}

	return nullptr;
}


/**
 * RefreshACEWithRank
 *
 * 查询当前 ACE 排名并刷新 HUD 上的 ACE 文字颜色
 * - 全场第一: 金色
 * - 队内第一: 白色
 * - 其他: 无
 */
void ABaseCharacter::RefreshACEWithRank()
{
	// 【2026-07-01 重构 v2】: 改用 CharacterEvents 广播 ACE + 排名颜色事件
	// IsLocallyControlled 守卫: ACE 排名只对本机玩家有意义
	if (!IsLocallyControlled() || !CharacterEvents)
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

	// 3. 获取我的队伍
	ERoomTeam MyTeam = MyPS->CurrentTeam;

	// 4. 查询队内 AC 最高者
	ARoomPlayerState* TeamTop = GS->GetTeamTopACPlayer(MyTeam);

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
	CharacterEvents->OnACEWithRankChanged.Broadcast(ACEValue, RankType);
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
	if (!EnergyComponent)
	{
		return false;
	}

	// 消耗能量后打断回复 (有动作表示玩家活跃)
	const bool bSuccess = EnergyComponent->Consume(Amount);
	if (bSuccess)
	{
		if (HealthRegenComponent)
		{
			HealthRegenComponent->NotifyDamageTaken();
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
	if (EnergyComponent)
	{
		EnergyComponent->Add(EnergyRewardPerKill);
	}
	// 【2026-06-15 重构】: 通过 HealthComponent 加血
	if (HealthComponent)
	{
		HealthComponent->Heal(HealthRewardPerKill);
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

		// 【连斩核心绑定】: 左键按下 (Started) 和 松开 (Completed)
		if (LightAttackAction)
		{
			EnhancedInputComponent->BindAction(LightAttackAction, ETriggerEvent::Started, this, &ABaseCharacter::LightAttack_Pressed);
			EnhancedInputComponent->BindAction(LightAttackAction, ETriggerEvent::Completed, this, &ABaseCharacter::LightAttack_Released);
		}

		// 重击只需单击
		if (HeavyAttackAction) EnhancedInputComponent->BindAction(HeavyAttackAction, ETriggerEvent::Started, this, &ABaseCharacter::HeavyAttack);

		// 绑定技能释放
		if (UseSkillAction) EnhancedInputComponent->BindAction(UseSkillAction, ETriggerEvent::Started, this, &ABaseCharacter::UseSkill);

		// 绑定下蹲 (按下时触发 StartCrouch，松开时触发 StopCrouch)
		if (CrouchAction)
		{
			EnhancedInputComponent->BindAction(CrouchAction, ETriggerEvent::Started, this, &ABaseCharacter::StartCrouch);
			EnhancedInputComponent->BindAction(CrouchAction, ETriggerEvent::Completed, this, &ABaseCharacter::StopCrouch);
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
// 12. 自动连斩系统核心逻辑
// ==========================================

/**
 * LightAttack_Pressed
 *
 * 轻击按下事件
 * 1. 死亡/暂停/无武器/下蹲禁止攻击都不能挥刀
 * 2. 记录按住状态
 * 3. 根据武器配置决定是否锁步
 * 4. 第一刀起手: 初始化状态机
 * 5. 连击区间: 记录缓存
 */
void ABaseCharacter::LightAttack_Pressed()
{
	// 死人、武器未装备不能攻击
	// 【注意】暂停检查已移除: 暂停逻辑改由 PlayerController 通过 InputMode=UIOnly 阻塞,
	// 不再使用全局 SetGamePaused, 因此 BaseCharacter 无需再读全局暂停状态
	if (IsDead() || !CurrentWeapon) return;

	// 下蹲攻击权限拦截
	// 如果你正蹲着，并且这把武器禁止下蹲攻击，直接 return，无视玩家按键
	if (bIsCrouched && !CurrentWeapon->bCanAttackWhileCrouched)
	{
		return;
	}

	bIsHoldingLightAttack = true; // 记录按住状态

	// 根据当前武器的配置，决定要不要锁步
	bIsMovementLocked = !CurrentWeapon->bCanMoveWhileLightAttack;

	if (bIsMovementLocked)
	{
		GetCharacterMovement()->MaxWalkSpeed = 0.0f; // 物理刹车
	}

	if (!bIsAttacking)
	{
		// 1. 第一刀起手: 上锁，初始化状态
		bIsAttacking = true;
		ComboIndex = 1;
		bSaveAttack = false;
		bCanReceiveInput = false;
		ExecuteComboSequence();
	}
	else if (bCanReceiveInput)
	{
		// 2. 如果正在挥刀，且【绿色输入区间】开启了
		// 记录玩家点过鼠标了（不管点几次，只记为 true）
		bSaveAttack = true;
	}
}


/**
 * LightAttack_Released
 *
 * 轻击松开事件
 * 记录: 玩家松手了
 */
void ABaseCharacter::LightAttack_Released()
{
	bIsHoldingLightAttack = false; // 记录: 玩家松手了
}


/**
 * ExecuteComboSequence
 *
 * 连招核心执行逻辑
 * 工业级做法: 所有连招全在第 0 个蒙太奇里
 * 智能拼接要跳转的片段名字 (Combo1 或 Combo2)
 */
void ABaseCharacter::ExecuteComboSequence()
{
	if (!CurrentWeapon) return;

	// 工业级做法: 所有连招全在第 0 个蒙太奇里
	UAnimMontage* ComboMontage = CurrentWeapon->GetAttackMontage(false, 0);
	if (ComboMontage)
	{
		// 智能拼接要跳转的片段名字 (Combo1 或者是 Combo2)
		FName SectionName = (ComboIndex == 1) ? FName("Combo1") : FName("Combo2");

		PlayAnimMontage(ComboMontage, 1.0f, SectionName);

		// 【激活网络同步】: 告诉服务器我出的是第几刀
		Server_PlayAttackAnim(false, ComboIndex);
	}
}


/**
 * HeavyAttack
 *
 * 重击事件（单击）
 * 注意: 当前版本 HeavyAttack 实现被注释，仅留下权限检查
 */
void ABaseCharacter::HeavyAttack()
{
	// 死人、武器未装备不能重击
	// 【注意】暂停检查已移除: 暂停逻辑改由 PlayerController 通过 InputMode=UIOnly 阻塞,
	// 不再使用全局 SetGamePaused, 因此 BaseCharacter 无需再读全局暂停状态
	if (IsDead() || !CurrentWeapon) return;

	// 【新增】: 重击同样需要检查下蹲权限
	if (bIsCrouched && !CurrentWeapon->bCanAttackWhileCrouched)
	{
		return;
	}
	// 注: 重击主体实现已注释（保留扩展位）
}


/**
 * UseSkill
 *
 * 释放技能事件
 * TODO: 技能系统具体实现
 */
void ABaseCharacter::UseSkill()
{
	// 死亡状态不能释放技能
	// 【注意】暂停检查已移除: 暂停逻辑改由 PlayerController 通过 InputMode=UIOnly 阻塞,
	// 不再使用全局 SetGamePaused, 因此 BaseCharacter 无需再读全局暂停状态
	if (IsDead()) return;

	// TODO: 技能系统具体实现
	// 这里可以扩展为技能槽系统，支持多个技能
}


// ==========================================
// 13. 动画通知 (Anim Notify) 回调接口
// ==========================================

/**
 * EnableComboWindow
 *
 * 区间开始时触发（开启连招窗口）
 * 绿灯亮起，开始接收输入
 */
void ABaseCharacter::EnableComboWindow()
{
	bCanReceiveInput = true; // 绿灯亮起，开始接收输入
	bSaveAttack = false;     // 清空上一轮的垃圾缓存
}


/**
 * CheckCombo
 *
 * 区间结束时触发（检查缓存区是否有缓存攻击）
 * 终极结算: 如果有缓存的点击或玩家正死死按住左键，则跳转下一段
 */
void ABaseCharacter::CheckCombo()
{
	bCanReceiveInput = false; // 绿灯熄灭，关闭窗口

	// 【终极结算】: 如果有缓存的点击，或者玩家正死死按住左键
	if (bSaveAttack || bIsHoldingLightAttack)
	{
		// 【新增防逃课锁】: 连击派生前，检查当前是不是已经蹲下了
		if (bIsCrouched && !CurrentWeapon->bCanAttackWhileCrouched)
		{
			bSaveAttack = false; // 清除缓存
			return; // 强行中断后续连招
		}

		bSaveAttack = false; // 消耗掉这次缓存

		// 核心需求: 1变2，2变1，无限循环
		ComboIndex = (ComboIndex == 1) ? 2 : 1;

		ExecuteComboSequence(); // 丝滑跳转下一刀
	}
}


/**
 * EndAttackState
 *
 * 动画彻底结束时触发
 * 彻底收招，解开所有锁
 */
void ABaseCharacter::EndAttackState()
{
	// 彻底收招，解开所有锁
	bIsAttacking = false;
	bSaveAttack = false;
	bCanReceiveInput = false;
	ComboIndex = 1;
	// 收刀时，彻底解除移动锁定
	bIsMovementLocked = false;
	GetCharacterMovement()->MaxWalkSpeed = 600.0f; // 恢复正常速度
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
// OnAIRequestAttack_Simple — AI 专用轻攻击入口 (P0 大厂架构重构 2026.07.06)
//
// 【大厂架构设计思路】
//   旧设计 (硬路径): CurrentWeapon->GetAttackMontage(false, 1)
//                    一旦 BP 配错 → null → 攻击失败 → AI 不动
//   新设计 (职责链): AIAttackMontageResolver::ResolveLightAttackMontage(...)
//                    4 层兜底: 设计意图 → ComboIndex=0 → 重击代替 → 失败
//                    任意一层能拿到蒙太奇 → AI 都能"挥刀"
//
// 【为什么 ComboIndex=1?】
//   玩家轻击连击从 Combo1 开始 (Combo1/Combo2/Combo3 三段)
//   AI 只需要"挥一刀", 不做连击, 直接用 Combo1 段 (ComboIndex=1)
//   注: ComboIndex 是段号, 0 是预备段, 1/2/3 是 3 段连击
//
// 【为什么单独走 AI 通道不调 LightAttack_Pressed?】
//   玩家路径: LightAttack_Pressed → 设 bIsMovementLocked=true, MaxWalkSpeed=0
//             → 动画蓝图 NotifyEnd → EndAttackState → 解锁
//   AI 风险: 如果动画蓝图 NotifyEnd 漏配 / 时序错位 → AI 永远 lock → 不动
//   AI 通道: 不 lock 移动, 攻击期间也能跑, 真正符合 AI 行为模式
bool ABaseCharacter::OnAIRequestAttack_Simple()
{
	// ============================================================
	// 【P0 2026.07.06 19:25 大厂架构加固】本地时间戳节流
	//
	// 背景: BTTask 层 (BB.CooldownEndTime + BTDecorator_CooldownReady 实时决策, P0 2026.07.09 重构后)
	//       已做防抖, 但用户原问题"AI 持续连击"可能源于:
	//         (a) BT 跳过 BTTask 直接调 OnAIRequestAttack_Simple (代码路径耦合)
	//         (b) Timer 句柄失效 / OwnerComp 销毁导致 Token 永远卡在 true 时又被绕过
	//         (c) 多个 BT 共享同一 TaskNode 实例, Timer 串扰
	//
	// 防御: 在 Character 自己手里再加一道墙 — 上次攻击时间戳 + AttackInterval
	//       即使所有上层防抖都失效, 玩家也不会被连击到恶心
	//
	// 注意: 攻击间隔优先读 Profile.AttackInterval (策划最直观位置),
	//       fallback 到 ConfigSO + 难度缩放 (走 AIController 统一入口),
	//       最后兜底 1.2s
	// ============================================================
	{
		float AttackInterval = 1.2f;

		// 从 AI Controller 拿到最终攻击间隔
		if (AAIController* AIC = Cast<AAIController>(GetController()))
		{
			if (ABaseAIController* BaseAIC = Cast<ABaseAIController>(AIC))
			{
				AttackInterval = BaseAIC->GetEffectiveAttackInterval();
			}
		}

		// AttackInterval == 0 表示"完全禁用攻击" (Profile 里故意设的)
		if (AttackInterval <= 0.0f)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[BaseCharacter] OnAIRequestAttack_Simple: AI=%s AttackInterval=%.2f <= 0, 禁止攻击"),
				*GetName(), AttackInterval);
			return false;
		}

		// 检查上次攻击时间 — 防御性兜底, 防止上层节流全部失效时玩家被连击
		const double Now = FPlatformTime::Seconds();
		const double TimeSinceLastAttack = Now - LastAIAttackTimeSeconds;
		const float SafeInterval = FMath::Max(AttackInterval, 0.1f); // 至少 100ms 间隔, 防意外

		if (LastAIAttackTimeSeconds > 0.0 && TimeSinceLastAttack < SafeInterval)
		{
			UE_LOG(LogTemp, Verbose,
				TEXT("[BaseCharacter] OnAIRequestAttack_Simple: AI=%s 距离上次攻击仅 %.2fs (< 间隔 %.2fs), 跳过"),
				*GetName(), TimeSinceLastAttack, SafeInterval);
			return false;
		}

		// 记录本次攻击时间戳
		LastAIAttackTimeSeconds = Now;
	}

	// ============================================================
	// 防御 1: 死了不能打
	// ============================================================
	if (IsDead())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[BaseCharacter] OnAIRequestAttack_Simple: AI=%s 已死亡, 跳过"),
			*GetName());
		return false;
	}

	// ============================================================
	// 防御 2: 没武器不能打
	// ============================================================
	if (!CurrentWeapon)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[BaseCharacter] OnAIRequestAttack_Simple: AI=%s 无 CurrentWeapon, 跳过"),
			*GetName());
		return false;
	}

	// ============================================================
	// 【核心】走 Resolver 职责链 — 不再硬调 GetAttackMontage(false, 1)
	// ============================================================
	// 设计要点:
	//   - ComboIndex=1 对应玩家 Combo1 段 (玩家连击序列的第 1 段)
	//   - 如果 BP 没配 Combo1 (只有 Combo1 之前的预备段), 解析器会兜底
	//   - 兜底链: ComboIndex=1 → ComboIndex=0 → 重击代替 → 失败
	// ============================================================
	const FAIAttackMontageResult ResolveResult =
		UAIAttackMontageResolver::ResolveLightAttackMontage(CurrentWeapon, /*ComboIndex=*/1);

	if (!ResolveResult.Montage)
	{
		// Resolver 内部已经打 Error 日志 + 修复指南, 这里只需要汇总结果
		UE_LOG(LogTemp, Error,
			TEXT("[BaseCharacter] OnAIRequestAttack_Simple: AI=%s 无可用攻击蒙太奇 (Level=%d), 放弃本次攻击"),
			*GetName(), ResolveResult.FallbackLevel);
		return false;
	}

	// ============================================================
	// 【兜底成功日志】记录是否走了兜底链 (用于排查 BP 配置问题)
	// ============================================================
	if (ResolveResult.bFromFallback)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[BaseCharacter] OnAIRequestAttack_Simple: AI=%s 走了兜底链 Level=%d (Index=%d), "
				 "BP 配置可能有问题, 请检查 LightAttackMontages 数组"),
			*GetName(), ResolveResult.FallbackLevel, ResolveResult.ResolvedIndex);
	}

	// ============================================================
	// 播放蒙太奇 (AI 不需要连击, 只播第一段 Combo1)
	// ============================================================
	UAnimMontage* AttackMontage = ResolveResult.Montage;
	const float MontageLen = PlayAnimMontage(AttackMontage, 1.0f, FName("Combo1"));
	if (MontageLen <= 0.0f)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[BaseCharacter] OnAIRequestAttack_Simple: PlayAnimMontage 失败 (返回 %.2f), "
				 "AI=%s, 武器=%s"),
			MontageLen, *GetName(), *CurrentWeapon->GetName());
		return false;
	}

	// ============================================================
	// 【P0 2026.07.06 大厂架构重构】绑定 Montage 结束事件 (事件驱动 Cooldown)
	//
	// 旧设计问题:
	//   - BT 用 Timer 控制 Cooldown (固定时间)
	//   - 蒙太奇还没播完 Timer 就到时 → AI 进入下一次攻击
	//   - 结果: Combo1 永远播不完, 攻击动作割裂
	//
	// 新设计 (事件驱动):
	//   - 绑定 UAnimInstance::OnMontageEnded (multicast dynamic delegate)
	//   - Combo1 自然播完 → 引擎回调 → OnAIAttackMontageEnded(false)
	//   - OnAIAttackMontageEnded 通知 AIController 攻击结束 → BT 进入冷却
	//   - 即使玩家跑远, 蒙太奇也完整播完 (用户明确要求!)
	//
	// 安全:
	//   - 用 UAnimInstance::OnMontageEnded.AddDynamic (multicast), 不需要缓存本地委托
	//   - 该 multicast 委托原生支持多次回调 (每次播放蒙太奇都会触发, 我们用 Montage 参数过滤)
	//   - 蒙太奇指针验证避免误处理其他蒙太奇 (跨武器复用 Character 时安全)
	// ============================================================
	if (USkeletalMeshComponent* CharMesh = GetMesh())
	{
		if (UAnimInstance* AnimInst = CharMesh->GetAnimInstance())
		{
			// 【关键】直接绑到 UAnimInstance 的 multicast OnMontageEnded
			// 不用 FOnMontageEnded (单播), 不用缓存 delegate, UHT 也看不到这种类型
			// 引擎自身实现: 每次 Montage_Play 都会触发 OnMontageEnded 广播
			// 我们的回调函数 OnAIAttackMontageEnded 必须有 UFUNCTION() 才能 AddDynamic
			//
			// ============================================================
			// 【P0 2026.07.07 大厂架构修复】防 Ensure 崩溃 — RemoveDynamic + IsAlreadyBound
			//
			// PIE 日志 (2026.07.07-09.05) 复现:
			//   Ensure condition failed: InvocationList[ CurFunctionIndex ] != InDelegate
			//   in OnMontageEnded.AddDynamic → BaseCharacter.cpp:1542
			//
			// 原因: UE 的 TMulticastScriptDelegate::AddInternal 有 ensure:
			//   - AddDynamic (同一对象 + 同一函数指针) 不能重复添加
			//   - 即便先 RemoveDynamic 再 AddDynamic 也偶尔因 Inst 指针变化触发
			//   - 我们的 OnAIRequestAttack_Simple 会被 BT 多次调用, 每次都走到这里
			//
			// 修复:
			//   1. RemoveDynamic 先解绑 (幂等: 不存在时跳过, 无副作用)
			//   2. IsAlreadyBound 检查 (双保险)
			//   3. bIsWaitingForAIMontageCallback 标志防 OnAIAttackMontageEnded 二次处理
			// ============================================================

			// 步骤 1: 先解绑 (RemoveDynamic 是幂等的 — 不存在的 binding 静默跳过)
			AnimInst->OnMontageEnded.RemoveDynamic(this, &ABaseCharacter::OnAIAttackMontageEnded);

			// 步骤 2: 再绑定 (IsAlreadyBound 检查 — 双保险, 防止 Inst 内部状态异常)
			if (!AnimInst->OnMontageEnded.IsAlreadyBound(this, &ABaseCharacter::OnAIAttackMontageEnded))
			{
				AnimInst->OnMontageEnded.AddDynamic(this, &ABaseCharacter::OnAIAttackMontageEnded);
			}

			// 缓存正在播放的攻击蒙太奇 — 用于回调时判断是不是"我触发的蒙太奇"
			// (multicast OnMontageEnded 会对所有结束的蒙太奇广播, 必须过滤)
			CachedAIMontage = AttackMontage;
			bIsWaitingForAIMontageCallback = true;
		}
	}

	// ============================================================
	// 同步给服务器 (Multicast_PlayAttackAnim 走 GetAttackMontage 旧路径)
	// 注: 这里不传 Montage 指针, 沿用 ComboIndex 让服务器侧也走相同 fallback
	// ============================================================
	Server_PlayAttackAnim(false, 1);

	// ============================================================
	// 【P0 2026.07.07 大厂架构重构 — 最终版】攻击扣血入口
	//
	// 用户原话: "ai的攻击扣血时机, 应该像像玩家一样用武器上的射线检测触碰扣血的,
	//          而不是动画一开始就扣血。"
	//
	// 旧版问题 (2026.07.06):
	//   OnAIRequestAttack_Simple 在动画开始**第一帧**就调 Server_ReportHit
	//   → AI 抬手瞬间, 玩家已经掉血 → 视觉/物理不一致
	//
	// 新版架构 (玩家/AI 完全共用 trace 路径):
	//   1. SetAttackerIsAI(true) — 让武器 Tick 在 trace 命中时知道这是 AI 攻击
	//      (蒙太奇播完后在 OnAIAttackMontageEnded 复位 false)
	//   2. CurrentWeapon->StartWeaponTrace(false) — **立即开启 trace 通道**
	//      (玩家路径靠蒙太奇里的 AnimNotify 触发, AI 蒙太奇不一定有 AnimNotify, 必须主动开)
	//   3. 蒙太奇播完后在 OnAIAttackMontageEnded:
	//      - SetAttackerIsAI(false)
	//      - CurrentWeapon->StopWeaponTrace() (对称关闭, 防残留)
	//
	// 命中流程:
	//   - BaseWeapon::Tick 每帧 BoxTrace → 命中
	//   - 调用 Server_ReportHit(bIsAIDriven=true)
	//   - 服务器读 Owner Character 的 bIsCurrentlyAttackerAI → 走 AI 通道
	//   - 服务器权威读 AIController->GetEffectiveAttackDamage() → ConfigSO.Damage
	//   - ApplyPointDamage 扣血
	//
	// 关键: 玩家和 AI **完全共用同一套 Server_ReportHit 代码路径**
	//       只在 bIsAIDriven 决定伤害来源, 单一职责
	// ============================================================
	SetAttackerIsAI(true);

	// 主动开启 trace 通道 — 这是大厂关键设计:
	//   玩家靠蒙太奇 AnimNotify, AI 蒙太奇经常复用同一蒙太奇 (如果有 AnimNotify 就更好)
	//   但旧版项目里 AI 用的蒙太奇可能没 StartWeaponTrace AnimNotify
	//   这里 AI 主动开启 trace: 万一 AnimNotify 也开了, 第二次只是复位 IgnoreActors (无害)
	//   这样: 不管 AI 蒙太奇有没有 AnimNotify, trace 都开, 玩家也照常工作
	if (HasAuthority() && CurrentWeapon)
	{
		CurrentWeapon->StartWeaponTrace(false);
	}

	UE_LOG(LogTemp, Log,
		TEXT("[BaseCharacter] OnAIRequestAttack_Simple: AI=%s 成功播放攻击动画 "
			 "(Level=%d, Index=%d, Montage=%s, Length=%.2fs, 扣血等待 trace 命中)"),
		*GetName(),
		ResolveResult.FallbackLevel,
		ResolveResult.ResolvedIndex,
		*AttackMontage->GetName(),
		MontageLen);
	return true;
}


// ==========================================
// 【P0 2026.07.06 大厂架构重构】AI 攻击蒙太奇结束回调
// ==========================================

/**
 * OnAIAttackMontageEnded — AI 攻击蒙太奇自然/被打断结束时由引擎回调
 *
 * 【核心职责】通知 AIController 攻击阶段结束, 解除 bIsCurrentlyAttacking
 *
 * 触发时机:
 *   - Combo1 段自然播完 (bInterrupted=false) → AI 进入冷却
 *   - 蒙太奇被 StopAllMontages 打断 (bInterrupted=true) → 立即解锁
 *
 * 设计: 通知 AIController 让 C++ 状态机与 BT 状态机同步
 *   - 玩家路径: 动画 BP NotifyEnd → EndAttackState (类似机制)
 *   - AI 路径:   引擎 Montage 结束 → OnAIAttackMontageEnded → AIController 解锁
 *   - 解耦: AI 不依赖动画 BP 配置 (避免 NotifyEnd 漏配导致永远 lock)
 *
 * 【签名】UAnimInstance::OnMontageEnded (Dynamic Multicast) 要求 (UAnimMontage*, bool) + UFUNCTION
 *        与 Montage_SetEndDelegate 接口完全对齐
 */
void ABaseCharacter::OnAIAttackMontageEnded(UAnimMontage* Montage, bool bInterrupted)
{
	// ============================================================
	// 防御: Montage 参数验证 — 忽略不匹配的蒙太奇 (防误触发)
	// ============================================================
	// UAnimInstance::OnMontageEnded 是 multicast, 会对所有结束的蒙太奇广播
	// 我们只处理自己 CachedAIMontage (本次攻击触发的) 的结束事件
	if (Montage != CachedAIMontage)
	{
		return; // 不是我们这次触发的, 忽略 (例如: 玩家 AI 切换武器/受伤/死亡导致别的蒙太奇结束)
	}

	if (!bIsWaitingForAIMontageCallback)
	{
		// 不在等回调 (防御: 收到二次回调, 第一次已处理)
		return;
	}

	AAIController* AIC = Cast<AAIController>(GetController());
	ABaseAIController* BaseAIC = Cast<ABaseAIController>(AIC);

	if (!BaseAIC)
	{
		// 没 AIController (可能是玩家), 不处理 AI 状态
		return;
	}

	// 核心: 通知 AIController 攻击阶段结束
	//   1. C++ 层: bIsCurrentlyAttacking=false + bIsInAttackCooldown=false
	//      - bIsCurrentlyAttacking: BT 装饰器 DistanceCheck 看到 false → 重新选 Sequence
	//      - bIsInAttackCooldown: 退出 LockAtAttackRange 模式, BT 恢复正常决策
	//   2. BT 层: Cooldown 机制自行处理
	//      - 自然结束 (bInterrupted=false): Cooldown 在 BT 端计时
	//      - 被打断 (bInterrupted=true): 立即解锁, BT 重新评估
	// ============================================================
	BaseAIC->SetCurrentlyAttacking(false);
	// 【P0 2026.07.07 v10 大厂架构重构】Cooldown 在蒙太奇结束时**不**清零!
	//
	// 用户原话 2026.07.07 18:25 反馈:
	//   "现在AI好像是在下一次攻击开始时才判断AttackRange设定的距离移动的"
	//   "应该在进入攻击冷却时, 只要检测到敌人移动, 就根据AttackRange设定距离移动, 直到攻击冷却结束"
	//
	// 旧 v9 设计 Bug:
	//   - 蒙太奇结束 → OnAIAttackMontageEnded → bIsInAttackCooldown=false
	//   - 此时 Cooldown Timer 还在跑 (e.g. 还剩 0.8s)
	//   - AI 进入 P4 状态, D<R 立即 LockStop, 玩家移动 AI 不会反应
	//   - 只有下次 PerformAttack 重新触发 → 又进入 P1 (Cooldown) 状态 → 才重新判断距离
	//   - 用户感知: "AI 在下一次攻击开始时才判断 AttackRange 移动"
	//
	// v10 修复 — 对称职责分离:
	//   - bIsCurrentlyAttacking=false: 蒙太奇已结束, 不再播放攻击 (这里清)
	//   - bIsInAttackCooldown: 由 BTTask::ConsumeAttackToken 的 Timer 回调负责清零
	//     这是 P0 修复 "冷却外期间 AI 不动" 的核心 — Cooldown 状态机的生命周期
	//     严格对齐 ConfigSO.AttackCooldown 秒数, 与 BT 决策周期完全同步
	//
	// 多退出路径对称:
	//   1. 蒙太奇自然播完 → 本路径 (bIsInAttackCooldown 由 Timer 清)
	//   2. 蒙太奇被打断 → bInterrupted=true → 立即清 (下面 else 分支)
	//   3. AI 死亡/销毁 → EndPlay → AIController 解 Possess 自动清理
	//   4. BT 中断 → BTTask::AbortTask → 立即清

	bool bExpectCooldownEnd = !bInterrupted;

	// 异常路径: 蒙太奇被打断 (玩家 AI 切换武器/受伤/死亡导致 StopAllMontages)
	//   - Timer 不会跑完, 必须立即清 Cooldown, 否则 AI 永远卡在冷却中
	//   - 这是"对称职责分离"的关键边界
	if (bInterrupted)
	{
		BaseAIC->SetInAttackCooldown(false);
		bExpectCooldownEnd = false; // 已立即清, Timer 再触发时是 no-op
	}

	UE_LOG(LogTemp, Log,
		TEXT("[BaseCharacter] OnAIAttackMontageEnded: AI=%s 蒙太奇结束 (Interrupted=%s, ExpectCooldownEnd=%s), "
			 "SetCurrentlyAttacking=false; Cooldown %s"),
		*GetName(),
		bInterrupted ? TEXT("true") : TEXT("false"),
		bExpectCooldownEnd ? TEXT("true") : TEXT("false"),
		bInterrupted ? TEXT("已立即清 (打断分支)") : TEXT("保留, 由 Timer 自然清 (正常分支)"));

	// 清理缓存 (避免下次攻击时复用旧状态)
	bIsWaitingForAIMontageCallback = false;
	CachedAIMontage = nullptr;

	// ============================================================
	// 【P0 2026.07.07 大厂架构重构】还原攻击者标志 + 停止 trace
	//
	// 这是 OnAIRequestAttack_Simple SetAttackerIsAI(true) + StartWeaponTrace() 的对称关闭
	// 必须在蒙太奇自然结束 / 被打断 / AI 死亡时无条件关闭, 否则:
	//   1. bIsCurrentlyAttackerAI 残留为 true
	//      → 下次 AI 攻击 trace 命中, 服务器走 AI 通道读 ConfigSO (没坏, 但状态错乱)
	//      → 更危险: 玩家路径的轻击 trace 命中时也走 AI 通道 (读 ConfigSO.Damage 而非 LightDamageBody)
	//   2. bIsWeaponActive 残留为 true → trace 永远激活, 任何人接近都扣血
	//
	// 单一职责: 攻击者标志由攻击发起方管理, 武器 Tick 只读不写
	// ============================================================
	SetAttackerIsAI(false);

	if (HasAuthority() && CurrentWeapon)
	{
		// 对称关闭 trace — AI 主动开启 trace 的对应关闭
		// (即便 BP 蓝图 AnimNotify 也会调 StopWeaponTrace, 这里再调一次幂等无害)
		CurrentWeapon->StopWeaponTrace();
	}

	UE_LOG(LogTemp, Verbose,
		TEXT("[BaseCharacter] OnAIAttackMontageEnded: 已还原攻击者标志 (Player 路径) + 关闭 trace, AI=%s"),
		*GetName());
}




// ==========================================================
// 14. RPC 动画广播实现
// ==========================================================
// ==========================================

/**
 * Server_PlayAttackAnim_Implementation
 *
 * Server RPC 实现: 客户端向服务器请求挥刀
 * 1. 服务器端也必须同步锁死速度
 * 2. 服务器收到请求后，向全频道广播
 */
void ABaseCharacter::Server_PlayAttackAnim_Implementation(bool bIsHeavy, int32 InComboIndex)
{
	// 服务器端也必须同步锁死速度
	// 这样服务器和客户端的物理推演就完全一致了，再也不会发生"强行拽人"的瞬移
	if (CurrentWeapon && !CurrentWeapon->bCanMoveWhileLightAttack)
	{
		bIsMovementLocked = true;
		GetCharacterMovement()->MaxWalkSpeed = 0.0f;
	}

	// 服务器收到请求后，直接向全频道广播
	Multicast_PlayAttackAnim(bIsHeavy, InComboIndex);
}


/**
 * Multicast_PlayAttackAnim_Implementation
 *
 * NetMulticast 实现: 服务器向所有客户端广播
 * 发起攻击的本地玩家自己已经播过动画了，防鬼畜直接跳过
 */
void ABaseCharacter::Multicast_PlayAttackAnim_Implementation(bool bIsHeavy, int32 InComboIndex)
{
	// 发起攻击的本地玩家自己已经播过动画了，防鬼畜直接跳过
	if (IsLocallyControlled()) return;

	if (CurrentWeapon)
	{
		// 工业级做法: 直接拿第 0 个蒙太奇（因为所有轻击招式都在这里面）
		UAnimMontage* MontageToPlay = CurrentWeapon->GetAttackMontage(bIsHeavy, 0);
		if (MontageToPlay)
		{
			// 根据服务器传来的 InComboIndex，智能推断该播哪个片段
			FName SectionName = (InComboIndex == 1) ? FName("Combo1") : FName("Combo2");

			// 让其他玩家屏幕上的你，也精准跳到对应的连招片段
			PlayAnimMontage(MontageToPlay, 1.0f, SectionName);
		}
	}
}


// ==========================================
// 【P0 大厂架构 2026.07.06】AI 攻击伤害上报 (备用通道)
// ==========================================
//
// 【设计说明】当前版本 AI 攻击伤害走 BaseWeapon::Server_ReportHit 统一入口
//            (OnAIRequestAttack_Simple 调用 CurrentWeapon->Server_ReportHit(AIDamage))
//            本 RPC 保留为"备选实现", 如果未来有非武器 AI (例如召唤物、远程法术) 时使用
//
// 注意: 不要在当前 OnAIRequestAttack_Simple 流程里调它, 否则会重复扣血!
//
// 数据流 (历史实现, 现已弃用但保留):
//   AIBehaviorConfigSO.Combat.Damage
//     ↓ UAIRuntimeConfigComponent::GetScaledCombat
//     ↓ ABaseCharacter::OnAIRequestAttack_Simple (读 CombatParam, 上报)
//     ↓ Server_ReportAIAttackHit (本函数, 服务器权威, 备用)
//     ↓ UGameplayStatics::ApplyPointDamage
//     ↓ HealthComponent::ApplyDamage

void ABaseCharacter::Server_ReportAIAttackHit_Implementation(AActor* HitActor, float Damage)
{
	// 防御 1: HitActor 必须有效
	if (!HitActor)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[BaseCharacter] Server_ReportAIAttackHit: AI=%s 收到空 HitActor, 忽略"),
			*GetName());
		return;
	}

	// 防御 2: 目标必须是 ABaseCharacter
	ABaseCharacter* Victim = Cast<ABaseCharacter>(HitActor);
	if (!Victim)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[BaseCharacter] Server_ReportAIAttackHit: AI=%s -> HitActor=%s 不是 ABaseCharacter, 忽略"),
			*GetName(), *HitActor->GetName());
		return;
	}

	// 防御 3: 目标已死则不扣 (避免重复扣血)
	if (Victim->IsDead())
	{
		return;
	}

	// 防御 4: 自伤防御 (AI 不会打自己)
	if (Victim == this)
	{
		return;
	}

	// 防御 5: 同阵营不互砍 (走 IGenericTeamAttitude Towards)
	// 设计: 阵营协议自动判断, 这里不重复造轮子
	//       但 Pawn 链可能没 Set 好阵营, 暂时跳过阵营检查 (下个 Phase 加)
	const float FinalDamage = FMath::Max(Damage, 0.0f);

	// 走 UGameplayStatics::ApplyPointDamage 走标准伤害事件链
	// 注意: 用 Victim->GetActorLocation() 当 HitLocation 是简化, 实际应该用武器刀刃 Trace
	//       现阶段 AI 攻击没有 Trace 接入, 用"AI 当前世界坐标"近似, 视觉效果无差 (伤害无距离衰减)
	FHitResult HitInfo(Victim, Victim->GetMesh(), GetActorLocation(), -GetActorForwardVector());

	UGameplayStatics::ApplyPointDamage(
		Victim,
		FinalDamage,
		-GetActorForwardVector(),  // 攻击来向 (从 AI 指向玩家)
		HitInfo,
		GetController(),            // Instigator 是 AI 的 Controller
		this,                       // DamageCauser 是 AI Character
		UDamageType::StaticClass()
	);

	UE_LOG(LogTemp, Log,
		TEXT("[BaseCharacter] Server_ReportAIAttackHit: AI=%s -> Victim=%s, Damage=%.1f (由 ConfigSO.Damage 决定)"),
		*GetName(), *Victim->GetName(), FinalDamage);
}


/**
 * Server_ReportAIAttackHit_Validate
 * 校验: HitActor 非空, Damage 在合法范围 (0~10000)
 */
bool ABaseCharacter::Server_ReportAIAttackHit_Validate(AActor* HitActor, float Damage)
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


// ==========================================
// 15. 装备武器
// ==========================================

/**
 * EquipWeapon
 *
 * 供上帝 (GameMode) 调用的装备武器接口
 * 1. 只有服务器上帝有资格发枪
 * 2. 如果手里已经有武器了，先销毁旧的
 * 3. 在世界中凭空召唤出这把武器
 * 4. 焊接到角色的插槽上
 */
void ABaseCharacter::EquipWeapon(TSubclassOf<ABaseWeapon> WeaponClassToEquip)
{
	// 只有服务器上帝有资格发枪
	if (HasAuthority() && WeaponClassToEquip)
	{
		// 如果手里已经有武器了，先销毁旧的（为以后切枪做准备）
		if (CurrentWeapon)
		{
			CurrentWeapon->Destroy();
		}

		FActorSpawnParameters SpawnParams;
		SpawnParams.Owner = this; // 武器的主人是我
		SpawnParams.SpawnCollisionHandlingOverride = ESpawnActorCollisionHandlingMethod::AlwaysSpawn;

		// 在世界中凭空召唤出这把武器
		CurrentWeapon = GetWorld()->SpawnActor<ABaseWeapon>(WeaponClassToEquip, GetActorLocation(), GetActorRotation(), SpawnParams);

		if (CurrentWeapon)
		{
			// 【第三人称关键】: 将武器死死地焊在第三人称全身模型 (GetMesh) 的 "WeaponSocket" 插槽上
			CurrentWeapon->AttachToComponent(GetMesh(), FAttachmentTransformRules::SnapToTargetNotIncludingScale, FName("WeaponSocket_L"));
		}
	}
}

/**
 * ABaseCharacter::RequestWeaponSpawn
 *
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
void ABaseCharacter::RequestWeaponSpawn(FString WeaponID)
{
	UE_LOG(LogTemp, Log, TEXT("[BaseCharacter] RequestWeaponSpawn: WeaponID=%s, Pawn=%s"),
		*WeaponID, *GetName());

	// 直接调内部 protected 方法, 复用完整链路
	SpawnAndEquipWeapon(WeaponID);
}


// ==========================================
// 16. 受伤与死亡
// ==========================================

/**
 * TakeDamage
 *
 * UE 原生函数: 处理伤害（仅服务器）
 * 1. 安全扣血
 * 2. 判定生死
 * 3. 服务器处理击杀结算（查找击杀方式、设置击杀者、通知助攻、广播计分板）
 */
float ABaseCharacter::TakeDamage(float DamageAmount, struct FDamageEvent const& DamageEvent, class AController* EventInstigator, AActor* DamageCauser)
{
	// 1. 如果已经死了，或者客户端没有权限，直接退出
	// 【2026-06-15 重构】: bIsDead 字段已下沉,改调 IsDead()
	if (IsDead() || !HasAuthority()) return 0.0f;

	// 先执行父类的逻辑
	float ActualDamage = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);

	// 2. 【2026-06-15 重构】安全扣血改为调 HealthComponent
	const float ActualApplied = HealthComponent ? HealthComponent->ApplyDamage(ActualDamage) : 0.0f;
	UE_LOG(LogTemp, Warning, TEXT("[Health] 服务器扣血完成: Damage=%.1f, NewHealth=%.1f/%.1f, Dead=%d, Auth=%d"),
		ActualApplied, GetCurrentHealth(), GetMaxHealth(), IsDead(), HasAuthority());

	// 3. 【2026-07-01 P0 新增】通知回血组件: 被打断了, 必须重新等 RegenerationDelay 才能回血
	// 即使默认 HealthRegenRate=0, 仍调用一次以重置内部状态 (防止未来开启回血时残留计时)
	if (HasAuthority() && HealthRegenComponent)
	{
		HealthRegenComponent->NotifyDamageTaken();
	}

	// 4. 判定生死
	// 【2026-07-01 重构】: 死亡流程不再由 TakeDamage 主动调 Die() 触发
	//   旧: TakeDamage → Die() → Multicast_Die  (依赖 TakeDamage 调用路径)
	//   新: TakeDamage → HealthComponent::ApplyDamage → OnDeath.Broadcast
	//       → BaseCharacter::OnHealthComponentDeath (BeginPlay 已订阅) → Die()
	//   优势: 任何修改 HealthComponent 的路径都会触发, 不再依赖 TakeDamage
	if (IsDead())
	{
		// 服务器处理击杀结算 (仅在这里, 不在 Die() 内, 避免事件驱动路径重复触发)
		if (HasAuthority())
		{
			// 获取击杀方式（从武器获取）
			EKillMethod KillMethod = EKillMethod::MeleeWeapon; // 默认近战
			if (ABaseWeapon* Weapon = Cast<ABaseWeapon>(DamageCauser))
			{
				KillMethod = Weapon->GetLastKillMethod();
			}

			// 获取伤害发起者（击杀者）
			AController* DamageInstigatorController = nullptr;
			if (EventInstigator)
			{
				DamageInstigatorController = EventInstigator;
			}

			// 设置击杀者的击杀方式
			if (DamageInstigatorController)
			{
				ABaseCharacter* KillerCharacter = Cast<ABaseCharacter>(DamageInstigatorController->GetPawn());
				if (KillerCharacter)
				{
					KillerCharacter->LastKillMethod = KillMethod;

					// 查找在最近5秒内攻击过受害者的所有玩家
					GrantAssistsToEligiblePlayers(this, KillerCharacter);

					// 通知所有客户端更新计分板和击杀信息
					KillerCharacter->Multicast_NotifyKill(this, nullptr);
				}
				else
				{
					// 如果找不到击杀者角色，直接通知死亡（这种情况一般不会发生）
					Multicast_NotifyKill(this, nullptr);
				}
			}
			else
			{
				// 没有击杀者控制器的情况
				Multicast_NotifyKill(this, nullptr);
			}
		}

		// 【移除 Die()】死亡流程已由 HealthComponent::OnDeath 事件驱动:
		//   ApplyDamage 内部 OnDeath.Broadcast → OnHealthComponentDeath → Die()
		//   客户端通过 OnRep_bIsDead → OnDeath.Broadcast → OnHealthComponentDeath → ExecuteDeathLocal
	}

	return ActualDamage;
}


/**
 * Die — 统一死亡入口 (玩家 & AI 共用)
 *
 * 【大厂架构重构 2026.07.10 - P0 关键修复 v2】
 *
 * 设计原则:
 *   - Die() 负责: 服务器校验 + 武器清理 + 派发复活请求 + 广播死亡 + 销毁 Pawn
 *   - 顺序: 先派发复活 (因为 GetController() 在 Pawn 销毁后可能返回 nullptr)
 *   - 复活逻辑完全委托给 ARoomGameMode::RequestRespawn
 *
 * 【P0 2026.07.10 v2 关键修复】
 *
 * 历史 bug (v1):
 *   Die() 先销毁 Pawn, 再调用 RequestRespawn(GetController())
 *   Pawn 销毁后, Controller.GetPawn() == nullptr, Controller 可能失效
 *   → RequestRespawn 没有被调用
 *   → AI 永远不复活
 *
 * 新实现:
 *   1. 武器清理: StopWeaponTrace + SetAttackerIsAI(false)
 *   2. 【先派发复活】在销毁 Pawn 前调用 RequestRespawn
 *   3. 广播死亡: Multicast_Die (布娃娃等视觉效果)
 *   4. 销毁 Pawn: this->Destroy()
 *
 * 调用链:
 *   HealthComponent::ApplyDamage -> TakeDamage -> Die()
 *   Die() -> RequestRespawn (先派发, 避免 Pawn 销毁后 Controller 失效)
 *        -> Multicast_Die (广播死亡)
 *        -> Destroy Pawn
 */
void ABaseCharacter::Die()
{
	// 服务器权威校验
	if (!HasAuthority()) return;

	UE_LOG(LogTemp, Warning,
		TEXT("[BaseCharacter][Die] 执行死亡流程: Pawn=%s, HasAuthority=%d"),
		*GetName(), HasAuthority() ? 1 : 0);

	// 【P0 2026.07.10 武器清理】必须在 Pawn 销毁前
	if (CurrentWeapon)
	{
		SetAttackerIsAI(false);
		CurrentWeapon->StopWeaponTrace();
	}

	// 【P0 2026.07.10 v2 关键修复】先派发复活请求
	// 原因: Pawn 销毁后, Controller 的 Pawn 引用失效, GetController() 可能返回 nullptr
	//       RequestRespawn 需要从 Controller 获取 Profile, 必须在 Pawn 销毁前调用
	// 引用方式: 先保存 Controller 指针 (Pawn 销毁后指针仍有效, 只是 GetPawn() 返回 nullptr)
	if (AController* MyController = GetController())
	{
		if (ARoomGameMode* GM = Cast<ARoomGameMode>(GetWorld()->GetAuthGameMode()))
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[BaseCharacter][Die] 派发复活请求: Controller=%s (Pawn=%s 即将销毁)"),
				*MyController->GetName(), *GetName());
			GM->RequestRespawn(MyController, false);
		}
		else
		{
			UE_LOG(LogTemp, Error,
				TEXT("[BaseCharacter][Die] Cannot find ARoomGameMode, skipping respawn for Pawn=%s"),
				*GetName());
		}
	}
	else
	{
		UE_LOG(LogTemp, Error,
			TEXT("[BaseCharacter][Die] Controller 已为空, 无法派发复活请求! Pawn=%s"),
			*GetName());
	}

	// 【P0 2026.07.10 v3】广播死亡视觉效果给客户端
	// ExecuteDeathLocal 处理: 武器掉落/溶解, 胶囊体透明, 死亡动画, 布娃娃
	// 注意: 服务器端也调用, 但服务器即将销毁 Pawn, 所以实际上不会有视觉效果
	ExecuteDeathLocal();

	// 【P0 2026.07.10 v2/v3】销毁 Pawn
	// RequestRespawn 已在上面派发, 现在销毁 Pawn 是安全的
	// 旧武器随 Pawn 一起销毁, 不会再攻击玩家
	UE_LOG(LogTemp, Warning,
		TEXT("[BaseCharacter][Die] 销毁 Pawn: %s"),
		*GetName());
	Destroy(); // ABaseCharacter 继承自 AActor, Destroy() 是 AActor 的方法
}


/**
 * Multicast_Die_Implementation
 *
 * NetMulticast 实现: 死亡流程在所有机器上执行
 *
 * 【2026-07-01 架构升级】本函数已被简化为客户端兜底通道
 *   服务器流程: HealthComponent::ApplyDamage → OnDeath.Broadcast → OnHealthComponentDeath → Die() → Multicast_Die (兜底) → 服务器本地 ExecuteDeathLocal
 *   客户端流程: HealthComponent::OnRep_bIsDead → OnDeath.Broadcast → OnHealthComponentDeath → ExecuteDeathLocal()
 *   本 RPC 同时作为:
 *     1. 服务器死亡事件传播给客户端的主通道 (即便 bIsDead Replicated 延迟, 客户端也能立刻收到)
 *     2. 服务器本地兜底 (防止服务器侧 OnHealthComponentDeath 事件未订阅时仍能进入死亡流程)
 */
void ABaseCharacter::Multicast_Die_Implementation()
{
	// 服务器专属: 重置 AC/ACE
	if (HasAuthority())
	{
		ResetAC();
	}

	// 执行本地死亡流程 (幂等, CurrentWeapon 已被 nullptr 后再次进入会被 if 短路)
	ExecuteDeathLocal();

	// 复活定时器仅服务器需要
	if (HasAuthority())
	{
		if (AController* MyController = GetController())
		{
			if (ARoomPlayerController* PC = Cast<ARoomPlayerController>(MyController))
			{
				PC->StartRespawnTimer(RespawnDelaySeconds);
			}
		}
	}
}


/**
 * DropAndFadeWeapon
 *
 * 【大厂 P0 2026.07.10 重构】武器死亡处理
 *
 * 历史 bug (v22 及以前):
 *   1. 服务器和客户端都调度 1 秒后 Destroy 同一把武器 (服务器先 Destroy, 客户端 timer 1 秒后 IsValid=false → 静默 no-op, 但语义混乱)
 *   2. 1 秒延迟太短, 玩家根本看不到武器掉地的物理动画
 *   3. 武器的溶解特效永远不生效, 因为 DissolveComponent::GetOwnerWeaponMesh() 在已 Detach 的 Owner 上用 FindComponentByClass 找不到
 *
 * 历史修复 (v23):
 *   调 DissolveComponent->CollectWeaponDynamicMaterials(WeaponMesh) 把武器的 MID 加到角色 DissolveComponent 数组
 *   → 责任错位 (角色的组件驱动武器的材质)
 *   → 跨边界引用 (武器 Detach 后, 角色 Component 还 hold 武器 MID, 销毁时序混乱)
 *   → 武器材质蓝图没调用 MF_Dissolve → 武器依然不溶解
 *
 * 新实现 (2026.07.10 大厂架构 — 职责对等):
 *   - 服务器: SetLifeSpan(3.0f) - 让 UE 在 3 秒后自动销毁, 单一权威
 *   - 所有机器: 立刻给武器 Mesh 启物理 + 取消 Attach + 调 Weapon->StartDissolve()
 *   - 武器自治: 武器自己持有 WeaponDissolveComponent, 管自己的溶解
 *   - 角色不再穿透调用, 零跨边界引用
 *   - 武器材质蓝图必须调用 MF_Dissolve (单一协议, 与身体完全一致)
 */
void ABaseCharacter::DropAndFadeWeapon(ABaseWeapon* Weapon)
{
	if (!IsValid(Weapon))
	{
		return;
	}

	UE_LOG(LogTemp, Log,
		TEXT("[BaseCharacter] DropAndFadeWeapon: %s (Auth=%d)"),
		*Weapon->GetName(), HasAuthority() ? 1 : 0);

	// 1. 取消武器与角色的骨骼绑定
	//    KeepWorldTransform: 武器保持在世界中的当前位置, 不会跳到原点
	Weapon->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);

	// 2. 激活武器 Mesh 物理模拟 + 碰撞, 让它掉地上
	UMeshComponent* WeaponMesh = Weapon->FindComponentByClass<UMeshComponent>();
	if (WeaponMesh)
	{
		WeaponMesh->SetCollisionEnabled(ECollisionEnabled::PhysicsOnly);
		WeaponMesh->SetCollisionResponseToAllChannels(ECR_Block);
		WeaponMesh->SetSimulatePhysics(true);
		WeaponMesh->WakeAllRigidBodies();
		WeaponMesh->SetEnableGravity(true);
	}

	// 3. 【P0 2026.07.10 大厂重构】武器溶解 — 委托给武器自己
	//    老路径: DissolveComponent->CollectWeaponDynamicMaterials(WeaponMesh)  (跨边界)
	//    新路径: Weapon->StartDissolve()  (职责对等, 武器自治)
	//
	//    设计 (类比身体 DissolveComponent):
	//      身体溶解: ABaseCharacter::DissolveComponent 驱动自己的身体 Mesh
	//      武器溶解: ABaseWeapon::WeaponDissolveComponent 驱动自己的武器 Mesh
	//      零跨边界: 武器 Detach 后, 它的 Component 随 Actor 继续工作
	//
	//    协议 (与身体一致 — 零兜底):
	//      武器材质蓝图必须调用 MF_Dissolve 节点 (有 DissolveAmount 参数)
	//      协议不满足时, WeaponDissolveComponent 内部 Log(Warning) 报警
	//      C++ 不静默兜底, 不兼容老材质
	Weapon->StartDissolve();

	// 4. 【P0 修复】统一销毁授权 - 仅服务器调用 SetLifeSpan
	//    客户端通过 Actor 的 OnDestroyed 自动同步移除, 不需要客户端各自销毁
	//    这解决了"服务器销毁后, 客户端的 1 秒 timer 还在跑"的不一致问题
	if (HasAuthority())
	{
		Weapon->SetLifeSpan(WeaponDestroyDelaySeconds);
	}
}

/**
 * EnableRagdoll
 *
 * 启用布娃娃物理（UE 标准 5 步）
 *
 * 之前的 bug: 仅有 StopAnimMontage + SetCollisionProfileName + SetSimulatePhysics,
 *   缺了 3 个关键步骤, 物理模拟被 CharacterMovement 强制覆盖, Mesh 不会真的倒下
 *
 * UE 官方标准流程:
 *   1. 打断所有动画 (避免动画蒙太奇继续驱动骨骼, 与物理冲突)
 *   2. 禁用 CharacterMovement (MOVE_None, 让 Movement Component 不再强制控制 Mesh)
 *   3. 设置 Mesh 碰撞为 Ragdoll profile (项目设置里要预定义)
 *   4. SetAllBodiesSimulatePhysics(true) + bBlendPhysics=true (关键: 让 Mesh 的 root 与胶囊体解耦)
 *   5. WakeAllRigidBodies() (强制所有物理骨骼激活, 否则可能因 sleep 而不响应)
 *
 * @note 物理资产 PhysicsAsset 必须在 BP/资产侧配置正确, 否则骨骼没有碰撞体
 * @note 调用前必须确保胶囊体碰撞已设为 Ignore Pawn (Multicast_Die 已做)
 */
void ABaseCharacter::EnableRagdoll()
{
	USkeletalMeshComponent* MeshComp = GetMesh();
	if (!MeshComp)
	{
		UE_LOG(LogTemp, Error, TEXT("[BaseCharacter] EnableRagdoll: GetMesh() 为空, 无法启用布娃娃"));
		return;
	}

	// 1. 打断所有还在播的动画 (蒙太奇会与物理模拟冲突, 必须停掉)
	StopAnimMontage();

	// 2. 【P0 修复】禁用 CharacterMovement
	// 原因: 角色死亡后 Movement Component 仍在每帧强制 Mesh 跟随 Capsule 移动,
	//       这会**完全覆盖** Mesh 的物理模拟结果, 导致 Mesh 像被钉在 Capsule 上一样不倒下
	//       这是"布娃娃不倒下"bug 的**根因**
	if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
	{
		MoveComp->StopMovementImmediately();        // 立即清零所有速度
		MoveComp->DisableMovement();                 // 禁用所有移动模式
		MoveComp->SetComponentTickEnabled(false);    // 关闭 Movement 的 Tick, 彻底不再干预 Mesh
	}

	// 3. 碰撞 Profile 切换到 Ragdoll (项目设置里要预定义 "Ragdoll" profile)
	MeshComp->SetCollisionProfileName(TEXT("Ragdoll"));

	// 4. 【P0 修复】bBlendPhysics = true
	// 原因: 当 bBlendPhysics 为 false 时, UE 仍然把 Mesh 视作"由动画驱动", 物理只是辅助
	//       必须设为 true 才能让物理完全接管 Mesh 骨骼链
	//       这是 UE Ragdoll 官方教程明确要求的**关键标志**
	MeshComp->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);
	MeshComp->SetAllBodiesSimulatePhysics(true);
	MeshComp->bBlendPhysics = true;

	// 5. 【P0 修复】唤醒所有刚体
	// 原因: 物理骨骼初始可能处于 Sleep 状态, 不会响应重力
	//       强制唤醒确保重力立即作用于所有骨骼
	MeshComp->WakeAllRigidBodies();

	// 6. 【关键】Mesh 不再跟随 Capsule (因为 Movement 已关, 但显式声明避免边缘情况)
	MeshComp->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);

	UE_LOG(LogTemp, Log,
		TEXT("[BaseCharacter] EnableRagdoll: 已启用布娃娃 (Mesh=%s, PhysicsAsset=%s)"),
		*MeshComp->GetName(),
		*GetNameSafe(MeshComp->GetPhysicsAsset()));
}


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
 * ABaseCharacter::SetMeleeProfile
 *
 * 【P0 大厂架构 2026.07.06】Profile 直接注入武器/角色配置
 *
 * 设计: SetSpawnLoadout 只做字段写入, 不承担"来源解析"职责.
 *       本方法负责从 UAIProfileAsset 解析 WeaponID/CharacterRowName,
 *       然后调用 SetSpawnLoadout 写入.
 *
 * 调用方: AMeleeAIController::SetupMeleeAI (关卡预放 AI 自举路径)
 */
void ABaseCharacter::SetMeleeProfile(UAIProfileAsset* InProfile)
{
	// 存 Profile 引用, 后续 PossessedBy 可直接用
	MeleeProfile = InProfile;

	if (!InProfile)
	{
		UE_LOG(LogTemp, Warning, TEXT("[BaseCharacter] SetMeleeProfile: InProfile 为空, 跳过武器/角色配置"));
		return;
	}

	// 从 Profile 解析 WeaponID (FName -> FString)
	FString WeaponIDStr;
	if (!InProfile->WeaponID.IsNone())
	{
		WeaponIDStr = InProfile->WeaponID.ToString();
	}

	// 从 Profile 解析 CharacterRowName (FName -> FString)
	FString CharIDStr;
	if (!InProfile->CharacterRowName.IsNone())
	{
		CharIDStr = InProfile->CharacterRowName.ToString();
	}

	UE_LOG(LogTemp, Log,
		TEXT("[BaseCharacter] SetMeleeProfile: Profile=%s, DesiredCharID='%s', DesiredWeaponID='%s'"),
		*InProfile->GetName(), *CharIDStr, *WeaponIDStr);

	// 调用 SetSpawnLoadout 写入 (新实现: 无论是否为空都写入)
	SetSpawnLoadout(CharIDStr, WeaponIDStr);
}

/**
 * ABaseCharacter::SyncWeaponFromProfile
 *
 * 【P0 大厂架构 2026.07.06】从缓存的 Profile 同步武器/角色数据
 *
 * 调用方: PossessedBy (在读取 SpawnWeaponID 之前调用)
 *
 * 逻辑:
 *   1. 检查 MeleeProfile 是否有效
 *   2. 如果 SpawnWeaponID 为空, 从 Profile.WeaponID 填充
 *   3. 如果 CharacterID 为空, 从 Profile.CharacterRowName 填充
 *   4. 诊断日志记录同步结果
 */
void ABaseCharacter::SyncWeaponFromProfile()
{
	if (!MeleeProfile)
	{
		// 没有缓存的 Profile, 直接返回
		return;
	}

	// 如果 SpawnWeaponID 为空, 从 Profile.WeaponID 填充
	if (SpawnWeaponID.IsEmpty() && !MeleeProfile->WeaponID.IsNone())
	{
		SpawnWeaponID = MeleeProfile->WeaponID.ToString();
		UE_LOG(LogTemp, Log,
			TEXT("[BaseCharacter] SyncWeaponFromProfile: 填充 SpawnWeaponID='%s' from Profile"),
			*SpawnWeaponID);
	}

	// 如果 CharacterID 为空, 从 Profile.CharacterRowName 填充
	if (CharacterID.IsEmpty() && !MeleeProfile->CharacterRowName.IsNone())
	{
		CharacterID = MeleeProfile->CharacterRowName.ToString();
		UE_LOG(LogTemp, Log,
			TEXT("[BaseCharacter] SyncWeaponFromProfile: 填充 CharacterID='%s' from Profile"),
			*CharacterID);
	}
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
	if (HealthRegenComponent)
	{
		HealthRegenComponent->ResetRegenerationState();
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
		FString WeaponID;

		// 【P0 大厂架构 2026.07.06】四路兜底 — 让 AI 也能拿到武器
		//
		// Layer 0 (Profile 路径): MeleeProfile->WeaponID (关卡预放 AI)
		// Layer 1 (AI 路径): Pawn.SpawnWeaponID 由 SpawnAIInternal 写入
		// Layer 2 (玩家路径): PlayerState.GetSelectedWeapon1ID (原代码)
		// Layer 3 (玩家兜底): RoomGameMode::GetPlayerSpawnData 缓存
		//
		// 大厂原则: 关键行为不能依赖单一路径

		// Layer 0: 从缓存的 Profile 同步武器/角色配置 (关卡预放 AI 专用)
		// 调用 SyncWeaponFromProfile 会检查 MeleeProfile, 如果有效则填充空的 SpawnWeaponID
		SyncWeaponFromProfile();

		// Layer 1: AI 路径
		if (!SpawnWeaponID.IsEmpty())
		{
			WeaponID = SpawnWeaponID;
			UE_LOG(LogTemp, Log, TEXT("[PossessedBy] Using AI-profile weapon: %s, char: %s"),
				*WeaponID, *CharacterID);
		}

		// Layer 2: 玩家路径
		if (WeaponID.IsEmpty())
		{
			if (ARoomPlayerState* PS = NewController->GetPlayerState<ARoomPlayerState>())
			{
				// 设置当前角色ID（用于查找武器挂载配置）
				if (CharacterID.IsEmpty())
				{
					CharacterID = PS->GetSelectedCharacterID();
				}

				WeaponID = PS->GetSelectedWeapon1ID();
				UE_LOG(LogTemp, Log, TEXT("[PossessedBy] Using PlayerState weapon: %s"),
					*WeaponID);
			}
		}

		// Layer 3: GameMode 缓存兜底 (绕过 PS 网络复制时序问题)
		if (WeaponID.IsEmpty())
		{
			if (ARoomGameMode* GM = Cast<ARoomGameMode>(GetWorld()->GetAuthGameMode()))
			{
				FString CachedCharID;
				FString CachedWeaponID;
				if (GM->GetPlayerSpawnData(NewController->GetUniqueID(), CachedCharID, CachedWeaponID))
				{
					WeaponID = CachedWeaponID;
					if (!CachedCharID.IsEmpty() && CharacterID.IsEmpty())
					{
						CharacterID = CachedCharID;
					}
					UE_LOG(LogTemp, Log, TEXT("[PossessedBy] Using cached weapon: %s, char: %s"),
						*CachedWeaponID, *CachedCharID);
				}
			}
		}

		// 最终兜底: 没有武器就跳过 Spawn (不会崩, AI 也可能是无武器单位)
		if (!WeaponID.IsEmpty())
		{
			SpawnAndEquipWeapon(WeaponID);
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("[PossessedBy] 没找到武器ID, 跳过武器 Spawn. AI=%s, HasPS=%d"),
				*GetNameSafe(this),
				NewController->GetPlayerState<ARoomPlayerState>() ? 1 : 0);
		}
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
 * SpawnAndEquipWeapon
 *
 * 根据 WeaponID 查表并生成+装备武器
 * 工业级规范: 指针与有效性校验贯穿全流程
 */
void ABaseCharacter::SpawnAndEquipWeapon(FString WeaponID)
{
	// 工业级规范: 指针与有效性校验
	if (WeaponID.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("[SpawnWeapon] WeaponID 为空，直接返回"));
		return;
	}

	// 【P0 修复 2026.07.10 v2】幂等性检查 - 防止重复生成武器
	// 根因: PossessedBy 和 SetupMeleeAI 在同一帧内都调用了 SpawnAndEquipWeapon
	//        导致生成了两个武器,旧武器没有被销毁,飞出去成为残留物
	// 根因链:
	//   1. PossessedBy 先调用 SpawnAndEquipWeapon → 生成 BP_Weapon_Knife_C_4 → 挂载
	//   2. SetupMeleeAI 再调用 SpawnAndEquipWeapon → 生成 BP_Weapon_Knife_C_5 → 覆盖 CurrentWeapon
	//   3. C_4 没有被销毁,留在场景中 → AI 死亡时 detach + 物理模拟 → 飞出去
	// 修复: 检查当前是否已有武器, 有则跳过
	if (IsValid(CurrentWeapon))
	{
		UE_LOG(LogTemp, Log, TEXT("[SpawnWeapon] 已有武器 %s, 跳过生成 — WeaponID=[%s]"),
			*CurrentWeapon->GetName(), *WeaponID);
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("[SpawnWeapon] 开始生成武器: WeaponID=[%s], CharacterID=[%s]"), *WeaponID, *CharacterID);

	// 1. 从 GameMode 那里获取武器数据表
	ARoomGameMode* GM = Cast<ARoomGameMode>(GetWorld()->GetAuthGameMode());
	if (!GM)
	{
		UE_LOG(LogTemp, Warning, TEXT("[SpawnWeapon] GM 为空"));
		return;
	}
	UE_LOG(LogTemp, Log, TEXT("[SpawnWeapon] GM 获取成功: %s"), *GetNameSafe(GM));

	if (!GM->WeaponDataTable)
	{
		UE_LOG(LogTemp, Warning, TEXT("[SpawnWeapon] WeaponDataTable 未设置，请检查 BP_RoomGameMode 中的 WeaponDataTable 配置"));
		return;
	}
	UE_LOG(LogTemp, Log, TEXT("[SpawnWeapon] WeaponDataTable 有效"));

	static const FString ContextString(TEXT("WeaponSpawnContext"));
	FWeaponInfo* WeaponInfo = GM->WeaponDataTable->FindRow<FWeaponInfo>(FName(*WeaponID), ContextString);

	if (!WeaponInfo)
	{
		UE_LOG(LogTemp, Warning, TEXT("[SpawnWeapon] WeaponInfo 未找到: %s，请检查 WeaponDataTable 中是否存在该行"), *WeaponID);
		return;
	}
	UE_LOG(LogTemp, Log, TEXT("[SpawnWeapon] WeaponInfo 查表成功: %s"), *WeaponID);

	if (!WeaponInfo->WeaponBlueprint)
	{
		UE_LOG(LogTemp, Warning, TEXT("[SpawnWeapon] WeaponBlueprint 为空，请在 WeaponDataTable 中为 %s 配置 WeaponBlueprint"), *WeaponID);
		return;
	}
	UE_LOG(LogTemp, Log, TEXT("[SpawnWeapon] WeaponBlueprint 有效: %s"), *WeaponInfo->WeaponBlueprint->GetName());

	// 2. 查找武器挂载配置
	FWeaponAttachmentConfig* AttachmentConfig = FindWeaponAttachmentConfig(CharacterID, WeaponID);

	// 3. 在服务器上生成武器
	FActorSpawnParameters SpawnParams;
	SpawnParams.Owner = this;
	SpawnParams.Instigator = this;

	// 改造: WeaponBlueprint 改为 TSoftClassPtr (启动期不加载, 避免内存浪费)
	// 这里运行时调用 LoadSynchronous 同步加载拿到 UClass* 再 SpawnActor
	UClass* WeaponClass = WeaponInfo->WeaponBlueprint.LoadSynchronous();
	if (!WeaponClass)
	{
		UE_LOG(LogTemp, Warning, TEXT("[SpawnWeapon] 武器蓝图加载失败 (TSoftClassPtr)"));
		return;
	}

	ABaseWeapon* NewWeapon = GetWorld()->SpawnActor<ABaseWeapon>(
		WeaponClass,
		GetActorLocation(),
		GetActorRotation(),
		SpawnParams
	);

	if (!NewWeapon)
	{
		UE_LOG(LogTemp, Warning, TEXT("[SpawnWeapon] SpawnActor 失败"));
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("[SpawnWeapon] 武器生成成功: %s"), *NewWeapon->GetName());

	// 4. 确定挂载参数
	FName SocketName = TEXT("WeaponSocket_R"); // 默认右手插槽
	FVector RelativeLocation = FVector::ZeroVector;
	FRotator RelativeRotation = FRotator::ZeroRotator;
	FVector RelativeScale = FVector(1.0f, 1.0f, 1.0f);

	// 如果找到了挂载配置，使用配置值
	if (AttachmentConfig)
	{
		UE_LOG(LogTemp, Log, TEXT("[SpawnWeapon] 使用挂载配置: Socket=%s, Loc=%s, Rot=%s"),
			*AttachmentConfig->SocketName.ToString(),
			*AttachmentConfig->RelativeLocation.ToString(),
			*AttachmentConfig->RelativeRotation.ToString());
		SocketName = AttachmentConfig->SocketName;
		RelativeLocation = AttachmentConfig->RelativeLocation;
		RelativeRotation = AttachmentConfig->RelativeRotation;
		RelativeScale = AttachmentConfig->RelativeScale;
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("[SpawnWeapon] 未找到挂载配置，使用默认值"));
	}

	// 5. 将武器焊接到角色的插槽上
	FAttachmentTransformRules AttachmentRules = FAttachmentTransformRules::SnapToTargetNotIncludingScale;
	NewWeapon->AttachToComponent(GetMesh(), AttachmentRules, SocketName);

	// 6. 应用相对位置、旋转、缩放偏移
	if (!RelativeLocation.IsNearlyZero())
	{
		NewWeapon->SetActorRelativeLocation(RelativeLocation);
	}
	if (!RelativeRotation.IsNearlyZero())
	{
		NewWeapon->SetActorRelativeRotation(RelativeRotation);
	}
	if (!RelativeScale.Equals(FVector(1.0f, 1.0f, 1.0f)))
	{
		NewWeapon->SetActorRelativeScale3D(RelativeScale);
	}

	// 7. 更新 CurrentWeapon 指针，这会触发网络同步
	CurrentWeapon = NewWeapon;

	// 8. 刷新战斗 HUD 上的武器图标（仅本地玩家）
	if (IsLocallyControlled())
	{
		if (APlayerController* PC = GetController<APlayerController>())
		{
			if (AMyGameHUD* HUD = Cast<AMyGameHUD>(PC->GetHUD()))
			{
				if (UGameHUDWidget* GameHUD = HUD->GetGameHUDWidget())
				{
					GameHUD->UpdateWeaponIconFromID(WeaponID);
				}
			}
		}
	}
}


/**
 * FindWeaponAttachmentConfig
 *
 * 在 WeaponAttachmentDataTable 中查找挂载配置
 * 优先返回有具体 WeaponBlueprint 配置的（精确匹配）
 * 退而求其次，记录一个通配配置作为 fallback
 */
FWeaponAttachmentConfig* ABaseCharacter::FindWeaponAttachmentConfig(const FString& InCharacterID, const FString& InWeaponID) const
{
	static const FString ContextString(TEXT("WeaponAttachmentLookup"));

	if (!WeaponAttachmentDataTable)
	{
		UE_LOG(LogTemp, Warning, TEXT("[WeaponAttachment] WeaponAttachmentDataTable 未设置!"));
		return nullptr;
	}

	UClass* CurrentCharacterClass = GetClass();

	UE_LOG(LogTemp, Log, TEXT("[WeaponAttachment] 查找配置: CharacterID=%s, WeaponID=%s, CurrentClass=%s"), *InCharacterID, *InWeaponID, *CurrentCharacterClass->GetName());

	TArray<FName> RowNames = WeaponAttachmentDataTable->GetRowNames();
	FWeaponAttachmentConfig* FallbackMatch = nullptr;

	for (const FName& RowName : RowNames)
	{
		FWeaponAttachmentConfig* Config = WeaponAttachmentDataTable->FindRow<FWeaponAttachmentConfig>(RowName, ContextString);
		if (!Config) continue;

		// 检查 TargetCharacter（软引用，需要 IsNull + LoadSynchronous）
		if (!Config->TargetCharacter.IsNull())
		{
			UClass* TargetCharClass = Config->TargetCharacter.LoadSynchronous();
			if (!TargetCharClass || TargetCharClass != CurrentCharacterClass) continue;
		}

		// 【关键修复】: 如果数据表中配置了具体 WeaponBlueprint（硬引用），检查是否匹配
		// WeaponBlueprint 是 TSubclassOf，直接 == nullptr 判断
		if (Config->WeaponBlueprint != nullptr && !InWeaponID.IsEmpty())
		{
			ARoomGameMode* GM = Cast<ARoomGameMode>(GetWorld()->GetAuthGameMode());
			if (GM && GM->WeaponDataTable)
			{
				static const FString WeaponContextString(TEXT("WeaponBlueprintLookup"));
				FWeaponInfo* WeaponInfo = GM->WeaponDataTable->FindRow<FWeaponInfo>(FName(*InWeaponID), WeaponContextString);
				// 改造: WeaponBlueprint 都是 TSoftClassPtr, 需 LoadSynchronous 拿到 UClass* 再 IsChildOf
				if (WeaponInfo)
				{
					UClass* ConfigWeaponClass = Config->WeaponBlueprint.LoadSynchronous();
					UClass* ActualWeaponClass = WeaponInfo->WeaponBlueprint.LoadSynchronous();
					if (!ConfigWeaponClass || !ActualWeaponClass || !ActualWeaponClass->IsChildOf(ConfigWeaponClass))
					{
						continue;
					}
				}
			}
		}

		UE_LOG(LogTemp, Log, TEXT("[WeaponAttachment] 找到匹配配置: RowName=%s"), *RowName.ToString());

		// 优先返回有具体 WeaponBlueprint 配置的（精确匹配）
		if (Config->WeaponBlueprint != nullptr)
		{
			return Config;
		}

		// 退而求其次，记录一个通配配置作为 fallback
		if (!FallbackMatch)
		{
			FallbackMatch = Config;
		}
	}

	if (!FallbackMatch)
	{
		UE_LOG(LogTemp, Warning, TEXT("[WeaponAttachment] 未找到匹配配置"));
	}

	return FallbackMatch;
}


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


/**
 * Multicast_NotifyKill_Implementation
 *
 * NetMulticast 实现: 服务器广播击杀信息给所有客户端
 * 1. 获取击杀者和被击杀者的名称
 * 2. 增加击杀者的击杀数和得分
 * 3. 向所有客户端的 HUD 广播击杀消息
 * 4. 如果击杀者是本地玩家，触发连杀图标更新
 * 5. 如果有助攻者，更新助攻者的数据
 */
void ABaseCharacter::Multicast_NotifyKill_Implementation(AActor* VictimActor, AActor* AssistantActor)
{
	UE_LOG(LogTemp, Log, TEXT("[BaseCharacter] Multicast_NotifyKill: Killer=%s, Victim=%s, HasAuthority=%d"),
		*GetName(), *VictimActor->GetName(), HasAuthority());

	// 获取击杀者和被击杀者的名称
	FString KillerName = TEXT("Unknown");
	FString VictimName = TEXT("Unknown");

	// 获取击杀者名称
	if (ARoomPlayerState* PS = GetRoomPlayerState())
	{
		KillerName = PS->GetPlayerName();
		// 增加击杀者的击杀数和得分
		PS->AddKillScore();
	}

	// 获取被击杀者名称
	if (ABaseCharacter* VictimChar = Cast<ABaseCharacter>(VictimActor))
	{
		if (ARoomPlayerState* VictimPS = VictimChar->GetRoomPlayerState())
		{
			VictimName = VictimPS->GetPlayerName();
		}
	}

	// 向所有客户端的 HUD 广播击杀消息
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
						HUDWidget->AddKillFeedMessage(KillerName, VictimName, LastKillMethod);

						// 判断是否是本地玩家（击杀者）自己的 HUD
						// 如果击杀者是当前控制的角色，则更新自己的连杀图标
						if (IsLocallyControlled())
						{
							// 根据击杀方式判断是否是爆头
							bool bIsHeadshot = (LastKillMethod == EKillMethod::PrimaryHeadshot ||
								LastKillMethod == EKillMethod::SecondaryHeadshot ||
								LastKillMethod == EKillMethod::MeleeHeadshot);
							HUDWidget->OnPlayerKill(bIsHeadshot);
						}
					}
				}
			}
		}
	}

	// 如果有助攻者，也更新助攻者的数据
	if (AssistantActor)
	{
		ABaseCharacter* AssistantChar = Cast<ABaseCharacter>(AssistantActor);
		if (AssistantChar)
		{
			if (ARoomPlayerState* AssistantPS = AssistantChar->GetRoomPlayerState())
			{
				AssistantPS->AddAssistScore();
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
 * 授予符合条件的玩家助攻得分
 * 静态方法: 遍历受害者的最后攻击者记录
 * 排除击杀者本人，检查时间窗口
 * 更新助攻者的得分 + 广播助攻信息
 */
void ABaseCharacter::GrantAssistsToEligiblePlayers(ABaseCharacter* Victim, ABaseCharacter* Killer)
{
	if (!Victim || !Killer)
	{
		return;
	}

	UWorld* World = Victim->GetWorld();
	if (!World || !World->GetAuthGameMode())
	{
		return;
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
		float TimeSinceHit = World->GetTimeSeconds() - HitPair.Value;
		if (TimeSinceHit <= Victim->AssistTimeWindow)
		{
			// 符合条件的助攻者
			ABaseCharacter* AssistantChar = Cast<ABaseCharacter>(AttackerActor);
			if (AssistantChar)
			{
				// 获取助攻者的 PlayerState
				if (ARoomPlayerState* AssistantPS = AssistantChar->GetRoomPlayerState())
				{
					AssistantPS->AddAssistScore();

					// 广播助攻信息给所有客户端
					AssistantChar->Multicast_NotifyKill(Victim, nullptr);
				}
			}
		}
	}

	// 更新击杀者的死亡次数
	if (ARoomPlayerState* VictimPS = Victim->GetRoomPlayerState())
	{
		VictimPS->AddDeath();
	}

	// 清理时间戳
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
	if (FootstepComponent)
	{
		FootstepComponent->PlayFootstep(this, Location);
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

	// 血量
	if (HealthComponent)
	{
		const float Current = HealthComponent->GetCurrent();
		const float Max = HealthComponent->GetMax();
		GameHUDWidget->UpdateHealth(Current, Max);
		GameHUDWidget->UpdateHealthText(FMath::CeilToInt(Current), FMath::CeilToInt(Max));
	}

	// 能量 - 【2026-06-15 重构】通过 EnergyComponent
	if (EnergyComponent)
	{
		const float ECurrent = EnergyComponent->GetCurrent();
		const float EMax = EnergyComponent->GetMax();
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
	if (!CharacterEvents)
	{
		return;
	}

	// 【2026-07-01 P0 修复】"事件 + 缓存"双轨制
	// 先写缓存, 再 Broadcast, 保证订阅者订阅时能从缓存拿到最新值
	CharacterEvents->SetCachedCharacterIcon(CharID, Icon);
	CharacterEvents->OnCharacterIconReady.Broadcast(CharID, Icon);
}


/**
 * BroadcastACValueChanged
 *
 * 广播 AC 防护服值变化事件
 * 调用链: OnRep_ACValue → BroadcastACValueChanged
 */
void ABaseCharacter::BroadcastACValueChanged(int32 NewAC)
{
	if (!CharacterEvents)
	{
		return;
	}

	// 【2026-07-01 P0 修复】事件 + 缓存双轨
	CharacterEvents->SetCachedACValue(NewAC);
	CharacterEvents->OnACValueChanged.Broadcast(NewAC);
}


/**
 * BroadcastACEWithRankChanged
 *
 * 广播 ACE 击杀数 + 排名颜色变化事件
 * 调用链: RefreshACEWithRank → BroadcastACEWithRankChanged
 */
void ABaseCharacter::BroadcastACEWithRankChanged(int32 NewACE, EACERankType RankType)
{
	if (!CharacterEvents)
	{
		return;
	}

	// 【2026-07-01 P0 修复】事件 + 缓存双轨
	CharacterEvents->SetCachedACEState(NewACE, RankType);
	CharacterEvents->OnACEWithRankChanged.Broadcast(NewACE, RankType);
}


/**
 * BroadcastWeaponIconReady
 *
 * 广播武器图标加载完毕事件
 * 调用链: RefreshWeaponIconOnHUD → BroadcastWeaponIconReady
 */
void ABaseCharacter::BroadcastWeaponIconReady(const FString& WeaponID, UTexture2D* Icon)
{
	if (!CharacterEvents)
	{
		return;
	}
	CharacterEvents->OnWeaponIconReady.Broadcast(WeaponID, Icon);
}


// ==========================================
// 【Phase 1 重构】 IGenericTeamAgentInterface 实现
//
// 设计: GameplayTag 是单一真实源 (Single Source of Truth).
//       uint8 GetTeamID() 是派生属性 (从 FactionTag 推 0/1),
//       仅供旧代码兼容. 新代码全部用 GetGenericTeamId/GetTeamAttitudeTowards.
// ==========================================

FGenericTeamId ABaseCharacter::ResolveGenericTeamIdFromTag(FGameplayTag InTag)
{
	// 【P0 大厂架构修复 2026.07.03】改为查表 + 严格匹配父级 Faction.X
	// 旧版坑: 漏匹配 Faction.Enemy, AI 走 Profile.FactionTag="Faction.Enemy" → 走到末尾 return NoTeam
	//        → Controller::SetGenericTeamId(NoTeam) 把 Pawn FactionTag 清空
	//        → 下一帧 GetGenericTeamId() fallback 到 FGenericTeamId(0) = Player 阵营
	//        → AIPerception 看到玩家后用阵营协议判定为 Friendly → 永远看不到
	//
	// 新版:
	//   1. 用 TMap 集中维护 (添加新阵营不用动控制流)
	//   2. 严格 MatchesTagExact, 避免 "Faction.Enemy" 误匹配 "Faction.EnemyBoss"
	//   3. 兜底: 不认识的 FactionTag 一律返回 NoTeam(255), 不再污染为 0(Player)
	//   4. 首次调用时懒初始化表 (避免模块启动期 GameplayTag 表未就绪)

	static TMap<FName, FGenericTeamId> TagToTeamId;
	static bool bTableInitialized = false;
	if (!bTableInitialized)
	{
		TagToTeamId.Add(TEXT("Faction.Player"),     FGenericTeamId(0));
		TagToTeamId.Add(TEXT("Faction.Zombie"),     FGenericTeamId(1));
		TagToTeamId.Add(TEXT("Faction.FriendlyAI"), FGenericTeamId(2));
		TagToTeamId.Add(TEXT("Faction.Neutral"),    FGenericTeamId::NoTeam);
		TagToTeamId.Add(TEXT("Faction.Enemy"),      FGenericTeamId(1)); // 【P0 关键】MeleeAI 走这个
		TagToTeamId.Add(TEXT("Faction.Hostile.All"),FGenericTeamId(255));
		bTableInitialized = true;
	}

	if (!InTag.IsValid())
	{
		return FGenericTeamId::NoTeam;
	}

	if (const FGenericTeamId* Found = TagToTeamId.Find(InTag.GetTagName()))
	{
		return *Found;
	}

	// 【P0 兜底】未识别 → NoTeam(255), 不再返回 0 (避免把 AI 误标为玩家阵营)
	return FGenericTeamId::NoTeam;
}

void ABaseCharacter::SetGenericTeamId(const FGenericTeamId& NewTeamID)
{
	// 【P0 修复 2026.07.03】反向写回 FactionTag, 跟正向 Resolve 严格对齐
	// 旧版坑: ID=1 写 Faction.Zombie, 但 AI 配的是 Faction.Enemy, 下一帧 Resolve 会再算回 ID=1
	//        → 反复横跳不影响功能, 但日志难追. 现在: 写 Faction.Enemy 保持命名稳定.
	const int32 Id = NewTeamID.GetId();
	if (Id == 0)         FactionTag = FGameplayTag::RequestGameplayTag(TEXT("Faction.Player"));
	else if (Id == 1)    FactionTag = FGameplayTag::RequestGameplayTag(TEXT("Faction.Enemy")); // 跟正向表一致
	else if (Id == 2)    FactionTag = FGameplayTag::RequestGameplayTag(TEXT("Faction.FriendlyAI"));
	else if (Id == 255)  FactionTag = FGameplayTag::RequestGameplayTag(TEXT("Faction.Hostile.All"));
	else                 FactionTag = FGameplayTag(); // NoTeam → 空 tag
}

FGenericTeamId ABaseCharacter::GetGenericTeamId() const
{
	if (FactionTag.IsValid())
	{
		return ResolveGenericTeamIdFromTag(FactionTag);
	}
	// 【P0 修复 2026.07.03】无 FactionTag 时回退到 NoTeam(255), 不再默认 0(Player)
	// 原理: AI 没配阵营时应该是 "非玩家" 的兜底, 跟 Resolve 表末尾的 NoTeam 对齐
	//      之前默认 0 会导致: AI 没配 Profile → 跟玩家同阵营 → 不追玩家 (本次 bug 链的源头)
	return FGenericTeamId::NoTeam;
}

ETeamAttitude::Type ABaseCharacter::GetTeamAttitudeTowards(const AActor& Other) const
{
	if (const IGenericTeamAgentInterface* OtherTeamAgent =
		Cast<IGenericTeamAgentInterface>(&Other))
	{
		const FGenericTeamId OtherTeamId = OtherTeamAgent->GetGenericTeamId();
		const FGenericTeamId MyTeamId = GetGenericTeamId();

		// 同阵营 = Friendly
		if (MyTeamId == OtherTeamId && OtherTeamId != FGenericTeamId::NoTeam)
		{
			return ETeamAttitude::Friendly;
		}

		// 显式 Hostile 标记 (UE 5.6 中没有 ::Hostile 静态, 用 (255) 数字)
		const FGenericTeamId HostileId(255);
		if (OtherTeamId == HostileId || MyTeamId == HostileId)
		{
			return ETeamAttitude::Hostile;
		}

		// 否则按 ID 分组: 偶数阵营互相 Friendly, 奇数阵营互相对立 (简单启发式)
		const bool bOtherEvenTeam = (OtherTeamId.GetId() % 2) == 0;
		const bool bMyEvenTeam     = (MyTeamId.GetId() % 2) == 0;
		if (bOtherEvenTeam == bMyEvenTeam && MyTeamId != FGenericTeamId::NoTeam)
		{
			return ETeamAttitude::Friendly;
		}
		return ETeamAttitude::Hostile;
	}
	return ETeamAttitude::Neutral;
}

uint8 ABaseCharacter::GetTeamID() const
{
	// 【Phase 1 重构】派生属性 — 从 FactionTag 反推 uint8, 仅旧代码用
	switch (GetGenericTeamId().GetId())
	{
	case 0: return 0; // Faction.Player -> 攻方
	case 1: return 1; // Faction.Zombie -> 守方
	case 2: return 2;
	default: return 0;
	}
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
