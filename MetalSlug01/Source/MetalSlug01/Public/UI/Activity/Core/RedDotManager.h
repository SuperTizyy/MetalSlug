// 版权声明：在项目设置的描述页面填写您的版权信息。

#pragma once

// ==========================================
// 头文件包含说明
// ==========================================
#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "UI/Activity/Data/DailyLoginConfig.h"  // 包含整合后的 FRedDotData
#include "RedDotManager.generated.h"

class UActivitySubsystem;

// 注意: FRedDotData 结构体现在已在 DailyLoginConfig.h 中统一管理


/**
 * @class URedDotManager
 * @brief 红点管理器
 *
 * 职责说明:
 * - 负责计算和管理所有活动项的红点状态
 * - 提供统一的查询/手动设置接口
 * - 缓存上次刷新时间, 避免重复计算
 *
 * 架构理念:
 * 1. 单例角色: 由 UActivitySubsystem 持有, 生命周期跟随
 * 2. 缓存机制: RedDotCache (TMap) + LastRefreshTime
 * 3. 策略模式: 优先执行条件函数, 失败 fallback 到默认逻辑
 * 4. 优先级: 手动设置（高优先级 = 100） > 条件函数 > 默认
 */
UCLASS(BlueprintType)
class METALSLUG01_API URedDotManager : public UObject
{
	GENERATED_BODY()

public:
	// ==========================================
	// 1. 初始化
	// ==========================================

	/**
	 * 初始化红点管理器
	 * @param InSubsystem 活动子系统引用
	 */
	UFUNCTION(BlueprintCallable, Category = "RedDot")
	void InitializeManager(UActivitySubsystem* InSubsystem);

	// ==========================================
	// 2. 公共接口
	// ==========================================

	/**
	 * 刷新所有红点状态
	 * 流程: 获取所有导航项 -> 逐个计算 -> 写入缓存
	 */
	UFUNCTION(BlueprintCallable, Category = "RedDot")
	void RefreshAllRedDots();

	/**
	 * 获取指定活动的红点数据
	 * @param ActivityId 活动标识符
	 * @return 红点数据（无数据时返回默认 None 类型）
	 */
	UFUNCTION(BlueprintPure, Category = "RedDot")
	FRedDotData GetRedDotData(int32 ActivityId) const;

	/**
	 * 检查是否有红点需要显示（任一 bShouldShow = true）
	 */
	UFUNCTION(BlueprintPure, Category = "RedDot")
	bool HasAnyRedDots() const;

	/**
	 * 获取红点总数（用于汇总显示, 仅 NumberBadge/ProgressBadge）
	 */
	UFUNCTION(BlueprintPure, Category = "RedDot")
	int32 GetTotalRedDotCount() const;

	/**
	 * 手动设置红点状态（用于测试或特殊逻辑）
	 * @param ActivityId 活动标识符
	 * @param bShow 是否显示
	 * @param Value 红点数值
	 * 注意: 手动设置优先级 = 100（覆盖条件函数）
	 */
	UFUNCTION(BlueprintCallable, Category = "RedDot")
	void SetRedDotManually(int32 ActivityId, bool bShow, int32 Value = 1);

protected:
	// ==========================================
	// 3. 成员
	// ==========================================

	/**
	 * 活动子系统引用（弱引用, 防止循环）
	 */
	UPROPERTY()
	TWeakObjectPtr<UActivitySubsystem> ActivitySubsystem;

	/** 红点数据缓存 (Key: ActivityId, Value: FRedDotData) */
	UPROPERTY()
	TMap<int32, FRedDotData> RedDotCache;

	/** 上次刷新时间（秒, FPlatformTime::Seconds） */
	UPROPERTY()
	double LastRefreshTime = 0.0;

private:
	// ==========================================
	// 4. 内部计算
	// ==========================================

	/**
	 * 计算单个活动的红点状态
	 * 1. 优先尝试 RedDotConditionFunction
	 * 2. fallback 到 CalculateDefaultRedDot
	 */
	FRedDotData CalculateRedDotForActivity(const FActivityInfoRow* Config);

	/**
	 * 执行红点条件函数（反射调用）
	 * 用途: 支持自定义红点逻辑
	 * 已知: CheckDailyLoginRedDot / CheckFirstMatchRedDot
	 */
	bool ExecuteRedDotCondition(const FName& FunctionName, int32& OutValue);

	/**
	 * 默认的红点计算逻辑
	 * 规则: StaticRedDotValue > 0 时显示
	 */
	FRedDotData CalculateDefaultRedDot(const FActivityInfoRow* Config);
};
