// 10
#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "TreasureTrack.generated.h"

class UTreasureBoxItem;

/**
 * Treasure Track
 * 职责：
 * 1. 管理宝箱 Item 生命周期
 * 2. 控制宝箱解锁
 * 3. 接收宝箱领取完成回调
 */
UCLASS()
class METALSLUG01_API UTreasureTrack : public UObject
{
	GENERATED_BODY()

public:
	/** 初始化宝箱数据 */
	void Init();

	/** 解锁指定宝箱 */
	void UnlockBox(int32 BoxIndex);

	/** 宝箱领取完成回调（由 Item 调用） */
	void OnBoxReceived(UTreasureBoxItem* BoxItem);

public:
	/** 所有宝箱 */
	UPROPERTY()
	TArray<UTreasureBoxItem*> TreasureBoxes;
};
