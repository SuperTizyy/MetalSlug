#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "UI/Activity/Data/DailyLoginConfig.h"
#include "UI/Activity/Data/DailyLoginSave.h"  // 包含FActivityRuntimeState定义
#include "ActivityTimeManager.generated.h"

class UActivitySubsystem;

// 注意：时间状态信息现已整合到FActivityInfoRow结构中
// 包含：CurrentStatus、TimeUntilStart、TimeUntilEnd等运行时字段

/**
 * @brief 活动时间管理器
 * 负责管理所有活动的时间状态和生命周期
 */
UCLASS(BlueprintType)
class METALSLUG01_API UActivityTimeManager : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * @brief 初始化时间管理器
	 * @param InSubsystem 活动子系统引用
	 */
	UFUNCTION(BlueprintCallable, Category = "TimeManager")
	void InitializeManager(UActivitySubsystem* InSubsystem);

	/**
	 * @brief 刷新所有活动的时间状态
	 */
	UFUNCTION(BlueprintCallable, Category = "TimeManager")
	void RefreshAllActivityTimes();

	/**
	 * @brief 获取指定活动的时间信息
	 * @param ActivityId 活动标识符
	 * @return 时间信息（直接返回FActivityRuntimeState中的运行时字段）
	 */
	UFUNCTION(BlueprintPure, Category = "TimeManager")
	FActivityRuntimeState GetActivityTimeInfo(int32 ActivityId) const;

	/**
	 * @brief 检查活动是否可用（进行中或即将开始）
	 * @param ActivityId 活动标识符
	 * @return 是否可用
	 */
	UFUNCTION(BlueprintPure, Category = "TimeManager")
	bool IsActivityAvailable(int32 ActivityId) const;

	/**
	 * @brief 获取所有可用的活动列表
	 * @return 可用活动ID列表
	 */
	UFUNCTION(BlueprintPure, Category = "TimeManager")
	TArray<int32> GetAvailableActivities() const;

	/**
	 * @brief 手动设置活动状态（用于测试或紧急维护）
	 * @param ActivityId 活动标识符
	 * @param Status 新状态
	 */
	UFUNCTION(BlueprintCallable, Category = "TimeManager")
	void SetActivityStatusManually(int32 ActivityId, EActivityStatus Status);

	/**
	 * @brief 获取服务器时间（可用于同步）
	 * @return 当前服务器时间
	 */
	UFUNCTION(BlueprintPure, Category = "TimeManager")
	FDateTime GetServerTime() const;

protected:
	/** 活动子系统引用 */
	UPROPERTY()
	TWeakObjectPtr<UActivitySubsystem> ActivitySubsystem;

	/** 时间信息缓存 */
	UPROPERTY()
	TMap<int32, FActivityRuntimeState> TimeInfoCache;

	/** 上次刷新时间 */
	UPROPERTY()
	double LastRefreshTime = 0.0;

	/** 时间刷新间隔（秒） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TimeManager")
	float RefreshInterval = 60.0f;

private:
	/**
	 * @brief 计算单个活动的时间状态
	 */
	FActivityRuntimeState CalculateTimeInfoForActivity(const FActivityInfoRow* Config);

	/**
	 * @brief 根据时间控制类型计算状态
	 */
	EActivityStatus CalculateStatusByTimeControl(const FActivityInfoRow* Config, const FDateTime& CurrentTime);

	/**
	 * @brief 计算固定周期活动状态
	 */
	EActivityStatus CalculateFixedPeriodStatus(const FActivityInfoRow* Config, const FDateTime& CurrentTime);

	/**
	 * @brief 计算循环活动状态
	 */
	EActivityStatus CalculateRecurringStatus(const FActivityInfoRow* Config, const FDateTime& CurrentTime);

	/**
	 * @brief 计算手动控制活动状态
	 */
	EActivityStatus CalculateManualStatus(const FActivityInfoRow* Config);
};