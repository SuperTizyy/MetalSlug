// 必须排在第一行！
#include "Characters/BaseCharacter.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Net/UnrealNetwork.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "GameFramework/SpringArmComponent.h"
// 引入武器基类，以便调用武器的接口
#include "GameFramework/CharacterMovementComponent.h"
#include "TimerManager.h" // 为了使用定时器
#include "Weapons/BaseWeapon.h"
#include "UI/Game/GameHUDWidget.h"
#include "Kismet/GameplayStatics.h"
#include "UI/Login/Core/RoomPlayerState.h"

ABaseCharacter::ABaseCharacter()
{
	PrimaryActorTick.bCanEverTick = true;

	// 1. 极其关键：开启角色的网络同步！
	bReplicates = true;
	
	// 允许武器本身在网络中存在
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
	// 角色模型设置
	// ==========================================
	// 【关键修改】：允许玩家自己看到自己的全身模型！
	GetMesh()->SetOwnerNoSee(false); 
	GetMesh()->SetRelativeLocation(FVector(0.f, 0.f, -90.f)); // 往下挪一点，对齐胶囊体底盘
	GetMesh()->SetRelativeRotation(FRotator(0.f, -90.f, 0.f)); // 把脸转到正前方

	// 5. 初始化战斗属性
	MaxHealth = 100.0f;
	CurrentHealth = MaxHealth;
	bIsDead = false; // 初始化存活

	// 初始化能量系统
	MaxEnergy = 100.0f;
	CurrentEnergy = MaxEnergy;

	// 初始化AC/ACE系统
	ACValue = 0;
	ACEValue = 0;

	// 初始化回复系统
	LastMoveTime = 0.0f;
	bIsRegenerating = false;

	// 6. 初始化连击系统状态
	bIsHoldingLightAttack = false;
	bIsAttacking = false;
	CurrentWeapon = nullptr; // 初始状态手里没武器
	
	// 【核心权限】：告诉引擎底层的导航代理，这个角色可以下蹲！
	// 如果不加这句，你按破键盘角色也不会蹲下。
	GetCharacterMovement()->GetNavAgentPropertiesRef().bCanCrouch = true;
	
	// 设置下蹲时的走路速度 (比如 300)
	GetCharacterMovement()->MaxWalkSpeedCrouched = 300.0f;
}

void ABaseCharacter::BeginPlay()
{
	Super::BeginPlay();
	
	// 【新增】：初始化动态材质
	if (GetMesh())
	{
		int32 MaterialCount = GetMesh()->GetNumMaterials();
		for (int32 i = 0; i < MaterialCount; i++)
		{
			UMaterialInstanceDynamic* DynamicMat = GetMesh()->CreateDynamicMaterialInstance(i);
			if (DynamicMat)
			{
				DynamicMaterials.Add(DynamicMat);
			}
		}
	}
	
}

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
	
	// ==========================================
	// 生命/能量回复系统
	// ==========================================
	if (!bIsDead && HasAuthority())
	{
		// 检测角色是否在移动
		FVector Velocity = GetVelocity();
		bool bIsMoving = !Velocity.IsNearlyZero(0.1f);

		if (bIsMoving)
		{
			LastMoveTime = GetWorld()->GetTimeSeconds();
			StopRegeneration();
		}
		else
		{
			// 静止超过设定时间后开始回复
			float TimeSinceLastMove = GetWorld()->GetTimeSeconds() - LastMoveTime;
			if (TimeSinceLastMove >= RegenerationDelay && !bIsRegenerating)
			{
				StartRegeneration();
			}
		}

		// 执行回复
		if (bIsRegenerating)
		{
			// 回复生命值
			if (CurrentHealth < MaxHealth)
			{
				CurrentHealth = FMath::Clamp(CurrentHealth + HealthRegenRate * DeltaTime, 0.0f, MaxHealth);
			}

			// 回复能量值
			if (CurrentEnergy < MaxEnergy)
			{
				CurrentEnergy = FMath::Clamp(CurrentEnergy + EnergyRegenRate * DeltaTime, 0.0f, MaxEnergy);
			}
		}
	}
	
	if (bStartDissolving)
	{
		// 1. 累加溶解值
		CurrentDissolveValue += DeltaTime * DissolveSpeed;

		// 2. 更新所有材质槽的 "DissolveAmount" 参数 (需与材质球中的命名一致)
		for (UMaterialInstanceDynamic* Mat : DynamicMaterials)
		{
			if (Mat)
			{
				Mat->SetScalarParameterValue(FName("DissolveAmount"), CurrentDissolveValue);
			}
		}

		// 3. 当完全溶解（值超过1）时，销毁尸体
		if (CurrentDissolveValue >= 1.1f) // 给一点余量确保完全消失
		{
			Destroy();
		}
	}
}

// ==========================================
// 注册需要网络同步的变量
// ==========================================
void ABaseCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// 同步生命值
	DOREPLIFETIME(ABaseCharacter, CurrentHealth);
	DOREPLIFETIME(ABaseCharacter, bIsDead); // 同步死亡状态
	// 同步武器指针，让所有人都知道你拿了什么武器！
	DOREPLIFETIME(ABaseCharacter, CurrentWeapon);
	// 同步能量值
	DOREPLIFETIME(ABaseCharacter, CurrentEnergy);
	// 同步AC/ACE值
	DOREPLIFETIME(ABaseCharacter, ACValue);
	DOREPLIFETIME(ABaseCharacter, ACEValue);
}

void ABaseCharacter::OnRep_Health()
{
	// 更新客户端HUD血条
	if (IsLocallyControlled() && GameHUDWidget)
	{
		GameHUDWidget->UpdateHealth(CurrentHealth, MaxHealth);
		GameHUDWidget->UpdateHealthText(FMath::CeilToInt(CurrentHealth), FMath::CeilToInt(MaxHealth));
	}
}

void ABaseCharacter::OnRep_Energy()
{
	// 更新客户端HUD能量条
	if (IsLocallyControlled() && GameHUDWidget)
	{
		GameHUDWidget->UpdateEnergy(CurrentEnergy, MaxEnergy);
		GameHUDWidget->UpdateEnergyText(FMath::CeilToInt(CurrentEnergy), FMath::CeilToInt(MaxEnergy));
	}
}

bool ABaseCharacter::ConsumeEnergy(float Amount)
{
	if (Amount <= 0.0f) return true;
	if (CurrentEnergy >= Amount)
	{
		CurrentEnergy -= Amount;
		StopRegeneration(); // 消耗能量后停止回复
		return true;
	}
	return false;
}

void ABaseCharacter::AddEnergy(float Amount)
{
	if (HasAuthority())
	{
		CurrentEnergy = FMath::Clamp(CurrentEnergy + Amount, 0.0f, MaxEnergy);
	}
}

void ABaseCharacter::StartRegeneration()
{
	if (!bIsRegenerating)
	{
		bIsRegenerating = true;
	}
}

void ABaseCharacter::StopRegeneration()
{
	if (bIsRegenerating)
	{
		bIsRegenerating = false;
	}
}

void ABaseCharacter::OnKill(ABaseCharacter* KilledCharacter)
{
	if (!HasAuthority()) return;

	// 击杀奖励：增加血量和能量
	AddEnergy(EnergyRewardPerKill);
	CurrentHealth = FMath::Clamp(CurrentHealth + HealthRewardPerKill, 0.0f, MaxHealth);

	// 增加AC
	AddAC(1);

	// 增加连杀数（如果有HUD的话可以在这里更新）
	if (IsLocallyControlled() && GameHUDWidget)
	{
		// TODO: 增加连杀计数
	}
}

void ABaseCharacter::AddAC(int32 Amount)
{
	if (HasAuthority())
	{
		ACValue += Amount;
		ACEValue += Amount; // ACE也同时增加
	}
}

void ABaseCharacter::ResetACE()
{
	if (HasAuthority())
	{
		ACEValue = 0;
	}
}

// ==========================================
// 输入绑定与移动逻辑
// ==========================================
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
		
		// 【连斩核心绑定】：左键按下 (Started) 和 松开 (Completed)
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

void ABaseCharacter::Move(const FInputActionValue& Value)
{
	if (bIsDead) return; // 死人不能动
	
	FVector2D MovementVector = Value.Get<FVector2D>();
	if (Controller != nullptr)
	{
		// 找出摄像机当前面向的方位
		const FRotator Rotation = Controller->GetControlRotation();
		const FRotator YawRotation(0, Rotation.Yaw, 0);

		// 获取摄像机视角下的“正前方”和“正右方”
		const FVector ForwardDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
		const FVector RightDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);

		// 按照摄像机的视角去驱动角色移动
		AddMovementInput(ForwardDirection, MovementVector.Y);
		AddMovementInput(RightDirection, MovementVector.X);

		// 记录移动时间，用于回复系统
		LastMoveTime = GetWorld()->GetTimeSeconds();
	}
}

void ABaseCharacter::Look(const FInputActionValue& Value)
{
	if (bIsDead) return; // 死人不能看
	
	FVector2D LookAxisVector = Value.Get<FVector2D>();
	if (Controller != nullptr)
	{
		AddControllerYawInput(LookAxisVector.X);
		AddControllerPitchInput(LookAxisVector.Y);
	}
}

// ==========================================
// 自动连斩系统核心逻辑
// ==========================================

void ABaseCharacter::LightAttack_Pressed()
{
	if (bIsDead || !CurrentWeapon) return; // 死人不能砍人
	
	// 下蹲攻击权限拦截！
	// 如果你正蹲着，并且这把武器禁止下蹲攻击，直接 return，无视玩家按键！
	if (bIsCrouched && !CurrentWeapon->bCanAttackWhileCrouched)
	{
		return;
	}
	
	bIsHoldingLightAttack = true; // 记录按住状态
	
	// 根据当前武器的配置，决定要不要锁步！
	bIsMovementLocked = !CurrentWeapon->bCanMoveWhileLightAttack;
	
	if (bIsMovementLocked)
	{
		GetCharacterMovement()->MaxWalkSpeed = 0.0f; // 物理刹车
	}
	
	if (!bIsAttacking)
	{
		// 1. 第一刀起手：上锁，初始化状态
		bIsAttacking = true;
		ComboIndex = 1;
		bSaveAttack = false;
		bCanReceiveInput = false;
		ExecuteComboSequence();
	}
	else if (bCanReceiveInput)
	{
		// 2. 如果正在挥刀，且【绿色输入区间】开启了
		// 记录玩家点过鼠标了！(不管点几次，只记为 true)
		bSaveAttack = true;
	}
}

void ABaseCharacter::LightAttack_Released()
{
	bIsHoldingLightAttack = false; // 记录：玩家松手了！
}

void ABaseCharacter::ExecuteComboSequence()
{
	if (!CurrentWeapon) return;

	// 工业级做法：所有连招全在第 0 个蒙太奇里
	UAnimMontage* ComboMontage = CurrentWeapon->GetAttackMontage(false, 0);
	if (ComboMontage)
	{
		// 智能拼接要跳转的片段名字 (Combo1 或者是 Combo2)
		FName SectionName = (ComboIndex == 1) ? FName("Combo1") : FName("Combo2");

		PlayAnimMontage(ComboMontage, 1.0f, SectionName);
		
		// 【激活网络同步】：告诉服务器我出的是第几刀！
		Server_PlayAttackAnim(false, ComboIndex);
	}
}

void ABaseCharacter::HeavyAttack()
{
	if (bIsDead || !CurrentWeapon) return;

	// 【新增】：重击同样需要检查下蹲权限
	if (bIsCrouched && !CurrentWeapon->bCanAttackWhileCrouched)
	{
		return;
	}
	// // 必须在没挥刀的时候才能触发重击，防止轻击动作被强行打断
	// if (!bIsAttacking && CurrentWeapon)
	// {
	// 	bIsAttacking = true;
	// 	GetWorldTimerManager().ClearTimer(ComboResetTimer);
	//
	// 	// 重击不需要连击索引，默认传 0
	// 	UAnimMontage* MontageToPlay = CurrentWeapon->GetAttackMontage(true, 0);
	// 	if (MontageToPlay)
	// 	{
	// 		PlayAnimMontage(MontageToPlay);
	// 		Server_PlayAttackAnim(true, 0);
	// 	}
	// }
}

void ABaseCharacter::UseSkill()
{
	if (bIsDead) return;

	// TODO: 技能系统具体实现
	// 这里可以扩展为技能槽系统，支持多个技能
}

// ==========================================
// 动画通知 (Anim Notify) 回调接口
// ==========================================

void ABaseCharacter::EnableComboWindow()
{
	bCanReceiveInput = true; // 绿灯亮起，开始接收输入
	bSaveAttack = false;     // 清空上一轮的垃圾缓存
}

void ABaseCharacter::CheckCombo()
{
	bCanReceiveInput = false; // 绿灯熄灭，关闭窗口

	// 【终极结算】：如果有缓存的点击，或者玩家正死死按住左键！
	if (bSaveAttack || bIsHoldingLightAttack)
	{
		
		// 【新增防逃课锁】：连击派生前，检查当前是不是已经蹲下了！
		if (bIsCrouched && !CurrentWeapon->bCanAttackWhileCrouched)
		{
			bSaveAttack = false; // 清除缓存
			return; // 强行中断后续连招
		}
		
		bSaveAttack = false; // 消耗掉这次缓存
		
		// 核心需求：1变2，2变1，无限循环！
		ComboIndex = (ComboIndex == 1) ? 2 : 1; 
		
		ExecuteComboSequence(); // 丝滑跳转下一刀
	}
}

void ABaseCharacter::EndAttackState()
{
	// 彻底收招，解开所有锁
	bIsAttacking = false;
	bSaveAttack = false;
	bCanReceiveInput = false;
	ComboIndex = 1;
	// 收刀时，彻底解除移动锁定
	bIsMovementLocked = false;
	GetCharacterMovement()->MaxWalkSpeed = 600.0f; // 恢复正常速度 (请填入你自己的满速)
}

// ==========================================
// RPC 动画广播实现
// ==========================================
void ABaseCharacter::Server_PlayAttackAnim_Implementation(bool bIsHeavy, int32 InComboIndex)
{
	// 服务器端也必须同步锁死速度！
	// 这样服务器和客户端的物理推演就完全一致了，再也不会发生“强行拽人”的瞬移！
	if (CurrentWeapon && !CurrentWeapon->bCanMoveWhileLightAttack)
	{
		bIsMovementLocked = true;
		GetCharacterMovement()->MaxWalkSpeed = 0.0f;
	}
	
	// 服务器收到请求后，直接向全频道广播
	Multicast_PlayAttackAnim(bIsHeavy, InComboIndex);
}

void ABaseCharacter::Multicast_PlayAttackAnim_Implementation(bool bIsHeavy, int32 InComboIndex)
{
	// 发起攻击的本地玩家自己已经播过动画了，防鬼畜直接跳过
	if (IsLocallyControlled()) return;

	if (CurrentWeapon)
	{
		// 工业级做法：直接拿第 0 个蒙太奇（因为所有轻击招式都在这里面）
		UAnimMontage* MontageToPlay = CurrentWeapon->GetAttackMontage(bIsHeavy, 0);
		if (MontageToPlay) 
		{
			// 根据服务器传来的 InComboIndex，智能推断该播哪个片段！
			FName SectionName = (InComboIndex == 1) ? FName("Combo1") : FName("Combo2");
			
			// 让其他玩家屏幕上的你，也精准跳到对应的连招片段！
			PlayAnimMontage(MontageToPlay, 1.0f, SectionName);
		}
	}
}

// ==========================================
// 外部调用的发枪接口
// ==========================================
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
			// 【第三人称关键】：将武器死死地焊在第三人称全身模型 (GetMesh) 的 "WeaponSocket" 插槽上！
			CurrentWeapon->AttachToComponent(GetMesh(), FAttachmentTransformRules::SnapToTargetNotIncludingScale, FName("WeaponSocket_L"));
		}
	}
}

float ABaseCharacter::TakeDamage(float DamageAmount, struct FDamageEvent const& DamageEvent, class AController* EventInstigator, AActor* DamageCauser)
{
	// 1. 如果已经死了，或者客户端没有权限，直接退出
	if (bIsDead || !HasAuthority()) return 0.0f;
	
	// 先执行父类的逻辑
	float ActualDamage = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
	
	// 2. 安全扣血 (保证不会扣成负数)
	CurrentHealth = FMath::Clamp(CurrentHealth - ActualDamage, 0.0f, MaxHealth);
	
	if (GEngine) GEngine->AddOnScreenDebugMessage(-1, 3.0f, FColor::Yellow, FString::Printf(TEXT("受到伤害: %f，剩余血量: %f"), ActualDamage, CurrentHealth));

	// 3. 判定生死！
	if (CurrentHealth <= 0.0f)
	{
		Die(); 
	}
	
	// 这里未来可以播放受击蒙太奇、喷血特效、或者死亡逻辑
	// PlayAnimMontage(HitReactMontage);

	return ActualDamage;
}

void ABaseCharacter::Die()
{
	// 只有服务器能宣判死亡
	if (!HasAuthority() || bIsDead) return;

	// 告诉所有客户端：这个人死了，准备看布娃娃！
	Multicast_Die(); 
}

void ABaseCharacter::Multicast_Die_Implementation()
{
	// 【所有人都会执行这个函数，包括发起死亡的服务器和所有客户端】
	bIsDead = true;
	
	// ==========================================
    // 【终极重构】：武器脱手掉落系统
    // ==========================================
    if (CurrentWeapon)
    {
    	// 1. 彻底断开与死人手的骨骼绑定！
    	CurrentWeapon->DetachFromActor(FDetachmentTransformRules::KeepWorldTransform);

    	// 2. 找到武器的模型组件，激活真实的物理模拟，让它当啷掉在地上！
    	UMeshComponent* WeaponMesh = CurrentWeapon->FindComponentByClass<UMeshComponent>();
    	if (WeaponMesh)
    	{
    		// 开启碰撞，确保它能砸在地上而不是掉出世界
    		WeaponMesh->SetCollisionEnabled(ECollisionEnabled::PhysicsOnly);
    		WeaponMesh->SetCollisionResponseToAllChannels(ECR_Block);
    		// 开启物理重力
    		WeaponMesh->SetSimulatePhysics(true);
    	}
    }
	
	// 开启 10 秒倒计时
	FTimerHandle DissolveTimerHandle;
	GetWorldTimerManager().SetTimer(DissolveTimerHandle, this, &ABaseCharacter::StartDissolveProcess, DissolveDelay, false);

	// 1. 禁用操作和移动
	GetCharacterMovement()->DisableMovement();
	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		PC->DisableInput(PC);
	}

	// 2. 核心防穿模：让胶囊体变透明，不阻挡别人走路，也不阻挡摄像机
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Pawn, ECR_Ignore);
	GetCapsuleComponent()->SetCollisionResponseToChannel(ECC_Camera, ECR_Ignore);

	// 3. 播放死亡动画
	float AnimDuration = 0.1f;
	if (DeathMontage)
	{
		AnimDuration = PlayAnimMontage(DeathMontage);
	}

	// 4. 定时开启布娃娃 (比如动画总长的 70% 处开启，顺带解决插地问题)
	float TimeToRagdoll = AnimDuration > 0.1f ? (AnimDuration * 0.7f) : 0.1f;
	GetWorldTimerManager().SetTimer(RagdollTimerHandle, this, &ABaseCharacter::EnableRagdoll, TimeToRagdoll, false);
}

// 定时器触发后，标记开始融化
void ABaseCharacter::StartDissolveProcess()
{
	bStartDissolving = true;
	
	// 在开始融化的瞬间，去把武器上的材质也拽进来！
	if (CurrentWeapon)
	{
		// 泛型查找法：不管你的武器组件叫啥，直接找它身上的 Mesh 组件
		UMeshComponent* WeaponMesh = CurrentWeapon->FindComponentByClass<UMeshComponent>();
		if (WeaponMesh)
		{
			for (int32 i = 0; i < WeaponMesh->GetNumMaterials(); i++)
			{
				UMaterialInstanceDynamic* DynMat = WeaponMesh->CreateDynamicMaterialInstance(i);
				if (DynMat)
				{
					// 塞进同一个数组里！Tick 函数不用改任何代码，就能自动带它一起融化！
					DynamicMaterials.Add(DynMat);
				}
			}
		}
	}
}

void ABaseCharacter::EnableRagdoll()
{
	// 1. 打断所有还在播的动画
	StopAnimMontage();

	// 2. 将控制权完全交给物理资产
	GetMesh()->SetCollisionProfileName(TEXT("Ragdoll"));
	GetMesh()->SetSimulatePhysics(true);
}

// 实现下蹲函数
void ABaseCharacter::StartCrouch()
{
	// 只有没死的时候才能蹲
	if (!bIsDead) 
	{
		Crouch(); // 调用 UE ACharacter 自带的神级下蹲函数
	}
}

void ABaseCharacter::StopCrouch()
{
	if (!bIsDead) 
	{
		UnCrouch(); // 调用自带的起立函数 (它会自动检测头顶有没有障碍物，有的话会保持蹲着！)
	}
}

// 当物理系统判定角色【真正蹲下】的瞬间触发
void ABaseCharacter::OnStartCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust)
{
	Super::OnStartCrouch(HalfHeightAdjust, ScaledHalfHeightAdjust);

	// 此时胶囊体瞬间矮了，摄像机跟着掉下去了。
	// 我们给摄像机的 TargetOffset (目标偏移) 瞬间加上这段高度差！
	// 这样摄像机在视觉上就仿佛停留在原处没动！
	if (CameraBoom)
	{
		CameraBoom->TargetOffset.Z += HalfHeightAdjust;
	}
}

// 当物理系统判定角色【真正站起】的瞬间触发
void ABaseCharacter::OnEndCrouch(float HalfHeightAdjust, float ScaledHalfHeightAdjust)
{
	Super::OnEndCrouch(HalfHeightAdjust, ScaledHalfHeightAdjust);

	// 此时胶囊体瞬间长高了，摄像机被瞬间顶上去了。
	// 我们把摄像机瞬间往下拉同样的距离，抵消瞬移！
	if (CameraBoom)
	{
		CameraBoom->TargetOffset.Z -= HalfHeightAdjust;
	}
}

void ABaseCharacter::PossessedBy(AController* NewController)
{
	Super::PossessedBy(NewController);

	// 仅在服务器执行
	if (ARoomPlayerState* PS = NewController->GetPlayerState<ARoomPlayerState>())
	{
		//使用新的 Getter 方法获取一号位武器
		SpawnAndEquipWeapon(PS->GetSelectedWeapon1ID());
	}
}

void ABaseCharacter::SpawnAndEquipWeapon(FString WeaponID)
{
	// 1. 查 DT_WeaponInfo 表获取 WeaponClass
	// 2. SpawnActor<ABaseWeapon>(Info->WeaponClass)
	// 3. Weapon->AttachToComponent(GetMesh(), FAttachmentTransformRules::SnapToTargetIncludingScale, TEXT("WeaponSocket"));
}