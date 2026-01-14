/*// 版权声明：在项目设置的描述页面填写您的版权信息


#include "MetalSlug01/Public/UI/GameRewardItemWidget.h"

// SetRewardData - 设置奖励项的数据
// 根据传入的参数更新UI控件的显示内容
void UGameRewardItemWidget::SetRewardData(FString Title, ERewardStatus Status, int32 DayNumber)
{
	// 1. 设置顶部标题
	if (Text_RewardTitle)
	{
		// 设置奖励标题文字
		Text_RewardTitle->SetText(FText::FromString(Title)); // 设置奖励名称
	}

	// 2. 根据状态切换底部显示和颜色
	if (Text_StatusLabel)
	{
		// 根据不同的奖励状态设置标签文本内容
		switch (Status)
		{
		case ERewardStatus::Claimable:
			// 可领取状态：显示"CLAIM"
			Text_StatusLabel->SetText(FText::FromString(TEXT("CLAIM"))); // 显示领取
			// 这里可以添加逻辑：改变背景为黄色
			break;
		case ERewardStatus::Tomorrow:
			// 明天领取状态：显示"AVAILABLE TOMORROW"并换行
			Text_StatusLabel->SetText(FText::FromString(TEXT("AVAILABLE\nTOMORROW"))); // 换行显示明天领取
			break;
		case ERewardStatus::Locked:
			// 已锁定状态：显示天数
			Text_StatusLabel->SetText(FText::Format(FText::FromString(TEXT("DAY {0}")), DayNumber)); // 显示天数
			break;
		}
	}
}*/