// ==========================================
// 母体残血虚弱闪烁 Component 实现 【v101.4 大厂架构 — C++ 自己驱动闪烁效果，不依赖 BP Timeline】
//
// 架构 (v101.4 重构):
//   HealthComponent (数据) → OnHealthChanged (事件, Replicated 同步)
//      ↓
//   UMotherLowHealthFlickerComponent (视觉编排)
//      ├─ HandleHealthChanged: 订阅 + 阈值检测 → SetIsWeakFlickering (集中入口)
//      ├─ OnRep_WeakFlickeringChanged: 启动/停止 Timer
//      ├─ StartFlickerTimer/OnFlickerTimerTick: C++ 自己驱动 FlickerAmount
//      ├─ PrepareMaterials: 收集 MID, 验证协议
//      └─ 零跨边界: 不触碰武器, 不触碰 HealthComp 数据
// ==========================================
#include "Combat/MotherLowHealthFlickerComponent.h"

// BaseCharacter: ResolveHealthComponent / GetMesh
#include "Characters/BaseCharacter.h"

// HealthComponent: OnHealthChanged 订阅
#include "Components/HealthComponent.h"

// Material / Mesh
#include "Materials/MaterialInstanceDynamic.h"
#include "Materials/MaterialInterface.h"  // FMaterialParameterInfo + GetAllScalarParameterInfo
#include "Components/SkeletalMeshComponent.h"

// 网络复制支持
#include "Net/UnrealNetwork.h"

// TimerManager
#include "Engine/World.h"
#include "TimerManager.h"


// ==========================================
// 1. 构造函数
// ==========================================
UMotherLowHealthFlickerComponent::UMotherLowHealthFlickerComponent()
{
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.bStartWithTickEnabled = false;

	SetIsReplicatedByDefault(true);
}


// ==========================================
// 2. 网络复制支持
// ==========================================
void UMotherLowHealthFlickerComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(UMotherLowHealthFlickerComponent, bIsWeakFlickering);
}


// ==========================================
// 3. UE 生命周期
// ==========================================
void UMotherLowHealthFlickerComponent::BeginPlay()
{
	Super::BeginPlay();

	UE_LOG(LogTemp, Log,
		TEXT("[MotherLowHealthFlickerComponent] BeginPlay: Owner=%s, Threshold=%.1f, Parameter=%s, SlotFilter.Num=%d"),
		*GetNameSafe(GetOwner()),
		WeakHealthThreshold,
		*FlickerMaterialParameterName.ToString(),
		TargetMaterialSlotIndices.Num());

	ABaseCharacter* OwnerChar = Cast<ABaseCharacter>(GetOwner());
	if (!OwnerChar)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[MotherLowHealthFlickerComponent] BeginPlay: Owner 不是 ABaseCharacter (Owner=%s). "
				 "本组件必须挂在 ABaseCharacter 上."),
			*GetNameSafe(GetOwner()));
		return;
	}

	UHealthComponent* HC = OwnerChar->ResolveHealthComponent();
	if (!HC)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[MotherLowHealthFlickerComponent] BeginPlay: Owner 找不到 HealthComponent (Owner=%s). "
				 "检查 BP 是否挂载 HealthComponent."),
			*GetNameSafe(GetOwner()));
		return;
	}

	CachedHealthComponent = HC;

	// 订阅 OnHealthChanged
	HC->OnHealthChanged.AddDynamic(this, &UMotherLowHealthFlickerComponent::HandleHealthChanged);

	UE_LOG(LogTemp, Log,
		TEXT("[MotherLowHealthFlickerComponent] BeginPlay: 成功订阅 OnHealthChanged. Owner=%s, HC=%s, "
			 "当前 CurrentHealth=%.1f, Threshold=%.1f"),
		*OwnerChar->GetName(),
		*HC->GetName(),
		HC->GetCurrent(),
		WeakHealthThreshold);

	// 边缘 case: BeginPlay 时已经处于残血状态
	HandleHealthChanged(HC->GetCurrent());

	// 标记 BeginPlay 完成
	bHasBeginPlayCompleted = true;
}


void UMotherLowHealthFlickerComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// 空实现 — 闪烁效果由 Timer 驱动，不需要 Tick
}


void UMotherLowHealthFlickerComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 停止 Timer
	StopFlickerTimer();

	// 重置材质
	ResetFlicker();

	// 取消订阅
	if (UHealthComponent* HC = CachedHealthComponent.Get())
	{
		HC->OnHealthChanged.RemoveDynamic(this, &UMotherLowHealthFlickerComponent::HandleHealthChanged);
	}
	CachedHealthComponent.Reset();

	Super::EndPlay(EndPlayReason);
}


// ==========================================
// 4. 核心事件处理 — HealthComponent 血量变化 + 阈值检测
// ==========================================
void UMotherLowHealthFlickerComponent::HandleHealthChanged(float NewHealth)
{
	const bool bShouldFlicker = NewHealth <= WeakHealthThreshold;

	UE_LOG(LogTemp, Display,
		TEXT("[MotherLowHealthFlickerComponent] HandleHealthChanged: Owner=%s, NewHealth=%.1f, "
			 "Threshold=%.1f, bShouldFlicker=%d, 当前 bIsWeakFlickering=%d"),
		*GetNameSafe(GetOwner()),
		NewHealth,
		WeakHealthThreshold,
		bShouldFlicker ? 1 : 0,
		bIsWeakFlickering ? 1 : 0);

	SetIsWeakFlickering(bShouldFlicker);
}


void UMotherLowHealthFlickerComponent::OnRep_WeakFlickeringChanged()
{
	UE_LOG(LogTemp, Log,
		TEXT("[MotherLowHealthFlickerComponent] OnRep_WeakFlickeringChanged: Owner=%s, bIsWeakFlickering=%d, bHasBeginPlayCompleted=%d"),
		*GetNameSafe(GetOwner()),
		bIsWeakFlickering ? 1 : 0,
		bHasBeginPlayCompleted ? 1 : 0);

	if (!bHasBeginPlayCompleted)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[MotherLowHealthFlickerComponent] OnRep_WeakFlickeringChanged: BeginPlay 尚未完成, 跳过处理 (Owner=%s, bIsWeakFlickering=%d). "
				 "等待 HandleHealthChanged 后续处理."),
			*GetNameSafe(GetOwner()),
			bIsWeakFlickering ? 1 : 0);
		return;
	}

	if (bIsWeakFlickering)
	{
		// 进入虚弱闪烁 — 启动 Timer
		StartFlickerTimer();
	}
	else
	{
		// 退出虚弱闪烁 — 停止 Timer + 重置材质
		StopFlickerTimer();
		ResetFlicker();
	}
}


void UMotherLowHealthFlickerComponent::SetIsWeakFlickering(bool bNewState)
{
	if (bIsWeakFlickering == bNewState)
	{
		return;
	}

	bIsWeakFlickering = bNewState;

	if (GetOwnerRole() == ROLE_Authority)
	{
		OnRep_WeakFlickeringChanged();
	}
}


// ==========================================
// 5. Timer 驱动闪烁
// ==========================================
void UMotherLowHealthFlickerComponent::StartFlickerTimer()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// 准备材质（懒加载）
	if (!bMaterialsPrepared)
	{
		PrepareMaterials();
	}

	// 如果没有有效材质，不启动 Timer
	if (FlickerMaterials.Num() == 0)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[MotherLowHealthFlickerComponent] StartFlickerTimer: 没有有效材质, 不启动闪烁. Owner=%s"),
			*GetNameSafe(GetOwner()));
		return;
	}

	// 记录开始时间
	FlickerStartTime = World->GetTimeSeconds();

	// 设置 Timer
	World->GetTimerManager().SetTimer(FlickerTimerHandle, this, &UMotherLowHealthFlickerComponent::OnFlickerTimerTick, FlickerTimerInterval, true);

	UE_LOG(LogTemp, Display,
		TEXT("[MotherLowHealthFlickerComponent] StartFlickerTimer: 启动闪烁. Owner=%s, Period=%.2fs, Interval=%.3fs"),
		*GetNameSafe(GetOwner()),
		FlickerPeriodSeconds,
		FlickerTimerInterval);
}


void UMotherLowHealthFlickerComponent::StopFlickerTimer()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	if (World->GetTimerManager().IsTimerActive(FlickerTimerHandle))
	{
		World->GetTimerManager().ClearTimer(FlickerTimerHandle);
		UE_LOG(LogTemp, Display,
			TEXT("[MotherLowHealthFlickerComponent] StopFlickerTimer: 停止闪烁. Owner=%s"),
			*GetNameSafe(GetOwner()));
	}
}


void UMotherLowHealthFlickerComponent::OnFlickerTimerTick()
{
	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// 如果不再虚弱，停止 Timer
	if (!bIsWeakFlickering)
	{
		StopFlickerTimer();
		return;
	}

	// 如果没有有效材质，停止 Timer
	if (FlickerMaterials.Num() == 0)
	{
		StopFlickerTimer();
		return;
	}

	// 计算正弦波 FlickerAmount
	// FlickerAmount = (sin(2π * (elapsed / period) + 1) / 2
	// 这样在 [0, period] 时间内，FlickerAmount 从 0→1→0 循环
	float ElapsedTime = World->GetTimeSeconds() - FlickerStartTime;
	float Phase = (2.0f * PI) * (ElapsedTime / FlickerPeriodSeconds);
	float FlickerAmount = (FMath::Sin(Phase) + 1.0f) * 0.5f;

	// 设置材质参数
	SetFlickerAmountInternal(FlickerAmount);
}


// ==========================================
// 6. Material 操作
// ==========================================
void UMotherLowHealthFlickerComponent::PrepareMaterials()
{
	if (bMaterialsPrepared)
	{
		return;
	}
	bMaterialsPrepared = true;

	FlickerMaterials.Empty();

	USkeletalMeshComponent* Mesh = GetOwnerSkeletalMesh();
	if (!Mesh)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[MotherLowHealthFlickerComponent] PrepareMaterials: Owner 没有 SkeletalMesh Component "
				 "(Owner=%s). 虚弱闪烁无法工作."),
			*GetNameSafe(GetOwner()));
		return;
	}

	const int32 NumMaterials = Mesh->GetNumMaterials();
	UE_LOG(LogTemp, Log,
		TEXT("[MotherLowHealthFlickerComponent] PrepareMaterials: Mesh=%s, NumMaterials=%d, "
			 "SlotFilter.Num=%d, Parameter=%s"),
		*Mesh->GetName(),
		NumMaterials,
		TargetMaterialSlotIndices.Num(),
		*FlickerMaterialParameterName.ToString());

	if (NumMaterials == 0)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[MotherLowHealthFlickerComponent] PrepareMaterials: Mesh=%s 没有 Material Slot."),
			*Mesh->GetName());
		return;
	}

	int32 CollectedCount = 0;
	int32 ProtocolFailedCount = 0;

	for (int32 i = 0; i < NumMaterials; i++)
	{
		// Slot 过滤
		if (TargetMaterialSlotIndices.Num() > 0 && !TargetMaterialSlotIndices.Contains(i))
		{
			continue;
		}

		// 创建 MID
		UMaterialInstanceDynamic* DynMat = Mesh->CreateDynamicMaterialInstance(i, nullptr);
		if (!DynMat)
		{
			continue;
		}

		// 协议验证
		if (!ValidateMaterialHasFlickerParameter(DynMat))
		{
			ProtocolFailedCount++;
			continue;
		}

		FlickerMaterials.Add(DynMat);
		CollectedCount++;
	}

	if (CollectedCount == 0)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[MotherLowHealthFlickerComponent] PrepareMaterials: 没有任何 Slot 通过协议验证 "
				 "(Owner=%s, Mesh=%s). 虚弱闪烁不会启动. "
				 "修复: 在材质蓝图加 ScalarParameter \"%s\" 并连到 Emissive 或 BaseColor."),
			*GetNameSafe(GetOwner()),
			*Mesh->GetName(),
			*FlickerMaterialParameterName.ToString());
	}
	else if (ProtocolFailedCount > 0)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[MotherLowHealthFlickerComponent] PrepareMaterials: 部分 Slot 通过验证 "
				 "(Owner=%s, 通过=%d, 失败=%d). 失败的 Slot 不会闪烁."),
			*GetNameSafe(GetOwner()),
			CollectedCount,
			ProtocolFailedCount);
	}
	else
	{
		UE_LOG(LogTemp, Log,
			TEXT("[MotherLowHealthFlickerComponent] PrepareMaterials: 全部 %d 个 Slot 通过协议验证."),
			CollectedCount);
	}
}


bool UMotherLowHealthFlickerComponent::ValidateMaterialHasFlickerParameter(UMaterialInstanceDynamic* Mat) const
{
	if (!Mat)
	{
		return false;
	}

	TArray<FMaterialParameterInfo> ScalarParams;
	TArray<FGuid> ScalarParamIds;
	Mat->GetAllScalarParameterInfo(ScalarParams, ScalarParamIds);

	for (const FMaterialParameterInfo& Info : ScalarParams)
	{
		if (Info.Name == FlickerMaterialParameterName)
		{
			return true;
		}
	}

	UE_LOG(LogTemp, Warning,
		TEXT("[MotherLowHealthFlickerComponent] 协议验证失败: MID=%s 缺少参数 '%s'. "
			 "修复: 在材质蓝图加 ScalarParameter '%s' 并连到 Emissive 或 BaseColor."),
		*Mat->GetName(),
		*FlickerMaterialParameterName.ToString(),
		*FlickerMaterialParameterName.ToString());
	return false;
}


void UMotherLowHealthFlickerComponent::SetFlickerAmountInternal(float FlickerValue)
{
	for (UMaterialInstanceDynamic* Mat : FlickerMaterials)
	{
		if (Mat)
		{
			Mat->SetScalarParameterValue(FlickerMaterialParameterName, FlickerValue);
		}
	}
}


void UMotherLowHealthFlickerComponent::ResetFlicker()
{
	SetFlickerAmountInternal(0.0f);
}


USkeletalMeshComponent* UMotherLowHealthFlickerComponent::GetOwnerSkeletalMesh() const
{
	if (ACharacter* Char = Cast<ACharacter>(GetOwner()))
	{
		return Char->GetMesh();
	}
	return nullptr;
}
