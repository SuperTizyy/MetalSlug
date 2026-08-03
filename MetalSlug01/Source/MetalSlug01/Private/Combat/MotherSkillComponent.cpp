// Copyright (c) 2026
// UMotherSkillComponent.cpp — 母体加速技能 (镜像 MotherSlowComponent 模式)

#include "Combat/MotherSkillComponent.h"

#include "Engine/World.h"
#include "Components/SkeletalMeshComponent.h"
#include "Kismet/GameplayStatics.h"
#include "Particles/ParticleSystem.h"
#include "Particles/ParticleSystemComponent.h"
#include "Components/AudioComponent.h"
#include "Net/UnrealNetwork.h"
#include "TimerManager.h"

#include "Characters/BaseCharacter.h"
#include "Components/Image.h"
#include "UI/MyGameHUD.h"
#include "UI/Game/GameHUDWidget.h"
#include "UI/Game/Widgets/PlayerStatusWidget.h"


// ==========================================
// 构造 + 生命周期
// ==========================================

UMotherSkillComponent::UMotherSkillComponent()
{
	// 【大厂原则 — 组件同步必备】
	// 1. SetIsReplicatedByDefault(true) — 组件字段能复制到客户端
	// 2. GetLifetimeReplicatedProps 中 DOREPLIFETIME — 具体字段标 Replicated
	// 3. 字段 UPROPERTY 标 Replicated — 真正写入复制列表
	// 三层缺一不可 (v40.0 P0 修复教训)
	SetIsReplicatedByDefault(true);
}

void UMotherSkillComponent::BeginPlay()
{
	Super::BeginPlay();
}

void UMotherSkillComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	// 防御型设计: 销毁时清 Timer, 防止回调残留
	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(SkillTimerHandle);
		World->GetTimerManager().ClearTimer(SkillParticleTimerHandle);
	}

	// 停止技能粒子（防御型清理）
	StopSkillParticle();

	Super::EndPlay(EndPlayReason);
}

void UMotherSkillComponent::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	// 【v119 大厂架构】镜像 MotherSlowComponent:
	//   - bIsSkillActive: ReplicatedUsing (OnRep_SkillActiveChanged 触发客户端广播)
	//   - SkillActiveExpiresAtWorldTime: 普通 Replicated (辅助字段, 客户端读剩余时间)
	//   - SkillCooldownEndTime: 普通 Replicated (冷却时间, UI/BT 都读)
	DOREPLIFETIME(UMotherSkillComponent, bIsSkillActive);
	DOREPLIFETIME(UMotherSkillComponent, SkillActiveExpiresAtWorldTime);
	DOREPLIFETIME(UMotherSkillComponent, SkillCooldownEndTime);
	DOREPLIFETIME(UMotherSkillComponent, TotalCooldownDuration);
}


// ==========================================
// 公开 API (真理源写入入口)
// ==========================================

void UMotherSkillComponent::ActivateSkill(float SpeedMultiplier, float Duration, float Cooldown, float CurrentMaxWalkSpeed)
{
	AActor* OwnerActor = GetOwner();

	// 【v121.3 零兜底】拒绝在 CDO (Class Default Object) 上调用
	// 根因: BP 在 HotReload 后错误地在 CDO 上调用此方法
	if (!OwnerActor || OwnerActor->HasAnyFlags(RF_ClassDefaultObject))
	{
		UE_LOG(LogTemp, Error,
			TEXT("[MotherSkillComponent] ActivateSkill: Owner 为空或是 CDO, 拒绝调用. Owner=%s"),
			OwnerActor ? *OwnerActor->GetName() : TEXT("NULL"));
		return;
	}

	// 【v120 零兜底】SpeedMultiplier ≤ 0 → 拒绝激活
	if (SpeedMultiplier <= 0.0f)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[MotherSkillComponent] ActivateSkill: SpeedMultiplier=%.2f (≤0, 配错). 拒绝激活技能. "
			     "【v120 零兜底】请检查 DA_PlayerConfig → Config|Combat|Mother → MotherSkillSpeedMultiplier (>0)."),
			SpeedMultiplier);
		return;
	}


	// 【v120 零兜底】Duration ≤ 0 → 拒绝激活
	if (Duration <= 0.0f)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[MotherSkillComponent] ActivateSkill: Duration=%.2f (≤0, 配错). 拒绝激活技能. "
			     "【v120 零兜底】请检查 DA_PlayerConfig → Config|Combat|Mother → MotherSkillDurationSeconds (>0)."),
			Duration);
		return;
	}

	// 【v120 零兜底】Cooldown ≤ 0 → 拒绝激活
	if (Cooldown <= 0.0f)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[MotherSkillComponent] ActivateSkill: Cooldown=%.2f (≤0, 配错). 拒绝激活技能. "
			     "【v120 零兜底】请检查 DA_PlayerConfig → Config|Combat|Mother → MotherSkillCooldownSeconds (>0)."),
			Cooldown);
		return;
	}

	// 【v119 零兜底】缓存速度 ≤ 0 → 拒绝激活
	if (CurrentMaxWalkSpeed <= 0.0f)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[MotherSkillComponent] ActivateSkill: CurrentMaxWalkSpeed=%.2f (≤0, 配错). 拒绝激活. "
			     "【v119 零兜底】调用方应在调本函数前从 CharacterMovement 取 CurrentMaxWalkSpeed."),
			CurrentMaxWalkSpeed);
		return;
	}

	// 只对服务器生效 — 客户端不能激活, 只能收 OnRep
	if (!OwnerActor || !OwnerActor->HasAuthority())
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[MotherSkillComponent] ActivateSkill: World 无效. Owner=%s"),
			OwnerActor ? *OwnerActor->GetName() : TEXT("NULL"));
		return;
	}

	const float Now = World->GetTimeSeconds();

	// 【v121.4 诊断】冷却检查 — 详细日志定位为何不能重激活
	if (Now < SkillCooldownEndTime)
	{
		// 【v121.4】改为 Display 级别，方便诊断
		UE_LOG(LogTemp, Display,
			TEXT("[MotherSkillComponent] ActivateSkill: ★冷却中拒绝激活. Owner=%s Now=%.2f CooldownEndTime=%.2f (剩余%.2fs)"),
			*OwnerActor->GetName(), Now, SkillCooldownEndTime, SkillCooldownEndTime - Now);
		return;
	}

	// 【v121.4 诊断】成功通过冷却检查
	UE_LOG(LogTemp, Display,
		TEXT("[MotherSkillComponent] ActivateSkill: 冷却检查通过. Owner=%s Now=%.2f CooldownEndTime=%.2f"),
		*OwnerActor->GetName(), Now, SkillCooldownEndTime);

	// ==========================================
	// 【大厂原则 — 拒绝缩短】仅当新到期时间更晚才更新
	// ==========================================
	const float NewActiveExpireTime = Now + Duration;

	if (bIsSkillActive && NewActiveExpireTime <= SkillActiveExpiresAtWorldTime + KINDA_SMALL_NUMBER)
	{
		// 【v121.4 诊断】已经在加速中, 且新到期时间不更晚 → 拒绝缩短, 保留原状态
		UE_LOG(LogTemp, Display,
			TEXT("[MotherSkillComponent] ActivateSkill: ★已在加速中且新到期时间 (%.2f) 不晚于原到期 (%.2f), 拒绝缩短. Owner=%s"),
			NewActiveExpireTime, SkillActiveExpiresAtWorldTime, *OwnerActor->GetName());
		return;
	}

	// ==========================================
	// 【v119 大厂架构 — 缓存原速度】激活前缓存调用方传入的 MaxWalkSpeed
	// ==========================================
	// 第一次激活时才缓存 (避免重复激活时缓存了"已经加速的值")
	if (CachedBaseMaxWalkSpeed <= 0.0f)
	{
		CachedBaseMaxWalkSpeed = CurrentMaxWalkSpeed;
		CachedSpeedMultiplier = SpeedMultiplier;
	}

	// ==========================================
	// 激活技能状态
	// ==========================================
	// 1. 写字段 (Replicated → 客户端 OnRep 自动同步)
	SkillActiveExpiresAtWorldTime = NewActiveExpireTime;
	// 【v121.5 修复】冷却时间改为技能生效结束后才开始计时
	// 旧: 激活时立即开始冷却 → 新: 持续时间到期时才设置 SkillCooldownEndTime (见 ExpireSkillActive_Internal)
	TotalCooldownDuration = Cooldown; // 保存总冷却时间用于 UI 倒计时

	// 2. 幂等: 清旧 Timer
	World->GetTimerManager().ClearTimer(SkillTimerHandle);

	// 3. 设新 Timer — 到期自动 ExpireSkillActive_Internal
	const float RealDuration = FMath::Max(Duration, 0.01f);
	World->GetTimerManager().SetTimer(
		SkillTimerHandle,
		this,
		&UMotherSkillComponent::ExpireSkillActive_Internal,
		RealDuration,
		false /* 不循环 */
	);

	// 【v121.5 修复】保存当前倍率到缓存 — 用于冷却条走完时可以再次激活时恢复速度
	// 注意：这是上一次的倍率，不是本次的
	CachedSpeedMultiplier = SpeedMultiplier;

	// 4. 写 bIsSkillActive = true (触发 OnRep + 服务器主动 Broadcast)
	const bool bWasActive = bIsSkillActive;
	bIsSkillActive = true;

	// 【v121.5 修复】服务器端也设置材质 bIsActive (ListenServer 上 OnRep 不会触发)
	SetMaterialSkillActive(true);

	// 【v121.6 新增】播放技能粒子特效
	PlaySkillParticle();

	// 【v121.7 新增】播放技能激活声音
	UE_LOG(LogTemp, Display,
		TEXT("[MotherSkillComponent] ActivateSkill: 准备播放技能声音. Owner=%s, SkillActivateSound=%s"),
		*OwnerActor->GetName(),
		SkillActivateSound ? *SkillActivateSound->GetName() : TEXT("NULL"));
	PlaySkillSound();

	// 5. 服务器主动 Broadcast (客户端 OnRep 也会 Broadcast — 双发保证)
	// 【v121.4 新增】广播 bIsSkillActive 状态，UI/材质可订阅此事件设置 bIsActive
	OnSkillActiveChanged.Broadcast(bIsSkillActive);

	UE_LOG(LogTemp, Display,
		TEXT("[MotherSkillComponent] ActivateSkill: 已激活加速. Owner=%s SpeedMul=%.2fx Duration=%.2fs Cooldown=%.2fs (Now=%.2f, ExpireActive=%.2f, Cooldown在Duration结束后才计时)"),
		*OwnerActor->GetName(), SpeedMultiplier, Duration, Cooldown, Now, NewActiveExpireTime);
}


void UMotherSkillComponent::DeactivateSkill()
{
	AActor* OwnerActor = GetOwner();

	// 只对服务器生效
	if (!OwnerActor || !OwnerActor->HasAuthority())
	{
		return;
	}

	if (UWorld* World = GetWorld())
	{
		World->GetTimerManager().ClearTimer(SkillTimerHandle);
	}

	// 幂等: 未激活时调用是 no-op
	if (!bIsSkillActive)
	{
		return;
	}

	// 清字段
	bIsSkillActive = false;
	SkillActiveExpiresAtWorldTime = 0.0f;
	// 【v121.3 修复】不清 CachedBaseMaxWalkSpeed — 用于恢复原始速度
	// CachedSpeedMultiplier 也保留 (可能被其他系统查询)
	CachedSpeedMultiplier = 1.0f;

	// 服务器主动 Broadcast (客户端 OnRep 也会 — 双发保证)
	// 【v121.4 新增】广播 bIsSkillActive 状态，UI/材质可订阅此事件设置 bIsActive
	OnSkillActiveChanged.Broadcast(bIsSkillActive);

	UE_LOG(LogTemp, Display,
		TEXT("[MotherSkillComponent] DeactivateSkill: 已停用加速. Owner=%s"),
		*OwnerActor->GetName());
}


// ==========================================
// 蓝图可读状态
// ==========================================

bool UMotherSkillComponent::IsCooldownReady() const
{
	if (UWorld* World = GetWorld())
	{
		return World->GetTimeSeconds() >= SkillCooldownEndTime;
	}
	return true; // World 无效时默认放行
}

float UMotherSkillComponent::GetSkillActiveRemainingSeconds() const
{
	if (!bIsSkillActive)
	{
		return 0.0f;
	}

	if (UWorld* World = GetWorld())
	{
		const float Remaining = SkillActiveExpiresAtWorldTime - World->GetTimeSeconds();
		return FMath::Max(Remaining, 0.0f);
	}
	return 0.0f;
}

float UMotherSkillComponent::GetSkillCooldownRemainingSeconds() const
{
	if (UWorld* World = GetWorld())
	{
		const float Remaining = SkillCooldownEndTime - World->GetTimeSeconds();
		return FMath::Max(Remaining, 0.0f);
	}
	return 0.0f;
}


// ==========================================
// 内部辅助
// ==========================================

void UMotherSkillComponent::ExpireSkillActive_Internal()
{
	// 【v121.5 修复】技能持续时间到期，停止加速 + 开始冷却计时
	// 注意: 冷却时间现在是在生效结束后才开始计时 (不是激活时立即开始)
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor || !OwnerActor->HasAuthority())
	{
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	const float Now = World->GetTimeSeconds();

	bIsSkillActive = false;
	SkillActiveExpiresAtWorldTime = 0.0f;
	// 【v121.5 修复】现在开始冷却计时 (技能生效结束后才计时)
	// CooldownEndTime = Now + TotalCooldownDuration (TotalCooldownDuration 在 ActivateSkill 时已设置)
	SkillCooldownEndTime = Now + TotalCooldownDuration;
	// 【v121.5 修复】不清 CachedSpeedMultiplier — 用于冷却条走完时可以再次激活
	// 注意：这里不清 CachedBaseMaxWalkSpeed — 用于恢复原始速度

	// 【v121.5 修复】服务器端也设置材质 bIsActive (ListenServer 上 OnRep 不会触发)
	SetMaterialSkillActive(false);

	// 【v121.6 新增】停止技能粒子特效
	StopSkillParticle();

	// 服务器主动 Broadcast (客户端 OnRep 也会 — 双发保证)
	// 【v121.4 新增】广播 bIsSkillActive 状态，UI/材质可订阅此事件设置 bIsActive
	OnSkillActiveChanged.Broadcast(false);

	UE_LOG(LogTemp, Display,
		TEXT("[MotherSkillComponent] ExpireSkillActive_Internal: 技能持续时间到期, 开始冷却计时. Owner=%s Now=%.2f CooldownEndTime=%.2f (Duration=%.2fs)"),
		*OwnerActor->GetName(), Now, SkillCooldownEndTime, TotalCooldownDuration);
}

void UMotherSkillComponent::OnRep_SkillActiveChanged()
{
	// 【v121.5 修复】添加 Display 日志确认 OnRep 被触发
	UE_LOG(LogTemp, Display,
		TEXT("[MotherSkillComponent] OnRep_SkillActiveChanged: bIsSkillActive=%d Owner=%s"),
		bIsSkillActive ? 1 : 0,
		GetOwner() ? *GetOwner()->GetName() : TEXT("NULL"));

	// 客户端收到复制值变化 → Broadcast 给所有订阅方
	// 【v121.5 新增】直接设置材质 bIsActive 参数
	// 触发场景:
	//   - bIsSkillActive=true: 技能激活 → 材质 bIsActive=1
	//   - bIsSkillActive=false: 技能结束 → 材质 bIsActive=0
	SetMaterialSkillActive(bIsSkillActive);

	// 【v121.4 新增】广播 bIsSkillActive 状态，UI/材质可订阅此事件设置 bIsActive
	OnSkillActiveChanged.Broadcast(bIsSkillActive);
}

void UMotherSkillComponent::SetMaterialSkillActive(bool bInIsActive)
{
	// 【v121.5 新增】C++ 直接设置材质 bIsActive 参数
	// 触发场景:
	//   - bInIsActive=true: 技能激活 → 材质 bIsActive=1
	//   - bInIsActive=false: 技能结束 → 材质 bIsActive=0

	// 1. 获取 Owner Character
	ABaseCharacter* OwnerChar = Cast<ABaseCharacter>(GetOwner());
	if (!OwnerChar)
	{
		return;
	}

	// 2. 通过 PlayerController 获取 HUD → GameHUDWidget → PlayerStatusWidget → Image_MotherSkillIcon
	APlayerController* PC = Cast<APlayerController>(OwnerChar->GetController());
	if (!PC)
	{
		// 非玩家角色 (AI) 没有 HUD, 跳过
		return;
	}

	AMyGameHUD* HUD = Cast<AMyGameHUD>(PC->GetHUD());
	if (!HUD)
	{
		return;
	}

	UGameHUDWidget* HUDWidget = HUD->GetGameHUDWidget();
	if (!HUDWidget)
	{
		return;
	}

	UPlayerStatusWidget* StatusWidget = HUDWidget->GetWidget_PlayerStatus();
	if (!StatusWidget)
	{
		return;
	}

	// 3. 获取 Image_MotherSkillIcon
	UImage* SkillIcon = StatusWidget->GetMotherSkillIcon();
	if (!SkillIcon)
	{
		return;
	}

	// 4. 获取或创建动态材质
	FSlateBrush Brush = SkillIcon->GetBrush();
	if (!Brush.HasUObject())
	{
		return;
	}

	UMaterialInterface* Material = Brush.GetResourceObject() ? Cast<UMaterialInterface>(Brush.GetResourceObject()) : nullptr;
	if (!Material)
	{
		return;
	}

	UMaterialInstanceDynamic* DynMat = Cast<UMaterialInstanceDynamic>(Material);
	if (!DynMat)
	{
		// 材质还不是动态材质，创建并应用
		DynMat = UMaterialInstanceDynamic::Create(Material, this);
		if (DynMat)
		{
			FSlateBrush NewBrush = Brush;
			NewBrush.SetResourceObject(DynMat);
			SkillIcon->SetBrush(NewBrush);
		}
	}

	if (!DynMat)
	{
		return;
	}

	// 5. 设置 bIsActive 参数
	const float ActiveValue = bInIsActive ? 1.0f : 0.0f;
	DynMat->SetScalarParameterValue(FName(TEXT("bIsActive")), ActiveValue);

	UE_LOG(LogTemp, Verbose,
		TEXT("[MotherSkillComponent] SetMaterialSkillActive: bIsActive=%.0f Owner=%s"),
		ActiveValue,
		*OwnerChar->GetName());
}


// ==========================================
// 【v121.6 新增】技能粒子特效
// ==========================================

void UMotherSkillComponent::PlaySkillParticle()
{
	AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		return;
	}

	// 幂等: 已有活动粒子，拒绝再次播放
	if (bIsSkillParticleActive)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[MotherSkillComponent] PlaySkillParticle: 已有活动粒子，拒绝重复播放. Owner=%s"),
			*OwnerActor->GetName());
		return;
	}

	// 检查粒子资产是否配置
	if (!SkillParticleSystem)
	{
		// 粒子资产未配置时静默跳过（BP 可能没配）
		return;
	}

	UWorld* World = GetWorld();
	if (!World)
	{
		return;
	}

	// 获取母体 Mesh
	USkeletalMeshComponent* MeshComp = OwnerActor->FindComponentByClass<USkeletalMeshComponent>();
	if (!MeshComp)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[MotherSkillComponent] PlaySkillParticle: 找不到 SkeletalMeshComponent. Owner=%s"),
			*OwnerActor->GetName());
		return;
	}

	// 使用 SpawnEmitterAttached 创建并附着粒子（参考 MotherSpawnParticleComponent）
	// bAutoDestroy=false: 生命周期由 Timer 控制
	// bAutoActivate=true: 立即播放
	UParticleSystemComponent* SpawnedPSC = UGameplayStatics::SpawnEmitterAttached(
		SkillParticleSystem,
		MeshComp,
		SkillParticleAttachSocket,
		SkillParticleRelativeLocation,           // 相对位置偏移
		SkillParticleRelativeRotation,           // 相对旋转偏移
		EAttachLocation::SnapToTarget,           // 锁定到目标原点
		true                                     // bAutoDestroy: false（由 Timer 控制销毁）
	);

	if (!SpawnedPSC)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[MotherSkillComponent] PlaySkillParticle: SpawnEmitterAttached 返回 null. "
				 "Owner=%s, Particle=%s, Socket=%s."),
			*OwnerActor->GetName(),
			*GetNameSafe(SkillParticleSystem),
			*SkillParticleAttachSocket.ToString());
		return;
	}

	// 设置相对缩放（SpawnEmitterAttached 不支持直接设缩放，需要生成后设置）
	SpawnedPSC->SetRelativeScale3D(SkillParticleRelativeScale);

	// 保存引用
	ActiveSkillParticleComponent = SpawnedPSC;
	bIsSkillParticleActive = true;

	UE_LOG(LogTemp, Display,
		TEXT("[MotherSkillComponent] PlaySkillParticle: 已播放技能粒子. Owner=%s Socket=%s"),
		*OwnerActor->GetName(),
		*SkillParticleAttachSocket.ToString());
}

void UMotherSkillComponent::StopSkillParticle()
{
	// 幂等: 无活动粒子时 no-op
	if (!bIsSkillParticleActive || !ActiveSkillParticleComponent)
	{
		return;
	}

	AActor* OwnerActor = GetOwner();

	// 停止播放
	ActiveSkillParticleComponent->Deactivate();

	// 销毁组件
	ActiveSkillParticleComponent->DestroyComponent();

	// 清空引用
	ActiveSkillParticleComponent = nullptr;
	bIsSkillParticleActive = false;

	UE_LOG(LogTemp, Display,
		TEXT("[MotherSkillComponent] StopSkillParticle: 已停止技能粒子. Owner=%s"),
		OwnerActor ? *OwnerActor->GetName() : TEXT("NULL"));
}

void UMotherSkillComponent::HandleSkillParticleLifetimeExpired()
{
	// 由技能持续时间 Timer 触发，与 ExpireSkillActive_Internal 共用同一个 Duration Timer
	// 这里不需要额外处理，因为技能结束时 ExpireSkillActive_Internal 会调用 StopSkillParticle
}

void UMotherSkillComponent::PlaySkillSound()
{
	// ===== 校验层: Sound =====
	if (!SkillActivateSound)
	{
		// BP 可能没配置声音，业务禁用声音不算错
		return;
	}

	AActor* OwnerActor = GetOwner();
	if (!OwnerActor)
	{
		return;
	}

	USkeletalMeshComponent* BodyMesh = OwnerActor->FindComponentByClass<USkeletalMeshComponent>();
	if (!BodyMesh)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[MotherSkillComponent][v121.7] PlaySkillSound: GetMesh() 为空! "
				 "Owner=%s. 【零兜底】检查 BP 是否有 SkeletalMeshComponent."),
			*OwnerActor->GetName());
		return;
	}

	// 已存在时先 Destroy 再 Spawn (避免残留)
	if (ActiveSkillAudioComponent)
	{
		ActiveSkillAudioComponent->Stop();
		ActiveSkillAudioComponent->DestroyComponent();
		ActiveSkillAudioComponent = nullptr;
	}

	// 附身到 Mesh, 自动播放, 播放完毕自动销毁
	ActiveSkillAudioComponent = UGameplayStatics::SpawnSoundAttached(
		SkillActivateSound,
		BodyMesh,
		NAME_None,                          // 附着到根 Socket
		FVector::ZeroVector,                // 相对位置偏移
		FRotator::ZeroRotator,             // 相对旋转偏移
		EAttachLocation::KeepRelativeOffset, // 保持相对偏移
		true,                               // bStopWhenGameends: 游戏结束时停止
		1.0f,                               // VolumeMultiplier
		1.0f,                               // PitchMultiplier
		0.0f,                               // StartTime
		nullptr,                             // AttenuationSettings
		nullptr,                             // ConcurrencySettings
		true                                // bAutoDestroy: 播放完毕自动销毁
	);

	if (!ActiveSkillAudioComponent)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[MotherSkillComponent][v121.7] PlaySkillSound: SpawnSoundAttached 失败. "
				 "Owner=%s, Sound=%s. 【零兜底】检查 Sound 资产是否损坏."),
			*OwnerActor->GetName(),
			*SkillActivateSound->GetName());
		return;
	}

	UE_LOG(LogTemp, Display,
		TEXT("[MotherSkillComponent][v121.7] PlaySkillSound: 已播放技能声音. Owner=%s, Sound=%s"),
		*OwnerActor->GetName(),
		*SkillActivateSound->GetName());
}
