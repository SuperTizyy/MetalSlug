#pragma once

#include "CoreMinimal.h"
#include "Characters/MSCharacterBase.h"
#include "InputActionValue.h"
#include "MSPlayerCharacter.generated.h"

class USpringArmComponent;
class UCameraComponent;
class UInputMappingContext;
class UMSInputConfig;

UCLASS()
class METALSLUG01_API AMSPlayerCharacter : public AMSCharacterBase
{
	GENERATED_BODY()

public:
	AMSPlayerCharacter();

protected:
	virtual void BeginPlay() override;
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MS|Camera")
	TObjectPtr<USpringArmComponent> CameraBoom;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MS|Camera")
	TObjectPtr<UCameraComponent> FollowCamera;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MS|Input")
	TObjectPtr<UInputMappingContext> DefaultMappingContext;

	UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "MS|Input")
	TObjectPtr<UMSInputConfig> InputConfig;

	// 回调函数
	void Input_Move(const FInputActionValue& Value);
	void Input_Crouch();
	void Input_StopCrouching();
};