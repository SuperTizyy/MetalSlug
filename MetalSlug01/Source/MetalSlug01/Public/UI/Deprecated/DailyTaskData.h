/*
#pragma once

#include "CoreMinimal.h"
#include "DailyTaskData.generated.h"

/**
 * 单条每日任务数据
 * 定义每日任务的基本信息和状态
 #1#
USTRUCT(BlueprintType)
struct FDailyTaskData
{
	GENERATED_BODY()

public:

	// 任务 ID（服务器用）- 用于在服务器端唯一标识任务
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int32 TaskId = 0;

	// 任务标题 - 显示给用户看的任务名称
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FText Title;

	// 当前进度 - 用户当前完成的数量
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int32 CurrentValue = 0;

	// 目标值 - 完成任务需要达到的数量
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int32 TargetValue = 0;

	// 是否已完成 - 标识任务是否已完成但奖励未领取
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	bool bCompleted = false;

	// 是否已领取奖励 - 标识任务奖励是否已被领取
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	bool bRewardClaimed = false;
};
*/
