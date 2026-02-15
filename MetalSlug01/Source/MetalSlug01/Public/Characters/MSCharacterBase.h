#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Character.h"
#include "MSCharacterBase.generated.h"

class UPaperFlipbookComponent;
class UPaperFlipbook;

UCLASS(Abstract)
class METALSLUG01_API AMSCharacterBase : public ACharacter
{
	GENERATED_BODY()

public:
	AMSCharacterBase();

protected:
	virtual void BeginPlay() override;
	virtual void Tick(float DeltaTime) override;

	// --- 核心渲染组件 ---
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MS|Render")
	TObjectPtr<UPaperFlipbookComponent> BodyComponent;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "MS|Render")
	TObjectPtr<UPaperFlipbookComponent> TorsoComponent;
    
	// --- 动画资产槽位 ---
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MS|Animations")
	TObjectPtr<UPaperFlipbook> BodyIdle;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MS|Animations")
	TObjectPtr<UPaperFlipbook> BodyRun;

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MS|Animations")
	TObjectPtr<UPaperFlipbook> BodyCrouch; 

	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MS|Animations")
	TObjectPtr<UPaperFlipbook> TorsoIdle;
    
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MS|Animations")
	TObjectPtr<UPaperFlipbook> JumpAnimation;

	// --- 基础属性 ---
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "MS|Stats")
	float Health = 100.f;

	virtual void UpdateAnimation();
};