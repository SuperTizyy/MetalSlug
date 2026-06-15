// ==========================================
// 活动信息表行
// 关联: DT_ActivityInfo / DT_NavigationItems
// ==========================================
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Data/Enums/ActivityEnums.h"
#include "ActivityTableRow.generated.h"

/**
 * @struct FActivityInfoRow
 * @brief 活动信息数据表行
 * 用途: 定义活动级别的全局属性配置，直接包含所有必要字段
 * 注意: 移除了冗余的嵌套结构，职责更加单一明确
 */
USTRUCT(BlueprintType)
struct FActivityInfoRow : public FTableRowBase
{
	GENERATED_BODY()

public:
	// ==================== 基础活动信息 ====================
	
	/** 活动唯一标识符 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Activity|Meta")
	int32 ActivityID;

	/** 活动显示标题 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Activity|UI")
	FString ActivityTitle;
	
	/** 活动类型 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Activity|Meta")
	EActivityType ActivityType = EActivityType::NormalActivity;

	/** 活动总天数（用于 UI 布局计算） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Activity|Logic")
	int32 TotalDays;

	/** 活动背景图片资源 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Activity|UI")
	TSoftObjectPtr<UTexture2D> BackgroundTexture;

	// ==================== 导航配置 ====================
	
	/** 导航项唯一标识符 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Navigation")
	FName NavId;

	/** 导航项显示名称 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Navigation")
	FText DisplayName;

	/** 导航项图标资源 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Navigation")
	TSoftObjectPtr<UTexture2D> IconTexture;

	/** 是否默认选中 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Navigation")
	bool bIsDefaultSelected = false;

	/** 导航项排序权重 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Navigation")
	int32 SortOrder = 0;

	// ==================== 红点配置 ====================
	
	/** 红点显示类型 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RedDot")
	ERedDotType RedDotType = ERedDotType::None;

	/** 红点条件计算函数名称 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RedDot")
	FName RedDotConditionFunction;

	/** 静态红点数值 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RedDot")
	int32 StaticRedDotValue = 0;

	/** 红点显示优先级 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "RedDot")
	int32 RedDotPriority = 0;

	// ==================== 时间控制配置 ====================
	
	/** 时间控制模式 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TimeControl")
	ETimeControlType TimeControlType = ETimeControlType::Permanent;

	/** 活动开始时间 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TimeControl")
	FDateTime StartTime;

	/** 活动结束时间 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TimeControl")
	FDateTime EndTime;

	/** 开始前提醒时间 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TimeControl")
	FTimespan PreNoticeTime;

	/** 结束前提醒时间 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TimeControl")
	FTimespan EndWarningTime;

	/** 循环周期时长 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TimeControl")
	FTimespan CycleDuration;

	/** 手动开关状态 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "TimeControl")
	bool bManualEnabled = true;

	// ==================== 页面路由配置 ====================
	
	/** 目标页面 Widget 类 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Routing")
	TSubclassOf<class UUserWidget> TargetPageClass;

	/** 页面标题 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Routing")
	FText PageTitle;

	/** 页面描述信息 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Routing")
	FText PageDescription;

	// ==================== 运行时状态引用 ====================
	
	/** 运行时状态数据引用 ID（指向 DailyLoginSave 中的数据） */
	UPROPERTY(BlueprintReadOnly, Category = "Runtime")
	FName RuntimeStateId;

	/** 缓存的运行时状态指针（运行时使用） */
	UPROPERTY(Transient)
	TWeakObjectPtr<UObject> CachedRuntimeState;

	// ==================== 快捷访问方法 ====================
	
	/** 获取活动 ID */
	FORCEINLINE int32 GetActivityID() const { return ActivityID; }

	/** 获取导航 ID */
	FORCEINLINE FName GetNavId() const { return NavId; }

	/** 获取显示名称 */
	FORCEINLINE FText GetDisplayName() const { return DisplayName; }

	/** 获取当前状态 */
	FORCEINLINE EActivityStatus GetCurrentStatus() const { return EActivityStatus::Active; }

	/** 默认构造函数 */
	FActivityInfoRow() 
		: ActivityID(101)
		, ActivityTitle(TEXT("八日登录大礼"))
		, ActivityType(EActivityType::NormalActivity)
		, TotalDays(8)
		, bIsDefaultSelected(false)
		, SortOrder(0)
		, RedDotType(ERedDotType::None)
		, StaticRedDotValue(0)
		, RedDotPriority(0)
		, TimeControlType(ETimeControlType::Permanent)
		, bManualEnabled(true)
	{};
};
