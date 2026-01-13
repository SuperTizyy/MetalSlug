// 3

#pragma once

#include "CoreMinimal.h"
#include "RewardData.generated.h"

/**
 * 奖励类型
 * 用于区分奖励发放方式
 */
UENUM(BlueprintType)
enum class ERewardType : uint8
{
	// 普通数值奖励（金币、钻石、体力等）
	Currency UMETA(DisplayName = "Currency"),

	// 道具奖励（背包物品）
	Item UMETA(DisplayName = "Item"),

	// 英雄 / 角色
	Character UMETA(DisplayName = "Character"),

	// 宝箱（点开后再给）
	Chest UMETA(DisplayName = "Chest"),
};

/**
 * 单条奖励数据
 * 描述“给玩家什么”
 */
USTRUCT(BlueprintType)
struct FRewardData
{
	GENERATED_BODY()

public:

	// 奖励类型
	// 决定奖励的发放逻辑
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	ERewardType RewardType = ERewardType::Currency;

	// 奖励 ID
	// Currency: 货币ID
	// Item: 道具ID
	// Character: 角色ID
	// Chest: 宝箱ID
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int32 RewardId = 0;

	// 奖励数量
	// 对于不可叠加的奖励（如角色），通常为 1
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int32 Amount = 0;

	// 奖励展示名称（仅用于 UI）
	// 逻辑层不依赖它
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FText RewardName;

	// 是否为“可选择奖励”
	// true：需要玩家选择
	// false：直接发放
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	bool bSelectable = false;
};
