#include "UI/Activity/Pages/DailyLogin/DailyLoginDayItemWidget.h"
#include "Components/TextBlock.h"
#include "Components/Button.h"
#include "Components/Image.h"
#include "Components/Widget.h"
#include "Components/CanvasPanelSlot.h"
#include "Styling/SlateTypes.h"
#include "UI/Activity/Core/ActivitySubsystem.h"
#include "UI/Activity/Pages/DailyLogin/DailyLoginPage.h"  // 临时添加
#include "Engine/Texture2D.h"
#include "TimerManager.h"
#include "Async/Async.h"

void UDailyLoginDayItemWidget::NativeConstruct()
{
    Super::NativeConstruct();
    
    // 检查必需的控件是否已绑定
    if (!RewardContainer)
    {
        UE_LOG(LogTemp, Error, TEXT("DailyLoginDayItemWidget: RewardContainer未绑定，请检查蓝图中的控件名称！"));
    }
    
    if (!RewardIconClass)
    {
        UE_LOG(LogTemp, Error, TEXT("DailyLoginDayItemWidget: RewardIconClass未设置，请在编辑器中指定奖励图标Widget类！"));
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
                    // UE_LOG(LogTemp, Log, TEXT("延迟设置按钮颜色为黄色(可领取)"));
                    break;
                case ERewardState::Claimed:
                    ClaimButton->SetBackgroundColor(ClaimedDisabledColor); // 灰色
                    // UE_LOG(LogTemp, Log, TEXT("延迟设置按钮颜色为灰色(已领取)"));
                    break;
                case ERewardState::Incomplete:
                    if (DayIndex == CurrentProgress + 1)
                    {
                        ClaimButton->SetBackgroundColor(TomorrowColor); // 基础颜色
                        // UE_LOG(LogTemp, Log, TEXT("延迟设置按钮颜色为黄色(明日可领取)"));
                    }
                    else
                    {
                        ClaimButton->SetBackgroundColor(IncompleteDisabledColor); // 灰色
                        // UE_LOG(LogTemp, Log, TEXT("延迟设置按钮颜色为灰色(未完成)"));
                    }
                    break;
                }
                ClaimButton->InvalidateLayoutAndVolatility();
            }
        }, 0.1f, false); // 延迟0.1秒执行
    }
}

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

    // 3. 如果提供了ActivitySubsystem，自动加载奖励图标
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

void UDailyLoginDayItemWidget::UpdateVisualState()
{
    // 根据是否为大奖控制背景控件的显示/隐藏
    if (BigRewardBackground)
    {
        BigRewardBackground->SetVisibility(bIsSpecialReward ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
        // UE_LOG(LogTemp, Warning, TEXT("BigRewardBackground visibility set to: %s"), 
        //     bIsSpecialReward ? TEXT("Visible") : TEXT("Collapsed"));
    }

    if (CharacterIcon)
    {
        CharacterIcon->SetVisibility(bIsSpecialReward ? ESlateVisibility::Visible : ESlateVisibility::Collapsed);
        // UE_LOG(LogTemp, Warning, TEXT("CharacterIcon visibility set to: %s"), 
        //     bIsSpecialReward ? TEXT("Visible") : TEXT("Collapsed"));
    }

    // 移除 OnSetupBigRewardStyle() 的调用，改为直接设置样式
    // if (bIsSpecialReward) { OnSetupBigRewardStyle(); }

    // 更新按钮状态和信息
    if (ClaimButton)
    {
        // UE_LOG(LogTemp, Log, TEXT("ClaimButton存在，当前状态: %d"), (int32)CurrentState);
        // UE_LOG(LogTemp, Log, TEXT("ClaimButton可见性: %s"), 
        //     ClaimButton->GetVisibility() == ESlateVisibility::Visible ? TEXT("可见") : TEXT("不可见"));
        // UE_LOG(LogTemp, Log, TEXT("ClaimButton启用状态: %s"), 
        //     ClaimButton->GetIsEnabled() ? TEXT("启用") : TEXT("禁用"));
        
        // 检查按钮样式
        const FButtonStyle& Style = ClaimButton->GetStyle();
        // UE_LOG(LogTemp, Log, TEXT("按钮Normal样式资源: %s"), 
        //     Style.Normal.GetResourceObject() ? Style.Normal.GetResourceObject()->GetName() : TEXT("无"));
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
        // UE_LOG(LogTemp, Log, TEXT("使用基础方法设置背景色: R=%f G=%f B=%f A=%f"), 
        //     ClaimedColor.R, ClaimedColor.G, ClaimedColor.B, ClaimedColor.A);
        // 设置按钮缩放
        ClaimButton->SetRenderScale(FVector2D(1.5f, 1.5f));
        ClaimButton->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
        // 通过CanvasPanelSlot设置ZOrder
        if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(ClaimButton->Slot))
        {
            CanvasSlot->SetZOrder(1000);
        }
        // UE_LOG(LogTemp, Log, TEXT("设置按钮为可领取状态 - 黄色背景色和缩放"));
        break;

    case ERewardState::Claimed:
        ClaimButton->SetIsEnabled(false);
        ButtonText->SetText(FText::FromString(TEXT("已领取")));
        ClaimButton->SetRenderOpacity(0.3f);
        // 设置已领取状态颜色 - 灰色
        ClaimButton->SetBackgroundColor(ClaimedDisabledColor);
        // UE_LOG(LogTemp, Log, TEXT("设置按钮颜色为灰色(已领取)"));
        
        // 强制刷新按钮样式
        ClaimButton->InvalidateLayoutAndVolatility();
        // UE_LOG(LogTemp, Log, TEXT("强制刷新按钮样式"));
        // 设置按钮缩放
        ClaimButton->SetRenderScale(FVector2D(1.5f, 1.5f));
        ClaimButton->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
        // 通过CanvasPanelSlot设置ZOrder
        if (UCanvasPanelSlot* CanvasSlot = Cast<UCanvasPanelSlot>(ClaimButton->Slot))
        {
            CanvasSlot->SetZOrder(1000);
        }
        // UE_LOG(LogTemp, Log, TEXT("设置按钮为已领取状态 - 灰色背景色和缩放"));
        break;

    case ERewardState::Incomplete:
        ClaimButton->SetIsEnabled(false);
        ClaimButton->SetRenderOpacity(0.5f);

        // 使用成员变量判断明日可领取
        // Progress=N表示第1-N天可领取，第(N+1)天为明日可领
        if (DayIndex == CurrentProgress + 1)
        {
            UE_LOG(LogTemp, Warning, TEXT("UpdateVisualState: 第%d天设置为明日可领状态 (CurrentProgress=%d)"), DayIndex, CurrentProgress);
            ButtonText->SetText(FText::FromString(TEXT("明日可领")));
            // 设置明日可领取状态颜色 - 黄色
            ClaimButton->SetBackgroundColor(TomorrowColor);
            // 设置按钮缩放
            ClaimButton->SetRenderScale(FVector2D(1.5f, 1.5f));
            ClaimButton->SetRenderTransformPivot(FVector2D(0.5f, 0.5f));
            // 通过CanvasPanelSlot设置ZOrder
            if (UCanvasPanelSlot* CanvasSlot2 = Cast<UCanvasPanelSlot>(ClaimButton->Slot))
            {
                CanvasSlot2->SetZOrder(1000);
            }
            // UE_LOG(LogTemp, Log, TEXT("设置按钮为明日可领取状态 - 黄色背景色和缩放"));
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
            // 通过CanvasPanelSlot设置ZOrder
            if (UCanvasPanelSlot* CanvasSlot2 = Cast<UCanvasPanelSlot>(ClaimButton->Slot))
            {
                CanvasSlot2->SetZOrder(1000);
            }
            // UE_LOG(LogTemp, Log, TEXT("设置按钮为未完成状态 - 灰色背景色和缩放"));
        }
        break;
    }
}

void UDailyLoginDayItemWidget::OnClaimButtonClicked()
{
    // UE_LOG(LogTemp, Error, TEXT("==== 按钮点击事件 ===="));
    // 输出调试信息，确保传入的参数正确
    // UE_LOG(LogTemp, Error, TEXT("--- 按钮点击事件，开始处理参数: %d ---"), DayIndex);
    // UE_LOG(LogTemp, Error, TEXT("--- 当前状态: %d, 是否为大奖: %s ---"), (int32)CurrentState, bIsSpecialReward ? TEXT("是") : TEXT("否"));
    
    if (CurrentState != ERewardState::Claimable) 
    {
        // // UE_LOG(LogTemp, Warning, TEXT("原始状态不是 Claimable，当前状态: %d"), (int32)CurrentState);
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
                // // UE_LOG(LogTemp, Warning, TEXT("第%d天奖励类型: %d"), DayIndex, (int32)RewardType);
                
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
            else
            {
                // UE_LOG(LogTemp, Error, TEXT("未找到第%d天的奖励信息，"), DayIndex);
            }
        }
    }
}


void UDailyLoginDayItemWidget::AddRewardIconFromConfig(const FDailyLoginConfigRow& ConfigRow)
{
    UE_LOG(LogTemp, Warning, TEXT("=== AddRewardIconFromConfig 被调用 === DayIndex: %d, RewardItemID: %d, RewardType: %d"), 
        ConfigRow.DayIndex, ConfigRow.RewardItemID, (int32)ConfigRow.RewardType);
    
    // 根据奖励类型决定图标来源
    if (ConfigRow.RewardType == ELoginRewardType::Box)
    {
        UE_LOG(LogTemp, Warning, TEXT("  奖励类型为Box，从TreasureBoxItemRow表查找宝箱图标"));
        // 礼包/宝箱类型：从TreasureBoxItemRow表查找BoxIcon
        AddRewardIconFromTreasureBox(ConfigRow.RewardItemID, ConfigRow.RewardCount);
    }
    else
    {
        UE_LOG(LogTemp, Warning, TEXT("  奖励类型为普通物品，使用RewardItemID查询ItemIcon"));
        // 其他类型：通过RewardItemID查询FItemDetailRow的ItemIcon
        AddRewardIcon(ConfigRow.RewardItemID, ConfigRow.RewardCount);
    }
}

void UDailyLoginDayItemWidget::ClearRewardIcons()
{
    if (RewardContainer)
    {
        int32 ChildCountBefore = RewardContainer->GetChildrenCount();
        RewardContainer->ClearChildren();
        // UE_LOG(LogTemp, Log, TEXT("清空奖励容器前有 %d 个子控件"), ChildCountBefore);
    }
}

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

// 清空操作已移到Init函数中统一处理

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
            UE_LOG(LogTemp, Warning, TEXT("检查BoxImage状态 - IsValid: %s, IsNull: %s, IsPending: %s"), 
                BoxImage.IsValid() ? TEXT("是") : TEXT("否"),
                BoxImage.IsNull() ? TEXT("是") : TEXT("否"),
                BoxImage.IsPending() ? TEXT("是") : TEXT("否"));
            UE_LOG(LogTemp, Warning, TEXT("开始异步加载BoxImage纹理"));
            // 使用UE官方异步加载方式
            RewardImage->SetBrushFromSoftTexture(BoxImage);
            RewardImage->SetRenderScale(FVector2D(2.5f, 7.0f));
            RewardImage->SetDesiredSizeOverride(FVector2D(64.0f, 64.0f));
            UE_LOG(LogTemp, Warning, TEXT("✅ 已设置软引用纹理，UE会自动异步加载: %s"), 
                *BoxImage.ToString());
        }
        else
        {
            UE_LOG(LogTemp, Error, TEXT("❌ RewardImage为空或BoxImage无效"));
            if (!RewardImage)
            {
                UE_LOG(LogTemp, Error, TEXT("   RewardImage控件未找到"));
            }
            if (!BoxImage.IsValid())
            {
                UE_LOG(LogTemp, Error, TEXT("   BoxImage资源无效"));
            }
        }

        if (CountText)
        {
            CountText->SetText(FText::AsNumber(RewardCount));
        }
    }
}

void UDailyLoginDayItemWidget::AddRewardIconFromTreasureBox(int32 BoxID, int32 RewardCount)
{
    UE_LOG(LogTemp, Warning, TEXT("=== AddRewardIconFromTreasureBox 被调用 === BoxID: %d, Count: %d"), BoxID, RewardCount);
    
    // 专门处理从TreasureBoxItemRow表查找宝箱图标的逻辑
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

// 清空操作已移到Init函数中统一处理

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
            UE_LOG(LogTemp, Warning, TEXT("开始处理宝箱图标，BoxID: %d"), BoxID);
            // 通过BoxID从TreasureBoxItemRow表获取BoxIcon
            if (UGameInstance* GI = GetGameInstance())
            {
                if (UActivitySubsystem* ActivitySub = GI->GetSubsystem<UActivitySubsystem>())
                {
                    UE_LOG(LogTemp, Warning, TEXT("✅ 获取到ActivitySubsystem"));
                    
                    // 从TreasureBoxItemRow表查询BoxIcon
                    const FTreasureBoxItemRow* TreasureBoxItem = ActivitySub->GetTreasureBoxItem(BoxID);
                    
                    UE_LOG(LogTemp, Warning, TEXT("🔍 GetTreasureBoxItem返回结果: %s"), TreasureBoxItem ? TEXT("找到记录") : TEXT("未找到记录"));
                    
                    // 添加详细的调试信息
                    if (TreasureBoxItem)
                    {
                        UE_LOG(LogTemp, Warning, TEXT("🔍 TreasureBoxItem记录详情:"));
                        UE_LOG(LogTemp, Warning, TEXT("   BoxID: %d"), TreasureBoxItem->BoxID);
                        UE_LOG(LogTemp, Warning, TEXT("   ItemID: %d"), TreasureBoxItem->ItemID);
                        UE_LOG(LogTemp, Warning, TEXT("   BoxIcon IsValid: %s"), TreasureBoxItem->BoxIcon.IsValid() ? TEXT("是") : TEXT("否"));
                        UE_LOG(LogTemp, Warning, TEXT("   BoxIcon IsNull: %s"), TreasureBoxItem->BoxIcon.IsNull() ? TEXT("是") : TEXT("否"));
                        UE_LOG(LogTemp, Warning, TEXT("   BoxIcon IsPending: %s"), TreasureBoxItem->BoxIcon.IsPending() ? TEXT("是") : TEXT("否"));
                        
                        if (!TreasureBoxItem->BoxIcon.IsNull())
                        {
                            UE_LOG(LogTemp, Warning, TEXT("✅ 找到TreasureBoxItem记录，使用UMG异步加载BoxIcon"));
                            // 使用UMG原生异步接口 - 更优雅的方式
                            // 注意：即使资源处于IsPending状态(!IsNull() && !IsValid())，UMG也会正确处理
                            RewardImage->SetBrushFromSoftTexture(TreasureBoxItem->BoxIcon);
                            RewardImage->SetRenderScale(FVector2D(2.5f, 7.0f));
                            RewardImage->SetDesiredSizeOverride(FVector2D(64.0f, 64.0f));
                            UE_LOG(LogTemp, Warning, TEXT("✅ 已设置软引用纹理，UMG会自动异步加载: BoxID %d"), BoxID);
                        }
                        else
                        {
                            UE_LOG(LogTemp, Error, TEXT("❌ BoxIcon资源为空，BoxID: %d"), BoxID);
                            // 显示灰色占位符
                            RewardImage->SetColorAndOpacity(FLinearColor(0.5f, 0.5f, 0.5f, 1.0f)); // 灰色
                            RewardImage->SetRenderScale(FVector2D(2.5f, 7.0f));
                            RewardImage->SetDesiredSizeOverride(FVector2D(64.0f, 64.0f));
                            UE_LOG(LogTemp, Warning, TEXT("⚠️ 显示灰色占位符图标: BoxID %d"), BoxID);
                        }
                    }
                    else
                    {
                        UE_LOG(LogTemp, Error, TEXT("❌ 未找到TreasureBoxItem记录，BoxID: %d"), BoxID);
                        // 显示灰色占位符
                        RewardImage->SetColorAndOpacity(FLinearColor(0.5f, 0.5f, 0.5f, 1.0f)); // 灰色
                        RewardImage->SetRenderScale(FVector2D(2.5f, 7.0f));
                        RewardImage->SetDesiredSizeOverride(FVector2D(64.0f, 64.0f));
                        UE_LOG(LogTemp, Warning, TEXT("⚠️ 显示灰色占位符图标: BoxID %d"), BoxID);
                    }
                }
                else
                {
                    UE_LOG(LogTemp, Error, TEXT("❌ 无法获取ActivitySubsystem"));
                }
            }
            else
            {
                UE_LOG(LogTemp, Error, TEXT("❌ 无法获取GameInstance"));
            }
        }
        else
        {
            UE_LOG(LogTemp, Error, TEXT("❌ RewardImage控件未找到"));
        }

        if (CountText)
        {
            CountText->SetText(FText::AsNumber(RewardCount));
        }
    }
}

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
    
    // 检查RewardID是否有效
    if (RewardID <= 0)
    {
        UE_LOG(LogTemp, Error, TEXT("DayItemWidget: 无效的RewardID: %d"), RewardID);
        return;
    }

// 2. 清空操作已移到Init函数中统一处理

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
            UE_LOG(LogTemp, Warning, TEXT("开始处理普通奖励图标，RewardID: %d"), RewardID);
            // 通过RewardItemID从FItemDetailRow表获取ItemIcon
            if (UGameInstance* GI = GetGameInstance())
            {
                if (UActivitySubsystem* ActivitySub = GI->GetSubsystem<UActivitySubsystem>())
                {
                    UE_LOG(LogTemp, Warning, TEXT("✅ 获取到ActivitySubsystem"));
                    // TODO: 这里需要从FItemDetailRow表查询ItemIcon
                    // 暂时保持原有逻辑，后续需要实现从物品详情表获取图标
                    
                    // 获取所有奖励信息以查找匹配的RewardItemID
                    TArray<FDailyLoginConfigRow*> AllRewards = ActivitySub->GetDailyLoginConfigs(101);
                    UE_LOG(LogTemp, Warning, TEXT("获取到 %d 个奖励配置"), AllRewards.Num());
                    bool bFoundConfig = false;
                    
                    for (auto* RewardRow : AllRewards)
                    {
                        if (RewardRow && RewardRow->RewardItemID == RewardID)
                        {
                            bFoundConfig = true;
                            UE_LOG(LogTemp, Warning, TEXT("✅ 找到匹配的奖励信息，RewardID: %d"), RewardID);
                            
                            // 正确逻辑：非Box类型应该查询FItemDetailRow.ItemIcon
                            UE_LOG(LogTemp, Warning, TEXT("🔍 查询ItemDetail表获取图标，ItemID: %d"), RewardID);
                            
                            // 通过ActivitySubsystem查询ItemDetail
                            const FItemDetailRow* ItemDetail = ActivitySub->GetItemDetail(RewardID);
                            
                            UE_LOG(LogTemp, Warning, TEXT("🔍 GetItemDetail返回结果: %s"), ItemDetail ? TEXT("找到记录") : TEXT("未找到记录"));
                            
                            // 添加详细的调试信息
                            if (ItemDetail)
                            {
                                UE_LOG(LogTemp, Warning, TEXT("🔍 ItemDetail记录详情:"));
                                UE_LOG(LogTemp, Warning, TEXT("   ItemID: %d"), ItemDetail->ItemID);
                                UE_LOG(LogTemp, Warning, TEXT("   ItemName: %s"), *ItemDetail->ItemName.ToString());
                                UE_LOG(LogTemp, Warning, TEXT("   ItemIcon IsValid: %s"), ItemDetail->ItemIcon.IsValid() ? TEXT("是") : TEXT("否"));
                                UE_LOG(LogTemp, Warning, TEXT("   ItemIcon IsNull: %s"), ItemDetail->ItemIcon.IsNull() ? TEXT("是") : TEXT("否"));
                                UE_LOG(LogTemp, Warning, TEXT("   ItemIcon IsPending: %s"), ItemDetail->ItemIcon.IsPending() ? TEXT("是") : TEXT("否"));
                                
                                if (!ItemDetail->ItemIcon.IsNull())
                                {
                                    UE_LOG(LogTemp, Warning, TEXT("✅ 找到ItemDetail记录，使用UMG异步加载ItemIcon"));
                                    // 使用UMG原生异步接口 - 更优雅的方式
                                    // 注意：即使资源处于IsPending状态(!IsNull() && !IsValid())，UMG也会正确处理
                                    RewardImage->SetBrushFromSoftTexture(ItemDetail->ItemIcon);
                                    RewardImage->SetRenderScale(FVector2D(2.5f, 7.0f));
                                    RewardImage->SetDesiredSizeOverride(FVector2D(64.0f, 64.0f));
                                    UE_LOG(LogTemp, Warning, TEXT("✅ 已设置软引用纹理，UMG会自动异步加载: ID %d"), RewardID);
                                }
                                else
                                {
                                    UE_LOG(LogTemp, Error, TEXT("❌ ItemIcon资源为空，ItemID: %d"), RewardID);
                                    // 显示灰色占位符
                                    RewardImage->SetColorAndOpacity(FLinearColor(0.5f, 0.5f, 0.5f, 1.0f)); // 灰色
                                    RewardImage->SetRenderScale(FVector2D(2.5f, 7.0f));
                                    RewardImage->SetDesiredSizeOverride(FVector2D(64.0f, 64.0f));
                                    UE_LOG(LogTemp, Warning, TEXT("⚠️ 显示灰色占位符图标: ID %d"), RewardID);
                                }
                            }
                            else
                            {
                                UE_LOG(LogTemp, Error, TEXT("❌ 未找到ItemDetail记录，ItemID: %d"), RewardID);
                                // 显示灰色占位符
                                RewardImage->SetColorAndOpacity(FLinearColor(0.5f, 0.5f, 0.5f, 1.0f)); // 灰色
                                RewardImage->SetRenderScale(FVector2D(2.5f, 7.0f));
                                RewardImage->SetDesiredSizeOverride(FVector2D(64.0f, 64.0f));
                                UE_LOG(LogTemp, Warning, TEXT("⚠️ 显示灰色占位符图标: ID %d"), RewardID);
                            }
                            break;
                        }
                    }
                    
                    if (!bFoundConfig)
                    {
                        UE_LOG(LogTemp, Error, TEXT("❌ 未找到匹配的奖励信息，RewardID: %d"), RewardID);
                    }
                }
                else
                {
                    UE_LOG(LogTemp, Error, TEXT("❌ 无法获取ActivitySubsystem"));
                }
            }
            else
            {
                UE_LOG(LogTemp, Error, TEXT("❌ 无法获取GameInstance"));
            }
        }
        else
        {
            UE_LOG(LogTemp, Error, TEXT("❌ RewardImage控件未找到"));
        }

        if (CountText)
        {
            // 设置奖励数量文本
            CountText->SetText(FText::AsNumber(RewardCount));
        }
    }
}

void UDailyLoginDayItemWidget::OnTextureLoaded(const FSoftObjectPath& Path, UObject* LoadedObject, UImage* RewardImage, int32 RewardID)
{
    if (UTexture2D* LoadedTexture = Cast<UTexture2D>(LoadedObject))
    {
        // // UE_LOG(LogTemp, Log, TEXT("异步加载完成: ID %d, 资源 %s"), RewardID, *LoadedTexture->GetName());
        
        if (RewardImage && RewardImage->IsValidLowLevel())
        {
            // 在游戏线程中更新UI
            AsyncTask(ENamedThreads::GameThread, [RewardImage, LoadedTexture]()
            {
                if (RewardImage && RewardImage->IsValidLowLevel())
                {
                    RewardImage->SetBrushFromTexture(LoadedTexture);
                    RewardImage->SetRenderScale(FVector2D(2.5f, 7.0f));
                    RewardImage->SetDesiredSizeOverride(FVector2D(64.0f, 64.0f));
                    RewardImage->InvalidateLayoutAndVolatility();
                    // // UE_LOG(LogTemp, Log, TEXT("异步加载后更新UI"));
                }
            });
        }
    }
    else
    {
        // // UE_LOG(LogTemp, Warning, TEXT("异步加载失败: ID %d"), RewardID);
    }
}
