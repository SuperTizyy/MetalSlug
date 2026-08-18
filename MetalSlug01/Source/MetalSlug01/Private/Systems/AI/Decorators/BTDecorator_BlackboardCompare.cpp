// Copyright (c) 2026.
//
// 【v119 2026.08.01】BTDecorator — 通用 BB Key 比较实现
//
// 详见头文件注释. 实现要点:
//   1. 构造函数注册 KeyA / KeyB 的 AllowedTypes (全 BB 类型, 运行时按 KeyType 分支)
//   2. CalculateRawConditionValue 是单点决策 (无 Tick / 无 BecameRelevant 通知)
//   3. 6 个 CompareXXX 函数对应 6 种 BB 类型, 每个覆盖 6 种 Op
//   4. 类型不匹配 / Key 未配 → Log Error + return false (零兜底)
//
// 调试日志级别:
//   - Verbose (默认隐藏, 蓝图编辑器 PIE 高频 tick 时刷屏): 单次比较结果
//   - Error (默认显示): 配置错 / 类型不匹配 (一次性)

#include "Systems/AI/Decorators/BTDecorator_BlackboardCompare.h"

#include "AIController.h"
#include "BehaviorTree/BehaviorTreeComponent.h"
#include "BehaviorTree/BlackboardComponent.h"
#include "BehaviorTree/BlackboardData.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Float.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Int.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Bool.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Vector.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_String.h"
#include "BehaviorTree/Blackboard/BlackboardKeyType_Object.h"
#include "GameFramework/Actor.h"

// ============================================================
// 编译期必须 include (UE 5.6 consteval 重载解析)
// GetKeyType / GetValueAsFloat 等模板在跨 .cpp 时丢失类型信息,
// 必须显式 include 所有 6 个 BlackboardKeyType 才能在 CalculateRawConditionValue 中调用
// 参考 v40.5.1 BTDecorator_CooldownReady 同款教训
// ============================================================

UBTDecorator_BlackboardCompare::UBTDecorator_BlackboardCompare()
{
	NodeName = TEXT("Blackboard Compare");

	bNotifyBecomeRelevant = false;
	bNotifyTick = false;

	// FlowAbortMode: Self — 与项目其他 Decorator 家族一致
	//   KeyA 或 KeyB 变化时 AI 应该重新评估分支
	FlowAbortMode = EBTFlowAbortMode::Self;

	// KeyA / KeyB 接受所有 BB 类型 — 大厂通用做法
	//   AddFloatFilter / AddObjectFilter 等是 AllowedTypes 数组的 AddUnique,
	//   重复注册 6 种类型 → 策划在编辑器可选任意类型
	// 直接内联 GET_MEMBER_NAME_CHECKED — 返回 FName, 类型与 AddFloatFilter 等签名严格匹配
	// (参考 BTDecorator_HPThreshold / BTDecorator_InAttackRange 同款写法 — 抽局部变量会误改成 FString)
	KeyA.AddFloatFilter(this, GET_MEMBER_NAME_CHECKED(UBTDecorator_BlackboardCompare, KeyA));
	KeyA.AddIntFilter(this, GET_MEMBER_NAME_CHECKED(UBTDecorator_BlackboardCompare, KeyA));
	KeyA.AddBoolFilter(this, GET_MEMBER_NAME_CHECKED(UBTDecorator_BlackboardCompare, KeyA));
	KeyA.AddVectorFilter(this, GET_MEMBER_NAME_CHECKED(UBTDecorator_BlackboardCompare, KeyA));
	KeyA.AddStringFilter(this, GET_MEMBER_NAME_CHECKED(UBTDecorator_BlackboardCompare, KeyA));
	KeyA.AddObjectFilter(this, GET_MEMBER_NAME_CHECKED(UBTDecorator_BlackboardCompare, KeyA), AActor::StaticClass());

	KeyB.AddFloatFilter(this, GET_MEMBER_NAME_CHECKED(UBTDecorator_BlackboardCompare, KeyB));
	KeyB.AddIntFilter(this, GET_MEMBER_NAME_CHECKED(UBTDecorator_BlackboardCompare, KeyB));
	KeyB.AddBoolFilter(this, GET_MEMBER_NAME_CHECKED(UBTDecorator_BlackboardCompare, KeyB));
	KeyB.AddVectorFilter(this, GET_MEMBER_NAME_CHECKED(UBTDecorator_BlackboardCompare, KeyB));
	KeyB.AddStringFilter(this, GET_MEMBER_NAME_CHECKED(UBTDecorator_BlackboardCompare, KeyB));
	KeyB.AddObjectFilter(this, GET_MEMBER_NAME_CHECKED(UBTDecorator_BlackboardCompare, KeyB), AActor::StaticClass());
}

/**
 * @brief 生成 BT 编辑器中显示的节点描述 (BB.KeyA Op Right)
 * @return 形如 "BB.DistanceToTarget > 200.00" 的字符串, 显示在节点标题下方
 *
 * 大厂原则: 调试/可视化辅助, 不参与决策. 仅用于编辑器节点展示.
 */
FString UBTDecorator_BlackboardCompare::GetStaticDescription() const
{
	// 把 Op 翻成符号 — 与 BTDecorator_HPThreshold 风格一致
	auto OpToStr = [](EBBCompareOp InOp) -> const TCHAR*
	{
		switch (InOp)
		{
		case EBBCompareOp::Equal:          return TEXT("==");
		case EBBCompareOp::NotEqual:       return TEXT("!=");
		case EBBCompareOp::Less:           return TEXT("<");
		case EBBCompareOp::LessOrEqual:    return TEXT("<=");
		case EBBCompareOp::Greater:        return TEXT(">");
		case EBBCompareOp::GreaterOrEqual: return TEXT(">=");
		}
		return TEXT("?");
	};

	// 右侧表达式
	FString RightStr;
	switch (RightSource)
	{
	case EBBCompareRightSource::Key:
		RightStr = FString::Printf(TEXT("BB.%s"), *KeyB.SelectedKeyName.ToString());
		break;
	case EBBCompareRightSource::FloatValue:
		RightStr = FString::Printf(TEXT("%.2f"), BFloatValue);
		break;
	case EBBCompareRightSource::IntValue:
		RightStr = FString::Printf(TEXT("%d"), BIntValue);
		break;
	case EBBCompareRightSource::BoolValue:
		RightStr = BBoolValue ? TEXT("true") : TEXT("false");
		break;
	case EBBCompareRightSource::VectorValue:
		RightStr = BVectorValue.ToString();
		break;
	case EBBCompareRightSource::StringValue:
		RightStr = FString::Printf(TEXT("\"%s\""), *BStringValue);
		break;
	case EBBCompareRightSource::ObjectValue:
		RightStr = BObjectValue ? BObjectValue->GetName() : TEXT("<null>");
		break;
	}

	return FString::Printf(TEXT("BB.%s %s %s"),
		*KeyA.SelectedKeyName.ToString(),
		OpToStr(Op),
		*RightStr);
}

/**
 * @brief BT 单次决策入口 — 按 BB Key 类型分发比较 KeyA 与右侧(常量或 KeyB)
 * @param OwnerComp BT 组件引用, 用于获取 BB / AIController
 * @param NodeMemory Decorator 节点内存(本类未使用, 因没设 bUsesOwnMemory)
 * @return 比较结果 true=放行分支 / false=拒绝
 *
 * 单点决策: 无 Tick / 无 BecameRelevant, 装饰器家族一致行为.
 * 三层零兜底: KeyA 必须配 → KeyB 在 Key 模式下必须配 → 类型必须匹配.
 * 任何错配 Log Error + return false, 显式失败绝不静默 fallback.
 */
bool UBTDecorator_BlackboardCompare::CalculateRawConditionValue(
	UBehaviorTreeComponent& OwnerComp, uint8* NodeMemory) const
{
	UBlackboardComponent* BB = OwnerComp.GetBlackboardComponent();
	if (!BB)
	{
		// 与现有 Decorator 家族一致 — BB 缺失静默返回 false
		// (BT 框架层已 Log Error, 这里不再重复刷屏)
		return false;
	}

	AAIController* AIC = OwnerComp.GetAIOwner();
	if (!AIC)
	{
		return false;
	}

	// ─── 零兜底 1/3: KeyA 必须配 ───
	if (KeyA.SelectedKeyName.IsNone())
	{
		UE_LOG(LogTemp, Error,
			TEXT("[BTDecorator_BlackboardCompare] %s: KeyA 未配置 (SelectedKeyName=None). "
			     "【修复路径】BT 编辑器 → 节点 Details → KeyA 选择 Key 名."),
			*AIC->GetName());
		return false;
	}

	const FBlackboard::FKey KeyAID = BB->GetKeyID(KeyA.SelectedKeyName);
	if (KeyAID == FBlackboard::InvalidKey)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[BTDecorator_BlackboardCompare] %s: KeyA '%s' 在 BB 里找不到. "
			     "【修复路径】打开 BB_AI_*.uasset, 确认 Key '%s' 存在."),
			*AIC->GetName(),
			*KeyA.SelectedKeyName.ToString(),
			*KeyA.SelectedKeyName.ToString());
		return false;
	}

	const TSubclassOf<UBlackboardKeyType> KeyAType = BB->GetBlackboardAsset()->GetKeyType(KeyAID);

	// ─── 零兜底 2/3: RightSource=Key 时 KeyB 必须配 + 类型匹配 ───
	TSubclassOf<UBlackboardKeyType> KeyBType = nullptr;

	if (RightSource == EBBCompareRightSource::Key)
	{
		if (KeyB.SelectedKeyName.IsNone())
		{
			UE_LOG(LogTemp, Error,
				TEXT("[BTDecorator_BlackboardCompare] %s: RightSource=Key 但 KeyB 未配置. "
				     "【修复路径】Details → RightSource 改成常量模式 (FloatValue 等) 或配置 KeyB."),
				*AIC->GetName());
			return false;
		}

		const FBlackboard::FKey KeyBID = BB->GetKeyID(KeyB.SelectedKeyName);
		if (KeyBID == FBlackboard::InvalidKey)
		{
			UE_LOG(LogTemp, Error,
				TEXT("[BTDecorator_BlackboardCompare] %s: KeyB '%s' 在 BB 里找不到. "
				     "【修复路径】打开 BB 资产, 确认 Key '%s' 存在."),
				*AIC->GetName(),
				*KeyB.SelectedKeyName.ToString(),
				*KeyB.SelectedKeyName.ToString());
			return false;
		}

		KeyBType = BB->GetBlackboardAsset()->GetKeyType(KeyBID);
	}
	else
	{
		// RightSource = 常量 → KeyBType 与 KeyAType 同类 (让 ValidateTypeMatch 直接通过)
		KeyBType = KeyAType;
	}

	// ─── 零兜底 3/3: 类型必须匹配 (异类型比较没意义) ───
	if (!ValidateTypeMatch(*BB, KeyAType, AIC))
	{
		return false;
	}

	// ─── 分支: 按 KeyAType 取值, 与 KeyB/常量 比较 ───
	bool bResult = false;

	if (KeyAType == UBlackboardKeyType_Float::StaticClass())
	{
		const float A = BB->GetValueAsFloat(KeyA.SelectedKeyName);
		const float B = (RightSource == EBBCompareRightSource::Key)
			? BB->GetValueAsFloat(KeyB.SelectedKeyName)
			: BFloatValue;
		bResult = CompareFloat(A, B);
	}
	else if (KeyAType == UBlackboardKeyType_Int::StaticClass())
	{
		const int32 A = BB->GetValueAsInt(KeyA.SelectedKeyName);
		const int32 B = (RightSource == EBBCompareRightSource::Key)
			? BB->GetValueAsInt(KeyB.SelectedKeyName)
			: BIntValue;
		bResult = CompareInt(A, B);
	}
	else if (KeyAType == UBlackboardKeyType_Bool::StaticClass())
	{
		const bool A = BB->GetValueAsBool(KeyA.SelectedKeyName);
		const bool B = (RightSource == EBBCompareRightSource::Key)
			? BB->GetValueAsBool(KeyB.SelectedKeyName)
			: BBoolValue;
		bResult = CompareBool(A, B);
	}
	else if (KeyAType == UBlackboardKeyType_Vector::StaticClass())
	{
		const FVector A = BB->GetValueAsVector(KeyA.SelectedKeyName);
		const FVector B = (RightSource == EBBCompareRightSource::Key)
			? BB->GetValueAsVector(KeyB.SelectedKeyName)
			: BVectorValue;
		bResult = CompareVector(A, B);
	}
	else if (KeyAType == UBlackboardKeyType_String::StaticClass())
	{
		const FString A = BB->GetValueAsString(KeyA.SelectedKeyName);
		const FString B = (RightSource == EBBCompareRightSource::Key)
			? BB->GetValueAsString(KeyB.SelectedKeyName)
			: BStringValue;
		bResult = CompareString(A, B);
	}
	else if (KeyAType == UBlackboardKeyType_Object::StaticClass())
	{
		UObject* A = BB->GetValueAsObject(KeyA.SelectedKeyName);
		UObject* B = (RightSource == EBBCompareRightSource::Key)
			? BB->GetValueAsObject(KeyB.SelectedKeyName)
			: BObjectValue.Get();
		bResult = CompareObject(A, B);
	}
	else
	{
		UE_LOG(LogTemp, Error,
			TEXT("[BTDecorator_BlackboardCompare] %s: KeyA '%s' 类型不支持 (类型=%s). "
			     "【修复路径】KeyA 必须是 Float/Int/Bool/Vector/String/Object 之一."),
			*AIC->GetName(),
			*KeyA.SelectedKeyName.ToString(),
			KeyAType ? *KeyAType->GetName() : TEXT("<null>"));
		return false;
	}

	// 决策日志 — Verbose (项目标准: 装饰器高频 tick 用 Verbose, 默认隐藏)
	// 与 BTDecorator_InAttackRange / BTDecorator_CooldownReady 风格对称
	// 显式 format (避免 lambda + RightChop 在 consteval 下出锅, 参考 v40.12 TCheckedFormatString 教训)
	{
		const TCHAR* OpStr = TEXT("?");
		switch (Op)
		{
		case EBBCompareOp::Equal:          OpStr = TEXT("=="); break;
		case EBBCompareOp::NotEqual:       OpStr = TEXT("!="); break;
		case EBBCompareOp::Less:           OpStr = TEXT("<");  break;
		case EBBCompareOp::LessOrEqual:    OpStr = TEXT("<="); break;
		case EBBCompareOp::Greater:        OpStr = TEXT(">");  break;
		case EBBCompareOp::GreaterOrEqual: OpStr = TEXT(">="); break;
		}

		// 描述符 (右半部分) — 直接调 GetStaticDescription 再截掉 "BB.KeyA " 前缀
		// 但 consteval 不喜欢在实参里调成员函数 + 拼接, 改用静态描述 (避免 C2100/C2131 雪崩)
		FString Desc;
		switch (RightSource)
		{
		case EBBCompareRightSource::Key:
			Desc = FString::Printf(TEXT("BB.%s"), *KeyB.SelectedKeyName.ToString());
			break;
		case EBBCompareRightSource::FloatValue:
			Desc = FString::Printf(TEXT("%.2f"), BFloatValue);
			break;
		case EBBCompareRightSource::IntValue:
			Desc = FString::Printf(TEXT("%d"), BIntValue);
			break;
		case EBBCompareRightSource::BoolValue:
			Desc = BBoolValue ? TEXT("true") : TEXT("false");
			break;
		case EBBCompareRightSource::VectorValue:
			Desc = BVectorValue.ToString();
			break;
		case EBBCompareRightSource::StringValue:
			Desc = FString::Printf(TEXT("\"%s\""), *BStringValue);
			break;
		case EBBCompareRightSource::ObjectValue:
			Desc = BObjectValue ? BObjectValue->GetName() : TEXT("<null>");
			break;
		}

		UE_LOG(LogTemp, Verbose,
			TEXT("[BTDecorator_BlackboardCompare] %s: BB.%s %s %s → %s"),
			*AIC->GetName(),
			*KeyA.SelectedKeyName.ToString(),
			OpStr,
			*Desc,
			bResult ? TEXT("PASS") : TEXT("FAIL"));
	}

	return bResult;
}

// ============================================================
// 类型匹配验证 — 大厂原则: 异类型比较无意义, 必须 Log Error + return false
// 设计权衡: KeyB 来自 BB 时 KeyBType 必须 == KeyAType
//         KeyB 来自常量时 KeyAType 决定一切 (常量按编辑器配的字段值)
// ============================================================
bool UBTDecorator_BlackboardCompare::ValidateTypeMatch(
	const UBlackboardComponent& BB,
	const TSubclassOf<UBlackboardKeyType> KeyAType,
	AAIController* AIC) const
{
	// 常量模式: KeyB 类型 = KeyA 类型 (编辑器配常量时类型由策划的 KeyA 决定)
	if (RightSource != EBBCompareRightSource::Key)
	{
		return true;
	}

	// Key 模式: KeyA 和 KeyB 必须同类型 (异类型比较无意义, 禁止静默 fallback)
	TSubclassOf<UBlackboardKeyType> KeyBType = nullptr;
	const FBlackboard::FKey KeyBID = BB.GetKeyID(KeyB.SelectedKeyName);
	if (KeyBID != FBlackboard::InvalidKey)
	{
		KeyBType = BB.GetBlackboardAsset()->GetKeyType(KeyBID);
	}

	if (KeyAType != KeyBType)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[BTDecorator_BlackboardCompare] %s: KeyA '%s' 与 KeyB '%s' 类型不匹配 "
			     "(KeyA=%s, KeyB=%s). 异类型比较无意义, 拒绝静默转换. "
			     "【修复路径】BT 资产 → BB Key 类型对齐, 或改成 KeyA vs 常量模式."),
			*AIC->GetName(),
			*KeyA.SelectedKeyName.ToString(),
			*KeyB.SelectedKeyName.ToString(),
			KeyAType ? *KeyAType->GetName() : TEXT("<null>"),
			KeyBType ? *KeyBType->GetName() : TEXT("<null>"));
		return false;
	}

	return true;
}

// ============================================================
// 6 种 CompareXXX 函数 — 每个函数覆盖 6 种 Op
// 大厂原则 - 0 重复: switch case 集中在一处, Op 改名只改一处
// Bool / Object 类型仅支持 ==/!= (其他 Op 无意义, 静默 false 不算兜底 — 这是语义本身)
// ============================================================
bool UBTDecorator_BlackboardCompare::CompareFloat(float A, float B) const
{
	switch (Op)
	{
	case EBBCompareOp::Equal:          return FMath::IsNearlyEqual(A, B);
	case EBBCompareOp::NotEqual:       return !FMath::IsNearlyEqual(A, B);
	case EBBCompareOp::Less:           return A < B;
	case EBBCompareOp::LessOrEqual:    return A <= B;
	case EBBCompareOp::Greater:        return A > B;
	case EBBCompareOp::GreaterOrEqual: return A >= B;
	}
	return false;
}

/**
 * @brief Int 类型比较 — 根据 Op 决定比较方式 (6 种 Op 全支持)
 * @param A 左侧 BB Int 值 / KeyA
 * @param B 右侧 Int 值 / KeyB / 常量
 * @return 比较结果, 未定义 Op 默认 false
 */
bool UBTDecorator_BlackboardCompare::CompareInt(int32 A, int32 B) const
{
	switch (Op)
	{
	case EBBCompareOp::Equal:          return A == B;
	case EBBCompareOp::NotEqual:       return A != B;
	case EBBCompareOp::Less:           return A < B;
	case EBBCompareOp::LessOrEqual:    return A <= B;
	case EBBCompareOp::Greater:        return A > B;
	case EBBCompareOp::GreaterOrEqual: return A >= B;
	}
	return false;
}

/**
 * @brief Bool 类型比较 — 仅支持 == / != (其他 Op 在 Bool 语义上无意义)
 * @param A 左侧 BB Bool 值 / KeyA
 * @param B 右侧 Bool 值 / KeyB / 常量
 * @return 比较结果, 除 ==/!= 外的 Op 静默返回 false (类型语义决定, 非兜底)
 */
bool UBTDecorator_BlackboardCompare::CompareBool(bool A, bool B) const
{
	// Bool 仅 ==/!= 有意义; 其他 Op 静默返回 false
	// (这不是兜底 — 是 Bool 类型的语义本身)
	switch (Op)
	{
	case EBBCompareOp::Equal:    return A == B;
	case EBBCompareOp::NotEqual: return A != B;
	default:                     return false;
	}
}

/**
 * @brief Vector 类型比较 — 分量逐项比较, X/Y/Z 均满足才返回 true
 * @param A 左侧 BB Vector 值 / KeyA
 * @param B 右侧 Vector 值 / KeyB / 常量
 * @return 比较结果, Equal 用 UE 等价容差(0.0001), 其他严格分量比较
 */
bool UBTDecorator_BlackboardCompare::CompareVector(const FVector& A, const FVector& B) const
{
	switch (Op)
	{
	case EBBCompareOp::Equal:          return A.Equals(B);
	case EBBCompareOp::NotEqual:       return !A.Equals(B);
	case EBBCompareOp::Less:           return A.X < B.X && A.Y < B.Y && A.Z < B.Z;
	case EBBCompareOp::LessOrEqual:    return A.X <= B.X && A.Y <= B.Y && A.Z <= B.Z;
	case EBBCompareOp::Greater:        return A.X > B.X && A.Y > B.Y && A.Z > B.Z;
	case EBBCompareOp::GreaterOrEqual: return A.X >= B.X && A.Y >= B.Y && A.Z >= B.Z;
	}
	return false;
}

/**
 * @brief String 类型比较 — 用 FString::Compare 字典序比较 (区分大小写)
 * @param A 左侧 BB String 值 / KeyA
 * @param B 右侧 String 值 / KeyB / 常量
 * @return 比较结果, Compare(B)<0 表示 A 字典序在前
 */
bool UBTDecorator_BlackboardCompare::CompareString(const FString& A, const FString& B) const
{
	switch (Op)
	{
	case EBBCompareOp::Equal:          return A == B;
	case EBBCompareOp::NotEqual:       return A != B;
	case EBBCompareOp::Less:           return A.Compare(B) < 0;
	case EBBCompareOp::LessOrEqual:    return A.Compare(B) <= 0;
	case EBBCompareOp::Greater:        return A.Compare(B) > 0;
	case EBBCompareOp::GreaterOrEqual: return A.Compare(B) >= 0;
	}
	return false;
}

/**
 * @brief Object 类型比较 — 仅指针相等比较 (引用同一对象才视为相等)
 * @param A 左侧 BB Object 指针 / KeyA
 * @param B 右侧 Object 指针 / KeyB / 常量
 * @return 比较结果, 除 ==/!= 外的 Op 静默返回 false (指针无大小语义)
 */
bool UBTDecorator_BlackboardCompare::CompareObject(UObject* A, UObject* B) const
{
	// Object 仅 ==/!= 有意义 (指针相等); 其他 Op 静默 false
	switch (Op)
	{
	case EBBCompareOp::Equal:    return A == B;
	case EBBCompareOp::NotEqual: return A != B;
	default:                     return false;
	}
}