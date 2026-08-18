// 版权声明：在项目设置的描述页面填写您的版权信息。

// ==========================================
// DayLockHintWidget 实现 — 天数锁提示 Widget
// ==========================================
//
// 文件作用:
//   1. 实现 UDayLockHintWidget — 天数锁定状态所有 UI 逻辑
//   2. InitializeWidget 设置 HintText + 任务/限时奖励图标
//
// 大厂原则:
//   - 数据驱动: DayIdentifier 从 UUpgradeActivitySubsystem 取数据
//   - 双容器: 限时奖励 + 任务奖励 分离显示
// ==========================================
#include "UI/Activity/Pages/DailyUpgradeReward/DayLockHintWidget.h"

#include "Components/HorizontalBox.h"
#include "Components/Image.h"
#include "Components/TextBlock.h"
#include "Components/CanvasPanelSlot.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/Engine.h"

// UE 会自动生成构造函数，无需手动实现


// ==========================================
// 1. 公共接口
// ==========================================

/**
 * UDayLockHintWidget::InitializeWidget
 *
 * 1. 设置 HintText（"DAY1 尚未解锁"）
 * 2. SetupTaskRewardIcons（任务奖励图标）
 * 3. SetupLimitedTimeRewardIcons（限时奖励图标）
 *
 * @param DayIdentifier 天数标识符（如"day1"）
 */
void UDayLockHintWidget::InitializeWidget(const FString& DayIdentifier)
{
	UE_LOG(LogTemp, Log, TEXT("🔧 UDayLockHintWidget: 初始化锁定提示 Widget - Day:%s"), *DayIdentifier);

	// 设置提示文字
	if (HintText)
	{
		FString HintString = FString::Printf(TEXT("%s 尚未解锁"), *DayIdentifier.ToUpper());
		HintText->SetText(FText::FromString(HintString));
		UE_LOG(LogTemp, Log, TEXT("🔧 UDayLockHintWidget: 设置提示文字: %s"), *HintString);
	}

	// 设置任务奖励图标
	SetupTaskRewardIcons(DayIdentifier);

	// 设置限时奖励图标
	SetupLimitedTimeRewardIcons(DayIdentifier);
}


// ==========================================
// 2. 任务奖励图标
// ==========================================

/**
 * UDayLockHintWidget::SetupTaskRewardIcons
 *
 * 1. 编辑器预览模式检查（防 GetWorld 异常）
 * 2. 防御链: TaskRewardIconsContainer / GameInstance / UUpgradeActivitySubsystem
 * 3. 读取 Subsystem->GetRewardIconsForDay(DayIdentifier) + GetConfigRowForDay
 * 4. 展平 RewardItemCounts（"1,1" -> ["1","1"]）
 * 5. 动态创建 WBP_RewardIcon + 设置图标 + 数量
 * 6. CanvasPanelSlot: 128x128
 *
 * @param DayIdentifier 天数标识符
 */
void UDayLockHintWidget::SetupTaskRewardIcons(const FString& DayIdentifier)
{
	if (!TaskRewardIconsContainer)
	{
		UE_LOG(LogTemp, Warning, TEXT("UDayLockHintWidget: TaskRewardIconsContainer 未设置"));
		return;
	}

	// 检查是否为编辑器预览模式
	if (!GetWorld() || !GetWorld()->IsGameWorld())
	{
		return; // 静默返回，不输出日志
	}

	UE_LOG(LogTemp, Log, TEXT("🔧 UDayLockHintWidget: 设置任务奖励图标容器 - Day:%s"), *DayIdentifier);

	// 🔧 调试：检查容器状态
	UE_LOG(LogTemp, Log, TEXT("[REWARD_ICON_DEBUG] TaskRewardIconsContainer 可见性: %d"), (int32)TaskRewardIconsContainer->GetVisibility());
	FVector2D ContainerSize = TaskRewardIconsContainer->GetDesiredSize();
	UE_LOG(LogTemp, Log, TEXT("[REWARD_ICON_DEBUG] TaskRewardIconsContainer 尺寸: %fx%f"), ContainerSize.X, ContainerSize.Y);

	// 清空现有内容
	TaskRewardIconsContainer->ClearChildren();

	// 获取 GameInstance
	UGameInstance* GameInstance = GetGameInstance();
	if (!GameInstance)
	{
		UE_LOG(LogTemp, Error, TEXT("UDayLockHintWidget: 无法获取 GameInstance"));
		return;
	}

	// 获取 UpgradeActivitySubsystem
	UUpgradeActivitySubsystem* Subsystem = GameInstance->GetSubsystem<UUpgradeActivitySubsystem>();
	if (!Subsystem)
	{
		UE_LOG(LogTemp, Error, TEXT("UDayLockHintWidget: 无法获取 UpgradeActivitySubsystem"));
		return;
	}

	// 获取指定天数的奖励图标
	TArray<UTexture2D*> RewardIcons = Subsystem->GetRewardIconsForDay(DayIdentifier);

	// 🔧 获取指定天数的奖励数量数据
	const FDailyUpgradeRewardConfigRow* ConfigRow = Subsystem->GetConfigRowForDay(DayIdentifier);
	TArray<FString> FlattenedRewardCounts; // 展平后的数量数组

	if (ConfigRow)
	{
		// 🔧 将 RewardItemCounts 数组展平为单个数量列表
		// 例如: ["1,1", "2,3,5", "1,1"] -> ["1", "1", "2", "3", "5", "1", "1"]
		for (const FString& CountString : ConfigRow->RewardItemCounts)
		{
			if (!CountString.IsEmpty())
			{
				TArray<FString> CountArray;
				CountString.ParseIntoArray(CountArray, TEXT(","));
				for (const FString& Count : CountArray)
				{
					FlattenedRewardCounts.Add(Count);
				}
			}
		}
	}

	UE_LOG(LogTemp, Log, TEXT("[REWARD_ICON_DEBUG] UDayLockHintWidget: 获取到 %d 个奖励图标"), RewardIcons.Num());
	UE_LOG(LogTemp, Log, TEXT("[REWARD_ICON_DEBUG] UDayLockHintWidget: 展平后 RewardItemCounts 数组大小: %d"), FlattenedRewardCounts.Num());

	// 🔧 调试: 输出所有奖励图标
	for (int32 idx = 0; idx < RewardIcons.Num(); ++idx)
	{
		UE_LOG(LogTemp, Log, TEXT("[REWARD_ICON_DEBUG] 图标[%d]: %p"), idx, RewardIcons[idx]);
	}

	// 🔧 调试: 输出展平后的数量
	for (int32 idx = 0; idx < FlattenedRewardCounts.Num(); ++idx)
	{
		UE_LOG(LogTemp, Log, TEXT("[REWARD_ICON_DEBUG] 数量[%d]: %s"), idx, *FlattenedRewardCounts[idx]);
	}

	// 检查 RewardIconClass 是否设置
	if (RewardIconClass == nullptr)
	{
		UE_LOG(LogTemp, Error, TEXT("UDayLockHintWidget: RewardIconClass 未设置! 请在蓝图中指定 WBP_RewardIcon 类"));
		return;
	}

	// 动态生成 WBP_RewardIcon 蓝图组件
	for (int32 i = 0; i < RewardIcons.Num(); ++i)
	{
		UTexture2D* IconTexture = RewardIcons[i];
		if (!IconTexture)
		{
			continue;
		}

		// 创建奖励图标 Widget
		UUserWidget* IconWidget = CreateWidget<UUserWidget>(GetWorld(), RewardIconClass);
		if (IconWidget)
		{
			// 获取图标控件
			UImage* RewardImage = Cast<UImage>(IconWidget->GetWidgetFromName(TEXT("RewardImage")));
			UTextBlock* CountText = Cast<UTextBlock>(IconWidget->GetWidgetFromName(TEXT("CountText")));

			if (RewardImage)
			{
				RewardImage->SetBrushFromTexture(IconTexture);
				// 🔧 Canvas Panel 中需要直接操作 Slot 来设置尺寸
				if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(RewardImage->Slot))
				{
					CanvasSlot->SetSize(FVector2D(128.0f, 128.0f));
					// 设置锚点为左上角
					CanvasSlot->SetAnchors(FAnchors(0.0f, 0.0f));
					CanvasSlot->SetAlignment(FVector2D(0.0f, 0.0f));
				}
			}

			// 🔧 设置 CountText 控件的值
			if (CountText)
			{
				bool bHasValidCount = false;
				FString CountValue = TEXT("");

				// 从展平后的数量数组中获取对应的数量
				if (i < FlattenedRewardCounts.Num())
				{
					CountValue = FlattenedRewardCounts[i];
					bHasValidCount = !CountValue.IsEmpty();

					UE_LOG(LogTemp, Log, TEXT("[REWARD_COUNT_DEBUG] UDayLockHintWidget: 第%d个图标，数量: '%s'"), i, *CountValue);
				}
				else
				{
					UE_LOG(LogTemp, Warning, TEXT("[REWARD_COUNT_DEBUG] UDayLockHintWidget: 第%d个图标，FlattenedRewardCounts[%d] 越界"), i, i);
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
			TaskRewardIconsContainer->AddChild(IconWidget);
			UE_LOG(LogTemp, Log, TEXT("[REWARD_ICON_DEBUG] ✅ UDayLockHintWidget: 成功添加奖励图标到 TaskRewardIconsContainer (索引: %d, 图标地址: %p)"), i, IconTexture);
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("UDayLockHintWidget: 创建 RewardIcon Widget 失败"));
		}
	}

	// 🔧 调试: 检查添加子控件后的容器状态
	UE_LOG(LogTemp, Log, TEXT("[REWARD_ICON_DEBUG] 添加子控件后 - 子控件数量: %d"), TaskRewardIconsContainer->GetChildrenCount());
	FVector2D FinalContainerSize = TaskRewardIconsContainer->GetDesiredSize();
	UE_LOG(LogTemp, Log, TEXT("[REWARD_ICON_DEBUG] 添加子控件后 - 容器尺寸: %fx%f"), FinalContainerSize.X, FinalContainerSize.Y);
}


// ==========================================
// 3. 限时奖励图标
// ==========================================

/**
 * UDayLockHintWidget::SetupLimitedTimeRewardIcons
 *
 * 与任务奖励类似, 但:
 * 1. 数据源: Subsystem->GetLimitedTimeRewardIconsForDay
 * 2. 限时奖励没有数量, CountText 始终 Collapsed
 * 3. CanvasPanelSlot: 90x90
 * 4. 大量调试日志（编辑器预览模式返回静默）
 *
 * @param DayIdentifier 天数标识符
 */
void UDayLockHintWidget::SetupLimitedTimeRewardIcons(const FString& DayIdentifier)
{
	if (!LimitedTimeRewardIconsContainer)
	{
		UE_LOG(LogTemp, Warning, TEXT("UDayLockHintWidget: LimitedTimeRewardIconsContainer 未设置"));
		return;
	}

	UE_LOG(LogTemp, Log, TEXT("🔧 UDayLockHintWidget: 设置限时奖励图标容器 - Day:%s"), *DayIdentifier);

	// 🔧 调试: 检查容器状态
	UE_LOG(LogTemp, Log, TEXT("[LIMITED_REWARD_DEBUG] LimitedTimeRewardIconsContainer 可见性: %d"), (int32)LimitedTimeRewardIconsContainer->GetVisibility());
	FVector2D ContainerSize = LimitedTimeRewardIconsContainer->GetDesiredSize();
	UE_LOG(LogTemp, Log, TEXT("[LIMITED_REWARD_DEBUG] LimitedTimeRewardIconsContainer 尺寸: %fx%f"), ContainerSize.X, ContainerSize.Y);

	// 清空现有内容
	LimitedTimeRewardIconsContainer->ClearChildren();

	// 获取 GameInstance
	UGameInstance* GameInstance = GetGameInstance();
	if (!GameInstance)
	{
		UE_LOG(LogTemp, Error, TEXT("UDayLockHintWidget: 无法获取 GameInstance"));
		return;
	}

	// 获取 UpgradeActivitySubsystem
	UUpgradeActivitySubsystem* Subsystem = GameInstance->GetSubsystem<UUpgradeActivitySubsystem>();
	if (!Subsystem)
	{
		UE_LOG(LogTemp, Error, TEXT("UDayLockHintWidget: 无法获取 UpgradeActivitySubsystem"));
		return;
	}

	// 获取指定天数的限时奖励图标
	TArray<UTexture2D*> LimitedTimeRewardIcons = Subsystem->GetLimitedTimeRewardIconsForDay(DayIdentifier);

	UE_LOG(LogTemp, Log, TEXT("[LIMITED_REWARD_DEBUG] UDayLockHintWidget: 获取到 %d 个限时奖励图标"), LimitedTimeRewardIcons.Num());

	// 🔧 调试: 输出所有限时奖励图标
	for (int32 idx = 0; idx < LimitedTimeRewardIcons.Num(); ++idx)
	{
		UE_LOG(LogTemp, Log, TEXT("[LIMITED_REWARD_DEBUG] 限时图标[%d]: %p"), idx, LimitedTimeRewardIcons[idx]);
	}

	// 检查 RewardIconClass 是否设置
	if (RewardIconClass == nullptr)
	{
		UE_LOG(LogTemp, Error, TEXT("UDayLockHintWidget: RewardIconClass 未设置! 请在蓝图中指定 WBP_RewardIcon 类"));
		return;
	}

	// 动态生成 WBP_RewardIcon 蓝图组件
	for (UTexture2D* IconTexture : LimitedTimeRewardIcons)
	{
		if (!IconTexture)
		{
			continue;
		}

		// 创建奖励图标 Widget
		UUserWidget* IconWidget = CreateWidget<UUserWidget>(GetWorld(), RewardIconClass);
		if (IconWidget)
		{
			// 获取图标控件
			UImage* RewardImage = Cast<UImage>(IconWidget->GetWidgetFromName(TEXT("RewardImage")));
			UTextBlock* CountText = Cast<UTextBlock>(IconWidget->GetWidgetFromName(TEXT("CountText")));

			if (RewardImage)
			{
				RewardImage->SetBrushFromTexture(IconTexture);
				RewardImage->SetVisibility(ESlateVisibility::Visible); // 确保 RewardImage 始终可见
				// 🔧 Canvas Panel 中需要直接操作 Slot 来设置尺寸
				if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(RewardImage->Slot))
				{
					CanvasSlot->SetSize(FVector2D(90.0f, 90.0f));
					// 设置锚点为左上角
					CanvasSlot->SetAnchors(FAnchors(0.0f, 0.0f));
					CanvasSlot->SetAlignment(FVector2D(0.0f, 0.0f));
				}

				// 🔧 调试: 检查纹理是否有效
				if (IconTexture)
				{
					UE_LOG(LogTemp, Log, TEXT("[LIMITED_REWARD_DEBUG] 图标纹理有效 - 地址: %p, 名称: %s, 尺寸: %dx%d"),
						IconTexture, *IconTexture->GetName(), IconTexture->GetSizeX(), IconTexture->GetSizeY());

					// 🔧 额外调试: 检查 Brush 是否有效
					const FSlateBrush& Brush = RewardImage->GetBrush();
					if (Brush.GetResourceObject() == IconTexture)
					{
						UE_LOG(LogTemp, Log, TEXT("[LIMITED_REWARD_DEBUG] RewardImage Brush 设置成功 - 纹理匹配"));
					}
					else
					{
						UE_LOG(LogTemp, Warning, TEXT("[LIMITED_REWARD_DEBUG] RewardImage Brush 设置失败 - 纹理不匹配"));
					}
				}
				else
				{
					UE_LOG(LogTemp, Warning, TEXT("[LIMITED_REWARD_DEBUG] 图标纹理为空!"));
				}
			}

			// 🔧 限时奖励图标没有对应的数量数据，隐藏 CountText
			if (CountText)
			{
				CountText->SetVisibility(ESlateVisibility::Collapsed);
			}

			// 将图标添加到容器中
			LimitedTimeRewardIconsContainer->AddChild(IconWidget);
			UE_LOG(LogTemp, Log, TEXT("[LIMITED_REWARD_DEBUG] ✅ UDayLockHintWidget: 成功添加限时奖励图标到 LimitedTimeRewardIconsContainer (图标地址: %p)"), IconTexture);
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("UDayLockHintWidget: 创建 RewardIcon Widget 失败"));
		}
	}

	// 🔧 调试: 检查添加子控件后的容器状态
	UE_LOG(LogTemp, Log, TEXT("[LIMITED_REWARD_DEBUG] 添加子控件后 - 子控件数量: %d"), LimitedTimeRewardIconsContainer->GetChildrenCount());
	FVector2D FinalContainerSize = LimitedTimeRewardIconsContainer->GetDesiredSize();
	UE_LOG(LogTemp, Log, TEXT("[LIMITED_REWARD_DEBUG] 添加子控件后 - 容器尺寸: %fx%f"), FinalContainerSize.X, FinalContainerSize.Y);
}
