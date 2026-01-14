// 5

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "DailyLoginTrack.generated.h"

class UDailyLoginDayItem;

UCLASS()
class METALSLUG01_API UDailyLoginTrack : public UObject
{
	GENERATED_BODY()

public:
	void Initialize(int32 InTotalDays);

	bool TryClaimDay(int32 DayIndex);

	int32 GetLoginDays() const { return LoginDays; }

	const TArray<UDailyLoginDayItem*>& GetDayItems() const { return DayItems; }

private:
	void RefreshClaimable();

private:
	UPROPERTY()
	TArray<UDailyLoginDayItem*> DayItems;

	UPROPERTY()
	int32 LoginDays = 0;
};
