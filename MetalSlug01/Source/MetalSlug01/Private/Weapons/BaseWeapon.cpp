// 版权声明：在项目设置的描述页面填写您的版权信息。

// ==========================================
// 头文件包含区
// ==========================================
// 引入本类头文件
#include "Weapons/BaseWeapon.h"

// 引入静态网格组件（武器模型）
#include "Components/StaticMeshComponent.h"

// 引入角色基类（用于伤害计算）
#include "Characters/BaseCharacter.h"
// 为了获取主人的视角和位置，必须包含 Character 头文件
#include "GameFramework/Character.h"
// 射线检测核心库
#include "Kismet/KismetSystemLibrary.h"
// 扣血指令库（为下一步真正扣血做准备）
#include "Kismet/GameplayStatics.h"


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
 */
void ABaseWeapon::StopWeaponTrace()
{
	bIsWeaponActive = false;
	IgnoreActors.Empty();
}


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
		// 1. 打印打中了谁
		if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor::Red, FString::Printf(TEXT("砍中了: %s"), *HitResult.GetActor()->GetName()));

		// 2. 将这名受害者加入黑名单，这刀还没收回之前，不会再判定他
		IgnoreActors.Add(HitResult.GetActor());

		// 【修复】: 所有机器都报告命中，由服务器统一执行伤害
		// 客户端直接报告命中，Server RPC 会自动路由到服务器执行
		Server_ReportHit(
			HitResult.GetActor(),
			0.0f, // 伤害值由服务器根据部位重新计算
			HitResult.ImpactPoint,
			HitResult.ImpactNormal,
			HitResult.BoneName,
			bIsCurrentAttackHeavy
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
 */
void ABaseWeapon::Server_ReportHit_Implementation(AActor* HitActor, float Damage, FVector HitLocation, FVector HitNormal, FName BoneName, bool bIsHeavy)
{
	if (!HasAuthority()) return; // 双重保险: 确保只在服务器执行

	ABaseCharacter* Victim = Cast<ABaseCharacter>(HitActor);
	if (!Victim) return;

	// 服务器根据部位计算伤害
	float FinalDamage = 0.0f;
	if (bIsHeavy)
	{
		FinalDamage = HeavyDamage;
		LastKillMethod = EKillMethod::MeleeWeapon;
	}
	else
	{
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
	// 注意: HitFromDirection 是从受伤者角度看过去的"攻击来向"，与 HitNormal 方向相反
	FHitResult HitInfo(Victim, Victim->GetMesh(), HitLocation, HitNormal);
	UGameplayStatics::ApplyPointDamage(
		Victim,
		FinalDamage,
		-HitNormal, // 攻击来向
		HitInfo,
		GetInstigatorController(),
		this,
		UDamageType::StaticClass()
	);

	UE_LOG(LogTemp, Warning, TEXT("[Damage] Server_ReportHit: %s -> %s, Damage=%.1f, Bone=%s, Heavy=%d"),
		*GetName(), *Victim->GetName(), FinalDamage, *BoneName.ToString(), bIsHeavy);
}


// ==========================================
// 【P0】WithValidation 验证实现 - 防止客户端伪造伤害数据
// ==========================================

/**
 * Server_ReportHit_Validate
 * 校验: 伤害值在合法范围 (0~10000), HitActor 非空, 位置不是 NaN
 */
bool ABaseWeapon::Server_ReportHit_Validate(AActor* HitActor, float Damage, FVector HitLocation, FVector HitNormal, FName BoneName, bool bIsHeavy)
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
