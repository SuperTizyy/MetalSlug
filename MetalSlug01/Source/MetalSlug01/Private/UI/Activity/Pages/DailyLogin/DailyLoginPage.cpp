#include "UI/Activity/Pages/DailyLogin/DailyLoginPage.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/Button.h"
#include "Components/EditableText.h"
#include "Components/ContentWidget.h"
#include "Components/PanelWidget.h"
#include "Components/CanvasPanel.h"

// 核心：引入新的 Subsystem 和 存档类
#include "UI/Activity/Core/ActivitySubsystem.h"
#include "UI/Activity/Data/DailyLoginSave.h"
#include "UI/Activity/Pages/DailyLogin/Widgets/TreasureBoxWidget.h"
#include "UI/Activity/Pages/DailyLogin/Widgets/DailyLoginDayItemWidget.h"
#include "UI/Activity/Pages/DailyLogin/Widgets/RewardOptionWidget.h"
#include "UI/Activity/Pages/DailyLogin/DailyLoginCheatWidget.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "UI/Activity/Pages/DailyLogin/Widgets/ClaimSuccessWidget.h"
// 移除重复的导航菜单包含，保持原有功能

void UDailyLoginPage::NativeConstruct()
{
	Super::NativeConstruct();

	// 初始化 Subsystem 指针
	if (UGameInstance* GI = GetGameInstance())
	{
		ActivitySub = GI->GetSubsystem<UActivitySubsystem>();
	}

	// 绑定数据更新回调（如果 Subsystem 有广播）
	if (ActivitySub)
	{
		ActivitySub->OnActivityDataChanged.AddDynamic(this, &UDailyLoginPage::RefreshRewardList);
	}

	// 绑定全部领取按钮点击事件
	// // UE_LOG(LogTemp, Warning, TEXT("检查ClaimAllButton是否存在: %s"), ClaimAllButton ? TEXT("存在") : TEXT("不存在"));
	if (ClaimAllButton)
	{
		ClaimAllButton->OnClicked.AddDynamic(this, &UDailyLoginPage::OnClaimAllButtonClicked);
		// UE_LOG(LogTemp, Warning, TEXT("成功绑定全部领取按钮事件"));
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("ClaimAllButton为空！请检查蓝图中是否正确设置了按钮绑定"));
	}

	// CheatWidget相关代码保持不变
	// CheatWidget会在自己的NativeConstruct中自动绑定事件
	// 这里不需要额外处理

	// 初次刷新
	RefreshRewardList();
}

void UDailyLoginPage::NativeDestruct()
{
	// DailyLoginPage清理逻辑
	Super::NativeDestruct();
}

void UDailyLoginPage::RefreshRewardList()
{
    UE_LOG(LogTemp, Warning, TEXT("=== DailyLoginPage::RefreshRewardList 被调用 ==="));
    
    if (!ActivitySub || !DayListScroll || !ItemClass) return;

    // 清理 BigRewardItem 中的奖励图标（防止重复添加）
    FName BigRewardItemName_Local(TEXT("BigRewardItem"));
    UWidget* BigRewardItemWidgetRef_Local = GetWidgetFromName(BigRewardItemName_Local);
    if (BigRewardItemWidgetRef_Local)
    {
        UDailyLoginDayItemWidget* BigRewardItemAsDayWidget = Cast<UDailyLoginDayItemWidget>(BigRewardItemWidgetRef_Local);
        if (BigRewardItemAsDayWidget)
        {
            BigRewardItemAsDayWidget->ClearRewardIcons();
            UE_LOG(LogTemp, Log, TEXT("已清空 BigRewardItem 的奖励容器"));
        }
    }

    DayListScroll->ClearChildren();
    FPlayerLoginRecord& Record = ActivitySub->GetOrInitPlayerRecord(101);
    
    const FActivityInfoRow* Info = ActivitySub->GetActivityInfo(101);
    if (!Info) return;

    // 存储大奖项控件
    UDailyLoginDayItemWidget* BigRewardItemWidget = nullptr;
    bool bHasBigReward = false;
        
    for (int32 i = 1; i <= Info->TotalDays; ++i)
    {
        ERewardState State = CalculateState(i);
        if (UWorld* World = GetWorld())
        {
            // 检查世界是否正在销毁
            if (World->bIsTearingDown)
            {
                // UE_LOG(LogTemp, Warning, TEXT("世界正在销毁，跳过Widget创建，天数: %d"), i);
                continue;
            }
            
            if (UDailyLoginDayItemWidget* NewItem = CreateWidget<UDailyLoginDayItemWidget>(World, ItemClass))
            {
                // 绑定奖励点击事件（先清除旧绑定防止重复）
                NewItem->OnRewardClicked.Clear();
                NewItem->OnRewardClicked.AddDynamic(this, &UDailyLoginPage::HandleRewardClick);
                
                // 获取当天的奖励配置，检查是否为特殊奖励
                TArray<FDailyLoginConfigRow*> Rewards = ActivitySub->GetRewardsByDay(101, i);
                bool bIsSpecial = false;
                
            // 检查当天是否有任一奖励被标记为特殊奖励
            for (auto* RewardRow : Rewards)
            {
                if (RewardRow && RewardRow->bIsSpecialReward)
                {
                    bIsSpecial = true;
                    break;
                }
            }
                
            // 关键：这里传入当前的 i、Record.Progress、State 和特殊奖励标志
            NewItem->Init(i, Record.Progress, State, bIsSpecial);
                
            // 根据 Is Special Reward 字段决定显示位置
            if (bIsSpecial)
            {
                // 特殊奖励：保存为大奖项，不添加到滚动列表
                BigRewardItemWidget = NewItem;
                bHasBigReward = true;
                // // UE_LOG(LogTemp, Warning, TEXT("第%d天是特殊奖励，将显示在大奖格子中"), i);
            }
            else
            {
                // 普通奖励：统一添加到滚动列表中
                // // UE_LOG(LogTemp, Warning, TEXT("第%d天是普通奖励，将添加到滚动列表中"), i);
                UScrollBoxSlot* NewSlot = Cast<UScrollBoxSlot>(DayListScroll->AddChild(NewItem));
                if (NewSlot)
                {
                    NewSlot->SetPadding(FMargin(80.0f)); // 增大间距
                }
                else
                {
                    DayListScroll->AddChild(NewItem);
                }
            }
                
            // 加载奖励图标
            for (auto* RewardRow : Rewards)
            {
                if (RewardRow)
                {
                    NewItem->AddRewardIconFromConfig(*RewardRow);
                }
            }
        }
    }
    }
    
    // 大奖项处理：基于 Is Special Reward 字段的特殊奖励显示控制
    // 通过 GetWidgetFromName 访问蓝图中的 BigRewardItem 控件
    FName BigRewardItemName(TEXT("BigRewardItem"));
    UWidget* BigRewardItemWidgetRef = GetWidgetFromName(BigRewardItemName);
    
    if (BigRewardItemWidgetRef)
    {
        // UE_LOG(LogTemp, Warning, TEXT("成功找到 BigRewardItem 控件，类型: %s"), 
        //     BigRewardItemWidgetRef->GetClass()->GetName());
    }
    else
    {
        // UE_LOG(LogTemp, Warning, TEXT("警告：未找到名为 BigRewardItem 的控件"));
    }
    
    if (BigRewardItemWidgetRef_Local && bHasBigReward)
    {
        // BigRewardItem 控件本身就是 DailyLoginDayItemWidget 类型
        UDailyLoginDayItemWidget* BigRewardItemAsDayWidget = Cast<UDailyLoginDayItemWidget>(BigRewardItemWidgetRef_Local);
        if (BigRewardItemAsDayWidget)
        {
            // 直接使用找到的 BigRewardItem 控件，更新其数据
            // 使用与其它奖励项相同的逻辑初始化
            FPlayerLoginRecord& BigRewardRecord = ActivitySub->GetOrInitPlayerRecord(101);
            ERewardState State = CalculateState(8); // 第8天
            
            // 获取大奖项天数的奖励配置（基于 Is Special Reward 字段）
            TArray<FDailyLoginConfigRow*> Rewards = ActivitySub->GetRewardsByDay(101, 8);
            bool bIsSpecial = false;
            for (auto* RewardRow : Rewards)
            {
                if (RewardRow && RewardRow->bIsSpecialReward)
                {
                    bIsSpecial = true;
                    break;
                }
            }
            
            // 初始化现有的 BigRewardItem 控件
            BigRewardItemAsDayWidget->Init(8, BigRewardRecord.Progress, State, bIsSpecial);
            
            // 绑定奖励点击事件（关键修复点）
            // 先清除旧的绑定，防止重复绑定
            BigRewardItemAsDayWidget->OnRewardClicked.Clear();
            BigRewardItemAsDayWidget->OnRewardClicked.AddDynamic(this, &UDailyLoginPage::HandleRewardClick);
            
            // 为该控件加载奖励图标
            for (auto* RewardRow : Rewards)
            {
                if (RewardRow)
                {
                    BigRewardItemAsDayWidget->AddRewardIconFromConfig(*RewardRow);
                }
            }
            
            // 强制设置 BigRewardItem 控件可见
            BigRewardItemAsDayWidget->SetVisibility(ESlateVisibility::Visible);
            BigRewardItemAsDayWidget->SetRenderOpacity(1.0f);
            
            // UE_LOG(LogTemp, Warning, TEXT("成功更新 BigRewardItem 控件（第8天），已强制设置可见"));
            // UE_LOG(LogTemp, Warning, TEXT("第8天点击事件绑定状态：%s"), 
            //     BigRewardItemAsDayWidget->OnRewardClicked.IsBound() ? TEXT("已绑定") : TEXT("未绑定"));
        }
        else
        {
            // UE_LOG(LogTemp, Warning, TEXT("警告：BigRewardItem 控件不是 DailyLoginDayItemWidget 类型"));
            // 降级方案：如果 BigRewardItem 不是 DayItemWidget 类型，仍将原始的 BigRewardItemWidget 添加到滚动框
            if (DayListScroll && BigRewardItemWidget)
            {
                UScrollBoxSlot* NewSlot = Cast<UScrollBoxSlot>(DayListScroll->AddChild(BigRewardItemWidget));
                if (NewSlot)
                {
                    NewSlot->SetPadding(FMargin(15.0f)); // 设置15像素间距
                }
                else
                {
                    DayListScroll->AddChild(BigRewardItemWidget);
                }
                BigRewardItemWidget->SetVisibility(ESlateVisibility::Visible);
                BigRewardItemWidget->SetRenderOpacity(1.0f);
                
                // 绑定点击事件（降级方案也要绑定）
                if (UDailyLoginDayItemWidget* DayItemWidget = Cast<UDailyLoginDayItemWidget>(BigRewardItemWidget))
                {
                    // 先清除旧的绑定，防止重复绑定
                    DayItemWidget->OnRewardClicked.Clear();
                    DayItemWidget->OnRewardClicked.AddDynamic(this, &UDailyLoginPage::HandleRewardClick);
                    // UE_LOG(LogTemp, Warning, TEXT("降级方案：已为 BigRewardItem 绑定点击事件"));
                }
                
                // UE_LOG(LogTemp, Warning, TEXT("降级方案：已将 BigRewardItem 添加到滚动列表并设置可见"));
            }
        }
    }
    else if (bHasBigReward)
    {
        if (!BigRewardItemWidgetRef_Local)
        {
            // UE_LOG(LogTemp, Warning, TEXT("警告：第8天是特殊奖励，但蓝图中找不到名为 BigRewardItem 的控件"));
        }
        
        // 降级方案：添加到主滚动框
        if (DayListScroll && BigRewardItemWidget)
        {
            UScrollBoxSlot* NewSlot = Cast<UScrollBoxSlot>(DayListScroll->AddChild(BigRewardItemWidget));
            if (NewSlot)
            {
                NewSlot->SetPadding(FMargin(15.0f)); // 设置15像素间距
            }
            else
            {
                DayListScroll->AddChild(BigRewardItemWidget);
            }
            BigRewardItemWidget->SetVisibility(ESlateVisibility::Visible);
            BigRewardItemWidget->SetRenderOpacity(1.0f);
            
            // 绑定点击事件（最终降级方案也要绑定）
            if (UDailyLoginDayItemWidget* DayItemWidget = Cast<UDailyLoginDayItemWidget>(BigRewardItemWidget))
            {
                DayItemWidget->OnRewardClicked.AddDynamic(this, &UDailyLoginPage::HandleRewardClick);
                // // UE_LOG(LogTemp, Warning, TEXT("最终降级方案：已为 BigRewardItem 绑定点击事件"));
            }
            
            // // UE_LOG(LogTemp, Warning, TEXT("最终降级方案：已将 BigRewardItem 添加到滚动列表并设置可见"));
        }
    }
}

// 移除导航相关函数，保持DailyLoginPage专注每日登录功能

void UDailyLoginPage::OnBigRewardClicked()
{
    // UE_LOG(LogTemp, Error, TEXT("=== OnBigRewardClicked 被调用 ==="));
    
    // 添加防重复调用保护
    static bool bAlreadyCalled = false;
    if (bAlreadyCalled)
    {
        // // UE_LOG(LogTemp, Warning, TEXT("OnBigRewardClicked 已被调用过，忽略重复调用"));
        return;
    }
    bAlreadyCalled = true;
    
    if (!ActivitySub) 
    {
        // UE_LOG(LogTemp, Error, TEXT("ActivitySub 为空，返回"));
        bAlreadyCalled = false; // 重置标志以便下次调用
        return;
    }

    // 1. 获取玩家记录
    FPlayerLoginRecord& Record = ActivitySub->GetOrInitPlayerRecord(101);
    
    // 2. 查找第一个特殊奖励天数（基于bIsSpecialReward字段）
    int32 SpecialDayIndex = -1;
    const FActivityInfoRow* ActivityInfo = ActivitySub->GetActivityInfo(101);
    if (ActivityInfo)
    {
        for (int32 Day = 1; Day <= ActivityInfo->TotalDays; ++Day)
        {
            TArray<FDailyLoginConfigRow*> Rewards = ActivitySub->GetRewardsByDay(101, Day);
            for (auto* Reward : Rewards)
            {
                if (Reward && Reward->bIsSpecialReward)
                {
                    SpecialDayIndex = Day;
                    break;
                }
            }
            if (SpecialDayIndex != -1) break;
        }
    }
    
    if (SpecialDayIndex == -1)
    {
        UE_LOG(LogTemp, Error, TEXT("未找到任何特殊奖励天数！"));
        return;
    }
    
    // UE_LOG(LogTemp, Warning, TEXT("找到特殊奖励天数: %d"), SpecialDayIndex);
    // UE_LOG(LogTemp, Warning, TEXT("当前领取计数: %d, 进度: %d"), Record.CurrentClaimCount, Record.Progress);

    // 3. 领取资格检查：进度达到且未领取即可
    if (Record.Progress < SpecialDayIndex || Record.IsDayClaimed(SpecialDayIndex)) 
    {
        // UE_LOG(LogTemp, Warning, TEXT("领取资格检查失败：Progress(%d) < SpecialDayIndex(%d) 或 已领取"), 
        //     Record.Progress, SpecialDayIndex);
        return;
    }

    // 4. 获取指针数组 (TArray<FDailyLoginConfigRow*>)
    TArray<FDailyLoginConfigRow*> OptionPtrs = ActivitySub->GetRewardsByDay(101, SpecialDayIndex);
    // UE_LOG(LogTemp, Warning, TEXT("获取到 %d 个奖励配置指针"), OptionPtrs.Num());

    // 5. 【核心修改】转换为值数组 (TArray<FDailyLoginConfigRow>)
    // 因为你的 InitSelection 接收的是引用/值，不支持直接传指针数组
    TArray<FDailyLoginConfigRow> ValueOptions;
    for (FDailyLoginConfigRow* Ptr : OptionPtrs)
    {
        if (Ptr)
        {
            ValueOptions.Add(*Ptr); // 解引用存入
            UE_LOG(LogTemp, Log, TEXT("添加奖励配置: ID=%d, Count=%d"), Ptr->RewardItemID, Ptr->RewardCount);
        }
    }
    
    // UE_LOG(LogTemp, Warning, TEXT("转换后值数组大小: %d"), ValueOptions.Num());

    // 6. 检查RewardOptionClass是否已设置
    if (!RewardOptionClass)
    {
        UE_LOG(LogTemp, Error, TEXT("错误：RewardOptionClass 未设置！请在蓝图中指定 WBP_RewardOptionWidget 类"));
        return;
    }
    
    // UE_LOG(LogTemp, Warning, TEXT("RewardOptionClass 类型: %s"), *RewardOptionClass->GetName());

    // 7. 创建并初始化弹窗
    UWorld* World = GetWorld();
    if (!World)
    {
        UE_LOG(LogTemp, Error, TEXT("无法获取世界上下文"));
        return;
    }
    
    // UE_LOG(LogTemp, Warning, TEXT("准备创建 RewardOptionWidget"));
    
    if (URewardOptionWidget* OptionPopup = CreateWidget<URewardOptionWidget>(World, RewardOptionClass))
    {
        // UE_LOG(LogTemp, Warning, TEXT("成功创建 RewardOptionWidget 实例"));
        
        // 传入转换后的值数组
        OptionPopup->InitSelection(ValueOptions); 
        // UE_LOG(LogTemp, Warning, TEXT("已完成 InitSelection 调用"));
        
        OptionPopup->AddToViewport(11);
        // UE_LOG(LogTemp, Warning, TEXT("已添加到视口"));
        
        // 清理可能存在的旧绑定，防止重复绑定
        OptionPopup->OnStoreToBag.Clear();
        OptionPopup->OnStoreToBag.AddDynamic(this, &UDailyLoginPage::HandleRewardOptionStore);
        // UE_LOG(LogTemp, Warning, TEXT("已绑定 OnStoreToBag 回调，天数: %d"), SpecialDayIndex);
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("创建 RewardOptionWidget 失败！"));
        bAlreadyCalled = false; // 重置标志以便下次调用
    }
    
    // 重置防重复调用标志
    bAlreadyCalled = false;
}

void UDailyLoginPage::HandleStoreLogic(int32 DayIndex)
{
    // UE_LOG(LogTemp, Warning, TEXT("=== HandleStoreLogic 被调用 === 天数: %d"), DayIndex);
    
    if (ActivitySub)
    {
        // 直接使用传入的天数参数领取奖励
        ActivitySub->TryClaimReward(101, DayIndex);
        
        // 刷新界面显示，更新按钮状态
        RefreshRewardList();
        
        // UE_LOG(LogTemp, Warning, TEXT("已领取第 %d 天奖励并刷新界面"), DayIndex);
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("ActivitySub 为空，无法处理奖励领取"));
    }
}

void UDailyLoginPage::HandleRewardOptionStore(int32 DayIndex)
{
    // UE_LOG(LogTemp, Warning, TEXT("=== HandleRewardOptionStore 被调用 === 天数: %d"), DayIndex);
    
    // 添加防重复调用保护
    static bool bAlreadyCalled = false;
    if (bAlreadyCalled)
    {
        // UE_LOG(LogTemp, Warning, TEXT("HandleRewardOptionStore 已被调用过，忽略重复调用"));
        return;
    }
    bAlreadyCalled = true;
    
    if (ActivitySub)
    {
        // 直接使用传入的天数参数领取奖励
        ActivitySub->TryClaimReward(101, DayIndex);
        
        // 刷新界面显示，更新按钮状态
        RefreshRewardList();
        
        // UE_LOG(LogTemp, Warning, TEXT("已领取第 %d 天奖励并刷新界面"), DayIndex);
    }
    else
    {
        UE_LOG(LogTemp, Error, TEXT("ActivitySub 为空，无法处理奖励领取"));
    }
    
    // 重置防重复调用标志
    bAlreadyCalled = false;
}

// 移除HandleStoreLogicWithDay函数，使用简单的HandleStoreLogic替代

void UDailyLoginPage::OpenTreasureBox()
{
    if (TreasureBoxClass)
    {
        UTreasureBoxWidget* BoxPopup = CreateWidget<UTreasureBoxWidget>(this, TreasureBoxClass);
        if (BoxPopup)
        {
            BoxPopup->OnClaimFinished.AddDynamic(this, &UDailyLoginPage::OnFinalClaimComplete);
            BoxPopup->AddToViewport(12);
        }
    }
}

void UDailyLoginPage::OnFinalClaimComplete()
{
    if (ActivitySub)
    {
        FPlayerLoginRecord& Record = ActivitySub->GetOrInitPlayerRecord(101);
        ActivitySub->TryClaimReward(101, Record.CurrentClaimCount);
    }
}

void UDailyLoginPage::Cheat_SetDayAndRefresh(int32 NewDay)
{
    if (ActivitySub)
    {
        // 调用 Subsystem 修改 Record.Progress
        ActivitySub->Cheat_JumpToDay(101, NewDay);
        
        // 修改完后立即执行页面刷新
        RefreshRewardList();
        
        // // UE_LOG(LogTemp, Warning, TEXT("Cheat: 设置进度为 %d 并重刷列表"), NewDay);
    }
}

void UDailyLoginPage::ScrollToCurrentDay()
{
    if (!DayListScroll) return;

    TArray<UWidget*> AllItems = DayListScroll->GetAllChildren();
    if (AllItems.IsValidIndex(3))
    {
        UWidget* TargetWidget = AllItems[3];
        TargetWidget->ForceLayoutPrepass();
        DayListScroll->ScrollWidgetIntoView(TargetWidget, true, EDescendantScrollDestination::TopOrLeft, 0.0f);
    }
}

// 辅助函数：计算当前天数的状态
ERewardState UDailyLoginPage::CalculateState(int32 DayIndex)
{
    if (!ActivitySub) return ERewardState::Incomplete;

    // 获取你定义的结构体记录
    FPlayerLoginRecord& Record = ActivitySub->GetOrInitPlayerRecord(101);
    
    // 1. 数组判定：领奖数组里有这一天，就是已领取
    if (Record.ClaimedDays.Contains(DayIndex))
    {
        return ERewardState::Claimed;
    }

    // 2. 进度判定：只要 DayIndex <= 当前签到进度，就是可领（补领核心）
    // 如果 Cheat 输入 2026，Progress 就是 2026。那么 i=1, 2 都会进入此分支
    if (DayIndex <= Record.Progress)
    {
        return ERewardState::Claimable;
    }
    
    return ERewardState::Incomplete;
}

void UDailyLoginPage::OnClaimAllButtonClicked()
{
    if (!ActivitySub) return;

    // 获取活动信息和玩家记录
    const FActivityInfoRow* Info = ActivitySub->GetActivityInfo(101);
    if (!Info) return;

    FPlayerLoginRecord& Record = ActivitySub->GetOrInitPlayerRecord(101);

    // 收集所有可领取的天数
    TArray<int32> DaysToClaim;
    for (int32 i = 1; i <= Info->TotalDays; ++i)
    {
        // 检查当天是否可领取（进度足够且尚未领取）
        if (i <= Record.Progress && !Record.IsDayClaimed(i))
        {
            DaysToClaim.Add(i);
        }
    }

    // 使用批量领取方法
    if (DaysToClaim.Num() > 0)
    {
        ActivitySub->TryClaimMultipleRewards(101, DaysToClaim);
        // UE_LOG(LogTemp, Warning, TEXT("全部领取完成！共领取 %d 天奖励"), DaysToClaim.Num());
        
        // 显示成功弹窗
        ShowClaimSuccessPopup();
    }
    else
    {
        // UE_LOG(LogTemp, Warning, TEXT("没有可领取的奖励"));
    }
}

void UDailyLoginPage::ShowRewardOptionPopup(int32 DayIndex)
{
    // // UE_LOG(LogTemp, Warning, TEXT("=== ShowRewardOptionPopup 被调用，天数: %d ==="), DayIndex);
    
    if (!ActivitySub) 
    {
        // UE_LOG(LogTemp, Error, TEXT("ActivitySub 为空，返回"));
        return;
    }

    // 1. 获取玩家记录
    FPlayerLoginRecord& Record = ActivitySub->GetOrInitPlayerRecord(101);
    
    // // UE_LOG(LogTemp, Warning, TEXT("当前领取计数: %d, 进度: %d"), Record.CurrentClaimCount, Record.Progress);

    // 2. 领取资格检查：进度达到且未领取即可
    if (Record.Progress < DayIndex || Record.IsDayClaimed(DayIndex)) 
    {
        // // UE_LOG(LogTemp, Warning, TEXT("领取资格检查失败：Progress(%d) < DayIndex(%d) 或 已领取"), 
        //        Record.Progress, DayIndex);
        return;
    }

    // 3. 获取指针数组 (TArray<FDailyLoginConfigRow*>)
    TArray<FDailyLoginConfigRow*> OptionPtrs = ActivitySub->GetRewardsByDay(101, DayIndex);
    // // UE_LOG(LogTemp, Warning, TEXT("获取到 %d 个奖励配置指针"), OptionPtrs.Num());

    // 4. 转换为值数组 (TArray<FDailyLoginConfigRow>)
    TArray<FDailyLoginConfigRow> ValueOptions;
    for (FDailyLoginConfigRow* Ptr : OptionPtrs)
    {
        if (Ptr)
        {
            ValueOptions.Add(*Ptr); // 解引用存入
            // UE_LOG(LogTemp, Log, TEXT("添加奖励配置: ID=%d, Count=%d, Type=%d"), 
            //        Ptr->RewardItemID, Ptr->RewardCount, (int32)Ptr->RewardType);
        }
    }
    
    // // UE_LOG(LogTemp, Warning, TEXT("转换后值数组大小: %d"), ValueOptions.Num());

    // 5. 检查RewardOptionClass是否已设置
    if (!RewardOptionClass)
    {
        // UE_LOG(LogTemp, Error, TEXT("错误：RewardOptionClass 未设置！请在蓝图中指定 WBP_RewardOptionWidget 类"));
        return;
    }
    
    // // UE_LOG(LogTemp, Warning, TEXT("RewardOptionClass 类型: %s"), *RewardOptionClass->GetName());

    // 6. 创建并初始化弹窗
    UWorld* World = GetWorld();
    if (!World)
    {
        // UE_LOG(LogTemp, Error, TEXT("无法获取世界上下文"));
        return;
    }
    
    // // UE_LOG(LogTemp, Warning, TEXT("准备创建 RewardOptionWidget"));
    
    if (URewardOptionWidget* OptionPopup = CreateWidget<URewardOptionWidget>(World, RewardOptionClass))
    {
        // // UE_LOG(LogTemp, Warning, TEXT("成功创建 RewardOptionWidget 实例"));
        
        // 传入转换后的值数组
        OptionPopup->InitSelection(ValueOptions); 
        // // UE_LOG(LogTemp, Warning, TEXT("已完成 InitSelection 调用"));
        
        OptionPopup->AddToViewport(11);
        // // UE_LOG(LogTemp, Warning, TEXT("已添加到视口"));
        
        // 清理可能存在的旧绑定，防止重复绑定
        OptionPopup->OnStoreToBag.Clear();
        OptionPopup->OnStoreToBag.AddDynamic(this, &UDailyLoginPage::HandleRewardOptionStore);
        // // UE_LOG(LogTemp, Warning, TEXT("已绑定 OnStoreToBag 回调"));
    }
    else
    {
        // UE_LOG(LogTemp, Error, TEXT("创建 RewardOptionWidget 失败！"));
    }
}

void UDailyLoginPage::ShowClaimSuccessPopup()
{
    // // UE_LOG(LogTemp, Warning, TEXT("=== ShowClaimSuccessPopup 被调用 ==="));
    
    // 检查ClaimSuccessPopClass是否已设置
    if (!ClaimSuccessPopClass)
    {
        // UE_LOG(LogTemp, Error, TEXT("错误：ClaimSuccessPopClass 未设置！请在蓝图中指定 WBP_ClaimSuccessPop 类"));
        return;
    }
    
    // // UE_LOG(LogTemp, Warning, TEXT("ClaimSuccessPopClass 类型: %s"), *ClaimSuccessPopClass->GetName());

    // 创建并显示成功弹窗
    UWorld* World = GetWorld();
    if (!World)
    {
        // UE_LOG(LogTemp, Error, TEXT("无法获取世界上下文"));
        return;
    }
    
    if (UClaimSuccessWidget* SuccessPopup = CreateWidget<UClaimSuccessWidget>(World, ClaimSuccessPopClass))
    {
        // // UE_LOG(LogTemp, Warning, TEXT("成功创建 ClaimSuccessPopup 实例"));
        SuccessPopup->AddToViewport(100);
        // // UE_LOG(LogTemp, Warning, TEXT("已添加成功弹窗到视口"));
    }
    else
    {
        // UE_LOG(LogTemp, Error, TEXT("创建 ClaimSuccessPopup 失败！"));
    }
}

void UDailyLoginPage::HandleRewardClick(int32 DayIndex, ELoginRewardType RewardType)
{
    // // UE_LOG(LogTemp, Warning, TEXT("=== HandleRewardClick 被调用 === 天数: %d, 类型: %d"), DayIndex, (int32)RewardType);
    
    // 添加防重复调用保护
    static bool bAlreadyCalled = false;
    if (bAlreadyCalled)
    {
        // // UE_LOG(LogTemp, Warning, TEXT("HandleRewardClick 已被调用过，忽略重复调用"));
        return;
    }
    bAlreadyCalled = true;
    
    // 检查是否为特殊奖励天数
    bool bIsSpecialReward = false;
    if (ActivitySub)
    {
        TArray<FDailyLoginConfigRow*> Rewards = ActivitySub->GetRewardsByDay(101, DayIndex);
        for (auto* Reward : Rewards)
        {
            if (Reward && Reward->bIsSpecialReward)
            {
                bIsSpecialReward = true;
                break;
            }
        }
        
        if (bIsSpecialReward)
        {
            // // UE_LOG(LogTemp, Warning, TEXT("检测到特殊奖励天数 %d 点击！"), DayIndex);
        }
        
        // 添加调试信息
        FPlayerLoginRecord& Record = ActivitySub->GetOrInitPlayerRecord(101);
        // // UE_LOG(LogTemp, Warning, TEXT("当前进度: %d, 已领取天数: %s"), Record.Progress, 
        //        *FString::JoinBy(Record.ClaimedDays, TEXT(","), [](int32 Day){ return FString::FromInt(Day); }));
    }
    
    switch (RewardType)
    {
    case ELoginRewardType::Box:
        // 礼包/宝箱类型 - 显示选择弹窗
        // UE_LOG(LogTemp, Warning, TEXT("处理礼包/宝箱奖励，显示选择弹窗"));
        ShowRewardOptionPopup(DayIndex);
        break;
        
    default:
        // 其他类型 - 显示成功弹窗
        // UE_LOG(LogTemp, Warning, TEXT("处理普通奖励，显示成功弹窗"));
        ShowClaimSuccessPopup();
        break;
    }
    
    // 重置防重复调用标志
    bAlreadyCalled = false;
}