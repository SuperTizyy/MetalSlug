#include "Characters/MSCharacterBase.h"
#include "Components/CapsuleComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "PaperFlipbookComponent.h"
#include "PaperFlipbook.h"

AMSCharacterBase::AMSCharacterBase()
{
    PrimaryActorTick.bCanEverTick = true;

    // 1. 初始化组件并挂载
    BodyComponent = CreateDefaultSubobject<UPaperFlipbookComponent>(TEXT("BodyComponent"));
    BodyComponent->SetupAttachment(RootComponent);

    TorsoComponent = CreateDefaultSubobject<UPaperFlipbookComponent>(TEXT("TorsoComponent"));
    TorsoComponent->SetupAttachment(BodyComponent);
    
    // 2. 配置物理移动
    if (UCharacterMovementComponent* MoveComp = GetCharacterMovement())
    {
        MoveComp->bConstrainToPlane = true; 
        MoveComp->SetPlaneConstraintNormal(FVector(0.f, 1.f, 0.f));
        
        MoveComp->GetNavAgentPropertiesRef().bCanCrouch = true;
        // 修正过时 API 警告
        MoveComp->SetCrouchedHalfHeight(60.f); 
        MoveComp->MaxWalkSpeedCrouched = 300.f;
    }
}

void AMSCharacterBase::BeginPlay() 
{ 
    Super::BeginPlay(); 
    // 首帧强制赋予贴图，防止黑屏或延迟显示
    if (BodyComponent && BodyIdle)
    {
        BodyComponent->SetFlipbook(BodyIdle);
    }
}

void AMSCharacterBase::Tick(float DeltaTime) 
{ 
    Super::Tick(DeltaTime); 
    UpdateAnimation(); 
}

void AMSCharacterBase::UpdateAnimation()
{
    if (!BodyComponent) return;
    
    const float Speed = GetVelocity().Size2D();
    const bool bIsFalling = GetCharacterMovement() ? GetCharacterMovement()->IsFalling() : false;

    UPaperFlipbook* TargetBody = BodyIdle;

    if (bIsFalling) { TargetBody = JumpAnimation; }
    else if (bIsCrouched) { TargetBody = BodyCrouch ? BodyCrouch : BodyIdle; }
    else if (Speed > 10.f) { TargetBody = BodyRun; }
    
    if (TargetBody && BodyComponent->GetFlipbook() != TargetBody)
    {
        BodyComponent->SetFlipbook(TargetBody);
    }
}