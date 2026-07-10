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
	// 【v32 零兜底改造】删除 Level 1 (ComboIndex→0 降级) + Level 2 (重击代替轻击)
	//
	// 历史 (v6-v31.x) 反模式:
	//   Level 1: ComboIndex 越界 → 退到 0 → AI 用错段 → 玩家节奏错乱
	//   Level 2: 完全没轻击蒙太奇 → 用重击代替 → AI 永远能挥刀, BP 配错被掩盖
	//
	// 新架构 (v32 — 单一 Level 0 路径):
	//   - 只有 Level 0 (设计意图) + Level -1 (Log Error 完全失败) 两种状态
	//   - BP 没配 LightAttackMontages → Log Error + return Montage=nullptr
	//   - ComboIndex 越界 → Log Error + return Montage=nullptr (不再降级到 0)
	//   - 调用方 OnAIRequestAttack_Simple 已合规处理 Montage=nullptr (v25 链, return false)
	//
	// 大厂原则 (v32):
	//   - BP 美术必须严格按 BP 编辑器配 ComboIndex=N 段的 LightAttackMontages
	//   - 不再"找个能用的蒙太奇蒙混过关"
	//   - 配置缺失立即 Log Error, BP 美术刷新后再继续
	// ============================================================

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
	// Level -1: ComboIndex 越界或没配 (v32 零兜底: 不再降级到 ComboIndex=0)
	// ============================================================
	UE_LOG(LogTemp, Error,
		TEXT("[AIAttackMontageResolver] 武器=%s 无法解析 LightAttackMontage (ComboIndex=%d). "
			 "【v32 零兜底】拒绝降级到 ComboIndex=0 / 重击代替. "
			 "根因: BP 编辑器 LightAttackMontages 数组未配置该 ComboIndex, 或数组为空. "
			 "请打开 %s 资产 → Class Defaults → Combat → Weapon Animations → "
			 "在 LightAttackMontages 数组中添加 ComboIndex=%d 对应的 UAnimMontage. "
			 "AI 本次攻击将放弃 (调用方 OnAIRequestAttack_Simple 已 Log Warning + return false)."),
		*WeaponName, ComboIndex, *WeaponName, ComboIndex);

	Result.Montage = nullptr;
	Result.ResolvedIndex = INDEX_NONE;
	Result.FallbackLevel = -1;
	Result.bFromFallback = false; // v32: 不算兜底, 算显式失败
	return Result;
}


// ==========================================
// 公共接口: ResolveHeavyAttackMontage
// ==========================================

/**
 * 解析 AI 重击蒙太奇 (v32 — 零兜底)
 *
 * 历史 (v6-v31.x):
 *   重击为空 → 降级到轻击[0]代替 (Level 1 兜底) → "AI 至少能挥刀" 幌子下掩盖 BP 配错
 *
 * 新架构 (v32):
 *   - 只有 Level 0 (设计意图) + Level -1 (Log Error 完全失败)
 *   - 重击没配 → Log Error + return Montage=nullptr (不再降级到轻击)
 *   - 调用方 AI 攻击失败, BP 美术重配 HeavyAttackMontage
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

	// Level 0: 设计意图 — Weapon->GetAttackMontage(true, 0)
	UAnimMontage* Montage = SafeGetMontage(Weapon, true, 0);
	if (Montage)
	{
		Result.Montage = Montage;
		Result.ResolvedIndex = INDEX_NONE;
		Result.FallbackLevel = 0;
		Result.bFromFallback = false;
		return Result;
	}

	// 【v32 零兜底改造】删除 Level 1 (用轻击[0]代替重击) (line 173-187)
	//
	// 历史 (v6-v31.x) 反模式:
	//   重击蒙太奇为空 → 降级到轻击[0]代替 (Level 1)
	//   AI 永远能挥刀, BP 重击蒙太奇配置错误被永远掩盖
	//
	// 新行为 (v32):
	//   - Log Error + return Montage=nullptr
	//   - BP 美术必须配 HeavyAttackMontage 才能让 AI 重击
	UE_LOG(LogTemp, Error,
		TEXT("[AIAttackMontageResolver] 武器=%s HeavyAttackMontage 为空! "
			 "【v32 零兜底】拒绝降级到轻击[0]代替. "
			 "根因: BP 编辑器 HeavyAttackMontage 字段未配置. "
			 "请打开 %s 资产 → Class Defaults → Combat → Weapon Animations → "
			 "设置 HeavyAttackMontage 字段. "
			 "AI 本次重击将放弃."),
		*WeaponName, *WeaponName);

	Result.Montage = nullptr;
	Result.ResolvedIndex = INDEX_NONE;
	Result.FallbackLevel = -1;
	Result.bFromFallback = false; // v32: 不算兜底
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