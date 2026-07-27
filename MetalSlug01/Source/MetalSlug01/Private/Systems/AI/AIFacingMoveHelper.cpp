// Copyright (c) 2026.
//
// 【大厂重构 v40.10 2026.07.31】UAIFacingMoveHelper 实现
//
// 详见 AIFacingMoveHelper.h 头部注释, 这里只补充实现要点.

#include "Systems/AI/AIFacingMoveHelper.h"

#include "AIController.h"
#include "GameFramework/Character.h"
#include "GameFramework/CharacterMovementComponent.h"

// 【v40.10.1 编译修复】AIFacingMoveHelper.cpp 单独编译单元, 拿不到 LogBehaviorTree 类别
//   项目中其他 BTTask (BTTask_FaceTarget.cpp 等) 能用 LogBehaviorTree, 是因为它们在
//   同一编译单元下, 之前已编 BehaviorTreeComponent.h 把 LogCategory 暴露到 PCH 缓存。
//   Helper.cpp 是新编译单元, 单独编译时这条传递链断了。
//   解决: 改用 LogTemp (UE 通用日志类别, 与业务逻辑无关, 不是"兜底行为")。
//   大厂原则: 日志类别选择 ≠ 业务逻辑兜底。LogTemp 是 UE 标准的"哪个类别都能用"回退。
//   如果未来想统一用 LogBehaviorTree, 在 Helper.cpp 加 PCH include (Build.cs PublicDependencyModuleNames) 即可。

bool UAIFacingMoveHelper::ConfigureFacingMove(
	ACharacter* Character,
	AAIController* Controller,
	AActor* TargetActor,
	FMovementOrientationSnapshot& OutSnapshot)
{
	// 重置输出快照 (大厂对称 — Restore 时 bValid=false 即跳过)
	OutSnapshot = FMovementOrientationSnapshot();

	// 【零兜底 1/3】Character 必须有效
	if (!Character)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[UAIFacingMoveHelper::ConfigureFacingMove] Character=null. "
			     "调用方必须先检查 Pawn 是否有效, 否则回头走."));
		return false;
	}

	// 【零兜底 2/3】Controller 必须有效
	if (!Controller)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[UAIFacingMoveHelper::ConfigureFacingMove] AIController=null for Character='%s'. "
			     "调用方必须从 OwnerComp.GetAIOwner() 获取."),
			*Character->GetName());
		return false;
	}

	// 【零兜底 3/3】TargetActor 必须有效
	if (!TargetActor || TargetActor->IsActorBeingDestroyed())
	{
		UE_LOG(LogTemp, Error,
			TEXT("[UAIFacingMoveHelper::ConfigureFacingMove] TargetActor=null/being-destroyed. "
			     "【修复路径】1) 在 BT 序列前置 Decorator: TargetActor Is Set "
			     "2) 检查 BB.TargetActor Key 的写入者 (BTService_UpdateZombieTargets)."));
		return false;
	}

	UCharacterMovementComponent* MoveComp = Character->GetCharacterMovement();
	if (!MoveComp)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[UAIFacingMoveHelper::ConfigureFacingMove] Character='%s' 没有 CharacterMovement 组件. "
			     "检查 Pawn 是否继承 ACharacter."),
			*Character->GetName());
		return false;
	}

	// ============================================================
	// 保存原值 (大厂对称 — Restore 时恢复)
	// ============================================================
	OutSnapshot.bValid = true;
	OutSnapshot.bOriginalOrientRotationToMovement = MoveComp->bOrientRotationToMovement;
	OutSnapshot.bOriginalUseControllerDesiredRotation = MoveComp->bUseControllerDesiredRotation;

	// ============================================================
	// 改朝向方式 — 关 OrientRotationToMovement, 开 UseControllerDesiredRotation
	// ============================================================
	// 原理:
	//   OrientRotationToMovement = false → Movement 不再每帧把 AI 朝向 = 移动方向
	//   UseControllerDesiredRotation = true → Controller 的 DesiredRotation 生效
	//                                      SetFocus 后 Controller.DesiredRotation = 朝向 Target
	MoveComp->bOrientRotationToMovement = false;
	MoveComp->bUseControllerDesiredRotation = true;

	// ============================================================
	// SetFocus — 强制 AI 朝向目标 (Gameplay 优先级最高, 压过 MoveTo 的 MoveFocus)
	// ============================================================
	Controller->SetFocus(TargetActor, EAIFocusPriority::Gameplay);

	UE_LOG(LogTemp, Verbose,
		TEXT("[UAIFacingMoveHelper::ConfigureFacingMove] Character='%s' Target='%s' OK. "
		     "Saved: OrientRotationToMovement=%d, UseControllerDesiredRotation=%d."),
		*Character->GetName(),
		*TargetActor->GetName(),
		OutSnapshot.bOriginalOrientRotationToMovement ? 1 : 0,
		OutSnapshot.bOriginalUseControllerDesiredRotation ? 1 : 0);

	return true;
}

void UAIFacingMoveHelper::RestoreFacingMove(
	ACharacter* Character,
	AAIController* Controller,
	FMovementOrientationSnapshot& Snapshot)
{
	// 【幂等 1/3】快照无效 → 静默跳过 (Configure 失败的对应行为)
	if (!Snapshot.bValid)
	{
		return;
	}

	// 【幂等 2/3】Controller 无效 → 跳过 ClearFocus, 但仍尝试恢复 Movement
	// (Character 和 Controller 是独立检查 — 允许其中一个失效)

	// ============================================================
	// 恢复 Movement 原值
	// ============================================================
	if (Character)
	{
		if (UCharacterMovementComponent* MoveComp = Character->GetCharacterMovement())
		{
			MoveComp->bOrientRotationToMovement = Snapshot.bOriginalOrientRotationToMovement;
			MoveComp->bUseControllerDesiredRotation = Snapshot.bOriginalUseControllerDesiredRotation;
		}
		else
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[UAIFacingMoveHelper::RestoreFacingMove] Character='%s' Movement 组件失效, 无法恢复原值. "
				     "可能是 Character 被销毁."),
				*Character->GetName());
		}
	}

	// ============================================================
	// ClearFocus (与 SetFocus 配对)
	// ============================================================
	if (Controller)
	{
		Controller->ClearFocus(EAIFocusPriority::Gameplay);
	}

	// 标记已恢复 (防二次 Restore)
	Snapshot.bValid = false;

	UE_LOG(LogTemp, Verbose,
		TEXT("[UAIFacingMoveHelper::RestoreFacingMove] 已恢复 (Character=%s, Controller=%s)."),
		*GetNameSafe(Character),
		*GetNameSafe(Controller));
}
