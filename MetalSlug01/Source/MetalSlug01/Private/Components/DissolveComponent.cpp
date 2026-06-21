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

	// 【2026-07-01 P0 重构】移除 OnDeath 订阅, 改为外部主动调用 StartDissolveImmediate
	//
	// 旧架构 bug:
	//   - DissolveComponent::OnOwnerDeath 用 DissolveDelay=5s 定时器启动溶解
	//   - 但 RespawnDelaySeconds=3s, 复活定时器比溶解早 2 秒触发
	//   - 角色被销毁, 溶解流程永远跑不到 → 身体"立马消失"
	//
	// 新架构:
	//   - BaseCharacter::ExecuteDeathLocal 直接调 StartDissolveImmediate (无延迟)
	//   - 溶解速度由 DissolveSpeed 控制, 典型 1.0~2.0 秒内完成
	//   - RespawnDelaySeconds 必须 > 溶解完成时间, 否则角色提前销毁看不到溶解
	//   - 集中编排: 死亡 → 立即溶解 → 等待溶解完成 → 复活 → 销毁旧角色
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
	// 【2026-07-01 P0 重构】原代码使用 DissolveDelay=5s 定时器, 与 RespawnDelay=3s 冲突
	// 新设计: 立即启动溶解, 不用任何延迟
	UE_LOG(LogCombat, Warning, TEXT("[DissolveComponent] OnOwnerDeath 触发: 立即启动溶解 (无延迟)"));

	// 立即开始溶解效果 (无延迟)
	StartDissolveEffect();
}


/**
 * 【2026-07-01 P0 新增】立即启动溶解 (公开 API)
 * 死亡流程编排: BaseCharacter::ExecuteDeathLocal 调用, 替代旧 BeginPlay 订阅 OnDeath
 * 这样死亡编排器可以精确控制溶解时序 (与复活定时器协调)
 */
void UDissolveComponent::StartDissolveImmediate()
{
	UE_LOG(LogCombat, Warning, TEXT("[DissolveComponent] StartDissolveImmediate: 立即开始溶解"));
	StartDissolveEffect();
}


void UDissolveComponent::CollectWeaponDynamicMaterials(UMeshComponent* WeaponMesh)
{
	if (!WeaponMesh)
	{
		UE_LOG(LogCombat, Warning, TEXT("[DissolveComponent] CollectWeaponDynamicMaterials: WeaponMesh 为空, 跳过武器溶解"));
		return;
	}

	// 防御性检查: 武器是否已被销毁/标记为 pending kill
	if (!IsValid(WeaponMesh) || WeaponMesh->IsBeingDestroyed())
	{
		UE_LOG(LogCombat, Warning, TEXT("[DissolveComponent] CollectWeaponDynamicMaterials: 武器 Mesh 已失效, 跳过溶解"));
		return;
	}

	UE_LOG(LogCombat, Log,
		TEXT("[DissolveComponent] CollectWeaponDynamicMaterials: %s (Materials=%d)"),
		*WeaponMesh->GetName(), WeaponMesh->GetNumMaterials());

	// 为武器每个材质槽创建动态材质实例, 加入驱动队列
	for (int32 i = 0; i < WeaponMesh->GetNumMaterials(); i++)
	{
		UMaterialInstanceDynamic* DynMat = WeaponMesh->CreateDynamicMaterialInstance(i, DissolveMaterial);
		if (DynMat)
		{
			DynamicMaterials.Add(DynMat);
		}
	}

	// 如果溶解流程已经在跑 (Tick 已激活), 新加入的材质会自动在下一帧被驱动
	if (!bIsDissolving)
	{
		bIsDissolving = true;
		CurrentDissolveValue = 0.0f;
		UE_LOG(LogCombat, Log, TEXT("[DissolveComponent] 启动武器溶解 (手动触发)"));
	}
}


void UDissolveComponent::CollectAllDynamicMaterials()
{
	// 重置幂等标志后重新收集 - 允许在 OnDeath 之前主动触发
	bMaterialsCollected = false;
	CollectDynamicMaterials();
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
