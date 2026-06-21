// ==========================================
// UHealthRegenComponent 实现
// 【2026-07-01 P0 重构】把 BaseCharacter::Tick() 里的回血回蓝逻辑独立化
// ==========================================
#include "Components/HealthRegenComponent.h"
#include "Components/HealthComponent.h"
#include "Components/EnergyComponent.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Engine/World.h"

UHealthRegenComponent::UHealthRegenComponent()
{
	// 必须 Tick - 每帧检测移动状态和驱动回复
	PrimaryComponentTick.bCanEverTick = true;
	PrimaryComponentTick.TickInterval = 0.0f; // 每帧
}


void UHealthRegenComponent::BeginPlay()
{
	Super::BeginPlay();

	// 初始化 LastMoveTime 为当前时间
	// 这样角色刚开始静止时, 需要等 RegenerationDelay 才开始回血
	if (UWorld* World = GetWorld())
	{
		LastMoveTime = World->GetTimeSeconds();
	}
}


void UHealthRegenComponent::EndPlay(const EEndPlayReason::Type EndPlayReason)
{
	Super::EndPlay(EndPlayReason);
}


void UHealthRegenComponent::TickComponent(float DeltaTime, ELevelTick TickType, FActorComponentTickFunction* ThisTickFunction)
{
	Super::TickComponent(DeltaTime, TickType, ThisTickFunction);

	// 防御: 必须有 Owner, 必须有 World, 必须有 Character
	UWorld* World = GetWorld();
	ACharacter* OwnerChar = Cast<ACharacter>(GetOwner());
	if (!World || !OwnerChar)
	{
		return;
	}

	// 防御: 死亡不回复 (HealthComponent 已短路, 这里再次防御性检查)
	if (UHealthComponent* HealthComp = OwnerChar->FindComponentByClass<UHealthComponent>())
	{
		if (HealthComp->IsDead())
		{
			bIsRegenerating = false;
			return;
		}
	}

	// 只在服务器跑回复逻辑 (客户端 HealthComponent 已 Replicated, 不需要本地回血)
	// 这样可以避免客户端预测导致的双轨不一致
	if (!OwnerChar->HasAuthority())
	{
		return;
	}

	// 未启用自动回血: 完全跳过
	if (!bEnableAutoRegen || (HealthRegenRate <= 0.0f && EnergyRegenRate <= 0.0f))
	{
		return;
	}

	// 检测移动状态
	if (IsOwnerMoving())
	{
		// 在移动: 更新 LastMoveTime, 打断回血
		LastMoveTime = World->GetTimeSeconds();
		bIsRegenerating = false;
		return;
	}

	// 静止: 检查是否过了 RegenerationDelay
	float TimeSinceLastMove = World->GetTimeSeconds() - LastMoveTime;
	if (TimeSinceLastMove < RegenerationDelay)
	{
		return; // 还在等待期
	}

	bIsRegenerating = true;

	// 执行回复
	UHealthComponent* HealthComp = OwnerChar->FindComponentByClass<UHealthComponent>();
	if (HealthComp && HealthRegenRate > 0.0f && HealthComp->GetCurrent() < HealthComp->GetMax())
	{
		HealthComp->Heal(HealthRegenRate * DeltaTime);
	}

	UEnergyComponent* EnergyComp = OwnerChar->FindComponentByClass<UEnergyComponent>();
	if (EnergyComp && EnergyRegenRate > 0.0f)
	{
		EnergyComp->Add(EnergyRegenRate * DeltaTime);
	}
}


void UHealthRegenComponent::NotifyDamageTaken()
{
	// 受伤后立即打断回血, 重新计时
	bIsRegenerating = false;
	if (UWorld* World = GetWorld())
	{
		LastMoveTime = World->GetTimeSeconds();
	}
}


void UHealthRegenComponent::ResetRegenerationState()
{
	// 重生/复活时调用: 重置所有回复状态
	bIsRegenerating = false;
	if (UWorld* World = GetWorld())
	{
		LastMoveTime = World->GetTimeSeconds();
	}
}


bool UHealthRegenComponent::IsOwnerMoving() const
{
	ACharacter* OwnerChar = Cast<ACharacter>(GetOwner());
	if (!OwnerChar)
	{
		return false;
	}

	// 通过速度向量判断移动
	// 注意: 速度是上一帧累积的, 即使物理引擎给了 0.01 的小数值也算移动
	FVector Velocity = OwnerChar->GetVelocity();
	return !Velocity.IsNearlyZero(0.1f);
}