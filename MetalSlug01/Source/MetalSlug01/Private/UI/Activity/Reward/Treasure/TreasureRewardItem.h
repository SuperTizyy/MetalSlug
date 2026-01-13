#pragma once
#include "CoreMinimal.h"
#include "TreasureRewardState.h"
#include "UI/Activity/Data/RewardData.h"
#include "TreasureRewardItem.generated.h"

UCLASS(BlueprintType)
class METALSLUG01_API UTreasureRewardItem : public UObject
{
	GENERATED_BODY()

public:
	// 宝箱唯一ID（服务器）
	UPROPERTY(BlueprintReadOnly)
	int32 TreasureId = 0;

	// 可选奖励
	UPROPERTY(BlueprintReadOnly)
	TArray<FRewardData> RewardOptions;

	// 当前选中
	UPROPERTY(BlueprintReadOnly)
	int32 SelectedIndex = INDEX_NONE;

	UPROPERTY(BlueprintReadOnly)
	ETreasureRewardState State = ETreasureRewardState::Locked;

public:
	bool CanOpen() const
	{
		return State == ETreasureRewardState::Claimable;
	}

	bool CanConfirm() const
	{
		return State == ETreasureRewardState::Selecting
			&& SelectedIndex != INDEX_NONE;
	}

	void Select(int32 Index)
	{
		if (State == ETreasureRewardState::Claimable ||
			State == ETreasureRewardState::Selecting)
		{
			SelectedIndex = Index;
			State = ETreasureRewardState::Selecting;
		}
	}

	void Confirm()
	{
		if (CanConfirm())
		{
			State = ETreasureRewardState::Confirmed;
		}
	}

	void ResetSelection()
	{
		if (State == ETreasureRewardState::Selecting)
		{
			SelectedIndex = INDEX_NONE;
			State = ETreasureRewardState::Claimable;
		}
	}
};

