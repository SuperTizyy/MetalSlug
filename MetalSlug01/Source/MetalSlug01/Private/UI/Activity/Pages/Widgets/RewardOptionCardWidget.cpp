// RewardOptionCardWidget.cpp
#include "UI/Activity/Pages/Widgets/RewardOptionCardWidget.h"
#include "UI/Activity/Core/ActivitySubsystem.h"
#include "Kismet/GameplayStatics.h"

void URewardOptionCardWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// 绑定复选框状态改变事件
	if (SelectionCheckBox)
	{
		SelectionCheckBox->OnCheckStateChanged.AddDynamic(this, &URewardOptionCardWidget::OnCheckBoxStateChanged);
	}
}

void URewardOptionCardWidget::InitializeCard(int32 InRewardID, int32 InRewardCount, const FText& InRewardName, UTexture2D* InRewardIcon)
{
	RewardID = InRewardID;
	RewardCount = InRewardCount;
	bIsSelected = false;

	// 设置奖励图标
	if (RewardImage && InRewardIcon)
	{
		RewardImage->SetBrushFromTexture(InRewardIcon);
	}

	// 设置奖励文本
	if (RewardText)
	{
		FString DisplayText = FString::Printf(TEXT("%s x%d"), *InRewardName.ToString(), InRewardCount);
		RewardText->SetText(FText::FromString(DisplayText));
	}

	// 初始化复选框状态
	if (SelectionCheckBox)
	{
		SelectionCheckBox->SetIsChecked(false);
	}
}

void URewardOptionCardWidget::InitializeCardWithDataTables(int32 InItemID, int32 InBoxID)
{
	RewardID = InItemID;
	bIsSelected = false;

	// 通过GameInstance获取ActivitySubsystem
	UGameInstance* GameInstance = GetGameInstance();
	if (!GameInstance)
	{
		UE_LOG(LogTemp, Error, TEXT("RewardOptionCardWidget: 无法获取GameInstance"));
		return;
	}

	UActivitySubsystem* ActivitySub = GameInstance->GetSubsystem<UActivitySubsystem>();
	if (!ActivitySub)
	{
		UE_LOG(LogTemp, Error, TEXT("RewardOptionCardWidget: 无法获取ActivitySubsystem"));
		return;
	}

	// 根据表关联关系获取数据：
	// 1. InBoxID 对应 TreasureBoxItemRow.BoxID
	// 2. InItemID 对应 ItemDetailRow.ItemID (通过TreasureBoxItemRow.ItemID关联)

	// 从ItemDetailRow表获取物品名称信息
	const FItemDetailRow* ItemDetail = ActivitySub->GetItemDetail(InItemID);
	if (!ItemDetail)
	{
		UE_LOG(LogTemp, Error, TEXT("RewardOptionCardWidget: 无法获取ItemID %d 的物品详情"), InItemID);
		return;
	}
	
	// 从TreasureBoxItemRow表获取ItemCount字段数据
	const FTreasureBoxItemRow* TreasureBoxItem = ActivitySub->GetTreasureBoxItem(InBoxID);
	if (TreasureBoxItem && RewardText)
	{
		UE_LOG(LogTemp, Warning, TEXT("RewardOptionCardWidget: 设置奖励文本，ItemID: %d, ItemName: %s, ItemCount: %d"), 
			InItemID, *ItemDetail->ItemName.ToString(), TreasureBoxItem->ItemCount);
		
		// 使用正确的格式：物品名称 x 数量
		FString DisplayText = FString::Printf(TEXT("%s x%d"), *ItemDetail->ItemName.ToString(), TreasureBoxItem->ItemCount);
		RewardText->SetText(FText::FromString(DisplayText));
		RewardCount = TreasureBoxItem->ItemCount;
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("RewardOptionCardWidget: 无法获取BoxID %d 的宝箱物品配置"), InBoxID);
		return;
	}

	// 设置奖励图标
	if (RewardImage)
	{
		UE_LOG(LogTemp, Warning, TEXT("RewardOptionCardWidget: 设置奖励图标，ItemID: %d"), InItemID);
		RewardImage->SetBrushFromSoftTexture(ItemDetail->ItemIcon);
	}

	// 初始化复选框状态
	if (SelectionCheckBox)
	{
		SelectionCheckBox->SetIsChecked(false);
	}
}

void URewardOptionCardWidget::SetSelected(bool bInSelected)
{
	bIsSelected = bInSelected;
	
	if (SelectionCheckBox)
	{
		SelectionCheckBox->SetIsChecked(bInSelected);
	}
}

void URewardOptionCardWidget::OnCheckBoxStateChanged(bool bIsChecked)
{
	bIsSelected = bIsChecked;
	
	// 广播选中事件
	OnRewardSelected.Broadcast(this, bIsChecked);
}

void URewardOptionCardWidget::InitializeCardWithDirectData(const FItemDetailRow* InItemDetail, const FTreasureBoxItemRow* InTreasureBoxItem)
{
	if (!InItemDetail || !InTreasureBoxItem)
	{
		UE_LOG(LogTemp, Error, TEXT("RewardOptionCardWidget: 初始化数据为空"));
		return;
	}
	
	RewardID = InTreasureBoxItem->ItemID;
	RewardCount = InTreasureBoxItem->ItemCount;
	bIsSelected = false;
	
	UE_LOG(LogTemp, Warning, TEXT("RewardOptionCardWidget: 直接初始化 - ItemID: %d, ItemName: %s, ItemCount: %d"), 
		InTreasureBoxItem->ItemID, *InItemDetail->ItemName.ToString(), InTreasureBoxItem->ItemCount);
	
	// 设置奖励文本
	if (RewardText)
	{
		FString DisplayText = FString::Printf(TEXT("%s x%d"), *InItemDetail->ItemName.ToString(), InTreasureBoxItem->ItemCount);
		RewardText->SetText(FText::FromString(DisplayText));
	}
	
	// 设置奖励图标
	if (RewardImage)
	{
		RewardImage->SetBrushFromSoftTexture(InItemDetail->ItemIcon);
	}
	
	// 初始化复选框状态
	if (SelectionCheckBox)
	{
		SelectionCheckBox->SetIsChecked(false);
	}
}