//8
#pragma once

#include "CoreMinimal.h"
#include "TreasureBoxTypes.generated.h"  // 修改：使用正确的生成文件名

/**
 * 宝箱状态
 * 职责：
 * 1. 描述宝箱在业务流程中的阶段
 * 2. 被 UI / Track / Model 共同使用
 */
UENUM(BlueprintType)
enum class ETreasureBoxState : uint8
{
	Locked UMETA(DisplayName = "Locked"),        // 未解锁（登录天数不足）
	Available UMETA(DisplayName = "Available"),  // 已解锁，可选择奖励
	Selected UMETA(DisplayName = "Selected"),    // 已选奖励，未确认
	Claimed UMETA(DisplayName = "Claimed")       // 已领取，流程结束
};