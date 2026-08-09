// ==========================================
// DailyUpgradeRewardPage 的 ViewModel
// 目的: 把"读取状态"从 Page 抽离到 ViewModel
// 优势:
//   1. Page 只负责 UI 呈现
//   2. ViewModel 负责数据访问 + 业务规则
//   3. 后续可让 ViewModel 被多个 Page/Widget 共享
// ==========================================
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "DailyUpgradeRewardViewModel.generated.h"

class UUpgradeActivitySubsystem;
class UUpgradeActivitySaveModifier;

UCLASS(BlueprintType)
class METALSLUG01_API UDailyUpgradeRewardViewModel : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * 绑定到 Subsystem (Page 在 Initialize 时调用)
	 */
	UFUNCTION(BlueprintCallable, Category = "ViewModel")
	void Bind(UUpgradeActivitySubsystem* InSubsystem);

	/**
	 * 解除绑定 (Page 在 Destruct 时调用)
	 */
	UFUNCTION(BlueprintCallable, Category = "ViewModel")
	void Unbind();

	// ===== 数据访问 (View Only) =====

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "ViewModel")
	int32 GetCurrentDay() const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "ViewModel")
	int32 GetTotalDays() const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "ViewModel")
	bool IsCurrentDayClaimed() const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "ViewModel")
	int32 GetCurrentExperience() const;

	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "ViewModel")
	int32 GetCurrentRewardIconIndex() const;

	/** 当前是否被锁定 (Days 序列中未到解锁时间) */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "ViewModel")
	bool IsCurrentDayLocked() const;

	/** 玩家是否可领取今日奖励 (满足所有前置条件) */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "ViewModel")
	bool CanClaimToday() const;

	// ==========================================
	// 【v213 新增】Page 提交调试数据接口
	// 大厂原则:
	//   - Page 不直接 NewObject SaveModifier, 全部委托给 ViewModel
	//   - SaveModifier 实例在 ViewModel 内部管理 (Outer=this)
	//   - 单点真理: SelectedDay 解析 (RecordDate vs MaxRecordDate fallback)
	// ==========================================

	/**
	 * 【v213 大厂架构】提交调试数据: 在指定天数下设置 CurrentExperience
	 *
	 * 业务规则 (用户 2026.08.09 明确):
	 *   1. SelectedDay ∈ [1, 5] (合法范围, 超出 → Log Error + return false)
	 *   2. 若 AllRecords 中存在 RecordDate == SelectedDay 的记录 → 改该天
	 *   3. 若不存在 → Log Warning + fallback 到 MaxRecordDate 那天
	 *      (不允许"凭空创建", 因为 SaveModifier.CreateNewRecord 涉及策略问题: 不自动继承)
	 *   4. SaveModifier 内部已触发 ForceRefreshAllPages → OnGlobalRefresh,
	 *      Page 已订阅 OnGlobalRefresh → UI 自动刷新 (无 ViewModel 额外通知)
	 *
	 * 大厂原则 (单一真理 + 职责对等):
	 *   - SelectedDay 解析逻辑只在此一处, 禁止 Page 自行决策
	 *   - SaveModifier 生命周期归 ViewModel, Page 通过接口访问
	 *
	 * @param SelectedDay 用户在 ComboBox 选中的天数 (1-5)
	 * @param NewExp 新的经验值 (必须 >= 0, 否则 Log Error + return false)
	 * @return 是否修改成功
	 */
	UFUNCTION(BlueprintCallable, Category = "ViewModel|DebugModification")
	 bool ModifyCurrentExperience(int32 SelectedDay, int32 NewExp);

	/**
	 * 【v213 大厂架构】获取当前 AllRecords 中的最大 RecordDate
	 * 用途: ComboBox 默认值; SelectedDay fallback 目标
	 *
	 * @return MaxRecordDate; Subsystem 未 Bind / AllRecords 为空 → return 0 + Log Warning
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "ViewModel|DebugModification")
	int32 GetMaxAvailableDay() const;

	/**
	 * 【v213 大厂架构】获取指定天数的当前经验值 (只读)
	 * 用途: Page 显示参考 (不在 UI 上, 仅供 C++ 内部调用)
	 *
	 * @param Day 目标天数
	 * @return 该天 CurrentExperience; 记录不存在 → return 0 + Log Warning
	 */
	UFUNCTION(BlueprintCallable, BlueprintPure, Category = "ViewModel|DebugModification")
	int32 GetCurrentExpByDay(int32 Day) const;

private:
	UPROPERTY()
	TWeakObjectPtr<UUpgradeActivitySubsystem> Subsystem;

	/**
	 * 【v213 大厂架构】SaveModifier 实例 (Outer=this)
	 * 大厂原则 (生命周期集中管理):
	 *   - 不允许 Page 自行 NewObject, 必须经此 ViewModel 实例化
	 *   - Bind 时按需初始化, Unbind 时显式 Destroy
	 *   - 避免重复 NewObject 浪费内存
	 */
	UPROPERTY()
	TObjectPtr<UUpgradeActivitySaveModifier> CachedSaveModifier = nullptr;
};
