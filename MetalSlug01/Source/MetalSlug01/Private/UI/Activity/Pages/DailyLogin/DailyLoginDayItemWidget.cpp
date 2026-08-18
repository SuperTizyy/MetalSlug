// 版权声明：在项目设置的描述页面填写您的版权信息。

// ==========================================
// DailyLoginDayItemWidget 实现 — 每日登录单天条目
// ==========================================
//
// 文件作用:
//   1. 实现 UDailyLoginDayItemWidget — 单日登录条目所有 UI 逻辑
//   2. 4 种状态颜色: 可领取/明日可领/已领取/未完成
//   3. 第 8 天大奖特殊样式 (BigRewardItem)
//
// 大厂原则:
//   - 单一职责: 一个条目管自己, 不影响其他条目
//   - 防御性: 控件未绑 → Log Error
//   - 异步加载: TSoftObjectPtr 避免主线程卡顿
//   - 防重入: SetTimer 延迟初始化避免按钮未初始化时访问
// ==========================================
#include "UI/Activity/Pages/DailyLogin/DailyLoginDayItemWidget.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/Widget.h"
#include "Styling/SlateTypes.h"
#include "Systems/Activity/ActivitySubsystem.h"
#include "Engine/Texture2D.h"
#include "TimerManager.h"


// ==========================================
// 1. 生命周期
// ==========================================

/**
 * UDailyLoginDayItemWidget::NativeConstruct
 *
 * 1. 错误检查: RewardIconImage / RewardCountText / ClaimButton
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
	if (!RewardIconImage)
	{
		UE_LOG(LogTemp, Error, TEXT("DailyLoginDayItemWidget: RewardIconImage未绑定，请检查蓝图中的控件名称!"));
	}

	if (!RewardCountText)
	{
		UE_LOG(LogTemp, Error, TEXT("DailyLoginDayItemWidget: RewardCountText未绑定，请检查蓝图中的控件名称!"));
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
 * 2. 加载奖励图标（直接操作 RewardIconImage / RewardCountText）
 * 3. UpdateVisualState
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

	// 2. 加载奖励图标（直接操作 RewardIconImage / RewardCountText）
	if (ActivitySubsystem)
	{
		TArray<const FDailyLoginConfigRow*> Rewards = ActivitySubsystem->GetRewardsByDay(101, DayIndex);
		UE_LOG(LogTemp, Warning, TEXT("Init: 自动加载第%d天奖励图标，奖励数量: %d"), DayIndex, Rewards.Num());

		if (Rewards.Num() > 0 && Rewards[0])
		{
			auto* RewardRow = Rewards[0];
			UE_LOG(LogTemp, Warning, TEXT("  自动加载奖励: ActivityID=%d, DayIndex=%d, RewardItemID=%d, RewardType=%d"),
				RewardRow->ActivityID, RewardRow->DayIndex, RewardRow->RewardItemID, (int32)RewardRow->RewardType);
			AddRewardIconFromConfig(*RewardRow);
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
		// [修复] 删除原本强行缩放按钮的代码 —— SetRenderScale(1.5) + SetRenderTransformPivot 会让按钮在
		// ScrollBox 布局中占用 1.5 倍空间，导致生成内容尺寸异常。按钮尺寸交给蓝图 UMG 编辑器定义。
		// [修复] 删除原本通过 CanvasPanelSlot.SetZOrder(1000) 强行提升层级的代码 —
		// 按钮 ZOrder 由蓝图 Designer 面板自然定义，C++ 强行覆盖会导致 Layout 失效、容器尺寸异常。
		break;

	case ERewardState::Claimed:
		ClaimButton->SetIsEnabled(false);
		ButtonText->SetText(FText::FromString(TEXT("已领取")));
		ClaimButton->SetRenderOpacity(0.3f);
		// 设置已领取状态颜色 - 灰色
		ClaimButton->SetBackgroundColor(ClaimedDisabledColor);

		// 强制刷新按钮样式
		ClaimButton->InvalidateLayoutAndVolatility();
		// [修复] 删除原本强行缩放按钮的代码 (Claimed 分支) — 同上, 导致 ScrollBox 内容尺寸异常。
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
			// [修复] 删除原本强行缩放按钮的代码 (Incomplete 明日可领分支) — 同上。
		}
		else
		{
			UE_LOG(LogTemp, Warning, TEXT("UpdateVisualState: 第%d天显示为普通未完成状态 (CurrentProgress=%d)"), DayIndex, CurrentProgress);
			FString DayStr = FString::Printf(TEXT("第 %d 天"), DayIndex);
			ButtonText->SetText(FText::FromString(DayStr));
			// 设置未完成状态颜色 - 灰色
			ClaimButton->SetBackgroundColor(IncompleteDisabledColor);
			// [修复] 删除原本强行缩放按钮的代码 (Incomplete 普通分支) — 同上。
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
			TArray<const FDailyLoginConfigRow*> Rewards = ActivitySub->GetRewardsByDay(101, DayIndex);

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
 *
 * 清空奖励图标: 重置 Image 和 Text
 */
void UDailyLoginDayItemWidget::ClearRewardIcons()
{
	if (RewardIconImage)
	{
		RewardIconImage->SetBrush(FSlateBrush());
		RewardIconImage->SetColorAndOpacity(FLinearColor::White);
	}

	if (RewardCountText)
	{
		RewardCountText->SetText(FText::GetEmpty());
	}
}


/**
 * UDailyLoginDayItemWidget::AddRewardIconFromBoxImage
 *
 * 处理宝箱图标: 直接操作 RewardIconImage + RewardCountText
 * 1. SetBrushFromSoftTexture (UE 异步加载)
 * 2. RewardCountText "X{count}" 字体 40pt
 */
void UDailyLoginDayItemWidget::AddRewardIconFromBoxImage(const TSoftObjectPtr<UTexture2D>& BoxImage, int32 RewardCount)
{
	UE_LOG(LogTemp, Warning, TEXT("=== AddRewardIconFromBoxImage 被调用 === RewardCount: %d"), RewardCount);

	// 防御检查
	if (!RewardIconImage)
	{
		UE_LOG(LogTemp, Error, TEXT("DayItemWidget: RewardIconImage 未绑定，无法添加图标"));
		return;
	}

	if (!RewardCountText)
	{
		UE_LOG(LogTemp, Error, TEXT("DayItemWidget: RewardCountText 未绑定，无法设置数量"));
		return;
	}

	// 设置图标 (UE 异步加载)
	if (BoxImage.IsValid())
	{
		RewardIconImage->SetBrushFromSoftTexture(BoxImage);
		RewardIconImage->SetColorAndOpacity(FLinearColor::White);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("DayItemWidget: BoxImage 无效"));
		RewardIconImage->SetColorAndOpacity(FLinearColor(0.5f, 0.5f, 0.5f, 1.0f));
	}

	// 设置数量文本
	FString DisplayText = FString::Printf(TEXT("X%d"), RewardCount);
	RewardCountText->SetText(FText::FromString(DisplayText));
	// [修复] 删除原本强行 SetFont Size=40 的代码 — 字体大小由蓝图 UMG 编辑器定义, 强行覆盖会导致
	// 文本布局超出 RewardIconImage / RewardCountText 父容器, 进而撑大整个条目 widget, 影响 DayListScroll 内容尺寸。
}

/**
 * UDailyLoginDayItemWidget::AddRewardIconFromTreasureBox
 *
 * 处理宝箱类型: 直接操作 RewardIconImage + RewardCountText
 * 1. GetGameInstance -> UActivitySubsystem
 * 2. GetTreasureBoxItem(BoxID) -> SetBrushFromSoftTexture(BoxIcon)
 * 3. 失败 -> 灰色占位符
 * 4. RewardCountText "X{count}" 字体 24pt
 */
void UDailyLoginDayItemWidget::AddRewardIconFromTreasureBox(int32 BoxID, int32 RewardCount)
{
	UE_LOG(LogTemp, Warning, TEXT("=== AddRewardIconFromTreasureBox 被调用 === BoxID: %d, Count: %d"), BoxID, RewardCount);

	// 防御检查
	if (!RewardIconImage)
	{
		UE_LOG(LogTemp, Error, TEXT("DayItemWidget: RewardIconImage 未绑定，无法添加图标"));
		return;
	}

	if (!RewardCountText)
	{
		UE_LOG(LogTemp, Error, TEXT("DayItemWidget: RewardCountText 未绑定，无法设置数量"));
		return;
	}

	// 通过 BoxID 从 TreasureBoxItemRow 表获取 BoxIcon
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UActivitySubsystem* ActivitySub = GI->GetSubsystem<UActivitySubsystem>())
		{
			const FTreasureBoxItemRow* TreasureBoxItem = ActivitySub->GetTreasureBoxItem(BoxID);

			if (TreasureBoxItem)
			{
				// 使用 UE 官方异步加载方式
				RewardIconImage->SetBrushFromSoftTexture(TreasureBoxItem->BoxIcon);
				RewardIconImage->SetColorAndOpacity(FLinearColor::White);
			}
			else
			{
				UE_LOG(LogTemp, Error, TEXT("DayItemWidget: 未找到宝箱配置, BoxID: %d"), BoxID);
				RewardIconImage->SetColorAndOpacity(FLinearColor(0.5f, 0.5f, 0.5f, 1.0f));
			}
		}
	}

	// 设置数量文本
	FString DisplayText = FString::Printf(TEXT("X%d"), RewardCount);
	RewardCountText->SetText(FText::FromString(DisplayText));
	// [修复] 删除原本强行 SetFont Size=24 的代码 — 同上, 字体大小交给蓝图。
}


/**
 * UDailyLoginDayItemWidget::AddRewardIcon
 *
 * 通过 RewardItemID 查询 FItemDetailRow: 直接操作 RewardIconImage + RewardCountText
 * 1. GetItemDetail(RewardID) -> SetBrushFromSoftTexture
 * 2. 失败 -> 灰色占位符
 * 3. RewardCountText "X{count}" 字体 24pt
 */
void UDailyLoginDayItemWidget::AddRewardIcon(int32 RewardID, int32 RewardCount)
{
	UE_LOG(LogTemp, Warning, TEXT("=== AddRewardIcon 被调用 === RewardID: %d, Count: %d"), RewardID, RewardCount);

	// 防御检查
	if (!RewardIconImage)
	{
		UE_LOG(LogTemp, Error, TEXT("DayItemWidget: RewardIconImage 未绑定，无法添加图标"));
		return;
	}

	if (!RewardCountText)
	{
		UE_LOG(LogTemp, Error, TEXT("DayItemWidget: RewardCountText 未绑定，无法设置数量"));
		return;
	}

	// 检查 RewardID 是否有效
	if (RewardID <= 0)
	{
		UE_LOG(LogTemp, Error, TEXT("DayItemWidget: 无效的RewardID: %d"), RewardID);
		return;
	}

	// 通过 RewardID 查询 FItemDetailRow
	if (UGameInstance* GI = GetGameInstance())
	{
		if (UActivitySubsystem* ActivitySub = GI->GetSubsystem<UActivitySubsystem>())
		{
			const FItemDetailRow* ItemDetail = ActivitySub->GetItemDetail(RewardID);

			if (ItemDetail)
			{
				if (!ItemDetail->ItemIcon.IsNull())
				{
					// 使用 UE 官方异步加载方式
					RewardIconImage->SetBrushFromSoftTexture(ItemDetail->ItemIcon);
					RewardIconImage->SetColorAndOpacity(FLinearColor::White);
				}
				else
				{
					UE_LOG(LogTemp, Error, TEXT("DayItemWidget: ItemIcon 为空, RewardID: %d"), RewardID);
					RewardIconImage->SetColorAndOpacity(FLinearColor(0.5f, 0.5f, 0.5f, 1.0f));
				}
			}
			else
			{
				UE_LOG(LogTemp, Error, TEXT("DayItemWidget: 未找到物品配置, RewardID: %d"), RewardID);
				RewardIconImage->SetColorAndOpacity(FLinearColor(0.5f, 0.5f, 0.5f, 1.0f));
			}
		}
	}

	// 设置数量文本
	FString DisplayText = FString::Printf(TEXT("X%d"), RewardCount);
	RewardCountText->SetText(FText::FromString(DisplayText));
	// [修复] 删除原本强行 SetFont Size=24 的代码 — 第 3 处, 同上。
}
