// ==========================================
// 活动系统枚举定义
// 关联: 所有活动相关 DataTable 行结构体、活动子系统
// ==========================================
#pragma once

#include "CoreMinimal.h"

// ==================== 活动基础枚举 ====================

/**
 * @brief 活动类型枚举
 * @details 用于明确区分不同类型的活动项，支持活动系统的分类管理
 * @note CategoryHeader 表示导航分类标题，NormalActivity 表示普通活动页面
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
 * @details 表示活动的生命周期状态，用于时间控制和 UI 显示
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
 * @note NormalItem 为基础道具，Premium 为高级货币，Box 为礼包/宝箱
 */
UENUM(BlueprintType)
enum class ELoginRewardType : uint8
{
	NormalItem  UMETA(DisplayName = "基础道具"),    // 基础游戏道具奖励
	Premium     UMETA(DisplayName = "高级货币"),    // 高级游戏代币奖励
	Box         UMETA(DisplayName = "礼包/宝箱")     // 礼包或宝箱类型奖励
};

/**
 * @brief 游戏模式枚举
 * @details 用于每日升级奖励活动中的游戏模式分类
 * @note 包含团队竞技、个人竞技、枪王排位三种模式
 */
UENUM(BlueprintType)
enum class EGameModeType : uint8
{
	TeamCombat     UMETA(DisplayName = "团队竞技"),     // 团队竞技模式
	SoloCombat     UMETA(DisplayName = "个人竞技"),     // 个人竞技模式
	GunKingRank    UMETA(DisplayName = "枪王排位")      // 枪王排位模式
};

/**
 * @brief 任务类型枚举
 * @details 用于每日升级奖励活动中的任务类型分类
 * @note 包含游玩记录数、击杀数、对局排名第一、连续击杀 3 人四种类型
 */
UENUM(BlueprintType)
enum class ETaskType : uint8
{
	PlayRecordCount    UMETA(DisplayName = "游玩记录数"),     // 游玩记录数任务
	KillCount          UMETA(DisplayName = "击杀数"),         // 击杀数任务
	FirstPlace         UMETA(DisplayName = "对局排名第一"),    // 对局排名第一任务
	TripleKill         UMETA(DisplayName = "连续击杀3人")      // 连续击杀 3 人任务
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
