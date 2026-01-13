// 9


#include "UI/Activity/Treasure/Model/TreasureBoxItem.h"

void UTreasureBoxItem::InitializeBox(int32 InBoxIndex)
{
	// 设置宝箱索引
	BoxIndex = InBoxIndex;

	// 初始状态为锁定
	State = ETreasureBoxState::Locked;
}

void UTreasureBoxItem::Unlock()
{
	// 只有锁定状态才能解锁
	if (State == ETreasureBoxState::Locked)
	{
		State = ETreasureBoxState::Available;
	}
}

bool UTreasureBoxItem::SelectOption(UTreasureOptionItem* Option)
{
	// 只有可用或已选择状态才能重新选
	if (State != ETreasureBoxState::Available &&
		State != ETreasureBoxState::Selected)
	{
		return false;
	}

	// 清除之前的选中
	if (SelectedOption)
	{
		SelectedOption->SetSelected(false);
	}

	// 设置新选中
	SelectedOption = Option;
	SelectedOption->SetSelected(true);

	// 状态切换为已选择
	State = ETreasureBoxState::Selected;

	return true;
}

bool UTreasureBoxItem::Claim()
{
	// 只有已选择状态才能领取
	if (State != ETreasureBoxState::Selected || !SelectedOption)
	{
		return false;
	}

	// 状态锁死为已领取
	State = ETreasureBoxState::Claimed;

	return true;
}