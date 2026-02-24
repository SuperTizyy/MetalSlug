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
#include "Components/VerticalBox.h"
#include "Components/ScrollBox.h"
#include "Kismet/GameplayStatics.h"
#include "Engine/DataTable.h"
#include "UI/Activity/Core/ActivitySubsystem.h"
#include "UI/Activity/Data/DailyLoginConfig.h"
#include "UI/Activity/Pages/DailyUpgradeReward/ExperienceChestClaimWidget.h"
#include "UI/Activity/Core/UpgradeActivitySubsystem.h"
#include "UI/Activity/Pages/SelectMultiplePopup/ActivityConfirmPopupWidget.h"

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
	Super::NativeConstruct();
	
	// 订阅UpgradeActivitySubsystem的奖励图标索引更新事件
	SubscribeToSubsystemEvents();
	
	// 在NativeConstruct阶段执行初始化逻辑，确保UI完全准备好
	InitializeRewardItemIcons();
	UpdateChestCountText();
	InitializeExperienceChestWidgets();
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
	// 取消订阅UpgradeActivitySubsystem的事件
	UnsubscribeFromSubsystemEvents();
	
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

	// 1. 从DailyUpgradeRewardConfigRow表获取数据
	FString ConfigPath = TEXT("/Game/UI/Activity/Data/DT_DailyUpgradeRewardConfigRow");
	UDataTable* ConfigTable = LoadObject<UDataTable>(nullptr, *ConfigPath);

	if (!ConfigTable)
	{
		UE_LOG(LogTemp, Error, TEXT("DailyUpgradeRewardPage: 无法加载DT_DailyUpgradeRewardConfigRow表"));
		ChestCountText->SetText(FText::FromString(TEXT("数据加载失败")));
		return;
	}

	// 2. 获取ActivityID==110的数据
	static const FString ContextString(TEXT("DailyUpgradeRewardPage"));
	TMap<FName, uint8*> RowMap = ConfigTable->GetRowMap();
	
	if (RowMap.Num() == 0)
	{
		UE_LOG(LogTemp, Error, TEXT("DailyUpgradeRewardPage: DT_DailyUpgradeRewardConfigRow表为空"));
		ChestCountText->SetText(FText::FromString(TEXT("无数据")));
		return;
	}

	const FDailyUpgradeRewardConfigRow* TargetRow = nullptr;
	
	// 查找ActivityID==110的记录
	for (const auto& Pair : RowMap)
	{
		const FDailyUpgradeRewardConfigRow* Row = reinterpret_cast<const FDailyUpgradeRewardConfigRow*>(Pair.Value);
		if (Row && Row->ActivityID == 110)
		{
			TargetRow = Row;
			UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: 找到ActivityID=110的记录"));
			break;
		}
	}

	if (!TargetRow)
	{
		UE_LOG(LogTemp, Error, TEXT("DailyUpgradeRewardPage: 未找到ActivityID=110的记录"));
		ChestCountText->SetText(FText::FromString(TEXT("未找到记录")));
		return;
	}

	// 3. 检查RewardItemCounts数组是否为空
	if (TargetRow->RewardItemCounts.Num() == 0)
	{
		UE_LOG(LogTemp, Error, TEXT("DailyUpgradeRewardPage: RewardItemCounts数组为空"));
		ChestCountText->SetText(FText::FromString(TEXT("0")));
		return;
	}

	// 4. 获取最后一个索引的数据
	FString LastRewardItemCount = TargetRow->RewardItemCounts.Last();
	UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: 最后一个RewardItemCount: %s"), *LastRewardItemCount);

	// 5. 显示在ChestCountText控件上（拼接X前缀）
	FString DisplayText = FString::Printf(TEXT("X%s"), *LastRewardItemCount);
	ChestCountText->SetText(FText::FromString(DisplayText));
	UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: ChestCountText已更新为: X%s"), *LastRewardItemCount);
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

	auto& Record = Sub->GetRecord();

	// 检查 ExperienceChestWidgetClass 是否设置
	if (ExperienceChestWidgetClass == nullptr)
	{
		UE_LOG(LogTemp, Error, TEXT("ExperienceChestWidgetClass 未设置！请在蓝图中指定 WBP_ExperienceChestClaimWidget 类"));
		return;
	}

	// 清空现有的子控件
	ItemsScrollBox->ClearChildren();

	// 遍历RewardItemIDs创建Widget
	for (int32 i = 0; i < Config->RewardItemIDs.Num(); ++i)
	{
		UExperienceChestClaimWidget* ChestWidget = CreateWidget<UExperienceChestClaimWidget>(GetWorld(), ExperienceChestWidgetClass.Get());
		if (!ChestWidget)
		{
			UE_LOG(LogTemp, Error, TEXT("DailyUpgradeRewardPage: 创建ExperienceChestClaimWidget失败，索引: %d"), i);
			continue;
		}
		
		// 关键：根据存档 Record.ChestClaimStatus[i] 来决定显示状态
		bool bIsClaimed = Record.ChestClaimStatus.IsValidIndex(i) && Record.ChestClaimStatus[i] == 1;
		ChestWidget->UpdateVisualStatus(bIsClaimed);
		
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
		
		ItemsScrollBox->AddChild(ChestWidget);
		UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: 成功创建第%d个ExperienceChestClaimWidget，已领取: %s"), i + 1, bIsClaimed ? TEXT("是") : TEXT("否"));
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
		return;
	}

	UUpgradeActivitySubsystem* Sub = GameInstance->GetSubsystem<UUpgradeActivitySubsystem>();
	if (!Sub)
	{
		CurrentExpText->SetText(FText::FromString(TEXT("0")));
		return;
	}

	auto& Record = Sub->GetRecord();
	FString ExpText = FString::Printf(TEXT("%d"), Record.CurrentExperience);
	CurrentExpText->SetText(FText::FromString(ExpText));
	
	UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: 经验值已更新为: %d"), Record.CurrentExperience);
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
	// 重新初始化所有UI组件
	InitializeExperienceChestWidgets();
	UpdateChestCountText();
	UpdateExperienceDisplay();
	
	UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: UI已刷新"));
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
	
	// 订阅奖励图标索引更新事件
	UpgradeSub->OnRewardIconIndexChanged.AddDynamic(this, &UDailyUpgradeRewardPage::OnRewardIconIndexChanged);
	
	UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: 已订阅Subsystem事件"));
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
	
	UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: 已取消订阅Subsystem事件"));
}

void UDailyUpgradeRewardPage::OnRewardIconIndexChanged(int32 NewIndex)
{
	UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: 接收到奖励图标索引更新事件，新索引: %d"), NewIndex);
	
	// 重新初始化奖励图标数据
	InitializeRewardItemIcons();
	
	// 更新奖励物品图像显示
	UpdateRewardItemImage();
	
	UE_LOG(LogTemp, Log, TEXT("DailyUpgradeRewardPage: UI已根据新索引更新完成"));
}
