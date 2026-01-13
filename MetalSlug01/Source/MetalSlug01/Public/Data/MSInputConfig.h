#pragma once

#include "CoreMinimal.h"
#include "Engine/DataAsset.h"
#include "InputAction.h"
#include "MSInputConfig.generated.h"

/**
 * 工业级做法：将所有 InputAction 整合进一个 DataAsset
 * 这样策划可以在编辑器里直接拖拽分配按键，而无需修改 C++ 代码
 */
UCLASS()
class METALSLUG01_API UMSInputConfig : public UDataAsset
{
	GENERATED_BODY()

public:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> MoveAction; // 左右移动

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> JumpAction; // 跳跃

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> ShootAction; // 射击

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Input")
	TObjectPtr<UInputAction> CrouchAction; // 蹲下
};