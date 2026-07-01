// Copyright (c) 2026.

#include "Systems/AI/AIRuntimeConfigComponent.h"
#include "Data/AI/AIBehaviorConfigSO.h"

UAIRuntimeConfigComponent::UAIRuntimeConfigComponent()
{
    // 不需要 Tick - Config 是被动读取
    PrimaryComponentTick.bCanEverTick = false;
}

void UAIRuntimeConfigComponent::ApplyConfig(UAIBehaviorConfigSO* InConfig)
{
    Config = InConfig;
}

void UAIRuntimeConfigComponent::SetDifficultyTier(EAIDifficultyTier NewTier)
{
    DifficultyTier = NewTier;
}

FAIPerceptionParams UAIRuntimeConfigComponent::GetScaledPerception() const
{
    return Config ? Config->GetScaledPerception(DifficultyTier) : FAIPerceptionParams();
}

FAICombatParams UAIRuntimeConfigComponent::GetScaledCombat() const
{
    return Config ? Config->GetScaledCombat(DifficultyTier) : FAICombatParams();
}

FAIMovementParams UAIRuntimeConfigComponent::GetScaledMovement() const
{
    return Config ? Config->GetScaledMovement(DifficultyTier) : FAIMovementParams();
}
