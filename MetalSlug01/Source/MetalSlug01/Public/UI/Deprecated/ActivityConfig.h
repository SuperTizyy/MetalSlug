/*
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "ActivityConfig.generated.h"

/**
 * 活动配置结构体 - 定义活动项目的基本信息
 * 用于存储活动页面的ID、显示名称、Widget类和排序信息
 #1#
USTRUCT(BlueprintType)
struct FActivityConfig : public FTableRowBase
{
	GENERATED_BODY()

public:
	// 页面唯一 ID - 用于标识不同活动页面的唯一标识符
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FName PageId;

	// 左侧显示名称 - 在菜单中显示的活动名称
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FText DisplayName;

	// 页面 Widget - 对应活动页面的Widget类
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TSubclassOf<UUserWidget> PageWidgetClass;

	// 排序 - 用于确定菜单项的显示顺序
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int32 SortOrder = 0;
};
*/
