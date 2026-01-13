// 12


#include "UI/Activity/Model/Coordinator/ActivityLinkCoordinator.h"
#include "UI/Activity/Model/DailyLogin/DailyLoginTrack.h"
#include "UI/Activity/Model/Treasure/TreasureTrack.h"

/**
 * Initialize
 * 绑定两个 Track
 */
void UActivityLinkCoordinator::Initialize(
	UDailyLoginTrack* InDailyLoginTrack,
	UTreasureTrack* InTreasureTrack
)
{
	DailyLoginTrack = InDailyLoginTrack;
	TreasureTrack   = InTreasureTrack;
}

/**
 * OnLoginDayChanged
 * 核心联动逻辑入口
 */
void UActivityLinkCoordinator::OnLoginDayChanged(int32 NewLoginDay)
{
	if (!TreasureTrack)
	{
		return;
	}

	/**
	 * 示例规则：
	 * - 登录 3 天 → 解锁宝箱 0
	 * - 登录 5 天 → 解锁宝箱 1
	 * - 登录 7 天 → 解锁宝箱 2
	 *
	 * ⚠️ 实际项目中应来自配置表
	 */

	if (NewLoginDay >= 3)
	{
		TreasureTrack->UnlockBox(0);
	}

	if (NewLoginDay >= 5)
	{
		TreasureTrack->UnlockBox(1);
	}

	if (NewLoginDay >= 7)
	{
		TreasureTrack->UnlockBox(2);
	}
}
