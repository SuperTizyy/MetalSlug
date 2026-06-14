// 版权声明：在项目设置的描述页面填写您的版权信息。

/*
这个系统原本设计用于:
- 活动自动上下架 - 根据时间自动控制活动显示/隐藏
- 倒计时显示 - 在 UI 中显示活动开始/结束倒计时
- 状态提示 - 预告即将开始或即将结束的活动
- 红点系统 - 结合时间状态显示活动红点
- 运营维护 - 手动控制活动状态进行紧急维护
-------并未使用!!!!
*/

#pragma once

// ==========================================
// 头文件包含说明
// ==========================================
#include "CoreMinimal.h"
#include "UObject/NoExportTypes.h"
#include "UI/Activity/Data/DailyLoginConfig.h"
#include "UI/Activity/Data/DailyLoginSave.h"  // 包含 FActivityRuntimeState 定义
#include "ActivityTimeManager.generated.h"

class UActivitySubsystem;

// 注意: 时间状态信息现已整合到 FActivityInfoRow 结构中
// 包含: CurrentStatus、TimeUntilStart、TimeUntilEnd 等运行时字段


/**
 * @class UActivityTimeManager
 * @brief 活动时间管理器
 *
 * 职责说明:
 * - 负责管理所有活动的时间状态和生命周期
 * - 倒计时计算（活动开始/结束）
 * - 自动上下架决策
 * - 状态机: Upcoming/Active/EndingSoon/Ended/Maintenance
 *
 * 架构理念:
 * 1. 策略模式: FixedPeriod / Recurring / Manual / Permanent 四种策略
 * 2. 缓存机制: TimeInfoCache (TMap) + LastRefreshTime
 * 3. 提醒期: PreNoticeTime / EndWarningTime 控制 UI 提前提示
 * 4. 时间感知: GetServerTime 抽象, 未来可对接真实服务器
 *
 * 注意: 该系统当前**未被使用**（见 .cpp 顶部注释）
 */
UCLASS(BlueprintType)
class METALSLUG01_API UActivityTimeManager : public UObject
{
	GENERATED_BODY()

public:
	// ==========================================
	// 1. 初始化
	// ==========================================

	/**
	 * 初始化时间管理器
	 * @param InSubsystem 活动子系统引用
	 */
	UFUNCTION(BlueprintCallable, Category = "TimeManager")
	void InitializeManager(UActivitySubsystem* InSubsystem);

	// ==========================================
	// 2. 公共接口
	// ==========================================

	/** 刷新所有活动的时间状态 */
	UFUNCTION(BlueprintCallable, Category = "TimeManager")
	void RefreshAllActivityTimes();

	/**
	 * 获取指定活动的时间信息
	 * @param ActivityId 活动标识符
	 * @return 时间信息（直接返回 FActivityRuntimeState 中的运行时字段）
	 */
	UFUNCTION(BlueprintPure, Category = "TimeManager")
	FActivityRuntimeState GetActivityTimeInfo(int32 ActivityId) const;

	/**
	 * 检查活动是否可用（进行中或即将开始且在预告期）
	 */
	UFUNCTION(BlueprintPure, Category = "TimeManager")
	bool IsActivityAvailable(int32 ActivityId) const;

	/** 获取所有可用的活动 ID 列表 */
	UFUNCTION(BlueprintPure, Category = "TimeManager")
	TArray<int32> GetAvailableActivities() const;

	/**
	 * 手动设置活动状态（用于测试或紧急维护）
	 * @param ActivityId 活动标识符
	 * @param Status 新状态
	 */
	UFUNCTION(BlueprintCallable, Category = "TimeManager")
	void SetActivityStatusManually(int32 ActivityId, EActivityStatus Status);

	/**
	 * 获取服务器时间（可用于同步）
	 * @return 当前服务器时间（当前实现是本地时间）
	 */
	UFUNCTION(BlueprintPure, Category = "TimeManager")
	FDateTime GetServerTime() const;

protected:
	// ==========================================
	// 3. 成员
	// ==========================================

	/** 活动子系统引用（弱引用, 防止循环） */
	UPROPERTY()
	TWeakObjectPtr<UActivitySubsystem> ActivitySubsystem;

	/** 时间信息缓存 (Key: ActivityId, Value: FActivityRuntimeState) */
	UPROPERTY()
	TMap<int32, FActivityRuntimeState> TimeInfoCache;

	/** 上次刷新时间（秒） */
	UPROPERTY()
	double LastRefreshTime = 0.0;

	/** 时间刷新间隔（秒, 默认 60s） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TimeManager")
	float RefreshInterval = 60.0f;

private:
	// ==========================================
	// 4. 内部计算
	// ==========================================

	/**
	 * 计算单个活动的时间状态
	 * 1. 调用 CalculateStatusByTimeControl
	 * 2. 计算 SecondsUntilStart / SecondsUntilEnd
	 * 3. 判定 PreNotice / EndWarning
	 * 4. 计算 CycleIndex（Recurring）
	 */
	FActivityRuntimeState CalculateTimeInfoForActivity(const FActivityInfoRow* Config);

	/**
	 * 根据时间控制类型计算状态
	 * FixedPeriod / Recurring / Manual / Permanent
	 */
	EActivityStatus CalculateStatusByTimeControl(const FActivityInfoRow* Config, const FDateTime& CurrentTime);

	/**
	 * 计算固定周期活动状态
	 * 规则: <Start = Upcoming / >End = Ended / End-Now<EndWarningTime = EndingSoon / 否则 Active
	 */
	EActivityStatus CalculateFixedPeriodStatus(const FActivityInfoRow* Config, const FDateTime& CurrentTime);

	/**
	 * 计算循环活动状态
	 * 规则: 每个周期前 80% = Active, 后 20% = EndingSoon
	 */
	EActivityStatus CalculateRecurringStatus(const FActivityInfoRow* Config, const FDateTime& CurrentTime);

	/**
	 * 计算手动控制活动状态
	 * 规则: bManualEnabled = Active / 否则 Maintenance
	 */
	EActivityStatus CalculateManualStatus(const FActivityInfoRow* Config);
};
