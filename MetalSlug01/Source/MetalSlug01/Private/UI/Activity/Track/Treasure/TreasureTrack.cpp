// 10


#include "UI/Activity/Track/Treasure/TreasureTrack.h"
#include "UI/Activity/Model/Treasure/TreasureBoxItem.h"

void UTreasureTrack::Init()
{
	// 示例：创建 3 个宝箱
	const int32 BoxCount = 3;

	TreasureBoxes.Empty();

	for (int32 i = 0; i < BoxCount; ++i)
	{
		UTreasureBoxItem* BoxItem = NewObject<UTreasureBoxItem>(this);
		BoxItem->Init(i, this);
		TreasureBoxes.Add(BoxItem);
	}
}

void UTreasureTrack::UnlockBox(int32 BoxIndex)
{
	if (!TreasureBoxes.IsValidIndex(BoxIndex))
	{
		return;
	}

	UTreasureBoxItem* BoxItem = TreasureBoxes[BoxIndex];
	if (BoxItem)
	{
		BoxItem->Unlock();
	}
}

void UTreasureTrack::OnBoxReceived(UTreasureBoxItem* BoxItem)
{
	if (!BoxItem)
	{
		return;
	}

	// ⚠️ 目前先什么都不做
	// 后续这里会：
	// 1. 发奖励
	// 2. 存档
	// 3. 通知联动系统 / UI
}
