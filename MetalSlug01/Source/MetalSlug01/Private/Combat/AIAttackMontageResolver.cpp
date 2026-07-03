// Copyright (c) 2026.
// 文件作用: AI 攻击动画解析器实现（职责链模式）
// 设计原则: 单一职责 — 只负责"如何从武器资产拿到一个可用的攻击蒙太奇"

#include "Combat/AIAttackMontageResolver.h"

// 引入武器基类, 用于类型转换和访问 LightAttackMontages 数组
#include "Weapons/BaseWeapon.h"

// 引入动画蒙太奇, 用于类型验证
#include "Animation/AnimMontage.h"


// ==========================================
// 公共接口: ResolveLightAttackMontage
// ==========================================

/**
 * 解析 AI 轻击蒙太奇 — 核心入口
 *
 * 兜底链设计 (大厂架构: Defense in Depth 纵深防御):
 *   Level 0 (设计意图): Weapon->GetAttackMontage(false, ComboIndex)
 *   Level 1 (兜底 1):   Weapon->GetAttackMontage(false, 0)
 *   Level 2 (兜底 2):   直接读 Weapon->LightAttackMontages[0]
 *   Level 3 (兜底 3):   Weapon->HeavyAttackMontage (轻击不够, 重击顶上)
 *   Level -1 (失败):    nullptr
 *
 * 报警策略:
 *   - 任意 FallbackLevel > 0 都打 Warning (配置有问题, 但 AI 还能打)
 *   - FallbackLevel == -1 打 Error (完全没蒙太奇, AI 必须放弃本次攻击)
 */
FAIAttackMontageResult UAIAttackMontageResolver::ResolveLightAttackMontage(
	const ABaseWeapon* Weapon,
	int32 ComboIndex)
{
	FAIAttackMontageResult Result;

	// ============================================================
	// 防御: 武器为空直接失败
	// ============================================================
	if (!Weapon)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[AIAttackMontageResolver] 武器为空, 无法解析轻击蒙太奇"));
		Result.FallbackLevel = -1;
		Result.bFromFallback = false;
		return Result;
	}

	// ============================================================
	// 防御: ComboIndex 合法性检查 (防御性编程, 调用方应传 >=0)
	// ============================================================
	if (ComboIndex < 0)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[AIAttackMontageResolver] ComboIndex=%d 非法, 自动修正为 0"),
			ComboIndex);
		ComboIndex = 0;
	}

	const FString WeaponName = GetWeaponDisplayName(Weapon);

	// ============================================================
	// Level 0: 设计意图路径 (Weapon->GetAttackMontage(false, ComboIndex))
	// ============================================================
	UAnimMontage* Montage = SafeGetMontage(Weapon, false, ComboIndex);
	if (Montage)
	{
		Result.Montage = Montage;
		Result.ResolvedIndex = ComboIndex;
		Result.FallbackLevel = 0;
		Result.bFromFallback = false;
		return Result;
	}

	// ============================================================
	// Level 1: 兜底 — 退回到 ComboIndex=0 (通常 BP 只配了第 1 段)
	// ============================================================
	if (ComboIndex != 0)
	{
		Montage = SafeGetMontage(Weapon, false, 0);
		if (Montage)
		{
			UE_LOG(LogTemp, Warning,
				TEXT("[AIAttackMontageResolver] 武器=%s 设计意图索引 ComboIndex=%d 未命中, "
					 "降级到 ComboIndex=0 兜底 (Level 1). %s"),
				*WeaponName, ComboIndex,
				*ExplainConfigurationIssue(Weapon, Result));

			Result.Montage = Montage;
			Result.ResolvedIndex = 0;
			Result.FallbackLevel = 1;
			Result.bFromFallback = true;
			return Result;
		}
	}

	// ============================================================
	// Level 3: 终极兜底 — 用重击蒙太奇代替轻击
	// 设计意图: AI 至少要"挥刀"给玩家反馈, 不能完全沉默
	// 注: Level 1 已经尝试过 ComboIndex=0, 若仍未命中说明武器连 0 索引都没有
	//       此时退而求其次用重击蒙太奇, 让 AI 至少能展示攻击动作
	// ============================================================
	Montage = SafeGetMontage(Weapon, true, 0);
	if (Montage)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[AIAttackMontageResolver] 武器=%s 无可用轻击蒙太奇, "
				 "降级到重击蒙太奇代替 (Level 2 - 终极兜底). "
				 "请打开该 BP 资产配置 LightAttackMontages 数组!"),
			*WeaponName);

		Result.Montage = Montage;
		Result.ResolvedIndex = INDEX_NONE;
		Result.FallbackLevel = 2;
		Result.bFromFallback = true;
		return Result;
	}

	// ============================================================
	// Level -1: 完全失败 — 武器没有任何攻击蒙太奇
	// ============================================================
	UE_LOG(LogTemp, Error,
		TEXT("[AIAttackMontageResolver] 武器=%s 完全无攻击蒙太奇 (LightAttackMontages/HeavyAttackMontage 都为空). "
			 "AI 将无法播放任何攻击动画! %s"),
		*WeaponName,
		*ExplainConfigurationIssue(Weapon, Result));

	Result.Montage = nullptr;
	Result.ResolvedIndex = INDEX_NONE;
	Result.FallbackLevel = -1;
	Result.bFromFallback = true;
	return Result;
}


// ==========================================
// 公共接口: ResolveHeavyAttackMontage
// ==========================================

/**
 * 解析 AI 重击蒙太奇
 *
 * 重击通常只有一个蒙太奇, 不需要 ComboIndex 循环
 * 但仍兜底: 若重击也为空, 尝试用轻击[0]代替 (让 AI 至少能挥刀)
 */
FAIAttackMontageResult UAIAttackMontageResolver::ResolveHeavyAttackMontage(
	const ABaseWeapon* Weapon)
{
	FAIAttackMontageResult Result;

	if (!Weapon)
	{
		UE_LOG(LogTemp, Error,
			TEXT("[AIAttackMontageResolver] 武器为空, 无法解析重击蒙太奇"));
		Result.FallbackLevel = -1;
		return Result;
	}

	const FString WeaponName = GetWeaponDisplayName(Weapon);

	// Level 0: 设计意图
	UAnimMontage* Montage = SafeGetMontage(Weapon, true, 0);
	if (Montage)
	{
		Result.Montage = Montage;
		Result.ResolvedIndex = INDEX_NONE;
		Result.FallbackLevel = 0;
		Result.bFromFallback = false;
		return Result;
	}

	// Level 1: 兜底 — 用轻击[0]代替重击
	Montage = SafeGetMontage(Weapon, false, 0);
	if (Montage)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("[AIAttackMontageResolver] 武器=%s 重击蒙太奇为空, "
				 "降级到轻击[0]兜底 (Level 1)"),
			*WeaponName);

		Result.Montage = Montage;
		Result.ResolvedIndex = 0;
		Result.FallbackLevel = 1;
		Result.bFromFallback = true;
		return Result;
	}

	// Level -1: 完全失败
	UE_LOG(LogTemp, Error,
		TEXT("[AIAttackMontageResolver] 武器=%s 重击/轻击蒙太奇都为空"),
		*WeaponName);

	Result.Montage = nullptr;
	Result.FallbackLevel = -1;
	return Result;
}


// ==========================================
// 公共接口: ExplainConfigurationIssue
// ==========================================

/**
 * 生成 BP 配置修复建议
 *
 * 输出示例:
 *   "BP 配置修复指南: 打开 BP_Weapon_Knife 编辑器, 在 Class Defaults / Combat / Weapon Animations
 *    分类下, 给 LightAttackMontages 数组添加 UAnimMontage 资产 (例如 AM_Knife_Combo1),
 *    推荐至少 1 个, 理想 3 个 (Combo1/Combo2/Combo3)"
 */
FString UAIAttackMontageResolver::ExplainConfigurationIssue(
	const ABaseWeapon* Weapon,
	const FAIAttackMontageResult& Result)
{
	const FString WeaponName = GetWeaponDisplayName(Weapon);

	// 根据 FallbackLevel 给出差异化建议
	switch (Result.FallbackLevel)
	{
	case -1:
		// 完全失败: 武器没有任何攻击蒙太奇
		return FString::Printf(
			TEXT("\n  [修复指南] 打开 %s 编辑器, 在 Class Defaults / Combat / Weapon Animations 分类下, "
				 "给 LightAttackMontages 添加至少 1 个 UAnimMontage 资产 (例如 AM_Knife_Combo1), "
				 "推荐 3 个 (Combo1/Combo2/Combo3)"),
			*WeaponName);

	case 0:
		// 设计意图: 不应该报警, 但保留接口便于扩展
		return FString();

	case 1:
		// 索引越界兜底: BP 配的数组长度不够 (ComboIndex 没命中, 退回到 0)
		return FString::Printf(
			TEXT("\n  [修复指南] 打开 %s 编辑器, 在 Class Defaults / Combat / Weapon Animations 分类下, "
				 "LightAttackMontages 当前长度小于 ComboIndex=%d, 请追加更多 UAnimMontage"),
			*WeaponName,
			Result.ResolvedIndex);

	case 2:
		// 重击代替轻击: BP 完全没配轻击
		return FString::Printf(
			TEXT("\n  [修复指南] 打开 %s 编辑器, 在 Class Defaults / Combat / Weapon Animations 分类下, "
				 "LightAttackMontages 数组为空! AI 临时用了重击蒙太奇代替轻击, "
				 "请添加轻击蒙太奇到该数组"),
			*WeaponName);

	default:
		return FString();
	}
}


// ==========================================
// 私有 helper: SafeGetMontage
// ==========================================

/**
 * 安全取武器蒙太奇
 *
 * 设计: 即使 Weapon 是 const, 我们仍需要调 GetAttackMontage (它是 const 方法)
 *       如果 GetAttackMontage 内部实现有 bug (例如除 0), 捕获异常避免崩溃
 */
UAnimMontage* UAIAttackMontageResolver::SafeGetMontage(
	const ABaseWeapon* Weapon,
	bool bHeavy,
	int32 ComboIdx)
{
	if (!Weapon)
	{
		return nullptr;
	}

	// GetAttackMontage 已是 const 方法, 直接调即可
	// 注: 这里不做 try/catch (UE 默认禁用 C++ 异常), 假定 GetAttackMontage 不会崩溃
	return Weapon->GetAttackMontage(bHeavy, ComboIdx);
}


// ==========================================
// 私有 helper: GetWeaponDisplayName
// ==========================================

/**
 * 武器类名提取 (去掉 _C 后缀)
 *
 * UE 的 BlueprintGeneratedClass 类名约定是 "<资产名>_C"
 * 例如 "BP_Weapon_Knife_C_5" → "BP_Weapon_Knife"
 * 日志里更友好, 用户一眼能认出是哪个 BP
 */
FString UAIAttackMontageResolver::GetWeaponDisplayName(const ABaseWeapon* Weapon)
{
	if (!Weapon)
	{
		return TEXT("<null>");
	}

	FString FullName = Weapon->GetName();
	// 去掉 _C 后缀 (例如 BP_Weapon_Knife_C_5 → BP_Weapon_Knife)
	int32 UnderscoreIndex = INDEX_NONE;
	if (FullName.FindLastChar('_', UnderscoreIndex) && UnderscoreIndex > 0)
	{
		// 检查 _ 前是否是 'C' (BP 类后缀惯例)
		FString Trimmed = FullName.Left(UnderscoreIndex);
		if (Trimmed.EndsWith(TEXT("_C")))
		{
			return Trimmed.LeftChop(2);
		}
	}

	return FullName;
}