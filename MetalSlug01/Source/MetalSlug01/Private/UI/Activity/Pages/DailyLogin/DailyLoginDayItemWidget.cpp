// 版权声明：在项目设置的描述页面填写您的版权信息。

// ==========================================
// 头文件包含区
// ==========================================
#include "UI/Activity/Pages/DailyLogin/DailyLoginDayItemWidget.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/Widget.h"
#include "Components/CanvasPanelSlot.h"
#include "Styling/SlateTypes.h"
#include "Systems/Activity/ActivitySubsystem.h"
#include "UI/Activity/Pages/DailyLogin/DailyLoginPage.h"  // 临时添加
#include "Engine/Texture2D.h"
#include "TimerManager.h"
#include "Async/Async.h"


// ==========================================
// 1. 生命周期
// ==========================================

/**
 * UDailyLoginDayItemWidget::NativeConstruct
 *
 * 1. 错误检查: RewardContainer / RewardIconClass
 * 2. 默认颜色:
 *    - ClaimedColor        = 黄色(1,1,0)        // 可领取
 *    - TomorrowColor       = 青色(0,1,1)        // 明日可领
 *    - ClaimedDisabledColor     = 灰色(0.3)     // 已领取
 *    - IncompleteDisabledColor  = 浅灰(0.8)    // 未完成
 * 3. ClaimButton->OnClicked.RemoveAll + AddDynamic
 * 4. SetTimer 0.1s 延迟设置初始颜色（确保按钮完全初始化）
 */
void UDailyLoginDayItemWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// 检查必需的控件是否已绑定
	if (!RewardContainer)
	{
		UE_LOG(LogTemp, Error, TEXT("DailyLoginDayItemWidget: RewardContainer未绑定，请检查蓝图中的控件名称!"));
	}

	if (!RewardIconClass)
	{
		UE_LOG(LogTemp, Error, TEXT("DailyLoginDayItemWidget: RewardIconClass未设置，请在编辑器中指定奖励图标Widget类!"));
	}

	// 设置默认颜色值
	ClaimedColor = FLinearColor(1.0f, 1.0f, 0.0f, 1.0f);        // 基础颜色 - 领取状态
	TomorrowColor = FLinearColor(0.0f, 1.0f, 1.0f, 1.0f);       // 基础颜色 - 明日可领取
	ClaimedDisabledColor = FLinearColor(0.3f, 0.3f, 0.3f, 1.0f); // 灰色 - 已领取状态
	IncompleteDisabledColor = FLinearColor(0.8f, 0.8f, 0.8f, 1.0f); // 灰色 - 未完成状态

	if (ClaimButton)
	{
		// 确保按钮点击事件已移除并重新添加
		ClaimButton->OnClicked.RemoveAll(this);
		ClaimButton->OnClicked.AddDynamic(this, &UDailyLoginDayItemWidget::OnClaimButtonClicked);

		// 延迟设置初始颜色，确保按钮完全初始化
		FTimerHandle ColorTimerHandle;
		GetWorld()->GetTimerManager().SetTimer(ColorTimerHandle, [this]()
		{
			if (ClaimButton)
			{
				// 根据当前状态设置对应颜色
				switch(CurrentState)
				{
				case ERewardState::Claimable:
					ClaimButton->SetBackgroundColor(ClaimedColor); // 基础颜色
					break;
				case ERewardState::Claimed:
					ClaimButton->SetBackgroundColor(ClaimedDisabledColor); // 灰色
					break;
				case ERewardState::Incomplete:
					if (DayIndex == CurrentProgress + 1)
					{
						ClaimButton->SetBackgroundColor(TomorrowColor); // 基础颜色
					}
					else
					{
						ClaimButton->SetBackgroundColor(IncompleteDisabledColor); // 灰色
					}
					break;
				}
				ClaimButton->InvalidateLayoutAndVolatility();
			}
		}, 0.1f, false); // 延迟 0.1 秒执行
	}
}


// ==========================================
// 2. 公共接口 - Init
// ==========================================

/**
 * UDailyLoginDayItemWidget::Init
 *
 * 1. 缓存 DayIndex / CurrentProgress / CurrentState / bIsSpecialReward
 * 2. 清空 RewardContainer
 * 3. 遍历 ActivitySubsystem->GetRewardsByDay(101, DayIndex) -> AddRewardIconFromConfig
 * 4. UpdateVisualState
 *
 * @param InDayIndex 天数索引
 * @param InCurrentProgress 当前进度
 * @param RewardState 状态 (Claimable/Claimed/Incomplete)
 * @param bInIsSpecialReward 是否大奖（第 8 天等）
 * @param ActivitySubsystem 活动子系统
 */
void UDailyLoginDayItemWidget::Init(int32 InDayIndex, int32 InCurrentProgress, ERewardState RewardState, bool bInIsSpecialReward, UActivitySubsystem* ActivitySubsystem)
{
	// 1. 初始化所有成员变量
	DayIndex = InDayIndex;
	CurrentProgress = InCurrentProgress;
	CurrentState = RewardState;
	bIsSpecialReward = bInIsSpecialReward; // 设置是否为特殊奖励

	UE_LOG(LogTemp, Warning, TEXT("DailyLoginDayItemWidget::Init - DayIndex=%d, CurrentProgress=%d, State=%d"), DayIndex, CurrentProgress, (int32)CurrentState);

	// 2. 清空奖励容器中的所有子控件
	if (RewardContainer)
	{
		RewardContainer->ClearChildren();
	}

	// 3. 如果提供了 ActivitySubsystem，自动加载奖励图标
	if (ActivitySubsystem)
	{
		TArray<FDailyLoginConfigRow*> Rewards = ActivitySubsystem->GetRewardsByDay(101, DayIndex);
		UE_LOG(LogTemp, Warning, TEXT("Init: 自动加载第%d天奖励图标，奖励数量: %d"), DayIndex, Rewards.Num());

		for (auto* RewardRow : Rewards)
		{
			if (RewardRow)
			{
				UE_LOG(LogTemp, Warning, TEXT("  自动加载奖励: ActivityID=%d, DayIndex=%d, RewardItemID=%d, RewardType=%d"),
					RewardRow->ActivityID, RewardRow->DayIndex, RewardRow->RewardItemID, (int32)RewardRow->RewardType);
				AddRewardIconFromConfig(*RewardRow);
			}
		}
	}

	// 4. 更新视觉状态，确保所有成员变量已初始化
	UpdateVisualState();
}


// ==========================================
// 3. 视觉状态
// ==========================================

/**
 * UDailyLoginDayItemWidget::UpdateVisualState
 *
 * 1. 大奖: BigRewardBackground / CharacterIcon 可见
 * 2. 按钮状态机:
 *    - Claimable:
 *      a. ButtonText = "领取"
 *      b. RenderOpacity = 1.0
 *      c. BackgroundColor = ClaimedColor (黄)
 *      d. Scale = 1.5x
 *      e. ZOrder = 1000
 *    - Claimed:
 *      a. IsEnabled = false
 *      b. ButtonText = "已领取"
 *      c. Opacity = 0.3
 *      d. BackgroundColor = ClaimedDisabledColor (灰)
 *    - Incomplete:
 *      a. IsEnabled = false, Opacity = 0.5
 *      b. DayIndex == CurrentProgress + 1 (明日可领):
 *         ButtonText = "明日可领", Color = TomorrowColor
 *      c. 其他: ButtonText = "第 N 天", Color = IncompleteDisabledColor
 */
void UDailyLoginDayItemWidget::UpdateVisualState()
{
	// 根据是否为大奖控制背景控件的显示/隐藏
	if (BigRewardBackground)
	{
		BigRewardBackground->SetVisibility(bIsSpecialReward ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}

	if (CharacterIcon)
	{
		CharacterIcon->SetVisibility(bIsSpecialReward ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
	}

	// 移除 OnSetupBigRewardStyle() 的调用，改为直接设置样式
	// if (bIsSpecialReward) { OnSetupBigRewardStyle(); }

	// 更新按钮状态和信息
	if (ClaimButton)
	{
		// 检查按钮样式
		const FButtonStyle& Style = ClaimButton->GetStyle();
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("ClaimButton为空指针"));
	}

	switch (CurrentState)
	{
	case ERewardState::Claimable:
		ClaimButton->SetIsEnabled(true);
		ButtonText->SetText(FText::FromString(TEXT("领取")));
		ClaimButton->SetRenderOpacity(1.0f);
		// 使用基础方法设置背景色
		ClaimButton->SetBackgroundColor(ClaimedColor);
		ClaimButton->InvalidateLayoutAndVolatility();
		// 设置按钮缩放
		ClaimButton->SetRenderScale(FVector2D(1.5f, 1.5f));
		ClaimButton->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
		// 通过 CanvasPanelSlot 设置 ZOrder
		if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(ClaimButton->Slot))
		{
			CanvasSlot->SetZOrder(1000);
		}
		break;

	case ERewardState::Claimed:
		ClaimButton->SetIsEnabled(false);
		ButtonText->SetText(FText::FromString(TEXT("已领取")));
		ClaimButton->SetRenderOpacity(0.3f);
		// 设置已领取状态颜色 - 灰色
		ClaimButton->SetBackgroundColor(ClaimedDisabledColor);

		// 强制刷新按钮样式
		ClaimButton->InvalidateLayoutAndVolatility();
		// 设置按钮缩放
		ClaimButton->SetRenderScale(FVector2D(1.5f, 1.5f));
		ClaimButton->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
		// 通过 CanvasPanelSlot 设置 ZOrder
		if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(ClaimButton->Slot))
		{
			CanvasSlot->SetZOrder(1000);
		}
		break;

	case ERewardState::Incomplete:
		ClaimButton->SetIsEnabled(false);
		ClaimButton->SetRenderOpacity(0.5f);

		// 使用成员变量判断明日可领取
		// Progress=N 表示第 1-N 天可领取，第 (N+1) 天为明日可领
		if (DayIndex == CurrentProgress + 1)
		{
			UE_LOG(LogTemp, Warning, TEXT("UpdateVisualState: 第%d天设置为明日可领状态 (CurrentProgress=%d)"), DayIndex, CurrentProgress);
			ButtonText->SetText(FText::FromString(TEXT("明日可领")));
			// 设置明日可领取状态颜色 - 黄色
			ClaimButton->SetBackgroundColor(TomorrowColor);
			// 设置按钮缩放
			ClaimButton->SetRenderScale(FVector2D(1.5f, 1.5f));
			ClaimButton->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
			// 通过 CanvasPanelSlot 设置 ZOrder
			if (UCanvasPanelSlot* CanvasSlot2 = Cast<UCanvasPanelSlot>(ClaimButton->Slot))
			{
				CanvasSlot2->SetZOrder(1000);
			}
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("UpdateVisualState: 第%d天显示为普通未完成状态 (CurrentProgress=%d)"), DayIndex, CurrentProgress);
			FString DayStr = FString::Printf(TEXT("第 %d 天"), DayIndex);
			ButtonText->SetText(FText::FromString(DayStr));
			// 设置未完成状态颜色 - 灰色
			ClaimButton->SetBackgroundColor(IncompleteDisabledColor);
			// 设置按钮缩放
			ClaimButton->SetRenderScale(FVector2D(1.5f, 1.5f));
			ClaimButton->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
			// 通过 CanvasPanelSlot 设置 ZOrder
			if (UCanvasPanelSlot* CanvasSlot2 = Cast<UCanvasPanelSlot>(ClaimButton->Slot))
			{
				CanvasSlot2->SetZOrder(1000);
			}
		}
		break;
	}
}


// ==========================================
// 4. 按钮点击
// ==========================================

/**
 * UDailyLoginDayItemWidget::OnClaimButtonClicked
 *
 * 1. 仅在 CurrentState == Claimable 时处理
 * 2. 读取 Rewards[0].RewardType
 * 3. OnRewardClicked.Broadcast(DayIndex, RewardType)
 * 4. 非 Box 类型: TryClaimReward + CurrentState = Claimed + UpdateVisualState
 */
void UDailyLoginDayItemWidget::OnClaimButtonClicked()
{
	if (CurrentState != ERewardState::Claimable)
	{
		return;
	}

	// 获取 Subsystem 实例
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UActivitySubsystem* ActivitySub = GI->GetSubsystem<UActivitySubsystem>())
		{
			// 获取对应天数的奖励信息
			TArray<FDailyLoginConfigRow*> Rewards = ActivitySub->GetRewardsByDay(101, DayIndex);

			if (Rewards.Num() > 0)
			{
				// 获取第一个奖励类型，这里假设只有一个奖励
				ELoginRewardType RewardType = Rewards[0]->RewardType;

				// 通过委托通知页面
				OnRewardClicked.Broadcast(DayIndex, RewardType);

				// 保存奖励信息;判断是否为箱子
				if (RewardType != ELoginRewardType::Box)
				{
					// 领取奖励
					ActivitySub->TryClaimReward(101, DayIndex);

					// 更新状态
					CurrentState = ERewardState::Claimed;
					UpdateVisualState();
				}
			}
		}
	}
}


// ==========================================
// 5. 奖励图标
// ==========================================

/**
 * UDailyLoginDayItemWidget::AddRewardIconFromConfig
 *
 * 分派器:
 * - RewardType == Box: AddRewardIconFromTreasureBox
 * - 其他: AddRewardIcon
 */
void UDailyLoginDayItemWidget::AddRewardIconFromConfig(const FDailyLoginConfigRow& ConfigRow)
{
	UE_LOG(LogTemp, Warning, TEXT("=== AddRewardIconFromConfig 被调用 === DayIndex: %d, RewardItemID: %d, RewardType: %d"),
		ConfigRow.DayIndex, ConfigRow.RewardItemID, (int32)ConfigRow.RewardType);

	// 根据奖励类型决定图标来源
	if (ConfigRow.RewardType == ELoginRewardType::Box)
	{
		UE_LOG(LogTemp, Warning, TEXT("  奖励类型为Box，从TreasureBoxItemRow表查找宝箱图标"));
		// 礼包/宝箱类型: 从 TreasureBoxItemRow 表查找 BoxIcon
		AddRewardIconFromTreasureBox(ConfigRow.RewardItemID, ConfigRow.RewardCount);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("  奖励类型为普通物品，使用RewardItemID查询ItemIcon"));
		// 其他类型: 通过 RewardItemID 查询 FItemDetailRow 的 ItemIcon
		AddRewardIcon(ConfigRow.RewardItemID, ConfigRow.RewardCount);
	}
}


/**
 * UDailyLoginDayItemWidget::ClearRewardIcons
 */
void UDailyLoginDayItemWidget::ClearRewardIcons()
{
	if (RewardContainer)
	{
		int32 ChildCountBefore = RewardContainer->GetChildrenCount();
		RewardContainer->ClearChildren();
	}
}


/**
 * UDailyLoginDayItemWidget::AddRewardIconFromBoxImage
 *
 * 处理 SoftObjectPtr 形式宝箱图标
 * 1. SetBrushFromSoftTexture (UE 异步加载)
 * 2. CountText 字体 40pt
 */
void UDailyLoginDayItemWidget::AddRewardIconFromBoxImage(const TSoftObjectPtr<UTexture2D>& BoxImage, int32 RewardCount)
{
	UE_LOG(LogTemp, Warning, TEXT("=== AddRewardIconFromBoxImage 被调用 === RewardCount: %d"), RewardCount);

	// 专门处理礼包/宝箱类型的图标
	if (!RewardContainer)
	{
		UE_LOG(LogTemp, Error, TEXT("DayItemWidget: RewardContainer (容器) 为空，无法添加图标"));
		return;
	}

	if (!RewardIconClass)
	{
		UE_LOG(LogTemp, Error, TEXT("DayItemWidget: RewardIconClass (图标类) 未赋值，无法添加图标"));
		return;
	}

	// 创建奖励图标 Widget
	UE_LOG(LogTemp, Warning, TEXT("准备创建BoxImage RewardIcon Widget，类名: %s"), *RewardIconClass->GetName());
	UUserWidget* IconWidget = CreateWidget<UUserWidget>(GetWorld(), RewardIconClass);
	if (IconWidget)
	{
		UE_LOG(LogTemp, Warning, TEXT("✅ 成功创建BoxImage RewardIcon Widget"));
		RewardContainer->AddChild(IconWidget);
		UE_LOG(LogTemp, Warning, TEXT("✅ 已将BoxImage图标添加到RewardContainer"));

		// 获取图标控件
		UImage* RewardImage = Cast<UImage>(IconWidget->GetWidgetFromName(TEXT("RewardImage")));
		UTextBlock* CountText = Cast<UTextBlock>(IconWidget->GetWidgetFromName(TEXT("CountText")));

		if (RewardImage && BoxImage.IsValid())
		{
			// 使用 UE 官方异步加载方式
			RewardImage->SetBrushFromSoftTexture(BoxImage);
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("❌ RewardImage为空或BoxImage无效"));
		}

		if (CountText)
		{
			FString DisplayText = FString::Printf(TEXT("X%d"), RewardCount);
			CountText->SetText(FText::FromString(DisplayText));
			// 增大文字大小
			FSlateFontInfo FontInfo = CountText->GetFont();
			FontInfo.Size = 40;
			CountText->SetFont(FontInfo);
		}
	}
}


/**
 * UDailyLoginDayItemWidget::AddRewardIconFromTreasureBox
 *
 * 1. 防御链
 * 2. CreateWidget<URewardIconClass>
 * 3. GetWidgetFromName: RewardImage / CountText
 * 4. GetGameInstance -> UActivitySubsystem
 * 5. GetTreasureBoxItem(BoxID)
 * 6. SetBrushFromSoftTexture(BoxIcon) （UMG 异步加载）
 * 7. 失败 -> 灰色占位符
 * 8. CountText "X{count}" 字体 24pt
 */
void UDailyLoginDayItemWidget::AddRewardIconFromTreasureBox(int32 BoxID, int32 RewardCount)
{
	UE_LOG(LogTemp, Warning, TEXT("=== AddRewardIconFromTreasureBox 被调用 === BoxID: %d, Count: %d"), BoxID, RewardCount);

	// 专门处理从 TreasureBoxItemRow 表查找宝箱图标的逻辑
	if (!RewardContainer)
	{
		UE_LOG(LogTemp, Error, TEXT("DayItemWidget: RewardContainer (容器) 为空，无法添加图标"));
		return;
	}

	if (!RewardIconClass)
	{
		UE_LOG(LogTemp, Error, TEXT("DayItemWidget: RewardIconClass (图标类) 未赋值，无法添加图标"));
		return;
	}

	// 创建奖励图标 Widget
	UE_LOG(LogTemp, Warning, TEXT("准备创建TreasureBox RewardIcon Widget，类名: %s"), *RewardIconClass->GetName());
	UUserWidget* IconWidget = CreateWidget<UUserWidget>(GetWorld(), RewardIconClass);
	if (IconWidget)
	{
		UE_LOG(LogTemp, Warning, TEXT("✅ 成功创建TreasureBox RewardIcon Widget"));
		RewardContainer->AddChild(IconWidget);
		UE_LOG(LogTemp, Warning, TEXT("✅ 已将TreasureBox图标添加到RewardContainer"));

		// 获取图标控件
		UImage* RewardImage = Cast<UImage>(IconWidget->GetWidgetFromName(TEXT("RewardImage")));
		UTextBlock* CountText = Cast<UTextBlock>(IconWidget->GetWidgetFromName(TEXT("CountText")));

		if (RewardImage)
		{
			// 通过 BoxID 从 TreasureBoxItemRow 表获取 BoxIcon
			if (UGameInstance* GI = GetGameInstance())
			{
				if (UActivitySubsystem* ActivitySub = GI->GetSubsystem<UActivitySubsystem>())
				{
					// 从 TreasureBoxItemRow 表查询 BoxIcon
					const FTreasureBoxItemRow* TreasureBoxItem = ActivitySub->GetTreasureBoxItem(BoxID);

					// 添加详细的调试信息
					if (TreasureBoxItem)
					{
						// 使用 UMG 异步加载 BoxIcon
						// 注意: 即使资源处于 IsPending 状态(!IsNull() && !IsValid())，UMG 也会正确处理
						RewardImage->SetBrushFromSoftTexture(TreasureBoxItem->BoxIcon);
					}
					else
					{
						// 显示灰色占位符
						RewardImage->SetColorAndOpacity(FLinearColor(0.5f, 0.5f, 0.5f, 1.0f));
						RewardImage->SetRenderScale(FVector2D(2.5f, 7.0f));
						RewardImage->SetDesiredSizeOverride(FVector2D(64.0f, 64.0f));
					}
				}
			}
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("❌ RewardImage控件未找到"));
		}

		if (CountText)
		{
			FString DisplayText = FString::Printf(TEXT("X%d"), RewardCount);
			CountText->SetText(FText::FromString(DisplayText));
			// 增大文字大小
			FSlateFontInfo FontInfo = CountText->GetFont();
			FontInfo.Size = 24;
			CountText->SetFont(FontInfo);
		}
	}
}


/**
 * UDailyLoginDayItemWidget::AddRewardIcon
 *
 * 1. 防御链
 * 2. CreateWidget<URewardIconClass>
 * 3. GetWidgetFromName
 * 4. 遍历 GetDailyLoginConfigs(101) 匹配 RewardItemID
 * 5. GetItemDetail(RewardID) -> SetBrushFromSoftTexture
 * 6. 失败 -> 灰色占位符
 * 7. CountText "X{count}" 字体 24pt
 */
void UDailyLoginDayItemWidget::AddRewardIcon(int32 RewardID, int32 RewardCount)
{
	UE_LOG(LogTemp, Warning, TEXT("=== AddRewardIcon 被调用 === RewardID: %d, Count: %d"), RewardID, RewardCount);

	// 1. 全局检查:确保奖励容器和奖励图标类已设置
	if (!RewardContainer)
	{
		UE_LOG(LogTemp, Error, TEXT("DayItemWidget: RewardContainer (容器) 为空，无法添加图标"));
		return;
	}

	if (!RewardIconClass)
	{
		UE_LOG(LogTemp, Error, TEXT("DayItemWidget: RewardIconClass (图标类) 未赋值，无法添加图标"));
		return;
	}

	// 检查 RewardID 是否有效
	if (RewardID <= 0)
	{
		UE_LOG(LogTemp, Error, TEXT("DayItemWidget: 无效的RewardID: %d"), RewardID);
		return;
	}

	// 3. 创建奖励图标 Widget
	UE_LOG(LogTemp, Warning, TEXT("准备创建RewardIcon Widget，类名: %s"), *RewardIconClass->GetName());
	UUserWidget* IconWidget = CreateWidget<UUserWidget>(GetWorld(), RewardIconClass);
	if (IconWidget)
	{
		UE_LOG(LogTemp, Warning, TEXT("✅ 成功创建RewardIcon Widget"));
		// 4. 将图标添加到容器中
		RewardContainer->AddChild(IconWidget);
		UE_LOG(LogTemp, Warning, TEXT("✅ 已将图标添加到RewardContainer"));

		// 5. 获取图标 Widget 中的控件
		UImage* RewardImage = Cast<UImage>(IconWidget->GetWidgetFromName(TEXT("RewardImage")));
		UTextBlock* CountText = Cast<UTextBlock>(IconWidget->GetWidgetFromName(TEXT("CountText")));

		if (RewardImage)
		{
			// 通过 RewardItemID 从 FItemDetailRow 表获取 ItemIcon
			if (UGameInstance* GI = GetGameInstance())
			{
				if (UActivitySubsystem* ActivitySub = GI->GetSubsystem<UActivitySubsystem>())
				{
					// 获取所有奖励信息以查找匹配的 RewardItemID
					TArray<FDailyLoginConfigRow*> AllRewards = ActivitySub->GetDailyLoginConfigs(101);
					bool bFoundConfig = false;

					for (auto* RewardRow : AllRewards)
					{
						if (RewardRow && RewardRow->RewardItemID == RewardID)
						{
							bFoundConfig = true;

							// 通过 ActivitySubsystem 查询 ItemDetail
							const FItemDetailRow* ItemDetail = ActivitySub->GetItemDetail(RewardID);

							// 添加详细的调试信息
							if (ItemDetail)
							{
								if (!ItemDetail->ItemIcon.IsNull())
								{
									// 使用 UMG 原生异步接口 - 更优雅的方式
									// 注意: 即使资源处于 IsPending 状态(!IsNull() && !IsValid())，UMG 也会正确处理
									RewardImage->SetBrushFromSoftTexture(ItemDetail->ItemIcon);
								}
								else
								{
									// 显示灰色占位符
									RewardImage->SetColorAndOpacity(FLinearColor(0.5f, 0.5f, 0.5f, 1.0f));
									RewardImage->SetRenderScale(FVector2D(2.5f, 7.0f));
									RewardImage->SetDesiredSizeOverride(FVector2D(64.0f, 64.0f));
								}
							}
							else
							{
								// 显示灰色占位符
								RewardImage->SetColorAndOpacity(FLinearColor(0.5f, 0.5f, 0.5f, 1.0f));
								RewardImage->SetRenderScale(FVector2D(2.5f, 7.0f));
								RewardImage->SetDesiredSizeOverride(FVector2D(64.0f, 64.0f));
							}
							break;
						}
					}

					if (!bFoundConfig)
					{
						UE_LOG(LogTemp, Error, TEXT("❌ 未找到匹配的奖励信息，RewardID: %d"), RewardID);
					}
				}
			}
		}

		if (CountText)
		{
			// 设置奖励数量文本
			FString DisplayText = FString::Printf(TEXT("X%d"), RewardCount);
			CountText->SetText(FText::FromString(DisplayText));
			// 增大文字大小
			FSlateFontInfo FontInfo = CountText->GetFont();
			FontInfo.Size = 24;
			CountText->SetFont(FontInfo);
		}
	}
}


// ==========================================
// 6. 异步加载回调
// ==========================================

/**
 * UDailyLoginDayItemWidget::OnTextureLoaded
 *
 * 异步加载完成后, 在游戏线程更新 UI
 * 1. Cast<UTexture2D>(LoadedObject)
 * 2. AsyncTask(ENamedThreads::GameThread)
 * 3. SetBrushFromTexture + 2.5x7 Scale
 */
void UDailyLoginDayItemWidget::OnTextureLoaded(const FSoftObjectPath& Path, UObject* LoadedObject, UImage* RewardImage, int32 RewardID)
{
	if (UTexture2D* LoadedTexture = Cast<UTexture2D>(LoadedObject))
	{
		if (RewardImage && RewardImage->IsValidLowLevel())
		{
			// 在游戏线程中更新UI
			AsyncTask(ENamedThreads::GameThread, [RewardImage, LoadedTexture]()
			{
				if (RewardImage && RewardImage->IsValidLowLevel())
				{
					RewardImage->SetBrushFromTexture(LoadedTexture);
					RewardImage->SetRenderScale(FVector2D(2.5f, 7.0f));
					RewardImage->InvalidateLayoutAndVolatility();
				}
			});
		}
	}
}
