
#include "Characters/MSPlayerCharacter.h"

#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Data/MSInputConfig.h"
#include "GameFramework/CharacterMovementComponent.h"

AMSPlayerCharacter::AMSPlayerCharacter()
{
    // 启用 Character 自带的蹲下功能
    GetCharacterMovement()->GetNavAgentPropertiesRef().bCanCrouch = true;
}

void AMSPlayerCharacter::BeginPlay()
{
    Super::BeginPlay();

    // 工业级：在 BeginPlay 中添加 Input Mapping Context
    if (APlayerController* PC = Cast<APlayerController>(GetController()))
    {
        if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PC->GetLocalPlayer()))
        {
            Subsystem->AddMappingContext(DefaultMappingContext, 0);
        }
    }
}

void AMSPlayerCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
    // 将原生的 InputComponent 转换为增强型组件
    UEnhancedInputComponent* EIC = CastChecked<UEnhancedInputComponent>(PlayerInputComponent);

    if (InputConfig)
    {
        // 绑定移动：Triggered 持续触发
        EIC->BindAction(InputConfig->MoveAction, ETriggerEvent::Triggered, this, &AMSPlayerCharacter::Input_Move);
        
        // 绑定跳跃：Started 触发一次
        EIC->BindAction(InputConfig->JumpAction, ETriggerEvent::Started, this, &ACharacter::Jump);
        
        // 绑定蹲下：按住蹲下，松开站起
        EIC->BindAction(InputConfig->CrouchAction, ETriggerEvent::Started, this, &AMSPlayerCharacter::Input_Crouch);
        EIC->BindAction(InputConfig->CrouchAction, ETriggerEvent::Completed, this, &AMSPlayerCharacter::Input_StopCrouching);

        // 绑定射击
        EIC->BindAction(InputConfig->ShootAction, ETriggerEvent::Started, this, &AMSPlayerCharacter::Input_Shoot);
    }
}

void AMSPlayerCharacter::Input_Move(const FInputActionValue& Value)
{
    const float MoveValue = Value.Get<float>();

    if (Controller && MoveValue != 0.f)
    {
        // 向 X 轴移动（合金弹头标准方向）
        AddMovementInput(FVector(1.f, 0.f, 0.f), MoveValue);
        
        // 更新角色朝向（2D 镜像）
        UpdateSpriteFacing(MoveValue);
    }
}

void AMSPlayerCharacter::UpdateSpriteFacing(float MoveValue)
{
    // 根据移动方向缩放 X 轴。1 为右，-1 为左。
    if (MoveValue > 0.f)
        SpriteComponent->SetRelativeScale3D(FVector(1.f, 1.f, 1.f));
    else if (MoveValue < 0.f)
        SpriteComponent->SetRelativeScale3D(FVector(-1.f, 1.f, 1.f));
}

void AMSPlayerCharacter::Input_Crouch() { Crouch(); }
void AMSPlayerCharacter::Input_StopCrouching() { UnCrouch(); }

void AMSPlayerCharacter::Input_Shoot()
{
    // TODO: 触发武器组件的开火逻辑
    UE_LOG(LogTemp, Log, TEXT("Player fired a shot!"));
}