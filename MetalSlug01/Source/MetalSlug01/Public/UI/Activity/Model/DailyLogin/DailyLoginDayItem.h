// 4

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "DailyLoginDayItem.generated.h"

/**
 * 每日登录单日数据（Model）
 * ⚠ 仅 Track 可修改状态
 */
UCLASS()
class METALSLUG01_API UDailyLoginDayItem : public UObject
{
	GENERATED_BODY()

public:
	// ===== 只读查询 =====

	int32 GetDayIndex() const { return DayIndex; }

	bool CanClaim() const { return bCanClaim; }

	bool IsClaimed() const { return bClaimed; }

	// ===== 仅 Track 调用 =====

	void Init(int32 InDayIndex);

	void UpdateClaimable(bool bInCanClaim);

	bool TryClaim();

private:
	UPROPERTY()
	int32 DayIndex = 0;

	UPROPERTY()
	bool bCanClaim = false;

	UPROPERTY()
	bool bClaimed = false;
};
