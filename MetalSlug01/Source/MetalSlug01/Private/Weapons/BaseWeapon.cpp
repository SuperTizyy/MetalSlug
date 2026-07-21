// 版权声明：在项目设置的描述页面填写您的版权信息。

// ==========================================
// 头文件包含区
// ==========================================
// 引入本类头文件
#include "Weapons/BaseWeapon.h"

// 引入 Mesh 组件基类 (GetMeshComponent 返回 UMeshComponent*)
#include "Components/MeshComponent.h"
// 引入 SkeletalMesh 组件 (ResolveMagazineSkeletalMesh 返回类型)
#include "Components/SkeletalMeshComponent.h"

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

	WeaponDissolveComponent = CreateDefaultSubobject<UWeaponDissolveComponent>(TEXT("WeaponDissolveComponent"));
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
	// 防御性清场: 即便逻辑层(OnAIAttackMontageEnded / Die)漏了清理,
	// EndPlay 是最后一道防线, 保证 Actor 销毁时通道状态是干净的
	bIsWeaponActive = false;
	IgnoreActors.Empty();

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
	// 【v61.3 零兜底】Mesh 必须在开刃前就就位
	UMeshComponent* Mesh = GetMeshComponent();
	if (!Mesh)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[ABaseWeapon] StartWeaponTrace: Weapon=%s 没有 Mesh 组件 — 拒绝开刃. ")
			TEXT("【v61.3 零兜底】修复: 在 BP 蓝图 Components 面板添加 StaticMeshComponent (近战) 或 SkeletalMeshComponent (枪械)."),
			*GetName());
		return;
	}

	bIsWeaponActive = true;
	bIsCurrentAttackHeavy = bIsHeavyAttack;
	IgnoreActors.Empty();
	IgnoreActors.Add(this);
	IgnoreActors.Add(GetOwner());

	// 开刃的瞬间，立刻记录下刀刃的初始位置
	LastFrameStartLoc = Mesh->GetSocketLocation(FName("TraceStart"));
	LastFrameEndLoc = Mesh->GetSocketLocation(FName("TraceEnd"));
}


/**
 * StopWeaponTrace
 *
 * 关闭武器刀刃伤害判定
 * 同时清空黑名单
 *
 * 【P0 2026.07.07】不会关闭 AI 通道,两个通道独立管理
 *   AI 通道状态由 SetAIWeaponActive 显式控制
 */
void ABaseWeapon::StopWeaponTrace()
{
	bIsWeaponActive = false;
	IgnoreActors.Empty();
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
// 3. 核心刀刃扫掠判定
// ==========================================

/**
 * Tick
 *
 * 武器激活时每帧进行刀刃扫掠追踪
 * 核心黑科技: 缝合上一帧与当前帧的真空区（防止挥刀速度太快漏判）
 *
 * 流程:
 * 1. 获取刀刃起点和终点的世界坐标
 * 2. 计算上一帧和当前帧的中心点
 * 3. 构建长方体检测盒（贴合刀身方向）
 * 4. BoxTraceSingle 从上一帧中心滑向当前帧中心
 * 5. 命中则加入黑名单并调用 Server_ReportHit
 * 6. 记录当前帧位置作为下一帧的"上一帧"
 */
void ABaseWeapon::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// 如果没开刃，直接退出，节省性能
	if (!bIsWeaponActive) return;

	// 【P0 2026.07.07 大厂架构重构】删除原"AI 通道屏蔽 trace"分支
	//
	// 旧逻辑 (2026.07.07 之前): AI 攻击时 SetAIWeaponActive(true) 把 trace 拦住,
	//   然后由 OnAIRequestAttack_Simple 直接调 Server_ReportHit 在第一帧扣血
	//   → 一次挥刀两种伤害 (虽然能互斥屏蔽 trace, 但 AI 抬手瞬间就扣血)
	//
	// 新逻辑 (2026.07.07 大厂架构): 玩家和 AI **完全共用同一套 trace 路径**
	//   - 蒙太奇 AnimNotify 触发 StartWeaponTrace → Tick 跑 BoxTrace
	//   - 命中 → Server_ReportHit 读 Owner.bIsCurrentlyAttackerAI 决定走 ConfigSO 还是 LightDamageBody
	//   - 真正的"刀碰到了才扣血", 跟物理现实一致
	// ============================================================

	// 获取雷达探头的当前世界坐标
	UMeshComponent* Mesh = GetMeshComponent();
	if (!Mesh)
	{
		return;
	}
	FVector StartLoc = Mesh->GetSocketLocation(FName("TraceStart"));
	FVector EndLoc = Mesh->GetSocketLocation(FName("TraceEnd"));

	// 如果两点重合或未找到插槽，绝对不能往下执行矩阵计算
	if ((EndLoc - StartLoc).IsNearlyZero()) return;

	// ==========================================
	// 【核心黑科技】: 缝合上一帧与当前帧的真空区
	// ==========================================

	// 1. 算出上一帧刀刃的中心点，和这一帧刀刃的中心点
	FVector LastMid = (LastFrameStartLoc + LastFrameEndLoc) * 0.5f;
	FVector CurrentMid = (StartLoc + EndLoc) * 0.5f;

	// 2. 算出刀身到底有多长
	float BladeLength = FVector::Distance(StartLoc, EndLoc);

	// 3. 构建一个长方体检测盒 (长和宽是 15 的剑气厚度，高度是刀身长度的一半)
	FVector BoxHalfSize = FVector(15.0f, 15.0f, BladeLength * 0.5f);

	// 绝对不要用 MakeFromZ 猜角度，直接拿插槽最真实的物理旋转
	// 这样检测盒的长宽高，将完美且死死地贴合你的大锤/刀身，在空中绝对不会发生自转抽搐
	FRotator BoxRotation = Mesh->GetSocketRotation(FName("TraceStart"));

	// 碰撞检测结果
	FHitResult HitResult;

	// 4. 见证奇迹: 把这个检测盒，从上一帧的中心，滑向这一帧的中心
	bool bHit = UKismetSystemLibrary::BoxTraceSingle(
		this,
		LastMid,     // 起点: 上一帧的中心
		CurrentMid,  // 终点: 这一帧的中心
		BoxHalfSize, // 检测盒的体积
		BoxRotation, // 检测盒的倾斜角度
		UEngineTypes::ConvertToTraceType(ECC_Visibility),
		false,
		IgnoreActors,
		EDrawDebugTrace::ForDuration, // 保持开启红线调试
		HitResult,
		true
	);

	if (bHit && HitResult.GetActor())
	{
		// 1. 将这名受害者加入黑名单，这刀还没收回之前，不会再判定他
		IgnoreActors.Add(HitResult.GetActor());

		// 【修复】: 所有机器都报告命中，由服务器统一执行伤害
		// 客户端直接报告命中，Server RPC 会自动路由到服务器执行
		// ============================================================
		// 【P0 2026.07.07 大厂架构重构】伤害来源按攻击者决定
		//
		// 旧逻辑: 传 bIsAIDriven=false (硬编码)
		//   → 所有 trace 命中都按"玩家"路径扣 LightDamageBody/Head
		//   → AI 攻击读武器字段, 完全无视 ConfigSO.Damage
		//
		// 新逻辑 (玩家/AI 完全共用同一套 trace 路径):
		//   从 Owner Character 读 bIsCurrentlyAttackerAI:
		//     - true  → bIsAIDriven=true (走 AI 通道, 用 ConfigSO.Damage)
		//     - false → bIsAIDriven=false (走玩家通道, 用 LightDamageBody/Head)
		//
		// 关键: 单点决策 — 不再在 Server_ReportHit 里加新 if-else
		// ============================================================
		bool bAIDriven = false;
		if (const ABaseCharacter* OwnerChar = Cast<ABaseCharacter>(GetOwner()))
		{
			bAIDriven = OwnerChar->IsAttackerAI();
		}

		Server_ReportHit(
			HitResult.GetActor(),
			0.0f, // 伤害值由服务器根据部位重新计算 (或 AI 通道读 ConfigSO.Damage)
			HitResult.ImpactPoint,
			HitResult.ImpactNormal,
			HitResult.BoneName,
			bIsCurrentAttackHeavy,
			bAIDriven  // 【P0 2026.07.07】从攻击者 Character 动态读取
		);
	}

	// 【极其重要】: 当前帧结算完毕，把当前位置存入记忆，变成下一帧的"上一帧"
	LastFrameStartLoc = StartLoc;
	LastFrameEndLoc = EndLoc;
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

	if (!FFactionTags::CanDamage(
			AttackerCharFFCheck->GetFactionTag(),
			Victim->GetFactionTag(),
			TEXT("ABaseWeapon::Server_ReportHit"),
			AttackerCharFFCheck->GetName(),
			Victim->GetName()))
	{
		// CanDamage 内部已 Log Error/Warning + 指出修复路径, 这里仅静默 return
		// 大厂原则: 不重复打日志, 让 CanDamage 单一日志源
		return;
	}

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
 * StopDamageTrace — 停止伤害追踪
 *
 * v70 保守修复: 旧版 StopDamageTrace 是 Weapon 公开方法.
 * 新版近战武器用 MeleeSwStrategy::StopTrace, 这里 stub 给玩家/AI 路径做兼容.
 */
void ABaseWeapon::StopDamageTrace()
{
	// no-op: 近战伤害追踪由 MeleeSwStrategy 管理
}

/**
 * Multicast_PlayFireMontage — 多播播放开火蒙太奇
 *
 * v70 保守修复: 旧版是 Weapon RPC, 新版 Strategy 内.
 * 这里做 no-op stub (蒙太奇播放由 WeaponFireComponent::PerformSingleShot 内部处理).
 */
void ABaseWeapon::Multicast_PlayFireMontage_Implementation()
{
	// 【v72 大厂架构修复】RPC 在 Weapon 上，但动画要播在角色骨骼网格体上
	//   - 旧 (v70): no-op → 射击/换弹动画永远不播
	//   - 新 (v72): 获取 Owner (角色) 的骨骼网格体，播放蒙太奇
	USkeletalMeshComponent* CharMesh = nullptr;
	if (ABaseCharacter* Char = Cast<ABaseCharacter>(GetOwner()))
	{
		CharMesh = Char->GetMesh();
	}
	if (!CharMesh)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[BaseWeapon] Multicast_PlayFireMontage_Implementation: Owner 不是 ABaseCharacter 或 Mesh 为空! Weapon=%s Owner=%s. 修复: 检查武器挂载链路."),
			*GetName(),
			GetOwner() ? *GetOwner()->GetName() : TEXT("nullptr"));
		return;
	}

	UAnimInstance* AnimInst = CharMesh->GetAnimInstance();
	if (!AnimInst)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[BaseWeapon] Multicast_PlayFireMontage_Implementation: AnimInstance 为空! Weapon=%s CharMesh=%s. 修复: 检查角色 BP 的 Mesh 是否为 SkeletalMesh."),
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
			TEXT("[BaseWeapon] Multicast_PlayFireMontage_Implementation: FireMontage 为空! Weapon=%s. 修复: DT_WeaponInfo 中 BQ001 行的 FireMontage_Ironsights 必须配置 Anim_Fire_Rifle_Ironsights_Montage."),
			*GetName());
		return;
	}

	// 播放射击蒙太奇（非重叠模式）
	AnimInst->Montage_Play(FireMontage, 1.0f, EMontagePlayReturnType::MontageLength, 0.0f, false);
	UE_LOG(LogTemp, Log,
		TEXT("[BaseWeapon] Multicast_PlayFireMontage_Implementation: 武器=%s 播放射击蒙太奇=%s"),
		*GetName(), *FireMontage->GetName());
}

/**
 * Multicast_PlayReloadMontage — 多播播放换弹蒙太奇
 *
 * v72 大厂架构修复: RPC 在 Weapon 上，动画要播在角色骨骼网格体上
 */
void ABaseWeapon::Multicast_PlayReloadMontage_Implementation()
{
	USkeletalMeshComponent* CharMesh = nullptr;
	if (ABaseCharacter* Char = Cast<ABaseCharacter>(GetOwner()))
	{
		CharMesh = Char->GetMesh();
	}
	if (!CharMesh)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[BaseWeapon] Multicast_PlayReloadMontage_Implementation: Owner 不是 ABaseCharacter 或 Mesh 为空! Weapon=%s Owner=%s."),
			*GetName(),
			GetOwner() ? *GetOwner()->GetName() : TEXT("nullptr"));
		return;
	}

	UAnimInstance* AnimInst = CharMesh->GetAnimInstance();
	if (!AnimInst)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[BaseWeapon] Multicast_PlayReloadMontage_Implementation: AnimInstance 为空! Weapon=%s."),
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
			TEXT("[BaseWeapon] Multicast_PlayReloadMontage_Implementation: ReloadMontage 为空! Weapon=%s. 修复: DT_WeaponInfo 中 BQ001 行的 ReloadMontage_Ironsights 必须配置 Anim_Reload_Rifle_Ironsights_Montage."),
			*GetName());
		return;
	}

	AnimInst->Montage_Play(ReloadMontage, 1.0f, EMontagePlayReturnType::MontageLength, 0.0f, false);
	UE_LOG(LogTemp, Log,
		TEXT("[BaseWeapon] Multicast_PlayReloadMontage_Implementation: 武器=%s 播放换弹蒙太奇=%s"),
		*GetName(), *ReloadMontage->GetName());
}

/**
 * Server_StartFire — 服务器开始开火
 *
 * v70 保守修复: 旧版是 Weapon RPC, 新版 WeaponFireComponent 内.
 * 这里 stub 做 no-op (真实实现在 WeaponFireComponent::Server_StartFire).
 */
void ABaseWeapon::Server_StartFire_Implementation()
{
	if (!WeaponFireComponent)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[BaseWeapon] Server_StartFire: WeaponFireComponent 为空! Weapon=%s. 修复: 检查 BP_Weapon_Xxx 是否删了 UWeaponFireComponent 子对象."),
			*GetName());
		return;
	}
	WeaponFireComponent->StartFire();
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
