#include "UI/Activity/Pages/Widgets/ActivityConfirmPopupWidget.h"
#include "Components/Button.h"
#include "Components/HorizontalBox.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "UI/Activity/Data/DailyLoginConfig.h"

void UActivityConfirmPopupWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// 绑定关闭按钮事件
	if (CloseButton)
	{
		CloseButton->OnClicked.AddDynamic(this, &UActivityConfirmPopupWidget::OnCloseClicked);
		UE_LOG(LogTemp, Log, TEXT("ActivityConfirmPopup: 关闭按钮事件绑定成功"));
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("ActivityConfirmPopup: CloseButton未在蓝图中绑定"));
	}

	// 绑定确认按钮事件
	if (ConfirmButton)
	{
		ConfirmButton->OnClicked.AddDynamic(this, &UActivityConfirmPopupWidget::OnConfirmClicked);
		UE_LOG(LogTemp, Log, TEXT("ActivityConfirmPopup: 确认按钮事件绑定成功"));
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("ActivityConfirmPopup: ConfirmButton未在蓝图中绑定"));
	}

	// 绑定HorizontalBox点击事件
	if (RewardOptionsContainer)
	{
		// 注意：HorizontalBox本身不直接支持点击事件
		// 这里提供几种处理方案：
		// 1. 在容器内添加透明按钮覆盖整个区域
		// 2. 监听子元素的点击事件
		// 3. 通过自定义事件系统处理
		UE_LOG(LogTemp, Log, TEXT("ActivityConfirmPopup: RewardOptionsContainer绑定成功"));
		
		// 方案1：添加透明覆盖按钮
		SetupHorizontalBoxClickHandler();
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("ActivityConfirmPopup: RewardOptionsContainer未在蓝图中绑定"));
	}
}

void UActivityConfirmPopupWidget::InitializePopup(const TArray<FDailyLoginConfigRow>& InRewardOptions, int32 InSelectedIndex)
{
	RewardOptions = InRewardOptions;
	SelectedIndex = InSelectedIndex;
	
	UE_LOG(LogTemp, Log, TEXT("ActivityConfirmPopup: 初始化弹窗，奖励选项数量: %d, 默认选中索引: %d"), 
		RewardOptions.Num(), SelectedIndex);
	
	// 如果有奖励选项容器，创建奖励卡片
	if (RewardOptionsContainer && RewardOptions.Num() > 0)
	{
		// 清空现有内容
		RewardOptionsContainer->ClearChildren();
		
		// 为每个奖励选项创建卡片
		for (int32 i = 0; i < FMath::Min(3, RewardOptions.Num()); ++i)
		{
			UWidget* RewardCard = CreateRewardCard(RewardOptions[i], i);
			if (RewardCard)
			{
				RewardOptionsContainer->AddChildToHorizontalBox(RewardCard);
			}
		}
		
		UE_LOG(LogTemp, Log, TEXT("ActivityConfirmPopup: 奖励卡片创建完成，共创建 %d 个卡片"), 
			FMath::Min(3, RewardOptions.Num()));
	}
}



void UActivityConfirmPopupWidget::SetSelectedIndex(int32 Index)
{
	SelectedIndex = FMath::Clamp(Index, 0, 2);
	
	// 更新视觉选中状态
	UE_LOG(LogTemp, Log, TEXT("ActivityConfirmPopup: 设置选中索引为 %d"), SelectedIndex);
}

UWidget* UActivityConfirmPopupWidget::CreateRewardCard(const FDailyLoginConfigRow& Config, int32 Index)
{
	// 创建垂直布局容器
	UVerticalBox* CardContainer = NewObject<UVerticalBox>(this);
	if (!CardContainer)
	{
		UE_LOG(LogTemp, Error, TEXT("ActivityConfirmPopup: 无法创建奖励卡片容器"));
		return nullptr;
	}
	
	CardContainer->SetVisibility(ESlateVisibility::Visible);

	// 创建奖励标题
	UTextBlock* TitleText = NewObject<UTextBlock>(this);
	if (TitleText)
	{
		TitleText->SetText(FText::FromString(FString::Printf(TEXT("奖励 %d"), Config.RewardItemID)));
		TitleText->SetVisibility(ESlateVisibility::Visible);
		CardContainer->AddChild(TitleText);
	}

	// 创建奖励图标占位符
	UImage* RewardImage = NewObject<UImage>(this);
	if (RewardImage)
	{
		RewardImage->SetVisibility(ESlateVisibility::Visible);
		// 这里可以设置默认图标或从Config中加载图标
		CardContainer->AddChild(RewardImage);
	}

	// 创建奖励数量
	UTextBlock* CountText = NewObject<UTextBlock>(this);
	if (CountText)
	{
		CountText->SetText(FText::FromString(FString::Printf(TEXT("X%d"), Config.RewardCount)));
		CountText->SetVisibility(ESlateVisibility::Visible);
		CardContainer->AddChild(CountText);
	}

	// 创建按钮包装器
	UButton* CardButton = NewObject<UButton>(this);
	if (CardButton)
	{
		CardButton->SetVisibility(ESlateVisibility::Visible);
		CardButton->SetContent(CardContainer);
		
		// 根据索引绑定不同事件
		switch (Index)
		{
		case 0:
			CardButton->OnClicked.AddDynamic(this, &UActivityConfirmPopupWidget::OnRewardCardClicked_0);
			break;
		case 1:
			CardButton->OnClicked.AddDynamic(this, &UActivityConfirmPopupWidget::OnRewardCardClicked_1);
			break;
		case 2:
			CardButton->OnClicked.AddDynamic(this, &UActivityConfirmPopupWidget::OnRewardCardClicked_2);
			break;
		default:
			UE_LOG(LogTemp, Warning, TEXT("ActivityConfirmPopup: 无效的奖励卡片索引 %d"), Index);
			break;
		}
		
		UE_LOG(LogTemp, Log, TEXT("ActivityConfirmPopup: 奖励卡片 %d 创建成功，RewardItemID: %d, Count: %d"), 
			Index, Config.RewardItemID, Config.RewardCount);
	}

	return CardButton;
}

void UActivityConfirmPopupWidget::OnCloseClicked()
{
	UE_LOG(LogTemp, Log, TEXT("ActivityConfirmPopup: 关闭按钮被点击"));
	RemoveFromParent();
}

void UActivityConfirmPopupWidget::OnConfirmClicked()
{
	UE_LOG(LogTemp, Log, TEXT("ActivityConfirmPopup: 确认按钮被点击，当前选中索引: %d"), SelectedIndex);
	RemoveFromParent();
}

void UActivityConfirmPopupWidget::OnHorizontalBoxClicked()
{
	UE_LOG(LogTemp, Log, TEXT("ActivityConfirmPopup: HorizontalBox容器被点击"));
	// 这里可以添加容器级别的处理逻辑
	// 例如：取消所有选中状态、显示帮助信息等
}

void UActivityConfirmPopupWidget::SetupHorizontalBoxClickHandler()
{
	// 创建透明覆盖按钮来捕获点击事件
	UButton* OverlayButton = NewObject<UButton>(this);
	if (OverlayButton && RewardOptionsContainer)
	{
		OverlayButton->SetVisibility(ESlateVisibility::SelfHitTestInvisible);
		OverlayButton->OnClicked.AddDynamic(this, &UActivityConfirmPopupWidget::OnHorizontalBoxClicked);
		
		// 将按钮添加到HorizontalBox的第一个位置
		RewardOptionsContainer->AddChildToHorizontalBox(OverlayButton);
		
		UE_LOG(LogTemp, Log, TEXT("ActivityConfirmPopup: HorizontalBox点击处理器设置成功"));
	}
}

void UActivityConfirmPopupWidget::OnRewardCardClicked_0()
{
	UE_LOG(LogTemp, Log, TEXT("ActivityConfirmPopup: 奖励卡片0被点击"));
	InternalOnRewardCardClicked(0);
}

void UActivityConfirmPopupWidget::OnRewardCardClicked_1()
{
	UE_LOG(LogTemp, Log, TEXT("ActivityConfirmPopup: 奖励卡片1被点击"));
	InternalOnRewardCardClicked(1);
}

void UActivityConfirmPopupWidget::OnRewardCardClicked_2()
{
	UE_LOG(LogTemp, Log, TEXT("ActivityConfirmPopup: 奖励卡片2被点击"));
	InternalOnRewardCardClicked(2);
}

void UActivityConfirmPopupWidget::InternalOnRewardCardClicked(int32 Index)
{
	SetSelectedIndex(Index);
	UE_LOG(LogTemp, Log, TEXT("ActivityConfirmPopup: 奖励卡片点击处理完成，索引: %d"), Index);
}