// Copyright (c) 2026.
//
// 【v132 2026.08.02 P0 新增】BTDecorator — 有清晰射击线?(Has Clear Shot)
//
// 业务背景 (用户 2026.08.02):
//   "v132 删了 Has Line Of Sight?但目标躲到障碍物后面 AI 还在开火"
//
// 真根因:
//   - v132 IAISightTargetInterface 改了 AI 视野判定 (UE 官方方案)
//   - 但 BT 在 PrimaryFire Sequence 节点没有 LoS 检查 → BB.TargetActor 还在 → 开火
//   - 旧 BTDecorator_HasLineOfSight 已删除,BT 没有 LoS 装饰器兜底
//   - 结果: 视野丢的 AI 还能继续打 LastSeenPosition
//
// v132 P0 修复 — 业界共识方案 (UE 5.3 forum 推荐):
//   - 写新 Decorator 直接调 AActor::CanBeSeenFrom
//   - 这就是 UE 5.3 forum 上推荐的方案:
//     "By overriding IAISightTargetInterface::CanBeSeenFrom you can do any number
//      of LoS checks against different locations on your character..."
//   - 我们的 IAISightTargetInterface override 在 BaseCharacter 实现
//   - Decorator 直接 Cast<ACharacter> → GetActorEyesViewPoint → CanBeSeenFrom
//
// 大厂原则:
//   - 复用 v132 已实现的接口, 不写新 trace (零重复)
//   - 一个 Decorator 一个职责 (Has Clear Shot = Visible)
//   - 显式失败 > 静默兜底: Target 无效 / Pawn 无效 → Log Warning + Fail (让 BT 上溯)
//
// 与 BTDecorator_HasLineOfSight (旧 v129) 关键区别:
//   - 旧版: 自实现多 trace (头/胸/脚) — 性能开销大 + 自维护
//   - 新版: 委托 IAISightTargetInterface.CanBeSeenFrom — UE 官方 + 0 重复 + 0 维护
//
// 使用方法 (UE 编辑器内 BP 配置):
//   1. 打开 BT_ZombieModeAI.uasset / BT_MeleeAI.uasset
//   2. 找到 PrimaryFire Sequence / Attack Sequence
//   3. 加 Decorator: Has Clear Shot?
//   4. 配置:
//      - Target Actor Key: TargetActor (BB key)
//      - check mode: SingleTarget (默认, 单点 trace, 性能佳)
//      - 命中目标可见性: Visible (默认)
//      - 距离上限: 2000 cm (SightRadius 默认 3000 cm 之外的 BB.TargetActor 不应该过)
//      - Self Pawn Key: SelfActor (BB key, 一般都有)
//   5. Sequence 流: decor 拒判 → 进 Attack 分支失败 → BT 走 Chase/Retreat 等其他分支

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTDecorator.h"
#include "BehaviorTree/BehaviorTreeTypes.h"  // 【v132.1 P0 修复】FBlackboardKeySelector 在这里 (不是 BTBlackboardKey.h)
#include "BehaviorTree/BlackboardComponent.h"  // FBlackboardKeySelector 完整定义
#include "UObject/ObjectMacros.h"
#include "BTDecorator_HasClearShot.generated.h"

UENUM()
enum class EHasClearShotPolicy : uint8
{
	// 视线可见 = True (推荐) — 默认
	RequireVisible UMETA(DisplayName = "Require Visible (推荐)"),

	// 视线不可见 = True — 反向使用,业务定制
	RequireHidden   UMETA(DisplayName = "Require Hidden"),
};

UCLASS(Blueprintable, meta = (DisplayName = "Has Clear Shot? (有清晰射击线?)"))
class METALSLUG01_API UBTDecorator_HasClearShot : public UBTDecorator
{
	GENERATED_BODY()

public:
	UBTDecorator_HasClearShot();

	/** 目标 Actor Key (BB.Object, 通常 = TargetActor) */
	UPROPERTY(EditAnywhere, Category = "Config")
	FBlackboardKeySelector TargetActorKey;

	/** Self Actor Key (BB.Object, 通常 = SelfActor;用于算 ObserverLocation) */
	UPROPERTY(EditAnywhere, Category = "Config")
	FBlackboardKeySelector SelfActorKey;

	/** 视线判定策略 — 默认 Require Visible */
	UPROPERTY(EditAnywhere, Category = "Config")
	EHasClearShotPolicy Policy = EHasClearShotPolicy::RequireVisible;

	/** 距离上限 (cm) — 超过此距离视为 NotVisible (避免 trace 大范围远处)
	 *   默认 3000cm,等于 SightRadius */
	UPROPERTY(EditAnywhere, Category = "Config", meta = (ClampMin = "100.0", ClampMax = "10000.0"))
	float MaxRange = 3000.f;

	/** 是否画 DebugTrace — 帮助调试时可视化 */
	UPROPERTY(EditAnywhere, Category = "Debug")
	bool bDrawDebugTrace = false;

	virtual FString GetStaticDescription() const override;

	// 【v132.1 修复】InitializeFromAsset 必须 override 调 ResolveSelectedKey
	//   业界共识 (UE 5.6 论坛):
	//     "The id seems to be failing... debug value 255..."
	//     "Initialize your blackboard key selector in your cpp like this:
	//      void InitializeFromAsset(UBehaviorTree& Asset) { ResolveSelectedKey(*BBAsset); }"
	//   不调 → SelectedKeyID 是 255 (debug 值) → BB 查找失败
	virtual void InitializeFromAsset(UBehaviorTree& Asset) override;

protected:
	virtual bool CalculateRawConditionValue(
		UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) const override;
};
