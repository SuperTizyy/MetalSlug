// Copyright (c) 2026.
//
// 【v119 2026.08.01】BTDecorator — 通用 Blackboard Key vs Key/常量 比较
//
// 设计动机:
//   现有装饰器家族的不足 — v22 BT 决策库只有"单 Key vs 标量"对比 (BTDecorator_HPThreshold),
//   但策划经常需要"两个 BB Key 之间比较"或"BB Key vs 常量":
//     - "弹药数 >= 5 才允许开火" (AmmoCountKey >= 5)
//     - "人类数 > 母体数 × 2" 触发撤退  (AliveHumanCountKey > AliveMotherCountKey * 2)
//     - "母体距离 < 警戒距离"          (DistanceToMotherKey < AlertDistanceKey)
//   这些场景的通用解决方案, 避免每个项目新增一个 Decorator.
//
// 职责 (单一职责):
//   - 读取 BB.KeyA (强制, Key 路径 — 与 UE 标准 BlackboardBasedCondition Decorator 一致)
//   - 读取 BB.KeyB (可选 — 可为另一个 Key 或编辑器的常量)
//   - 按 Op 执行比较 (==, !=, <, <=, >, >=)
//   - 返回 bool 给 BT 决策
//
// 大厂原则落地:
//   - 通用性: KeyA/KeyB 通过 AllowedTypes 数组接受所有 BB 类型 (Float/Int/Bool/Vector/Object/String),
//             运行时按 BB->GetKeyType 派生分支, 0 重复实现 N 个 Decorator.
//   - DRY: 不新建"单 Key 阈值"装饰器, 沿用现有 BTDecorator_HPThreshold.
//   - 零兜底: 任何 Key 未配 / 类型不匹配 / Key 无效 → Log Error + return false (与 v40.5 装饰器族标准一致).
//   - 可观测: 每个分支 Verbose 日志 (与 BTDecorator_InAttackRange / BTDecorator_CooldownReady 对称).
//
// 与现有装饰器的边界 (大厂原则 - 零重复):
//   - BTDecorator_HPThreshold:        单 Key (Float) vs 标量阈值, 范围 [0, 1].   本节点 KeyA vs KeyB, 类型不限.
//   - BTDecorator_InAttackRange:      双 Key (Float) 区间判断 (AR ± Hyst).       本节点是单点比较, 不是区间.
//   - BTDecorator_CooldownReady:      单 Key (Float) vs 时间戳阈值.                本节点是 KeyA vs KeyB, 类型不限.
//   - BTDecorator_TooClose:           单 Key (Float) vs 配置半径.                  本节点 KeyA vs KeyB, 类型不限.
//   - BTDecorator_BlackboardCompare:  ★ 新增 — KeyA vs KeyB/常量, 6 种比较符, 全 BB 类型支持.
//
// 何时用:
//   - BT 序列前置守卫, 比较两个 BB Key (例如 AliveHuman > AliveMother 才攻击).
//   - BT 序列前置守卫, BB Key vs 常量 (例如 AmmoCount >= 3 才开火).
//   - 注意: 不支持异类型比较 (Float vs Bool 没意义). 异类型 → Log Error + return false.
//
// 何时不用:
//   - "血量 < 阈值" → 用 BTDecorator_HPThreshold (语义清晰, 限血量, ClampMin/Max 友好).
//   - "距离在 AR ± Hyst 区间内" → 用 BTDecorator_InAttackRange.
//   - "冷却时间到" → 用 BTDecorator_CooldownReady.

#pragma once

#include "CoreMinimal.h"
#include "BehaviorTree/BTDecorator.h"
#include "BehaviorTree/BehaviorTreeTypes.h"
#include "BTDecorator_BlackboardCompare.generated.h"

class UBlackboardComponent;

/**
 * 通用 BB Key vs Key/常量 比较符
 * DisplayName 用中文, 与现有 EBTHPCheckMode 风格一致 (策划在 BT 编辑器看)
 */
UENUM(BlueprintType)
enum class EBBCompareOp : uint8
{
	Equal          UMETA(DisplayName = "等于 (==)"),
	NotEqual       UMETA(DisplayName = "不等于 (!=)"),
	Less           UMETA(DisplayName = "小于 (<)"),
	LessOrEqual    UMETA(DisplayName = "小于等于 (<=)"),
	Greater        UMETA(DisplayName = "大于 (>)"),
	GreaterOrEqual UMETA(DisplayName = "大于等于 (>=)"),
};

/**
 * Key B 的"右侧值"如何取 — 设计师在编辑器选哪种
 *   - Key:               用 KeyB SelectedKeyName 读 BB 值 (Key vs Key 模式)
 *   - FloatValue:        用编辑器配的 BFloatValue
 *   - IntValue:          用编辑器配的 BIntValue
 *   - BoolValue:         用编辑器配的 BBoolValue
 *   - VectorValue:       用编辑器配的 BVectorValue
 *   - StringValue:       用编辑器配的 BStringValue
 *   - ObjectValue:       用编辑器配的 BObjectValue (策划塞个 Actor 引用, 例如玩家 Pawn Class default object)
 *
 * 大厂原则 - 显式优于默认:
 *   - 不允许 KeyB 模式 = "未配时 fallback 到常量 0" 这种隐式行为 (兜底反模式).
 *   - 必须在编辑器显式选 RightSource, 编译期即知运行时从哪读.
 */
UENUM(BlueprintType)
enum class EBBCompareRightSource : uint8
{
	Key         UMETA(DisplayName = "Blackboard Key (Key vs Key)"),
	FloatValue  UMETA(DisplayName = "常量 Float"),
	IntValue    UMETA(DisplayName = "常量 Int"),
	BoolValue   UMETA(DisplayName = "常量 Bool"),
	VectorValue UMETA(DisplayName = "常量 Vector"),
	StringValue UMETA(DisplayName = "常量 String"),
	ObjectValue UMETA(DisplayName = "常量 Object"),
};

/**
 * UBTDecorator_BlackboardCompare — 通用 BB Key vs Key/常量 比较
 *
 * 编辑器配置 (策划在 BT 节点 Details 面板里):
 *   - KeyA:         BB.KeyA (强制 — 必须配, 否则编辑器红框警告)
 *   - Op:           6 种比较符 (默认 Equal)
 *   - RightSource:  Key vs Key vs 各种常量 (默认 Key, 习惯兼容)
 *   - KeyB:         BB.KeyB (仅 RightSource=Key 时生效, 必须配)
 *   - 常量 5 个:     Float / Int / Bool / Vector / String / Object (仅对应 RightSource 时生效)
 *
 * 编译期检查:
 *   - 构造函数注册 KeyA/KeyB 的 AllowedTypes (全 BB 类型支持)
 *   - UE 编辑器自动按策划选的类型过滤
 *
 * 运行时分支:
 *   - 按 BB->GetKeyType(KeyID) 决定两个 Key 的实际类型, 走对应 Compare 函数
 *   - KeyA 与 KeyB/常量类型不一致 → Log Error + return false (零兜底)
 */
UCLASS(Blueprintable, meta = (DisplayName = "Blackboard Compare (通用 BB Key 比较)"))
class METALSLUG01_API UBTDecorator_BlackboardCompare : public UBTDecorator
{
	GENERATED_BODY()

public:
	UBTDecorator_BlackboardCompare();

	virtual FString GetStaticDescription() const override;

	/** 【KeyA】左侧 BB Key — 强制, 必须配 */
	UPROPERTY(EditAnywhere, Category = "Blackboard")
	FBlackboardKeySelector KeyA;

	/** 比较符 (默认 Equal, 6 种) */
	UPROPERTY(EditAnywhere, Category = "Compare")
	EBBCompareOp Op = EBBCompareOp::Equal;

	/** KeyB 值的来源 (默认 Key, 兼容"key vs key"用法) */
	UPROPERTY(EditAnywhere, Category = "Compare")
	EBBCompareRightSource RightSource = EBBCompareRightSource::Key;

	/** 【KeyB】右侧 BB Key — 仅 RightSource=Key 时生效 */
	UPROPERTY(EditAnywhere, Category = "Blackboard",
		meta = (EditCondition = "RightSource == EBBCompareRightSource::Key"))
	FBlackboardKeySelector KeyB;

	/** Float 常量 — 仅 RightSource=FloatValue 时生效 */
	UPROPERTY(EditAnywhere, Category = "Compare",
		meta = (EditCondition = "RightSource == EBBCompareRightSource::FloatValue"))
	float BFloatValue = 0.f;

	/** Int 常量 — 仅 RightSource=IntValue 时生效 */
	UPROPERTY(EditAnywhere, Category = "Compare",
		meta = (EditCondition = "RightSource == EBBCompareRightSource::IntValue"))
	int32 BIntValue = 0;

	/** Bool 常量 — 仅 RightSource=BoolValue 时生效 */
	UPROPERTY(EditAnywhere, Category = "Compare",
		meta = (EditCondition = "RightSource == EBBCompareRightSource::BoolValue"))
	bool BBoolValue = false;

	/** Vector 常量 — 仅 RightSource=VectorValue 时生效 */
	UPROPERTY(EditAnywhere, Category = "Compare",
		meta = (EditCondition = "RightSource == EBBCompareRightSource::VectorValue"))
	FVector BVectorValue = FVector::ZeroVector;

	/** String 常量 — 仅 RightSource=StringValue 时生效 */
	UPROPERTY(EditAnywhere, Category = "Compare",
		meta = (EditCondition = "RightSource == EBBCompareRightSource::StringValue"))
	FString BStringValue;

	/** Object 常量 — 仅 RightSource=ObjectValue 时生效 (策划可塞 Actor 引用等) */
	UPROPERTY(EditAnywhere, Category = "Compare",
		meta = (EditCondition = "RightSource == EBBCompareRightSource::ObjectValue"))
	TObjectPtr<UObject> BObjectValue = nullptr;

protected:
	virtual bool CalculateRawConditionValue(
		UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) const override;

private:
	/**
	 * 关键防御 — KeyA 与 KeyB/常量 类型不一致时 Log Error + return false
	 * 大厂原则 - 零兜底: 不允许"类型不一致时强转"或"按 string 转换"
	 *
	 * @return true = 类型匹配, false = 类型不匹配 (内部已 Log Error)
	 */
	bool ValidateTypeMatch(const UBlackboardComponent& BB,
		const TSubclassOf<class UBlackboardKeyType> KeyAType,
		AAIController* AIC) const;

	/**
	 * Float 比较 (统一调度, 6 种 Op 全部覆盖)
	 */
	bool CompareFloat(float A, float B) const;

	/** Int 比较 */
	bool CompareInt(int32 A, int32 B) const;

	/** Bool 比较 (==/!= 合法; < / <= / > / >= 无意义 → false) */
	bool CompareBool(bool A, bool B) const;

	/** Vector 比较 (分量近似相等 — 大厂标准 FMath::IsNearlyEqual, 沿用项目 Tolerance=UE_KINDA_SMALL_NUMBER 默认) */
	bool CompareVector(const FVector& A, const FVector& B) const;

	/** String 比较 (==/!= 合法; < / <= / > / >= 按字典序 — UE 5.6 FString::Compare 默认 lexicographic) */
	bool CompareString(const FString& A, const FString& B) const;

	/** Object 比较 (按指针 ==, 仅 ==/!= 合法) */
	bool CompareObject(UObject* A, UObject* B) const;
};