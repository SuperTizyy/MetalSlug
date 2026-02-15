#pragma once

#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "UI/Activity/Data/DailyLoginConfig.h"  // 包含整合后的FRedDotData
#include "RedDotManager.generated.h"

class UActivitySubsystem;

// 注意：FRedDotData结构体现在已在DailyLoginConfig.h中统一管理

/**
 * @brief 红点管理器
 * 负责计算和管理所有活动项的红点状态
 */
UCLASS(BlueprintType)
class METALSLUG01_API URedDotManager : public UObject
{
	GENERATED_BODY()

public:
	/**
	 * @brief 初始化红点管理器
	 * @param InSubsystem 活动子系统引用
	 */
	UFUNCTION(BlueprintCallable, Category = "RedDot")
	void InitializeManager(UActivitySubsystem* InSubsystem);

	/**
	 * @brief 刷新所有红点状态
	 */
	UFUNCTION(BlueprintCallable, Category = "RedDot")
	void RefreshAllRedDots();

	/**
	 * @brief 获取指定活动的红点数据
	 * @param ActivityId 活动标识符
	 * @return 红点数据
	 */
	UFUNCTION(BlueprintPure, Category = "RedDot")
	FRedDotData GetRedDotData(int32 ActivityId) const;

	/**
	 * @brief 检查是否有红点需要显示
	 * @return 是否有任何红点
	 */
	UFUNCTION(BlueprintPure, Category = "RedDot")
	bool HasAnyRedDots() const;

	/**
	 * @brief 获取红点总数（用于汇总显示）
	 * @return 所有数字红点的总和
	 */
	UFUNCTION(BlueprintPure, Category = "RedDot")
	int32 GetTotalRedDotCount() const;

	/**
	 * @brief 手动设置红点状态（用于测试或特殊逻辑）
	 * @param ActivityId 活动标识符
	 * @param bShow 是否显示
	 * @param Value 红点数值
	 */
	UFUNCTION(BlueprintCallable, Category = "RedDot")
	void SetRedDotManually(int32 ActivityId, bool bShow, int32 Value = 1);

protected:
	/** 活动子系统引用 */
	UPROPERTY()
	TWeakObjectPtr<UActivitySubsystem> ActivitySubsystem;

	/** 红点数据缓存 */
	UPROPERTY()
	TMap<int32, FRedDotData> RedDotCache;

	/** 上次刷新时间 */
	UPROPERTY()
	double LastRefreshTime = 0.0;

private:
	/**
	 * @brief 计算单个活动的红点状态
	 */
	FRedDotData CalculateRedDotForActivity(const FActivityInfoRow* Config);

	/**
	 * @brief 执行红点条件函数
	 */
	bool ExecuteRedDotCondition(const FName& FunctionName, int32& OutValue);

	/**
	 * @brief 默认的红点计算逻辑
	 */
	FRedDotData CalculateDefaultRedDot(const FActivityInfoRow* Config);
};