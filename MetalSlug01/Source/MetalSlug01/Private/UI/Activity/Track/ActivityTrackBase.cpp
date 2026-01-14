// 2

#include "UI/Activity/Track/ActivityTrackBase.h"

void UActivityTrackBase::InitializeTrack()
{
	// 默认什么都不做
	// 子类负责具体初始化
}
void UActivityTrackBase::RefreshTrack()
{
	// 默认什么都不做
	// 子类根据服务器数据刷新状态
}
void UActivityTrackBase::RebuildTrack()
{
	// 默认直接刷新
	// 子类可覆盖做更复杂的重建逻辑
	RefreshTrack();
}
bool UActivityTrackBase::CanShow() const
{
	// 默认规则：
	// 活动激活才允许显示
	return bIsActive;
}
