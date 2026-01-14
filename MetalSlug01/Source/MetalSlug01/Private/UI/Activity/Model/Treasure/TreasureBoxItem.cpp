// 9

#include "UI/Activity/Model/Treasure/TreasureBoxItem.h"
#include "UI/Activity/Track/Treasure/TreasureTrack.h"

void UTreasureBoxItem::Init(int32 InBoxIndex, UTreasureTrack* InOwnerTrack)
{
	BoxIndex   = InBoxIndex;
	OwnerTrack = InOwnerTrack;
}

void UTreasureBoxItem::Unlock()
{
	if (bUnlocked)
	{
		return;
	}

	bUnlocked = true;
}

void UTreasureBoxItem::RequestReceive()
{
	if (!bUnlocked || bReceived)
	{
		return;
	}

	// 真实项目这里会走 Track → 发奖励
	bReceived = true;

	if (OwnerTrack)
	{
		OwnerTrack->OnBoxReceived(this);
	}
}
