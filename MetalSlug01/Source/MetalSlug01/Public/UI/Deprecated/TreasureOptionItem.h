/*
// 7

#pragma once

#include "CoreMinimal.h"
#include "UI/Activity/Treasure/Data/TreasureOptionData.h"
#include "UObject/Object.h"
#include "TreasureOptionItem.generated.h"

/**
 * 宝箱选项运行时 Item
 * 职责：
 * 1. 记录是否被选中
 * 2. 不做流程控制
 #1#
UCLASS(BlueprintType)
class METALSLUG01_API UTreasureOptionItem : public UObject
{
	GENERATED_BODY()

public:
	// 静态奖励数据
	UPROPERTY(BlueprintReadOnly)
	FTreasureOptionData Data;

	// 是否被当前选中
	UPROPERTY(BlueprintReadOnly)
	bool bSelected = false;

public:
	// 设置选中状态
	void SetSelected(bool bInSelected);
};
*/

