// Copyright (c) 2026.
// UBTTask_ActivateMotherSpeedBoost.cpp — 激活母体加速技能 (AI 路径)
//
// 【v120 2026.08.03 大厂架构重构】
//
// 重构内容:
//   - 技能参数从 AI ConfigSO 迁移到 PlayerConfigAsset
//   - AI 路径从 RoomSpawnSubsystem::PlayerConfigAsset 读取 (与玩家路径对称)
//
// 触发链 (AI 路径):
//   1. BTDecorator_CooldownReady 判断冷却已结束
//   2. BTTask_ActivateMotherSpeedBoost::ExecuteTask
//   3. 从 RoomSpawnSubsystem::PlayerConfigAsset 读母体技能参数
//   4. MotherSkillComponent::ActivateSkill(SpeedMultiplier, Duration, Cooldown, CurrentSpeed)
//   5. 状态变更 → BaseCharacter 订阅 OnSkillStateChanged → 改 MaxWalkSpeed

#include "Systems/AI/Tasks/BTTask_ActivateMotherSpeedBoost.h"
#include "AIController.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "GameFramework/Pawn.h"
#include "Characters/BaseCharacter.h"
#include "Combat/MotherSkillComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "Systems/Spawn/RoomSpawnSubsystem.h"
#include "Data/Config/PlayerConfigAsset.h"


UBTTask_ActivateMotherSpeedBoost::UBTTask_ActivateMotherSpeedBoost()
{
	NodeName = TEXT("ActivateMotherSpeedBoost");
	bNotifyTick = false; // 纯同步任务
}


FString UBTTask_ActivateMotherSpeedBoost::GetStaticDescription() const
{
	return FString::Printf(TEXT("%s: 激活母体加速技能"), *NodeName);
}


EBTNodeResult::Type UBTTask_ActivateMotherSpeedBoost::ExecuteTask(
	UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory)
{
	AAIController* AIC = OwnerComp.GetAIOwner();
	if (!AIC)
	{
		UE_LOG(LogTemp, Display,
			TEXT("[BTTask_ActivateMotherSpeedBoost] ExecuteTask FAIL: AIController 无效"));
		return EBTNodeResult::Failed;
	}

	APawn* AIPawn = AIC->GetPawn();
	if (!AIPawn)
	{
		UE_LOG(LogTemp, Display,
			TEXT("[BTTask_ActivateMotherSpeedBoost] ExecuteTask FAIL: AIPawn 无效"));
		return EBTNodeResult::Failed;
	}

	ABaseCharacter* AIChar = Cast<ABaseCharacter>(AIPawn);
	if (!AIChar)
	{
		UE_LOG(LogTemp, Display,
			TEXT("[BTTask_ActivateMotherSpeedBoost] ExecuteTask FAIL: AIChar=%s 不是 ABaseCharacter"),
			*GetNameSafe(AIPawn));
		return EBTNodeResult::Failed;
	}

	UCharacterMovementComponent* MoveComp = AIChar->GetCharacterMovement();
	if (!MoveComp)
	{
		UE_LOG(LogTemp, Display,
			TEXT("[BTTask_ActivateMotherSpeedBoost] ExecuteTask FAIL: CharacterMovement 无效. AI=%s"),
			*AIChar->GetName());
		return EBTNodeResult::Failed;
	}

	// 【v120 重构】从 PlayerConfigAsset 读母体技能参数 (与玩家路径 PlayerComboComponent::UseSkill 对称)
	float SpeedMultiplier = 2.0f;
	float Duration = 2.0f;
	float Cooldown = 10.0f;

	if (UWorld* World = AIC->GetWorld())
	{
		if (URoomSpawnSubsystem* SpawnSys = URoomSpawnSubsystem::Get(World))
		{
			if (UPlayerConfigAsset* PC = SpawnSys->GetPlayerConfigAsset())
			{
				SpeedMultiplier = PC->MotherSkillSpeedMultiplier;
				Duration = PC->MotherSkillDurationSeconds;
				Cooldown = PC->MotherSkillCooldownSeconds;
			}
			else
			{
				UE_LOG(LogTemp, Warning,
					TEXT("[BTTask_ActivateMotherSpeedBoost] PlayerConfigAsset 为空. AI=%s 使用默认值 SpeedMul=%.1f Duration=%.1f Cooldown=%.1f"),
					*AIChar->GetName(), SpeedMultiplier, Duration, Cooldown);
			}
		}
		else
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[BTTask_ActivateMotherSpeedBoost] RoomSpawnSubsystem 未找到. AI=%s 使用默认值 SpeedMul=%.1f Duration=%.1f Cooldown=%.1f"),
				*AIChar->GetName(), SpeedMultiplier, Duration, Cooldown);
		}
	}

	// 从 MovementComponent 取当前 MaxWalkSpeed (传给 Component 缓存原速度)
	const float CurrentMaxWalkSpeed = MoveComp->MaxWalkSpeed;

	// 拿 MotherSkillComponent (按需 lazy resolve)
	UMotherSkillComponent* SkillComp = AIChar->ResolveMotherSkillComponent();
	if (!SkillComp)
	{
		UE_LOG(LogTemp, Display,
			TEXT("[BTTask_ActivateMotherSpeedBoost] ExecuteTask FAIL: MotherSkillComponent 未挂载. AI=%s. "
			     "[v119 零兜底]请在 BP_GruntAI 或 BP_MuTi 的 Components 面板添加 MotherSkillComponent."),
			*AIChar->GetName());
		return EBTNodeResult::Failed;
	}

	// 调 ActivateSkill (MotherSkillComponent 内部处理所有零兜底 + 拒绝缩短逻辑)
	SkillComp->ActivateSkill(SpeedMultiplier, Duration, Cooldown, CurrentMaxWalkSpeed);

	// 写 BB.CooldownEndTime (让 BTDecorator_CooldownReady 下次能读到冷却中)
	UBlackboardComponent* BB = AIC->GetBlackboardComponent();
	if (BB)
	{
		UWorld* World = AIC->GetWorld();
		if (World)
		{
			const float Now = World->GetTimeSeconds();
			const float CooldownEndTime = Now + Cooldown;
			BB->SetValueAsFloat(FName(TEXT("MotherSkillCooldownEndTime")), CooldownEndTime);
		}
	}

	UE_LOG(LogTemp, Display,
		TEXT("[BTTask_ActivateMotherSpeedBoost] ExecuteTask SUCCESS. AI=%s SpeedMul=%.2fx Duration=%.2fs Cooldown=%.2fs MaxWalkSpeed=%.1f (真理源=PlayerConfigAsset)"),
		*AIChar->GetName(), SpeedMultiplier, Duration, Cooldown, CurrentMaxWalkSpeed);

	return EBTNodeResult::Succeeded;
}
