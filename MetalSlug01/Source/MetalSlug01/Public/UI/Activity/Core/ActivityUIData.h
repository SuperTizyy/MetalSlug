// ==========================================
// 活动 UI 展示数据结构
// 关联: UI/Activity/Core/ (导航菜单、UI 显示)
// ==========================================
#pragma once

#include "CoreMinimal.h"
#include "Data/Tables/ItemTableRow.h"
#include "ActivityUIData.generated.h"

/**
 * @struct FActivityNavItem
 * @brief UI 导航项显示数据结构
 * @details 用于存储导航菜单项的 UI 显示状态信息
 * @note 通过引用 FActivityInfoRow 避免 DisplayName 等字段的冗余存储
 */
USTRUCT(BlueprintType)
struct FActivityNavItem
{
	GENERATED_BODY()

public:
	/** 关联的活动标识符 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Navigation")
	FName ActivityId;

	/** 是否当前选中状态 */
	UPROPERTY(BlueprintReadOnly, Category = "Navigation")
	bool bIsSelected = false;

	/** 导航项图标资源 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Navigation")
	TSoftObjectPtr<UTexture2D> IconTexture;

	/** 关联的活动 ID（通过 ID 关联避免数据复制冗余） */
	UPROPERTY(BlueprintReadOnly, Category = "RuntimeReference")
	FName LinkedActivityId;

	/** 缓存的活动信息指针（运行时使用） */
	UPROPERTY(Transient)
	TWeakObjectPtr<UObject> CachedActivityInfo;

	/** 获取显示名称 */
	FORCEINLINE FText GetDisplayName() const { return FText::GetEmpty(); }

	/** 获取导航 ID */
	FORCEINLINE FName GetNavId() const { return NAME_None; }

	FActivityNavItem() = default;
};
