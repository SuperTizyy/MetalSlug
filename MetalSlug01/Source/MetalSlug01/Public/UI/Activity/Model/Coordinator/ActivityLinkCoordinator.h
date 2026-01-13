//12
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "ActivityLinkCoordinator.generated.h"

class UDailyLoginTrack;
class UTreasureTrack;

/**
 * 活动联动协调器
 * 职责：
 * 1. 监听 DailyLoginTrack 进度变化
 * 2. 根据规则解锁 TreasureTrack 宝箱
 * 3. 不参与 UI、不存储数据
 */
UCLASS(BlueprintType)
class METALSLUG01_API UActivityLinkCoordinator : public UObject
{
	GENERATED_BODY()

public:
	// 初始化联动
	void Initialize(
		UDailyLoginTrack* InDailyLoginTrack,
		UTreasureTrack* InTreasureTrack
	);

	// 当登录天数变化时调用
	void OnLoginDayChanged(int32 NewLoginDay);

private:
	// 登录 Track（不拥有）
	UPROPERTY()
	UDailyLoginTrack* DailyLoginTrack = nullptr;

	// 宝箱 Track（不拥有）
	UPROPERTY()
	UTreasureTrack* TreasureTrack = nullptr;
};
