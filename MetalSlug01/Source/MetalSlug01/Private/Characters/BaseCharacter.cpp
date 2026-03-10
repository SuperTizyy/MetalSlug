#include "Characters/BaseCharacter.h"

#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"
#include "Components/InputComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Data/GameStaticTable.h"
#include "Engine/World.h"
#include "GameFramework/PlayerController.h"

ABaseCharacter::ABaseCharacter()
{
	// 创建弹簧臂组件，用于相机跟随
	SpringArmComp = CreateDefaultSubobject<USpringArmComponent>(TEXT("SpringArmComp"));
	SpringArmComp->SetupAttachment(RootComponent);
	SpringArmComp->TargetArmLength = 400.0f; // 相机距离角色的距离
	SpringArmComp->bUsePawnControlRotation = true; // 允许角色控制相机旋转

	// 创建相机组件
	CameraComp = CreateDefaultSubobject<UCameraComponent>(TEXT("CameraComp"));
	CameraComp->SetupAttachment(SpringArmComp, USpringArmComponent::SocketName);
	CameraComp->bUsePawnControlRotation = false; // 相机不跟随角色旋转

	// 获取角色移动组件
	CharacterMovementComp = GetCharacterMovement();
	CharacterMovementComp->bOrientRotationToMovement = true; // 移动方向决定角色朝向
	CharacterMovementComp->RotationRate = FRotator(0.0f, 540.0f, 0.0f); // 旋转速率
	CharacterMovementComp->bConstrainToPlane = true; // 限制在平面上移动
	CharacterMovementComp->bSnapToPlaneAtStart = true; // 开始时贴合平面

	// 设置默认值
	MovementSpeed = 600.0f;
	CurrentMovementSpeed = MovementSpeed;
	MaxHealth = 100.0f;
	CurrentHealth = MaxHealth;
	Level = 1;
	bIsAlive = true;
	AnimationState = ECharacterAnimationState::Idle;
	CharacterType = ECharacterType::Player;
	CharacterID = EControlledCharacterID::Character1; // 默认为角色1
	PlayerIndex = EPlayerIndex::Player1; // 默认为玩家1
	MovementState = EMovementState::OnGround;
	bIsCurrentControlled = false; // 默认不是当前控制的角色

	// 设置角色网格体
	CharacterMesh = GetMesh();
}

void ABaseCharacter::BeginPlay()
{
	Super::BeginPlay();
	
	// 在游戏开始时初始化角色状态
	CurrentHealth = MaxHealth;
	
	// 初始动画状态
	OnAnimationStateChanged(AnimationState);
	
	// 如果初始就是当前控制的角色，通知蓝图
	if (bIsCurrentControlled)
	{
		OnCharacterSwitched(true);
		OnPlayerControlChanged();
	}
}

void ABaseCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	// 更新移动状态
	UpdateMovementState();
	
	// 根据移动状态更新动画状态
	if (IsInAir())
	{
		if (GetCharacterMovement()->Velocity.Z > 0)
		{
			SetAnimationState(ECharacterAnimationState::Jumping);
		}
		else
		{
			SetAnimationState(ECharacterAnimationState::Falling);
		}
	}
	else if (CharacterMovementComp->Velocity.Size() > MovementSpeed * 0.5f)
	{
		SetAnimationState(ECharacterAnimationState::Running);
	}
	else
	{
		SetAnimationState(ECharacterAnimationState::Idle);
	}
}

void ABaseCharacter::PostInitializeComponents()
{
	Super::PostInitializeComponents();
}

void ABaseCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
	Super::SetupPlayerInputComponent(PlayerInputComponent);

	// 根据玩家索引设置不同的输入映射上下文
	if (APlayerController* PC = Cast<APlayerController>(GetController()))
	{
		// 为不同玩家设置不同的输入轴映射
		switch (PlayerIndex)
		{
		case EPlayerIndex::Player1:
			PlayerInputComponent->BindAxis(TEXT("MoveForward"), this, &ABaseCharacter::MoveForward);
			PlayerInputComponent->BindAxis(TEXT("MoveRight"), this, &ABaseCharacter::MoveRight);
			break;
		case EPlayerIndex::Player2:
			PlayerInputComponent->BindAxis(TEXT("MoveForward_P2"), this, &ABaseCharacter::MoveForward);
			PlayerInputComponent->BindAxis(TEXT("MoveRight_P2"), this, &ABaseCharacter::MoveRight);
			break;
		case EPlayerIndex::Player3:
			PlayerInputComponent->BindAxis(TEXT("MoveForward_P3"), this, &ABaseCharacter::MoveForward);
			PlayerInputComponent->BindAxis(TEXT("MoveRight_P3"), this, &ABaseCharacter::MoveRight);
			break;
		case EPlayerIndex::Player4:
			PlayerInputComponent->BindAxis(TEXT("MoveForward_P4"), this, &ABaseCharacter::MoveForward);
			PlayerInputComponent->BindAxis(TEXT("MoveRight_P4"), this, &ABaseCharacter::MoveRight);
			break;
		}
		
		// 绑定跳跃输入
		FString JumpActionName = FString::Printf(TEXT("Jump_P%d"), static_cast<int32>(PlayerIndex) + 1);
		PlayerInputComponent->BindAction(*JumpActionName, IE_Pressed, this, &ABaseCharacter::JumpInput);
		PlayerInputComponent->BindAction(*JumpActionName, IE_Released, this, &ABaseCharacter::StopJumping);
	}
}

void ABaseCharacter::MoveForward(float Value)
{
	// 前后移动输入处理
	if ((Controller != nullptr) && (Value != 0.0f))
	{
		// 计算前进方向
		const FRotator Rotation = Controller->GetControlRotation();
		const FRotator YawRotation(0, Rotation.Yaw, 0);

		// 获取前进方向向量
		const FVector Direction = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
		
		// 执行基础移动
		BasicMove(Direction * Value);
	}
}

void ABaseCharacter::MoveRight(float Value)
{
	// 左右移动输入处理
	if ((Controller != nullptr) && (Value != 0.0f))
	{
		// 计算右侧方向
		const FRotator Rotation = Controller->GetControlRotation();
		const FRotator YawRotation(0, Rotation.Yaw, 0);

		// 获取右侧方向向量
		const FVector Direction = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);
		
		// 执行基础移动
		BasicMove(Direction * Value);
	}
}

void ABaseCharacter::JumpInput()
{
	// 跳跃输入处理
	Jump();
}

void ABaseCharacter::StopJumping()
{
	// 停止跳跃输入处理
	StopJumping();
}

void ABaseCharacter::BasicMove(const FVector& Direction)
{
	// 基础移动逻辑实现
	if (Direction.SizeSquared() > 0.0f)
	{
		// 添加移动输入
		AddMovementInput(Direction, 1.0f);
	}
}

void ABaseCharacter::WallClimb_Implementation()
{
	// 爬墙功能的默认实现 - 当前为空，预留接口
	// 后续可以在派生类中实现具体逻辑
}

void ABaseCharacter::Slide_Implementation()
{
	// 滑铲功能的默认实现 - 当前为空，预留接口
	// 后续可以在派生类中实现具体逻辑
}

void ABaseCharacter::Dive_Implementation()
{
	// 潜水功能的默认实现 - 当前为空，预留接口
	// 后续可以在派生类中实现具体逻辑
}

void ABaseCharacter::SetMovementSpeed(float NewSpeed)
{
	// 设置新的移动速度
	if (NewSpeed >= 0.0f)
	{
		MovementSpeed = NewSpeed;
		CharacterMovementComp->MaxWalkSpeed = MovementSpeed;
		CurrentMovementSpeed = MovementSpeed;
	}
}

void ABaseCharacter::SetAnimationState(ECharacterAnimationState NewState)
{
	// 设置新动画状态
	if (AnimationState != NewState)
	{
		AnimationState = NewState;
		// 触发蓝图事件，让蓝图知道动画状态已改变
		OnAnimationStateChanged(AnimationState);
	}
}

void ABaseCharacter::SetCharacterType(ECharacterType NewType)
{
	// 设置角色类型
	CharacterType = NewType;
}

void ABaseCharacter::SetCharacterID(EControlledCharacterID NewID)
{
	// 设置角色ID
	CharacterID = NewID;
}

void ABaseCharacter::SetPlayerIndex(EPlayerIndex NewPlayerIndex)
{
	// 设置玩家索引
	if (PlayerIndex != NewPlayerIndex)
	{
		PlayerIndex = NewPlayerIndex;
		// 通知蓝图玩家控制已改变
		OnPlayerControlChanged();
	}
}

void ABaseCharacter::SetAsCurrentControlled(bool bNewValue)
{
	// 设置为当前控制的角色
	if (bIsCurrentControlled != bNewValue)
	{
		bIsCurrentControlled = bNewValue;
		// 通知蓝图角色控制状态已改变
		OnCharacterSwitched(bIsCurrentControlled);
	}
}

void ABaseCharacter::UpdateMovementState()
{
	// 根据当前情况更新移动状态
	if (IsInWater())
	{
		MovementState = EMovementState::Swimming;
	}
	else if (IsClimbing())
	{
		MovementState = EMovementState::Climbing;
	}
	else if (IsSliding())
	{
		MovementState = EMovementState::Sliding;
	}
	else if (IsInAir())
	{
		MovementState = EMovementState::InAir;
	}
	else
	{
		MovementState = EMovementState::OnGround;
	}
	
	// 如果移动状态发生变化，触发蓝图事件
	static EMovementState LastMovementState = EMovementState::OnGround;
	if (LastMovementState != MovementState)
	{
		LastMovementState = MovementState;
		OnMovementStateChanged(MovementState);
	}
}

void ABaseCharacter::SetCurrentHealth(float NewHealth)
{
	// 设置当前生命值
	CurrentHealth = FMath::Clamp(NewHealth, 0.0f, MaxHealth);
	
	// 检查是否死亡
	if (CurrentHealth <= 0.0f)
	{
		Die();
	}
}

float ABaseCharacter::TakeDamage(float DamageAmount, struct FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	// 1. 调用父类逻辑（非常重要！否则引擎的伤害处理逻辑会失效）
	float ActualDamage = Super::TakeDamage(DamageAmount, DamageEvent, EventInstigator, DamageCauser);
	// 处理受伤逻辑
	if (DamageAmount > 0.0f && bIsAlive)
	{
		// 减少生命值
		CurrentHealth -= DamageAmount;
		
		// 确保生命值不小于0
		CurrentHealth = FMath::Max(CurrentHealth, 0.0f);
		
		// 播放受伤动画
		if (CurrentHealth > 0.0f)
		{
			SetAnimationState(ECharacterAnimationState::TakingDamage);
		}
		
		// 检查是否死亡
		if (CurrentHealth <= 0.0f)
		{
			Die();
		}
	}
	// 3. 返回实际造成的伤害
	return ActualDamage;
}

void ABaseCharacter::Die()
{
	// 处理死亡逻辑
	if (bIsAlive)
	{
		bIsAlive = false;
		
		// 设置死亡动画状态
		SetAnimationState(ECharacterAnimationState::Dying);
		
		// 停止所有动作
		if (CharacterMovementComp)
		{
			CharacterMovementComp->StopMovementImmediately();
		}
		
		// 销毁角色或进入其他状态
		// 这里可以选择隐藏角色、应用Ragdoll物理等
		Destroy();
	}
}

void ABaseCharacter::LevelUp()
{
	// 提升角色等级
	Level++;
	
	// 可以在这里添加升级奖励逻辑
	// 如增加生命值上限、提升移动速度等
	MaxHealth += 10.0f; // 示例：每级增加10点生命值上限
	CurrentHealth = MaxHealth; // 升级时回满生命值
}

void ABaseCharacter::SwitchToNextCharacter()
{
	// 切换到下一个角色
	// 这个函数应该由外部管理器调用来切换角色
}

void ABaseCharacter::SwitchToPreviousCharacter()
{
	// 切换到上一个角色
	// 这个函数应该由外部管理器调用来切换角色
}

int32 ABaseCharacter::GetPlayerID() const
{
	/// 获取当前控制该角色的控制器
	APlayerController* PC = Cast<APlayerController>(GetController());
    
	// 检查是否为本地真人玩家控制
	if (PC && PC->IsLocalPlayerController())
	{
		ULocalPlayer* LP = PC->GetLocalPlayer();
		if (LP)
		{
			// 获取本地插槽 ID (0 为 P1, 1 为 P2...)
			return LP->GetControllerId();
		}
	}
	return INDEX_NONE;
}

EControlledCharacterID ABaseCharacter::GetNextCharacterID() const
{
	// 获取下一个角色ID
	switch (CharacterID)
	{
	case EControlledCharacterID::Character1:
		return EControlledCharacterID::Character2;
	case EControlledCharacterID::Character2:
		return EControlledCharacterID::Character3;
	case EControlledCharacterID::Character3:
		return EControlledCharacterID::Character1;
	default:
		return EControlledCharacterID::Character1;
	}
}

EControlledCharacterID ABaseCharacter::GetPreviousCharacterID() const
{
	// 获取上一个角色ID
	switch (CharacterID)
	{
	case EControlledCharacterID::Character1:
		return EControlledCharacterID::Character3;
	case EControlledCharacterID::Character2:
		return EControlledCharacterID::Character1;
	case EControlledCharacterID::Character3:
		return EControlledCharacterID::Character2;
	default:
		return EControlledCharacterID::Character1;
	}
}

FVector ABaseCharacter::GetCameraOffset() const
{
	// 根据玩家索引返回不同的相机偏移，用于分屏显示
	switch (PlayerIndex)
	{
	case EPlayerIndex::Player1:
		// 玩家1使用标准相机位置
		return FVector::ZeroVector;
	case EPlayerIndex::Player2:
		// 玩家2可能需要不同的相机偏移（如果需要特殊视觉效果）
		return FVector(0.0f, 0.0f, 0.0f);
	case EPlayerIndex::Player3:
		// 玩家3可能需要不同的相机偏移
		return FVector(0.0f, 0.0f, 0.0f);
	case EPlayerIndex::Player4:
		// 玩家4可能需要不同的相机偏移
		return FVector(0.0f, 0.0f, 0.0f);
	default:
		return FVector::ZeroVector;
	}
}

FVector ABaseCharacter::GetDesiredCameraOffset() const
{
	// 返回期望的相机偏移
	return GetCameraOffset();
}