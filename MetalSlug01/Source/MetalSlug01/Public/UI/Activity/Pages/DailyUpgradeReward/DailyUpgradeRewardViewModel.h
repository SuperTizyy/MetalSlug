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
	 * 【v214 大厂架构】提交调试数据: 在指定天数下设置 CurrentExperience
	 *
	 * 业务规则 (用户 2026.08.11 明确):
	 *   1. SelectedDay ∈ [1, 5] (合法范围, 超出 → Log Error + return false)
	 *   2. SelectedDay 是绝对天数 (1=day1, 2=day2, ...)
	 *   3. 若 SelectedDay > MaxRecordDate, 依次创建 MaxRecordDate+1 ~ SelectedDay 的所有中间记录
	 *      (空记录, 不继承; 每条记录 bAutoSave=false, 只在 Modify 末尾统一 Broadcast 一次)
	 *   4. SaveModifier 内部已触发 ForceRefreshAllPages → OnGlobalRefresh,
	 *      Page 已订阅 OnGlobalRefresh → UI 自动刷新
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

	// ==========================================
	// 【v213.1 新增】任务完成次数写入接口
	// ==========================================

	/**
	 * 【v213.1 大厂架构】写入指定日期的 3 个任务完成次数
	 * 大厂原则:
	 *   - Page 不直接调 SaveModifier, 全部委托 ViewModel
	 *   - 一次调用写入 3 个任务, 统一触发 OnGlobalRefresh
	 *   - 任务次数合法性: 0~9 (ComboBox 选项约束)
	 *
	 * @param SelectedDay   ComboBoxString_SelectedDay 选中的天数 (1-5)
	 * @param Task1Count   任务一完成次数 (0-9)
	 * @param Task2Count   任务二完成次数 (0-9)
	 * @param Task3Count   任务三完成次数 (0-9)
	 * @return 是否全部写入成功; 任一失败 → return false (已写的回滚由 SaveModifier 保证原子性)
	 */
	UFUNCTION(BlueprintCallable, Category = "ViewModel|DebugModification")
	bool ModifyAllTaskCounts(int32 SelectedDay, int32 Task1Count, int32 Task2Count, int32 Task3Count);

	// ==========================================
	// 【v222 新增】一键重置所有页面进度
	// ==========================================

	/**
	 * 【v222 大厂架构】一键重置 WBP_DailyUpgradeRewardPage 的全部活动进度
	 *
	 * 大厂原则 (thin wrapper):
	 *   - ViewModel 不做任何"清空/写盘"操作, 仅校验 Subsystem 非空 + 委托 Subsystem
	 *   - 真正"清 AllRecords + 落盘 + 重建 day1 + Broadcast" 全在 Subsystem 一处
	 *   - 与 ModifyCurrentExperience 走完全相同的"ViewModel 委托 → Subsystem 实施"路径
	 *
	 * 大厂原则 (职责分工):
	 *   - Page 不直接调 Subsystem, 必须经 ViewModel (与 Modify* 系列保持一致)
	 *   - 这样后续如果需要"二次确认"或"埋点统计", 只改 ViewModel 不改 Page
	 *
	 * 零兜底:
	 *   - Subsystem 未 Bind → Log Error + return false
	 *   - Subsystem 内部失败 (磁盘落盘 / day1 Config 缺失) → Subsystem 已 Log Error, 此处仅透传 false
	 *
	 * @return true=Subsystem 报告重置成功; false=失败
	 */
	UFUNCTION(BlueprintCallable, Category = "ViewModel|DebugModification")
	bool ResetAllActivityProgress();

private:
	UPROPERTY()
	TWeakObjectPtr<UUpgradeActivitySubsystem> Subsystem;

	/**
	 * 【v214 大厂架构】SaveModifier 实例 (Outer=this)
	 * 大厂原则 (生命周期集中管理):
	 *   - 不允许 Page 自行 NewObject, 必须经此 ViewModel 实例化
	 *   - Bind 时按需初始化, Unbind 时显式 Destroy
	 *   - 避免重复 NewObject 浪费内存
	 */
	UPROPERTY()
	TObjectPtr<UUpgradeActivitySaveModifier> CachedSaveModifier = nullptr;

	/**
	 * 【v214 新增】顺序创建 SelectedDay 及之前的中间记录
	 *
	 * 业务规则 (用户 2026.08.11):
	 *   当 SelectedDay > MaxRecordDate, 从 MaxRecordDate+1 到 SelectedDay 依次创建所有中间记录
	 *   (不继承前一天, 业务期望是"解锁"而不是"补做丢失内容")
	 *
	 * 多次创建时使用 bAutoSave=false, 由调用方的最后一次 Modify 触发 OnGlobalRefresh
	 *
	 * @param SelectedDay   目标绝对天 (1-5, 由 ModifyCurrentExperience/ModifyAllTaskCounts 入参校验)
	 * @return true=确保 SelectedDay 记录已存在 (不论是通过已存在 or 新建); false=创建失败
	 */
	bool EnsureRecordChainReady(int32 SelectedDay);
};
