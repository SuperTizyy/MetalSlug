// ==========================================
// WeaponDissolveComponent 实现
// 武器专用溶解特效组件 — 职责对等 + 单一协议 + 零兜底
//
// 【2026.07.10 大厂 P0 重构 v2】
//
// 根因 (v1 失败分析):
//   1. StartDissolve 中 CollectDynamicMaterials() 在 StartDissolve 内调用,但检查在调用之后
//      → 如果 CollectDynamicMaterials 失败 (bMaterialsCollected=true, DynamicMaterials=空),
//        StartDissolve 仍设置 bIsDissolving=true, 进入"伪溶解"状态
//   2. CollectDynamicMaterials 失败时提前 return, 设置了 bMaterialsCollected=true
//      → 第二次调用 StartDissolve 时 bMaterialsCollected=true, 直接跳过收集
//      → 永远无法重试收集
//   3. MID 创建后 DissolveAmount 参数没有初始化为 -1 (完全显示)
//      → 如果材质默认 DissolveAmount=0, 武器在溶解前就是半透明状态
//
// v2 修复架构:
//   1. StartDissolve 先检查 bMaterialsCollected, 如果已收集则跳过
//   2. CollectDynamicMaterials 失败时不清空, 让调用方决定是否重试
//   3. MID 创建后立即初始化 DissolveAmount=-1 (完全显示)
//   4. DissolveSpeed 影响溶解时长, DissolveAmount 从 -1 到 1 变化
// ==========================================
#include "Components/WeaponDissolveComponent.h"
#include "Components/MeshComponent.h"
#include "Logs/MetalSlugLogChannels.h"
#include "Materials/MaterialInstanceDynamic.h"

UWeaponDissolveComponent::UWeaponDissolveComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	// 启动时不 Tick — StartDissolve 触发后再开启, 避免无谓 CPU
	PrimaryComponentTick.bStartWithTickEnabled = false;
}


void UWeaponDissolveComponent::BeginPlay()
{
	Super::BeginPlay();
	// 状态: Attached (默认) — 等待 StartDissolve 触发
}


void UWeaponDissolveComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 【P0 修复】正确关闭 Tick
	SetComponentTickEnabled(false);

	Super::EndPlay(EndPlayReason);
}


/**
 * 【公开唯一入口】启动武器溶解
 *
 * 调用方: ABaseCharacter::DropAndFadeWeapon
 *
 * v2 架构 (大厂规范):
 *   1. 幂等: 已 Dissolving 时直接 return
 *   2. 收集: CollectDynamicMaterials 必须在设置 bIsDissolving 之前调用
 *   3. 检查: 收集结果必须在设置 bIsDissolving 之前检查, 失败则 return
 *   4. 启用 Tick: 检查通过后设置 bIsDissolving=true + SetComponentTickEnabled(true)
 *
 * 不允许的用法:
 *   - 在外部直接调 StartDissolve 多次 (幂等保护: 已 Dissolving 则忽略)
 */
void UWeaponDissolveComponent::StartDissolve()
{
	// 【防御 1】已 Dissolving 状态 — 幂等, 直接 return
	if (bIsDissolving)
	{
		UE_LOG(LogCombat, Verbose,
			TEXT("[WeaponDissolveComponent] StartDissolve: 已在 Dissolving 状态, 幂等跳过 — Owner=%s"),
			GetOwner() ? *GetOwner()->GetName() : TEXT("<null>"));
		return;
	}

	AActor* OwnerActor = GetOwner();
	if (!IsValid(OwnerActor))
	{
		// 【大厂零兜底】Owner 已失效: 显式失败, 不静默兼容
		UE_LOG(LogCombat, Error,
			TEXT("[WeaponDissolveComponent] StartDissolve: Owner 已失效, 武器不会溶解 — Owner=%s"),
			OwnerActor ? *OwnerActor->GetName() : TEXT("<null>"));
		return;
	}

	// 【防御 2】确保组件 TickFunction 已注册
	// 根因: WeaponDissolveComponent 在 ABaseWeapon 构造函数中通过 CreateDefaultSubobject 创建
	// bStartWithTickEnabled=false 时组件的 TickFunction 不会被注册
	// 对比: UActorComponent::RegisterComponent() 会调用 RegisterTickFunction()
	if (!IsRegistered())
	{
		UE_LOG(LogCombat, Warning,
			TEXT("[WeaponDissolveComponent] StartDissolve: 组件未注册, 强制 RegisterComponent — Owner=%s"),
			*OwnerActor->GetName());
		RegisterComponent();
	}

	// 【关键 v2 修复】收集材质必须在设置 bIsDissolving 之前
	// 收集失败时不清空 bMaterialsCollected, 让调用方决定是否重试
	const int32 PreviousCount = DynamicMaterials.Num();
	CollectDynamicMaterials();

	// 【关键 v2 修复】检查收集结果必须在设置 bIsDissolving 之前
	// 根因 (v1 bug): 如果在设置 bIsDissolving=true 之后才发现 DynamicMaterials.Num()==0,
	//   → 进入"伪溶解"状态: bIsDissolving=true 但没有任何 MID 可驱动
	//   → TickComponent 的 for 循环是空操作, CurrentDissolveValue 永远不累加
	//   → 武器溶解永远不完成, SetLifeSpan 永远不触发
	if (DynamicMaterials.Num() == 0)
	{
		// 【显式错误】收集材质失败, 不进入"伪溶解"状态
		// 根因: Mesh 找不到, 或材质槽为空, 或所有材质都没有 DissolveAmount 参数
		// 解决: 在武器材质蓝图 (M_Weapon_*) 中添加 MF_Dissolve 材质函数节点
		UE_LOG(LogCombat, Error,
			TEXT("[WeaponDissolveComponent] StartDissolve: 收集材质失败, 不进入伪溶解状态 — "
				 "Weapon=%s, PreviousCount=%d. "
				 "请检查: 1) 武器有 UMeshComponent; 2) 材质槽非空; "
				 "3) 材质蓝图 (M_*) 调用了 MF_Dissolve 节点."),
			*OwnerActor->GetName(), PreviousCount);

		// 【关键 v2 修复】不清空 bMaterialsCollected, 允许重试
		// 如果是第一次失败 (bMaterialsCollected 仍为 false), 下次调用 StartDissolve 可以重试
		// 如果已经收集过但材质有问题 (bMaterialsCollected=true), 说明是协议未满足
		return;
	}

	// 【收集成功】设置溶解状态
	bIsDissolving = true;
	CurrentDissolveValue = -1.0f; // 【关键 v2 修复】DissolveAmount 范围: -1=完全显示, 1=完全消失

	// 【关键 v2 修复】正确启用 Tick
	// 对比 UDissolveComponent: 它在构造函数设置 bCanEverTick=true, 但不设置 bStartWithTickEnabled
	// (默认为 true), 所以它的 TickFunction 始终注册, 只需要设置 bIsDissolving 即可
	// 我们这里 bStartWithTickEnabled=false, 所以需要显式调用 SetComponentTickEnabled
	SetComponentTickEnabled(true);

	UE_LOG(LogCombat, Log,
		TEXT("[WeaponDissolveComponent] StartDissolve: 武器溶解已启动 — "
			 "Weapon=%s MIDs=%d Speed=%.2f InitialDissolveAmount=%.1f"),
		*OwnerActor->GetName(), DynamicMaterials.Num(), DissolveSpeed, CurrentDissolveValue);
}


/**
 * 【大厂单一协议】收集武器 Mesh 的动态材质
 *
 * v2 修复:
 *   - 失败时不清空 bMaterialsCollected, 允许重试
 *   - MID 创建后立即初始化 DissolveAmount=-1 (完全显示)
 *   - 协议: 武器材质蓝图必须包含 MF_Dissolve 节点 (有 DissolveAmount 参数)
 */
void UWeaponDissolveComponent::CollectDynamicMaterials()
{
	// 【防御】已收集则跳过 (允许重试: 如果上次失败, bMaterialsCollected 仍为 false)
	if (bMaterialsCollected)
	{
		return;
	}

	AActor* OwnerActor = GetOwner();
	if (!IsValid(OwnerActor))
	{
		UE_LOG(LogCombat, Error,
			TEXT("[WeaponDissolveComponent] CollectDynamicMaterials: Owner 已失效"));
		return;
	}

	// 收集自己 Owner 的 Mesh 组件
	UMeshComponent* Mesh = OwnerActor->FindComponentByClass<UMeshComponent>();
	if (!Mesh)
	{
		UE_LOG(LogCombat, Error,
			TEXT("[WeaponDissolveComponent] CollectDynamicMaterials: Owner 没有 UMeshComponent — Weapon=%s"),
			*OwnerActor->GetName());
		return;
	}

	const int32 NumMaterials = Mesh->GetNumMaterials();
	UE_LOG(LogCombat, Log,
		TEXT("[WeaponDissolveComponent] CollectDynamicMaterials: Weapon=%s Mesh=%s Materials=%d"),
		*OwnerActor->GetName(), *Mesh->GetName(), NumMaterials);

	if (NumMaterials == 0)
	{
		UE_LOG(LogCombat, Error,
			TEXT("[WeaponDissolveComponent] CollectDynamicMaterials: Mesh 没有材质槽 — Weapon=%s"),
			*OwnerActor->GetName());
		return;
	}

	// 【关键 v2 修复】MID 创建后立即初始化 DissolveAmount=-1
	// 根因: DissolveAmount 参数没有默认值, 如果材质默认是 0,
	//   武器在溶解前就是半透明状态
	// 修复: CreateDynamicMaterialInstance 后立即设置 DissolveAmount=-1 (完全显示)
	for (int32 i = 0; i < NumMaterials; i++)
	{
		UMaterialInstanceDynamic* DynMat = Mesh->CreateDynamicMaterialInstance(i, nullptr);
		if (DynMat)
		{
			// 【关键 v2 修复】立即初始化 DissolveAmount=-1
			// DissolveAmount 范围: -1=完全显示, 1=完全消失
			DynMat->SetScalarParameterValue(FName(TEXT("DissolveAmount")), -1.0f);
			DynamicMaterials.Add(DynMat);

			// 【大厂协议验证】检查材质是否有 DissolveAmount 参数
			if (!ValidateMaterialHasDissolveParameter(DynMat))
			{
				UE_LOG(LogCombat, Warning,
					TEXT("[WeaponDissolveComponent] 协议不满足: Slot %d 材质没有 'DissolveAmount' 参数 — "
						 "请在材质蓝图 (M_*) 中调用 MF_Dissolve 节点 — Weapon=%s"),
					i, *OwnerActor->GetName());
				// 不阻塞: MID 仍加入数组, 让溶解继续 (只是没有溶解特效)
			}
		}
		else
		{
			UE_LOG(LogCombat, Error,
				TEXT("[WeaponDissolveComponent] CreateDynamicMaterialInstance 失败 — slot=%d Weapon=%s"),
				i, *OwnerActor->GetName());
		}
	}

	// 【关键 v2 修复】只有成功收集到至少一个 MID 才标记 bMaterialsCollected=true
	// 如果所有材质都创建失败, bMaterialsCollected 仍为 false, 下次 StartDissolve 可以重试
	if (DynamicMaterials.Num() > 0)
	{
		bMaterialsCollected = true;
		UE_LOG(LogCombat, Log,
			TEXT("[WeaponDissolveComponent] CollectDynamicMaterials: 成功收集 %d 个 MID — Weapon=%s"),
			DynamicMaterials.Num(), *OwnerActor->GetName());
	}
	else
	{
		UE_LOG(LogCombat, Error,
			TEXT("[WeaponDissolveComponent] CollectDynamicMaterials: 所有材质都创建失败 — Weapon=%s"),
			*OwnerActor->GetName());
		// 不设置 bMaterialsCollected, 允许重试
	}
}


/**
 * 【大厂协议验证】检查材质是否包含 DissolveAmount 参数
 *
 * 使用 UE 5.6 GetAllScalarParameterInfo API 检查
 */
bool UWeaponDissolveComponent::ValidateMaterialHasDissolveParameter(UMaterialInstanceDynamic* MID) const
{
	if (!MID)
	{
		return false;
	}

	TArray<FMaterialParameterInfo> ScalarInfo;
	TArray<FGuid> ScalarGuids;
	MID->GetAllScalarParameterInfo(ScalarInfo, ScalarGuids);

	for (const FMaterialParameterInfo& Info : ScalarInfo)
	{
		if (Info.Name == FName(TEXT("DissolveAmount")))
		{
			return true;
		}
	}
	return false;
}


/**
 * @brief Tick 驱动 — 累加 DissolveAmount 进度并刷新所有材质 MID
 * @param DeltaTime 帧间隔
 * @param TickType Tick 类型
 * @param ThisTickFunction Tick 函数引用
 *
 * 大厂原则 (v24):
 *   - 协议: 武器材质蓝图必须含 DissolveAmount 参数, 缺失则驱动无效 (UE no-op)
 *   - 范围: CurrentDissolveValue 从 -1 → 1, 钳到 [-1, 1]
 *   - 完成 (>=1.0) 自动关 Tick + 广播 OnWeaponDissolveFinished
 */
void UWeaponDissolveComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	if (!bIsDissolving)
	{
		return;
	}

	// 1. 累加溶解进度 (从 -1 到 1)
	CurrentDissolveValue += DeltaTime * DissolveSpeed;

	// 限制在 [-1, 1] 范围内
	if (CurrentDissolveValue > 1.0f)
	{
		CurrentDissolveValue = 1.0f;
	}

	// 2. 驱动所有 MID 的 DissolveAmount 参数
	// DissolveAmount 范围: -1=完全显示, 1=完全消失
	for (UMaterialInstanceDynamic* Mat : DynamicMaterials)
	{
		if (Mat)
		{
			Mat->SetScalarParameterValue(FName(TEXT("DissolveAmount")), CurrentDissolveValue);
		}
	}

	// 3. 溶解值 >= 1.0 时视为完成
	if (CurrentDissolveValue >= 1.0f)
	{
		bIsDissolving = false;
		SetComponentTickEnabled(false); // 关闭 Tick

		if (AActor* OwnerActor = GetOwner())
		{
			UE_LOG(LogCombat, Log,
				TEXT("[WeaponDissolveComponent] 溶解完成, 立即禁用碰撞并销毁 — Weapon=%s"),
				*OwnerActor->GetName());

			// 【P0 修复 2026.07.10】立即禁用碰撞,防止阻挡 AI 复活
			// 根因: 武器溶解完成后仍有碰撞(PhysicsOnly + SimulatePhysics)
			//        AI 复活时在原点生成遇到残留武器碰撞 → SpawnActor 失败
			// 修复: 溶解完成后立即禁用所有碰撞,让 AI 可以正常复活
			if (UMeshComponent* MeshComp = OwnerActor->FindComponentByClass<UMeshComponent>())
			{
				MeshComp->SetCollisionEnabled(ECollisionEnabled::NoCollision);
				MeshComp->SetSimulatePhysics(false);
			}

			// 销毁武器
			OwnerActor->SetLifeSpan(0.1f);
		}
		OnWeaponDissolveFinished.Broadcast();
	}
}
