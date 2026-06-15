// ==========================================
// 每日登录配置表行
// 关联: DT_DailyLoginConfigRow
// ==========================================
#pragma once

#include "CoreMinimal.h"
#include "Engine/DataTable.h"
#include "Data/Enums/ActivityEnums.h"
#include "DailyLoginTableRow.generated.h"

/**
 * @struct FDailyLoginConfigRow
 * @brief 每日登录配置数据表行
 * 用途: 定义每日登录活动中每一天的具体奖励配置
 * 注意: 这是按天配置的详细奖励信息表
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

	/** 第几天（1-8 天） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DailyLogin|Index")
	int32 DayIndex;

	// ==================== 奖励数据 ====================
	
	/** 奖励类型分类 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DailyLogin|Data")
	ELoginRewardType RewardType;

	/** 奖励物品 ID */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DailyLogin|Data")
	int32 RewardItemID;

	/** 奖励物品数量 */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DailyLogin|Data")
	int32 RewardCount;

	// ==================== UI 控制 ====================
	
	/** 是否为特殊奖励（大格子显示） */
	UPROPERTY(EditAnywhere, BlueprintReadWrite, Category = "DailyLogin|UI")
	bool bIsSpecialReward;

	/** 默认构造函数 */
	FDailyLoginConfigRow() 
		: ActivityID(0), DayIndex(0)
		, RewardType(ELoginRewardType::NormalItem)
		, RewardItemID(0), RewardCount(1)
		, bIsSpecialReward(false) 
	{}
};
