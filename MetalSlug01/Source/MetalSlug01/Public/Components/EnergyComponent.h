// ==========================================
// 能量 Component
// 目的: 抽离 BaseCharacter 能量管理
// ==========================================
#pragma once

#include "CoreMinimal.h"
#include "Components/ActorComponent.h"
#include "EnergyComponent.generated.h"

DECLARE_DYNAMIC_MULTICAST_DELEGATE_OneParam(FOnEnergyChanged, float, NewEnergy);

UCLASS(ClassGroup = (MetalSlug), meta = (BlueprintSpawnableComponent))
class METALSLUG01_API UEnergyComponent : public UActorComponent
{
	GENERATED_BODY()

public:
	UEnergyComponent();

	void InitializeEnergy(float InMax, float InCurrent);

	UFUNCTION(BlueprintCallable, Category = "Energy")
	bool Consume(float Amount);

	UFUNCTION(BlueprintCallable, Category = "Energy")
	void Add(float Amount);

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Energy")
	float GetCurrent() const { return CurrentEnergy; }

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Energy")
	float GetMax() const { return MaxEnergy; }

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "Energy")
	float GetPercent() const { return MaxEnergy > 0 ? CurrentEnergy / MaxEnergy : 0.0f; }

	UPROPERTY(BlueprintAssignable, Category = "Energy")
	FOnEnergyChanged OnEnergyChanged;

protected:
	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Energy")
	float MaxEnergy = 100.0f;

	UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Energy")
	float CurrentEnergy = 0.0f;
};
