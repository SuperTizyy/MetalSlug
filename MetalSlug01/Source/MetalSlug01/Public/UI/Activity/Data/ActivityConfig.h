#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "ActivityConfig.generated.h"

USTRUCT(BlueprintType)
struct FActivityConfig : public FTableRowBase
{
	GENERATED_BODY()

public:
	// 页面唯一 ID
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FName PageId;

	// 左侧显示名称
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	FText DisplayName;

	// 页面 Widget
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	TSubclassOf<UUserWidget> PageWidgetClass;

	// 排序
	UPROPERTY(EditAnywhere, BlueprintReadOnly)
	int32 SortOrder = 0;
};
