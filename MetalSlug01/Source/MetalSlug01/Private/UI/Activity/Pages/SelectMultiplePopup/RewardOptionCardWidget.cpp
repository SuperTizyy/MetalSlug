// RewardOptionCardWidget.cpp
#include "UI/Activity/Pages/SelectMultiplePopup/RewardOptionCardWidget.h"
#include "UI/Activity/Core/ActivitySubsystem.h"
#include "UI/Activity/Core/UpgradeActivitySubsystem.h"
#include "Kismet/GameplayStatics.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Components/CheckBox.h"
#include "Engine/GameInstance.h"
#include "UI/Activity/Data/DailyLoginConfig.h"

void URewardOptionCardWidget::NativeConstruct()
{
	UE_LOG(LogTemp, Log, TEXT("\n==========================================================="));
	UE_LOG(LogTemp, Log, TEXT("🔄 REWARD_OPTION_CARD_WIDGET_CONSTRUCT_START"));
	UE_LOG(LogTemp, Log, TEXT("🆔 卡片Widget地址: %p"), this);
	UE_LOG(LogTemp, Log, TEXT("📋 卡片索引: %d"), CardIndex);
	UE_LOG(LogTemp, Log, TEXT("⏰ 构造时间: %s"), *FDateTime::Now().ToString());
	UE_LOG(LogTemp, Log, TEXT("===========================================================\n"));
	
	Super::NativeConstruct();

	// 绑定复选框状态改变事件
	if (SelectionCheckBox)
	{
		SelectionCheckBox->OnCheckStateChanged.AddDynamic(this, &URewardOptionCardWidget::OnCheckBoxStateChanged);
	}
	
	// 订阅UpgradeActivitySubsystem的奖励图标索引更新事件
	SubscribeToRewardIconEvents();
	
	UE_LOG(LogTemp, Log, TEXT("\n==========================================================="));
	UE_LOG(LogTemp, Log, TEXT("✅ REWARD_OPTION_CARD_WIDGET_CONSTRUCT_END"));
	UE_LOG(LogTemp, Log, TEXT("🆔 最终Widget地址: %p"), this);
	UE_LOG(LogTemp, Log, TEXT("===========================================================\n"));
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

void URewardOptionCardWidget::InitializeCardWithDirectData(const FItemDetailRow* InItemDetail, const FTreasureBoxItemRow* InTreasureBoxItem, int32 InCardIndex)
{
	if (!InItemDetail || !InTreasureBoxItem)
	{
		UE_LOG(LogTemp, Error, TEXT("RewardOptionCardWidget: InitializeCardWithDirectData - 传入的指针为空"));
		return;
	}

	RewardID = InItemDetail->ItemID;
	RewardCount = InTreasureBoxItem->ItemCount;
	CardIndex = InCardIndex;
	bIsSelected = false;

	UE_LOG(LogTemp, Log, TEXT("RewardOptionCardWidget: 使用直接数据初始化卡片，ItemID: %d, ItemCount: %d, CardIndex: %d"), 
		RewardID, RewardCount, CardIndex);

	// 设置奖励文本 - 只显示数量
	if (RewardText)
	{
		FString DisplayText = FString::Printf(TEXT("X%d"), RewardCount);
		RewardText->SetText(FText::FromString(DisplayText));
	}

	// 设置奖励图标
	if (RewardImage && !InItemDetail->ItemIcon.IsNull())
	{
		RewardImage->SetBrushFromSoftTexture(InItemDetail->ItemIcon);
	}

	// 初始化复选框状态
	if (SelectionCheckBox)
	{
		SelectionCheckBox->SetIsChecked(false);
		SelectionCheckBox->OnCheckStateChanged.AddDynamic(this, &URewardOptionCardWidget::OnCheckBoxStateChanged);
	}

	// 订阅奖励图标索引更新事件
	SubscribeToRewardIconEvents();
}

void URewardOptionCardWidget::SubscribeToRewardIconEvents()
{
	// 通过GameInstance获取UpgradeActivitySubsystem
	UGameInstance* GameInstance = GetGameInstance();
	if (!GameInstance)
	{
		UE_LOG(LogTemp, Error, TEXT("RewardOptionCardWidget: 无法获取GameInstance"));
		return;
	}
	
	UUpgradeActivitySubsystem* UpgradeSub = GameInstance->GetSubsystem<UUpgradeActivitySubsystem>();
	if (!UpgradeSub)
	{
		UE_LOG(LogTemp, Error, TEXT("RewardOptionCardWidget: 无法获取UpgradeActivitySubsystem"));
		return;
	}
	
	// 订阅奖励图标索引更新事件
	UpgradeSub->OnRewardIconIndexChanged.AddDynamic(this, &URewardOptionCardWidget::OnRewardIconIndexChanged);
	
	UE_LOG(LogTemp, Log, TEXT("RewardOptionCardWidget: 已订阅奖励图标索引更新事件 - 卡片索引: %d"), CardIndex);
}

void URewardOptionCardWidget::OnRewardIconIndexChanged(int32 NewIndex)
{
	UE_LOG(LogTemp, Log, TEXT("RewardOptionCardWidget: 收到奖励图标索引更新事件 - 新索引: %d, 当前卡片索引: %d"), 
		NewIndex, CardIndex);
	
	// 检查当前卡片是否应该被选中
	bool bShouldBeSelected = (CardIndex == NewIndex);
	
	if (bIsSelected != bShouldBeSelected)
	{
		UE_LOG(LogTemp, Log, TEXT("RewardOptionCardWidget: 更新选中状态 - 从 %s 变为 %s"), 
			bIsSelected ? TEXT("选中") : TEXT("未选中"),
			bShouldBeSelected ? TEXT("选中") : TEXT("未选中"));
		
		SetSelected(bShouldBeSelected);
	}
}

void URewardOptionCardWidget::InitializeCardWithDataTablesAndSelection(int32 InItemID, int32 InBoxID, int32 InCardIndex, bool bShouldBeSelected)
{
	RewardID = InItemID;
	CardIndex = InCardIndex;
	bIsSelected = bShouldBeSelected;

	UE_LOG(LogTemp, Log, TEXT("RewardOptionCardWidget: 初始化卡片 %d，ItemID: %d, 应该选中: %s"), 
		CardIndex, InItemID, bShouldBeSelected ? TEXT("是") : TEXT("否"));

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

	// 获取UpgradeActivitySubsystem以读取当前选中的RewardIconIndex
	UUpgradeActivitySubsystem* UpgradeSub = GameInstance->GetSubsystem<UUpgradeActivitySubsystem>();
	if (UpgradeSub)
	{
		int32 CurrentRewardIconIndex = UpgradeSub->GetCurrentRewardIconIndex();
		UE_LOG(LogTemp, Log, TEXT("RewardOptionCardWidget: 当前RewardIconIndex为 %d，当前卡片索引为 %d"), CurrentRewardIconIndex, CardIndex);
		
		// 验证传入的选中状态是否与当前RewardIconIndex一致
		if (bShouldBeSelected && CardIndex != CurrentRewardIconIndex)
		{
			UE_LOG(LogTemp, Warning, TEXT("RewardOptionCardWidget: 选中状态与当前RewardIconIndex不匹配，将使用传入的选中状态"));
		}
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
		
		// 根据需求，RewardText只显示ItemCount字段数据（数字），前面加X
		FString DisplayText = FString::Printf(TEXT("X%d"), TreasureBoxItem->ItemCount);
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
		SelectionCheckBox->SetIsChecked(bIsSelected);
		UE_LOG(LogTemp, Log, TEXT("RewardOptionCardWidget: 复选框最终状态设置为 %s"), bIsSelected ? TEXT("选中") : TEXT("未选中"));
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

void URewardOptionCardWidget::InitializeCardWithDataTables(int32 InItemID, int32 InBoxID)
{
	RewardID = InItemID;
	
	UE_LOG(LogTemp, Log, TEXT("RewardOptionCardWidget: 初始化卡片，ItemID: %d, BoxID: %d"), InItemID, InBoxID);

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
	if (TreasureBoxItem)
	{
		// 使用正确的格式：物品名称 X 数量
		FString DisplayText = FString::Printf(TEXT("%s X%d"), *ItemDetail->ItemName.ToString(), TreasureBoxItem->ItemCount);
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

	// 初始化复选框为未选中状态
	if (SelectionCheckBox)
	{
		SelectionCheckBox->SetIsChecked(false);
		bIsSelected = false;
	}
}

void URewardOptionCardWidget::OnCheckBoxStateChanged(bool bIsChecked)
{
	bIsSelected = bIsChecked;
	
	UE_LOG(LogTemp, Log, TEXT("RewardOptionCardWidget: 复选框状态改变 - ID: %d, 选中: %s, 索引: %d"), 
		RewardID, bIsChecked ? TEXT("是") : TEXT("否"), CardIndex);
	
	// 注意：这里只更新本地状态，不立即更新Subsystem数据
	// 数据更新应该在用户点击ConfirmButton时进行
	
	// 广播选中事件
	OnRewardSelected.Broadcast(this, bIsChecked);
}
