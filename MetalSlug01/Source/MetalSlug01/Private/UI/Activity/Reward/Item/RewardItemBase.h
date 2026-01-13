#pragma once
#include "CoreMinimal.h"
#include "UI/Activity/Reward/Data/RewardProgressData.h"
#include "RewardItemBase.generated.h"

UENUM(BlueprintType)
enum class ERewardState : uint8
{
	Locked,
	Claimable,
	Claimed
};

UCLASS(Abstract, BlueprintType)
class METALSLUG01_API URewardItemBase : public UObject
{
	GENERATED_BODY()

public:
	UPROPERTY(BlueprintReadOnly)
	ERewardState State = ERewardState::Locked;

	UPROPERTY(BlueprintReadOnly)
	FRewardProgressData ProgressData;

	virtual bool CanClaim() const
	{
		return State == ERewardState::Claimable;
	}
};

