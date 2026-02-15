// 13

#include "UI/Activity/Pages/DailyLogin/Widgets/TreasureBoxWidget.h"

#include "Components/Button.h"

void UTreasureBoxWidget::NativeConstruct()
{
    Super::NativeConstruct();

    // 绑定三个选项的点击事件
    if (RewardOption_0) RewardOption_0->OnClicked.AddDynamic(this, &UTreasureBoxWidget::OnOption0Clicked);
    if (RewardOption_1) RewardOption_1->OnClicked.AddDynamic(this, &UTreasureBoxWidget::OnOption1Clicked);
    if (RewardOption_2) RewardOption_2->OnClicked.AddDynamic(this, &UTreasureBoxWidget::OnOption2Clicked);

    // 绑定确认按钮
    if (ConfirmButton) ConfirmButton->OnClicked.AddDynamic(this, &UTreasureBoxWidget::OnConfirmClicked);

    // 初始化：确保默认选择中间
    SelectedIndex = 1;
    OnSelectionChanged(SelectedIndex);
}

void UTreasureBoxWidget::OnOption0Clicked() { SelectedIndex = 0; OnSelectionChanged(SelectedIndex); }
void UTreasureBoxWidget::OnOption1Clicked() { SelectedIndex = 1; OnSelectionChanged(SelectedIndex); }
void UTreasureBoxWidget::OnOption2Clicked() { SelectedIndex = 2; OnSelectionChanged(SelectedIndex); }

void UTreasureBoxWidget::OnConfirmClicked()
{
    // 这里处理最终领取的逻辑，例如通知服务器玩家选择了索引为 SelectedIndex 的奖励
    RemoveFromParent();
}