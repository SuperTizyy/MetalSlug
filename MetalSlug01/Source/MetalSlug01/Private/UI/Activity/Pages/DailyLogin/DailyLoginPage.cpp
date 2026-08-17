// 版权声明：在项目设置的描述页面填写您的版权信息。

// ==========================================
// 头文件包含区
// ==========================================
#include "UI/Activity/Pages/DailyLogin/DailyLoginPage.h"
#include "Components/ScrollBox.h"
#include "Components/ScrollBoxSlot.h"
#include "Components/Button.h"
#include "Components/EditableText.h"
#include "Components/ContentWidget.h"
#include "Components/PanelWidget.h"
#include "Components/CanvasPanel.h"

// 核心: 引入新的 Subsystem 和 存档类
#include "Systems/Activity/ActivitySubsystem.h"
#include "Data/ActivitySaveGame.h"

#include "UI/Activity/Pages/DailyLogin/DailyLoginDayItemWidget.h"
#include "UI/Activity/Pages/ClaimBox/RewardOptionWidget.h"
#include "UI/Activity/Pages/DemoWidget/DailyLoginCheatWidget.h"
#include "UI/Activity/Pages/SelectMultiplePopup/ActivityConfirmPopupWidget.h"  // 添加这个头文件
#include "UI/Activity/Pages/SelectMultiplePopup/RewardOptionCardWidget.h"
#include "Engine/World.h"
#include "EngineUtils.h"
#include "UI/Activity/Pages/ClaimSuccess/ClaimSuccessWidget.h"
// 移除重复的导航菜单包含，保持原有功能


// ==========================================
// 1. 生命周期
// ==========================================

/**
 * UDailyLoginPage::NativeConstruct
 *
 * 1. 缓存 ActivitySub 弱引用
 * 2. 绑定 ActivitySub->OnActivityDataChanged -> RefreshRewardList
 * 3. 绑定 ClaimAllButton + ResetProgressButton
 * 4. Log 输出所有 TSubclassOf 设置状态
 * 5. 首次 RefreshRewardList
 * 6. SetTimerForNextTick 延迟 ScrollToCurrentDay（保证布局完成）
 */
void UDailyLoginPage::NativeConstruct()
{
	Super::NativeConstruct();

	// 初始化 Subsystem 指针
	if (UGameInstance* GI = GetGameInstance())
	{
		ActivitySub = GI->GetSubsystem<UActivitySubsystem>();
	}

	// 注册到 Subsystem: 让 CheatWidget 等外部模块可通过 GetLoginPage() 拿到本页面引用
	// 注意: 必须在拿到 ActivitySub 之后立即注册, 否则注册失败
	if (ActivitySub)
	{
		ActivitySub->RegisterLoginPage(this);
	}

	// 绑定数据更新回调（如果 Subsystem 有广播）
	if (ActivitySub)
	{
		ActivitySub->OnActivityDataChanged.AddDynamic(this, &UDailyLoginPage::RefreshRewardList);
	}

	// 绑定全部领取按钮点击事件
	if (ClaimAllButton)
	{
		ClaimAllButton->OnClicked.AddDynamic(this, &UDailyLoginPage::OnClaimAllButtonClicked);
		// UE_LOG(LogTemp, Warning, TEXT("成功绑定全部领取按钮事件"));
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("ClaimAllButton为空！请检查蓝图中是否正确设置了按钮绑定"));
	}

	// 绑定重置领取进度按钮点击事件
	if (ResetProgressButton)
	{
		ResetProgressButton->OnClicked.AddDynamic(this, &UDailyLoginPage::OnResetProgressButtonClicked);
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("ResetProgressButton为空！请检查蓝图中是否正确设置了按钮绑定"));
	}

	// 检查必要的类是否已设置
	UE_LOG(LogTemp, Warning, TEXT("检查必要类设置:"));
	UE_LOG(LogTemp, Warning, TEXT("  ItemClass: %s"), ItemClass ? *ItemClass->GetName() : TEXT("未设置"));
	UE_LOG(LogTemp, Warning, TEXT("  RewardOptionClass: %s"), RewardOptionClass ? *RewardOptionClass->GetName() : TEXT("未设置"));

	UE_LOG(LogTemp, Warning, TEXT("  ActivityConfirmPopupClass: %s"), ActivityConfirmPopupClass ? *ActivityConfirmPopupClass->GetName() : TEXT("未设置"));
	UE_LOG(LogTemp, Warning, TEXT("  ClaimSuccessPopClass: %s"), ClaimSuccessPopClass ? *ClaimSuccessPopClass->GetName() : TEXT("未设置"));

	// CheatWidget 相关代码保持不变
	// CheatWidget 会在自己的 NativeConstruct 中自动绑定事件
	// 这里不需要额外处理

	// 初次刷新
	RefreshRewardList();

	// 延迟滚动以确保布局完成
	GetWorld()->GetTimerManager().SetTimerForNextTick([this]() {
		ScrollToCurrentDay();
	});
}


/**
 * UDailyLoginPage::NativeDestruct
 *
 * 清理逻辑:
 * 1. 从 Subsystem 反注册（必须在 Super::NativeDestruct 之前, 因为反注册后外部不再持有引用, 加速 GC）
 * 2. 解绑事件委托（避免悬挂指针）
 * 3. 清理 Subsystem 指针
 */
void UDailyLoginPage::NativeDestruct()
{
	// 第一步: 反注册, 顺序很关键, 先于 Super::NativeDestruct 执行
	if (ActivitySub)
	{
		ActivitySub->UnregisterLoginPage(this);
		ActivitySub->OnActivityDataChanged.RemoveDynamic(this, &UDailyLoginPage::RefreshRewardList);
		ActivitySub = nullptr;
	}

	// DailyLoginPage 清理逻辑
	Super::NativeDestruct();
}


// ==========================================
// 2. 公共接口
// ==========================================

/**
 * UDailyLoginPage::RefreshRewardList
 *
 * 1. 防御: ActivitySub/DayListScroll/ItemClass 必须有效
 * 2. 清空 BigRewardItem 的奖励图标（防止重复）
 * 3. 清空 DayListScroll
 * 4. 获取 Record (玩家登录记录) + Info (活动信息)
 * 5. 遍历 TotalDays
 *    a. 计算 ERewardState
 *       - ClaimedDays.Contains(i) || IsDayClaimed(i) -> Claimed
 *       - i <= Progress -> Claimable
 *       - i == Progress + 1 -> Incomplete（明日可领）
 *       - 否则 -> Incomplete（未到期）
 *    b. 防销毁检查 World->bIsTearingDown
 *    c. CreateWidget<UDailyLoginDayItemWidget>
 *    d. Clear 旧绑定, AddDynamic(this, &HandleRewardClick)
 *    e. 判定 bIsSpecial: 检查当天是否有 bIsSpecialReward = true 的奖励
 *    f. NewItem->Init(i, Progress, State, bIsSpecial, ActivitySub)
 *    g. 特殊 -> BigRewardItemWidget, 普通 -> DayListScroll（间距由蓝图 Slot 定义）
 *
 * [修复历史 v2026.08.12]
 * 删除原本 SetPadding(FMargin(80.0f)) 的代码 — FMargin(float) 单参数构造导致
 * 每个滚动条目 +80px 内边距（上下左右均 80），DayListScroll ContentSize 异常膨胀。
 * 删除原本 3 处 SetRenderScale(1.5) + SetZOrder(1000) 的代码 — 让按钮占用 1.5 倍空间，
 * 也贡献于尺寸异常。删除原本 3 处 SetFont Size 的代码 — 强行放大文本撑大条目 widget。
 * 所有尺寸/间距/字号控制权交还蓝图 UMG 编辑器统一管理。
 * 6. 重新初始化 BigRewardItem（第 8 天）
 *    - 独立计算 State（基于 Progress 和 IsDayClaimed(8)）
 *    - Init(8, Progress, State, bIsSpecial)
 *    - Clear 旧绑定, AddDynamic HandleRewardClick
 *    - AddRewardIconFromConfig 加载图标
 *    - SetVisibility(Visible) + SetRenderOpacity(1.0)
 * 7. 降级方案: BigRewardItem 类型不对 / 找不到时, 添加到 DayListScroll
 */
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

	UE_LOG(LogTemp, Warning, TEXT("RefreshRewardList: TotalDays=%d, CurrentProgress=%d"), Info->TotalDays, Record.Progress);

	// 存储大奖项控件
	UDailyLoginDayItemWidget* BigRewardItemWidget = nullptr;
	bool bHasBigReward = false;

	for (int32 i = 1; i <= Info->TotalDays; ++i)
	{
		// 使用一致的 Record.Progress 值计算状态，避免重复调用 GetOrInitPlayerRecord
		ERewardState State;
		if (Record.IsDayClaimed(i) || Record.ClaimedDays.Contains(i))
		{
			State = ERewardState::Claimed;
		}
		else if (i <= Record.Progress)
		{
			State = ERewardState::Claimable;
		}
		else if (i == Record.Progress + 1)
		{
			State = ERewardState::Incomplete; // 明日可领
		}
		else
		{
			State = ERewardState::Incomplete; // 未到期
		}
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
				TArray<const FDailyLoginConfigRow*> Rewards = ActivitySub->GetRewardsByDay(101, i);
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

				// 关键: 这里传入当前的 i、Record.Progress、State、特殊奖励标志和 ActivitySubsystem
				NewItem->Init(i, Record.Progress, State, bIsSpecial, ActivitySub);

				// 根据 IsSpecialReward 字段决定显示位置
				if (bIsSpecial)
				{
					// 特殊奖励: 保存为大奖项，不添加到滚动列表
					// 【v206.3 修复】必须 continue 跳过 AddChild，否则特殊奖励会被重复添加到滚动列表
					BigRewardItemWidget = NewItem;
					bHasBigReward = true;
					UE_LOG(LogTemp, Log, TEXT("RefreshRewardList: 第 %d 天是特殊奖励，跳过滚动列表"), i);
					continue; // ← 关键！跳过下面的 AddChild
				}

				// 【v206.3 修复】删除原本 SetPadding(FMargin(80.0f)) 的代码 — FMargin(float) 单参数构造表示
				// 「左/上/右/下 全部 80px」，会在滚动方向上每个条目 +80px 内边距，导致 DayListScroll
				// 测得的 ContentSize 异常膨胀。原代码意图只是增大条目间距，应该交给蓝图 Widget 的
				// Slot Padding 属性或 ScrollBox 的 Orientation 方向布局策略来处理，而不是 C++ 强行覆盖。
				DayListScroll->AddChild(NewItem);

				// 奖励图标加载已移到 Init 函数中自动处理
			}
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("RefreshRewardList: 完成创建 %d 个子项（普通奖励）"), DayListScroll->GetChildrenCount());

	// 大奖项处理: 基于 IsSpecialReward 字段的特殊奖励显示控制
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
		// UE_LOG(LogTemp, Warning, TEXT("警告: 未找到名为 BigRewardItem 的控件"));
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
			// 使用一致的 Progress 值计算状态，避免重复调用 GetOrInitPlayerRecord
			ERewardState State;
			if (BigRewardRecord.IsDayClaimed(8) || BigRewardRecord.ClaimedDays.Contains(8))
			{
				State = ERewardState::Claimed;
			}
			else if (8 <= BigRewardRecord.Progress)
			{
				State = ERewardState::Claimable;
			}
			else if (8 == BigRewardRecord.Progress + 1)
			{
				State = ERewardState::Incomplete; // 明日可领
			}
			else
			{
				State = ERewardState::Incomplete; // 未到期
			}

			// 获取大奖项天数的奖励配置（基于 IsSpecialReward 字段）
			TArray<const FDailyLoginConfigRow*> Rewards = ActivitySub->GetRewardsByDay(101, 8);
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
			// UE_LOG(LogTemp, Warning, TEXT("第8天点击事件绑定状态: %s"),
			//     BigRewardItemAsDayWidget->OnRewardClicked.IsBound() ? TEXT("已绑定") : TEXT("未绑定"));
		}
		else
		{
			// UE_LOG(LogTemp, Warning, TEXT("警告: BigRewardItem 控件不是 DailyLoginDayItemWidget 类型"));
			// 降级方案: 如果 BigRewardItem 不是 DayItemWidget 类型，仍将原始的 BigRewardItemWidget 添加到滚动框
			if (DayListScroll && BigRewardItemWidget)
			{
				// [修复] 删除原本 SetPadding(FMargin(15.0f)) 的代码 — 同上, 间距交给蓝图处理。
				DayListScroll->AddChild(BigRewardItemWidget);
				BigRewardItemWidget->SetVisibility(ESlateVisibility::Visible);
				BigRewardItemWidget->SetRenderOpacity(1.0f);

				// 绑定点击事件（降级方案也要绑定）
				if (UDailyLoginDayItemWidget* DayItemWidget = Cast<UDailyLoginDayItemWidget>(BigRewardItemWidget))
				{
					// 先清除旧的绑定，防止重复绑定
					DayItemWidget->OnRewardClicked.Clear();
					DayItemWidget->OnRewardClicked.AddDynamic(this, &UDailyLoginPage::HandleRewardClick);
					// UE_LOG(LogTemp, Warning, TEXT("降级方案: 已为 BigRewardItem 绑定点击事件"));
				}

				// UE_LOG(LogTemp, Warning, TEXT("降级方案: 已将 BigRewardItem 添加到滚动列表并设置可见"));
			}
		}
	}
	else if (bHasBigReward)
	{
		if (!BigRewardItemWidgetRef_Local)
		{
			// UE_LOG(LogTemp, Warning, TEXT("警告: 第8天是特殊奖励，但蓝图中找不到名为 BigRewardItem 的控件"));
		}

		// 降级方案: 添加到主滚动框
		if (DayListScroll && BigRewardItemWidget)
		{
			// [修复] 删除原本 SetPadding(FMargin(15.0f)) 的代码 — 同上, 间距交给蓝图处理。
			DayListScroll->AddChild(BigRewardItemWidget);
			BigRewardItemWidget->SetVisibility(ESlateVisibility::Visible);
			BigRewardItemWidget->SetRenderOpacity(1.0f);

			// 绑定点击事件（最终降级方案也要绑定）
			if (UDailyLoginDayItemWidget* DayItemWidget = Cast<UDailyLoginDayItemWidget>(BigRewardItemWidget))
			{
				DayItemWidget->OnRewardClicked.AddDynamic(this, &UDailyLoginPage::HandleRewardClick);
				// // UE_LOG(LogTemp, Warning, TEXT("最终降级方案: 已为 BigRewardItem 绑定点击事件"));
			}

			// // UE_LOG(LogTemp, Warning, TEXT("最终降级方案: 已将 BigRewardItem 添加到滚动列表并设置可见"));
		}
	}
}


// ==========================================
// 3. 内部回调 - 大奖励
// ==========================================

/**
 * UDailyLoginPage::OnBigRewardClicked
 *
 * 大奖点击入口（备用, 当前主流程是 OnRewardClicked）
 * 1. 防重入: static bool bAlreadyCalled
 * 2. 查找第一个 bIsSpecialReward = true 的天数
 * 3. 校验 Progress >= SpecialDayIndex 且未领取
 * 4. 转换指针数组 -> 值数组（UHT 限制）
 * 5. CreateWidget<URewardOptionWidget> -> InitSelection -> AddToViewport
 * 6. 绑定 OnStoreToBag -> HandleRewardOptionStore
 */
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

	// 2. 查找第一个特殊奖励天数（基于 bIsSpecialReward 字段）
	int32 SpecialDayIndex = -1;
	const FActivityInfoRow* ActivityInfo = ActivitySub->GetActivityInfo(101);
	if (ActivityInfo)
	{
		for (int32 Day = 1; Day <= ActivityInfo->TotalDays; ++Day)
		{
			TArray<const FDailyLoginConfigRow*> Rewards = ActivitySub->GetRewardsByDay(101, Day);
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

	// 3. 领取资格检查: 进度达到且未领取即可
	if (Record.Progress < SpecialDayIndex || Record.IsDayClaimed(SpecialDayIndex))
	{
		// UE_LOG(LogTemp, Warning, TEXT("领取资格检查失败: Progress(%d) < SpecialDayIndex(%d) 或 已领取"),
		//     Record.Progress, SpecialDayIndex);
		return;
	}

	// 4. 获取指针数组 (TArray<FDailyLoginConfigRow*>)
	TArray<const FDailyLoginConfigRow*> OptionPtrs = ActivitySub->GetRewardsByDay(101, SpecialDayIndex);
	// UE_LOG(LogTemp, Warning, TEXT("获取到 %d 个奖励配置指针"), OptionPtrs.Num());

	// 5. 【核心修改】转换为值数组 (TArray<FDailyLoginConfigRow>)
	// 因为你的 InitSelection 接收的是引用/值，不支持直接传指针数组
	TArray<FDailyLoginConfigRow> ValueOptions;
	for (const FDailyLoginConfigRow* Ptr : OptionPtrs)
	{
		if (Ptr)
		{
			ValueOptions.Add(*Ptr); // 解引用存入
			UE_LOG(LogTemp, Log, TEXT("添加奖励配置: ID=%d, Count=%d"), Ptr->RewardItemID, Ptr->RewardCount);
		}
	}

	// UE_LOG(LogTemp, Warning, TEXT("转换后值数组大小: %d"), ValueOptions.Num());

	// 6. 检查 RewardOptionClass 是否已设置
	if (!RewardOptionClass)
	{
		UE_LOG(LogTemp, Error, TEXT("错误: RewardOptionClass 未设置！请在蓝图中指定 WBP_RewardOptionWidget 类"));
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


// ==========================================
// 4. 内部回调 - 存储
// ==========================================

/**
 * UDailyLoginPage::HandleStoreLogic
 *
 * "放入背包"逻辑（来自普通条目）
 * 1. ActivitySub->TryClaimReward(101, DayIndex) 真正写入存档
 * 2. RefreshRewardList 刷新 UI
 */
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


/**
 * UDailyLoginPage::HandleRewardOptionStore
 *
 * "放入背包"逻辑（来自 RewardOptionWidget）
 * 1. 防重入: static bool bAlreadyCalled
 * 2. ActivitySub->TryClaimReward(101, DayIndex)
 * 3. RefreshRewardList
 */
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


// ==========================================
// 5. 内部回调 - 宝箱
// ==========================================

/**
 * UDailyLoginPage::OpenTreasureBox
 *
 * 打开宝箱（创建多选项弹窗）
 * 1. 优先用参数 DayIndex, 其次用 CurrentProcessingDay, 最后默认 3
 * 2. 获取奖励配置 + 转值数组
 * 3. CreateWidget<UActivityConfirmPopupWidget>
 * 4. InitializePopup(ValueOptions, 1) + AddToViewport(13)
 */
void UDailyLoginPage::OpenTreasureBox(int32 DayIndex)
{
	UE_LOG(LogTemp, Warning, TEXT("=== OpenTreasureBox 被调用 ==="));

	// 优先使用传入的参数，否则使用存储的天数，最后使用默认值
	int32 CurrentDayIndex = -1;
	if (DayIndex != -1)
	{
		CurrentDayIndex = DayIndex;
	}
	else if (CurrentProcessingDay != -1)
	{
		CurrentDayIndex = CurrentProcessingDay;
		UE_LOG(LogTemp, Warning, TEXT("使用存储的处理天数: %d"), CurrentDayIndex);
	}
	else
	{
		CurrentDayIndex = 3; // 默认使用第3天
		UE_LOG(LogTemp, Warning, TEXT("使用默认天数: %d"), CurrentDayIndex);
	}

	// 获取当前天数的奖励配置数据
	TArray<const FDailyLoginConfigRow*> CurrentDayRewards = ActivitySub->GetRewardsByDay(101, CurrentDayIndex);
	TArray<FDailyLoginConfigRow> ValueOptions;
	for (const FDailyLoginConfigRow* Ptr : CurrentDayRewards)
	{
		if (Ptr)
		{
			ValueOptions.Add(*Ptr);
			UE_LOG(LogTemp, Log, TEXT("添加当前天数奖励配置: ID=%d, Count=%d"), Ptr->RewardItemID, Ptr->RewardCount);
		}
	}

		// 创建并显示 ActivityConfirmPopupWidget
		UWorld* World = GetWorld();
		if (!World)
		{
			UE_LOG(LogTemp, Error, TEXT("无法获取世界上下文"));
			return;
		}

		UE_LOG(LogTemp, Warning, TEXT("检查 ActivityConfirmPopupClass 是否设置: %s"),
			   ActivityConfirmPopupClass ? *ActivityConfirmPopupClass->GetName() : TEXT("未设置"));

		if (ActivityConfirmPopupClass)
		{
			UE_LOG(LogTemp, Warning, TEXT("开始创建 ActivityConfirmPopupWidget..."));
			UActivityConfirmPopupWidget* ConfirmPopup = CreateWidget<UActivityConfirmPopupWidget>(World, ActivityConfirmPopupClass.Get());
			if (ConfirmPopup)
			{
				UE_LOG(LogTemp, Warning, TEXT("✅ 成功创建 ActivityConfirmPopupWidget 实例"));

				// 初始化弹窗数据，传入当前天数
				ConfirmPopup->InitializePopup(ValueOptions, 1, CurrentDayIndex);

				// 【v206.2 修复】绑定领取完成回调
				// 原因: ActivityConfirmPopupWidget 确认后需要通知 DailyLoginPage 处理奖励领取和刷新
				ConfirmPopup->OnRewardConfirmed.AddDynamic(this, &UDailyLoginPage::HandleConfirmPopupRewardClaimed);

				ConfirmPopup->AddToViewport(13); // 层级高于 TreasureBox

				UE_LOG(LogTemp, Warning, TEXT("✅ ActivityConfirmPopupWidget 已添加到视口"));
			}
			else
			{
				UE_LOG(LogTemp, Error, TEXT("❌ 创建 ActivityConfirmPopupWidget 失败！"));
			}
		}
		else
		{
			UE_LOG(LogTemp, Error, TEXT("❌ ActivityConfirmPopupClass 未设置！请在蓝图中指定 WBP_ActivityConfirmPopupWidget 类"));
		}
	}


/**
 * UDailyLoginPage::OnFinalClaimComplete
 *
 * 最终领取完成回调
 * 用途: TryClaimReward 用 CurrentClaimCount 作为天索引
 */
void UDailyLoginPage::OnFinalClaimComplete()
{
	if (ActivitySub)
	{
		FPlayerLoginRecord& Record = ActivitySub->GetOrInitPlayerRecord(101);
		ActivitySub->TryClaimReward(101, Record.CurrentClaimCount);
	}
}


/**
 * UDailyLoginPage::HandleConfirmPopupRewardClaimed
 *
 * 【v206.2 新增】处理宝箱确认弹窗的奖励领取
 * 用途: ActivityConfirmPopupWidget 确认后广播 OnRewardConfirmed，通知 DailyLoginPage 处理奖励领取和刷新
 *
 * 流程:
 * 1. ActivitySub->TryClaimReward(101, DayIndex) - 领取奖励
 * 2. RefreshRewardList() - 刷新 UI
 */
void UDailyLoginPage::HandleConfirmPopupRewardClaimed(int32 DayIndex)
{
	UE_LOG(LogTemp, Log, TEXT("[v206.2] HandleConfirmPopupRewardClaimed: 宝箱确认完成，领取第 %d 天奖励"), DayIndex);

	if (!ActivitySub)
	{
		UE_LOG(LogTemp, Error, TEXT("[v206.2] HandleConfirmPopupRewardClaimed: ActivitySub 为空，无法领取奖励"));
		return;
	}

	// 1. 领取奖励
	ActivitySub->TryClaimReward(101, DayIndex);

	// 2. 刷新 UI
	RefreshRewardList();
}


// ==========================================
// 6. 公共接口 - 调试 / 滚动 / 全部领取
// ==========================================

/**
 * UDailyLoginPage::Cheat_SetDayAndRefresh
 *
 * 调试: 设置登录天数并刷新
 * 1. ActivitySub->Cheat_JumpToDay(101, NewDay)
 * 2. RefreshRewardList
 * 3. 下一帧 ScrollToCurrentDay
 */
void UDailyLoginPage::Cheat_SetDayAndRefresh(int32 NewDay)
{
	if (ActivitySub)
	{
		// 调用 Subsystem 修改 Record.Progress
		ActivitySub->Cheat_JumpToDay(101, NewDay);

		// 修改完后立即执行页面刷新
		RefreshRewardList();

		// 延迟滚动以确保布局完成
		GetWorld()->GetTimerManager().SetTimerForNextTick([this]() {
			ScrollToCurrentDay();
		});

		// // UE_LOG(LogTemp, Warning, TEXT("Cheat: 设置进度为 %d 并重刷列表"), NewDay);
	}
}


/**
 * UDailyLoginPage::ScrollToCurrentDay
 *
 * 滚动到当前可领取天
 * 1. Clamp Progress 到 1~8
 * 2. 大于等于 8 时 TargetIndex = 6（第 7 天, 滚动列表最大索引）
 * 3. ScrollWidgetIntoView + EDescendantScrollDestination::TopOrLeft
 * 4. ForceLayoutPrepass 强制刷新布局
 */
void UDailyLoginPage::ScrollToCurrentDay()
{
	if (!DayListScroll || !ActivitySub) return;

	// 获取当前进度
	FPlayerLoginRecord& Record = ActivitySub->GetOrInitPlayerRecord(101);
	int32 CurrentProgress = Record.Progress;

	// 确保进度在有效范围内（1-8）
	CurrentProgress = FMath::Clamp(CurrentProgress, 1, 8);

	// 计算目标索引
	// 注意: 滚动列表只包含普通奖励（第1-7天），大奖项（第8天）不在滚动列表中
	int32 TargetIndex;
	if (CurrentProgress >= 8)
	{
		// 如果进度达到或超过 8，滚动到滚动列表的最右边（第7天，索引6）
		TargetIndex = 6; // 第7天的索引
	}
	else
	{
		// 否则滚动到对应的普通天数
		TargetIndex = CurrentProgress - 1;
	}

	TArray<UWidget*> AllItems = DayListScroll->GetAllChildren();

	UE_LOG(LogTemp, Warning, TEXT("ScrollToCurrentDay: 当前Progress=%d, 目标索引=%d, 子项数量=%d"), CurrentProgress, TargetIndex, AllItems.Num());

	if (AllItems.IsValidIndex(TargetIndex))
	{
		UWidget* TargetWidget = AllItems[TargetIndex];
		TargetWidget->ForceLayoutPrepass();
		DayListScroll->ScrollWidgetIntoView(TargetWidget, true, EDescendantScrollDestination::TopOrLeft, 0.0f);
		UE_LOG(LogTemp, Log, TEXT("ScrollToCurrentDay: 滚动到第%d天（索引%d）"), CurrentProgress, TargetIndex);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("ScrollToCurrentDay: 无法滚动到第%d天，子项数量不足（当前有%d个子项）"), CurrentProgress, AllItems.Num());

		// 调试: 输出所有子项信息
		for (int32 i = 0; i < AllItems.Num(); ++i)
		{
			UE_LOG(LogTemp, Warning, TEXT("  子项[%d]: %s"), i, AllItems[i] ? *AllItems[i]->GetName() : TEXT("null"));
		}
	}
}


/**
 * UDailyLoginPage::OnClaimAllButtonClicked
 *
 * 全部领取按钮点击
 * 1. 收集可领取天数列表（i <= Progress && !IsDayClaimed(i)）
 * 2. TryClaimMultipleRewards(101, DaysToClaim) 批量领取
 * 3. ShowClaimSuccessPopup
 */
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


/**
 * UDailyLoginPage::OnResetProgressButtonClicked
 *
 * 重置领取进度按钮点击 — 大厂架构职责:
 * 1. UI 层不做兜底 (没有 else / 没有 try-catch)
 * 2. 唯一动作 = 通知 ActivitySubsystem
 * 3. ActivitySubsystem::ResetPlayerRecord 内部:
 *    - 委托 SaveModifier 重置存档 (Progress=1, 清空 ClaimedDays)
 *    - 广播 OnActivityDataChanged → RefreshRewardList 自动刷新 UI
 * 4. 不手动 RefreshRewardList — 这是大厂架构的反手关键点:
 *    "按钮只通知业务层, UI 刷新是业务层广播触发的, 不在按钮里写"
 *
 * 错误处理:
 * - ActivitySub 为 null 是开发者错误 (NativeConstruct 已绑), 必须暴露
 * - 不用 silent return, 因为会掩盖 Blueprint 配错 / BindWidget 失败等上游问题
 */
void UDailyLoginPage::OnResetProgressButtonClicked()
{
	if (!ActivitySub)
	{
		// 大厂架构: 不静默吞错 — 暴露给开发者立即修
		UE_LOG(LogTemp, Error,
			TEXT("[DailyLoginPage] OnResetProgressButtonClicked: ActivitySubsystem 为 null. "
				 "NativeConstruct 中应已通过 GameInstance->GetSubsystem 初始化. 请检查 Subsystem 注入流程."));
		return;
	}

	// 单一入口: 重置后 UI 自动通过广播刷新, 此处不调 RefreshRewardList
	const bool bSuccess = ActivitySub->ResetPlayerRecord(101, /*bAutoSave=*/true);
	if (!bSuccess)
	{
		// SaveModifier 内部已 Log Error (SaveGame 获取失败 / 存档损坏)
		// UI 层不再二次 Log, 避免重复日志
		UE_LOG(LogTemp, Warning, TEXT("[DailyLoginPage] 重置玩家领取进度返回 false, 详见上方 Modify 日志. ActivityID=101"));
	}
}


// ==========================================
// 7. 公共接口 - 弹窗
// ==========================================

/**
 * UDailyLoginPage::ShowRewardOptionPopup
 *
 * 显示奖励选项弹窗（用于礼包/宝箱类型奖励）
 * 1. 校验领取资格（DayIndex <= Progress 且未领取）
 * 2. 缓存 CurrentProcessingDay
 * 3. 转换指针数组 -> 值数组
 * 4. CreateWidget<URewardOptionWidget> + InitSelection
 * 5. AddToViewport(11)
 * 6. 绑定 OnStoreToBag / OnOpenNow
 */
void UDailyLoginPage::ShowRewardOptionPopup(int32 DayIndex)
{
	UE_LOG(LogTemp, Warning, TEXT("=== ShowRewardOptionPopup 被调用，天数: %d ==="), DayIndex);

	if (!ActivitySub)
	{
		UE_LOG(LogTemp, Error, TEXT("ActivitySub 为空，返回"));
		return;
	}

	// 1. 获取玩家记录
	FPlayerLoginRecord& Record = ActivitySub->GetOrInitPlayerRecord(101);

	UE_LOG(LogTemp, Warning, TEXT("当前领取计数: %d, 进度: %d"), Record.CurrentClaimCount, Record.Progress);

	// 2. 领取资格检查: 在 Progress 范围内的未领取天数
	if (DayIndex > Record.Progress)
	{
		UE_LOG(LogTemp, Warning, TEXT("领取资格检查失败: 第%d天超出可领取范围，当前进度只到第%d天"), DayIndex, Record.Progress);
		return;
	}

	if (Record.IsDayClaimed(DayIndex))
	{
		UE_LOG(LogTemp, Warning, TEXT("领取资格检查失败: 已领取第%d天"), DayIndex);
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("领取资格检查通过: 第%d天可领取"), DayIndex);

	// 存储当前处理的天数
	CurrentProcessingDay = DayIndex;
	UE_LOG(LogTemp, Warning, TEXT("设置当前处理天数: %d"), CurrentProcessingDay);

	// 3. 获取指针数组 (TArray<FDailyLoginConfigRow*>)
	TArray<const FDailyLoginConfigRow*> OptionPtrs = ActivitySub->GetRewardsByDay(101, DayIndex);
	UE_LOG(LogTemp, Warning, TEXT("获取到 %d 个奖励配置指针"), OptionPtrs.Num());

	// 4. 转换为值数组 (TArray<FDailyLoginConfigRow>)
	TArray<FDailyLoginConfigRow> ValueOptions;
	for (const FDailyLoginConfigRow* Ptr : OptionPtrs)
	{
		if (Ptr)
		{
			ValueOptions.Add(*Ptr); // 解引用存入
			UE_LOG(LogTemp, Log, TEXT("添加奖励配置: ID=%d, Count=%d, Type=%d"),
				   Ptr->RewardItemID, Ptr->RewardCount, (int32)Ptr->RewardType);
		}
	}

	UE_LOG(LogTemp, Warning, TEXT("转换后值数组大小: %d"), ValueOptions.Num());

	// 5. 检查 RewardOptionClass 是否已设置
	if (!RewardOptionClass)
	{
		UE_LOG(LogTemp, Error, TEXT("错误: RewardOptionClass 未设置！请在蓝图中指定 WBP_RewardOptionWidget 类"));
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("RewardOptionClass 类型: %s"), *RewardOptionClass->GetName());

	// 6. 创建并初始化弹窗
	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogTemp, Error, TEXT("无法获取世界上下文"));
		return;
	}

	UE_LOG(LogTemp, Warning, TEXT("准备创建 RewardOptionWidget"));

	if (URewardOptionWidget* OptionPopup = CreateWidget<URewardOptionWidget>(World, RewardOptionClass))
	{
		UE_LOG(LogTemp, Warning, TEXT("成功创建 RewardOptionWidget 实例"));

		// 传入转换后的值数组
		OptionPopup->InitSelection(ValueOptions);
		UE_LOG(LogTemp, Warning, TEXT("已完成 InitSelection 调用"));

		OptionPopup->AddToViewport(11);
		UE_LOG(LogTemp, Warning, TEXT("已添加到视口"));

		// 清理可能存在的旧绑定，防止重复绑定
		OptionPopup->OnStoreToBag.Clear();
		OptionPopup->OnStoreToBag.AddDynamic(this, &UDailyLoginPage::HandleRewardOptionStore);
		UE_LOG(LogTemp, Warning, TEXT("已绑定 OnStoreToBag 回调"));

		// 绑定 OnOpenNow 事件
		OptionPopup->OnOpenNow.Clear();
		OptionPopup->OnOpenNow.AddDynamic(this, &UDailyLoginPage::OpenTreasureBox);
		UE_LOG(LogTemp, Warning, TEXT("已绑定 OnOpenNow 回调"));

		UE_LOG(LogTemp, Warning, TEXT("RewardOptionWidget 创建和绑定完成"));
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("创建 RewardOptionWidget 失败！"));
	}
}


/**
 * UDailyLoginPage::ShowClaimSuccessPopup
 *
 * 显示领取成功弹窗
 * 1. 校验 ClaimSuccessPopClass
 * 2. CreateWidget<UClaimSuccessWidget> + AddToViewport(100)
 */
void UDailyLoginPage::ShowClaimSuccessPopup()
{
	// // UE_LOG(LogTemp, Warning, TEXT("=== ShowClaimSuccessPopup 被调用 ==="));

	// 检查 ClaimSuccessPopClass 是否已设置
	if (!ClaimSuccessPopClass)
	{
		// UE_LOG(LogTemp, Error, TEXT("错误: ClaimSuccessPopClass 未设置！请在蓝图中指定 WBP_ClaimSuccessPop 类"));
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


// ==========================================
// 8. 公共接口 - 奖励点击路由
// ==========================================

/**
 * UDailyLoginPage::HandleRewardClick
 *
 * 处理奖励点击事件
 * 1. 防重入: static bool bAlreadyCalled
 * 2. 检测 bIsSpecialReward（用于日志）
 * 3. switch (RewardType):
 *    - Box -> ShowRewardOptionPopup
 *    - 默认 -> ShowClaimSuccessPopup
 */
void UDailyLoginPage::HandleRewardClick(int32 DayIndex, ELoginRewardType RewardType)
{
	UE_LOG(LogTemp, Warning, TEXT("=== HandleRewardClick 被调用 === 天数: %d, 类型: %d"), DayIndex, (int32)RewardType);

	// 添加防重复调用保护
	static bool bAlreadyCalled = false;
	if (bAlreadyCalled)
	{
		UE_LOG(LogTemp, Warning, TEXT("HandleRewardClick 已被调用过，忽略重复调用"));
		return;
	}
	bAlreadyCalled = true;

	// 检查是否为特殊奖励天数
	bool bIsSpecialReward = false;
	if (ActivitySub)
	{
		TArray<const FDailyLoginConfigRow*> Rewards = ActivitySub->GetRewardsByDay(101, DayIndex);
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
		UE_LOG(LogTemp, Warning, TEXT("处理礼包/宝箱奖励，显示选择弹窗"));
		ShowRewardOptionPopup(DayIndex);
		break;

	default:
		// 其他类型 - 显示成功弹窗
		UE_LOG(LogTemp, Warning, TEXT("处理普通奖励，显示成功弹窗"));
		ShowClaimSuccessPopup();
		break;
	}

	// 重置防重复调用标志
	bAlreadyCalled = false;
}
