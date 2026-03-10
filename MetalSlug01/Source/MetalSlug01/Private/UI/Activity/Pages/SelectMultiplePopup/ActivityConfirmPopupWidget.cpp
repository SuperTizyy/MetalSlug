#include "UI/Activity/Pages/SelectMultiplePopup/ActivityConfirmPopupWidget.h"
#include "Components/Button.h"
#include "Components/HorizontalBox.h"
#include "UI/Activity/Pages/SelectMultiplePopup/RewardOptionCardWidget.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "UI/Activity/Data/DailyLoginConfig.h"
#include "UI/Activity/Core/ActivitySubsystem.h"
#include "UI/Activity/Core/UpgradeActivitySubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/GameInstance.h"

void UActivityConfirmPopupWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// 检查必要的类是否已设置
	UE_LOG(LogTemp, Warning, TEXT("ActivityConfirmPopup: 检查必要类设置:"));
	UE_LOG(LogTemp, Warning, TEXT("  RewardOptionCardClass: %s"), RewardOptionCardClass ? *RewardOptionCardClass->GetName() : TEXT("未设置"));

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

	// RewardOptionsContainer已在蓝图中绑定，用于放置奖励选项卡片
	if (RewardOptionsContainer)
	{
		UE_LOG(LogTemp, Log, TEXT("ActivityConfirmPopup: RewardOptionsContainer绑定成功"));
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
		
		// 为每个奖励选项创建多个卡片（一个宝箱可能对应多个奖励项）
		for (int32 i = 0; i < FMath::Min(3, RewardOptions.Num()); ++i)
		{
			CreateRewardCardsForBox(RewardOptions[i]);
		}
		
		UE_LOG(LogTemp, Log, TEXT("ActivityConfirmPopup: 奖励卡片创建完成"));
	}
}



void UActivityConfirmPopupWidget::SetSelectedIndex(int32 Index)
{
	SelectedIndex = FMath::Clamp(Index, 0, 2);
	
	// 更新视觉选中状态
	UE_LOG(LogTemp, Log, TEXT("ActivityConfirmPopup: 设置选中索引为 %d"), SelectedIndex);
}

void UActivityConfirmPopupWidget::CreateRewardCardsForBox(const FDailyLoginConfigRow& Config)
{
	// 使用新的RewardOptionCardWidget创建奖励卡片
	if (!RewardOptionCardClass)
	{
		UE_LOG(LogTemp, Error, TEXT("ActivityConfirmPopup: RewardOptionCardClass未设置！"));
		return;
	}
	
	if (!RewardOptionsContainer)
	{
		UE_LOG(LogTemp, Error, TEXT("ActivityConfirmPopup: RewardOptionsContainer未设置！"));
		return;
	}
	
	// 通过GameInstance获取ActivitySubsystem
	UGameInstance* GameInstance = GetGameInstance();
	if (!GameInstance)
	{
		UE_LOG(LogTemp, Error, TEXT("ActivityConfirmPopup: 无法获取GameInstance"));
		return;
	}
	
	UActivitySubsystem* ActivitySub = GameInstance->GetSubsystem<UActivitySubsystem>();
	if (!ActivitySub)
	{
		UE_LOG(LogTemp, Error, TEXT("ActivityConfirmPopup: 无法获取ActivitySubsystem"));
		return;
	}
	
	// 根据表关联关系获取数据：
	// 1. DailyLoginConfigRow.RewardItemID == TreasureBoxItemRow.BoxID
	// 2. TreasureBoxItemRow.ItemID == ItemDetailRow.ItemID
	
	// 获取指定BoxID的所有TreasureBoxItem记录
	TArray<const FTreasureBoxItemRow*> TreasureBoxItems = ActivitySub->GetTreasureBoxItemsByBoxID(Config.RewardItemID);
	if (TreasureBoxItems.Num() == 0)
	{
		UE_LOG(LogTemp, Error, TEXT("ActivityConfirmPopup: 未找到BoxID %d 的宝箱物品配置"), Config.RewardItemID);
		return;
	}
	
	UE_LOG(LogTemp, Log, TEXT("ActivityConfirmPopup: 找到%d个BoxID=%d的宝箱物品记录"), TreasureBoxItems.Num(), Config.RewardItemID);
	
	// 为每个TreasureBoxItem记录创建一个RewardOptionCardWidget并直接添加到容器中
	for (int32 i = 0; i < TreasureBoxItems.Num(); ++i)
	{
		const FTreasureBoxItemRow* TreasureBoxItem = TreasureBoxItems[i];
		
		// 通过TreasureBoxItem.ItemID查询ItemDetailRow表获取图标
		const FItemDetailRow* ItemDetail = ActivitySub->GetItemDetail(TreasureBoxItem->ItemID);
		if (!ItemDetail)
		{
			UE_LOG(LogTemp, Error, TEXT("ActivityConfirmPopup: 未找到ItemID %d 的物品详细信息"), TreasureBoxItem->ItemID);
			continue;
		}
		
		UE_LOG(LogTemp, Log, TEXT("ActivityConfirmPopup: 处理第%d个奖励项 - ItemID: %d, ItemCount: %d, ItemName: %s"), 
			i + 1, TreasureBoxItem->ItemID, TreasureBoxItem->ItemCount, *ItemDetail->ItemName.ToString());
		
		// 创建单个奖励卡片
		URewardOptionCardWidget* RewardCard = CreateWidget<URewardOptionCardWidget>(this, RewardOptionCardClass);
		if (RewardCard)
		{
			// 通过GameInstance获取UpgradeActivitySubsystem以获取当前选中的RewardIconIndex
			UUpgradeActivitySubsystem* UpgradeSub = GameInstance->GetSubsystem<UUpgradeActivitySubsystem>();
			int32 CurrentRewardIconIndex = 0;
			if (UpgradeSub)
			{
				CurrentRewardIconIndex = UpgradeSub->GetCurrentRewardIconIndex();
				UE_LOG(LogTemp, Log, TEXT("ActivityConfirmPopup: 当前RewardIconIndex为 %d"), CurrentRewardIconIndex);
			}
			
			// 判断当前卡片是否应该被选中
			bool bShouldBeSelected = (i == CurrentRewardIconIndex);
			
			// 使用直接数据初始化方法，避免重复查询
			RewardCard->InitializeCardWithDirectData(ItemDetail, TreasureBoxItem, i);
			
			// 设置选中状态
			RewardCard->SetSelected(bShouldBeSelected);
			
			// 绑定选中事件
			RewardCard->OnRewardSelected.AddDynamic(this, &UActivityConfirmPopupWidget::OnRewardCardSelected);
			
			// 直接添加到RewardOptionsContainer中
			RewardOptionsContainer->AddChildToHorizontalBox(RewardCard);
			
			UE_LOG(LogTemp, Log, TEXT("ActivityConfirmPopup: 成功创建第%d个奖励卡片: %s x%d (来自宝箱 %d)"), 
				i + 1, *ItemDetail->ItemName.ToString(), TreasureBoxItem->ItemCount, Config.RewardItemID);
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("ActivityConfirmPopup: 无法创建第%d个RewardOptionCardWidget实例"), i + 1);
		}
	}
	
	UE_LOG(LogTemp, Log, TEXT("ActivityConfirmPopup: 奖励卡片创建完成，共创建 %d 个卡片"), TreasureBoxItems.Num());
}

UWidget* UActivityConfirmPopupWidget::CreateRewardCard(const FDailyLoginConfigRow& Config, int32 Index)
{
	// 保留原有的单卡片创建逻辑以保持向后兼容
	if (!RewardOptionCardClass)
	{
		UE_LOG(LogTemp, Error, TEXT("ActivityConfirmPopup: RewardOptionCardClass未设置！"));
		return nullptr;
	}
	
	URewardOptionCardWidget* RewardCard = CreateWidget<URewardOptionCardWidget>(this, RewardOptionCardClass);
	if (RewardCard)
	{
		// 通过GameInstance获取ActivitySubsystem
		UGameInstance* GameInstance = GetGameInstance();
		if (GameInstance)
		{
			UActivitySubsystem* ActivitySub = GameInstance->GetSubsystem<UActivitySubsystem>();
			if (ActivitySub)
			{
				// 根据表关联关系获取数据：
				// 1. DailyLoginConfigRow.RewardItemID == TreasureBoxItemRow.BoxID
				// 2. TreasureBoxItemRow.ItemID == ItemDetailRow.ItemID
				
				// 首先从TreasureBoxItemRow表获取BoxID对应的记录
				const FTreasureBoxItemRow* TreasureBoxItem = ActivitySub->GetTreasureBoxItem(Config.RewardItemID);
				if (TreasureBoxItem)
				{
					UE_LOG(LogTemp, Log, TEXT("ActivityConfirmPopup: 找到TreasureBoxItem记录 - BoxID: %d, ItemID: %d, ItemCount: %d"), 
						TreasureBoxItem->BoxID, TreasureBoxItem->ItemID, TreasureBoxItem->ItemCount);
					
					// 然后通过TreasureBoxItem.ItemID查询ItemDetailRow表获取图标
					const FItemDetailRow* ItemDetail = ActivitySub->GetItemDetail(TreasureBoxItem->ItemID);
					if (ItemDetail)
					{
						UE_LOG(LogTemp, Log, TEXT("ActivityConfirmPopup: 找到ItemDetail记录 - ItemID: %d, ItemName: %s"), 
							ItemDetail->ItemID, *ItemDetail->ItemName.ToString());
						
						// 使用新的初始化方法，传入BoxID和ItemID
						RewardCard->InitializeCardWithDataTables(TreasureBoxItem->ItemID, Config.RewardItemID);
						
						// 绑定选中事件
						RewardCard->OnRewardSelected.AddDynamic(this, &UActivityConfirmPopupWidget::OnRewardCardSelected);
						
						UE_LOG(LogTemp, Log, TEXT("ActivityConfirmPopup: 成功创建奖励卡片 [%d]: %s x%d (来自宝箱 %d)"), 
							Index, *ItemDetail->ItemName.ToString(), TreasureBoxItem->ItemCount, Config.RewardItemID);
					}
					else
					{
						UE_LOG(LogTemp, Error, TEXT("ActivityConfirmPopup: 未找到ItemID %d 的物品详细信息"), TreasureBoxItem->ItemID);
						return nullptr;
					}
				}
				else
				{
					UE_LOG(LogTemp, Error, TEXT("ActivityConfirmPopup: 未找到BoxID %d 的宝箱物品配置"), Config.RewardItemID);
					return nullptr;
				}
			}
			else
			{
				UE_LOG(LogTemp, Error, TEXT("ActivityConfirmPopup: 无法获取ActivitySubsystem"));
				return nullptr;
			}
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("ActivityConfirmPopup: 无法获取GameInstance"));
			return nullptr;
		}
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("ActivityConfirmPopup: 无法创建RewardOptionCardWidget实例"));
	}
	
	return RewardCard;
}

void UActivityConfirmPopupWidget::OnCloseClicked()
{
	UE_LOG(LogTemp, Log, TEXT("ActivityConfirmPopup: 关闭按钮被点击"));
	RemoveFromParent();
}

void UActivityConfirmPopupWidget::OnConfirmClicked()
{
	UE_LOG(LogTemp, Log, TEXT("ActivityConfirmPopup: 确认按钮被点击"));
	
	// 使用临时选中状态而不是当前选中状态
	int32 FinalSelectedIndex = PendingSelectedIndex;
	if (FinalSelectedIndex == -1)
	{
		FinalSelectedIndex = SelectedIndex; // 如果没有临时选择，使用默认值
	}
	
	UE_LOG(LogTemp, Log, TEXT("ActivityConfirmPopup: 最终选中索引: %d"), FinalSelectedIndex);
	
	// 通过GameInstance获取UpgradeActivitySubsystem
	UGameInstance* GameInstance = GetGameInstance();
	if (GameInstance)
	{
		UUpgradeActivitySubsystem* UpgradeSub = GameInstance->GetSubsystem<UUpgradeActivitySubsystem>();
		if (UpgradeSub)
		{
			// 更新RewardIconIndex并保存到存档
			// 这里才是真正更新数据的地方
			if (UpgradeSub->UpdateRewardIconIndexAndSave(FinalSelectedIndex))
			{
				UE_LOG(LogTemp, Log, TEXT("ActivityConfirmPopup: 成功更新RewardIconIndex为 %d 并保存"), FinalSelectedIndex);
			}
			else
			{
				UE_LOG(LogTemp, Error, TEXT("ActivityConfirmPopup: 更新RewardIconIndex失败"));
			}
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("ActivityConfirmPopup: 无法获取UpgradeActivitySubsystem"));
		}
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("ActivityConfirmPopup: 无法获取GameInstance"));
	}
	
	// 关闭弹窗
	RemoveFromParent();
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

void UActivityConfirmPopupWidget::OnRewardCardSelected(URewardOptionCardWidget* CardWidget, bool bIsChecked)
{
	UE_LOG(LogTemp, Log, TEXT("ActivityConfirmPopup: 奖励卡片选中状态改变 - ID: %d, 选中: %s"), 
		CardWidget->RewardID, bIsChecked ? TEXT("是") : TEXT("否"));
	
	// 跟踪临时选中状态
	if (bIsChecked && CardWidget)
	{
		PendingSelectedIndex = CardWidget->GetCardIndex();
		UE_LOG(LogTemp, Log, TEXT("ActivityConfirmPopup: 记录临时选中索引: %d"), PendingSelectedIndex);
	}
	
	// 这里可以处理选中逻辑
	// 例如：单选模式下取消其他卡片的选中状态
	if (bIsChecked && RewardOptionsContainer)
	{
		for (int32 i = 0; i < RewardOptionsContainer->GetChildrenCount(); ++i)
		{
			UWidget* Child = RewardOptionsContainer->GetChildAt(i);
			URewardOptionCardWidget* OtherCard = Cast<URewardOptionCardWidget>(Child);
			if (OtherCard && OtherCard != CardWidget)
			{
				OtherCard->SetSelected(false);
			}
		}
	}
}