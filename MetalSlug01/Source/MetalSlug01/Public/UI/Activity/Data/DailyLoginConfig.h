/**
 * @file DailyLoginConfig.h
 * @brief 活动系统静态配置数据定义文件
 * @author AI Assistant
 * @date 2024
 * @version 1.0
 * 
 * @details 本文件包含活动系统的所有静态配置数据结构和枚举定义
 *          所有需要持久化存储的配置信息都定义在此文件中
 *          遵循"所有静态表结构体放入此文件"的项目规范
 */

#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "DailyLoginConfig.generated.h"

// ==================== 活动基础枚举 ====================

/**
 * @brief 活动类型枚举
 * @details 用于明确区分不同类型的活动项，支持活动系统的分类管理
 * @note CategoryHeader表示导航分类标题，NormalActivity表示普通活动页面
 */
UENUM(BlueprintType)
enum class EActivityType : uint8
{
	CategoryHeader   UMETA(DisplayName = "分类标题"),     // 导航分类标题
	NormalActivity   UMETA(DisplayName = "普通活动"),     // 常规活动页面
	WebActivity      UMETA(DisplayName = "网页活动"),     // 外链网页活动
	SpecialEvent     UMETA(DisplayName = "特殊事件")      // 特殊事件活动
};

/**
 * @brief 活动状态枚举
 * @details 表示活动的生命周期状态，用于时间控制和UI显示
 * @note 状态转换顺序：Upcoming -> Active -> EndingSoon -> Ended
 */
UENUM(BlueprintType)
enum class EActivityStatus : uint8
{
	Upcoming     UMETA(DisplayName = "即将开始"),     // 活动未开始，预热期
	Active       UMETA(DisplayName = "进行中"),       // 活动正常进行中
	EndingSoon   UMETA(DisplayName = "即将结束"),     // 活动即将结束，倒计时期
	Ended        UMETA(DisplayName = "已结束"),       // 活动已结束
	Maintenance  UMETA(DisplayName = "维护中")        // 活动维护中，暂停服务
};

// ==================== 时间控制枚举 ====================

/**
 * @brief 时间控制类型枚举
 * @details 用于区分不同的时间控制策略，支持灵活的活动时间管理
 * @note 支持固定周期、循环活动、永久活动和手动控制四种模式
 */
UENUM(BlueprintType)
enum class ETimeControlType : uint8
{
	FixedPeriod   UMETA(DisplayName = "固定周期"),     // 固定时间段活动
	Recurring     UMETA(DisplayName = "循环活动"),     // 周期性重复活动
	Permanent     UMETA(DisplayName = "永久活动"),     // 永久开放活动
	Manual        UMETA(DisplayName = "手动控制")      // 运维手动开关控制
};

// ==================== 红点系统枚举 ====================

/**
 * @brief 红点类型枚举
 * @details 用于区分不同类型的红点提示样式
 * @note 支持无红点、简单红点、数字徽章、特殊徽章和进度徽章五种类型
 */
UENUM(BlueprintType)
enum class ERedDotType : uint8
{
	None             UMETA(DisplayName = "无红点"),        // 不显示红点
	SimpleDot        UMETA(DisplayName = "简单红点"),      // 纯红点提示
	NumberBadge      UMETA(DisplayName = "数字徽章"),      // 带数字的红点
	SpecialBadge     UMETA(DisplayName = "特殊徽章"),      // 特殊样式红点
	ProgressBadge    UMETA(DisplayName = "进度徽章")       // 进度型红点
};

// ==================== 奖励系统枚举 ====================

/**
 * @brief 登录奖励类型枚举
 * @details 解耦具体的道具系统，支持不同类型奖励的统一管理
 * @note NormalItem为基础道具，Premium为高级货币，Box为礼包/宝箱
 */
UENUM(BlueprintType)
enum class ELoginRewardType : uint8
{
	NormalItem  UMETA(DisplayName = "基础道具"),    // 基础游戏道具奖励
	Premium     UMETA(DisplayName = "高级货币"),    // 高级游戏代币奖励
	Box         UMETA(DisplayName = "礼包/宝箱")     // 礼包或宝箱类型奖励
};



/**
 * @brief 通用奖励状态枚举
 * @details 所有奖励系统统一使用的状态定义
 * @note 适用于签到、任务、成就等各种奖励系统
 */
UENUM(BlueprintType)
enum class ERewardState : uint8
{
	Incomplete UMETA(DisplayName = "未到期"),    // 奖励尚未到达可领取时间
	Claimable  UMETA(DisplayName = "可领取"),     // 奖励已准备好，可以领取
	Claimed    UMETA(DisplayName = "已领取")      // 奖励已被玩家领取
};

// ==================== 数据结构定义 ====================

/**
 * @brief 红点数据结构
 * @details 用于存储单个活动项的红点状态信息
 * @note 现已整合到DailyLoginConfig.h中统一管理
 */
USTRUCT(BlueprintType)
struct FRedDotData
{
	GENERATED_BODY()

public:
	/** 红点显示类型 */
	UPROPERTY(BlueprintReadOnly, Category = "RedDot")
	ERedDotType DotType = ERedDotType::None;

	/** 红点显示数值（用于NumberBadge类型） */
	UPROPERTY(BlueprintReadOnly, Category = "RedDot")
	int32 DotValue = 0;

	/** 是否显示红点 */
	UPROPERTY(BlueprintReadOnly, Category = "RedDot")
	bool bShouldShow = false;

	/** 红点显示优先级 */
	UPROPERTY(BlueprintReadOnly, Category = "RedDot")
	int32 Priority = 0;

	/** 关联的活动标识符 */
	UPROPERTY(BlueprintReadOnly, Category = "RedDot")
	FName AssociatedActivityId;

	/**
	 * @brief 默认构造函数
	 */
	FRedDotData() = default;

	/**
	 * @brief 带参数构造函数
	 * @param InType 红点类型
	 * @param InValue 红点数值
	 * @param InShow 是否显示
	 * @param InPriority 优先级
	 * @param InActivityId 关联活动ID
	 */
	FRedDotData(ERedDotType InType, int32 InValue, bool InShow, int32 InPriority, FName InActivityId)
		: DotType(InType), DotValue(InValue), bShouldShow(InShow), Priority(InPriority), AssociatedActivityId(InActivityId)
	{}
};

/**
 * @brief 每日登录配置表结构
 * @details 用于定义每日登录活动中每一天的具体奖励配置
 * @note 这是按天配置的详细奖励信息表
 */
USTRUCT(BlueprintType)
struct FDailyLoginConfigRow : public FTableRowBase
{
	GENERATED_BODY()

public:
	// ==================== 索引字段 ====================
	
	/** 活动唯一标识符 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DailyLogin|Index")
	int32 ActivityID;

	/** 第几天（1-8天） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DailyLogin|Index")
	int32 DayIndex;

	// ==================== 奖励数据 ====================
	
	/** 奖励类型分类 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DailyLogin|Data")
	ELoginRewardType RewardType;

	/** 奖励物品ID */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DailyLogin|Data")
	int32 RewardItemID;

	/** 奖励物品数量 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DailyLogin|Data")
	int32 RewardCount;

	// ==================== UI控制 ====================
	
	/** 是否为特殊奖励（大格子显示） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DailyLogin|UI")
	bool bIsSpecialReward;

	/** 宝箱图片资源（仅用于RewardType为Box类型） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DailyLogin|UI")
	TSoftObjectPtr<UTexture2D> BoxImage;

	/**
	 * @brief 构造函数
	 * @details 初始化默认配置值
	 */
	FDailyLoginConfigRow() 
		: ActivityID(0), DayIndex(0)
		, RewardType(ELoginRewardType::NormalItem)
		, RewardItemID(0), RewardCount(1)
		, bIsSpecialReward(false) 
	{}
};

/**
 * @brief 活动信息表结构（精简版）
 * @details 用于定义活动级别的全局属性配置，直接包含所有必要字段
 * @note 移除了冗余的嵌套结构，职责更加单一明确
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

	/** 活动总天数（用于UI布局计算） */
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
	
	/** 目标页面Widget类 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Routing")
	TSubclassOf<UUserWidget> TargetPageClass;

	/** 页面标题 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Routing")
	FText PageTitle;

	/** 页面描述信息 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Routing")
	FText PageDescription;

	// ==================== 运行时状态引用 ====================
	
	/** 运行时状态数据引用ID（指向DailyLoginSave中的数据） */
	UPROPERTY(BlueprintReadOnly, Category = "Runtime")
	FName RuntimeStateId;

	/** 缓存的运行时状态指针（运行时使用） */
	UPROPERTY(Transient)
	TWeakObjectPtr<UObject> CachedRuntimeState;

	// ==================== 兼容性快捷访问 ====================
	
	/**
	 * @brief 获取活动ID
	 * @return 活动唯一标识符
	 */
	FORCEINLINE int32 GetActivityID() const { return ActivityID; }

	/**
	 * @brief 获取导航ID
	 * @return 导航项标识符
	 */
	FORCEINLINE FName GetNavId() const { return NavId; }

	/**
	 * @brief 获取显示名称
	 * @return 导航项显示文本
	 */
	FORCEINLINE FText GetDisplayName() const { return DisplayName; }

	/**
	 * @brief 获取当前状态
	 * @return 活动当前运行状态
	 * @note 需要在运行时通过ActivitySubsystem获取对应的运行时状态
	 */
	FORCEINLINE EActivityStatus GetCurrentStatus() const 
	{ 
		// 实际实现需要通过RuntimeStateId从运行时数据中查找对应状态
		// 这里返回默认活跃状态，具体实现在运行时由管理者处理
		return EActivityStatus::Active; 
	}

	/**
	 * @brief 构造函数
	 * @details 初始化默认活动配置
	 */
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

/**
 * @brief 基础物品详情表结构
 * @details 定义游戏中所有物品的基础属性和详细信息
 * @note 作为物品系统的中央配置表，支持各种类型物品的统一管理
 */
USTRUCT(BlueprintType)
struct FItemDetailRow : public FTableRowBase
{
	GENERATED_BODY()

public:
	// ==================== 基础标识 ====================
	
	/** 物品唯一ID */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item|Identity")
	int32 ItemID;

	/** 物品显示名称 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item|Identity")
	FText ItemName;

	/** 物品描述信息 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item|Identity")
	FText ItemDescription;

	// ==================== 视觉表现 ====================
	
	/** 物品图标资源 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "Item|Visual")
	TSoftObjectPtr<UTexture2D> ItemIcon;

	/** ==================== 快捷访问方法 ==================== */
	
	/**
	 * @brief 获取物品ID
	 * @return 物品唯一标识符
	 */
	FORCEINLINE int32 GetItemID() const { return ItemID; }

	/**
	 * @brief 获取物品名称
	 * @return 物品显示名称
	 */
	FORCEINLINE FText GetItemName() const { return ItemName; }

	/**
	 * @brief 构造函数
	 * @details 初始化默认物品配置
	 */
	FItemDetailRow() 
		: ItemID(0)
	{};
};

// ==================== 文件末尾说明 ====================

/**
 * @note 所有活动相关静态配置现已整合到上述结构中
 * 包括：基础信息、导航配置、红点配置、时间控制、页面路由等
 * 遵循项目规范：所有静态表结构体统一存放于此文件
 * 已移除冗余的FActivityConfig嵌套结构，使职责更加单一明确
 * 新增FItemDetailRow结构用于统一管理物品详细信息
 */