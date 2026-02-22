/**
 * @file DailyUpgradeRewardPage.cpp
 * @brief 每日升级奖励活动页面实现
 * @author AI Assistant
 * @date 2026
 * @version 1.0
 *
 * @details 实现每日升级奖励活动页面的核心功能
 */

#include "UI/Activity/Pages/DailyUpgradeReward/DailyUpgradeRewardPage.h"
#include "Components/HorizontalBox.h"
#include "Components/Image.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/VerticalBox.h"
#include "Components/ScrollBox.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/DataTable.h"
#include "UI/Activity/Core/ActivitySubsystem.h"
#include "UI/Activity/Data/DailyLoginConfig.h"
#include "UI/Activity/Pages/DailyUpgradeReward/ExperienceChestClaimWidget.h"

bool UDailyUpgradeRewardPage::Initialize()
{
	if (!Super::Initialize())
	{
		return false;
	}

	// 初始化默认值
	CurrentDayIndex = 1;
	CurrentExperience = 0;
	CurrentBonusMultiplier = 1.0f;

	// 初始化奖励物品图标数据
	InitializeRewardItemIcons();
	
	// 初始化宝箱数量显示（添加错误处理）
	if (ChestCountText)
	{
		UpdateChestCountText();
	}
	else
	{
		// 在编辑器预览模式下静默处理，不输出日志
	}

	// 初始化ItemsScrollBox中的ExperienceChestClaimWidget
	InitializeExperienceChestWidgets();

	return true;
}

void UDailyUpgradeRewardPage::NativeConstruct()
{
	Super::NativeConstruct();
}

void UDailyUpgradeRewardPage::NativeDestruct()
{
	Super::NativeDestruct();
}

void UDailyUpgradeRewardPage::InitializeRewardItemIcons()
{
	// 全局变量存储ItemIcon数据
	CachedItemIcons.Empty();

	// 1. 从DailyUpgradeRewardConfigRow表获取数据
	FString ConfigPath = TEXT("/Game/UI/Activity/Data/DT_DailyUpgradeRewardConfigRow");
	UDataTable* ConfigTable = LoadObject<UDataTable>(nullptr, *ConfigPath);

	if (!ConfigTable)
	{
		UE_LOG(LogTemp, Error, TEXT("DailyUpgradeRewardPage: 无法加载DT_DailyUpgradeRewardConfigRow表"));
		return;
	}

	// 获取ActivityID==110的数据
	static const FString ContextString(TEXT("DailyUpgradeRewardPage"));
	TMap<FName, uint8*> RowMap = ConfigTable->GetRowMap();
	
	if (RowMap.Num() == 0)
	{
		UE_LOG(LogTemp, Error, TEXT("DailyUpgradeRewardPage: DT_DailyUpgradeRewardConfigRow表为空"));
		return;
	}

	const FDailyUpgradeRewardConfigRow* TargetRow = nullptr;
	
	// 查找ActivityID==110的记录
	for (const auto& Pair : RowMap)
	{
		const FDailyUpgradeRewardConfigRow* Row = reinterpret_cast<const FDailyUpgradeRewardConfigRow*>(Pair.Value);
		if (Row && Row->ActivityID == 110)
		{
			TargetRow = Row;
			UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: 找到ActivityID=110的记录"));
			break;
		}
	}

	if (!TargetRow)
	{
		UE_LOG(LogTemp, Error, TEXT("DailyUpgradeRewardPage: 未找到ActivityID=110的记录"));
		return;
	}

	// 2. 检查RewardItemIDs数组是否为空
	if (TargetRow->RewardItemIDs.Num() == 0)
	{
		UE_LOG(LogTemp, Error, TEXT("DailyUpgradeRewardPage: RewardItemIDs数组为空"));
		return;
	}

	// 3. 获取最后一个索引的数据
	FString LastRewardItemID = TargetRow->RewardItemIDs.Last();
	UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: 最后一个RewardItemID: %s"), *LastRewardItemID);

	// 4. 将字符串转换为整数作为BoxID
	int32 BoxID = FCString::Atoi(*LastRewardItemID);
	if (BoxID <= 0)
	{
		UE_LOG(LogTemp, Error, TEXT("DailyUpgradeRewardPage: 无效的BoxID: %s"), *LastRewardItemID);
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: 转换后的BoxID: %d"), BoxID);

	// 5. 通过GameInstance获取ActivitySubsystem
	UGameInstance* GameInstance = GetGameInstance();
	if (!GameInstance)
	{
		// 在编辑器预览模式下静默处理，不输出日志
		CachedItemIcons.Empty();
		UpdateRewardItemImage();
		return;
	}

	UActivitySubsystem* ActivitySub = GameInstance->GetSubsystem<UActivitySubsystem>();
	if (!ActivitySub)
	{
		// 在编辑器预览模式下静默处理，不输出日志
		CachedItemIcons.Empty();
		UpdateRewardItemImage();
		return;
	}

	// 6. 根据BoxID遍历TreasureBoxItemRow表获取ItemID数据
	TArray<const FTreasureBoxItemRow*> TreasureBoxItems = ActivitySub->GetTreasureBoxItemsByBoxID(BoxID);
	if (TreasureBoxItems.Num() == 0)
	{
		UE_LOG(LogTemp, Error, TEXT("DailyUpgradeRewardPage: 未找到BoxID %d 的宝箱物品配置"), BoxID);
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: 找到%d个BoxID=%d的宝箱物品记录"), TreasureBoxItems.Num(), BoxID);

	// 7. 通过ItemID关联ItemDetailRow表获取ItemIcon数据
	for (const FTreasureBoxItemRow* TreasureBoxItem : TreasureBoxItems)
	{
		if (TreasureBoxItem)
		{
			const FItemDetailRow* ItemDetail = ActivitySub->GetItemDetail(TreasureBoxItem->ItemID);
			if (ItemDetail && !ItemDetail->ItemIcon.IsNull())
			{
				CachedItemIcons.Add(ItemDetail->ItemIcon);
				UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: 缓存ItemIcon - ItemID: %d, ItemName: %s"), 
					ItemDetail->ItemID, *ItemDetail->ItemName.ToString());
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("DailyUpgradeRewardPage: ItemID %d 的ItemIcon无效或为空"), TreasureBoxItem->ItemID);
			}
		}
	}

	UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: 缓存了 %d 个ItemIcon"), CachedItemIcons.Num());

	// 8. 更新RewardItemImage显示（默认显示第一个图标）
	UpdateRewardItemImage();
}

void UDailyUpgradeRewardPage::UpdateRewardItemImage()
{
	if (!RewardItemImage)
	{
		UE_LOG(LogTemp, Error, TEXT("DailyUpgradeRewardPage: RewardItemImage控件未绑定"));
		return;
	}

	if (CachedItemIcons.Num() > 0)
	{
		// 默认显示第一个索引的图标
		TSoftObjectPtr<UTexture2D> FirstIcon = CachedItemIcons[0];
		RewardItemImage->SetBrushFromSoftTexture(FirstIcon);
		UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: 设置RewardItemImage显示第一个图标"));
	}
	else
	{
		// 在编辑器预览模式下静默处理，不输出日志
		RewardItemImage->SetVisibility(ESlateVisibility::Hidden);
	}
}

void UDailyUpgradeRewardPage::UpdateChestCountText()
{
	if (!ChestCountText)
	{
		UE_LOG(LogTemp, Error, TEXT("DailyUpgradeRewardPage: ChestCountText控件未绑定"));
		return;
	}

	// 1. 从DailyUpgradeRewardConfigRow表获取数据
	FString ConfigPath = TEXT("/Game/UI/Activity/Data/DT_DailyUpgradeRewardConfigRow");
	UDataTable* ConfigTable = LoadObject<UDataTable>(nullptr, *ConfigPath);

	if (!ConfigTable)
	{
		UE_LOG(LogTemp, Error, TEXT("DailyUpgradeRewardPage: 无法加载DT_DailyUpgradeRewardConfigRow表"));
		ChestCountText->SetText(FText::FromString(TEXT("数据加载失败")));
		return;
	}

	// 2. 获取ActivityID==110的数据
	static const FString ContextString(TEXT("DailyUpgradeRewardPage"));
	TMap<FName, uint8*> RowMap = ConfigTable->GetRowMap();
	
	if (RowMap.Num() == 0)
	{
		UE_LOG(LogTemp, Error, TEXT("DailyUpgradeRewardPage: DT_DailyUpgradeRewardConfigRow表为空"));
		ChestCountText->SetText(FText::FromString(TEXT("无数据")));
		return;
	}

	const FDailyUpgradeRewardConfigRow* TargetRow = nullptr;
	
	// 查找ActivityID==110的记录
	for (const auto& Pair : RowMap)
	{
		const FDailyUpgradeRewardConfigRow* Row = reinterpret_cast<const FDailyUpgradeRewardConfigRow*>(Pair.Value);
		if (Row && Row->ActivityID == 110)
		{
			TargetRow = Row;
			UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: 找到ActivityID=110的记录"));
			break;
		}
	}

	if (!TargetRow)
	{
		UE_LOG(LogTemp, Error, TEXT("DailyUpgradeRewardPage: 未找到ActivityID=110的记录"));
		ChestCountText->SetText(FText::FromString(TEXT("未找到记录")));
		return;
	}

	// 3. 检查RewardItemCounts数组是否为空
	if (TargetRow->RewardItemCounts.Num() == 0)
	{
		UE_LOG(LogTemp, Error, TEXT("DailyUpgradeRewardPage: RewardItemCounts数组为空"));
		ChestCountText->SetText(FText::FromString(TEXT("0")));
		return;
	}

	// 4. 获取最后一个索引的数据
	FString LastRewardItemCount = TargetRow->RewardItemCounts.Last();
	UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: 最后一个RewardItemCount: %s"), *LastRewardItemCount);

	// 5. 显示在ChestCountText控件上（拼接X前缀）
	FString DisplayText = FString::Printf(TEXT("X%s"), *LastRewardItemCount);
	ChestCountText->SetText(FText::FromString(DisplayText));
	UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: ChestCountText已更新为: X%s"), *LastRewardItemCount);
}

void UDailyUpgradeRewardPage::InitializeExperienceChestWidgets()
{
	if (!ItemsScrollBox)
	{
		UE_LOG(LogTemp, Error, TEXT("DailyUpgradeRewardPage: ItemsScrollBox控件未绑定"));
		return;
	}

	// 清空现有的子控件
	ItemsScrollBox->ClearChildren();

	// 1. 从DailyUpgradeRewardConfigRow表获取数据
	FString ConfigPath = TEXT("/Game/UI/Activity/Data/DT_DailyUpgradeRewardConfigRow");
	UDataTable* ConfigTable = LoadObject<UDataTable>(nullptr, *ConfigPath);

	if (!ConfigTable)
	{
		UE_LOG(LogTemp, Error, TEXT("DailyUpgradeRewardPage: 无法加载DT_DailyUpgradeRewardConfigRow表"));
		return;
	}

	// 获取ActivityID==110的数据
	static const FString ContextString(TEXT("DailyUpgradeRewardPage"));
	TMap<FName, uint8*> RowMap = ConfigTable->GetRowMap();
	
	if (RowMap.Num() == 0)
	{
		UE_LOG(LogTemp, Error, TEXT("DailyUpgradeRewardPage: DT_DailyUpgradeRewardConfigRow表为空"));
		return;
	}

	const FDailyUpgradeRewardConfigRow* TargetRow = nullptr;
	
	// 查找ActivityID==110的记录
	for (const auto& Pair : RowMap)
	{
		const FDailyUpgradeRewardConfigRow* Row = reinterpret_cast<const FDailyUpgradeRewardConfigRow*>(Pair.Value);
		if (Row && Row->ActivityID == 110)
		{
			TargetRow = Row;
			UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: 找到ActivityID=110的记录"));
			break;
		}
	}

	if (!TargetRow)
	{
		UE_LOG(LogTemp, Error, TEXT("DailyUpgradeRewardPage: 未找到ActivityID=110的记录"));
		return;
	}

	// 2. 检查RewardItemIDs数组是否为空
	if (TargetRow->RewardItemIDs.Num() == 0)
	{
		UE_LOG(LogTemp, Error, TEXT("DailyUpgradeRewardPage: RewardItemIDs数组为空"));
		return;
	}

	// 3. 通过GameInstance获取ActivitySubsystem
	UGameInstance* GameInstance = GetGameInstance();
	if (!GameInstance)
	{
		UE_LOG(LogTemp, Warning, TEXT("DailyUpgradeRewardPage: 无法获取GameInstance，可能在编辑器预览模式下"));
		return;
	}

	UActivitySubsystem* ActivitySub = GameInstance->GetSubsystem<UActivitySubsystem>();
	if (!ActivitySub)
	{
		UE_LOG(LogTemp, Warning, TEXT("DailyUpgradeRewardPage: 无法获取ActivitySubsystem，可能在编辑器预览模式下"));
		return;
	}

	// 4. 遍历RewardItemIDs数组，为每个ID创建ExperienceChestClaimWidget
	TArray<TSoftObjectPtr<UTexture2D>> BoxIcons;
	
	for (const FString& RewardItemID : TargetRow->RewardItemIDs)
	{
		// 将字符串转换为整数作为BoxID
		int32 BoxID = FCString::Atoi(*RewardItemID);
		if (BoxID <= 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("DailyUpgradeRewardPage: 无效的BoxID: %s"), *RewardItemID);
			continue;
		}

		// 根据BoxID获取TreasureBoxItemRow数据
		TArray<const FTreasureBoxItemRow*> TreasureBoxItems = ActivitySub->GetTreasureBoxItemsByBoxID(BoxID);
		if (TreasureBoxItems.Num() > 0)
		{
			// 获取第一个记录的BoxIcon（假设每个BoxID对应一个宝箱图标）
			const FTreasureBoxItemRow* FirstItem = TreasureBoxItems[0];
			if (FirstItem)
			{
				BoxIcons.Add(FirstItem->BoxIcon);
				UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: 获取BoxID=%d的BoxIcon成功"), BoxID);
			}
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("DailyUpgradeRewardPage: 未找到BoxID %d 的宝箱物品配置"), BoxID);
		}
	}

	UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: 共获取到 %d 个BoxIcon"), BoxIcons.Num());

	// 5. 创建ExperienceChestClaimWidget并设置图标
	for (int32 i = 0; i < BoxIcons.Num(); ++i)
	{
		// 创建ExperienceChestClaimWidget实例
		UExperienceChestClaimWidget* ChestWidget = CreateWidget<UExperienceChestClaimWidget>(this, UExperienceChestClaimWidget::StaticClass());
		if (ChestWidget && ChestWidget->Initialize())
		{
			// 设置宝箱图标到ChestClaimButton
			if (ChestWidget->ChestClaimButton)
			{
				// 使用推荐的API设置按钮背景图片
				FSlateBrush ButtonBrush;
				ButtonBrush.SetResourceObject(BoxIcons[i].Get());
				ButtonBrush.DrawAs = ESlateBrushDrawType::Image;
				
				// 使用现代API设置按钮背景图片
				FButtonStyle NewStyle = ChestWidget->ChestClaimButton->GetStyle();
				NewStyle.SetNormal(ButtonBrush);
				NewStyle.SetPressed(ButtonBrush);
				NewStyle.SetHovered(ButtonBrush);
				NewStyle.SetDisabled(ButtonBrush);
				ChestWidget->ChestClaimButton->SetStyle(NewStyle);
			}
			
			// 添加到ItemsScrollBox
			ItemsScrollBox->AddChild(ChestWidget);
			UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: 成功创建第%d个ExperienceChestClaimWidget"), i + 1);
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("DailyUpgradeRewardPage: 创建ExperienceChestClaimWidget失败"));
		}
	}
}
