
#include "Animation/BaseAnimInstance.h"
#include "Characters/BaseCharacter.h" 
#include "Weapons/BaseWeapon.h"       
#include "GameFramework/CharacterMovementComponent.h"

void UBaseAnimInstance::NativeInitializeAnimation()
{
	Super::NativeInitializeAnimation();

	// 游戏开始时，拿到这具身体的主人
	Character = Cast<ABaseCharacter>(TryGetPawnOwner());
}

void UBaseAnimInstance::NativeUpdateAnimation(float DeltaSeconds)
{
	Super::NativeUpdateAnimation(DeltaSeconds);

	if (!Character) return; // 防崩保护
	
	// 极其高效地获取下蹲状态
	// bIsCrouched 是 UE 角色基类自带的网络同步变量
	bIsCrouching = Character->bIsCrouched;

	// 获取角色速度
	FVector Velocity = Character->GetVelocity();
	Speed = Velocity.Length();
	
	// 提取上下飞行的速度
	VelocityZ = Velocity.Z;
	
	// ==========================================
	// 终极电竞步法算法 (剥离物理与动画)+ 动态锁步系统
	// ==========================================
	// 获取玩家当前的键盘输入力度 (只要按了方向键，Acceleration 就会大于 0)
	float Acceleration = Character->GetCharacterMovement()->GetCurrentAcceleration().Length();

	// 动态获取当前的最高限速（下蹲时是 300，站立时是 600）
	float CurrentMaxSpeed = Character->bIsCrouched ? Character->GetCharacterMovement()->MaxWalkSpeedCrouched : 600.0f;
	
	if (Character->bIsMovementLocked) 
	{
		// 1. 如果被武器锁步了，不管玩家怎么按键盘，强制腿部变回 Idle 站立动画！
		AnimSpeed = 0.0f; 
	}
	else if (Acceleration > 0.0f)
	{
		// 【防滑步核心】：只要玩家按了键盘产生了加速度，哪怕物理速度还没加上去，
		// 动画系统直接按满速 (CurrentMaxSpeed) 播放！强行让腿迈开！
		AnimSpeed = CurrentMaxSpeed;
	}
	else
	{
		// 3. 玩家松手，真实刹车
		AnimSpeed = Speed;
	}
	
	
	// ==========================================
	// 计算 8 向移动的 Direction 角度
	// ==========================================
	if (Speed > 3.0f) // 只有在移动时才计算方向，防止原地轻微抖动
	{
		// 1. 把世界坐标系下的速度，转换成角色“相对自己”的本地速度
		FVector LocalVelocity = Character->GetActorTransform().InverseTransformVectorNoScale(Velocity);
		
		// 2. 利用反正切函数 (Atan2) 计算出完美的 X/Y 夹角，并转为度数
		// 正前方=0，正右方=90，正左方=-90，正后方=180/-180
		Direction = FMath::RadiansToDegrees(FMath::Atan2(LocalVelocity.Y, LocalVelocity.X));
	}
	else
	{
		Direction = 0.0f; // 停下时方向归零
	}
	
	// ==========================================
	// 检测是否离地
	// ==========================================
	bIsFalling = Character->GetCharacterMovement()->IsFalling();
	
	// ==========================================
	// 极其高效地计算上下瞄准角度 (Pitch)
	// ==========================================
	// 获取摄像机真正看着的方向，减去角色身体当前的朝向
	FRotator DeltaRot = Character->GetBaseAimRotation() - Character->GetActorRotation();
	DeltaRot.Normalize(); // 规范化到 -180 到 180 度之间
	
	// 把这个角度除以 3（因为我们只弯曲一节脊椎，如果直接用原角度，腰会折断！除以 2 或 3 会让弯腰显得更自然）
	AimPitch = (DeltaRot.Pitch * -1.0f) / 2.0f;

	// // 算出摄像机和人物身体的左右夹角，并把身体向准星方向扭转！
	// // 同样除以 2.0f 分摊受力，并用 Clamp 锁死在 -45度 到 45度 之间，防止人物向后看时把自己的脊椎拧成麻花！
	// AimYaw = FMath::Clamp(DeltaRot.Yaw / 2.0f, -45.0f, 45.0f);
	
	// 极其高效地搜寻磁铁坐标
	if (ABaseWeapon* Weapon = Character->GetCurrentWeapon())
	{
		// 去武器的根组件上找咱们打好的插槽 "LeftHandSocket"
		LeftHandIKTransform = Weapon->GetRootComponent()->GetSocketTransform(FName("LeftHandSocket"), RTS_World);
		
		// 拿到刀了，开启左手磁吸！
		LeftHandIKAlpha = 1.0f; 
	}
	else
	{
		// 手里没刀，乖乖把手放下
		LeftHandIKAlpha = 0.0f; 
	}
}