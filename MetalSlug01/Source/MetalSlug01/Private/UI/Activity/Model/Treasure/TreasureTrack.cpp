// 10

#include "UI/Activity/Model/Treasure/TreasureTrack.h"
#include "UI/Activity/Treasure/Model/TreasureBoxItem.h"

/**
 * InitializeTrack
 * Track 生命周期入口
 * 只在创建 Track 时调用一次
 */
void UTreasureTrack::InitializeTrack()
{
	// 设置活动 ID（用于 ActivitySubsystem 管理）
	ActivityId = TEXT("Treasure");

	// 标记 Track 为激活状态
	// 实际项目中通常由服务器控制
	bIsActive = true;

	// -------- 示例：初始化 3 个宝箱 --------
	// ⚠️ 实际项目中应由配置表 / 服务器数据生成
	const int32 BoxCount = 3;

	for (int32 Index = 0; Index < BoxCount; ++Index)
	{
		// 创建宝箱 Item（Outer 为 Track，生命周期绑定）
		UTreasureBoxItem* BoxItem = NewObject<UTreasureBoxItem>(this);

		// 设置宝箱索引
		BoxItem->BoxIndex = Index;

		// 初始状态：未解锁
		BoxItem->bUnlocked = false;
		BoxItem->bClaimed = false;

		// 示例：解锁条件（如需要 3 / 5 / 7 天）
		// ⚠️ 这里只是占位数据
		BoxItem->RequiredProgress = (Index + 1) * 3;

		// 添加到宝箱列表
		TreasureBoxes.Add(BoxItem);
	}

	// 初始化完成后刷新一次状态
	RefreshTrack();
}

/**
 * RefreshTrack
 * 统一刷新宝箱状态
 * 只根据当前 BoxItem 自身数据判断
 */
void UTreasureTrack::RefreshTrack()
{
	for (UTreasureBoxItem* Box : TreasureBoxes)
	{
		if (!Box)
		{
			continue;
		}

		// 如果已经领取，则不再处理
		if (Box->bClaimed)
		{
			Box->bUnlocked = false;
			continue;
		}

		// ⚠️ 注意：
		// TreasureTrack 本身不判断进度来源
		// 解锁条件是否满足，由外部推进
		// 这里只维持状态，不做计算
	}
}

/**
 * UnlockBox
 * 外部调用（如 DailyLoginTrack 联动）
 * 只负责解锁，不判断来源是否合法
 */
void UTreasureTrack::UnlockBox(int32 BoxIndex)
{
	UTreasureBoxItem* Box = GetBox(BoxIndex);
	if (!Box)
	{
		return;
	}

	// 已解锁则忽略
	if (Box->bUnlocked)
	{
		return;
	}

	// 设置为已解锁
	Box->bUnlocked = true;

	// 解锁后刷新一次
	RefreshTrack();
}

/**
 * GetBox
 * 根据索引获取宝箱
 */
UTreasureBoxItem* UTreasureTrack::GetBox(int32 BoxIndex) const
{
	if (!TreasureBoxes.IsValidIndex(BoxIndex))
	{
		return nullptr;
	}

	return TreasureBoxes[BoxIndex];
}
