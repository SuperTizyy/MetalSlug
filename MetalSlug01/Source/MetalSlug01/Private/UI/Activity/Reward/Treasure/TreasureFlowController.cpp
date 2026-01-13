// Fill out your copyright notice in the Description page of Project Settings.


#include "UI/Activity/Reward/Treasure/TreasureFlowController.h"
#include "UI/Activity/Reward/Treasure/TreasureRewardItem.h"  // 包含TreasureRewardItem头文件

void UTreasureFlowController::OpenTreasure()
{
	if (!TreasureItem || !TreasureItem->CanOpen())
		return;

	// 打开宝箱 UI
}

void UTreasureFlowController::SelectReward(int32 Index)
{
	TreasureItem->Select(Index);
}

void UTreasureFlowController::ConfirmReward()
{
	TreasureItem->Confirm();
	// 👉 这里之后走 Server RPC
}

void UTreasureFlowController::CancelAndClose()
{
	TreasureItem->ResetSelection();
}