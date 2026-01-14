// 9

#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "TreasureBoxItem.generated.h"

class UTreasureTrack;

/**
 * 宝箱数据 Model
 * 纯状态 + 行为，不涉及 UI
 */
UCLASS(BlueprintType)
class METALSLUG01_API UTreasureBoxItem : public UObject
{
	GENERATED_BODY()

public:
	/** 初始化（由 Track 创建时调用） */
	void Init(int32 InBoxIndex, UTreasureTrack* InOwnerTrack);

	/** 请求领取宝箱 */
	void RequestReceive();

	/** 解锁宝箱（由 Track 或联动系统调用） */
	void Unlock();

	// ======== 状态查询（给 UI 用） ========

	bool IsUnlocked() const { return bUnlocked; }
	bool IsReceived() const { return bReceived; }
	int32 GetBoxIndex() const { return BoxIndex; }

private:
	/** 所属 Track */
	UPROPERTY()
	UTreasureTrack* OwnerTrack = nullptr;

	/** 宝箱索引 */
	UPROPERTY()
	int32 BoxIndex = 0;

	/** 是否已解锁 */
	UPROPERTY()
	bool bUnlocked = false;

	/** 是否已领取 */
	UPROPERTY()
	bool bReceived = false;
};
