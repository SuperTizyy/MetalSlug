// ==========================================
// 地图信息表行
// 关联: DT_MapInfo
// 替代: 原 StaticTable.h 中的 FMapInfoRow 段
// ==========================================
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "MapTableRow.generated.h"

class UTexture2D;

/**
 * @struct FMapInfoRow
 * @brief 地图信息数据表行
 * 用途: 关联 DT_MapInfo, 让创房面板的地图下拉框数据驱动
 */
USTRUCT(BlueprintType)
struct FMapInfoRow : public FTableRowBase
{
	GENERATED_BODY()

	/** 真实关卡名 (必须和 .umap 文件名一致, 用于 OpenLevel) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Map")
	FName LevelName;

	/** UI 展示名 (支持多语言) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Map")
	FText DisplayName;

	/** 地图缩略图 (TSoftObjectPtr 避免一次性加载所有缩略图) */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Map")
	TSoftObjectPtr<UTexture2D> MapThumbnail;

	/** 地图描述/最大支持人数等可选信息 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Map")
	FText MapDescription;
};
