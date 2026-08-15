// 版权声明：在项目设置的描述页面填写您的版权信息。

// ==========================================
// 头文件包含区
// ==========================================
// 引入本类头文件
#include "Weapons/BaseWeapon.h"

// UE 反射: DOREPLIFETIME (GetLifetimeReplicatedProps 需要)
#include "Net/UnrealNetwork.h"

// 【v83】APawn (Multicast_PlayFireTraceVisual_Implementation 用 Owner Pawn->IsLocallyControlled 跳过服务器本地)
#include "GameFramework/Pawn.h"

// 引入武器模型类型枚举 (v61.2 — DT_WeaponInfo 真理源)
#include "Data/Tables/WeaponTableRow.h"

// 引入 Mesh 组件基类 (GetMeshComponent 返回 UMeshComponent*)
#include "Components/MeshComponent.h"
// 引入 SkeletalMesh 组件 (ResolveMagazineSkeletalMesh 返回类型)
#include "Components/SkeletalMeshComponent.h"
// 【v200.4 大厂架构】ProjectileMovementComponent (Multicast_FreezeWeaponTransform 关闭 PMC 用)
#include "GameFramework/ProjectileMovementComponent.h"

// 引入角色基类（用于伤害计算, 读 bIsCurrentlyAttackerAI）
#include "Characters/BaseCharacter.h"
// 为了获取主人的视角和位置，必须包含 Character 头文件
#include "GameFramework/Character.h"
// 引入 AIController（用于 bIsAIDriven 时从 AIC 读 ConfigSO.Damage）
#include "AIController.h"
#include "Systems/BaseAIController.h" // ABaseAIController::GetEffectiveAttackDamage
// 射线检测核心库
#include "Kismet/KismetSystemLibrary.h"
#include "GameFramework/Pawn.h"
// 扣血指令库（为下一步真正扣血做准备）
#include "Kismet/GameplayStatics.h"
// 【P0 2026.07.10 大厂重构】武器溶解组件 (职责对等)
#include "Components/WeaponDissolveComponent.h"
// 【v70 P0】武器开火组件 (CreateDefaultSubobject 需要完整类型)
#include "Components/WeaponFireComponent.h"
// 【2026.07.11 P0 大厂架构】友军伤害守卫 (单一真理源入口)
#include "Data/Faction/FactionTags.h"
// v70 缺失接口: IWeaponDamageStrategy (GetDamageStrategy / SetDamageStrategy)
#include "Weapons/WeaponDamageStrategy.h"
#include "Weapons/MeleeSwStrategy.h"
#include "Weapons/RangedLineStrategy.h"


// ==========================================
// 1. 构造函数
// ==========================================

/**
 * ABaseWeapon 构造函数
 *
 * 目的:
 * - 开启网络同步（武器要挂在玩家手上让所有人看见）
 * - 创建武器模型组件并设为 RootComponent
 * - 关闭武器自身物理碰撞（伤害判定靠代码射线）
 * - 初始化默认小刀的数值
 * - 修改 Tick 组为 TG_PostUpdateWork（保证插槽坐标绝对精准）
 */
ABaseWeapon::ABaseWeapon()
{
	// 开启网络同步，因为这把武器要挂在玩家手上让所有人看见
	bReplicates = true;

	// 【v61.3 重构】Mesh 组件不再 CreateDefaultSubobject — 由 BP 蓝图手动添加
	// 近战武器: StaticMeshComponent; 枪械: SkeletalMeshComponent
	// BeginPlay 中按需查找并验证

	// WeaponFireComponent — 纯 C++ 组件 (无 BP 配置需求)
	// 由 InitializeFromWeaponConfig 从 DT_WeaponInfo 运行时初始化，不需要手动加 BP
	WeaponFireComponent = CreateDefaultSubobject<UWeaponFireComponent>(TEXT("WeaponFireComponent"));

	// 初始化默认值 (BP 可覆盖)
	LightDamageBody = 20.0f;
	LightDamageHead = 80.0f;
	HeavyDamage = 999.0f;
	AttackRange = 150.0f;
	AttackRadius = 15.0f;

	PrimaryActorTick.bCanEverTick = true;
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;

	// 【v83 客户端同步修复】SetReplicateMovement(true)
	//   - bReplicates 已在 line 59 设为 true (基础复制)
	//   - 但 transform (位置/旋转) 默认不同步, 客户端武器可能停在初始 Spawn 位置
	//   - SetReplicateMovement(true) → 客户端能跟着武器 Attach 到角色 Socket 移动
	SetReplicateMovement(true);

	// 【v200.2.18 大厂架构 — 物理同步关键参数】
	//   根因: 武器掉落后, 服务器 SetSimulatePhysics(true) 物理引擎接管 transform
	//         默认 NetUpdateFrequency=10Hz 对物理下落太低, 客户端看到"卡顿/悬空"
	//         → 显式设 NetUpdateFrequency=30Hz (物理下落平滑但带宽可接受)
	//         → MinNetUpdateFrequency=2Hz (避免 relevancy 切回时太突兀)
	//   大厂原则 (与 AirdropPickup 一致 — 单一真理源 = NetUpdateFrequency = 30Hz):
	//     - 武器掉落物理同步标准频率
	//     - 带宽: 30Hz × 1 武器 × ~64B = ~2KB/s (一台 PC 几乎可忽略)
	//     - 客户端看到的是服务器物理引擎 1/3 的采样, 完全够看清下落过程
	SetNetUpdateFrequency(30.0f);
	SetMinNetUpdateFrequency(2.0f);

	// 【v200.2.18 大厂架构 — Relevancy】
	//   根因: 默认 Actor relevancy 由 NetCullDistanceSquared 决定 (225m 视距)
	//         玩家走远后武器可能 relevancy=false → 客户端看到武器"消失"
	//         → bAlwaysRelevant=true 让武器始终在所有客户端 relevancy 列表里
	//   大厂原则 (武器掉落物理需要):
	//     - 武器在地上等捡起, 任何走到附近的玩家都能看到/捡起
	//     - 不需要 relevancy 剔除 (武器是低数量级对象, 带宽可控)
	bAlwaysRelevant = true;

	// 【v200.2.18 大厂架构 — RootComponent 真理源】
	//   根因(v200.2.18 验证): C++ 完全没设 RootComponent, 完全依赖 BP 蓝图配置
	//         如果 BP 没显式设 RootComponent (常见情况), UE 会用第一个注册 Component 作 Root
	//         → 物理模拟、Attach、Movement Replication 都基于错误的 RootComponent → 客户端悬空
	//   大厂原则 — 单一真理源 (与 AirdropPickup 一致):
	//     - AirdropPickup 在 C++ 构造函数设 RootComponent = StaticMeshComponent
	//     - BaseWeapon 不设 RootComponent = 反模式 (依赖 BP 配置 = 不确定性)
	//   修复策略 (大厂双轨):
	//     1. C++ 默认值: 不强制设 RootComponent (因为武器可能是 StaticMesh 或 SkeletalMesh, 类型未知)
	//     2. BeginPlay 时检查并修复: 如果 RootComponent 不是 PrimitiveComponent, 自动用第一个 MeshComponent
	//        这是防御式编程 — 即使 BP 没配对, 运行时也能保证物理同步正常
	WeaponDissolveComponent = CreateDefaultSubobject<UWeaponDissolveComponent>(TEXT("WeaponDissolveComponent"));
}


/**
 * 【v82 客户端武器可用性修复】GetLifetimeReplicatedProps — 注册需要复制的字段
 *
 * 大厂原则 (单一真理源 + 显式同步):
 *   - 服务器 SpawnAndEquipWeapon → InitializeWeaponFireConfigFromClass 写入策划配置
 *   - 客户端需要这些字段才能正确触发 RPC 和 trace
 *   - UE 自动复制: 服务器写入 → 网络同步 → 客户端 OnRep_* 触发 (本类无 OnRep, 客户端直读)
 *
 * 复制清单 (6 个核心字段):
 *   - AttackRange   (枪械射线射程)
 *   - MuzzleOffset  (枪口偏移)
 *   - WeaponRowName (DT_WeaponInfo 行名, 弹匣动画/伤害计算用)
 *   - MeshType      (武器类型: Primary/Secondary/Melee, 决定按左键走 LightAttack 还是 Server_StartFire)
 *   - DamageStrategy (TScriptInterface — v82 修复: 必须 DOREPLIFETIME, UE 不会自动复制 UObject 引用)
 *                       调用方: ANS_MeleeTraceState (客户端) + WeaponFireComponent (服务器)
 */
void ABaseWeapon::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// 6 个核心字段 — 客户端必须跟服务器一致
	DOREPLIFETIME(ABaseWeapon, AttackRange);
	DOREPLIFETIME(ABaseWeapon, MuzzleOffset);
	DOREPLIFETIME(ABaseWeapon, WeaponRowName);
	DOREPLIFETIME(ABaseWeapon, MeshType);

	// v82 修复: DamageStrategy 必须 Replicated (客户端 ANS_MeleeTraceState 调 GetDamageStrategy 不能为 nullptr)
	// 旧注释说"UE 标准对象复制系统自动处理"是错的, UObject 引用必须显式 DOREPLIFETIME
	DOREPLIFETIME(ABaseWeapon, DamageStrategy);

	// 【v82+ 修复】LightAttackMontages / HeavyAttackMontage 必须 Replicated
	//   - 旧版 (v6-v81) 没标 Replicated → 客户端拿不到蒙太奇引用 → GetAttackMontage 返回 nullptr
	//   - 客户端 ExecuteComboSequence 跳过 if (ComboMontage) → 客户端不播本地 montage
	//   - 客户端 ANS_MeleeTraceState::NotifyBegin 收不到 → 客户端屏幕看不到 trace
	//   - 服务器 multicast montage → 客户端 OnPlayerAttackMontageEnded 收到 (multicast) 但 CachedPlayerMontage 空
	//   - 根因: 武器 BP 设的蒙太奇引用没有传给客户端
	//   - 修复: UPROPERTY(Replicated) + DOREPLIFETIME → 客户端能拿到正确的 LightAttackMontages / HeavyAttackMontage
	DOREPLIFETIME(ABaseWeapon, LightAttackMontages);
	DOREPLIFETIME(ABaseWeapon, HeavyAttackMontage);

	// 【v200.2.20 大厂架构 — WeaponFireComponent 字段指针复制】
	//   根因: WeaponFireComponent 子组件本身 Replicated (SetIsReplicatedByDefault), 但字段指针不同步
	//         → 客户端 Weapon->WeaponFireComponent 永远 null → 客户端开火 RPC 失败
	//   修复: UPROPERTY(Replicated) + DOREPLIFETIME → UE 自动同步字段指针给客户端
	//   大厂原则 (单一真理源): 字段指针就是单一真理源, 不需要 BeginPlay 用 FindComponentByClass 兜底
	DOREPLIFETIME(ABaseWeapon, WeaponFireComponent);
}


/**
 * BeginPlay — 【v83 大厂架构】客户端武器 Mesh 强制 NoCollision
 *
 * 调用方: UE 引擎 (Actor 初始化完成)
 *
 * 【核心问题】客户端武器 Mesh 阻挡角色移动 + SpringArm 阻挡
 *   - 服务器路径 (SpawnAndEquipWeapon / SpawnAndConfigureWeaponInSlot / Server_SpawnAllWeapons
 *     / OnRep_CurrentWeaponSlot) 已 SetActorEnableCollision(NoCollision)
 *   - 但这些函数在客户端进程**不执行** (它们是 Server RPC 或 OnRep 触发)
 *   - 客户端进程拿到的 BP_Weapon_Xxx Actor 是 UE 复制创建, Mesh 仍保留 BP 默认 BlockAllDynamic 碰撞
 *   - 后果 1: 客户端玩家拿着 BP_Weapon_ShovelLarge (StaticMesh, 长 ~70cm) 在场景跑
 *     → 武器 StaticMesh 阻挡角色 Capsule 移动 → 画面"一直抖动"
 *   - 后果 2: 武器 mesh 阻挡 SpringArm (CameraBoom->ProbeSize=12 默认值)
 *     → 镜头缩到角色身上 → "射击时镜头被阻挡"
 *
 * 大厂原则 (零兜底 + 单一真理源 + 物理正确性):
 *   - 这是"物理正确性"不是"业务兜底": 武器 Mesh 不应该物理阻挡任何 Actor
 *   - 武器纯装饰 + 伤害源, 不参与碰撞
 *   - BeginPlay 双保险: 服务器端即使忘记在 SpawnAndEquipWeapon 中调 NoCollision, BeginPlay 也兜底
 *   - 客户端端 BeginPlay 是必须的: Server 函数不跑客户端 → 必须 BeginPlay 兜
 *
 * 大厂原则 (Decoupled Architecture):
 *   - 不用反射, 不用事件, 不用 OnRep
 *   - 直接调 GetMeshComponent() (单一真理源), 然后设 NoCollision
 *   - 找不到 Mesh → Log Warning (不影响游戏, 玩家不会察觉, 但策划配错应该报警)
 */
void ABaseWeapon::BeginPlay()
{
	Super::BeginPlay();

	// 【v200.2.20 大厂架构 P0 — WeaponFireComponent 字段复制链修复】
	//   根因 (v200.2.9 修复没覆盖的关键场景):
	//     - WeaponFireComponent 字段是 TObjectPtr<>, 没有 Replicated 标记
	//     - 服务器 attach 武器到客户端时, 子组件本身被复制 (Actor 默认复制子组件)
	//     - 但客户端 Actor 的 WeaponFireComponent 字段指针**永远 nullptr**
	//     - 因为字段指针不在复制链, UE 不会同步字段值
	//     - 服务端 Spawn 出来的武器, 客户端字段 = null
	//   表现:
	//     - 玩家自己 spawn 的枪 (服务器本地): 客户端字段 = 有效 (因为 spawn 同步创建)
	//     - 玩家捡起别人扔的枪: 服务器 attach 枪 → 客户端字段还是 null → 无法开火
	//   修复 (v200.2.20):
	//     - BeginPlay 在两端都执行 (服务器 + 客户端), 在两端都用 FindComponentByClass 重新找子组件
	//     - 如果字段为 null 或 Owner 不对, 用 FindComponentByClass 找并替换字段
	//     - 这是 UE 标准做法: 组件复制 ≠ 字段指针复制, 必须 FindComponentByClass
	if (!WeaponFireComponent || WeaponFireComponent->GetOwner() != this)
	{
		// 【v200.2.9 保留】BPGC 漂移修复 (旧版触发条件)
		if (WeaponFireComponent)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[v200.2.9] ABaseWeapon::BeginPlay: BPGC 漂移检测 — Weapon=%s WeaponFireComponent.Owner=%s (不是 this). "
				     "正在查找真实 Component..."),
				*GetName(),
				*GetNameSafe(WeaponFireComponent->GetOwner()));
		}
		else
		{
			// 【v200.2.20 新增】字段为 null (典型场景: 客户端捡别人扔的枪, 子组件被复制但字段指针没同步)
			UE_LOG(LogTemp, Display,
				TEXT("[v200.2.20] ABaseWeapon::BeginPlay: WeaponFireComponent 字段为 null — 修复组件复制链断. "
				     "Weapon=%s. 客户端捡别人扔的枪场景会出现此情况."),
				*GetName());
		}

		// 在自己身上查找 UWeaponFireComponent (无论字段是 null 还是 BPGC 漂移, 都用同一修复路径)
		UWeaponFireComponent* RealFireComp = FindComponentByClass<UWeaponFireComponent>();
		if (RealFireComp)
		{
			if (RealFireComp != WeaponFireComponent)
			{
				UE_LOG(LogTemp, Display,
					TEXT("[v200.2.20] ABaseWeapon::BeginPlay: 找到真实 WeaponFireComponent=%s, 替换字段 (修复字段为 null / BPGC 漂移)."),
					*RealFireComp->GetName());
				WeaponFireComponent = RealFireComp;
			}
		}
		else
		{
			// 【v200.2.20 零兜底】没找到子组件 → 武器没有开火能力 → 必须 Error
			UE_LOG(LogTemp, Error,
				TEXT("[v200.2.20] ABaseWeapon::BeginPlay: WeaponFireComponent 字段为 null 且找不到子组件. "
				     "Weapon=%s. 修复: 检查 BP_Weapon_%s 是否包含 UWeaponFireComponent 子对象."),
				*GetName(),
				*GetName());
		}
	}

	// 强制禁用武器 Actor 物理碰撞 (含所有组件 — Mesh + MagazineSkeletal + ...)
	//   - SetActorEnableCollision(false) 影响整个 Actor 的所有 primitive 组件
	//   - 武器 mesh 不应阻挡任何东西 (角色/SpringArm/物理对象)
	//   - 这是"装饰 Actor"的标准做法 (与 Projectile Actor 相反 — 子弹需要物理碰撞)
	SetActorEnableCollision(false);

	// 【v220.1 大厂架构 P0 — 移除 RootComponent 兜底 (零兜底原则)】
	//   v200.2.18 错误:
	//     - 之前在 BeginPlay 中检测到 RootComponent 为空/类型不对时, 主动 SetRootComponent(WeaponMesh)
	//     - 这在客户端触发 StaticMesh/SkeletalMesh 资产的同步加载 (DDC 同步 await)
	//     - 现象: 客户端进游戏 → SetRootComponent 触发 → 3.7s+ 卡死 (LogStaticMesh: Waiting on ...)
	//     - 与用户 2026.08.09 反馈"客户端进游戏又卡死了"直接相关
	//   v220.1 修复 (P0 大厂架构 — 零兜底):
	//     - 1. 不再 SetRootComponent (兜底自动修复 = 静默同步加载 = 卡死)
	//     - 2. 检测到 RootComponent 错配 → Log Error + 继续 (不改变组件结构)
	//     - 3. 强制要求开发者在 BP 中配置 RootComponent (零兜底 = 错误必须显式)
	//     - 4. 客户端不会触发放任性的同步加载 → 渲染线程/资产线程正常异步加载
	//
	//   大厂原则 — 零兜底:
	//     - "Hard Restore On Error" 看似用户友好, 实际是隐藏的同步阻塞
	//     - 错误必须显式化: Log Error + 强制修复 BP, 而不是"自动修复" (用户根本不知道)
	//     - 零兜底 = 错误可视化 + 强制开发者修复
	USceneComponent* CurrentRoot = GetRootComponent();
	if (!CurrentRoot || !CurrentRoot->IsA<UPrimitiveComponent>())
	{
		// 零兜底: 检测到 RootComponent 错配 → Log Error + 拒绝继续 (不修改组件结构)
		// 强制要求开发者在 BP 蓝图手动设 Mesh 为 RootComponent
		UE_LOG(LogTemp, Error,
			TEXT("[v220.1] ABaseWeapon::BeginPlay: RootComponent 错配 — Weapon=%s 当前 RootComponent=%s (类型不对/为空). "
			     "【零兜底】v220.1 已移除 SetRootComponent 兜底 (.presentate 引发客户端同步加载卡死). "
			     "【修复方法】在 BP_%s 的 Components 面板手动设 MeshComponent 为 RootComponent. "
			     "【客户端表现】actor 物理同步/Attach/Movement Replication 可能异常, 但不会卡死主线程."),
			*GetName(),
			CurrentRoot ? *CurrentRoot->GetName() : TEXT("nullptr"),
			*GetClass()->GetName());
		// ⚠️ 不调 SetRootComponent — 避免 3.7s+ 同步加载卡死
		// ⚠️ 不 return — 继续播放游戏, 让客户端异步加载资源 (UE 5.6 内部异步加载)
	}

	// 双保险: 单独对 Mesh 组件设 ECollisionEnabled::NoCollision
	//   - 某些 BP 的 Actor 可能有多个 Mesh (主 mesh + 弹匣 mesh + ...)
	//   - 对每个 Mesh 都设 NoCollision
	if (UMeshComponent* WeaponMesh = GetMeshComponent())
	{
		WeaponMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);
		WeaponMesh->SetCollisionResponseToAllChannels(ECR_Ignore);

		// 【v240.2 大厂架构 P0 修复】SkeletalMesh 物理模拟导致 DefaultSceneRoot 下坠
		//   根因: BP_Weapon_AK47_C 中 WeaponSkeletalMesh 被配置为"模拟物理"
		//         UE 物理引擎驱动 SkeletalMesh 骨骼动画
		//         而 DefaultSceneRoot 作为 RootComponent 会跟随骨骼运动 → 垂直下坠
		//   表现: AK47 武器完全看不见 (Diff Rot 达到 47°)
		//   匕首正常的原因: StaticMesh 没有开启物理模拟
		//   修复: 委托 EnsureSkeletalMeshPhysicsDisabled 统一处理 (单一真理源, 零重复)
		EnsureSkeletalMeshPhysicsDisabled(TEXT("BeginPlay"));
	}
	else
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[ABaseWeapon] BeginPlay: Weapon=%s 没有 Mesh 组件! 客户端可能依然会出现碰撞阻挡问题. "
			     "【v83 警告】修复: 在 BP 蓝图的 Components 面板添加 StaticMeshComponent (近战) 或 SkeletalMeshComponent (枪械)."),
			*GetName());
	}

	// 【v240.4 大厂架构 P0 终极修复】主动禁用 ProjectileMovementComponent (PMC)
	//   根因 (v240.3 诊断日志精确锁定):
	//     - BP_Weapon_AK47.uasset 配了 ProjectileMovementComponent, bAutoActivate=true
	//     - SpawnActor 后 PMC 立即 Active, 每帧 TickComponent 主动应用 Gravity*dt 修改 RootComponent 位置
	//     - 日志证据: AK47 RootComponent.RelativeLocation.Z 在 5.5s 内从 -1.58 持续下降到 -26019.96 (≈47 m/s, 匹配 g=980 cm/s² 重力加速度)
	//     - 匕首正常原因: BP_Weapon_Knife 没有 PMC → 稳定 Z=-42.57
	//   与 v240.2 EnsureSkeletalMeshPhysicsDisabled 的本质区别:
	//     - v240.2 关闭的是"物理引擎驱动的骨骼模拟" (IsSimulatingPhysics() 路径)
	//     - PMC 的"重力"是 PMC 内部主动 MoveComponent 走的, 完全不经过物理引擎 → IsSimulatingPhysics() 永远 false
	//     - 所以 v240.2 单独存在不能解决 PMC 问题
	//
	//   修复 (单一真理源 + 显式状态机):
	//     - BeginPlay 默认禁用 PMC, 保证武器 SpawnActor → 默认"手持/挂在角色身上"状态 → PMC 必须 inactive
	//     - 后续投掷时由 WeaponDropComponent::StartDroppedState 显式激活 PMC (唯一合法激活点)
	//     - 落地时由 Multicast_FreezeWeaponTransform 显式禁用 PMC (合法禁用点)
	//     - 捡起时由 WeaponDropComponent::CancelDroppedState 显式禁用 PMC (合法禁用点)
	//     - BeginPlay 是第 4 个合法禁用点 (防止 BP 配错 bAutoActivate=true)
	//
	//   【大厂原则 — 零兜底 (非"隐藏错误"而是"显式状态机")】
	//     - 这里禁用 PMC 不是"隐藏 BP 配错", 而是显式表达"手持武器 → PMC inactive"的系统约束
	//     - PMC bAutoActivate=true 配置错会通过 Display Log 暴露 (与 Warning/Error 区分, 表明是设计意图而非异常)
	//     - 投掷功能 100% 保留 (StartDroppedState 仍能激活 PMC)
	//   【匕首的 silent return】
	//     - BP_Weapon_Knife 没有 PMC → FindComponentByClass 返回 null → 不 Log
	//     - 这是正确行为, 不是兜底
	UProjectileMovementComponent* PMC = FindComponentByClass<UProjectileMovementComponent>();
	if (PMC)
	{
		if (PMC->IsActive())
		{
			UE_LOG(LogTemp, Display,
				TEXT("[v240.4] ABaseWeapon::BeginPlay: Weapon=%s 检测到 PMC 已激活 (BP bAutoActivate=true), "
				     "BeginPlay 主动禁用 — 防止挂载时持续下坠 (Z 轴重力 980 cm/s²). "
				     "PMC 将在 WeaponDropComponent::StartDroppedState 投掷时激活."),
				*GetName());
		}
		PMC->SetActive(false);
	}
}


/**
 * EnsureSkeletalMeshPhysicsDisabled — 【v240.2 大厂架构 P0】SkeletalMesh 物理强制关闭
 *
 * 调用方:
 *   - BaseWeapon::BeginPlay (默认防御)
 *   - BaseWeapon::Multicast_FreezeWeaponTransform_Implementation (落地时防御)
 *   - WeaponDropComponent::CancelDroppedState (取消投掷状态时防御)
 *
 * 大厂原则 (单一真理源 + 零重复):
 *   - 3 处独立的 SkeletalMesh 物理关闭逻辑合并为一个 helper
 *   - 避免 3 处重复的 IsSimulatingPhysics + SetSimulatePhysics + RecreatePhysicsState
 *   - 避免 3 处重复的 Log (Warning/Error)
 *   - 统一 Log Error 而非 Warning (配置错必须显式化, 强制美术/策划修复 BP)
 *
 * 大厂原则 (零兜底):
 *   - 强制关闭物理模拟 = "运行时"兜底, 但**不是**隐藏的同步加载 (SetRootComponent 那种)
 *   - SetSimulatePhysics + RecreatePhysicsState 都是 UE 异步操作, 不会引发 DDC 同步加载
 *   - 与 v220.1 的零兜底策略不冲突
 */
void ABaseWeapon::EnsureSkeletalMeshPhysicsDisabled(const TCHAR* CallerContext)
{
	if (!GetMeshComponent())
	{
		// 没 Mesh 组件的情况由 BeginPlay 单独处理 (Log Warning), 这里 silent return
		return;
	}

	UMeshComponent* MeshComp = GetMeshComponent();

	// 只对 SkeletalMesh 处理 (StaticMesh 没有物理引擎驱动的骨骼)
	USkeletalMeshComponent* SkelMesh = Cast<USkeletalMeshComponent>(MeshComp);
	if (!SkelMesh)
	{
		return; // StaticMesh 不需要处理
	}

	if (!SkelMesh->IsSimulatingPhysics())
	{
		return; // 已经关闭, 不重复 Log
	}

	UE_LOG(LogTemp, Error,
		TEXT("[v240.2][ABaseWeapon] EnsureSkeletalMeshPhysicsDisabled (%s): Weapon=%s 的 SkeletalMesh 开启了物理模拟, 强制关闭! ")
		TEXT("根因: BP 蓝图中 WeaponSkeletalMesh 的 Physics - Simulate 勾选了. ")
		TEXT("修复: 在 BP_%s 的 Components 面板取消 WeaponSkeletalMesh 的 Simulate Physics. ")
		TEXT("【v240.2 大厂架构】已强制关闭物理模拟防止 DefaultSceneRoot 下坠."),
		CallerContext,
		*GetName(),
		*GetClass()->GetName());

	SkelMesh->SetSimulatePhysics(false);
	SkelMesh->RecreatePhysicsState();
}


/**
 * EndPlay — 【P0 2026.07.07 大厂架构】销毁兜底
 *
 * 清理通道状态, 防:
 *   - 武器销毁时 bIsWeaponActive 残留为 true (Trace 永远激活)
 *   - 状态泄漏到下一关卡
 *   - GC 抖动 (虽然 bool 不需要 GC, 但显式清理是好习惯)
 *
 * 【2026.07.07 重构】bIsAIWeaponActive 已删除, 不再需要清理
 */
void ABaseWeapon::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 【v75 修复】删除旧版 bIsWeaponActive / IgnoreActors 字段清理 (v60 之前的死代码)
	//   新版 trace 完全由 Strategy (UMeleeSwStrategy / URangedLineStrategy) 管理
	//   BaseWeapon 不持有 trace 状态字段

	Super::EndPlay(EndPlayReason);
}


// ==========================================
// 2. 武器激活控制
// ==========================================

/**
 * StartWeaponTrace
 *
 * 开启武器刀刃伤害判定
 * 1. 标记激活
 * 2. 记录是轻击还是重击
 * 3. 清空黑名单
 * 4. 把自己和主人加入黑名单
 * 5. 记录刀刃初始位置（开刃瞬间立刻记下，防止从零点开刀）
 *
 * 【P0 2026.07.07 大厂架构重构】玩家和 AI 共用同一套 trace 路径
 *   旧版这里有"AI 激活期间忽略"分支 — 已删除
 *   新版: 不管是玩家蒙太奇还是 AI 蒙太奇, BP AnimNotify 调过来就开 trace
 *   谁是 AI 由 Owner Character 的 bIsCurrentlyAttackerAI 决定
 */
void ABaseWeapon::StartWeaponTrace(bool bIsHeavyAttack)
{
	// 【v75 架构清理】转发到 DamageStrategy
	// 旧版 (v1-v60.9): 本函数内联 trace 算法 + 字段状态
	// 新版 (v60.3+):   trace 算法由 UMeleeSwStrategy / URangedLineStrategy 实现
	// ANS_MeleeTraceState::NotifyBegin 调本方法 → 转发到 Strategy
	StartDamageTrace(bIsHeavyAttack);
}


void ABaseWeapon::StopWeaponTrace()
{
	// 【v75 架构清理】转发到 DamageStrategy
	StopDamageTrace();
}


/**
 * 【P0 2026.07.07 大厂架构重构 — 已删除】SetAIWeaponActive
 *
 * 原 AI 通道与 trace 互斥机制已废弃, 函数体内联到头文件中做空操作
 * 删除原因: 玩家/AI 共用同一 trace 路径, 不再需要"AI 屏蔽 trace"
 * 替代: ABaseCharacter::SetAttackerIsAI (从攻击者状态决定伤害来源)
 *
 * 此注释保留以示历史; 实际函数定义在 BaseWeapon.h 中 (deprecated no-op)
 */


// ==========================================
// 3. 核心刀刃扫掠判定 (v75 — 委托 Strategy)
// ==========================================

/**
 * Tick (v75 架构清理 — 删除重复 BoxTrace)
 *
 * 旧版 (v1-v60.9): ABaseWeapon::Tick 内联 BoxTrace 算法
 *   - 字段: bIsWeaponActive / bIsCurrentAttackHeavy / LastFrameStartLoc / LastFrameEndLoc / IgnoreActors
 *   - StartWeaponTrace / StopWeaponTrace 维护这些字段
 *   - Tick 每帧跑 BoxTrace
 *
 * 新版 (v60.3+): ABaseWeapon::Tick 委托 Strategy
 *   - Strategy 内部: bIsActive / bIsCurrentHeavy / LastFrameStartLoc / LastFrameEndLoc / IgnoreActors
 *   - StartDamageTrace / StopDamageTrace 转发到 DamageStrategy
 *   - Tick 仅在 Strategy 激活时调 TickDetection
 *
 * 大厂原则 - 单一真理源 + DRY:
 *   - 删除 BaseWeapon 内重复字段 → 真理源唯一 (Strategy 内部)
 *   - 删除 Tick 内重复 BoxTrace → 0 算法 (委托)
 *   - 旧 StartWeaponTrace / StopWeaponTrace 保留为转发壳, 兼容历史调用
 *
 * 【v95.1 大厂架构 P0 修复 — 服务器权威 + 客户端本地预测】
 *   根因 (从 Session1.log 定位):
 *     日志: "LogNet: UNetDriver::ProcessRemoteFunction: No owning connection for actor BP_Weapon_ShovelLarge_C_0.
 *            Function Server_ReportHit will not be processed."
 *     → 远端玩家 Pawn 的武器在 Session1 客户端跑了 trace, 命中后调 Server_ReportHit
 *       但武器 Actor 没有 owning connection → RPC 失败 → 伤害不传导
 *
 *   架构分析 (大厂原则 - 服务器权威):
 *     - 武器 Actor 是 bReplicates=true → 所有客户端都有这个武器实例
 *     - 所有客户端都跑 Tick → 所有客户端都跑 Strategy.TickDetection (BoxTrace)
 *     - 所有客户端都调 Server_ReportHit → 但只有本地控制 Pawn 的客户端能成功发 RPC
 *     - 远端玩家的武器 RPC 全部失败 (owning connection 不存在) → 客户端 trace 是"无用功"
 *     - 反过来: 服务器也是每个武器都跑 trace, 自己 trace 命中后调 Server_ReportHit
 *       Implementation (本地直接执行) → 扣血 1 次 (✓ 正确)
 *     - 客户端 trace 命中 + 发 RPC → 服务器 Implementation → 又扣血 1 次 (✗ 重复扣血)
 *
 *   修复方案 (大厂原则 - 服务器权威 + 客户端本地预测 + 职责分离):
 *     - 服务器 (Authority): 跑所有武器 trace (玩家 + AI, 权威命中判定 → 扣血)
 *     - 客户端 (本地玩家控制 Pawn): 跑本地玩家武器 trace (本地预测, 仅画视觉)
 *     - 客户端 (远端玩家控制 Pawn): 跳过 trace (避免 owning connection 失败, 也避免重复 RPC)
 *
 *   收益:
 *     1. 不再有 "No owning connection" 警告 — 根因消除
 *     2. 服务器权威: 扣血只在服务器, 不会出现客户端 RPC 成功 + 服务器自身 trace 的重复扣血
 *     3. 客户端 trace 仍有价值: 本地玩家 trace 命中后画 BoxTrace 视觉 (EDrawDebugTrace 仍工作)
 *     4. 远端玩家 trace 完全跳过: 节省 CPU + 不发无效 RPC
 *
 *   零重复扣血 (大厂原则):
 *     - 服务器跑 trace 命中 → 调 Server_ReportHit (本地直接执行 Implementation) → 扣血 1 次
 *     - 客户端不调 Server_ReportHit (Tick 跳过) → 没有重复扣血
 *     - 客户端仅画 BoxTrace 视觉 (EDrawDebugTrace 是本地行为, 不影响服务器)
 *
 *   互不冲突:
 *     - 服务器权威伤害: 命中 → ApplyPointDamage → HealthComponent.OnHealthChanged → OnRep_CurrentHealth → 客户端 HUD 同步
 *     - 客户端本地预测: BoxTrace 视觉立即反馈玩家 (命中时绿色 Box), 服务器同步后再扣血
 *
 *   【不影响其他调用方】Ranged 路径走 Server_StartFire RPC → 服务器端 PerformSingleShot → 不走 Tick 委托, 修复对 Ranged 无影响
 *   【不影响 AI】AI 是服务器控制, HasAuthority()=true → 服务器守卫通过 → AI 武器 trace 正常跑
 *   【不影响母体】母体路径 bUseOwnerMesh=true → TickDetection 走 Owner.Mesh 路径, 与武器路径完全对称
 */
void ABaseWeapon::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// 【v95.1 大厂架构 P0 修复】Tick 守卫 — 服务器权威 + 客户端本地预测
	//
	// 服务器跑所有 trace (玩家 + AI, 权威伤害判定)
	// 客户端只跑本地控制 Pawn 的 trace (本地预测, 仅画视觉)
	// 客户端跳过远端控制 Pawn 的 trace (避免 owning connection 失败 + 避免重复扣血)
	const ABaseCharacter* OwnerChar = Cast<ABaseCharacter>(GetOwner());
	if (!OwnerChar)
	{
		// 武器没 Owner → 异常状态, 跳过 trace (大厂原则 - 不浪费 CPU)
		return;
	}

	const bool bIsAuthority = HasAuthority();
	const bool bIsLocallyControlled = OwnerChar->IsLocallyControlled();

	// 服务器 (Authority) || 本地玩家控制 Pawn → 跑 trace
	// 远端玩家控制 Pawn → 跳过 (避免 RPC 失败 + 节省 CPU)
	if (!bIsAuthority && !bIsLocallyControlled)
	{
		return;
	}

	// v75 架构清理: 仅委托 Strategy.TickDetection (Strategy 内部自带"激活态"判断)
	if (DamageStrategy.GetObject())
	{
		DamageStrategy->TickDetection(this, DeltaTime);
	}
}


// ==========================================
// 4. 动画分配逻辑
// ==========================================

/**
 * GetAttackMontage
 *
 * 根据角色的连击进度（ComboIndex），返回对应的那一段挥刀动画
 * - 重击: 直接返回重击动画
 * - 轻击: 从数组里取对应索引（取余算法自动循环）
 *
 * @param bIsHeavy 是否为重击
 * @param ComboIndex 连击段数（轻击使用）
 * @return 对应的 AnimMontage
 */
UAnimMontage* ABaseWeapon::GetAttackMontage(bool bIsHeavy, int32 ComboIndex) const
{
	// 如果是重击，直接返回重击动画
	if (bIsHeavy)
	{
		return HeavyAttackMontage;
	}

	// 如果是轻击，从数组里挑出对应的连击动画
	if (LightAttackMontages.Num() > 0)
	{
		// 魔法取余算法: 如果你只配了 3 个轻击动画（索引 0,1,2）
		// 但玩家连击到了第 4 下（ComboIndex = 3），它会自动循环回第 1 个动画
		int32 SafeIndex = ComboIndex % LightAttackMontages.Num();
		return LightAttackMontages[SafeIndex];
	}

	return nullptr;
}


// ==========================================
// 5. Server RPC - 服务器执行伤害
// ==========================================

/**
 * Server_ReportHit_Implementation
 *
 * 服务器专用: 客户端报告命中后由服务器执行
 * 1. 服务器根据部位重新计算伤害（重击/轻击/爆头）
 * 2. 更新 LastKillMethod（供 HUD 显示击杀图标）
 * 3. 调用 UGameplayStatics::ApplyPointDamage 真正扣血
 *
 * 【P0 2026.07.07 大厂架构重构】AI 通道伤害来源 - 服务器权威读 ConfigSO.Damage
 *   - bIsAIDriven=true  (AI 路径):   服务器拉 AIController->GetEffectiveAttackDamage()
 *                                    (含难度缩放, 单一真值源)
 *   - bIsAIDriven=false (玩家路径): 走武器字段重算 (LightDamageBody/Head/Heavy)
 *
 * 为什么服务器读 ConfigSO.Damage 而不是 RPC 传入:
 *   - trace 端在 Tick 中跑, 没必要每帧查 AIController 浪费 CPU
 *   - 服务器端只在命中那 1 帧调用, 集中查 1 次更省
 *   - RPC 网络: 少传一个 float = 省带宽
 *   - 单一职责: 服务器权威决定伤害 (防客户端伪造 Damage 攻击 AI 通道)
 */
void ABaseWeapon::Server_ReportHit_Implementation(AActor* HitActor, float Damage, FVector HitLocation, FVector HitNormal, FName BoneName, bool bIsHeavy, bool bIsAIDriven)
{
	if (!HasAuthority()) return; // 双重保险: 确保只在服务器执行

	ABaseCharacter* Victim = Cast<ABaseCharacter>(HitActor);
	if (!Victim) return;

	// ==================================================================
	// 【v210 P0 大厂架构】结算状态服务器侧拒绝伤害报告 (深度防御)
	// ==================================================================
	// 业务规则 (用户 2026.08.07 明确):
	//   一整局游戏完全结束 → 进入结算页面 → 所有 AI 和玩家都不能攻击
	// 大厂原则:
	//   - 深度防御: ABaseCharacter::Server_StartFire 已拦截, 但 RPC 可能在中途到达
	//     (玩家在结算开始前 1ms 触发的 trace 在 100ms 后命中, RPC 仍在网络飞行)
	//   - 0 兜底: 单一真理源 IsInSettlement() (读 GameState->bInSettlement)
	const ABaseCharacter* AttackerCharSettlement = Cast<ABaseCharacter>(GetOwner());
	if (AttackerCharSettlement && AttackerCharSettlement->IsInSettlement())
	{
		UE_LOG(LogTemp, Display,
			TEXT("[BaseWeapon] Server_ReportHit: Attacker=%s 处于结算状态, 拒绝伤害报告 (深度防御)."),
			*AttackerCharSettlement->GetName());
		return;
	}

	// ============================================================
	// 【2026.07.11 P0 大厂架构】友军伤害守卫 (Friendly Fire Guard)
	//
	// 单一真理源: 走 FFactionTags::CanDamage 集中校验
	//   - 同阵营 (Friendly) → 拒绝扣血 + Log Warning (用户需求: 队友之间不能有队伤)
	//   - 任一阵营无效 → 拒绝扣血 + Log Error (强制修复 Pawn.FactionTag 同步链路)
	//   - 异阵营 + 均有效 → 通过
	//
	// 设计 (大厂原则):
	//   - 攻击者 Character: 从武器 Owner 拿 (this 是武器 Actor, Owner 才是攻击 Pawn)
	//   - 受击者 Character: Victim (Cast 已完成)
	//   - 不能放在 ApplyPointDamage 之后, 因为扣血一旦发生就要走 OnDeath 流程, 不可逆
	//   - 必须放在伤害计算之前 (这里 FinalDamage 还没算), 但唯一需要的是 Pawn.FactionTag
	//   - 命名 AttackerCharFFCheck, 避免与下方 bIsAIDriven 分支内的 AttackerChar 局部同名 (编译期红线)
	//
	// 【v40.9.6 P0 修复】服务器权威重算 bIsAIDriven — 真理源 = Controller 类型
	//   旧版 bIsAIDriven 由 RPC 传入 (依赖 Owner.bIsCurrentlyAttackerAI, 残留不可信)
	//   新版: 从 AttackerCharFFCheck->GetController() 重新判断 — AI 的 Controller 永远是 AAIController
	// ============================================================
	const ABaseCharacter* AttackerCharFFCheck = Cast<ABaseCharacter>(GetOwner());
	if (!AttackerCharFFCheck)
	{
		// 防御性兜底 (零兜底): 武器没有 Character Owner, 这是配置错误
		UE_LOG(LogTemp, Error,
			TEXT("[ABaseWeapon::Server_ReportHit] Weapon=%s 没有 ABaseCharacter Owner (类型=%s), 拒绝扣血 Victim=%s. "
			     "大厂原则: 武器必须挂在 Character 上, 由 Character 驱动攻击."),
			*GetName(),
			GetOwner() ? *GetOwner()->GetClass()->GetName() : TEXT("<null>"),
			*Victim->GetName());
		return;
	}

	// 【v40.9.6 P0】服务器权威重算 — Controller 类型 (运行时不可变)
	const AController* AttackerController = AttackerCharFFCheck->GetController();
	const bool bIsAIDriven_Authoritative = (AttackerController != nullptr)
		&& AttackerController->IsA(AAIController::StaticClass());

	// 【v85.1 诊断】存储友军检查结果
	const bool bCanDamage = FFactionTags::CanDamage(
		AttackerCharFFCheck->GetFactionTag(),
		Victim->GetFactionTag(),
		TEXT("ABaseWeapon::Server_ReportHit"),
		AttackerCharFFCheck->GetName(),
		Victim->GetName());

	if (!bCanDamage)
	{
	// 【v85.1 诊断】友军拒绝
	UE_LOG(LogTemp, Warning,
			TEXT("[ABaseWeapon::Server_ReportHit] 友军伤害拒绝: Attacker=%s (Faction=%s) -> Victim=%s (Faction=%s). 不扣血."),
			*AttackerCharFFCheck->GetName(),
			*AttackerCharFFCheck->GetFactionTag().ToString(),
			*Victim->GetName(),
			*Victim->GetFactionTag().ToString());
		return;
	}

	// 【v85.1 诊断】友军允许 — 继续扣血
	UE_LOG(LogTemp, Warning,
		TEXT("[ABaseWeapon::Server_ReportHit] 友军检查通过: Attacker=%s (Faction=%s) -> Victim=%s (Faction=%s). 继续扣血."),
		*AttackerCharFFCheck->GetName(),
		*AttackerCharFFCheck->GetFactionTag().ToString(),
		*Victim->GetName(),
		*Victim->GetFactionTag().ToString());

	// ============================================================
	// 【2026.07.11 P0 大厂架构】复活无敌期守卫 (Spawn Invincibility Guard) — 早期观察点
	//
	// 单一真理源:  HealthComponent->bIsInvincible 是无敌期唯一权威 (Layer 0 入口)
	//
	// 为什么不在这里 return:
	//   - HealthComponent::ApplyDamage 已会在被调前拦截 (Layer 0 单一真理源)
	//   - 这里仅做 Verbose 日志 (可观测性增强), 不重复实现拦截逻辑
	//   - 大厂原则 - 零重复: 拦截决策唯一, 仅分散点用作可观测性
	//
	// 实战效果:
	//   - 一旦 Victim 无敌期激活, 下游 ApplyPointDamage → ApplyDamage → 立即 return 0
	//   - 这里加 Verbose 是为了让 Verbose 调试时能区分"Faction 拒绝"还是"无敌期拒绝"
	// ============================================================
	// [v40 P0 修复] 必须用 ResolveHealthComponent() 而非裸字段 — Victim 可能被 BP archetype nullify
	if (UHealthComponent* VictimHC = Victim->ResolveHealthComponent())
	{
		if (VictimHC->IsInvincible())
		{
			UE_LOG(LogTemp, Verbose,
				TEXT("[ABaseWeapon::Server_ReportHit] Victim=%s 在无敌期, 已被 HealthComponent Layer 0 拦截, 剩余=%.2fs"),
				*Victim->GetName(),
				VictimHC->GetInvincibilityRemainingSeconds());
		}
	}

	// 服务器根据部位计算伤害
	float FinalDamage = 0.0f;

	// 【v40.9.6 P0 大厂架构】AI 通道: 服务器权威读 ConfigSO.Damage
	//
	// 旧版: bIsAIDriven 由客户端读 Owner.bIsCurrentlyAttackerAI 传入 → 残留 bug
	//   → AI 攻击蒙太奇被打断时, AIAttackComponent::OnAIAttackMontageEnded 早退出,
	//     SetAttackerIsAI(false) 没调用, 但下一帧新 AI 攻击的 box trace 命中玩家
	//     → IsAttackerAI() 返回残留 false → RPC 传 bIsAIDriven=false → 走玩家路径 → 60 伤害
	// 新版: bIsAIDriven 用服务器权威值 (bIsAIDriven_Authoritative), 从 Controller 类型判定
	//   → 真理源 = Actor 类型 (运行时不可变), 不依赖攻击状态 bool
	if (bIsAIDriven_Authoritative)
	{
		// 服务器从攻击者 Owner 拉 AIController → 难度缩放后的 ConfigSO.Damage
		if (const ABaseCharacter* AttackerChar = Cast<ABaseCharacter>(GetOwner()))
		{
			if (const AAIController* AIC = Cast<AAIController>(AttackerChar->GetController()))
			{
				if (const ABaseAIController* BaseAIC = Cast<ABaseAIController>(AIC))
				{
					FinalDamage = BaseAIC->GetEffectiveAttackDamage();
				}
				else
				{
					UE_LOG(LogTemp, Error,
						TEXT("ABaseWeapon::Server_ReportHit: AIC 不是 ABaseAIController (类型=%s), 无法读 ConfigSO.Damage — 攻击者=%s 受击者=%s"),
						AIC ? *AIC->GetClass()->GetName() : TEXT("<null>"),
						*AttackerChar->GetName(),
						*Victim->GetName());
					return;
				}
			}
			else
			{
				UE_LOG(LogTemp, Error,
					TEXT("ABaseWeapon::Server_ReportHit: Attacker=%s Controller 不是 AAIController (类型=%s), AI 通道无法解析"),
					*AttackerChar->GetName(),
					AttackerChar->GetController() ? *AttackerChar->GetController()->GetClass()->GetName() : TEXT("<null>"));
				return;
			}
		}
		else
		{
			UE_LOG(LogTemp, Error,
				TEXT("ABaseWeapon::Server_ReportHit: Weapon=%s Owner 不是 ABaseCharacter (类型=%s), AI 通道无法解析"),
				*GetName(),
				GetOwner() ? *GetOwner()->GetClass()->GetName() : TEXT("<null>"));
			return;
		}

		if (FinalDamage <= 0.0f)
		{
			UE_LOG(LogTemp, Error,
				TEXT("ABaseWeapon::Server_ReportHit: AI 通道读到的 FinalDamage<=0 (ConfigSO.Damage * DifficultyScale=%.2f), 拒绝造成 0 伤害攻击 — Attacker=%s Victim=%s"),
				FinalDamage,
				GetOwner() ? *GetOwner()->GetName() : TEXT("<none>"),
				*Victim->GetName());
			return;
		}

		// EKillMethod: AI 攻击不区分身体/头 (现阶段), 默认近战武器击杀
		LastKillMethod = EKillMethod::MeleeWeapon;
	}
	else if (Damage > 0.0f)
	{
		// 玩家路径但客户端传了非零 Damage — 不再兼容旧版, 直接走武器字段
		//   兜底逻辑会掩盖"玩家通道 + ConfigSO 不一致"的真因, 大厂架构已删除
		if (bIsHeavy)
		{
			FinalDamage = HeavyDamage;
		}
		else if (BoneName == FName("head"))
		{
			FinalDamage = LightDamageHead;
		}
		else
		{
			FinalDamage = LightDamageBody;
		}
		LastKillMethod = EKillMethod::MeleeWeapon;
	}
	else if (bIsHeavy)
	{
		FinalDamage = HeavyDamage;
		LastKillMethod = EKillMethod::MeleeWeapon;
	}
	else
	{
		// 玩家路径: 轻击按部位判定
		if (BoneName == FName("head"))
		{
			FinalDamage = LightDamageHead;
			LastKillMethod = EKillMethod::MeleeHeadshot;
		}
		else
		{
			FinalDamage = LightDamageBody;
			LastKillMethod = EKillMethod::MeleeWeapon;
		}
	}

	// 服务器执行伤害
	// 【P0 修复 2026.07.10】正确设置 FHitResult 的 BoneName 用于部位伤害判定
	FHitResult HitInfo(ForceInit);
	HitInfo.bBlockingHit = true;
	HitInfo.Location = HitLocation;
	HitInfo.Normal = HitNormal;
	HitInfo.BoneName = BoneName;
	HitInfo.ImpactPoint = HitLocation;
	HitInfo.ImpactNormal = HitNormal;

	// 【v85.1 诊断】确认 FinalDamage 合理
	if (FinalDamage <= 0.0f)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[ABaseWeapon::Server_ReportHit] FinalDamage<=0! Victim=%s Damage=%.1f. 修复: 检查 DT_WeaponInfo 伤害字段."),
			*Victim->GetName(), FinalDamage);
		return;
	}

	// UE5: 使用 HitInfo 的 GetActor() 会返回 Actor
	// ApplyPointDamage 内部会从 HitInfo 提取必要信息
	// 注意: HitInfo 的 Actor 成员可能需要通过碰撞查询自动填充
	// 对于手动构造的 HitInfo,DamageEvent 会正确传递 BoneName

	UGameplayStatics::ApplyPointDamage(
		Victim,
		FinalDamage,
		-HitNormal, // 攻击来向
		HitInfo,
		GetInstigatorController(),
		this,
		UDamageType::StaticClass()
	);

	UE_LOG(LogTemp, Warning, TEXT("[Damage] Server_ReportHit: %s -> %s, Damage=%.1f, Bone=%s, Heavy=%d, AIChannel=%d (AuthReCalc from Controller)"),
		*GetName(), *Victim->GetName(), FinalDamage, *BoneName.ToString(), bIsHeavy, bIsAIDriven_Authoritative ? 1 : 0);
}


// ==========================================
// 【P0】WithValidation 验证实现 - 防止客户端伪造伤害数据
// ==========================================

/**
 * Server_ReportHit_Validate
 * 校验: 伤害值在合法范围 (0~10000), HitActor 非空, 位置不是 NaN
 */
bool ABaseWeapon::Server_ReportHit_Validate(AActor* HitActor, float Damage, FVector HitLocation, FVector HitNormal, FName BoneName, bool bIsHeavy, bool bIsAIDriven)
{
	// 1. HitActor 必须存在
	if (!IsValid(HitActor))
	{
		return false;
	}

	// 2. 伤害值在合理范围 (防止负数/极大值注入)
	if (Damage < 0.0f || Damage > 10000.0f)
	{
		return false;
	}

	// 3. 位置/法线不能是 NaN/无限大
	if (HitLocation.ContainsNaN() || HitNormal.ContainsNaN())
	{
		return false;
	}

	// 4. 法线应该是单位向量 (容差 0.01)
	if (FMath::Abs(HitNormal.Size() - 1.0f) > 0.01f)
	{
		return false;
	}

	return true;
}


// ==========================================
// 6. 武器溶解入口 (2026.07.10 大厂 P0 重构)
// ==========================================

/**
 * StartDissolve
 *
 * 【大厂 P0 2026.07.10】武器死亡溶解 — 公开唯一入口
 *
 * 调用方: ABaseCharacter::DropAndFadeWeapon
 *
 * 设计 (类比身体 DissolveComponent):
 *   - 身体溶解: ABaseCharacter::DissolveComponent 驱动自己的身体 Mesh
 *   - 武器溶解: ABaseWeapon::WeaponDissolveComponent 驱动自己的武器 Mesh
 *   - 职责对等: 武器管自己的溶解, 角色不再穿透调用武器的材质
 *   - 零跨边界: 武器 Detach 后, DissolveComponent 随 Actor 继续工作
 *     → 不再依赖角色任何状态, 不再被角色销毁影响
 *
 * 销毁时序 (大厂规范):
 *   - 溶解由本方法启动, 内部 Tick 累加 DissolveAmount
 *   - UE 引擎 SetLifeSpan(3.0s) 由 ABaseCharacter::DropAndFadeWeapon 设置
 *   - 3 秒后 Actor 自动 Destroy, Component 随 Actor 自动清理
 *   - 即便 SetLifeSpan 时间比溶解时长短, 也会先 Destroy (UE 标准行为, 不兜底)
 */
void ABaseWeapon::StartDissolve()
{
	if (!WeaponDissolveComponent)
	{
		// 【大厂零兜底】组件未挂载: 显式失败
		UE_LOG(LogTemp, Error,
			TEXT("[ABaseWeapon] StartDissolve: WeaponDissolveComponent 未挂载 — 武器不会溶解 — Weapon=%s"),
			*GetName());
		return;
	}

	UE_LOG(LogTemp, Log,
		TEXT("[ABaseWeapon] StartDissolve: 委托给 WeaponDissolveComponent — Weapon=%s"),
		*GetName());

	// 委托给组件处理 (单一职责: 组件管溶解, Actor 转发入口)
	WeaponDissolveComponent->StartDissolve();
}


/**
 * IsDissolving — 查询武器是否正在溶解中
 *
 * 用于外部 (如角色死亡流程) 查询武器溶解状态
 */
bool ABaseWeapon::IsDissolving() const
{
	if (WeaponDissolveComponent)
	{
		return WeaponDissolveComponent->IsDissolving();
	}
	return false;
}


// ==========================================
// v70 缺失方法补充 (保守修复 — BaseWeapon.cpp 被还原后调用的遗留方法)
// ==========================================

/**
 * GetMeshType — 查询武器 Mesh 类型
 *
 * v61.2 重构: MeshType 字段由 WeaponAttachmentComponent 从 DT_WeaponInfo 写入.
 * GetMeshType() 直接返回字段值 (单一真理源).
 */
EWeaponMeshType ABaseWeapon::GetMeshType() const
{
	return MeshType;
}


/**
 * GetMeshComponent — 获取武器 Mesh 组件
 *
 * 【v61.3 重构】运行时按类型查找, 不依赖 C++ 字段
 * - StaticMeshComponent (Melee)
 * - SkeletalMeshComponent (Primary/Secondary)
 * - 两者都找不到 → Log Error + return nullptr
 */
UMeshComponent* ABaseWeapon::GetMeshComponent() const
{
	// 【v61.3 运行时查找】按类型查找 Mesh 组件
	// - StaticMeshComponent (Melee)
	// - SkeletalMeshComponent (Primary/Secondary)
	// - 两者都找不到 → Log Error + return nullptr
	//
	// 不做字段缓存 — 避免 TObjectPtr 类型转换问题，且 FindComponentByClass 哈希查找极快（< 0.001ms）
	if (UStaticMeshComponent* SMC = FindComponentByClass<UStaticMeshComponent>())
	{
		return SMC;
	}

	if (USkeletalMeshComponent* SkMC = FindComponentByClass<USkeletalMeshComponent>())
	{
		return SkMC;
	}

	// 零兜底: BP 没挂 Mesh 组件
	UE_LOG(LogTemp, Error,
		TEXT("[ABaseWeapon] GetMeshComponent: Weapon=%s 没有 Mesh 组件! ")
		TEXT("【v61.3 零兜底】修复: 在 BP 蓝图的 Components 面板添加 StaticMeshComponent (近战武器) 或 SkeletalMeshComponent (枪械)."),
		*GetName());
	return nullptr;
}


/**
 * 【v200.2.19 大厂架构 — 单一真理源】GetAttachedCharacter — 获取挂载武器的角色
 *
 * 旧版反模式:
 *   - 所有 RPC Implementation 用 GetOwner() 拿角色
 *   - UE 5.6 Owner 字段不是 Replicated → 客户端永远 null → 找角色失败 → 静默 return
 *   - 客户端捡起武器后无法开火 (问题 2)
 *   - 同一个 Action 5+ 处写 GetOwner() GetOwner() GetOwner() — 重复架构
 *
 * 新版 (v200.2.19) 大厂原则:
 *   - 集中到一个函数 GetAttachedCharacter()
 *   - 用 GetAttachParentActor() (Attach 关系是 Replicated, UE 5.6 默认同步 AttachParent)
 *   - 调用方统一用这个函数, 不再直接写 GetOwner() / GetAttachParentActor()
 *
 * 零兜底:
 *   - 武器没 attach (在地上) → return nullptr (调用方检查)
 *   - AttachParent 不是 ABaseCharacter → return nullptr (配置错误, 大厂应该 Log Error 但调用方决定)
 */
ABaseCharacter* ABaseWeapon::GetAttachedCharacter() const
{
	// 【v200.2.19 单一真理源】Attach 关系是 Replicated, 用 GetAttachParentActor() 拿
	//   - 客户端 attach 武器后 AttachParentActor = ABaseCharacter (正确)
	//   - 客户端没 attach (武器在地上) → AttachParentActor = null (合理)
	//   - 客户端 OnRep 刚触发 → AttachParentActor 可能瞬时 null (短暂, 重试)
	AActor* AttachParent = GetAttachParentActor();
	if (!AttachParent)
	{
		// 武器在地上或瞬时未挂 → 正常状态, 不 Log Error
		return nullptr;
	}

	ABaseCharacter* Char = Cast<ABaseCharacter>(AttachParent);
	if (!Char)
	{
		// 零兜底: AttachParent 不是 ABaseCharacter (配置错, 武器挂到非角色 Actor)
		//   - 此处不 Log Error, 调用方有责任检查返回 null
		//   - Log Error 会重复 (多个 RPC 入口都调到)
		return nullptr;
	}

	return Char;
}

/**
 * StartDamageTrace — 启动伤害追踪
 *
 * 【v73 大厂架构】转发到 DamageStrategy (单一真理源)
 *   - 调用方: PlayerComboComponent::EnableComboWindow (玩家挥刀中段)
 *   - 调用方: AIAttackComponent 蒙太奇中段 (未来)
 *   - 替代旧版"BP AnimNotify 调用 PerformDamageTrace" 的 BP 配置错反模式
 *     (大厂原则 - 零兜底: 不依赖 BP AnimNotify 是否配了)
 */
void ABaseWeapon::StartDamageTrace(bool bIsHeavy)
{
	// 【v85.2 大厂架构修复】懒创建 DamageStrategy
	//
	// 根因 (从 Session1.log 定位):
	//   日志: "[ABaseWeapon] StartDamageTrace: DamageStrategy 未注册 → 拒绝开启"
	//   → DamageStrategy 在客户端是 nullptr
	//
	// 根因分析:
	//   1. 服务器 Spawn 武器时, WeaponAttachmentComponent::SpawnAndEquipWeapon 调 SetDamageStrategy(NewObject<UMeleeSwStrategy>)
	//   2. DamageStrategy 字段虽然有 UPROPERTY(Replicated), 但 TScriptInterface 内部持有的 UObject 引用
	//      在跨 Actor 引用复制的场景下可能失效 (Weapon 自身是复制过来的引用, 但内部的 subobject 引用未必同步)
	//   3. 客户端 ANS_MeleeTraceState::NotifyBegin 调 Weapon->StartDamageTrace
	//      → DamageStrategy.GetObject() == nullptr → 直接 return
	//   4. 后果: 客户端没有任何 melee trace, 既没有伤害也没有视觉
	//
	// 架构设计 (大厂原则 - 单一职责):
	//   - DamageStrategy 的创建应该只在 Spawn 链路中发生一次 (WeaponAttachmentComponent)
	//   - 但如果 Spawn 链路有 bug (复制问题), 不能让 StartDamageTrace 静默失败
	//   - 懒创建 = 确保功能可用的防御性措施, 而非"兜底"
	//   - 最终还是要修 Spawn 链路, 让 DamageStrategy 在 Spawn 时正确创建
	//
	if (!DamageStrategy.GetObject())
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[ABaseWeapon] StartDamageTrace: DamageStrategy 为空, 懒创建. Weapon=%s MeshType=%d. "
			     "【v85.2 懒创建】确保客户端能正常工作. "
			     "但这说明 Spawn 链路有问题 — DamageStrategy 应该在 Spawn 时注入, 不是这里才创建."),
			*GetName(),
			static_cast<int32>(MeshType));

		// 按 MeshType 创建对应的 Strategy
		switch (MeshType)
		{
		case EWeaponMeshType::Melee:
			{
				UObject* NewStrategy = NewObject<UMeleeSwStrategy>(this);
				DamageStrategy.SetObject(NewStrategy);
				DamageStrategy.SetInterface(Cast<IWeaponDamageStrategy>(NewStrategy));
				break;
			}
		case EWeaponMeshType::Primary:
		case EWeaponMeshType::Secondary:
			{
				UObject* NewStrategy = NewObject<URangedLineStrategy>(this);
				DamageStrategy.SetObject(NewStrategy);
				DamageStrategy.SetInterface(Cast<IWeaponDamageStrategy>(NewStrategy));
				break;
			}
		default:
			UE_LOG(LogTemp, Error,
				TEXT("[ABaseWeapon] StartDamageTrace: MeshType 为 None, 无法懒创建 DamageStrategy. Weapon=%s."),
				*GetName());
			return;
		}

		if (!DamageStrategy.GetObject())
		{
			UE_LOG(LogTemp, Error,
				TEXT("[ABaseWeapon] StartDamageTrace: DamageStrategy 懒创建失败. Weapon=%s."),
				*GetName());
			return;
		}

		UE_LOG(LogTemp, Log,
			TEXT("[ABaseWeapon] StartDamageTrace: DamageStrategy 懒创建成功. Weapon=%s Strategy=%s"),
			*GetName(),
			*DamageStrategy.GetObject()->GetClass()->GetName());
	}

	if (!DamageStrategy->StartTrace(this, bIsHeavy))
	{
		UE_LOG(LogTemp, Error,
			TEXT("[ABaseWeapon] StartDamageTrace: Strategy->StartTrace 失败. Weapon=%s 【v73 零兜底】检查 Mesh / Socket / BP 配置."),
			*GetName());
	}
}

/**
 * StopDamageTrace — 停止伤害追踪
 *
 * 【v73 大厂架构修复】转发到 DamageStrategy (与 StartDamageTrace 对称)
 *   - 旧版 (v70-v72) 是 no-op stub, 导致 EnableComboWindow 中 StartTrace 后永远没停
 *   - 修复: 转发到 DamageStrategy->StopTrace(this)
 *
 * 调用方:
 *   - PlayerComboComponent::OnPlayerAttackMontageEnded (蒙太奇结束)
 *   - AIAttackComponent::OnAIAttackMontageEnded (蒙太奇结束)
 *   - BaseWeapon::OnWeaponChanged (切换武器槽位, 强制停当前检测)
 */
void ABaseWeapon::StopDamageTrace()
{
	if (!DamageStrategy.GetObject())
	{
		// 【v73 零兜底】若 Strategy 已 Disposed (Destroy 链路), 静默 — 幂等
		return;
	}

	DamageStrategy->StopTrace(this);
}

/**
 * Multicast_PlayFireMontage — 多播播放开火蒙太奇
 *
 * v70 保守修复: 旧版是 Weapon RPC, 新版 Strategy 内.
 * 这里做 no-op stub (蒙太奇播放由 WeaponFireComponent::PerformSingleShot 内部处理).
 */
void ABaseWeapon::Multicast_PlayFireMontage_Implementation()
{
	// 【v200.2.19 大厂架构 — 单一真理源】修复问题 2
	//   旧版 (v200.2.18 之前) 一个致命问题:
	//     GetOwner() 客户端永远 null (Owner 字段不是 Replicated) → 找不到 CharMesh → 静默 return
	//     → 客户端"捡起武器后无法开火" — 因为 Owner 永远 null
	//   新版 (v200.2.19):
	//     用 GetAttachedCharacter() (单一真理源) 替代 GetOwner()
	//     - GetAttachedCharacter 内部用 GetAttachParentActor() (Attach 关系是 Replicated)
	//     - 客户端 attach 武器后 AttachParentActor = 角色 (正确) → 找到 CharMesh → 播动画
	//
	//   服务器本地进程守卫 (撤销 v200.2.19 草稿的 HasAuthority + IsLocallyControlled → return):
	//     - PerformSingleShot 内部不直接 Montage_Play, 只调 MulticastPlayFireMontage → Multicast_PlayFireMontage_Implementation
	//     - 服务器本地 Implementation **必须** 播动画 — 这是 host 玩家看到自己开火的唯一路径
	//     - 跳过守卫 = host 玩家看不到自己开火动画 → 业务错误
	//     - 撤销这个守卫, 服务器本地继续播动画

	// 【v200.2.19 单一真理源】用 GetAttachedCharacter() 替代 GetOwner() / GetAttachParentActor()
	ABaseCharacter* Char = GetAttachedCharacter();
	if (!Char)
	{
		// 大厂原则: 武器在地上 (没 attach) → 客户端 Implementation 拿不到角色 → 静默 return
		// 不 Log Error — 这是正常状态 (武器 dropped, 等玩家捡起)
		// 但如果是"挂载中突然拿不到" → 可能是 Attach 关系未同步, 下一帧会自动恢复
		return;
	}

	USkeletalMeshComponent* CharMesh = Char->GetMesh();
	if (!CharMesh)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[BaseWeapon][v200.2.19] Multicast_PlayFireMontage_Implementation: CharMesh 为空! "
			     "Weapon=%s Char=%s. 修复: 检查角色 BP 的 Mesh 是否为 SkeletalMesh."),
			*GetName(), *Char->GetName());
		return;
	}

	UAnimInstance* AnimInst = CharMesh->GetAnimInstance();
	if (!AnimInst)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[BaseWeapon][v200.2.19] Multicast_PlayFireMontage_Implementation: AnimInstance 为空! Weapon=%s CharMesh=%s. 修复: 检查角色 BP 的 Mesh 是否为 SkeletalMesh."),
			*GetName(), *CharMesh->GetName());
		return;
	}

	UAnimMontage* FireMontage = nullptr;
	if (UWeaponFireComponent* FC = WeaponFireComponent)
	{
		FireMontage = FC->GetFireMontageHip();
	}

	if (!FireMontage)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[BaseWeapon][v200.2.19] Multicast_PlayFireMontage_Implementation: FireMontage 为空! Weapon=%s. 修复: DT_WeaponInfo 中 BQ001 行的 FireMontage_Ironsights 必须配置 Anim_Fire_Rifle_Ironsights_Montage."),
			*GetName());
		return;
	}

	// 播放射击蒙太奇（非重叠模式）
	AnimInst->Montage_Play(FireMontage, 1.0f, EMontagePlayReturnType::MontageLength, 0.0f, false);
	UE_LOG(LogTemp, Log,
		TEXT("[BaseWeapon][v200.2.19] Multicast_PlayFireMontage_Implementation: 武器=%s 播放射击蒙太奇=%s"),
		*GetName(), *FireMontage->GetName());
}

/**
 * Multicast_PlayReloadMontage — 多播播放换弹蒙太奇
 *
 * v72 大厂架构修复: RPC 在 Weapon 上，动画要播在角色骨骼网格体上
 */
void ABaseWeapon::Multicast_PlayReloadMontage_Implementation()
{
	// 【v200.2.19 大厂架构 — 单一真理源】同 Multicast_PlayFireMontage
	//   - 用 GetAttachedCharacter() (单一真理源) 替代 GetOwner()
	//   - 服务器本地继续跑 (没守卫) — 这是换弹动画在 host 玩家屏幕上的唯一路径
	ABaseCharacter* Char = GetAttachedCharacter();
	if (!Char)
	{
		// 同 FireMontage: 武器在地上/瞬时未挂载 → 静默 return
		return;
	}

	USkeletalMeshComponent* CharMesh = Char->GetMesh();
	if (!CharMesh)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[BaseWeapon][v200.2.19] Multicast_PlayReloadMontage_Implementation: CharMesh 为空. Weapon=%s Char=%s."),
			*GetName(), *Char->GetName());
		return;
	}

	UAnimInstance* AnimInst = CharMesh->GetAnimInstance();
	if (!AnimInst)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[BaseWeapon][v200.2.19] Multicast_PlayReloadMontage_Implementation: AnimInstance 为空! Weapon=%s."),
			*GetName());
		return;
	}

	UAnimMontage* ReloadMontage = nullptr;
	if (UWeaponFireComponent* FC = WeaponFireComponent)
	{
		ReloadMontage = FC->GetReloadMontageHip();
	}

	if (!ReloadMontage)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[BaseWeapon][v200.2.19] Multicast_PlayReloadMontage_Implementation: ReloadMontage 为空! Weapon=%s. 修复: DT_WeaponInfo 中 BQ001 行的 ReloadMontage_Ironsights 必须配置 Anim_Reload_Rifle_Ironsights_Montage."),
			*GetName());
		return;
	}

	AnimInst->Montage_Play(ReloadMontage, 1.0f, EMontagePlayReturnType::MontageLength, 0.0f, false);
	UE_LOG(LogTemp, Log,
		TEXT("[BaseWeapon][v200.2.19] Multicast_PlayReloadMontage_Implementation: 武器=%s 播放换弹蒙太奇=%s"),
		*GetName(), *ReloadMontage->GetName());
}


/**
 * Multicast_PlayFireTraceVisual_Implementation — 客户端同步显示服务器 trace 视觉
 *
 * 【v208.5 大厂架构修复】移除 HasAuthority() 守卫
 *
 * 调用方: URangedLineStrategy::PerformSingleShot / UMeleeSwStrategy::TickDetection (服务器命中时)
 *
 * 架构:
 *   - 服务器权威 trace (单次权威判定)
 *   - 服务器 trace 完后调 Multicast → 所有客户端画 DrawDebugLine (红线/绿线)
 *   - 所有进程都画 (包括 host 玩家的客户端), 不重复
 *
 * 【v208.5 修复内容】移除旧版 HasAuthority() 守卫:
 *   - 旧版: HasAuthority() + IsLocallyControlled() → 跳过 (防止 host 重复画)
 *   - 根因: 客户端调 Multicast 时 HasAuthority=false, 守卫不触发, 画线正常
 *   - 但服务器 Implementation 先执行 (DrawDebugLine), 然后客户端再执行 (DrawDebugLine)
 *   - 问题可能是: 两边都画同一组线 (服务器 EDrawDebugTrace + 客户端 DrawDebugLine) 时视觉重叠?
 *   - 无论如何, 移除守卫让所有进程都画同一组 DrawDebugLine, 保证客户端可见
 *
 * @param StartLoc trace 起点
 * @param EndLoc   trace 终点
 * @param bHit     是否命中 (决定颜色: 命中绿, 未命中红)
 * @param HitLoc   命中点 (bHit=true 时有效)
 */
void ABaseWeapon::Multicast_PlayFireTraceVisual_Implementation(FVector StartLoc, FVector EndLoc, bool bHit, FVector HitLoc)
{
	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[BaseWeapon] ★ Multicast_PlayFireTraceVisual_Implementation: World 无效! Weapon=%s. "
			     "【v208.6 诊断】"),
			*GetName());
		return;
	}

	// 颜色: 命中绿, 未命中红 (与服务器 trace 一致)
	const FColor LineColor = bHit ? FColor::Green : FColor::Red;

	// ★ 诊断日志 — 让用户能看到射线可视化是否在客户端被调用
	UE_LOG(LogTemp, Display,
		TEXT("[BaseWeapon] ★ Multicast_PlayFireTraceVisual_Implementation: Weapon=%s bHit=%d "
		     "Start=%s End=%s. "
		     "【v208.6】如果在服务器日志看到这个 = Multicast 被调用了. "
		     "如果在客户端日志看到这个 = 客户端收到了 Multicast, 应该正在画线."),
		*GetName(), bHit ? 1 : 0,
		*StartLoc.ToCompactString(), *EndLoc.ToCompactString());

	// 1. 主线: StartLoc → (bHit ? HitLoc : EndLoc)
	// 【v85.x 修复】bPersistentLines=true 让射线持续显示，不再是 0.5s 闪烁
	const FVector LineEnd = bHit ? HitLoc : EndLoc;
	DrawDebugLine(
		World,
		StartLoc,
		LineEnd,
		LineColor,
		true,                        // bPersistentLines: 持续显示 (不再消失)
		3.0f,                        // LifeTime: 3秒后消失 (足够看清)
		0,                           // DepthPriority
		2.0f                         // Thickness: 2.0 粗线 (比之前 1.5 更明显)
	);

	// 2. 命中十字 (bHit=true 时画, 让客户端清楚"打中了哪里")
	// 【v85.x 修复】命中十字也持续显示
	if (bHit)
	{
		const float CrossSize = 10.0f; // 10cm 十字
		DrawDebugLine(World, HitLoc - FVector(CrossSize, 0, 0), HitLoc + FVector(CrossSize, 0, 0), FColor::Green, true, 3.0f, 0, 2.0f);
		DrawDebugLine(World, HitLoc - FVector(0, CrossSize, 0), HitLoc + FVector(0, CrossSize, 0), FColor::Green, true, 3.0f, 0, 2.0f);
		DrawDebugLine(World, HitLoc - FVector(0, 0, CrossSize), HitLoc + FVector(0, 0, CrossSize), FColor::Green, true, 3.0f, 0, 2.0f);
	}
}


/**
 * 【v200.2.20 大厂架构 — 撤销 v200.2.18 的错误做法, 但保留物理同步 Multicast】
 *
 * v200.2.18 错误:
 *   - 直接调 SetSimulatePhysics(true) → 客户端 Mesh 单独模拟, RootComponent 走 ReplicateMovement
 *   - 两个 transform 冲突 → Mesh 视觉错乱 / 消失
 *
 * v200.2.20 修复 (本版本):
 *   - 先检查 RootComponent 是否是 Mesh → 不是则 SetRootComponent(Mesh) 强制修复
 *   - 然后才 SetSimulatePhysics(true) → Mesh = RootComponent = 物理 Body, 完美对齐
 *   - 客户端镜像服务器物理状态, ReplicateMovement 同步物理 transform
 *
 * 调用方: WeaponDropComponent::StartDroppedState (服务器)
 *
 * 大厂原则 — 零兜底:
 *   - 客户端没 MeshComponent → Log Error + return
 *   - Mesh 不是 RootComponent → 强制 SetRootComponent 修复 (不允许跳过)
 *   - 服务器本地: HasAuthority() + GetAttachParentActor() != null (已 attach) → return (已经在玩家手上不需要物理)
 */
// ==========================================
// 掉落物理同步 (v200.3.4 完整重构)
// ==========================================

void ABaseWeapon::Multicast_DropWeapon_Implementation(FVector_NetQuantize NewLocation, FRotator NewRotation,
	FVector LaunchVelocity, FVector LaunchAngularVelocity)
{
	// 【v200.3.12 终极修复 — 完全符合 UE 官方推荐方案】
	//
	// 根因分析 (基于 UE 5.6 官方文档 + Epic Developer Community Forums):
	//   1. 服务器: SetSimulatePhysics(true) + SetPhysicsLinearVelocity
	//   2. 客户端之前在 Multicast 中调用 SetActorLocationAndRotation(..., TeleportPhysics)
	//   3. 【致命】SetActorLocationAndRotation 与 ReplicateMovement 产生竞争:
	//      - 客户端收到 Multicast 时, 武器被 teleport 到旧位置 (物理启动前捕获的 SpawnLocation)
	//      - 33ms 后 ReplicateMovement 同步服务器物理真实位置
	//      - 中间窗口期: 武器短暂"陷进地面" (服务器物理在反弹, 客户端 teleport 到旧位置)
	//   4. 监听服务器不陷进去: 同一进程直接读物理引擎状态, 跳过 ReplicateMovement
	//
	// v200.3.12 修复 (UE 5.6 官方方案 — Lyra/Paragon/Fortnite 同款):
	//   - 客户端 【完全不调用】 SetActorLocationAndRotation
	//   - 位置由 UE 内置 ReplicateMovement 自动同步 (服务器权威)
	//   - 客户端 Multicast 只做 "状态变更": Detach + 设置碰撞 (ReplicateMovement 不同步这些)
	//   - 代价: 武器掉落的前 0~33ms (1 个 ReplicateMovement 周期) 客户端可能看到武器仍在角色手上
	//     - 但 33ms 后第一次 ReplicateMovement 同步 → 武器跳到服务器物理计算位置
	//     - 视觉上: "枪突然从手上消失, 落到地上" — 符合扔枪预期
	//
	// 官方参考:
	//   - https://dev.epicgames.com/documentation/unreal-engine/networked-physics-overview
	//   - https://dev.epicgames.com/community/learning/tutorials/1YBK/unreal-engine-persistent-multiplayer-inventory-system-2-persistent-pickups
	//   - Lyra 武器系统: bReplicates=true + bReplicateMovement=true + 服务器 SetSimulatePhysics

	if (!GetRootComponent())
	{
		UE_LOG(LogTemp, Error,
			TEXT("[v200.3.12][BaseWeapon] Multicast_DropWeapon: RootComponent 为空! Weapon=%s."),
			*GetName());
		return;
	}

	// 1. DetachFromActor (清除 AttachParent)
	//   ReplicateMovement 不会同步 Attach 状态 — 必须客户端手动 Detach
	if (GetAttachParentActor())
	{
		DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);
	}

	// 2. 获取 Mesh Component
	UPrimitiveComponent* MeshComp = Cast<UPrimitiveComponent>(GetMeshComponent());
	if (!MeshComp)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[v200.3.12][BaseWeapon] Multicast_DropWeapon: MeshComponent 为空! Weapon=%s."),
			*GetName());
		return;
	}

	// 3. 设置碰撞 (ReplicateMovement 不会同步碰撞设置)
	//   客户端必须和服务器保持一致, 否则客户端 overlap 触发不了 (玩家走过去不会触发拾取)
	MeshComp->SetCollisionEnabled(ECollisionEnabled::QueryAndPhysics);

	// 【v200.3.11 修复】设置 ObjectType 为 WorldDynamic
	//   原因: 武器在地上是"世界动态物体", 不是 "PhysicsBody" (物理模拟中的物体)
	//   WorldDynamic 与其他 WorldDynamic 物体 (其他掉落物) 正确互动
	MeshComp->SetCollisionObjectType(ECC_WorldDynamic);

	// 关键通道配置
	MeshComp->SetCollisionResponseToAllChannels(ECR_Ignore);
	MeshComp->SetCollisionResponseToChannel(ECC_WorldStatic, ECR_Block);    // 阻挡地面
	MeshComp->SetCollisionResponseToChannel(ECC_WorldDynamic, ECR_Block);   // 与其他世界动态物体阻挡
	MeshComp->SetCollisionResponseToChannel(ECC_Pawn, ECR_Overlap);         // 玩家 overlap 触发拾取
	MeshComp->SetCollisionResponseToChannel(ECC_PhysicsBody, ECR_Overlap);   // 物理体 overlap
	MeshComp->SetCollisionResponseToChannel(ECC_Visibility, ECR_Block);      // 射线检测命中
	MeshComp->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);         // 忽略相机
	MeshComp->SetCollisionResponseToChannel(ECC_Destructible, ECR_Ignore);   // 忽略可破坏物

	// 开启 GenerateOverlapEvents，确保 overlap 事件能触发
	MeshComp->SetGenerateOverlapEvents(true);

	// 4. 【v200.4.1 大厂架构修复】客户端启动 ProjectileMovementComponent (本地预测)
	//   - 服务器传 LaunchVelocity (世界空间) → 客户端启动本地 PMC
	//   - 客户端 PMC 预测轨迹, 服务器权威同步真实位置 (ReplicateMovement)
	//   - 服务器冻结位置后调 Multicast_FreezeWeaponTransform 关闭客户端 PMC
	//   - 大厂原则: 服务器和客户端 PMC 状态最终一致 (落地都关闭)
	//
	// 【v200.4.1 P0 修复】SetVelocityInLocalSpace 而非直接赋值 Velocity
	//   根因: PMC 默认 bInitialVelocityInLocalSpace=true → 直接赋值 Velocity 会被当 LOCAL 处理
	//   修复: 先把世界速度转换到 Actor Local, 再用 SetVelocityInLocalSpace (UE 官方推荐 API)
	if (UProjectileMovementComponent* PMC = FindComponentByClass<UProjectileMovementComponent>())
	{
		PMC->SetActive(true);
		PMC->SetUpdatedComponent(MeshComp);

		const FVector LaunchVelLocal = GetActorTransform().InverseTransformVectorNoScale(LaunchVelocity);
		PMC->SetVelocityInLocalSpace(LaunchVelLocal);

		PMC->UpdateComponentVelocity();
		// 注意: BounceVelocityStopSimulatingThreshold 由 BP 子类在编辑器里配置 (服务器通过 ReplicateMovement 同步到客户端的状态变化)
	}

	UE_LOG(LogTemp, Verbose,
		TEXT("[v200.4.1][BaseWeapon] Multicast_DropWeapon: 客户端 Detach + 启动 PMC 预测完成. WorldVelocity=%s, Weapon=%s"),
		*LaunchVelocity.ToCompactString(),
		*GetName());
}

// ==========================================
// Multicast_FreezeWeaponTransform_Implementation
//
// 【v200.4 大厂架构 — UE 官方最优解】
// 武器落地后冻结位置 (服务器+客户端都冻结), 解决 "客户端陷进地面" 问题
//
// 大厂原则: 服务器-客户端状态必须完全一致
//   - ReplicateMovement 复制 Actor.Location 但不复制 "冻结" 状态
//   - 所以 PMC 关闭 / 物理关闭 必须靠 Multicast 同步
//   - Reliable: 一次性事件, 不能丢
// ==========================================
void ABaseWeapon::Multicast_FreezeWeaponTransform_Implementation(FVector_NetQuantize FinalLocation, FRotator FinalRotation)
{
	// 1. 关闭 ProjectileMovementComponent (如果存在)
	//   - BP 子类若配了 PMC, 客户端本地 PMC 也在跑 (本地预测)
	//   - 服务器冻结后, 客户端 PMC 仍可能继续计算 → 必须显式关闭
	if (UProjectileMovementComponent* PMC = FindComponentByClass<UProjectileMovementComponent>())
	{
		PMC->StopMovementImmediately();
		PMC->SetActive(false);
		PMC->SetUpdatedComponent(nullptr); // UE 5 官方推荐: 解耦 UpdatedComponent, 防止后续 tick 重新激活

		UE_LOG(LogTemp, Verbose,
			TEXT("[v200.4][BaseWeapon] Multicast_FreezeWeaponTransform: 客户端关闭 PMC. Weapon=%s"),
			*GetName());
	}

	// 2. 关闭物理引擎 (如果还在跑)
	//   - 服务器已关闭, 但客户端可能仍 simulate (理论上不会, 防御性)
	//   - 【v240.2 大厂架构】委托 EnsureSkeletalMeshPhysicsDisabled 统一处理 (单一真理源, 零重复)
	if (UMeshComponent* MeshComp = GetMeshComponent())
	{
		if (MeshComp->IsSimulatingPhysics())
		{
			EnsureSkeletalMeshPhysicsDisabled(TEXT("Multicast_FreezeWeaponTransform"));
		}
	}

	// 3. 【v240.8 大厂架构 P0 — SetActorLocationAndRotation 反算污染 RelativeTransform 真根因修复】
	//
	//   根因 (v240.7 修复后用户反馈仍未解决):
	//     - v240.7 Promote 把 Mesh 的 BP 默认 RelXform 烤到 Root (默认 (0,0,0))
	//     - v240.7 Reset 把 Mesh 子组件清零 (现在 Mesh=(0,0,0))
	//     - 抛下 → DetachFromActor(KeepWorldTransform) → Root 和 Mesh 都 RelXform=(0,0,0)
	//     - 服务器 SettleWeaponOnGround 调 SetActorLocation(SettledLocation) (line 724)
	//       → UE 反算: RelativeLocation = (WorldLoc - ParentWorldLoc) = SettledLocation - 0 = SettledLocation ❌
	//     - Multicast_FreezeWeaponTransform 调 SetActorLocationAndRotation(FinalLoc, FinalRot)
	//       → 同样反算 → 客户端 Root.RelativeLocation = FinalLoc ❌
	//     - 结果: Root 显示 WorldLoc 值 (-3258, 3196, 1299), Mesh 还是 (0,0,0) → "不一致" 完全就是 UE 的反算
	//
	//   修复 (绕开反算路径):
	//     - 直接调 RootComponent->SetWorldLocation + SetWorldRotation
	//     - UE 这两个 API 只改 WorldTransform,**不触发反算 RelativeTransform**
	//     - 抛下后 Root.RelativeTransform 保持 (0,0,0) (Promote 后值)
	//     - Mesh.RelativeTransform 保持 (0,0,0) (Reset 后值)
	//     - 两者保持一致 — 真正的"在地上姿态"
	//
	//   【大厂原则 — 单一真理源 + 显式状态机】
	//     - "抛下后 Root 和 Mesh 都是 (0,0,0)" 是 v240.7 建立的"在地上姿态"约定
	//     - Multicast_FreezeWeaponTransform 必须遵守这个约定, 不能用 SetActor* 触发反算
	//     - 服务器 Spawn 路径: SettleWeaponOnGround 也应该用 SetWorldLocation (而不是 SetActorLocation)
	//
	//   拾起路径 (CancelDroppedState):
	//     - 拾起时 AttachToComponent(socket) → UE 自动重算 RelativeTransform
	//     - 拾起后 ApplyAttachmentRuntime 走 SetWorld* 路径 → Root 和 Mesh 一起到 socket + DT 偏移
	//     - 不受本修复影响
	USceneComponent* RootComp = GetRootComponent();
	if (!RootComp)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[v240.8] Multicast_FreezeWeaponTransform: RootComponent 为空! 无法冻结位置 — Weapon=%s. "
			     "请检查 BP_Weapon_*.uasset 的 Components 面板是否设了 RootComponent."),
			*GetName());
		return;
	}

	// 【v240.8 关键】直接设 WorldTransform, 不触发反算 RelativeTransform
	//   - 抛下后 AttachParent = nullptr (DetachFromActor 后)
	//   - WorldTransform 设置不影响 RelativeTransform (AttachParent=nullptr 时两者数值上相同)
	//   - 但通过 SetWorld* 显式设置, UE 内部不会调用 UpdateComponentToWorld 反算
	//   - 配合 v240.7 Promote: Root 保持 (0,0,0), Mesh 保持 (0,0,0) — 两者一致
	RootComp->SetWorldLocation(FVector(FinalLocation), false, nullptr, ETeleportType::TeleportPhysics);
	RootComp->SetWorldRotation(FinalRotation, false, nullptr, ETeleportType::TeleportPhysics);

	// 【v240.10 大厂架构 P0 — 抛下后强制 Mesh RelXform 对齐 Root】
	//   根因:
	//     - BP_Weapon_AK47 的 Mesh 组件 BP 默认 RelXform = (-9, 178, -120) (美术在地上配的"地上姿态")
	//     - DetachFromActor(KeepWorldTransform) 后 Mesh 恢复 BP 默认 → Mesh.WorldLoc = Root.WorldLoc + Mesh.RelXform
	//     - 即使 Root 已贴地, Mesh 也因为自己的 BP 默认 RelXform 偏移 -120cm 而沉入地下
	//     - 用户反馈: "Mesh 进入地下 (-19, 178, -115)"
	//   修复 (硬约束):
	//     - 抛下后, 显式把所有非弹夹 Mesh 子组件的 RelativeTransform 设为 identity
	//     - Mesh.WorldLoc = Root.WorldLoc + identity = Root.WorldLoc → 完全贴地
	//     - 与 v240.10 Promote 共同构成 "抛下时 Root 和 Mesh 都 (0,0,0,identity)" 硬约束
	TArray<UMeshComponent*> MeshComps;
	GetComponents<UMeshComponent*>(MeshComps);
	int32 ResetMeshCount = 0;
	for (UMeshComponent* MeshComp : MeshComps)
	{
		if (!MeshComp) continue;
		// 跳过弹夹 — 与 Promote/Reset 白名单一致
		if (MeshComp->GetName().Contains(TEXT("Magazine")))
		{
			continue;
		}
		// 硬对齐 identity — 这是大厂原则: "扔枪时 Root 和 Mesh 姿态完全一致"
		MeshComp->SetRelativeTransform(FTransform::Identity);
		++ResetMeshCount;
	}

	UE_LOG(LogTemp, Display,
		TEXT("[v240.10][BaseWeapon] Multicast_FreezeWeaponTransform: 客户端已冻结武器位置 (绕开反算路径) + Mesh 子组件硬对齐 identity. Weapon=%s, Loc=%s, RootComp=%s, ResetMeshCount=%d"),
		*GetName(),
		*FVector(FinalLocation).ToCompactString(),
		*RootComp->GetName(),
		ResetMeshCount);
}
	//   - 服务器 CancelDroppedState: SetSimulatePhysics(false) + AttachToComponent
	//   - 客户端: ReplicateMovement 收到 attach transform, 自动恢复物理状态 (false)

// Server_StartFire — 服务器开始开火 (枪械)
// v82 大厂架构修复 — 客户端射线参数:
//   - 旧 (v70-v81) 反模式: Server_StartFire 无参数, 服务器 WeaponFireComponent::StartFire()
//     → 内部用 PC->GetViewportSize/Deproject → 服务器对远端玩家 PC 失败 → trace 永远失败
//   - 新 (v82): 客户端玩家路径 OnFirePressed 用 HUD Crosshair 算出射线 → RPC 传给服务器
//     → 服务器用客户端射线做权威 trace (防作弊)
//     → 兼容 AI 路径: 传零向量 = AI fallback (Strategy 内部用 BaseAimRotation)
void ABaseWeapon::Server_StartFire_Implementation(const FVector_NetQuantize& ClientRayOrigin, const FVector_NetQuantizeNormal& ClientRayDirection)
{
	// 【v85.1 诊断】入口 Log — 确认 RPC 是否被调用
	UE_LOG(LogTemp, Warning,
		TEXT("[BaseWeapon] Server_StartFire_Implementation ENTER. Weapon=%s Origin=%s Dir=%s"),
		*GetName(),
		*ClientRayOrigin.ToCompactString(),
		*ClientRayDirection.ToCompactString());

	if (!WeaponFireComponent)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[BaseWeapon] Server_StartFire: WeaponFireComponent 为空! Weapon=%s. 修复: 检查 BP_Weapon_Xxx 是否删了 UWeaponFireComponent 子对象."),
			*GetName());
		return;
	}
	WeaponFireComponent->StartFire(ClientRayOrigin, ClientRayDirection);
}

bool ABaseWeapon::Server_StartFire_Validate()
{
	return true;
}

/**
 * Server_StopFire — 服务器停止开火
 *
 * v70 保守修复: 同上.
 */
void ABaseWeapon::Server_StopFire_Implementation()
{
	if (!WeaponFireComponent)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[BaseWeapon] Server_StopFire: WeaponFireComponent 为空! Weapon=%s. 修复: 检查 BP_Weapon_Xxx 是否删了 UWeaponFireComponent 子对象."),
			*GetName());
		return;
	}
	WeaponFireComponent->StopFire();
}

bool ABaseWeapon::Server_StopFire_Validate()
{
	return true;
}

/**
 * Server_StartReload — 服务器开始换弹
 *
 * v70 保守修复: 同上.
 */
void ABaseWeapon::Server_StartReload_Implementation()
{
	if (!WeaponFireComponent)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[BaseWeapon] Server_StartReload: WeaponFireComponent 为空! Weapon=%s. 修复: 检查 BP_Weapon_Xxx 是否删了 UWeaponFireComponent 子对象."),
			*GetName());
		return;
	}
	WeaponFireComponent->StartReload();
}

bool ABaseWeapon::Server_StartReload_Validate()
{
	return true;
}

/**
 * ResolveMagazineSkeletalMesh — 获取弹匣 SkeletalMesh 组件
 *
 * v72 大厂架构修复: 旧版 (v70) 只写了 stub, 对所有武器返回 nullptr.
 * 新版 (v72): 从武器上按名称查找 MagazineSkeletal 组件.
 *
 * 调用方: AnimNotify_DetachMagazine / AnimNotify_AttachMagazine
 *
 * 用户配置: 枪械武器 BP 中必须有一个 SkeletalMeshComponent 子对象, 组件名 = "MagazineSkeletal"
 */
USkeletalMeshComponent* ABaseWeapon::ResolveMagazineSkeletalMesh() const
{
	// 遍历所有 SkeletalMeshComponent, 找名字含 "Magazine" 的
	TArray<USkeletalMeshComponent*> AllSkeletalMeshes;
	GetComponents(AllSkeletalMeshes);
	for (USkeletalMeshComponent* SkeletalMesh : AllSkeletalMeshes)
	{
		if (SkeletalMesh && SkeletalMesh->GetName().Contains(TEXT("Magazine")))
		{
			return SkeletalMesh;
		}
	}

	// 未找到 → 零兜底: Log Error, 让用户修复 BP 配置
	UE_LOG(LogTemp, Error,
		TEXT("[ABaseWeapon] ResolveMagazineSkeletalMesh: 武器 '%s' 上找不到 MagazineSkeletal 组件! 修复: 在 BP_Weapon_AK47 中添加 SkeletalMeshComponent 子对象, 命名为包含 'Magazine' 的名称."),
		*GetName());
	return nullptr;
}

/**
 * RestoreMagazineToWeapon — 将弹匣重新挂回武器
 *
 * v72 大厂架构修复: 旧版 (v70) 是 no-op, 弹匣永远留在手上.
 * 新版 (v72): 把弹匣从角色手上挂回武器的 MagazineSocket 插槽.
 *
 * 调用方: AnimNotify_AttachMagazine
 *
 * 协议: 弹匣 Detach 时用 KeepWorld 保持世界坐标, Attach 时同样用 KeepWorld 保持世界坐标,
 * 这样弹匣从手上回到武器时位置不变 (动画驱动换弹视觉效果).
 */
void ABaseWeapon::RestoreMagazineToWeapon()
{
	USkeletalMeshComponent* MagazineMesh = ResolveMagazineSkeletalMesh();
	if (!MagazineMesh)
	{
		// ResolveMagazineSkeletalMesh 内部已 Log Error, 这里只 return
		return;
	}

	// 获取武器的主骨骼网格体
	USkeletalMeshComponent* WeaponMesh = Cast<USkeletalMeshComponent>(GetMeshComponent());
	if (!WeaponMesh)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[BaseWeapon] RestoreMagazineToWeapon: Weapon Mesh 为空! Weapon=%s"),
			*GetName());
		return;
	}

	// 策略 1: 武器 Skeleton 上有 MagazineSocket → 挂到插槽
	FName TargetSocket = FName("MagazineSocket");
	if (WeaponMesh->DoesSocketExist(TargetSocket))
	{
		// Detach 当前挂载 (保持世界坐标, 防止弹匣跳变)
		FDetachmentTransformRules DetachRules(EDetachmentRule::KeepWorld, true);
		MagazineMesh->DetachFromComponent(DetachRules);

		// 挂到武器的 MagazineSocket
		FAttachmentTransformRules AttachmentRules(EAttachmentRule::KeepWorld, true);
		MagazineMesh->AttachToComponent(WeaponMesh, AttachmentRules, TargetSocket);

		UE_LOG(LogTemp, Log,
			TEXT("[BaseWeapon] RestoreMagazineToWeapon: 弹匣 '%s' 挂回武器 '%s' 的 MagazineSocket. FinalParent='%s' FinalSocket='%s'"),
			*MagazineMesh->GetName(),
			*GetName(),
			*MagazineMesh->GetAttachParent()->GetName(),
			*MagazineMesh->GetAttachSocketName().ToString());
		return;
	}

	// 策略 2: 没有 MagazineSocket，MagazineSkeletal 直接作为武器 Mesh 的子组件
	// → 挂回武器 Mesh 根节点（用户蓝图里的结构）
	FDetachmentTransformRules DetachRules(EDetachmentRule::KeepWorld, true);
	MagazineMesh->DetachFromComponent(DetachRules);

	FAttachmentTransformRules AttachmentRules(EAttachmentRule::KeepWorld, true);
	MagazineMesh->AttachToComponent(WeaponMesh, AttachmentRules, NAME_None);

	UE_LOG(LogTemp, Log,
		TEXT("[BaseWeapon] RestoreMagazineToWeapon: 弹匣 '%s' 挂回武器 '%s' 根节点 (无 MagazineSocket). FinalParent='%s'"),
		*MagazineMesh->GetName(),
		*GetName(),
		*MagazineMesh->GetAttachParent()->GetName());
}
