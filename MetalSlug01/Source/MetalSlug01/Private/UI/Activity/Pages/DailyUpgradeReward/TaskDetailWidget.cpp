// Copyright Epic Games, Inc. All Rights Reserved.

#include "UI/Activity/Pages/DailyUpgradeReward/TaskDetailWidget.h"
#include "Components/TextBlock.h"
#include "Components/HorizontalBox.h"
#include "Components/Image.h"
#include "Components/Button.h"
#include "UI/Activity/Pages/ClaimBox/RewardOptionWidget.h"
#include "Kismet/GameplayStatics.h"
#include "UI/Activity/Core/UpgradeActivitySubsystem.h"
#include "Blueprint/UserWidget.h"
#include "UI/Activity/Core/ActivitySubsystem.h"

// UE 会自动生成构造函数，无需手动实现

void UTaskDetailWidget::SetupClaimButton(const FString& DayIdentifier, int32 TaskIndex, int32 CompleteCount, int32 RequiredCount)
{
	if (!ClaimButton)
	{
		UE_LOG(LogTemp, Warning, TEXT("UTaskDetailWidget: ClaimButton 未设置"));
		return;
	}
	
	UE_LOG(LogTemp, Log, TEXT("🔧 UTaskDetailWidget: 设置领取按钮 - Day:%s, TaskIndex:%d, Complete:%d, Required:%d"), 
		*DayIdentifier, TaskIndex, CompleteCount, RequiredCount);
	
	// 🔧 调试日志：确认接收到的任务完成数量
	UE_LOG(LogTemp, Log, TEXT("🔍 UTaskDetailWidget: 接收到任务完成数量 - Day:%s, TaskIndex:%d, Complete:%d"), *DayIdentifier, TaskIndex, CompleteCount);
	
	// 🔧 核心业务逻辑：获取 TaskClaimStatus
	UGameInstance* GameInstance = GetGameInstance();
	if (!GameInstance)
	{
		UE_LOG(LogTemp, Error, TEXT("UTaskDetailWidget: 无法获取 GameInstance"));
		return;
	}
	
	UUpgradeActivitySubsystem* Subsystem = GameInstance->GetSubsystem<UUpgradeActivitySubsystem>();
	if (!Subsystem)
	{
		UE_LOG(LogTemp, Error, TEXT("UTaskDetailWidget: 无法获取 UpgradeActivitySubsystem"));
		return;
	}
	
	// 从指定天数的记录中获取任务领取状态
	int32 DayNumber = FCString::Atoi(*DayIdentifier.RightChop(3)); // 从 "day1" 提取数字 1
	const FUpgradeRewardSaveRecord* DayRecord = Subsystem->GetRecordByDate(DayNumber);
	const TArray<int32>& TaskClaimStatus = DayRecord ? DayRecord->TaskClaimStatus : Subsystem->GetCurrentTaskClaimStatus();
	
	// 检查索引是否越界
	if (TaskIndex >= TaskClaimStatus.Num())
	{
		UE_LOG(LogTemp, Warning, TEXT("UTaskDetailWidget: TaskIndex(%d) 超出 TaskClaimStatus 数组范围 (%d)"), TaskIndex, TaskClaimStatus.Num());
		ClaimButton->SetVisibility(ESlateVisibility::Hidden);
		if (ClaimHintText)
		{
			ClaimHintText->SetText(FText::FromString(TEXT("")));
		}
		return;
	}
	
	int32 ClaimStatus = TaskClaimStatus[TaskIndex];
	UE_LOG(LogTemp, Log, TEXT("🔧 UTaskDetailWidget: TaskClaimStatus[%d] = %d"), TaskIndex, ClaimStatus);
	
	// 🔧 核心业务逻辑：判断按钮状态
	if (ClaimStatus == 1)
	{
		// ✅ 已领取：隐藏按钮和提示文本
		ClaimButton->SetVisibility(ESlateVisibility::Hidden);
		if (ClaimHintText)
		{
			ClaimHintText->SetText(FText::FromString(TEXT("")));
		}
		UE_LOG(LogTemp, Log, TEXT("🔧 UTaskDetailWidget: 按钮已隐藏（已领取）"));
	}
	else if (ClaimStatus == 0)
	{
		// 未领取，继续检查完成度
		bool bCanClaim = CompleteCount >= RequiredCount;
		
		// 🔧 核心业务逻辑：设置 ClaimHintText 文本
		if (ClaimHintText)
		{
			if (bCanClaim)
			{
				// ✅ 满足条件：显示“可领取”
				ClaimHintText->SetText(FText::FromString(TEXT("可领取")));
				UE_LOG(LogTemp, Log, TEXT("🔧 UTaskDetailWidget: ClaimHintText 显示“可领取”"));
			}
			else
			{
				// ❌ 不满足条件：显示“去完成”
				ClaimHintText->SetText(FText::FromString(TEXT("去完成")));
				UE_LOG(LogTemp, Log, TEXT("🔧 UTaskDetailWidget: ClaimHintText 显示“去完成”"));
			}
		}
		
		if (bCanClaim)
		{
			// ✅ 满足条件：设置按钮为黄色，启用点击事件
			ClaimButton->SetVisibility(ESlateVisibility::Visible);
			ClaimButton->SetIsEnabled(true);
			
			// 设置按钮颜色为黄色
			ClaimButton->SetColorAndOpacity(FLinearColor::Yellow);
			
			// 存储参数到成员变量（用于无参委托）
			CurrentDayIdentifier = DayIdentifier;
			CurrentTaskIndex = TaskIndex;
			
			// 绑定点击事件（使用无参 UFUNCTION 包装器）
			ClaimButton->OnClicked.AddDynamic(this, &UTaskDetailWidget::HandleClaimButtonClickWrapper);
			
			UE_LOG(LogTemp, Log, TEXT("🔧 UTaskDetailWidget: 按钮已启用（黄色），可点击"));
		}
		else
		{
			// ❌ 不满足条件：禁用按钮，设置为灰色
			ClaimButton->SetVisibility(ESlateVisibility::Visible);
			ClaimButton->SetIsEnabled(false);
			ClaimButton->SetColorAndOpacity(FLinearColor::Gray);
			
			UE_LOG(LogTemp, Log, TEXT("🔧 UTaskDetailWidget: 按钮已禁用（灰色），条件不满足（%d/%d）"), CompleteCount, RequiredCount);
		}
	}
	else
	{
		// 未知的 ClaimStatus 值
		UE_LOG(LogTemp, Warning, TEXT("🔧 UTaskDetailWidget: 未知的 ClaimStatus 值：%d"), ClaimStatus);
		ClaimButton->SetVisibility(ESlateVisibility::Hidden);
		if (ClaimHintText)
		{
			ClaimHintText->SetText(FText::FromString(TEXT("")));
		}
	}
}

/**
 * @brief 处理领取按钮点击事件 - 弹出 RewardOptionWidget 弹窗
 */
void UTaskDetailWidget::HandleClaimButtonClicked(const FString& DayIdentifier, int32 TaskIndex)
{
	UE_LOG(LogTemp, Log, TEXT("\n==========================================================="));
	UE_LOG(LogTemp, Log, TEXT("🎁 UTaskDetailWidget: 点击了领取按钮 - Day:%s, TaskIndex:%d"), *DayIdentifier, TaskIndex);
	
	// 获取 GameInstance
	UGameInstance* GameInstance = GetGameInstance();
	if (!GameInstance)
	{
		UE_LOG(LogTemp, Error, TEXT("UTaskDetailWidget: 无法获取 GameInstance"));
		return;
	}
	
	// 获取 UpgradeActivitySubsystem
	UUpgradeActivitySubsystem* Subsystem = GameInstance->GetSubsystem<UUpgradeActivitySubsystem>();
	if (!Subsystem)
	{
		UE_LOG(LogTemp, Error, TEXT("UTaskDetailWidget: 无法获取 UpgradeActivitySubsystem"));
		return;
	}
	
	// TODO: 调用 Subsystem 的方法获取奖励数据并弹出 RewardOptionWidget
	// 这里需要根据 DayIdentifier 和 TaskIndex 查询配置表获取奖励信息
	
	UE_LOG(LogTemp, Log, TEXT("🎁 UTaskDetailWidget: 准备弹出 RewardOptionWidget 弹窗（待实现）"));
	UE_LOG(LogTemp, Log, TEXT("===========================================================\n"));
}

/**
 * @brief 处理领取按钮点击事件的无参包装器（用于委托绑定）
 */
void UTaskDetailWidget::HandleClaimButtonClickWrapper()
{
	// 使用成员变量中存储的参数调用实际处理方法
	HandleClaimButtonClicked(CurrentDayIdentifier, CurrentTaskIndex);
}

/**
 * @brief 设置奖励展示容器内容
 * @param DayIdentifier 天数标识
 * @param TaskIndex 任务索引
 */
void UTaskDetailWidget::SetupRewardsContainer(const FString& DayIdentifier, int32 TaskIndex)
{
	if (!RewardsContainer)
	{
		UE_LOG(LogTemp, Warning, TEXT("UTaskDetailWidget: RewardsContainer 未设置"));
		return;
	}
	
	UE_LOG(LogTemp, Log, TEXT("🔧 UTaskDetailWidget: 设置奖励容器 - Day:%s, TaskIndex:%d"), *DayIdentifier, TaskIndex);
	
	// 清空现有内容
	RewardsContainer->ClearChildren();
	
	// 获取 Subsystem
	UGameInstance* GameInstance = GetGameInstance();
	if (!GameInstance)
	{
		UE_LOG(LogTemp, Error, TEXT("UTaskDetailWidget: 无法获取 GameInstance"));
		return;
	}
	
	UUpgradeActivitySubsystem* Subsystem = GameInstance->GetSubsystem<UUpgradeActivitySubsystem>();
	if (!Subsystem)
	{
		UE_LOG(LogTemp, Error, TEXT("UTaskDetailWidget: 无法获取 UpgradeActivitySubsystem"));
		return;
	}
	
	// 🔧 核心业务逻辑：获取配置行
	const FDailyUpgradeRewardConfigRow* ConfigRow = Subsystem->GetConfigRowForDay(DayIdentifier);
	if (!ConfigRow)
	{
		UE_LOG(LogTemp, Warning, TEXT("UTaskDetailWidget: 未找到配置数据 - Day:%s"), *DayIdentifier);
		return;
	}
	
	// 🔧 核心业务逻辑：检查 TaskIndex 是否越界
	if (TaskIndex >= ConfigRow->RewardItemIDs.Num())
	{
		UE_LOG(LogTemp, Warning, TEXT("UTaskDetailWidget: TaskIndex(%d) 超出 RewardItemIDs 数组范围 (%d)"), TaskIndex, ConfigRow->RewardItemIDs.Num());
		return;
	}
	
	// 🔧 核心业务逻辑：获取宝箱 ID 字符串（用逗号分隔）
	FString BoxIDString = ConfigRow->RewardItemIDs[TaskIndex];
	UE_LOG(LogTemp, Log, TEXT("🔧 UTaskDetailWidget: 原始宝箱 ID 字符串：%s (TaskIndex=%d, RewardItemIDs.Num=%d)"), 
		*BoxIDString, TaskIndex, ConfigRow->RewardItemIDs.Num());
	
	if (BoxIDString.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("🔧 UTaskDetailWidget: 宝箱 ID 字符串为空"));
		return;
	}
	
	// 🔧 核心业务逻辑：解析逗号分隔的宝箱 ID
	TArray<FString> BoxIDArray;
	BoxIDString.ParseIntoArray(BoxIDArray, TEXT(","));
	
	UE_LOG(LogTemp, Log, TEXT("🔧 UTaskDetailWidget: 解析到%d个宝箱 ID"), BoxIDArray.Num());
	for (int32 Idx = 0; Idx < BoxIDArray.Num(); ++Idx)
	{
		UE_LOG(LogTemp, Log, TEXT("  - BoxID[%d] = %s"), Idx, *BoxIDArray[Idx]);
	}
	
	// 🔧 核心业务逻辑：遍历宝箱 ID，加载对应的宝箱图标并创建 RewardIcon Widget
	for (int32 i = 0; i < BoxIDArray.Num(); ++i)
	{
		int32 BoxID = FCString::Atoi(*BoxIDArray[i]);
		UE_LOG(LogTemp, Log, TEXT("🔧 UTaskDetailWidget: 处理宝箱 ID %d"), BoxID);
		
		if (BoxID <= 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("UTaskDetailWidget: 无效的 BoxID: %d"), BoxID);
			continue;
		}
		
		// 🔧 核心业务逻辑：通过 BoxID 查找 TreasureBoxItemRow 配置
		UGameInstance* LocalGameInstance = GetGameInstance();
		if (!LocalGameInstance)
		{
			UE_LOG(LogTemp, Error, TEXT("UTaskDetailWidget: 无法获取 GameInstance"));
			continue;
		}
		
		UActivitySubsystem* ActivitySub = LocalGameInstance->GetSubsystem<UActivitySubsystem>();
		if (!ActivitySub)
		{
			UE_LOG(LogTemp, Warning, TEXT("UTaskDetailWidget: 无法获取 ActivitySubsystem"));
			continue;
		}
		
		const FTreasureBoxItemRow* TreasureBoxItem = ActivitySub->GetTreasureBoxItem(BoxID);
		if (!TreasureBoxItem || TreasureBoxItem->BoxIcon.IsNull())
		{
			UE_LOG(LogTemp, Warning, TEXT("UTaskDetailWidget: 未找到 BoxID %d 的有效宝箱图标"), BoxID);
			continue;
		}
		
		UE_LOG(LogTemp, Log, TEXT("🔧 UTaskDetailWidget: 获取到宝箱图标 - BoxID: %d, ItemID: %d"), 
			BoxID, TreasureBoxItem->ItemID);
		
		// 🔧 核心业务逻辑：加载宝箱图标纹理
		UTexture2D* BoxIconTexture = TreasureBoxItem->BoxIcon.LoadSynchronous();
		if (!BoxIconTexture)
		{
			UE_LOG(LogTemp, Warning, TEXT("UTaskDetailWidget: 宝箱图标加载失败 - BoxID: %d"), BoxID);
			continue;
		}
		
		// 🔧 核心业务逻辑：创建并配置 UImage控件
		UImage* RewardImage = NewObject<UImage>(RewardsContainer);
		if (!RewardImage)
		{
			UE_LOG(LogTemp, Error, TEXT("UTaskDetailWidget: 创建 UImage 失败"));
			continue;
		}
		
		RewardImage->SetBrushFromTexture(BoxIconTexture);
		RewardImage->SetDesiredSizeOverride(FVector2D(64.0f, 64.0f)); // 设置默认尺寸
		
		// 🔧 重要：将 Image 添加到容器
		UPanelSlot* AddedSlot = RewardsContainer->AddChild(RewardImage);
		if (AddedSlot)
		{
			UE_LOG(LogTemp, Log, TEXT("🔧 UTaskDetailWidget: 成功添加奖励图标 %d/%d 到容器"), i + 1, BoxIDArray.Num());
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("🔧 UTaskDetailWidget: AddChild 返回空指针，图标未添加"));
		}
	}
	
	UE_LOG(LogTemp, Log, TEXT("🔧 UTaskDetailWidget: 奖励容器设置完成"));
}