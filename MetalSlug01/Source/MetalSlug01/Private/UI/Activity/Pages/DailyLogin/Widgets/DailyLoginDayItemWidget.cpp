#include "UI/Activity/Pages/DailyLogin/Widgets/DailyLoginDayItemWidget.h"
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

void UDailyLoginDayItemWidget::Init(int32 InDayIndex, int32 InCurrentProgress, ERewardState RewardState, bool bInIsSpecialReward)
{
    // 1. 初始化所有成员变量
    DayIndex = InDayIndex;
    CurrentProgress = InCurrentProgress;
    CurrentState = RewardState;
    bIsSpecialReward = bInIsSpecialReward; // 设置是否为特殊奖励

    // 2. 清空奖励容器中的所有子控件
    if (RewardContainer)
    {
        RewardContainer->ClearChildren();
    }

    // 输出调试信息
    // // UE_LOG(LogTemp, Warning, TEXT("DailyLoginDayItemWidget::Init - Day: %d, IsSpecial: %s"), 
    //        DayIndex, bIsSpecialReward ? TEXT("True") : TEXT("False"));

    // 3. 更新视觉状态，确保所有成员变量已初始化
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
        if (DayIndex == CurrentProgress + 1)
        {
            ButtonText->SetText(FText::FromString(TEXT("明日可领取")));
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
    AddRewardIcon(ConfigRow.RewardItemID, ConfigRow.RewardCount);
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

void UDailyLoginDayItemWidget::AddRewardIcon(int32 RewardID, int32 RewardCount)
{
    // 1. 全局检查:确保奖励容器和奖励图标类已设置
    if (!RewardContainer)
    {
        UE_LOG(LogTemp, Error, TEXT("DayItemWidget: RewardContainer (容器) 为空，无法添加图标"));
        return;
    }

    if (!RewardIconClass)
    {
        // UE_LOG(LogTemp, Warning, TEXT("DayItemWidget: RewardIconClass (图标类) 未赋值，无法添加图标"));
        return;
    }

    // 2. 清空奖励容器中的所有子控件
    if (RewardContainer)
    {
        int32 ChildCountBefore = RewardContainer->GetChildrenCount();
        RewardContainer->ClearChildren();
        // UE_LOG(LogTemp, Log, TEXT("清空奖励容器前有 %d 个子控件"), ChildCountBefore);
    }

    // 3. 创建奖励图标 Widget
    UUserWidget* IconWidget = CreateWidget<UUserWidget>(GetWorld(), RewardIconClass);
    if (IconWidget)
    {
        // 4. 将图标添加到容器中
        RewardContainer->AddChild(IconWidget);

        // 5. 获取图标 Widget 中的控件
        // 假设 WBP_RewardIcon 中有 RewardImage(Image) 和 CountText(TextBlock) 控件
        UImage* RewardImage = Cast<UImage>(IconWidget->GetWidgetFromName(TEXT("RewardImage")));
        UTextBlock* CountText = Cast<UTextBlock>(IconWidget->GetWidgetFromName(TEXT("CountText")));

        // UE_LOG(LogTemp, Log, TEXT("开始获取图标控件:RewardImage指针: %s, CountText指针: %s"), 
        //     RewardImage ? TEXT("有效") : TEXT("无效"), 
        //     CountText ? TEXT("有效") : TEXT("无效"));

        if (RewardImage)
        {
            // 根据 RewardID 查找对应的奖励信息并设置
            // UE_LOG(LogTemp, Log, TEXT("开始查找奖励信息，RewardID: %d"), RewardID);
            
            if (UGameInstance* GI = GetGameInstance())
            {
                if (UActivitySubsystem* ActivitySub = GI->GetSubsystem<UActivitySubsystem>())
                {
                    // 获取所有奖励信息
                    TArray<FDailyLoginConfigRow*> AllRewards = ActivitySub->GetDailyLoginConfigs(101);
                    // UE_LOG(LogTemp, Log, TEXT("获取到 %d 个奖励信息"), AllRewards.Num());
                    
                    // 查找匹配的 RewardID
                    bool bFoundConfig = false;
                    for (auto* RewardRow : AllRewards)
                    {
                        if (RewardRow && RewardRow->RewardItemID == RewardID)
                        {
                            // UE_LOG(LogTemp, Log, TEXT("找到匹配的奖励信息，RewardID: %d"), RewardID);
                            bFoundConfig = true;
                            
                            // 查找资源
                            // 暂时注释复杂日志避免语法错误
                            
                            // 查找详细信息
                            if (!RewardRow->RewardIcon.IsValid())
                            {
                                // UE_LOG(LogTemp, Warning, TEXT("RewardIcon属性:"));
                                // UE_LOG(LogTemp, Warning, TEXT("  - 是否正在加载: %s"), RewardRow->RewardIcon.IsPending() ? TEXT("是") : TEXT("否"));
                                // UE_LOG(LogTemp, Warning, TEXT("  - 是否为null: %s"), RewardRow->RewardIcon.IsNull() ? TEXT("是") : TEXT("否"));
                            }
                            
                            if (RewardRow->RewardIcon.IsValid())
                            {
                                // 使用同步加载确保资源有效
                                UTexture2D* Texture = RewardRow->RewardIcon.LoadSynchronous();
                                // 暂时注释复杂日志避免语法错误
                                
                                if (Texture)
                                {
                                    // UE_LOG(LogTemp, Log, TEXT("正确设置奖励图标控件"));
                                    RewardImage->SetBrushFromTexture(Texture);
                                    // 设置图标大小为原始大小
                                    RewardImage->SetRenderScale(FVector2D(2.5f, 7.0f)); // 2倍大小，宽高比为1:2.8
                                    
                                    // 强制设置图标大小
                                    RewardImage->SetDesiredSizeOverride(FVector2D(64.0f, 64.0f));
                                    
                                    // 暂时注释复杂日志避免语法错误
                                    
                                    // 输出图标控件信息
                                    // UE_LOG(LogTemp, Log, TEXT("图标控件:"));
                                    // UE_LOG(LogTemp, Log, TEXT("  - 控件名称: %s"), *RewardImage->GetName());
                                    // UE_LOG(LogTemp, Log, TEXT("  - 父控件: %s"), RewardImage->GetParent() ? *RewardImage->GetParent()->GetName() : TEXT("无"));
                                    // UE_LOG(LogTemp, Log, TEXT("  - 画布大小: %s"), *RewardImage->GetCachedGeometry().GetLocalSize().ToString());
                                    
                                    // UE_LOG(LogTemp, Log, TEXT("成功设置奖励图标: ID %d, 资源 %s, 大小为原始大小"), 
                                    //     RewardID, Texture->GetName());
                                }
                                else
                                {
                                    // UE_LOG(LogTemp, Warning, TEXT("无法加载资源: ID %d"), RewardID);
                                }
                            }
                            else
                            {
                                // UE_LOG(LogTemp, Warning, TEXT("奖励信息中没有有效资源: ID %d"), RewardID);
                                
                                // 如果处于pending状态，尝试异步加载
                                if (!RewardRow->RewardIcon.IsNull() && RewardRow->RewardIcon.IsPending())
                                {
                                    // UE_LOG(LogTemp, Log, TEXT("奖励信息处于pending状态，使用异步加载: ID %d"), RewardID);
                                    
                                    // 使用同步异步加载
                                    FLoadAssetAsyncOptionalParams Params;
                                    RewardRow->RewardIcon.LoadAsync(
                                        FLoadSoftObjectPathAsyncDelegate::CreateUObject(this, &UDailyLoginDayItemWidget::OnTextureLoaded, RewardImage, RewardID),
                                        Params
                                    );
                                }
                            }
                            break;
                        }
                    }
                    
                    if (!bFoundConfig)
                    {
                        // UE_LOG(LogTemp, Warning, TEXT("未找到匹配的奖励信息，RewardID: %d"), RewardID);
                    }
                }
                else
                {
                    UE_LOG(LogTemp, Error, TEXT("无法获取ActivitySubsystem"));
                }
            }
            else
            {
                UE_LOG(LogTemp, Error, TEXT("无法获取GameInstance"));
            }
        }

        if (CountText)
        {
            // 设置奖励数量文本
            CountText->SetText(FText::AsNumber(RewardCount));
            // // UE_LOG(LogTemp, Log, TEXT("设置奖励数量: %d"), RewardCount);
        }
        
        // // UE_LOG(LogTemp, Log, TEXT("成功添加奖励图标: ID %d, 数量 %d"), RewardID, RewardCount);
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
