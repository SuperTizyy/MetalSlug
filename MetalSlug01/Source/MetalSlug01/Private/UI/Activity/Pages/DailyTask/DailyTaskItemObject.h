#pragma once

#include "CoreMinimal.h"
#include "UObject/Object.h"
#include "UI/Activity/Pages/DailyTask/DailyTaskData.h"
#include "DailyTaskItemObject.generated.h"

/**
 * ListView 使用的任务对象
 * 用于在列表视图中显示每日任务数据
 */
UCLASS(BlueprintType)
class METALSLUG01_API UDailyTaskItemObject : public UObject
{
	GENERATED_BODY()

public:

	// 任务数据 - 存储每日任务的具体信息
	UPROPERTY(BlueprintReadOnly)
	FDailyTaskData TaskData;
};

