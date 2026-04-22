#include "Weapons/BaseWeapon.h"
#include "Components/StaticMeshComponent.h"
// 为了获取主人的视角和位置，必须包含 Character 头文件
#include "GameFramework/Character.h" 
// 射线检测核心库
#include "Kismet/KismetSystemLibrary.h" 
// 扣血指令库（为下一步真正扣血做准备）
#include "Kismet/GameplayStatics.h" 

ABaseWeapon::ABaseWeapon()
{
	
	// 开启网络同步，因为这把武器要挂在玩家手上让所有人看见
	bReplicates = true;

	// 初始化武器模型组件
	WeaponMesh = CreateDefaultSubobject<UStaticMeshComponent>(TEXT("WeaponMesh"));
	RootComponent = WeaponMesh;
	
	// 【关键】：关闭武器本身的物理碰撞！
	// 在刀战游戏中，武器模型本身是不参与物理碰撞的（否则会挡住角色的移动或摄像机），
	// 伤害判定纯靠我们自己写的代码射线（Trace）来计算。
	WeaponMesh->SetCollisionEnabled(ECollisionEnabled::NoCollision);

	// 初始化一组“默认小刀”的初始数据
	LightDamageBody = 20.0f;
	LightDamageHead = 80.0f;
	HeavyDamage = 999.0f;
	AttackRange = 150.0f; // 150 厘米的攻击距离
	AttackRadius = 15.0f; // 判定球体的半径，稍微大一点防止“人体描边”
	
	PrimaryActorTick.bCanEverTick = true;
	
	//修改 Tick 组！
	// 强制这把武器在所有动画和物理彻底更新完毕后 (PostUpdateWork) 再执行 Tick。
	// 保证每一帧读取的 Socket 坐标是绝对精准、毫无延迟的！
	PrimaryActorTick.TickGroup = TG_PostUpdateWork;
}

void ABaseWeapon::StartWeaponTrace(bool bIsHeavyAttack)
{
	bIsWeaponActive = true;
	bIsCurrentAttackHeavy = bIsHeavyAttack; // 记住这刀是轻是重
	IgnoreActors.Empty(); // 每次挥刀前，清空黑名单

	// 把自己和持有武器的主人加进黑名单，防止自己砍死自己
	IgnoreActors.Add(this);
	IgnoreActors.Add(GetOwner()); 
	
	//根据传进来的参数，动态决定这一刀的真实伤害！
	//BaseDamage = bIsHeavyAttack ? HeavyDamage : LightDamage;
	
	// 初始化起始位置，防止0点开刀
	if (WeaponMesh)
	{
		//开刃的瞬间，立刻记录下刀刃的初始位置！
		LastFrameStartLoc = WeaponMesh->GetSocketLocation(FName("TraceStart"));
		LastFrameEndLoc = WeaponMesh->GetSocketLocation(FName("TraceEnd"));
	}
}

void ABaseWeapon::StopWeaponTrace()
{
	bIsWeaponActive = false;
	IgnoreActors.Empty(); 
}

void ABaseWeapon::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// 如果没开刃，直接退出，节省性能
	if (!bIsWeaponActive) return;

	// 获取雷达探头的当前世界坐标 (假设你的武器组件叫 WeaponMesh)
	FVector StartLoc = WeaponMesh->GetSocketLocation(FName("TraceStart"));
	FVector EndLoc = WeaponMesh->GetSocketLocation(FName("TraceEnd"));
	
	// 如果两点重合或未找到插槽，绝对不能往下执行矩阵计算！
	if ((EndLoc - StartLoc).IsNearlyZero()) return;
	
	// ==========================================
	// 【核心黑科技】：缝合上一帧与当前帧的真空区！
	// ==========================================
	
	// 1. 算出上一帧刀刃的中心点，和这一帧刀刃的中心点
	FVector LastMid = (LastFrameStartLoc + LastFrameEndLoc) * 0.5f;
	FVector CurrentMid = (StartLoc + EndLoc) * 0.5f;
	
	// 2. 算出刀身到底有多长？
	float BladeLength = FVector::Distance(StartLoc, EndLoc);
	
	// 3. 构建一个长方体检测盒 (长和宽是 15 的剑气厚度，高度是刀身长度的一半)
	FVector BoxHalfSize = FVector(15.0f, 15.0f, BladeLength * 0.5f);
	
	// // 4. 让这个长方体的 Z 轴完美贴合刀身的方向！
	// FRotator BoxRotation = FRotationMatrix::MakeFromZ(EndLoc - StartLoc).Rotator();
	
	// 绝对不要用 MakeFromZ 猜角度，直接拿插槽最真实的物理旋转！
	// 这样检测盒的长宽高，将完美且死死地贴合你的大锤/刀身，在空中绝对不会发生自转抽搐！
	FRotator BoxRotation = WeaponMesh->GetSocketRotation(FName("TraceStart"));

	// 碰撞检测结果
	FHitResult HitResult;

	// 5. 见证奇迹：把这个检测盒，从上一帧的中心，滑向这一帧的中心！
	bool bHit = UKismetSystemLibrary::BoxTraceSingle(
		this, 
		LastMid,     // 起点：上一帧的中心
		CurrentMid,  // 终点：这一帧的中心
		BoxHalfSize, // 检测盒的体积
		BoxRotation, // 检测盒的倾斜角度
		UEngineTypes::ConvertToTraceType(ECC_Visibility), 
		false, 
		IgnoreActors, 
		EDrawDebugTrace::ForDuration, // 保持开启红线调试！
		HitResult, 
		true 
	);

	if (bHit && HitResult.GetActor())
	{
		// 1. 打印打中了谁
		if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 2.0f, FColor::Red, FString::Printf(TEXT("砍中了: %s"), *HitResult.GetActor()->GetName()));

		// 2. 将这名受害者加入黑名单，这刀还没收回之前，不会再判定他
		IgnoreActors.Add(HitResult.GetActor());

		// 【权限与部位检测】：只在服务器算血量
		if (HasAuthority())
		{
			float FinalDamage = 0.0f;
			
			// 1. 判定这一刀的威力和击杀方式
			if (bIsCurrentAttackHeavy)
			{
				FinalDamage = HeavyDamage; // 重击一击必杀
				LastKillMethod = EKillMethod::MeleeWeapon; // 重击视为近战武器击杀
				if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Red, TEXT("重击斩杀！"));
			}
			else
			{
				// 轻击检查骨骼部位
				if (HitResult.BoneName == FName("head")) // 注意物理资产里的头部必须叫 head
				{
					FinalDamage = LightDamageHead;
					LastKillMethod = EKillMethod::MeleeHeadshot; // 轻击爆头
					if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Orange, TEXT("爆头！"));
				}
				else 
				{
					FinalDamage = LightDamageBody;
					LastKillMethod = EKillMethod::MeleeWeapon; // 轻击身体
					if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Yellow, TEXT("击中身体"));
				}
			}
			
			//发送精准伤害 (能把部位信息传给对方)
			UGameplayStatics::ApplyPointDamage(
				HitResult.GetActor(), 
				FinalDamage, 
				(CurrentMid - LastMid).GetSafeNormal(), // 攻击冲量方向
				HitResult,
				GetInstigatorController(), 
				this, 
				UDamageType::StaticClass()
			);
		}
	}
	// 【极其重要】：当前帧结算完毕，把当前位置存入记忆，变成下一帧的“上一帧”！
	LastFrameStartLoc = StartLoc;
	LastFrameEndLoc = EndLoc;
}

// ==========================================
// 动画分配逻辑
// ==========================================
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
		// 魔法取余算法：如果你只配了 3 个轻击动画（索引 0,1,2），
		// 但玩家连击到了第 4 下（ComboIndex = 3），它会自动循环回第 1 个动画！
		int32 SafeIndex = ComboIndex % LightAttackMontages.Num();
		return LightAttackMontages[SafeIndex];
	}
	
	return nullptr;
}




