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

private:
	UPROPERTY()
	TWeakObjectPtr<UUpgradeActivitySubsystem> Subsystem;
};
