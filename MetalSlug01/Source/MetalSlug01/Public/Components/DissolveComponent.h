// ==========================================
// 溶解特效 Component
// 目的: 抽离 BaseCharacter 的死亡后材质溶解效果
// ==========================================
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "DissolveComponent.generated.h"

class UMaterialInterface;
class UMaterialInstanceDynamic;

UCLASS(ClassGroup = (MetalSlug), meta = (BlueprintSpawnableComponent))
class METALSLUG01_API UDissolveComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UDissolveComponent();

	/** 开始溶解 (由 HealthComponent 死亡后调用) */
	UFUNCTION(BlueprintCallable, Category = "Dissolve")
	void StartDissolve();

	/** 是否正在溶解中 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Dissolve")
	bool IsDissolving() const { return bIsDissolving; }

protected:
	virtual void TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction) override;

	/** 溶解时使用的材质 (动态实例) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dissolve")
	TObjectPtr<UMaterialInterface> DissolveMaterial = nullptr;

	/** 溶解时长 (秒) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Dissolve")
	float DissolveDuration = 3.0f;

	/** 溶解进度 0.0~1.0 */
	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dissolve")
	float DissolveProgress = 0.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Dissolve")
	bool bIsDissolving = false;
};
