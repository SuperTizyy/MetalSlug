// ==========================================
// UDissolveComponent 实现 【2026-07-10 大厂 P0 重构 — 职责对等】
// 角色身体溶解, 不再管武器溶解 (武器已下放给 WeaponDissolveComponent)
// ==========================================
#include "Components/DissolveComponent.h"
#include "Components/HealthComponent.h"
#include "Logs/MetalSlugLogChannels.h"
#include "GameFramework/Character.h"
#include "Components/SkeletalMeshComponent.h"
#include "TimerManager.h"
#include "Materials/MaterialInstanceDynamic.h"

UDissolveComponent::UDissolveComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
}


/**
 * UDissolveComponent::BeginPlay
 *
 * 初始化:依赖外部 (BaseCharacter::ExecuteDeathLocal) 主动调 StartDissolveEffect
 * 启动溶解 — Component 本身不订阅 OnDeath, 避免重复触发.
 */
void UDissolveComponent::BeginPlay()
{
	Super::BeginPlay();

	// 外部主动调用 StartDissolveEffect 启动溶解
	// (BaseCharacter::ExecuteDeathLocal 在 OnHealthComponentDeath 时触发)
}


void UDissolveComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 工业规范: 显式清理 Timer
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(DissolveTimerHandle);
	}

	Super::EndPlay(EndPlayReason);
}


/**
 * UDissolveComponent::OnOwnerDeath
 *
 * 死亡回调入口 — 由 Owner Character 在 OnHealthComponentDeath 时调用,
 * 立即启动溶解 (无延迟, 满足 2026-07-01 P0 重构要求).
 */
void UDissolveComponent::OnOwnerDeath()
{
	// 【2026-07-01 P0 重构】立即启动溶解, 不用任何延迟
	UE_LOG(LogCombat, Warning, TEXT("[DissolveComponent] OnOwnerDeath 触发: 立即启动溶解 (无延迟)"));
	StartDissolveEffect();
}


void UDissolveComponent::StartDissolveEffect()
{
	// 收集 Owner 角色自己的骨骼网格材质
	CollectDynamicMaterials();

	// 【Bug 修复】收集失败时不应该进入溶解状态
	// 根因: CollectDynamicMaterials 失败时 (Mesh 找不到等) 直接 return
	//        但 StartDissolveEffect 仍设置 bIsDissolving=true → 进入"伪溶解"状态
	if (DynamicMaterials.Num() == 0)
	{
		UE_LOG(LogCombat, Error,
			TEXT("[DissolveComponent] StartDissolveEffect: 收集材质失败, 不进入伪溶解状态 — Owner=%s"),
			GetOwner() ? *GetOwner()->GetName() : TEXT("<null>"));
		return;
	}

	bIsDissolving = true;
	CurrentDissolveValue = 0.0f;

	// 【Bug 修复】立即启用 Tick — 对比 UDissolveComponent 的构造函数只设置了 bCanEverTick=true
	// bStartWithTickEnabled 默认为 true, 所以理论上应该注册了
	// 但为了安全, 显式启用 Tick
	SetComponentTickEnabled(true);
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

	// 2. 驱动所有 MID 的 DissolveAmount 参数 (身体骨骼网格)
	//
	// 【大厂单一协议 2026.07.10】零兜底
	//   - 只驱动 DynamicMaterials 数组里的 MID (收集阶段已确认有效)
	//   - 不调用 SetScalarParameterValueOnMaterials 无状态 API (那是兜底, 协议不允许)
	//   - 协议不满足 (材质没 DissolveAmount 参数) 时, 该调用是 UE no-op
	//   - 协议未满足应在材质资产侧修复 (美术加 MF_Dissolve 节点)
	for (UMaterialInstanceDynamic* Mat : DynamicMaterials)
	{
		if (Mat)
		{
			Mat->SetScalarParameterValue(FName(TEXT("DissolveAmount")), CurrentDissolveValue);
		}
	}

	// 3. 溶解超过 1.1 时视为完成
	if (CurrentDissolveValue >= 1.1f)
	{
		bIsDissolving = false;
		UE_LOG(LogCombat, Verbose, TEXT("[DissolveComponent] 身体溶解完成: %s"), *GetOwner()->GetName());
		OnDissolveFinished.Broadcast();
	}
}


/**
 * 【大厂 P0 2026.07.10 重构 — 职责收窄】
 * 只收集 Owner 角色自己的骨骼网格材质
 * 武器溶解已下放给 ABaseWeapon::WeaponDissolveComponent, 不再管武器
 *
 * 协议 (单一来源, 零兜底):
 *   - 身体材质蓝图必须调用 MF_Dissolve 节点 (有 DissolveAmount 参数)
 *   - C++ 协议: CreateDynamicMaterialInstance(i, nullptr) → UE 自动复制当前槽材质
 *   - 协议不满足时, 材质能创建但驱动无效, 需在材质资产侧修复
 */
void UDissolveComponent::CollectDynamicMaterials()
{
	if (bMaterialsCollected)
	{
		return;
	}
	bMaterialsCollected = true;

	USkeletalMeshComponent* Mesh = GetOwnerSkeletalMesh();
	if (!Mesh)
	{
		// 【大厂零兜底】没有骨骼网格: 显式失败, 不静默
		UE_LOG(LogCombat, Error,
			TEXT("[DissolveComponent] CollectDynamicMaterials: Owner 没有骨骼网格 — 身体无法溶解 — Owner=%s"),
			GetOwner() ? *GetOwner()->GetName() : TEXT("<null>"));
		return;
	}

	const int32 NumMaterials = Mesh->GetNumMaterials();
	UE_LOG(LogCombat, Log,
		TEXT("[DissolveComponent] CollectDynamicMaterials: 身体 Mesh=%s Materials=%d"),
		*Mesh->GetName(), NumMaterials);

	if (NumMaterials == 0)
	{
		UE_LOG(LogCombat, Error,
			TEXT("[DissolveComponent] 身体骨骼网格没有材质槽 — 不会溶解 — Mesh=%s"),
			*Mesh->GetName());
		return;
	}

	// 协议: CreateDynamicMaterialInstance(i, nullptr) → UE 自动复制当前槽材质
	for (int32 i = 0; i < NumMaterials; i++)
	{
		UMaterialInstanceDynamic* DynMat = Mesh->CreateDynamicMaterialInstance(i, nullptr);
		if (DynMat)
		{
			DynamicMaterials.Add(DynMat);
		}
		else
		{
			UE_LOG(LogCombat, Error,
				TEXT("[DissolveComponent] 身体材质 Slot %d 创建 MID 失败 — Mesh=%s"),
				i, *Mesh->GetName());
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
