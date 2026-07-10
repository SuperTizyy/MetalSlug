// 版权声明：在项目设置的描述页面填写您的版权信息。

// ==========================================
// 头文件包含区
// ==========================================
// 引入本类头文件
#include "Weapons/BaseWeapon.h"

// 引入静态网格组件（武器模型）
#include "Components/StaticMeshComponent.h"

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
// 【2026.07.11 P0 大厂架构】友军伤害守卫 (单一真理源入口)
#include "Data/Faction/FactionTags.h"


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

	// 初始化武器模型组件
	WeaponMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("WeaponMesh"));
	RootComponent = WeaponMesh;

	// 【关键】: 关闭武器本身的物理碰撞
	// 在刀战游戏中，武器模型本身是不参与物理碰撞的（否则会挡住角色的移动或摄像机）
	// 伤害判定纯靠我们自己写的代码射线（Trace）来计算
	WeaponMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// 初始化一组"默认小刀"的初始数据
	LightDamageBody = 20.0f;
	LightDamageHead = 80.0f;
	HeavyDamage = 999.0f;
	AttackRange = 150.0f; // 150 厘米的攻击距离
	AttackRadius = 15.0f; // 判定球体的半径，稍微大一点防止"人体描边"

	PrimaryActorTick.bCanEverTick = true;

	// 修改 Tick 组
	// 强制这把武器在所有动画和物理彻底更新完毕后 (PostUpdateWork) 再执行 Tick
	// 保证每一帧读取的 Socket 坐标是绝对精准、毫无延迟的
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;

	// 【P0 2026.07.10 大厂架构重构】武器溶解组件
	// 历史 (v23 及以前): 武器溶解由 ABaseCharacter::DissolveComponent 驱动 (跨边界)
	// 新架构: 武器管自己 — 自己的 Mesh 自己的 DissolveComponent, 职责对等, 零跨边界
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
	bIsWeaponActive = true;
	bIsCurrentAttackHeavy = bIsHeavyAttack; // 记住这刀是轻是重
	IgnoreActors.Empty(); // 每次挥刀前，清空黑名单

	// 把自己和持有武器的主人加进黑名单，防止自己砍死自己
	IgnoreActors.Add(this);
	IgnoreActors.Add(GetOwner());

	// 初始化起始位置，防止0点开刀
	if (WeaponMesh)
	{
		// 开刃的瞬间，立刻记录下刀刃的初始位置
		LastFrameStartLoc = WeaponMesh->GetSocketLocation(FName("TraceStart"));
		LastFrameEndLoc = WeaponMesh->GetSocketLocation(FName("TraceEnd"));
	}
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

	// 获取雷达探头的当前世界坐标 (假设你的武器组件叫 WeaponMesh)
	FVector StartLoc = WeaponMesh->GetSocketLocation(FName("TraceStart"));
	FVector EndLoc = WeaponMesh->GetSocketLocation(FName("TraceEnd"));

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
	FRotator BoxRotation = WeaponMesh->GetSocketRotation(FName("TraceStart"));

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
