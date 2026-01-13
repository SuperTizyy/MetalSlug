#pragma once

#include "CoreMinimal.h"
#include "Characters/MSCharacterBase.h"
#include "InputActionValue.h" 
#include "MSPlayerCharacter.generated.h"

//#include "InputActionValue.h"  必须包含，用于获取按键数值

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

	// 工业级规范：重写设置输入组件的方法
	virtual void SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent) override;

	// --- 输入资产引用 ---
    
	UPROPERTY(EditDefaultsOnly, Category = "MS|Input")
	TObjectPtr<UInputMappingContext> DefaultMappingContext;

	UPROPERTY(EditDefaultsOnly, Category = "MS|Input")
	TObjectPtr<UMSInputConfig> InputConfig;

	// --- 输入处理函数 ---

	void Input_Move(const FInputActionValue& Value);  // 处理移动
	void Input_Jump();                                // 处理跳跃
	void Input_Crouch();                              // 处理蹲下
	void Input_StopCrouching();                       // 停止蹲下
	void Input_Shoot();                               // 处理射击

private:
	// 内部状态控制
	void UpdateSpriteFacing(float MoveValue);         // 处理 2D 镜像转向
};