// 5

#pragma once

#include "CoreMinimal.h"
#include "UI/Activity/Model/ActivityTrackBase.h"
#include "DailyLoginTrack.generated.h"

// 前向声明，避免头文件依赖膨胀
class UDailyLoginDayItem;

/**
 * 每日登录活动 Track
 *
 * 职责：
 * 1. 维护当前连续登录天数（来自服务器）
 * 2. 管理 Day1 ~ DayN 的奖励 Item
 * 3. 根据规则刷新奖励状态
 *
 * ⚠️ 注意：
 * - 不直接发奖励
 * - 不处理 UI
 * - 只裁决“状态”
 */
UCLASS(BlueprintType)
class METALSLUG01_API UDailyLoginTrack : public UActivityTrackBase
{
	GENERATED_BODY()

public:
	/**
	 * 当前连续登录天数
	 * 由服务器同步，Track 不自行计算
	 */
	UPROPERTY(BlueprintReadOnly)
	int32 CurrentLoginDay = 0;

	/**
	 * 是否要求连续登录
	 * 后期支持“断签补领”时会用到
	 */
	UPROPERTY(BlueprintReadOnly)
	bool bRequireContinuousLogin = true;

	/**
	 * 所有登录奖励 Item（Day1 ~ DayN）
	 */
	UPROPERTY(BlueprintReadOnly)
	TArray<UDailyLoginDayItem*> DayItems;

public:
	// Track 初始化（创建 DayItem 结构）
	virtual void InitializeTrack() override;

	// 根据当前登录天数刷新奖励状态
	virtual void RefreshTrack() override;

public:
	/**
	 * 尝试领取指定天数奖励
	 * @return 是否领取成功
	 */
	bool TryClaimDay(int32 DayIndex);

	/**
	 * 获取指定天数的奖励 Item
	 */
	UDailyLoginDayItem* GetDayItem(int32 DayIndex) const;
	
	/**
	 * 登录天数变化事件
	 */
	DECLARE_MULTICAST_DELEGATE_OneParam(FOnLoginDayChanged, int32);
	FOnLoginDayChanged OnLoginDayChanged;


};
