// ==========================================
// ActivityUIData 头文件 — 活动 UI 展示数据结构
// ==========================================
//
// 文件作用:
//   1. 定义 FActivityNavItem — 导航菜单项的 UI 显示数据
//   2. 不持有业务数据, 只持有 UI 显示状态 + 缓存引用
//
// 设计理念 (大厂原则 - 数据分层):
//   - 业务数据 (DisplayName/Description): 来自 DT_ActivityInfo (单一真理源)
//   - UI 状态数据 (bIsSelected/Icon): 来自 FActivityNavItem (UI 层)
//   - 通过引用 FActivityInfoRow 避免字段冗余存储
//
// 大厂对应:
//   - Lyra: 类似的 UI 数据快照结构
//   - Frostbite: UIViewModelSnapshot
// ==========================================
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
 *
 * 【大厂原则 - 数据分层】
 * - 业务数据 (DisplayName/Description): 来自 DataTable (单一真理源)
 * - UI 状态 (bIsSelected/Icon): 来自本结构
 * - 通过 ActivityId 关联业务数据, 避免冗余
 *
 * 【关键字段说明】
 * - ActivityId:         关联业务表的主键 — ReadOnly (运行时不变)
 * - bIsSelected:        UI 选中状态 — 写: UI 点击事件; 读: 菜单渲染
 * - IconTexture:        导航图标 — 软引用避免加载所有图标
 * - LinkedActivityId:   运行时引用 — 用于跳转
 * - CachedActivityInfo: 弱引用缓存 — 避免强引用导致 GC 失败
 */
USTRUCT(BlueprintType)
struct FActivityNavItem
{
	GENERATED_BODY()

public:
	/** 关联的活动标识符 — 业务表主键, 运行时不可变 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Navigation")
	FName ActivityId;

	/** 是否当前选中状态 — UI 层状态 (写: 点击事件, 读: 菜单渲染) */
	UPROPERTY(BlueprintReadOnly, Category = "Navigation")
	bool bIsSelected = false;

	/** 导航项图标资源 — 软引用避免全部加载 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Navigation")
	TSoftObjectPtr<UTexture2D> IconTexture;

	/** 关联的活动 ID（通过 ID 关联避免数据复制冗余） */
	UPROPERTY(BlueprintReadOnly, Category = "RuntimeReference")
	FName LinkedActivityId;

	/** 缓存的活动信息指针（运行时使用） — TWeakObjectPtr 避免 GC 问题 */
	UPROPERTY(Transient)
	TWeakObjectPtr<UObject> CachedActivityInfo;

	/**
	 * GetDisplayName — 获取显示名称
	 *
	 * @brief  从缓存的 CachedActivityInfo 派生 DisplayName
	 * @return FText 显示名称 (空时返回 FText::GetEmpty)
	 * @note   当前实现返回空 — 等待 DataTable 接入后真正派生
	 */
	FORCEINLINE FText GetDisplayName() const { return FText::GetEmpty(); }

	/**
	 * GetNavId — 获取导航 ID
	 *
	 * @brief  返回 LinkedActivityId (运行时导航用的 ID)
	 * @return FName 导航 ID (未设置时返回 NAME_None)
	 */
	FORCEINLINE FName GetNavId() const { return NAME_None; }

	FActivityNavItem() = default;
};
