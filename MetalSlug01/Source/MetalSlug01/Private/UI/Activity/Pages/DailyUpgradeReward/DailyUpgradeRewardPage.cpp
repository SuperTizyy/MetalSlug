/**
 * @file DailyUpgradeRewardPage.cpp
 * @brief 每日升级奖励活动页面实现
 * @author AI Assistant
 * @date 2026
 * @version 1.0
 *
 * @details 实现每日升级奖励活动页面的核心功能
 * 
 * @section Overview 系统概述
 * 该文件实现了每日升级奖励活动页面的完整功能，采用Subsystem模式实现
 * 数据访问层与UI层的分离，确保代码的可维护性和扩展性。
 * 
 * @section Architecture 系统架构
 * - UI层：DailyUpgradeRewardPage负责界面显示和用户交互
 * - 数据层：UpgradeActivitySubsystem提供数据访问接口
 * - 配置层：通过DataTable配置活动规则和奖励信息
 * 
 * @section KeyFeatures 核心功能
 * 1. 奖励物品图标动态加载和显示
 * 2. 经验宝箱状态的实时更新
 * 3. 重选奖励功能的弹窗交互
 * 4. 编辑器预览模式的支持
 * 5. 完善的事件绑定和资源管理
 * 
 * @section DataFlow 数据流向
 * 配置表(DT_DailyUpgradeRewardConfigRow) → Subsystem → UI组件 → 界面显示
 * 
 * @section BestPractices 最佳实践
 * - 使用Subsystem模式解耦数据访问
 * - 实现完善的错误处理和日志记录
 * - 支持编辑器预览模式下的优雅降级
 * - 遵循UE C++编码规范和内存管理原则
 */

#include "UI/Activity/Pages/DailyUpgradeReward/DailyUpgradeRewardPage.h"
#include "Components/HorizontalBox.h"
#include "Components/Image.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"
#include "Components/ProgressBar.h"
#include "Components/VerticalBox.h"
#include "Components/ScrollBox.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/DataTable.h"
#include "UI/Activity/Core/ActivitySubsystem.h"
#include "UI/Activity/Data/DailyLoginConfig.h"
#include "UI/Activity/Pages/DailyUpgradeReward/ExperienceChestClaimWidget.h"
#include "UI/Activity/Core/UpgradeActivitySubsystem.h"
#include "UI/Activity/Pages/SelectMultiplePopup/ActivityConfirmPopupWidget.h"
#include "UI/Activity/Pages/ClaimBox/RewardOptionWidget.h"

/**
 * @brief 初始化每日升级奖励页面
 * @details 在Widget初始化阶段设置默认值并绑定UI事件
 * 主要功能：
 * 1. 调用父类初始化
 * 2. 设置默认的游戏状态值
 * 3. 绑定重选奖励按钮的点击事件
 * @return 初始化是否成功
 * @note 此方法在构造函数之后、NativeConstruct之前调用
 */
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

	// 绑定重选奖励按钮事件
	if (ReselectRewardButton)
	{
		ReselectRewardButton->OnClicked.AddDynamic(this, &UDailyUpgradeRewardPage::OnReselectRewardClicked);
		UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: 重选奖励按钮事件已绑定"));
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("DailyUpgradeRewardPage: ReselectRewardButton未绑定"));
	}

	return true;
}

/**
 * @brief 原生构造函数 - UI完全构建完成后执行
 * @details 在Widget的所有子控件都创建完毕后调用
 * 主要功能：
 * 1. 调用父类原生构造
 * 2. 初始化奖励物品图标缓存
 * 3. 更新宝箱数量显示文本
 * 4. 初始化经验宝箱控件列表
 * @note 此阶段所有UI控件均已创建完成，可以安全访问
 */
void UDailyUpgradeRewardPage::NativeConstruct()
{
	UE_LOG(LogTemp, Log, TEXT("\n==========================================================="));
	UE_LOG(LogTemp, Log, TEXT("🔄 DAILY_UPGRADE_REWARD_PAGE_CONSTRUCT_START"));
	UE_LOG(LogTemp, Log, TEXT("🆔 初始页面地址: %p"), this);
	UE_LOG(LogTemp, Log, TEXT("⏰ 构造时间: %s"), *FDateTime::Now().ToString());
	UE_LOG(LogTemp, Log, TEXT("===========================================================\n"));
	
	Super::NativeConstruct();
	
	// 订阅UpgradeActivitySubsystem的奖励图标索引更新事件
	SubscribeToSubsystemEvents();
	
	// 在NativeConstruct阶段执行初始化逻辑，确保UI完全准备好
	InitializeRewardItemIcons();
	UpdateChestCountText();
	InitializeExperienceChestWidgets();
	InitializeFixedPrizeWidget();  // 初始化固定奖励控件
	UpdateExperienceDisplay();
	
	// 延迟执行居中显示，等待UI完全渲染完成
	FTimerHandle CenterTimerHandle;
	GetWorld()->GetTimerManager().SetTimer(CenterTimerHandle, this, &UDailyUpgradeRewardPage::CenterScrollBoxOnCurrentExperience, 0.2f, false);
	UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: 已设置延迟居中显示定时器(0.2秒)"));
	
	UE_LOG(LogTemp, Log, TEXT("\n==========================================================="));
	UE_LOG(LogTemp, Log, TEXT("✅ DAILY_UPGRADE_REWARD_PAGE_CONSTRUCT_END"));
	UE_LOG(LogTemp, Log, TEXT("🆔 最终页面地址: %p"), this);
	UE_LOG(LogTemp, Log, TEXT("===========================================================\n"));
}

/**
 * @brief 原生析构函数 - Widget销毁前执行清理工作
 * @details 在Widget即将被销毁时调用，用于释放资源和解绑事件
 * 主要功能：
 * 1. 解绑重选奖励按钮的点击事件
 * 2. 防止悬空指针和内存泄漏
 * 3. 调用父类原生析构
 * @note 必须在Super::NativeDestruct()之前解绑事件
 */
void UDailyUpgradeRewardPage::NativeDestruct()
{
	// 注意：不取消订阅Subsystem事件，让缓存的页面也能持续接收广播
	// UnsubscribeFromSubsystemEvents(); // 已删除此行
	
	// 解绑重选奖励按钮事件
	if (ReselectRewardButton)
	{
		ReselectRewardButton->OnClicked.RemoveDynamic(this, &UDailyUpgradeRewardPage::OnReselectRewardClicked);
		UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: 重选奖励按钮事件已解绑"));
	}
	
	Super::NativeDestruct();
}

/**
 * @brief 初始化奖励物品图标缓存
 * @details 从配置表中读取数据，通过多表关联获取奖励物品的图标资源
 * 数据流向：
 * 1. 读取DT_DailyUpgradeRewardConfigRow表(ActivityID==110)
 * 2. 获取RewardItemIDs数组的最后一个元素作为BoxID
 * 3. 通过ActivitySubsystem查询TreasureBoxItemRow表
 * 4. 通过ItemID关联ItemDetailRow表获取ItemIcon
 * 5. 将所有ItemIcon缓存到CachedItemIcons数组
 * @note 支持编辑器预览模式下的优雅降级处理
 */
void UDailyUpgradeRewardPage::InitializeRewardItemIcons()
{
	// 检查是否为编辑器预览模式
	if (!GetWorld() || !GetWorld()->IsGameWorld())
	{
		return; // 静默返回，不输出日志
	}

	// 全局变量存储ItemIcon数据
	CachedItemIcons.Empty();

	// 1. 通过GameInstance获取UpgradeActivitySubsystem
	UGameInstance* GameInstance = GetGameInstance();
	if (!GameInstance)
	{
		// 在编辑器预览模式下静默处理
		UE_LOG(LogTemp, Warning, TEXT("DailyUpgradeRewardPage: 无法获取GameInstance，使用空图标数组"));
		UpdateRewardItemImage();
		return;
	}

	UUpgradeActivitySubsystem* UpgradeSub = GameInstance->GetSubsystem<UUpgradeActivitySubsystem>();
	if (!UpgradeSub)
	{
		// 在编辑器预览模式下静默处理
		UE_LOG(LogTemp, Warning, TEXT("DailyUpgradeRewardPage: 无法获取UpgradeActivitySubsystem，使用空图标数组"));
		UpdateRewardItemImage();
		return;
	}

	// 2. 调用Subsystem方法获取奖励物品图标数据
	CachedItemIcons = UpgradeSub->GetRewardItemIcons();
	
	if (CachedItemIcons.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("DailyUpgradeRewardPage: 未获取到任何奖励物品图标"));
	}
	else
	{
		UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: 成功获取到 %d 个奖励物品图标"), CachedItemIcons.Num());
	}

	// 3. 更新RewardItemImage显示
	UpdateRewardItemImage();
}

/**
 * @brief 更新奖励物品图像显示
 * @details 将缓存的ItemIcon数据显示到RewardItemImage控件上
 * 主要功能：
 * 1. 检查RewardItemImage控件是否存在
 * 2. 如果有缓存的图标，则显示当前索引的图标
 * 3. 如果没有缓存图标，则隐藏控件
 * @note 使用SetBrushFromSoftTexture支持异步资源加载
 */
void UDailyUpgradeRewardPage::UpdateRewardItemImage()
{
	if (!RewardItemImage)
	{
		UE_LOG(LogTemp, Error, TEXT("DailyUpgradeRewardPage: RewardItemImage控件未绑定"));
		return;
	}

	if (CachedItemIcons.Num() > 0)
	{
		// 通过GameInstance获取UpgradeActivitySubsystem来获取当前索引
		UGameInstance* GameInstance = GetGameInstance();
		if (GameInstance)
		{
			UUpgradeActivitySubsystem* UpgradeSub = GameInstance->GetSubsystem<UUpgradeActivitySubsystem>();
			if (UpgradeSub)
			{
				int32 CurrentIndex = UpgradeSub->GetCurrentRewardIconIndex();
				
				// 确保索引在有效范围内
				if (CurrentIndex >= 0 && CurrentIndex < CachedItemIcons.Num())
				{
					RewardItemImage->SetBrushFromSoftTexture(CachedItemIcons[CurrentIndex]);
					UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: 设置RewardItemImage显示索引 %d 的图标"), CurrentIndex);
				}
				else
				{
					// 索引超出范围，显示第一个图标
					RewardItemImage->SetBrushFromSoftTexture(CachedItemIcons[0]);
					UE_LOG(LogTemp, Warning, TEXT("DailyUpgradeRewardPage: 索引 %d 超出范围，显示第一个图标"), CurrentIndex);
				}
			}
			else
			{
				// 无法获取Subsystem，显示第一个图标
				RewardItemImage->SetBrushFromSoftTexture(CachedItemIcons[0]);
				UE_LOG(LogTemp, Warning, TEXT("DailyUpgradeRewardPage: 无法获取UpgradeActivitySubsystem，显示第一个图标"));
			}
		}
		else
		{
			// 无法获取GameInstance，显示第一个图标
			RewardItemImage->SetBrushFromSoftTexture(CachedItemIcons[0]);
			UE_LOG(LogTemp, Warning, TEXT("DailyUpgradeRewardPage: 无法获取GameInstance，显示第一个图标"));
		}
	}
	else
	{
		// 在编辑器预览模式下静默处理，不输出日志
		RewardItemImage->SetVisibility(ESlateVisibility::Hidden);
	}
}

/**
 * @brief 切换到下一个奖励图标
 * @details 循环切换CachedItemIcons数组中的图标索引，并更新Subsystem中的RewardIconIndex
 */
void UDailyUpgradeRewardPage::SwitchToNextRewardIcon()
{
	if (CachedItemIcons.Num() <= 1)
	{
		UE_LOG(LogTemp, Warning, TEXT("DailyUpgradeRewardPage: 图标数量不足，无法切换"));
		return;
	}

	// 通过GameInstance获取UpgradeActivitySubsystem
	UGameInstance* GameInstance = GetGameInstance();
	if (!GameInstance)
	{
		UE_LOG(LogTemp, Error, TEXT("DailyUpgradeRewardPage: 无法获取GameInstance"));
		return;
	}

	UUpgradeActivitySubsystem* UpgradeSub = GameInstance->GetSubsystem<UUpgradeActivitySubsystem>();
	if (!UpgradeSub)
	{
		UE_LOG(LogTemp, Error, TEXT("DailyUpgradeRewardPage: 无法获取UpgradeActivitySubsystem"));
		return;
	}

	// 获取当前索引并计算下一个索引
	int32 CurrentIndex = UpgradeSub->GetCurrentRewardIconIndex();
	int32 NextIndex = (CurrentIndex + 1) % CachedItemIcons.Num();

	// 设置新的索引
	if (UpgradeSub->SetCurrentRewardIconIndex(NextIndex))
	{
		UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: 切换到下一个图标索引 %d"), NextIndex);
		UpdateRewardItemImage();
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("DailyUpgradeRewardPage: 设置图标索引失败"));
	}
}

/**
 * @brief 切换到上一个奖励图标
 * @details 循环切换CachedItemIcons数组中的图标索引，并更新Subsystem中的RewardIconIndex
 */
void UDailyUpgradeRewardPage::SwitchToPreviousRewardIcon()
{
	if (CachedItemIcons.Num() <= 1)
	{
		UE_LOG(LogTemp, Warning, TEXT("DailyUpgradeRewardPage: 图标数量不足，无法切换"));
		return;
	}

	// 通过GameInstance获取UpgradeActivitySubsystem
	UGameInstance* GameInstance = GetGameInstance();
	if (!GameInstance)
	{
		UE_LOG(LogTemp, Error, TEXT("DailyUpgradeRewardPage: 无法获取GameInstance"));
		return;
	}

	UUpgradeActivitySubsystem* UpgradeSub = GameInstance->GetSubsystem<UUpgradeActivitySubsystem>();
	if (!UpgradeSub)
	{
		UE_LOG(LogTemp, Error, TEXT("DailyUpgradeRewardPage: 无法获取UpgradeActivitySubsystem"));
		return;
	}

	// 获取当前索引并计算上一个索引
	int32 CurrentIndex = UpgradeSub->GetCurrentRewardIconIndex();
	int32 PreviousIndex = (CurrentIndex - 1 + CachedItemIcons.Num()) % CachedItemIcons.Num();

	// 设置新的索引
	if (UpgradeSub->SetCurrentRewardIconIndex(PreviousIndex))
	{
		UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: 切换到上一个图标索引 %d"), PreviousIndex);
		UpdateRewardItemImage();
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("DailyUpgradeRewardPage: 设置图标索引失败"));
	}
}

/**
 * @brief 更新宝箱数量显示文本
 * @details 从配置表中获取奖励物品数量信息并显示在界面上
 * 数据流向：
 * 1. 读取DT_DailyUpgradeRewardConfigRow表(ActivityID==110)
 * 2. 获取RewardItemCounts数组的最后一个元素
 * 3. 格式化为"X数量"的形式显示在ChestCountText控件上
 * @note 在编辑器预览模式下会静默返回
 */
void UDailyUpgradeRewardPage::UpdateChestCountText()
{
	if (!ChestCountText)
	{
		UE_LOG(LogTemp, Error, TEXT("DailyUpgradeRewardPage: ChestCountText控件未绑定"));
		return;
	}

	// 检查是否为编辑器预览模式
	if (!GetWorld() || !GetWorld()->IsGameWorld())
	{
		return; // 静默返回，不输出日志
	}

	// 通过Subsystem获取宝箱数量
	UGameInstance* GameInstance = GetGameInstance();
	if (!GameInstance)
	{
		UE_LOG(LogTemp, Warning, TEXT("DailyUpgradeRewardPage: 无法获取GameInstance"));
		ChestCountText->SetText(FText::FromString(TEXT("0")));
		return;
	}

	UUpgradeActivitySubsystem* Sub = GameInstance->GetSubsystem<UUpgradeActivitySubsystem>();
	if (!Sub)
	{
		UE_LOG(LogTemp, Error, TEXT("DailyUpgradeRewardPage: 无法获取UpgradeActivitySubsystem"));
		ChestCountText->SetText(FText::FromString(TEXT("0")));
		return;
	}

	// 调用Subsystem方法获取宝箱数量
	FString ChestCount = Sub->GetChestCount();
	
	// 显示在ChestCountText控件上（拼接X前缀）
	FString DisplayText = FString::Printf(TEXT("X%s"), *ChestCount);
	ChestCountText->SetText(FText::FromString(DisplayText));
	UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: ChestCountText已更新为: %s"), *DisplayText);
}

/**
 * @brief 初始化经验宝箱控件列表
 * @details 动态创建经验宝箱控件并根据存档状态设置显示效果
 * 主要功能：
 * 1. 通过UpgradeActivitySubsystem获取活动配置和玩家记录
 * 2. 遍历RewardItemIDs数组创建对应的ExperienceChestClaimWidget
 * 3. 根据存档状态(Record.ChestClaimStatus)设置控件的视觉状态
 * 4. 设置每个宝箱的奖励数量显示
 * @note 使用Subsystem模式实现数据访问层与UI层的分离
 */
void UDailyUpgradeRewardPage::InitializeExperienceChestWidgets()
{
	if (!ItemsScrollBox)
	{
		UE_LOG(LogTemp, Error, TEXT("DailyUpgradeRewardPage: ItemsScrollBox控件未绑定"));
		return;
	}

	// 检查是否为编辑器预览模式
	if (!GetWorld() || !GetWorld()->IsGameWorld())
	{
		return; // 静默返回，不输出日志
	}

	// 通过Subsystem获取配置和记录
	UGameInstance* GameInstance = GetGameInstance();
	if (!GameInstance)
	{
		UE_LOG(LogTemp, Warning, TEXT("DailyUpgradeRewardPage: 无法获取GameInstance"));
		return;
	}

	UUpgradeActivitySubsystem* Sub = GameInstance->GetSubsystem<UUpgradeActivitySubsystem>();
	if (!Sub)
	{
		UE_LOG(LogTemp, Error, TEXT("DailyUpgradeRewardPage: 无法获取UpgradeActivitySubsystem"));
		return;
	}

	const auto* Config = Sub->GetActivityConfig();
	if (!Config)
	{
		UE_LOG(LogTemp, Error, TEXT("DailyUpgradeRewardPage: 无法获取活动配置"));
		return;
	}

	// 调用UpgradeActivitySubsystem获取宝箱图标数据
	TArray<TSoftObjectPtr<UTexture2D>> ChestBoxIcons = Sub->GetChestBoxIcons();
	UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: 获取到 %d 个宝箱图标"), ChestBoxIcons.Num());
	
	// 调用UpgradeActivitySubsystem获取TaskRelatedValues数据
	TArray<int32> TaskRelatedValues = Sub->GetTaskRelatedValues();
	UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: 获取到 %d 个TaskRelatedValues"), TaskRelatedValues.Num());
	
	// 调用UpgradeActivitySubsystem获取当前经验值
	int32 CurrentExpFromSubsystem = Sub->GetCurrentExperience();
	UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: 获取到当前经验值: %d"), CurrentExpFromSubsystem);
	
	// 计算需要显示的Widget数量（刨除最后一个索引）
	int32 DisplayWidgetCount = FMath::Min(Config->RewardItemIDs.Num() - 1, ChestBoxIcons.Num());
	UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: 计划显示 %d 个ExperienceChestClaimWidget（刨除最后一个索引）"), DisplayWidgetCount);

	auto& Record = Sub->GetRecord();

	// 检查 ExperienceChestWidgetClass 是否设置
	if (ExperienceChestWidgetClass == nullptr)
	{
		UE_LOG(LogTemp, Error, TEXT("ExperienceChestWidgetClass 未设置！请在蓝图中指定 WBP_ExperienceChestClaimWidget 类"));
		return;
	}

	// 清空现有的子控件
	ItemsScrollBox->ClearChildren();

	// 遍历RewardItemIDs创建Widget（刨除最后一个索引）
	for (int32 i = 0; i < DisplayWidgetCount; ++i)
	{
		UExperienceChestClaimWidget* ChestWidget = CreateWidget<UExperienceChestClaimWidget>(GetWorld(), ExperienceChestWidgetClass.Get());
		if (!ChestWidget)
		{
			UE_LOG(LogTemp, Error, TEXT("DailyUpgradeRewardPage: 创建ExperienceChestClaimWidget失败，索引: %d"), i);
			continue;
		}
		
		// 设置宝箱索引
		ChestWidget->SetChestIndex(i);
		
		// 立即更新进度条（使用正确的索引）
		ChestWidget->UpdateProgressBar();
		
		// 关键：绑定事件
		ChestWidget->OnChestClaimRequested.AddDynamic(this, &UDailyUpgradeRewardPage::HandleChestClaimRequest);
		
		// 关键：根据完整条件来决定显示状态
		bool bIsClaimed = Record.ChestClaimStatus.IsValidIndex(i) && Record.ChestClaimStatus[i] == 1;
		bool bHasEnoughExp = false;
		
		// 检查经验值条件
		if (!bIsClaimed && TaskRelatedValues.IsValidIndex(i))
		{
			int32 RequiredExp = TaskRelatedValues[i];
			bHasEnoughExp = (CurrentExpFromSubsystem >= RequiredExp);
		}
		
		// 根据完整条件设置按钮状态
		if (bIsClaimed)
		{
			// 已领取状态
			ChestWidget->SetButtonClaimedState();
			UE_LOG(LogTemp, Log, TEXT("宝箱%d: 已领取状态"), i);
		}
		else if (bHasEnoughExp)
		{
			// 满足条件，启用按钮
			ChestWidget->SetButtonEnabledState();
			UE_LOG(LogTemp, Log, TEXT("宝箱%d: 满足条件，启用按钮"), i);
		}
		else
		{
			// 未满足条件，禁用但保持原外观
			ChestWidget->SetButtonDisabledState();
			UE_LOG(LogTemp, Log, TEXT("宝箱%d: 条件不足，禁用按钮"), i);
		}
		
		// 设置奖励数量
		FString RewardCount = TEXT("0");
		if (i < Config->RewardItemCounts.Num())
		{
			RewardCount = Config->RewardItemCounts[i];
		}
		
		if (ChestWidget->ChestCountText)
		{
			FString DisplayText = FString::Printf(TEXT("X%s"), *RewardCount);
			ChestWidget->ChestCountText->SetText(FText::FromString(DisplayText));
		}
		
		// 设置宝箱图标到ChestClaimButton（刨除最后一个索引）
		if (i < ChestBoxIcons.Num() && ChestWidget->ChestClaimButton)
		{
			UTexture2D* BoxIcon = ChestBoxIcons[i].LoadSynchronous();
			if (BoxIcon)
			{
				ChestWidget->SetChestBoxIcon(BoxIcon);
				UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: 为第%d个宝箱成功设置图标（刨除最后一个索引）"), i + 1);
			}
			else
			{
				UE_LOG(LogTemp, Warning, TEXT("DailyUpgradeRewardPage: 第%d个宝箱图标加载失败"), i + 1);
			}
		}
		
		// 设置ExperienceText显示TaskRelatedValues对应索引的数据
		if (i < TaskRelatedValues.Num() && ChestWidget->ExperienceText)
		{
			FString ExperienceValue = FString::FromInt(TaskRelatedValues[i]);
			ChestWidget->ExperienceText->SetText(FText::FromString(ExperienceValue));
			UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: 为第%d个宝箱设置ExperienceText: %s"), i + 1, *ExperienceValue);
			
			// 根据条件控制HighlightFrameImage显示：
			// ChestClaimStatus=0 且 CurrentExperience >= TaskRelatedValues[i] 时显示高亮框
			bool bShouldShowHighlight = false;
			if (Record.ChestClaimStatus.IsValidIndex(i) && Record.ChestClaimStatus[i] == 0)
			{
				if (CurrentExpFromSubsystem >= TaskRelatedValues[i])
				{
					bShouldShowHighlight = true;
				}
			}
			
			UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: 第%d个宝箱详细判断:"), i + 1);
			UE_LOG(LogTemp, Log, TEXT("  - 当前经验: %d"), CurrentExpFromSubsystem);
			UE_LOG(LogTemp, Log, TEXT("  - 需要经验: %d"), TaskRelatedValues[i]);
			UE_LOG(LogTemp, Log, TEXT("  - 是否已领取: %s"), (Record.ChestClaimStatus.IsValidIndex(i) && Record.ChestClaimStatus[i] == 1) ? TEXT("是") : TEXT("否"));
			UE_LOG(LogTemp, Log, TEXT("  - 条件判断: %d >= %d = %s"), CurrentExpFromSubsystem, TaskRelatedValues[i], 
				CurrentExpFromSubsystem >= TaskRelatedValues[i] ? TEXT("满足") : TEXT("不满足"));
			UE_LOG(LogTemp, Log, TEXT("  - 最终显示状态: %s"), bShouldShowHighlight ? TEXT("显示") : TEXT("隐藏"));
			
			if (ChestWidget->HighlightFrameImage)
			{
				ChestWidget->HighlightFrameImage->SetVisibility(bShouldShowHighlight ? ESlateVisibility::Visible : ESlateVisibility::Hidden);
				UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: 第%d个宝箱HighlightFrameImage显示状态: %s (ChestClaimStatus: %d, TaskRelatedValue: %d, CurrentExperience: %d)"), 
					i + 1, bShouldShowHighlight ? TEXT("显示") : TEXT("隐藏"), 
					Record.ChestClaimStatus.IsValidIndex(i) ? Record.ChestClaimStatus[i] : -1,
					TaskRelatedValues[i], CurrentExpFromSubsystem);
			}
		}
		else if (ChestWidget->ExperienceText)
		{
			// 如果索引超出范围，显示默认值0
			ChestWidget->ExperienceText->SetText(FText::FromString(TEXT("0")));
			UE_LOG(LogTemp, Warning, TEXT("DailyUpgradeRewardPage: 第%d个宝箱的TaskRelatedValues索引超出范围，显示默认值0"), i + 1);
			
			// 索引超出范围时隐藏高亮框
			if (ChestWidget->HighlightFrameImage)
			{
				ChestWidget->HighlightFrameImage->SetVisibility(ESlateVisibility::Hidden);
				UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: 第%d个宝箱索引超出范围，隐藏HighlightFrameImage"), i + 1);
			}
		}
		
		ItemsScrollBox->AddChild(ChestWidget);
		UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: 成功创建第%d个ExperienceChestClaimWidget，已领取: %s，事件绑定: %s（刨除最后一个索引）"), 
			i + 1, bIsClaimed ? TEXT("是") : TEXT("否"),
			ChestWidget->OnChestClaimRequested.IsBound() ? TEXT("✅ 已绑定") : TEXT("❌ 未绑定"));
		
		// 更新DiamondIcon颜色
		ChestWidget->UpdateDiamondIconColor();
		
		// 更新ExperienceText颜色
		ChestWidget->UpdateExperienceTextColor();
	}
}

/**
 * @brief 更新经验值显示
 * @details 从UpgradeActivitySubsystem获取当前经验值并在界面上显示
 * 主要功能：
 * 1. 检查CurrentExpText控件是否存在
 * 2. 通过Subsystem获取玩家的当前经验值
 * 3. 将数值格式化后显示在文本控件上
 * @note 支持编辑器预览模式下的默认值显示
 */
void UDailyUpgradeRewardPage::UpdateExperienceDisplay()
{
	if (!CurrentExpText)
	{
		UE_LOG(LogTemp, Error, TEXT("DailyUpgradeRewardPage: CurrentExpText控件未绑定"));
		return;
	}

	// 检查是否为编辑器预览模式
	if (!GetWorld() || !GetWorld()->IsGameWorld())
	{
		return; // 静默返回，不输出日志
	}

	// 通过Subsystem获取当前经验值
	UGameInstance* GameInstance = GetGameInstance();
	if (!GameInstance)
	{
		CurrentExpText->SetText(FText::FromString(TEXT("0")));
		UE_LOG(LogTemp, Warning, TEXT("DailyUpgradeRewardPage: 无法获取GameInstance"));
		return;
	}

	UUpgradeActivitySubsystem* Sub = GameInstance->GetSubsystem<UUpgradeActivitySubsystem>();
	if (!Sub)
	{
		CurrentExpText->SetText(FText::FromString(TEXT("0")));
		UE_LOG(LogTemp, Warning, TEXT("DailyUpgradeRewardPage: 无法获取UpgradeActivitySubsystem"));
		return;
	}

	// 调用UpgradeActivitySubsystem类中的方法，查询UpgradeRewardSaveRecord动态表，
	// 获取最大RecordDate的CurrentExperience的值
	int32 CurrentExp = Sub->GetCurrentExperience();
	FString ExpText = FString::Printf(TEXT("%d"), CurrentExp);
	CurrentExpText->SetText(FText::FromString(ExpText));
	
	UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: CurrentExpText已更新为: %d"), CurrentExp);
}

/**
 * @brief 刷新整个页面UI
 * @details 重新初始化所有UI组件，确保显示与数据同步
 * 主要功能：
 * 1. 重新初始化经验宝箱控件列表
 * 2. 更新宝箱数量显示
 * 3. 更新经验值显示
 * @note 通常在数据发生变化后调用此方法
 */
void UDailyUpgradeRewardPage::RefreshUI()
{
	// 🆔 自我身份核对 - 显示页面唯一标识
	FString PageIdentity = GetPageIdentity();
	UE_LOG(LogTemp, Log, TEXT("\n==========================================================="));
	UE_LOG(LogTemp, Log, TEXT("🔄 DAILY_UPGRADE_REWARD_PAGE_REFRESH_START"));
	UE_LOG(LogTemp, Log, TEXT("🆔 页面身份: %s"), *PageIdentity);
	UE_LOG(LogTemp, Log, TEXT("📍 刷新时页面地址: %p"), this);
	UE_LOG(LogTemp, Log, TEXT("⏰ 刷新时间: %s"), *FDateTime::Now().ToString());
	UE_LOG(LogTemp, Log, TEXT("===========================================================\n"));
	
	// 执行全量刷新：重新初始化所有核心UI组件
	
	// 1. 重新初始化奖励物品图标缓存（最重要的数据源）
	UE_LOG(LogTemp, Log, TEXT("[步骤1/5] 🎯 初始化奖励物品图标缓存..."));
	InitializeRewardItemIcons();
	UE_LOG(LogTemp, Log, TEXT("✅ 奖励物品图标缓存已刷新 (缓存数量: %d)"), CachedItemIcons.Num());
	
	// 2. 更新现有经验宝箱控件状态（不重新创建）
	UE_LOG(LogTemp, Log, TEXT("\n[步骤2/6] 📦 更新经验宝箱控件状态..."));
	UpdateExperienceChestWidgetsState();
	int32 ChestCount = ItemsScrollBox ? ItemsScrollBox->GetChildrenCount() : 0;
	UE_LOG(LogTemp, Log, TEXT("✅ 经验宝箱控件状态已更新 (控件数量: %d)"), ChestCount);
	
	// 2.1 额外刷新所有进度条显示
	UE_LOG(LogTemp, Log, TEXT("\n[步骤2.1/6] 📊 刷新经验进度条显示..."));
	RefreshAllProgressBars();
	UE_LOG(LogTemp, Log, TEXT("✅ 经验进度条已刷新"));
	
	// 2.2 根据当前经验值居中显示相关内容
	UE_LOG(LogTemp, Log, TEXT("\n[步骤2.2/6] 🎯 根据经验值居中显示内容..."));
	CenterScrollBoxOnCurrentExperience();
	UE_LOG(LogTemp, Log, TEXT("✅ ScrollBox已根据经验值居中定位"));
	
	// 3. 更新宝箱数量显示
	UE_LOG(LogTemp, Log, TEXT("\n[步骤3/6] 📊 更新宝箱数量显示..."));
	UpdateChestCountText();
	UE_LOG(LogTemp, Log, TEXT("✅ 宝箱数量文本已刷新"));
	
	// 4. 更新经验值显示
	UE_LOG(LogTemp, Log, TEXT("\n[步骤4/6] ⭐ 更新经验值显示..."));
	UpdateExperienceDisplay();
	UE_LOG(LogTemp, Log, TEXT("✅ 经验值显示已刷新"));
	
	// 5. 更新奖励物品图像显示（基于最新的缓存数据）
	UE_LOG(LogTemp, Log, TEXT("\n[步骤5/6] 🖼️ 更新奖励物品图像显示..."));
	UpdateRewardItemImage();
	UE_LOG(LogTemp, Log, TEXT("✅ 奖励物品图像已刷新"));
	
	// 6. 更新固定奖励控件状态
	UE_LOG(LogTemp, Log, TEXT("\n[步骤6/6] 🎁 更新固定奖励控件状态..."));
	UpdateFixedPrizeWidget();
	UE_LOG(LogTemp, Log, TEXT("✅ 固定奖励控件状态已刷新"));
	
	// 🎉 刷新完成总结
	UE_LOG(LogTemp, Log, TEXT("\n==========================================================="));
	UE_LOG(LogTemp, Log, TEXT("🎉 DAILY_UPGRADE_REWARD_PAGE_REFRESH_COMPLETE"));
	UE_LOG(LogTemp, Log, TEXT("🆔 页面身份: %s"), *PageIdentity);
	UE_LOG(LogTemp, Log, TEXT("📊 最终状态: 宝箱控件数=%d, 图标缓存数=%d"), ChestCount, CachedItemIcons.Num());
	UE_LOG(LogTemp, Log, TEXT("===========================================================\n"));
}

/**
 * @brief 重选奖励按钮点击事件处理
 * @details 处理重选奖励功能的核心逻辑
 * 主要功能：
 * 1. 通过UpgradeActivitySubsystem获取重选奖励选项数据
 * 2. 验证必要的组件和数据是否存在
 * 3. 创建ActivityConfirmPopupWidget弹窗实例
 * 4. 初始化弹窗并添加到视口显示
 * 5. 将奖励选项数据传递给弹窗组件
 * @note 这是重选奖励功能的入口点，实现了UI层与数据层的解耦
 */
void UDailyUpgradeRewardPage::OnReselectRewardClicked()
{
	UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: 重选奖励按钮被点击"));
	
	// 通过Subsystem获取重选奖励选项数据
	UGameInstance* GameInstance = GetGameInstance();
	if (!GameInstance)
	{
		UE_LOG(LogTemp, Error, TEXT("DailyUpgradeRewardPage: 无法获取GameInstance"));
		return;
	}
	
	UUpgradeActivitySubsystem* UpgradeSub = GameInstance->GetSubsystem<UUpgradeActivitySubsystem>();
	if (!UpgradeSub)
	{
		UE_LOG(LogTemp, Error, TEXT("DailyUpgradeRewardPage: 无法获取UpgradeActivitySubsystem"));
		return;
	}
	
	TArray<TSoftObjectPtr<UTexture2D>> RewardOptions = UpgradeSub->GetReselectRewardOptions();
	if (RewardOptions.Num() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("DailyUpgradeRewardPage: 没有可重选的奖励选项"));
		return;
	}
	
	UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: 准备弹出确认页面，奖励选项数量: %d"), RewardOptions.Num());
	
	// 检查ActivityConfirmPopupWidgetClass是否已设置
	if (!ActivityConfirmPopupWidgetClass)
	{
		UE_LOG(LogTemp, Error, TEXT("DailyUpgradeRewardPage: ActivityConfirmPopupWidgetClass未设置！请在蓝图中指定WBP_ActivityConfirmPopupWidget类"));
		return;
	}
	
	// 创建ActivityConfirmPopupWidget实例
	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogTemp, Error, TEXT("DailyUpgradeRewardPage: 无法获取世界上下文"));
		return;
	}
	
	UActivityConfirmPopupWidget* ConfirmPopup = CreateWidget<UActivityConfirmPopupWidget>(World, ActivityConfirmPopupWidgetClass);
	if (!ConfirmPopup)
	{
		UE_LOG(LogTemp, Error, TEXT("DailyUpgradeRewardPage: 创建ActivityConfirmPopupWidget失败"));
		return;
	}
	
	UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: 成功创建ActivityConfirmPopupWidget实例"));
	
	// 获取ActivityID==110的配置数据用于初始化弹窗
	const FDailyUpgradeRewardConfigRow* Config = UpgradeSub->GetActivityConfig();
	if (!Config)
	{
		UE_LOG(LogTemp, Error, TEXT("DailyUpgradeRewardPage: 无法获取活动配置数据"));
		return;
	}
	
	// 转换为DailyLoginConfigRow格式（因为ActivityConfirmPopupWidget期望这种格式）
	TArray<FDailyLoginConfigRow> PopupOptions;
	FDailyLoginConfigRow TempRow;
	TempRow.ActivityID = Config->ActivityID;
	
	// 使用最后一个RewardItemID作为RewardItemID
	if (Config->RewardItemIDs.Num() > 0)
	{
		FString LastRewardItemID = Config->RewardItemIDs.Last();
		TempRow.RewardItemID = FCString::Atoi(*LastRewardItemID);
		UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: 使用RewardItemID: %d"), TempRow.RewardItemID);
	}
	
	PopupOptions.Add(TempRow);
	
	// 初始化弹窗
	ConfirmPopup->InitializePopup(PopupOptions, 0);
	
	// 添加到视口显示
	ConfirmPopup->AddToViewport(1000); // 使用高Z-order确保显示在最上层
	
	UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: ActivityConfirmPopupWidget已添加到视口显示"));
	
	// 记录奖励选项信息
	for (int32 i = 0; i < RewardOptions.Num(); ++i)
	{
		UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: 奖励选项 %d: %s"), i + 1, 
			RewardOptions[i].IsNull() ? TEXT("空") : *RewardOptions[i].ToString());
	}
}

void UDailyUpgradeRewardPage::SubscribeToSubsystemEvents()
{
	// 通过GameInstance获取UpgradeActivitySubsystem
	UGameInstance* GameInstance = GetGameInstance();
	if (!GameInstance)
	{
		UE_LOG(LogTemp, Error, TEXT("DailyUpgradeRewardPage: 无法获取GameInstance"));
		return;
	}
	
	UUpgradeActivitySubsystem* UpgradeSub = GameInstance->GetSubsystem<UUpgradeActivitySubsystem>();
	if (!UpgradeSub)
	{
		UE_LOG(LogTemp, Error, TEXT("DailyUpgradeRewardPage: 无法获取UpgradeActivitySubsystem"));
		return;
	}
	
	// 记录Subsystem地址用于调试
	UE_LOG(LogTemp, Log, TEXT("🔗 Subsystem地址绑定: %p -> Page地址: %p"), UpgradeSub, this);
	
	// 先解绑已存在的事件绑定，防止重复绑定（因为NativeConstruct会多次触发）
	UpgradeSub->OnRewardIconIndexChanged.RemoveDynamic(this, &UDailyUpgradeRewardPage::OnRewardIconIndexChanged);
	UpgradeSub->OnGlobalRefresh.RemoveDynamic(this, &UDailyUpgradeRewardPage::RefreshUI);
	
	// 订阅奖励图标索引更新事件
	UpgradeSub->OnRewardIconIndexChanged.AddDynamic(this, &UDailyUpgradeRewardPage::OnRewardIconIndexChanged);
	
	// 订阅全局刷新事件
	UpgradeSub->OnGlobalRefresh.AddDynamic(this, &UDailyUpgradeRewardPage::RefreshUI);
	
	UE_LOG(LogTemp, Log, TEXT("✅ 事件绑定完成:"));
	UE_LOG(LogTemp, Log, TEXT("   OnRewardIconIndexChanged: %s"), UpgradeSub->OnRewardIconIndexChanged.IsBound() ? TEXT("✅ 已绑定") : TEXT("❌ 未绑定"));
	UE_LOG(LogTemp, Log, TEXT("   OnGlobalRefresh: %s"), UpgradeSub->OnGlobalRefresh.IsBound() ? TEXT("✅ 已绑定") : TEXT("❌ 未绑定"));
	UE_LOG(LogTemp, Log, TEXT("🔗 绑定关系: Subsystem[%p] <-> Page[%p]"), UpgradeSub, this);
	
	// 验证绑定是否真的成功
	if (UpgradeSub->OnGlobalRefresh.IsBound())
	{
		UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: ✅ OnGlobalRefresh事件绑定成功"));
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("DailyUpgradeRewardPage: ❌ OnGlobalRefresh事件绑定失败"));
	}
}

void UDailyUpgradeRewardPage::UnsubscribeFromSubsystemEvents()
{
	// 通过GameInstance获取UpgradeActivitySubsystem
	UGameInstance* GameInstance = GetGameInstance();
	if (!GameInstance)
	{
		return;
	}
	
	UUpgradeActivitySubsystem* UpgradeSub = GameInstance->GetSubsystem<UUpgradeActivitySubsystem>();
	if (!UpgradeSub)
	{
		return;
	}
	
	// 取消订阅奖励图标索引更新事件
	UpgradeSub->OnRewardIconIndexChanged.RemoveDynamic(this, &UDailyUpgradeRewardPage::OnRewardIconIndexChanged);
	
	// 取消订阅全局刷新事件
	UpgradeSub->OnGlobalRefresh.RemoveDynamic(this, &UDailyUpgradeRewardPage::RefreshUI);
	
	UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: 已取消订阅Subsystem事件"));
}

void UDailyUpgradeRewardPage::OnRewardIconIndexChanged(int32 NewIndex)
{
	UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: 接收到奖励图标索引更新事件，新索引: %d"), NewIndex);
	
	// 重新初始化奖励图标数据
	InitializeRewardItemIcons();
	
	// 更新奖励物品图像显示
	UpdateRewardItemImage();
	
	UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: 奖励图标索引更新完成"));
}

void UDailyUpgradeRewardPage::ManualRefreshUI()
{
	UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: 手动刷新UI被调用 - 页面地址=%p"), this);
	RefreshUI();
}

FString UDailyUpgradeRewardPage::GetPageIdentity() const
{
	// 生成页面唯一身份标识
	FString AddressStr = FString::Printf(TEXT("0x%p"), this);
	FString TimestampStr = FDateTime::Now().ToString(TEXT("MMdd-HHmmss"));
	
	// 返回格式化的身份字符串
	return FString::Printf(TEXT("Page[%s]@%s"), *AddressStr, *TimestampStr);
}

// ==================== 事件处理函数 ====================

void UDailyUpgradeRewardPage::HandleChestClaimRequest(int32 ChestIndex)
{
	UE_LOG(LogTemp, Log, TEXT("=========================================="));
	UE_LOG(LogTemp, Log, TEXT("🎉 DailyUpgradeRewardPage: 接收到宝箱领取请求！"));
	UE_LOG(LogTemp, Log, TEXT("📦 宝箱索引: %d"), ChestIndex);
	UE_LOG(LogTemp, Log, TEXT("📍 页面地址: %p"), this);
	UE_LOG(LogTemp, Log, TEXT("=========================================="));
	
	// 获取Subsystem
	UGameInstance* GameInstance = GetGameInstance();
	if (!GameInstance)
	{
		UE_LOG(LogTemp, Error, TEXT("DailyUpgradeRewardPage: 无法获取GameInstance"));
		return;
	}
	
	UUpgradeActivitySubsystem* Subsystem = GameInstance->GetSubsystem<UUpgradeActivitySubsystem>();
	if (!Subsystem)
	{
		UE_LOG(LogTemp, Error, TEXT("DailyUpgradeRewardPage: 无法获取UpgradeActivitySubsystem"));
		return;
	}
	
	// 弹出RewardOptionWidget页面
	ShowRewardOptionWidget(ChestIndex);
	
	// 注意：状态更新将在RewardOptionWidget的StoreBtn点击时执行
}

void UDailyUpgradeRewardPage::ShowRewardOptionWidget(int32 ChestIndex)
{
	UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: 准备显示RewardOptionWidget，宝箱索引: %d"), ChestIndex);
	
	// 检查RewardOptionWidgetClass是否已设置
	if (!RewardOptionWidgetClass)
	{
		UE_LOG(LogTemp, Error, TEXT("DailyUpgradeRewardPage: RewardOptionWidgetClass未设置！请在蓝图中指定WBP_RewardOptionWidget类"));
		return;
	}
	
	// 创建RewardOptionWidget实例
	UWorld* World = GetWorld();
	if (!World)
	{
		UE_LOG(LogTemp, Error, TEXT("DailyUpgradeRewardPage: 无法获取世界上下文"));
		return;
	}
	
	URewardOptionWidget* RewardOptionWidget = CreateWidget<URewardOptionWidget>(World, RewardOptionWidgetClass.Get());
	if (!RewardOptionWidget)
	{
		UE_LOG(LogTemp, Error, TEXT("DailyUpgradeRewardPage: 创建RewardOptionWidget失败"));
		return;
	}
	
	UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: 成功创建RewardOptionWidget实例"));
	
	// 获取活动配置数据
	UGameInstance* GameInstance = GetGameInstance();
	if (!GameInstance)
	{
		UE_LOG(LogTemp, Error, TEXT("DailyUpgradeRewardPage: 无法获取GameInstance"));
		return;
	}
	
	UUpgradeActivitySubsystem* Subsystem = GameInstance->GetSubsystem<UUpgradeActivitySubsystem>();
	if (!Subsystem)
	{
		UE_LOG(LogTemp, Error, TEXT("DailyUpgradeRewardPage: 无法获取UpgradeActivitySubsystem"));
		return;
	}
	
	const FDailyUpgradeRewardConfigRow* Config = Subsystem->GetActivityConfig();
	if (!Config)
	{
		UE_LOG(LogTemp, Error, TEXT("DailyUpgradeRewardPage: 无法获取活动配置数据"));
		return;
	}
	
	// 准备奖励选项数据
	TArray<FDailyLoginConfigRow> RewardOptions;
	FDailyLoginConfigRow TempRow;
	TempRow.ActivityID = Config->ActivityID;
	
	// 使用对应的RewardItemID
	if (Config->RewardItemIDs.IsValidIndex(ChestIndex))
	{
		FString RewardItemID = Config->RewardItemIDs[ChestIndex];
		TempRow.RewardItemID = FCString::Atoi(*RewardItemID);
		UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: 使用RewardItemID: %d"), TempRow.RewardItemID);
	}
	
	// 设置天数索引（用于后续状态更新）
	TempRow.DayIndex = ChestIndex;
	
	RewardOptions.Add(TempRow);
	
	// 初始化弹窗
	RewardOptionWidget->InitSelection(RewardOptions);
	
	// 添加到视口显示
	RewardOptionWidget->AddToViewport(1000); // 使用高Z-order确保显示在最上层
	
	// 绑定StoreBtn事件
	RewardOptionWidget->OnStoreToBag.Clear();
	RewardOptionWidget->OnStoreToBag.AddDynamic(this, &UDailyUpgradeRewardPage::HandleRewardStore);
	
	UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: RewardOptionWidget已添加到视口并绑定事件"));
}

void UDailyUpgradeRewardPage::RefreshAllProgressBars()
{
	UE_LOG(LogTemp, Log, TEXT("RefreshAllProgressBars 开始执行"));
	
	if (!ItemsScrollBox)
	{
		UE_LOG(LogTemp, Warning, TEXT("ItemsScrollBox 为空，无法刷新进度条"));
		return;
	}
	
	UE_LOG(LogTemp, Log, TEXT("ItemsScrollBox 子控件数量: %d"), ItemsScrollBox->GetChildrenCount());
	
	// 遍历所有子控件并刷新进度条
	int32 UpdatedCount = 0;
	for (int32 i = 0; i < ItemsScrollBox->GetChildrenCount(); ++i)
	{
		UWidget* ChildWidget = ItemsScrollBox->GetChildAt(i);
		UExperienceChestClaimWidget* ChestWidget = Cast<UExperienceChestClaimWidget>(ChildWidget);
		
		if (ChestWidget)
		{
			UE_LOG(LogTemp, Log, TEXT("刷新第 %d 个宝箱控件的进度条"), i);
			ChestWidget->RefreshProgressBar();
			
			// 更新DiamondIcon颜色
			ChestWidget->UpdateDiamondIconColor();
			
			// 更新ExperienceText颜色
			ChestWidget->UpdateExperienceTextColor();
			UpdatedCount++;
		}
		else
		{
			UE_LOG(LogTemp, Log, TEXT("子控件 %d 不是 ExperienceChestClaimWidget 类型"), i);
		}
	}
	
	UE_LOG(LogTemp, Log, TEXT("RefreshAllProgressBars 执行完成，共刷新 %d 个进度条"), UpdatedCount);
}

void UDailyUpgradeRewardPage::UpdateExperienceChestWidgetsState()
{
	UE_LOG(LogTemp, Log, TEXT("UpdateExperienceChestWidgetsState 开始执行"));
	
	if (!ItemsScrollBox)
	{
		UE_LOG(LogTemp, Log, TEXT("ItemsScrollBox 为空"));
		return;
	}
	
	UE_LOG(LogTemp, Log, TEXT("ItemsScrollBox 子控件数量: %d"), ItemsScrollBox->GetChildrenCount());
	
	// 遍历所有子控件并更新状态
	for (int32 i = 0; i < ItemsScrollBox->GetChildrenCount(); ++i)
	{
		UWidget* ChildWidget = ItemsScrollBox->GetChildAt(i);
		UExperienceChestClaimWidget* ChestWidget = Cast<UExperienceChestClaimWidget>(ChildWidget);
		
		if (ChestWidget)
		{
			UE_LOG(LogTemp, Log, TEXT("更新宝箱控件 %d 的状态"), i);
			ChestWidget->UpdateButtonState();
			
			// 更新DiamondIcon颜色
			ChestWidget->UpdateDiamondIconColor();
			
			// 更新ExperienceText颜色
			ChestWidget->UpdateExperienceTextColor();
		}
		else
		{
			UE_LOG(LogTemp, Log, TEXT("子控件 %d 不是 ExperienceChestClaimWidget 类型"), i);
		}
	}
	
	UE_LOG(LogTemp, Log, TEXT("UpdateExperienceChestWidgetsState 执行完成"));
}

void UDailyUpgradeRewardPage::HandleRewardStore(int32 DayIndex)
{
	UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: 接收到奖励存储请求，宝箱索引: %d"), DayIndex);
	
	// 获取Subsystem
	UGameInstance* GameInstance = GetGameInstance();
	if (!GameInstance)
	{
		UE_LOG(LogTemp, Error, TEXT("DailyUpgradeRewardPage: 无法获取GameInstance"));
		return;
	}
	
	UUpgradeActivitySubsystem* Subsystem = GameInstance->GetSubsystem<UUpgradeActivitySubsystem>();
	if (!Subsystem)
	{
		UE_LOG(LogTemp, Error, TEXT("DailyUpgradeRewardPage: 无法获取UpgradeActivitySubsystem"));
		return;
	}
	
	// DayIndex实际上就是ChestIndex
	int32 ChestIndex = DayIndex;
	
	// 修改内存数据
	FUpgradeRewardSaveRecord& Record = const_cast<FUpgradeRewardSaveRecord&>(Subsystem->GetRecord());
	if (Record.ChestClaimStatus.IsValidIndex(ChestIndex))
	{
		Record.ChestClaimStatus[ChestIndex] = 1; // 标记为已领取
		Record.LastUpdateTime = FDateTime::Now();
		
		UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: 成功更新宝箱%d状态为已领取"), ChestIndex);
		
		// 全局刷新活动页面
		Subsystem->OnGlobalRefresh.Broadcast();
		UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: 已广播全局刷新事件"));
		
		// 特别更新FixedPrizeWidget状态
		UpdateFixedPrizeWidget();
		UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: FixedPrizeWidget状态已更新"));
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("DailyUpgradeRewardPage: ChestClaimStatus索引%d无效"), ChestIndex);
	}
	
	// 关闭RewardOptionWidget弹窗
	if (RewardOptionWidgetClass)
	{
		// 查找并移除现有的RewardOptionWidget
		for (TObjectIterator<URewardOptionWidget> It; It; ++It)
		{
			URewardOptionWidget* ExistingWidget = *It;
			if (ExistingWidget && ExistingWidget->IsInViewport())
			{
				ExistingWidget->RemoveFromParent();
				UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: 已关闭RewardOptionWidget弹窗"));
				break;
			}
		}
	}
}

void UDailyUpgradeRewardPage::InitializeFixedPrizeWidget()
{
	UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: 开始初始化FixedPrizeWidget"));
	
	// 检查FixedPrizeWidget控件是否存在
	if (!FixedPrizeWidget)
	{
		UE_LOG(LogTemp, Warning, TEXT("DailyUpgradeRewardPage: FixedPrizeWidget控件未绑定"));
		return;
	}
	
	// 检查是否为编辑器预览模式
	if (!GetWorld() || !GetWorld()->IsGameWorld())
	{
		return; // 静默返回，不输出日志
	}
	
	// 通过Subsystem获取必要数据
	UGameInstance* GameInstance = GetGameInstance();
	if (!GameInstance)
	{
		UE_LOG(LogTemp, Warning, TEXT("DailyUpgradeRewardPage: 无法获取GameInstance"));
		return;
	}
	
	UUpgradeActivitySubsystem* Sub = GameInstance->GetSubsystem<UUpgradeActivitySubsystem>();
	if (!Sub)
	{
		UE_LOG(LogTemp, Error, TEXT("DailyUpgradeRewardPage: 无法获取UpgradeActivitySubsystem"));
		return;
	}
	
	// 使用Subsystem提供的接口获取数据
	bool bShouldShowHighlight = Sub->ShouldShowFixedPrizeHighlight();
	int32 ExperienceValue = Sub->GetFixedPrizeExperienceValue();
	int32 FixedIndex = Sub->GetFixedPrizeIndex();
	
	UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: FixedPrizeWidget数据获取结果:"));
	UE_LOG(LogTemp, Log, TEXT("  - Highlight显示状态: %s"), bShouldShowHighlight ? TEXT("显示") : TEXT("隐藏"));
	UE_LOG(LogTemp, Log, TEXT("  - ExperienceValue(来自TaskRelatedValues最后一个索引): %d"), ExperienceValue);
	UE_LOG(LogTemp, Log, TEXT("  - FixedIndex: %d"), FixedIndex);
	UE_LOG(LogTemp, Log, TEXT("  - 当前经验值: %d"), Sub->GetCurrentExperience());
	
	// 设置HighlightFrameImage的显示状态
	if (FixedPrizeWidget->HighlightFrameImage)
	{
		FixedPrizeWidget->HighlightFrameImage->SetVisibility(
			bShouldShowHighlight ? ESlateVisibility::Visible : ESlateVisibility::Hidden);
		UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: FixedPrizeWidget HighlightFrameImage已%s"), 
			bShouldShowHighlight ? TEXT("显示") : TEXT("隐藏"));
	}
	
	// 设置ExperienceText显示值
	if (FixedPrizeWidget->ExperienceText)
	{
		FString ExperienceText = FString::FromInt(ExperienceValue);
		FixedPrizeWidget->ExperienceText->SetText(FText::FromString(ExperienceText));
		UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: FixedPrizeWidget ExperienceText已设置为: %s (来自TaskRelatedValues最后一个索引)"), *ExperienceText);
	}
	
	// 设置宝箱索引
	if (FixedIndex >= 0)
	{
		FixedPrizeWidget->SetChestIndex(FixedIndex);
		UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: FixedPrizeWidget索引已设置为: %d"), FixedIndex);
	}
	
	// 更新按钮状态
	FixedPrizeWidget->UpdateButtonState();
	
	// 更新钻石图标颜色
	FixedPrizeWidget->UpdateDiamondIconColor();
	
	// 更新经验文本颜色 - 这里会根据CurrentExperience和TaskRelatedValues值判断颜色
	FixedPrizeWidget->UpdateExperienceTextColor();
	
	// 绑定FixedPrizeWidget的领取事件
	FixedPrizeWidget->OnChestClaimRequested.AddDynamic(this, &UDailyUpgradeRewardPage::HandleChestClaimRequest);
	UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: FixedPrizeWidget事件绑定完成"));
	
	// 设置FixedPrizeWidget的宝箱数量文本
	FString ChestCount = Sub->GetFixedPrizeChestCount();
	if (FixedPrizeWidget->ChestCountText)
	{
		FString DisplayText = FString::Printf(TEXT("X%s"), *ChestCount);
		FixedPrizeWidget->ChestCountText->SetText(FText::FromString(DisplayText));
		UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: FixedPrizeWidget宝箱数量已设置为: %s"), *DisplayText);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("DailyUpgradeRewardPage: FixedPrizeWidget的ChestCountText控件未绑定"));
	}
	
	// 更新FixedPrizeWidget专用进度条（调用Subsystem方法）
	float Progress = Sub->CalculateFixedPrizeProgress();
	if (FixedPrizeWidget->ExperienceProgressBar)
	{
		FixedPrizeWidget->ExperienceProgressBar->SetPercent(Progress);
		UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: FixedPrizeWidget进度条已更新为 %.2f%%"), Progress * 100);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("DailyUpgradeRewardPage: FixedPrizeWidget的ExperienceProgressBar控件未绑定"));
	}
	
	// 设置FixedPrizeWidget的宝箱图标
	UTexture2D* BoxIcon = Sub->GetFixedPrizeBoxIcon();
	if (BoxIcon)
	{
		// 使用ExperienceChestClaimWidget已有的SetChestBoxIcon方法
		FixedPrizeWidget->SetChestBoxIcon(BoxIcon);
		UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: FixedPrizeWidget宝箱图标已设置"));
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("DailyUpgradeRewardPage: 无法获取FixedPrizeWidget宝箱图标"));
	}
	
	UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: FixedPrizeWidget初始化完成"));
}

void UDailyUpgradeRewardPage::UpdateFixedPrizeWidget()
{
	UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: 开始更新FixedPrizeWidget状态"));
	
	// 直接调用初始化函数，因为它已经包含了完整的更新逻辑
	InitializeFixedPrizeWidget();
	
	UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: FixedPrizeWidget状态更新完成"));
}

// 全局静态变量用于跨函数通信
namespace
{
	bool GHasInvalidGeometryDetected = false;
}

void UDailyUpgradeRewardPage::CenterScrollBoxOnCurrentExperience()
{
	UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: 开始根据当前经验值居中ScrollBox内容"));
	
	// 强制刷新布局，确保几何信息准确
	if (ItemsScrollBox)
	{
		ItemsScrollBox->ForceLayoutPrepass();
		UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: 已强制刷新ScrollBox布局"));
	}
	
	// 检查必要组件
	if (!ItemsScrollBox)
	{
		UE_LOG(LogTemp, Warning, TEXT("DailyUpgradeRewardPage: ItemsScrollBox控件未绑定"));
		return;
	}
	
	if (ItemsScrollBox->GetChildrenCount() == 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("DailyUpgradeRewardPage: ItemsScrollBox中没有子控件"));
		return;
	}
	
	// 通过Subsystem获取业务逻辑结果
	UGameInstance* GameInstance = GetGameInstance();
	if (!GameInstance)
	{
		UE_LOG(LogTemp, Warning, TEXT("DailyUpgradeRewardPage: 无法获取GameInstance"));
		return;
	}
	
	UUpgradeActivitySubsystem* Sub = GameInstance->GetSubsystem<UUpgradeActivitySubsystem>();
	if (!Sub)
	{
		UE_LOG(LogTemp, Error, TEXT("DailyUpgradeRewardPage: 无法获取UpgradeActivitySubsystem"));
		return;
	}
	
	// 直接调用Subsystem的业务逻辑函数
	int32 TargetIndex = Sub->GetTargetChestIndexForCurrentExperience();
	UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: 从Subsystem获取的目标宝箱索引: %d"), TargetIndex);
	UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: ItemsScrollBox子控件总数: %d"), ItemsScrollBox->GetChildrenCount());
	UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: 目标索引有效性检查: %s"), 
		(TargetIndex >= 0 && TargetIndex < ItemsScrollBox->GetChildrenCount()) ? TEXT("有效") : TEXT("无效"));
	
	if (TargetIndex >= 0 && TargetIndex < ItemsScrollBox->GetChildrenCount())
	{
		float ScrollOffset = 0.0f;
		
		// 检查是否为目标是最后一个控件（需要靠右显示）
		if (TargetIndex == ItemsScrollBox->GetChildrenCount() - 1)
		{
			// 最后一个控件：使用最大滚动偏移量，使其靠右显示
			ScrollOffset = CalculateMaxScrollOffset();
			UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: 目标为最后一个控件，设置最大滚动偏移量: %.2f"), ScrollOffset);
					
			// 检查是否需要使用备选方案（几何信息无效或计算结果为0）
			if (ScrollOffset <= 0.0f || GHasInvalidGeometryDetected)
			{
				// 备选方案：基于控件数量和估算宽度计算
				float EstimatedWidgetWidth = 130.0f; // 使用实际测量的控件宽度
				float EstimatedSpacing = 10.0f; // 估算间距
				int32 WidgetCount = ItemsScrollBox->GetChildrenCount();
						
				float TotalEstimatedWidth = WidgetCount * EstimatedWidgetWidth + (WidgetCount - 1) * EstimatedSpacing;
				float ViewportWidth = ItemsScrollBox->GetCachedGeometry().GetLocalSize().X;
				float EstimatedMaxOffset = FMath::Max(0.0f, TotalEstimatedWidth - ViewportWidth);
						
				// 使用更大的估算值确保靠右效果
				EstimatedMaxOffset += 50.0f; // 额外偏移确保完全靠右
						
				ScrollOffset = EstimatedMaxOffset;
				UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: 使用备选方案估算值: %.2f (控件数:%d, 估算总宽:%.2f, 可视宽:%.2f, 额外偏移:50.00)"), 
					EstimatedMaxOffset, WidgetCount, TotalEstimatedWidth, ViewportWidth);
						
				GHasInvalidGeometryDetected = false; // 重置状态
			}
		}
		else
		{
			// 非最后一个控件：使用居中逻辑
			ScrollOffset = CalculateCenterScrollOffset(TargetIndex);
			UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: 目标为中间控件，设置居中滚动偏移量: %.2f"), ScrollOffset);
		}
		
		// 设置滚动位置
		ItemsScrollBox->SetScrollOffset(ScrollOffset);
		
		UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: 已设置ScrollBox滚动偏移量为 %.2f，目标控件索引: %d"), 
			ScrollOffset, TargetIndex);
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("DailyUpgradeRewardPage: 目标索引 %d 超出范围 [0, %d]"), 
			TargetIndex, ItemsScrollBox->GetChildrenCount() - 1);
	}
}

int32 UDailyUpgradeRewardPage::FindTargetChestIndexForExperience(int32 CurrentExp, const TArray<int32>& TaskRelatedValues)
{
	// 调用Subsystem的业务逻辑函数
	UGameInstance* GameInstance = GetGameInstance();
	if (GameInstance)
	{
		UUpgradeActivitySubsystem* Sub = GameInstance->GetSubsystem<UUpgradeActivitySubsystem>();
		if (Sub)
		{
			return Sub->GetTargetChestIndexForCurrentExperience();
		}
	}
	
	// 降级方案：使用原来的本地逻辑
	for (int32 i = 0; i < TaskRelatedValues.Num(); ++i)
	{
		if (TaskRelatedValues[i] > CurrentExp)
		{
			int32 TargetIndex = FMath::Max(0, i - 1);
			UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: 降级方案 - 找到经验值分界点，索引 %d 的值 %d > 当前经验 %d，目标索引: %d"), 
				i, TaskRelatedValues[i], CurrentExp, TargetIndex);
			return TargetIndex;
		}
	}
	
	int32 LastIndex = FMath::Max(0, TaskRelatedValues.Num() - 1);
	UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: 降级方案 - 当前经验 %d 超过了所有TaskRelatedValues，返回最后一个索引: %d"), 
		CurrentExp, LastIndex);
	return LastIndex;
}

float UDailyUpgradeRewardPage::CalculateCenterScrollOffset(int32 TargetIndex)
{
	// 计算使目标控件居中显示所需的滚动偏移量
	
	if (ItemsScrollBox->GetChildrenCount() == 0)
		return 0.0f;
	
	// 获取ScrollBox的可视区域宽度
	float ViewportWidth = ItemsScrollBox->GetCachedGeometry().GetLocalSize().X;
	UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: ScrollBox可视区域宽度: %.2f"), ViewportWidth);
	
	// 计算目标控件的累计位置
	float TargetPosition = 0.0f;
	float TargetWidgetWidth = 0.0f;
	
	for (int32 i = 0; i < ItemsScrollBox->GetChildrenCount(); ++i)
	{
		UWidget* ChildWidget = ItemsScrollBox->GetChildAt(i);
		if (ChildWidget)
		{
			float WidgetWidth = ChildWidget->GetCachedGeometry().GetLocalSize().X;
			
			// 添加间距（假设有默认间距）
			if (i > 0)
			{
				TargetPosition += 10.0f; // 假设10像素间距
			}
			
			if (i == TargetIndex)
			{
				TargetWidgetWidth = WidgetWidth;
				break; // 找到目标控件，停止累加
			}
			
			TargetPosition += WidgetWidth;
		}
	}
	
	UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: 目标控件累积位置: %.2f, 宽度: %.2f"), TargetPosition, TargetWidgetWidth);
	
	// 计算居中偏移量
	float CenterOffset = TargetPosition + (TargetWidgetWidth / 2.0f) - (ViewportWidth / 2.0f);
	
	// 确保偏移量在有效范围内
	float MaxOffset = CalculateMaxScrollOffset();
	CenterOffset = FMath::Clamp(CenterOffset, 0.0f, MaxOffset);
	
	UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: 计算得出的居中偏移量: %.2f (范围: 0.0 - %.2f)"), CenterOffset, MaxOffset);
	
	return CenterOffset;
}

float UDailyUpgradeRewardPage::CalculateMaxScrollOffset()
{
	// 计算ScrollBox的最大滚动偏移量
	
	if (ItemsScrollBox->GetChildrenCount() == 0)
		return 0.0f;
	
	float TotalContentWidth = 0.0f;
	
	// 计算所有子控件的总宽度
	for (int32 i = 0; i < ItemsScrollBox->GetChildrenCount(); ++i)
	{
		UWidget* ChildWidget = ItemsScrollBox->GetChildAt(i);
		if (ChildWidget)
		{
			TotalContentWidth += ChildWidget->GetCachedGeometry().GetLocalSize().X;
			
			// 添加间距
			if (i < ItemsScrollBox->GetChildrenCount() - 1)
			{
				TotalContentWidth += 10.0f; // 假设10像素间距
			}
		}
	}
	
	// 获取可视区域宽度
	float ViewportWidth = ItemsScrollBox->GetCachedGeometry().GetLocalSize().X;
	
	// 最大偏移量 = 总内容宽度 - 可视区域宽度
	float MaxOffset = FMath::Max(0.0f, TotalContentWidth - ViewportWidth);
	
	UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: 总内容宽度: %.2f, 可视区域宽度: %.2f, 最大偏移量: %.2f"), 
		TotalContentWidth, ViewportWidth, MaxOffset);
	
	// 详细调试信息
	UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: 详细几何信息:"));
	UE_LOG(LogTemp, Log, TEXT("  - ScrollBox实际宽度: %.2f"), ItemsScrollBox->GetCachedGeometry().GetLocalSize().X);
	
	// 检查所有子控件的信息
	bool bHasInvalidGeometry = false;
	for (int32 i = 0; i < ItemsScrollBox->GetChildrenCount(); ++i)
	{
		UWidget* ChildWidget = ItemsScrollBox->GetChildAt(i);
		if (ChildWidget)
		{
			FVector2D WidgetSize = ChildWidget->GetCachedGeometry().GetLocalSize();
			FVector2D WidgetPosition = ChildWidget->GetCachedGeometry().LocalToAbsolute(FVector2D(0, 0));
			UE_LOG(LogTemp, Log, TEXT("  - 控件[%d] 宽度: %.2f, 位置X: %.2f"), i, WidgetSize.X, WidgetPosition.X);
			
			// 检查是否有无效的几何信息
			if (WidgetSize.X <= 0.0f || WidgetPosition.X <= 0.0f)
			{
				bHasInvalidGeometry = true;
				UE_LOG(LogTemp, Log, TEXT("  ⚠️  控件[%d] 几何信息无效，将使用备选方案"), i);
			}
		}
	}
	
	// 如果发现无效几何信息，提前标记需要使用备选方案
	if (bHasInvalidGeometry)
	{
		UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: 检测到无效几何信息，准备使用备选计算方案"));
		
		// 设置全局标志，让滚动函数知道需要使用备选方案
		GHasInvalidGeometryDetected = true;
	}
	
	return MaxOffset;
}
