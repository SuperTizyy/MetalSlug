// Copyright (c) 2026.
//
// 【大厂重构 v40.10 2026.07.31】AI 朝向移动通用工具
//
// 设计动机 (大厂原则 - 单一真理源 + DRY):
//   v23.2 之前, "边移动边面向目标" 的实现散落在 BTTask_MoveAwayFromTarget 内部:
//     1. 保存 bOrientRotationToMovement / bUseControllerDesiredRotation
//     2. 临时改值 + SetFocus
//     3. MoveToLocation
//     4. 完成/Abort 恢复 + ClearFocus
//   v40.10 之后, 这套机制下沉到 UAIFacingMoveHelper, 任何 BTTask 复用即可.
//
//   影响范围 (重构点):
//     - BTTask_MoveAwayFromTarget  → 删除私有 Save/Restore, 调 Helper
//     - BTTask_MoveToFacingTarget  → 新建, 直接调 Helper
//     - BTTask_CircleAroundTarget  → 未来重构点 (目前直接用 SetFocus 没动 Movement,
//                                          存在回头走风险, 建议未来 P1 改为调 Helper)
//
// 职责 (单一职责):
//   - 解决 UE CharacterMovement "OrientRotationToMovement = true 时,
//     MoveTo 会强制 AI 朝向 = 移动方向 → 看起来背对目标走" 的回头走问题.
//   - 提供原子 Configure/Restore API, 调用方负责 MoveTo/Tick 检查到达.
//
// 零兜底:
//   - Character 无效 → 返回失败快照 + Log Error, 调用方判断.
//   - 调用方必须配对调用 Configure/Restore, 否则 Movement 设置泄漏.
//   - 二次 Configure 时, Helper 自动 Restore 上一次的快照, 防状态污染.
//
// 抗抖动:
//   - Save 失败 (Character 无效) 时, 调用方不能继续 MoveTo, 否则回头走.
//   - Restore 时如发现上次 Save 失败, 直接跳过恢复, 不报错 (幂等).

#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "AIFacingMoveHelper.generated.h"

class ACharacter;
class UCharacterMovementComponent;
class AAIController;
class AActor;

/**
 * 快照数据 — 记录 Pawn Movement 原值, 用于 Restore 恢复.
 * 大厂原则: 字段对齐 CharacterMovement 的真实 bool, 不增加冗余字段.
 */
USTRUCT(BlueprintType)
struct METALSLUG01_API FMovementOrientationSnapshot
{
	GENERATED_BODY()

	/** 是否有效 — Character 无效时为 false, Restore 时跳过 */
	UPROPERTY(BlueprintReadOnly, Category = "AI|FacingMove")
	bool bValid = false;

	/** 原 OrientRotationToMovement 值 */
	UPROPERTY(BlueprintReadOnly, Category = "AI|FacingMove")
	bool bOriginalOrientRotationToMovement = false;

	/** 原 UseControllerDesiredRotation 值 */
	UPROPERTY(BlueprintReadOnly, Category = "AI|FacingMove")
	bool bOriginalUseControllerDesiredRotation = false;
};

/**
 * UAIFacingMoveHelper — AI 边移动边面向目标的通用工具.
 *
 * 解决问题:
 *   UE CharacterMovement 默认 OrientRotationToMovement = true,
 *   MoveTo 会强制 AI 朝向 = 移动方向 → 移动期间 AI 背对目标走 (回头走).
 *
 * 解决方案 (Uncharted / Last of Us 同款):
 *   移动期间: 关 OrientRotationToMovement, 开 UseControllerDesiredRotation,
 *             AIC->SetFocus(Target, Gameplay) 让 AI 朝向固定为目标.
 *   移动结束: 恢复原值, ClearFocus.
 *
 * 调用模式 (大厂对称 — 必须配对):
 *   FMovementOrientationSnapshot Snapshot;
 *   UAIFacingMoveHelper::ConfigureFacingMove(Character, Controller, TargetActor, Snapshot);
 *   // ... MoveTo ...
 *   UAIFacingMoveHelper::RestoreFacingMove(Character, Controller, Snapshot);
 *
 * 错误处理:
 *   - Configure 失败 (Character/Controller/Target 无效) → 返回 bOutSnapshot.bValid = false,
 *     调用方收到失败快照必须拒绝 MoveTo, 否则回头走.
 *   - Restore 时如 bValid = false, 静默跳过 (幂等).
 */
UCLASS()
class METALSLUG01_API UAIFacingMoveHelper : public UBlueprintFunctionLibrary
{
	GENERATED_BODY()

public:
	/**
	 * 配置 AI 移动期间朝向目标的设置.
	 *
	 * @param Character         AI 的 Pawn (必须是 ACharacter, 否则失败)
	 * @param Controller        AIController (用于 SetFocus)
	 * @param TargetActor       面向的目标 Actor (null → 失败)
	 * @param OutSnapshot       输出快照 (失败时 bValid = false)
	 * @return true = 配置成功, false = 配置失败 (调用方必须拒绝 MoveTo)
	 *
	 * 副作用 (按顺序):
	 *   1. 如果有上一次的快照且有效, 自动 Restore (防状态污染)
	 *   2. 保存 Character Movement 原值到 OutSnapshot
	 *   3. 关 OrientRotationToMovement (不让 MoveTo 抢 AI 朝向)
	 *   4. 开 UseControllerDesiredRotation (让 FocalPoint 控制朝向)
	 *   5. Controller->SetFocus(Target, Gameplay) (强制 AI 朝向目标)
	 */
	UFUNCTION(BlueprintCallable, Category = "AI|FacingMove")
	static bool ConfigureFacingMove(
		ACharacter* Character,
		AAIController* Controller,
		AActor* TargetActor,
		FMovementOrientationSnapshot& OutSnapshot);

	/**
	 * 恢复 AI Movement 原值 + 清理 Focus.
	 *
	 * @param Character    AI 的 Pawn (可以为 null, Restore 时如 Snapshot 无效也静默跳过)
	 * @param Controller   AIController (用于 ClearFocus)
	 * @param Snapshot     Configure 输出的快照
	 *
	 * 幂等: Snapshot.bValid = false 时静默跳过.
	 */
	UFUNCTION(BlueprintCallable, Category = "AI|FacingMove")
	static void RestoreFacingMove(
		ACharacter* Character,
		AAIController* Controller,
		UPARAM(ref) FMovementOrientationSnapshot& Snapshot);
};
