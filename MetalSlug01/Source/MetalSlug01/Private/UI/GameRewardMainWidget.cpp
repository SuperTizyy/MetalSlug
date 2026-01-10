// 版权声明：在项目设置的描述页面填写您的版权信息


#include "MetalSlug01/Public/UI/GameRewardMainWidget.h"
#include "MetalSlug01/Public/UI/GameRewardItemWidget.h"

// NativeConstruct - Widget构造时调用的初始化函数
// 注册按钮点击事件回调并刷新奖励列表
void UGameRewardMainWidget::NativeConstruct()
{
	Super::NativeConstruct(); // 调用父类构造

	// 1. 注册按钮点击事件回调
	if (Btn_ClaimAll)
	{
		// 将"全部领取"按钮的点击事件绑定到OnClaimAllClicked回调函数
		Btn_ClaimAll->OnClicked.AddDynamic(this, &UGameRewardMainWidget::OnClaimAllClicked);
	}

	// 2. 模拟加载数据并刷新列表
	RefreshRewardList();
}

// RefreshRewardList - 刷新奖励列表
// 清空现有列表并重新创建奖励项
void UGameRewardMainWidget::RefreshRewardList()
{
	if (!RewardItemClass || !RewardListContainer) return; // 安全检查

	RewardListContainer->ClearChildren(); // 清空当前旧列表

	// 模拟循环创建 5 天的奖励卡片
	for (int32 i = 1; i <= 5; i++)
	{
		// 创建子项 Widget 实例
		UGameRewardItemWidget* NewItem = CreateWidget<UGameRewardItemWidget>(this, RewardItemClass);
		if (NewItem)
		{
			// 设置状态：第一天设为可领取，第二天设为明天，之后设为锁定
			ERewardStatus Status = (i == 1) ? ERewardStatus::Claimable : 
								   (i == 2) ? ERewardStatus::Tomorrow : ERewardStatus::Locked;
            
			// 初始化数据
			NewItem->SetRewardData(TEXT("夹克派对宝箱"), Status, i);
            
			// 将子项添加到水平布局容器中
			RewardListContainer->AddChildToHorizontalBox(NewItem);
		}
	}
}

// OnClaimAllClicked - 处理"全部领取"按钮点击事件
// 执行批量领取奖励的逻辑
void UGameRewardMainWidget::OnClaimAllClicked()
{
	// 这里处理点击"全部领取"的逻辑（如发送网络请求、播放音效）
	UE_LOG(LogTemp, Warning, TEXT("用户点击了全部领取按钮！"));
}