// 10

#pragma once

#include "CoreMinimal.h"
#include "UI/Activity/Model/ActivityTrackBase.h"
#include "TreasureBoxItem.h"
#include "TreasureTrack.generated.h"

/**
 * Treasure 轨道（活动级）
 * 职责：
 * 1. 管理所有宝箱 Item
 * 2. 统一刷新宝箱状态
 * 3. 对外提供解锁 / 查询接口
 *
 * ⚠️ 不关心进度来源（登录 / 任务）
 */
UCLASS(BlueprintType)
class METALSLUG01_API UTreasureTrack : public UActivityTrackBase
{
	GENERATED_BODY()

public:
	// 所有宝箱（顺序即配置顺序）
	UPROPERTY(BlueprintReadOnly)
	TArray<UTreasureBoxItem*> TreasureBoxes;

public:
	/**
	 * 初始化 Treasure Track
	 * 只在 Track 创建时调用一次
	 */
	virtual void InitializeTrack() override;

	/**
	 * 刷新所有宝箱状态
	 */
	virtual void RefreshTrack() override;

public:
	/**
	 * 解锁指定宝箱
	 * @param BoxIndex 宝箱索引
	 */
	void UnlockBox(int32 BoxIndex);

	/**
	 * 获取指定宝箱
	 */
	UTreasureBoxItem* GetBox(int32 BoxIndex) const;
};

