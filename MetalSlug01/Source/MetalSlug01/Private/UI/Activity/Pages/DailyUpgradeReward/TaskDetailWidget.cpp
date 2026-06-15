// 版权声明：在项目设置的描述页面填写您的版权信息。

// ==========================================
// 头文件包含区
// ==========================================
#include "UI/Activity/Pages/DailyUpgradeReward/TaskDetailWidget.h"
#include "Components/TextBlock.h"
#include "Components/HorizontalBox.h"
#include "Components/Image.h"
#include "Components/Button.h"
#include "Components/CanvasPanelSlot.h"
#include "Styling/SlateTypes.h"
#include "UI/Activity/Pages/ClaimBox/RewardOptionWidget.h"
#include "Kismet/GameplayStatics.h"
#include "Systems/Activity/UpgradeActivitySubsystem.h"
#include "Blueprint/UserWidget.h"
#include "Systems/Activity/ActivitySubsystem.h"
#include "UI/Activity/Pages/DailyUpgradeReward/TaskDetailWidget.h"
#include "Data/Tables/DailyLoginTableRow.h"

// UE 会自动生成构造函数，无需手动实现


// ==========================================
// 1. 公共接口 - SetupClaimButton
// ==========================================

/**
 * UTaskDetailWidget::SetupClaimButton
 *
 * 1. 校验 ClaimButton
 * 2. 防御链: GameInstance / UUpgradeActivitySubsystem
 * 3. 解析 DayIdentifier (RightChop(3)) 提取 DayNumber
 * 4. 读取 TaskClaimStatus[TaskIndex]（可能越界 -> 隐藏所有）
 * 5. 状态机:
 *    - Claimed (=1): 隐藏按钮+提示, 显示成功图标
 *    - Unclaimed + 满足条件: 按钮黄色 + "可领取"
 *    - Unclaimed + 未满足: 按钮灰色 + "去完成"
 *    - Invalid: 全部隐藏
 * 6. 绑定 HandleClaimButtonClickWrapper（无参）
 *
 * @param DayIdentifier 天数标识
 * @param TaskIndex 任务索引
 * @param CompleteCount 当前完成次数
 * @param RequiredCount 需要完成的次数
 */
void UTaskDetailWidget::SetupClaimButton(const FString& DayIdentifier, int32 TaskIndex, int32 CompleteCount, int32 RequiredCount)
{
	if (!ClaimButton)
	{
		UE_LOG(LogTemp, Warning, TEXT("UTaskDetailWidget: ClaimButton 未设置"));
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("🔧 UTaskDetailWidget: 设置领取按钮 - Day:%s, TaskIndex:%d, Complete:%d, Required:%d"),
		*DayIdentifier, TaskIndex, CompleteCount, RequiredCount);

	// 🔧 调试日志: 确认接收到的任务完成数量
	UE_LOG(LogTemp, Log, TEXT("🔍 UTaskDetailWidget: 接收到任务完成数量 - Day:%s, TaskIndex:%d, Complete:%d"), *DayIdentifier, TaskIndex, CompleteCount);

	// 🔧 核心业务逻辑: 获取 TaskClaimStatus
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
		UE_LOG(LogTemp, Warning, TEXT("[HIDE_DEBUG] ⚠️ TaskIndex(%d) 超出 TaskClaimStatus 数组范围 (%d)"), TaskIndex, TaskClaimStatus.Num());
		ClaimButton->SetVisibility(ESlateVisibility::Collapsed);
		if (ClaimHintText)
		{
			ClaimHintText->SetText(FText::FromString(TEXT("")));
			ClaimHintText->SetVisibility(ESlateVisibility::Collapsed);
		}
		if (ClaimSuccessImage)
		{
			ClaimSuccessImage->SetVisibility(ESlateVisibility::Collapsed);
		}
		UE_LOG(LogTemp, Log, TEXT("[HIDE_DEBUG] ⚠️ UI状态已处理（越界情况）"));
		return;
	}

	int32 ClaimStatus = TaskClaimStatus[TaskIndex];
	UE_LOG(LogTemp, Log, TEXT("🔧 UTaskDetailWidget: TaskClaimStatus[%d] = %d"), TaskIndex, ClaimStatus);

	// 🔥 强制检查: 如果已领取，立即设置 UI 状态
	if (ClaimStatus == 1)
	{
		UE_LOG(LogTemp, Log, TEXT("[HIDE_DEBUG] 🎯 已领取状态检测到! ClaimStatus=1, Day=%s, TaskIndex=%d"), *DayIdentifier, TaskIndex);
		// 隐藏按钮和提示文本（不占位）
		if (ClaimButton)
		{
			ClaimButton->SetVisibility(ESlateVisibility::Collapsed);
		}
		if (ClaimHintText)
		{
			ClaimHintText->SetText(FText::FromString(TEXT("")));
			ClaimHintText->SetVisibility(ESlateVisibility::Collapsed);
		}
		// 显示成功图标
		if (ClaimSuccessImage)
		{
			ClaimSuccessImage->SetVisibility(ESlateVisibility::Visible);
			UE_LOG(LogTemp, Log, TEXT("[HIDE_DEBUG] ✅ ClaimSuccessImage 设置为 Visible"));
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("[HIDE_DEBUG] ❌ ClaimSuccessImage 为空指针!"));
		}

		UE_LOG(LogTemp, Log, TEXT("[HIDE_DEBUG] 🎯 UI状态更新完成!"));
		return;
	}

	// 🔧 核心业务逻辑: 判断按钮状态
	// ClaimStatus == 1 的情况已经在上面处理过了
	if (ClaimStatus == 0)
	{
		// 未领取，继续检查完成度
		bool bCanClaim = CompleteCount >= RequiredCount;

		// 🔧 核心业务逻辑: 设置 ClaimHintText 文本
		if (ClaimHintText)
		{
			if (bCanClaim)
			{
				// ✅ 满足条件: 显示"可领取"
				ClaimHintText->SetText(FText::FromString(TEXT("可领取")));
				// 设置字体颜色为黑色
				FSlateColor BlackColor(FLinearColor::Black);
				ClaimHintText->SetColorAndOpacity(BlackColor);
				UE_LOG(LogTemp, Log, TEXT("[HIDE_DEBUG] 🟢 ClaimHintText 显示「可领取」，字体颜色设为黑色"));
			}
			else
			{
				// ❌ 不满足条件: 显示"去完成"
				ClaimHintText->SetText(FText::FromString(TEXT("去完成")));
				// 可以保持默认颜色或设置其他颜色
				UE_LOG(LogTemp, Log, TEXT("[HIDE_DEBUG] 🔴 ClaimHintText 显示「去完成」"));
			}
		}

		if (bCanClaim)
		{
			// ✅ 满足条件: 设置按钮为黄色，启用点击事件
			ClaimButton->SetVisibility(ESlateVisibility::Visible);
			ClaimButton->SetIsEnabled(true);

			// 设置按钮颜色为黄色 - 使用 GetStyle 和 SetStyle 方法
			FButtonStyle YellowStyle = ClaimButton->GetStyle();
			YellowStyle.Normal.TintColor = FLinearColor::Yellow;
			ClaimButton->SetStyle(YellowStyle);

			// 隐藏 ClaimSuccessImage
			if (ClaimSuccessImage)
			{
				ClaimSuccessImage->SetVisibility(ESlateVisibility::Collapsed);
				UE_LOG(LogTemp, Log, TEXT("[HIDE_DEBUG] 🟡 ClaimSuccessImage 已隐藏"));
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("[HIDE_DEBUG] ❌ ClaimSuccessImage 为空指针（启用状态）!"));
			}

			// 也存储到 RewardOptionWidget 的上下文中，用于事件处理
			CurrentTaskIndexForReward = TaskIndex;

			// 存储参数到成员变量（用于无参委托）
			CurrentDayIdentifier = DayIdentifier;
			CurrentTaskIndex = TaskIndex;

			// 绑定点击事件（使用无参 UFUNCTION 包装器）
			ClaimButton->OnClicked.AddDynamic(this, &UTaskDetailWidget::HandleClaimButtonClickWrapper);

			UE_LOG(LogTemp, Log, TEXT("[HIDE_DEBUG] 🟡 按钮已启用（黄色），可点击"));
		}
		else
		{
			// ❌ 不满足条件: 禁用按钮，设置为灰色
			ClaimButton->SetVisibility(ESlateVisibility::Visible);
			ClaimButton->SetIsEnabled(false);
			FButtonStyle GrayStyle = ClaimButton->GetStyle();
			GrayStyle.Normal.TintColor = FLinearColor::Gray;
			ClaimButton->SetStyle(GrayStyle);

			// 隐藏 ClaimSuccessImage
			if (ClaimSuccessImage)
			{
				ClaimSuccessImage->SetVisibility(ESlateVisibility::Collapsed);
				UE_LOG(LogTemp, Log, TEXT("[HIDE_DEBUG] ⚪ ClaimSuccessImage 已隐藏"));
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("[HIDE_DEBUG] ❌ ClaimSuccessImage 为空指针（禁用状态）!"));
			}

			UE_LOG(LogTemp, Log, TEXT("[HIDE_DEBUG] ⚪ 按钮已禁用（灰色），条件不满足（%d/%d）"), CompleteCount, RequiredCount);
		}
	}
	else
	{
		// ClaimStatus 既不是 0 也不是 1，可能是无效值
		UE_LOG(LogTemp, Warning, TEXT("[HIDE_DEBUG] ⚠️ 无效的 ClaimStatus 值: %d"), ClaimStatus);
		ClaimButton->SetVisibility(ESlateVisibility::Collapsed);
		if (ClaimHintText)
		{
			ClaimHintText->SetText(FText::FromString(TEXT("")));
			ClaimHintText->SetVisibility(ESlateVisibility::Collapsed);
		}
		if (ClaimSuccessImage)
		{
			ClaimSuccessImage->SetVisibility(ESlateVisibility::Collapsed);
		}
	}
}


// ==========================================
// 2. 内部回调 - 领取按钮点击
// ==========================================

/**
 * UTaskDetailWidget::HandleClaimButtonClicked
 *
 * 1. 防御链: GameInstance / UUpgradeActivitySubsystem
 * 2. 校验 RewardOptionWidgetClass
 * 3. 读取 ConfigRow = GetConfigRowForDay(DayIdentifier)
 * 4. 准备 RewardOptions（单条, 取第一个 RewardItemID）
 * 5. CreateWidget<URewardOptionWidget> + InitSelection
 * 6. 绑定 OnStoreToBag -> HandleRewardStore
 * 7. AddToViewport(1000) 高 Z-order
 *
 * @param DayIdentifier 天数标识
 * @param TaskIndex 任务索引
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

	// 检查 RewardOptionWidgetClass 是否设置
	if (!RewardOptionWidgetClass)
	{
		UE_LOG(LogTemp, Error, TEXT("UTaskDetailWidget: RewardOptionWidgetClass 未设置! 请在蓝图中指定 RewardOptionWidget 类"));
		return;
	}

	// 获取配置数据
	const FDailyUpgradeRewardConfigRow* ConfigRow = Subsystem->GetConfigRowForDay(DayIdentifier);
	if (!ConfigRow)
	{
		UE_LOG(LogTemp, Error, TEXT("UTaskDetailWidget: 无法获取配置数据 - Day:%s"), *DayIdentifier);
		return;
	}

	// 准备奖励选项数据
	TArray<FDailyLoginConfigRow> RewardOptions;
	FDailyLoginConfigRow TempRow;
	TempRow.ActivityID = ConfigRow->ActivityID;

	// 设置任务索引（用于任务领取）
	TempRow.DayIndex = TaskIndex;

	// 使用对应的 RewardItemID
	if (ConfigRow->RewardItemIDs.IsValidIndex(TaskIndex))
	{
		FString RewardItemIDString = ConfigRow->RewardItemIDs[TaskIndex];
		// 解析逗号分隔的 ID 列表，取第一个作为示例
		TArray<FString> IDArray;
		RewardItemIDString.ParseIntoArray(IDArray, TEXT(","));
		if (IDArray.Num() > 0)
		{
			TempRow.RewardItemID = FCString::Atoi(*IDArray[0]);
			UE_LOG(LogTemp, Log, TEXT("UTaskDetailWidget: 使用RewardItemID: %d"), TempRow.RewardItemID);
		}
	}

	RewardOptions.Add(TempRow);

	// 创建 RewardOptionWidget 实例
	URewardOptionWidget* RewardOptionWidget = CreateWidget<URewardOptionWidget>(GetWorld(), RewardOptionWidgetClass);
	if (!RewardOptionWidget)
	{
		UE_LOG(LogTemp, Error, TEXT("UTaskDetailWidget: 无法创建 RewardOptionWidget 实例"));
		return;
	}

	// 初始化弹窗
	RewardOptionWidget->InitSelection(RewardOptions);

	// 绑定 StoreBtn 事件
	RewardOptionWidget->OnStoreToBag.Clear();
	RewardOptionWidget->OnStoreToBag.AddDynamic(this, &UTaskDetailWidget::HandleRewardStore);

	// 添加到视口并显示
	RewardOptionWidget->AddToViewport(1000); // 使用高 Z-order 确保显示在最上层

	UE_LOG(LogTemp, Log, TEXT("🎁 UTaskDetailWidget: 成功弹出 RewardOptionWidget 弹窗"));
	UE_LOG(LogTemp, Log, TEXT("===========================================================\n"));
}


/**
 * UTaskDetailWidget::HandleClaimButtonClickWrapper
 *
 * 无参包装器（用于委托绑定）
 * 使用成员变量中存储的参数调用实际处理方法
 */
void UTaskDetailWidget::HandleClaimButtonClickWrapper()
{
	// 使用成员变量中存储的参数调用实际处理方法
	this->HandleClaimButtonClicked(CurrentDayIdentifier, CurrentTaskIndex);
}


// ==========================================
// 3. 内部回调 - 存储到背包
// ==========================================

/**
 * UTaskDetailWidget::HandleRewardStore
 *
 * 1. 防御链: GameInstance / UUpgradeActivitySubsystem
 * 2. 使用 CurrentTaskIndexForReward 锁定任务索引
 * 3. 解析 DayNumber
 * 4. DayRecord = GetRecordByDate
 * 5. 创建可修改副本 FUpgradeRewardSaveRecord MutableRecord
 * 6. while 确保 TaskClaimStatus 数组足够大
 * 7. TaskClaimStatus[ActualTaskIndex] = 1
 * 8. LastUpdateTime = Now
 * 9. AddOrUpdateRecord 保存
 * 10. 同步 CurrentRecord（如匹配）
 * 11. UI: 隐藏按钮+提示, 显示成功图标
 *
 * @param TaskIndex 任务索引（从 RewardOptionWidget 传递, 实际使用 CurrentTaskIndexForReward）
 */
void UTaskDetailWidget::HandleRewardStore(int32 TaskIndex)
{
	UE_LOG(LogTemp, Log, TEXT("[HIDE_DEBUG] 🎯 HandleRewardStore 被调用! TaskIndex=%d"), TaskIndex);

	// 获取 GameInstance
	UGameInstance* GameInstance = GetGameInstance();
	if (!GameInstance)
	{
		UE_LOG(LogTemp, Error, TEXT("[HIDE_DEBUG] ❌ 无法获取 GameInstance"));
		return;
	}

	// 获取 UpgradeActivitySubsystem
	UUpgradeActivitySubsystem* Subsystem = GameInstance->GetSubsystem<UUpgradeActivitySubsystem>();
	if (!Subsystem)
	{
		UE_LOG(LogTemp, Error, TEXT("[HIDE_DEBUG] ❌ 无法获取 UpgradeActivitySubsystem"));
		return;
	}



	// 使用 CurrentTaskIndexForReward 确保正确的任务索引
	int32 ActualTaskIndex = CurrentTaskIndexForReward;
	UE_LOG(LogTemp, Log, TEXT("[HIDE_DEBUG] 🎯 实际任务索引: %d"), ActualTaskIndex);

	// 直接更新 TaskClaimStatus 而不通过 ClaimTaskReward
	const FUpgradeRewardSaveRecord* DayRecord = nullptr;
	int32 DayNumber = FCString::Atoi(*CurrentDayIdentifier.RightChop(3)); // 从 "day1" 提取数字 1
	DayRecord = Subsystem->GetRecordByDate(DayNumber);

	bool bClaimSuccess = false;

	if (DayRecord)
	{
		// 创建可修改的记录副本
		FUpgradeRewardSaveRecord MutableRecord = *DayRecord;

		// 确保 TaskClaimStatus 数组足够大
		while (MutableRecord.TaskClaimStatus.Num() <= ActualTaskIndex)
		{
			MutableRecord.TaskClaimStatus.Add(0);
		}

		// 设置为已领取
		MutableRecord.TaskClaimStatus[ActualTaskIndex] = 1;
		MutableRecord.LastUpdateTime = FDateTime::Now();

		// 更新到子系统
		Subsystem->AddOrUpdateRecord(DayNumber, MutableRecord);

		// 如果这是当前记录，也更新 CurrentRecord
		if (DayNumber == Subsystem->GetRecord().GetDayNumber())
		{
			Subsystem->GetRecord() = MutableRecord;
		}

		// 注意: 不在游戏运行时保存到本地磁盘，游戏关闭时统一保存

		bClaimSuccess = true;
		UE_LOG(LogTemp, Log, TEXT("[HIDE_DEBUG] 🎯 直接更新成功! Day=%d, TaskIndex=%d"), DayNumber, ActualTaskIndex);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[HIDE_DEBUG] ❌ 无法找到天数记录 Day=%d"), DayNumber);
	}

	if (bClaimSuccess)
	{

		// 强制更新 UI 状态
		UE_LOG(LogTemp, Log, TEXT("[HIDE_DEBUG] 🎯 强制更新 UI 状态"));

		// 隐藏按钮和提示文本（不占位）
		if (ClaimButton)
		{
			ClaimButton->SetVisibility(ESlateVisibility::Collapsed);
		}
		if (ClaimHintText)
		{
			ClaimHintText->SetText(FText::FromString(TEXT("")));
			ClaimHintText->SetVisibility(ESlateVisibility::Collapsed);
		}

		// 显示成功图标
		if (ClaimSuccessImage)
		{
			ClaimSuccessImage->SetVisibility(ESlateVisibility::Visible);
			UE_LOG(LogTemp, Log, TEXT("[HIDE_DEBUG] ✅ ClaimSuccessImage 设置为 Visible"));
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("[HIDE_DEBUG] ❌ ClaimSuccessImage 为空指针!"));
		}

		UE_LOG(LogTemp, Log, TEXT("[HIDE_DEBUG] 🎯 UI 更新完成!"));
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("[HIDE_DEBUG] ❌ 领取任务奖励失败 - TaskIndex:%d"), ActualTaskIndex);
	}
}


// ==========================================
// 4. 公共接口 - SetupRewardsContainer
// ==========================================

/**
 * UTaskDetailWidget::SetupRewardsContainer
 *
 * 1. 防御链
 * 2. 清空 RewardsContainer
 * 3. 校验 RewardIconClass + GameInstance + UUpgradeActivitySubsystem + ConfigRow
 * 4. 校验 TaskIndex 范围
 * 5. 解析 BoxIDString（逗号分隔）
 * 6. 解析 TaskRewardCounts（"1,1" -> ["1","1"]）
 * 7. 遍历 BoxIDArray:
 *    a. BoxID = Atoi
 *    b. GetTreasureBoxItem(BoxID) -> BoxIcon
 *    c. 加载 BoxIconTexture (LoadSynchronous)
 *    d. CreateWidget<URewardIconClass>
 *    e. GetWidgetFromName("RewardImage"/"CountText")
 *    f. SetBrushFromTexture + 80x80
 *    g. CountText "X{count}" 或 Collapsed
 *    h. AddChild
 *
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

	// 检查 RewardIconClass 是否设置
	if (RewardIconClass == nullptr)
	{
		UE_LOG(LogTemp, Error, TEXT("UTaskDetailWidget: RewardIconClass 未设置! 请在蓝图中指定 WBP_RewardIcon 类"));
		return;
	}

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

	// 🔧 核心业务逻辑: 获取配置行
	const FDailyUpgradeRewardConfigRow* ConfigRow = Subsystem->GetConfigRowForDay(DayIdentifier);
	if (!ConfigRow)
	{
		UE_LOG(LogTemp, Warning, TEXT("UTaskDetailWidget: 未找到配置数据 - Day:%s"), *DayIdentifier);
		return;
	}

	// 🔧 核心业务逻辑: 检查 TaskIndex 是否越界
	if (TaskIndex >= ConfigRow->RewardItemIDs.Num())
	{
		UE_LOG(LogTemp, Warning, TEXT("UTaskDetailWidget: TaskIndex(%d) 超出 RewardItemIDs 数组范围 (%d)"), TaskIndex, ConfigRow->RewardItemIDs.Num());
		return;
	}

	// 🔧 核心业务逻辑: 获取宝箱 ID 字符串（用逗号分隔）
	FString BoxIDString = ConfigRow->RewardItemIDs[TaskIndex];
	UE_LOG(LogTemp, Log, TEXT("🔧 UTaskDetailWidget: 原始宝箱 ID 字符串: %s (TaskIndex=%d, RewardItemIDs.Num=%d)"),
		*BoxIDString, TaskIndex, ConfigRow->RewardItemIDs.Num());

	if (BoxIDString.IsEmpty())
	{
		UE_LOG(LogTemp, Warning, TEXT("🔧 UTaskDetailWidget: 宝箱 ID 字符串为空"));
		return;
	}

	// 🔧 核心业务逻辑: 解析逗号分隔的宝箱 ID
	TArray<FString> BoxIDArray;
	BoxIDString.ParseIntoArray(BoxIDArray, TEXT(","));

	UE_LOG(LogTemp, Log, TEXT("🔧 UTaskDetailWidget: 解析到%d个宝箱 ID"), BoxIDArray.Num());
	for (int32 Idx = 0; Idx < BoxIDArray.Num(); ++Idx)
	{
		UE_LOG(LogTemp, Log, TEXT("  - BoxID[%d] = %s"), Idx, *BoxIDArray[Idx]);
	}

	// 🔧 获取指定天数和任务索引的奖励数量数据
	TArray<FString> TaskRewardCounts; // 当前任务的数量数组

	// 🔧 检查 TaskIndex 是否在 RewardItemCounts 范围内
	if (TaskIndex < ConfigRow->RewardItemCounts.Num())
	{
		FString CountString = ConfigRow->RewardItemCounts[TaskIndex];
		if (!CountString.IsEmpty())
		{
			CountString.ParseIntoArray(TaskRewardCounts, TEXT(","));
		}
	}

	UE_LOG(LogTemp, Log, TEXT("[REWARD_COUNT_DEBUG] UTaskDetailWidget: TaskIndex=%d, RewardItemCounts大小: %d"), TaskIndex, ConfigRow->RewardItemCounts.Num());
	UE_LOG(LogTemp, Log, TEXT("[REWARD_COUNT_DEBUG] UTaskDetailWidget: 当前任务数量数组大小: %d"), TaskRewardCounts.Num());

	// 🔧 调试: 输出当前任务的数量
	for (int32 idx = 0; idx < TaskRewardCounts.Num(); ++idx)
	{
		UE_LOG(LogTemp, Log, TEXT("[REWARD_COUNT_DEBUG] 数量[%d]: %s"), idx, *TaskRewardCounts[idx]);
	}

	// 🔧 核心业务逻辑: 遍历宝箱 ID，加载对应的宝箱图标并创建 WBP_RewardIcon 蓝图组件
	for (int32 i = 0; i < BoxIDArray.Num(); ++i)
	{
		int32 BoxID = FCString::Atoi(*BoxIDArray[i]);
		UE_LOG(LogTemp, Log, TEXT("🔧 UTaskDetailWidget: 处理宝箱 ID %d"), BoxID);

		if (BoxID <= 0)
		{
			UE_LOG(LogTemp, Warning, TEXT("UTaskDetailWidget: 无效的 BoxID: %d"), BoxID);
			continue;
		}

		// 🔧 核心业务逻辑: 通过 BoxID 查找 TreasureBoxItemRow 配置
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

		// 🔧 核心业务逻辑: 加载宝箱图标纹理
		UTexture2D* BoxIconTexture = TreasureBoxItem->BoxIcon.LoadSynchronous();
		if (!BoxIconTexture)
		{
			UE_LOG(LogTemp, Warning, TEXT("UTaskDetailWidget: 宝箱图标加载失败 - BoxID: %d"), BoxID);
			continue;
		}

		// 🔧 核心业务逻辑: 创建 WBP_RewardIcon 蓝图组件
		UUserWidget* IconWidget = CreateWidget<UUserWidget>(GetWorld(), RewardIconClass);
		if (IconWidget)
		{
			// 获取图标控件
			UImage* RewardImage = Cast<UImage>(IconWidget->GetWidgetFromName(TEXT("RewardImage")));
			UTextBlock* CountText = Cast<UTextBlock>(IconWidget->GetWidgetFromName(TEXT("CountText")));

			if (RewardImage)
			{
				RewardImage->SetBrushFromTexture(BoxIconTexture);
				RewardImage->SetVisibility(ESlateVisibility::Visible); // 确保 RewardImage 始终可见

				// 🔧 Canvas Panel 中需要直接操作 Slot 来设置尺寸
				if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(RewardImage->Slot))
				{
					CanvasSlot->SetSize(FVector2D(80.0f, 80.0f));
					// 设置锚点为左上角
					CanvasSlot->SetAnchors(FAnchors(0.0f, 0.0f));
					CanvasSlot->SetAlignment(FVector2D(0.0f, 0.0f));
				}
			}

			// 🔧 设置 CountText 控件的值（与 DayLockHintWidget 的 TaskRewardIconsContainer 逻辑一致）
			if (CountText)
			{
				bool bHasValidCount = false;
				FString CountValue = TEXT("");

				// 从展平后的数量数组中获取对应的数量
				if (i < TaskRewardCounts.Num())
				{
					CountValue = TaskRewardCounts[i];
					bHasValidCount = !CountValue.IsEmpty();

					UE_LOG(LogTemp, Log, TEXT("[REWARD_COUNT_DEBUG] UTaskDetailWidget: 第%d个图标，数量: '%s'"), i, *CountValue);
				}
				else
				{
					UE_LOG(LogTemp, Warning, TEXT("[REWARD_COUNT_DEBUG] UTaskDetailWidget: 第%d个图标，TaskRewardCounts[%d] 越界"), i, i);
				}

				if (bHasValidCount)
				{
					FString DisplayText = FString::Printf(TEXT("X%s"), *CountValue);
					CountText->SetText(FText::FromString(DisplayText));
					CountText->SetVisibility(ESlateVisibility::Visible);
				}
				else
				{
					// 为空或越界时隐藏 CountText
					CountText->SetVisibility(ESlateVisibility::Collapsed);
				}
			}

			// 将图标添加到容器中
			RewardsContainer->AddChild(IconWidget);
			UE_LOG(LogTemp, Log, TEXT("🔧 UTaskDetailWidget: 成功添加奖励图标 %d/%d 到容器"), i + 1, BoxIDArray.Num());
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("UTaskDetailWidget: 创建 WBP_RewardIcon Widget 失败"));
		}
	}

	UE_LOG(LogTemp, Log, TEXT("🔧 UTaskDetailWidget: 奖励容器设置完成"));
}
