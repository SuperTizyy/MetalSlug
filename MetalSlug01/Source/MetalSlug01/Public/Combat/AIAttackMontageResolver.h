// Copyright (c) 2026.
// 文件作用: AI 攻击动画解析器（职责链模式）
// 设计原则: 单一职责 — 只负责"如何从武器资产拿到一个可用的攻击蒙太奇"
// 架构: 与 BT/BTTask_MeleeAttack 解耦, 任何需要 AI 攻击动画的地方都能复用

#pragma once

// ==========================================
// 头文件包含说明
// ==========================================
// UE 引擎核心最小化头文件
#include "CoreMinimal.h"

// 引入 UAnimMontage 完整定义 (USTRUCT 字段使用 TObjectPtr<UAnimMontage> 必须有完整类型)
#include "Animation/AnimMontage.h"

// UE 自动生成的头文件
#include "AIAttackMontageResolver.generated.h"

// 前置声明（避免头文件循环包含, 加快编译）
class ABaseWeapon;


/**
 * @struct FAIAttackMontageResult
 * @brief AI 攻击动画解析结果（出参结构体）
 *
 * 设计要点:
 * 1. 不只返回 nullptr / 指针, 还返回来源层级, 让日志能精确指出"从哪一层兜底拿到的"
 * 2. bFromFallback = true 时, 调用方应该报警 (告诉用户"你 BP 配错了")
 *
 * 出参字段:
 * - Montage:        最终选中的蒙太奇 (可能为 nullptr, 此时调用方应放弃本次攻击)
 * - ResolvedIndex:  实际命中的数组索引 (用于日志/调试)
 * - FallbackLevel:  来源层级 (0 = 设计意图路径, 1/2/3 = 各级兜底)
 * - bFromFallback:  是否走了兜底链 (用于上报 / 报警)
 */
USTRUCT(BlueprintType)
struct METALSLUG01_API FAIAttackMontageResult
{
	GENERATED_BODY()

	/** 最终选中的蒙太奇 (可能为 nullptr) */
	UPROPERTY(BlueprintReadOnly, Category = "AI Combat")
	TObjectPtr<UAnimMontage> Montage = nullptr;

	/** 实际命中的数组索引 (用于日志) */
	UPROPERTY(BlueprintReadOnly, Category = "AI Combat")
	int32 ResolvedIndex = INDEX_NONE;

	/** 来源层级 (0 = 设计意图, -1 = 显式失败/v32 零兜底) */
	UPROPERTY(BlueprintReadOnly, Category = "AI Combat")
	int32 FallbackLevel = -1;

	/** 是否走了兜底链 (v32 零兜底: 永远是 false, 保留字段仅防止 BP 蓝图依赖错误) */
	UPROPERTY(BlueprintReadOnly, Category = "AI Combat")
	bool bFromFallback = false;
};


/**
 * @class UAIAttackMontageResolver
 * @brief AI 攻击动画解析器（静态工具类, v32 零兜底）
 *
 * 【v32 大厂改造】删除 Level 1/2/3 兜底链
 *
 * 业务背景 (大厂架构师视角):
 *   老设计: 4 层兜底链 (Level 0 设计意图 / Level 1 ComboIndex→0 / Level 2 重击代替 / Level -1 失败)
 *           BP 配置错误被静默兼容, AI 永远"能挥刀", 玩家察觉不到配置错
 *
 *   新设计 (v32 — 零兜底):
 *           - 只有 Level 0 (设计意图) + Level -1 (Log Error 完全失败)
 *           - 配置缺失 → Log Error + return Montage=nullptr → AI 放弃本次攻击
 *           - 调用方 OnAIRequestAttack_Simple 已合规处理 Montage=nullptr
 *           - BP 美术必须严格配 LightAttackMontages 数组 (Level 0 路径才会命中)
 *
 * 职责:
 *   1. 提供 ResolveLightAttackMontage: 解析 AI 轻击蒙太奇 (Level 0 唯一路径)
 *   2. 提供 ResolveHeavyAttackMontage: 解析 AI 重击蒙太奇 (Level 0 唯一路径)
 *   3. 提供 ExplainConfigurationIssue: 生成 BP 配置修复建议
 *
 * 不负责:
 *   - 不播放蒙太奇 (那是 BaseCharacter / AIAttackComponent 的事)
 *   - 不管冷却/BT (那是 BTTask 的事)
 *   - 不持有任何状态 (纯静态工具)
 */
UCLASS()
class METALSLUG01_API UAIAttackMontageResolver : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * 解析 AI 轻击蒙太奇 (Level 0 唯一路径 — v32 零兜底)
	 *
	 * 路径:
	 *   Level 0 (设计意图): Weapon->GetAttackMontage(false, ComboIndex)
	 *                       — 这是正常情况下应命中的路径
	 *   Level -1 (显式失败): 找不到 → Log Error + return Montage=nullptr
	 *
	 * 历史 (v22-v31.x):
	 *   - 4 层兜底链, AI 永远能挥刀 — 已删除
	 *   - BP 配置缺失被静默兼容 (Level 1/2 降级) — 已改为 Log Error
	 *   - 强制 BP 美术按规范配 LightAttackMontages 数组
	 *
	 * @param Weapon       当前 AI 装备的武器 (可为 null)
	 * @param ComboIndex   期望的轻击段索引 (0/1/2...)
	 * @return             解析结果 (Montage 或 nullptr)
	 */
	UFUNCTION(BlueprintCallable, Category = "AI Combat|Montage Resolver")
	static FAIAttackMontageResult ResolveLightAttackMontage(
		const ABaseWeapon* Weapon,
		int32 ComboIndex = 0);

	/**
	 * 解析 AI 重击蒙太奇 (Level 0 唯一路径 — v32 零兜底)
	 *
	 * 路径:
	 *   Level 0 (设计意图): Weapon->GetAttackMontage(true, 0)
	 *   Level -1 (显式失败): 找不到 → Log Error + return Montage=nullptr
	 *
	 * @param Weapon  当前 AI 装备的武器
	 * @return        解析结果
	 */
	UFUNCTION(BlueprintCallable, Category = "AI Combat|Montage Resolver")
	static FAIAttackMontageResult ResolveHeavyAttackMontage(const ABaseWeapon* Weapon);

	/**
	 * 生成 BP 配置修复建议 (用于报警日志)
	 *
	 * 输出示例:
	 *   "请打开 BP_Weapon_Knife, 在 Defaults Only / Combat / Weapon Animations
	 *    下配置 LightAttackMontages 数组 (至少 1 个轻击蒙太奇, 推荐 3 个)"
	 *
	 * @param Weapon       当前 AI 装备的武器 (可为 null)
	 * @param Result       解析结果
	 * @return             人类可读的修复建议
	 */
	UFUNCTION(BlueprintCallable, Category = "AI Combat|Montage Resolver")
	static FString ExplainConfigurationIssue(
		const ABaseWeapon* Weapon,
		const FAIAttackMontageResult& Result);

private:
	/**
	 * 内部: 安全取武器蒙太奇 (private helper, 封装 GetAttackMontage 的可访问性)
	 * @param Weapon   武器
	 * @param bHeavy   是否重击
	 * @param ComboIdx 轻击段索引
	 * @return         蒙太奇或 nullptr
	 */
	static UAnimMontage* SafeGetMontage(
		const ABaseWeapon* Weapon,
		bool bHeavy,
		int32 ComboIdx);

	/**
	 * 内部: 武器类名提取 (去掉 _C 后缀, 日志友好)
	 * @param Weapon 武器
	 * @return        例如 "BP_Weapon_Knife" (而不是 "BP_Weapon_Knife_C")
	 */
	static FString GetWeaponDisplayName(const ABaseWeapon* Weapon);
};