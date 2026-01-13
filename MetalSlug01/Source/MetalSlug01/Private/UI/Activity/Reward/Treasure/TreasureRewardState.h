#pragma once

UENUM(BlueprintType)
enum class ETreasureRewardState : uint8
{
	Locked,         // 未到条件
	Claimable,      // 可打开宝箱
	Selecting,      // 已选择但未确认
	Confirmed       // 已确认（最终态）
};

