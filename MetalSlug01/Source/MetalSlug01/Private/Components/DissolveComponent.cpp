// ==========================================
// UDissolveComponent 实现 【2026-06-15 重构: 完整迁移 BaseCharacter 溶解逻辑】
// ==========================================
#include "Components/DissolveComponent.h"
#include "Components/HealthComponent.h"
#include "Logs/MetalSlugLogChannels.h"
#include "GameFramework/Character.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/MeshComponent.h"
#include "TimerManager.h"
#include "Materials/MaterialInstanceDynamic.h"
#include "Weapons/BaseWeapon.h"

UDissolveComponent::UDissolveComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}


void UDissolveComponent::BeginPlay()
{
	Super::BeginPlay();

	// 订阅 HealthComponent 死亡事件（如果 Owner 有的话）
	// 这样外部不需要手动调用 StartDissolve，HealthComponent 死亡自动触发
	if (ACharacter* OwnerChar = Cast<ACharacter>(GetOwner()))
	{
		if (UHealthComponent* HealthComp = OwnerChar->FindComponentByClass<UHealthComponent>())
		{
			HealthComp->OnDeath.AddUniqueDynamic(this, &UDissolveComponent::OnOwnerDeath);
		}
	}
}


void UDissolveComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 工业规范：显式清理 Timer（防止快速死亡-重生-再死亡场景下残留）
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(DissolveTimerHandle);
	}

	Super::EndPlay(EndPlayReason);
}


void UDissolveComponent::OnOwnerDeath()
{
	// 死亡后延迟 DissolveDelay 秒再开始溶解
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(DissolveTimerHandle);
		World->GetTimerManager().SetTimer(
			DissolveTimerHandle,
			this,
			&UDissolveComponent::StartDissolveEffect,
			DissolveDelay,
			false
		);
	}
}


void UDissolveComponent::StartDissolveEffect()
{
	// 收集 Owner 角色 + 武器的动态材质
	CollectDynamicMaterials();
	bIsDissolving = true;
	CurrentDissolveValue = 0.0f;
}


void UDissolveComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!bIsDissolving)
	{
		return;
	}

	// 1. 累加溶解进度
	CurrentDissolveValue += DeltaTime * DissolveSpeed;

	// 2. 驱动所有动态材质的 DissolveAmount 参数
	for (UMaterialInstanceDynamic* Mat : DynamicMaterials)
	{
		if (Mat)
		{
			Mat->SetScalarParameterValue(FName("DissolveAmount"), CurrentDissolveValue);
		}
	}

	// 3. 溶解超过 1.1 时视为完成（留余量确保完全消失）
	if (CurrentDissolveValue >= 1.1f)
	{
		bIsDissolving = false;
		UE_LOG(LogCombat, Verbose, TEXT("[DissolveComponent] 溶解完成: %s"), *GetOwner()->GetName());
		OnDissolveFinished.Broadcast();
	}
}


void UDissolveComponent::CollectDynamicMaterials()
{
	if (bMaterialsCollected)
	{
		return;
	}
	bMaterialsCollected = true;

	// 收集 Owner 骨骼网格的动态材质
	if (USkeletalMeshComponent* Mesh = GetOwnerSkeletalMesh())
	{
		for (int32 i = 0; i < Mesh->GetNumMaterials(); i++)
		{
			UMaterialInstanceDynamic* DynMat = Mesh->CreateDynamicMaterialInstance(i, DissolveMaterial);
			if (DynMat)
			{
				DynamicMaterials.Add(DynMat);
			}
		}
	}

	// 收集 Owner 武器的动态材质
	if (UMeshComponent* WeaponMesh = GetOwnerWeaponMesh())
	{
		for (int32 i = 0; i < WeaponMesh->GetNumMaterials(); i++)
		{
			UMaterialInstanceDynamic* DynMat = WeaponMesh->CreateDynamicMaterialInstance(i, DissolveMaterial);
			if (DynMat)
			{
				DynamicMaterials.Add(DynMat);
			}
		}
	}
}


USkeletalMeshComponent* UDissolveComponent::GetOwnerSkeletalMesh() const
{
	if (ACharacter* Char = Cast<ACharacter>(GetOwner()))
	{
		return Char->GetMesh();
	}
	return nullptr;
}


UMeshComponent* UDissolveComponent::GetOwnerWeaponMesh() const
{
	if (ACharacter* Char = Cast<ACharacter>(GetOwner()))
	{
		// 泛型查找：不管武器组件叫什么，直接找武器上的 Mesh 组件
		return Char->FindComponentByClass<UMeshComponent>();
	}
	return nullptr;
}
