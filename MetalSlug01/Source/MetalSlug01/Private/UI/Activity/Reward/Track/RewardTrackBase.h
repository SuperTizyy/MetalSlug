#pragma once

#include "CoreMinimal.h"
#include "UI/Activity/Reward/Item/RewardItemBase.h"
#include "RewardTrackBase.generated.h"


UCLASS(Abstract)
class METALSLUG01_API URewardTrackBase : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadOnly)
	TArray<URewardItemBase*> Items;

	virtual void RefreshState() PURE_VIRTUAL(URewardTrackBase::RefreshState, );
};
