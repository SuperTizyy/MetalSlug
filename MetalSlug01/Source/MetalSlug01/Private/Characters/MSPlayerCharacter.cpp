#include "Characters/MSPlayerCharacter.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Data/MSInputConfig.h"
#include "GameFramework/SpringArmComponent.h"
#include "Camera/CameraComponent.h"
#include "PaperFlipbookComponent.h" // 核心修复：必须包含此头文件以操作 BodyComponent
#include "GameFramework/CharacterMovementComponent.h"

AMSPlayerCharacter::AMSPlayerCharacter()
{
    CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
    CameraBoom->SetupAttachment(RootComponent);
    CameraBoom->TargetArmLength = 600.f;
    CameraBoom->SetRelativeRotation(FRotator(0.f, -90.f, 0.f));
    CameraBoom->bInheritPitch = CameraBoom->bInheritYaw = CameraBoom->bInheritRoll = false;

    FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
    FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName);
    FollowCamera->ProjectionMode = ECameraProjectionMode::Orthographic;
    FollowCamera->OrthoWidth = 1024.f;

    // 解决按键失灵：强制接收输入
    AutoPossessPlayer = EAutoReceiveInput::Player0;
}

void AMSPlayerCharacter::BeginPlay() { Super::BeginPlay(); }

void AMSPlayerCharacter::SetupPlayerInputComponent(UInputComponent* PlayerInputComponent)
{
    Super::SetupPlayerInputComponent(PlayerInputComponent);

    if (UEnhancedInputComponent* EIC = CastChecked<UEnhancedInputComponent>(PlayerInputComponent))
    {
        if (InputConfig)
        {
            EIC->BindAction(InputConfig->MoveAction, ETriggerEvent::Triggered, this, &AMSPlayerCharacter::Input_Move);
            EIC->BindAction(InputConfig->JumpAction, ETriggerEvent::Started, this, &ACharacter::Jump);
            EIC->BindAction(InputConfig->CrouchAction, ETriggerEvent::Started, this, &AMSPlayerCharacter::Input_Crouch);
            EIC->BindAction(InputConfig->CrouchAction, ETriggerEvent::Completed, this, &AMSPlayerCharacter::Input_StopCrouching);
        }
    }
}

void AMSPlayerCharacter::Input_Move(const FInputActionValue& Value)
{
    const float MoveValue = Value.Get<float>();
    if (Controller && MoveValue != 0.f)
    {
        AddMovementInput(FVector(1.f, 0.f, 0.f), MoveValue);
        
        // 左右翻转逻辑
        if (BodyComponent)
        {
            float Direction = (MoveValue > 0.f) ? 1.f : -1.f;
            BodyComponent->SetRelativeScale3D(FVector(Direction, 1.f, 1.f));
        }
    }
}

void AMSPlayerCharacter::Input_Crouch() { Crouch(); }
void AMSPlayerCharacter::Input_StopCrouching() { UnCrouch(); }