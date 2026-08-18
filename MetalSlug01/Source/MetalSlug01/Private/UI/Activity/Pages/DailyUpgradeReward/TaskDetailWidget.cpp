// 版权声明：在项目设置的描述页面填写您的版权信息。

// ==========================================
// TaskDetailWidget 实现 — 任务明细 Widget
// ==========================================
//
// 文件作用:
//   1. 实现 UTaskDetailWidget — 单个任务所有 UI 逻辑
//   2. SetupClaimButton 解析 DayIdentifier + TaskIndex 状态机
//   3. 3 种状态: 已领取 / 可领取 / 未满足(去完成)
//
// 大厂原则:
//   - 单一职责: 每个状态调对应 helper (ApplyClaimedUI 等)
//   - 防御链: GameInstance / UUpgradeActivitySubsystem
//   - 越界防御: TaskClaimStatus 越界 → 全部隐藏
//   - 跳转: "去完成" 走 GameFlowSubsystem 状态机 (v229)
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
// 【v218 大厂架构 - 移除冗余】原有 line 18 重复 #include "UI/Activity/Pages/DailyUpgradeReward/TaskDetailWidget.h"
//   已在 line 6 include 过, 此处删除避免重复包含
#include "Data/Tables/DailyLoginTableRow.h"
// 【v218 大厂架构新增】"去完成" 跳转 LANRoom 所需
// 【v229 重构】改为调用 GameFlowSubsystem::TransitionToMainLobby() 走状态机
#include "Services/UIViewService.h"
#include "Systems/GameFlowSubsystem.h"
#include "Enums/CoreEnums.h"
#include "Engine/GameInstance.h"

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
 * 5. 状态机 (大厂单一职责, 每个分支调对应 helper):
 *    - Claimed (=1): 调 ApplyClaimedUI() (按钮置灰 + "已领取" + 成功图标)
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

// ✅ 【v218 修复】未完成状态也要可点击，按下去 → 跳转到 LANRoom (WBP_LANRoomPage)
// 大厂原则: 永远绑事件，内部 Wrapper 自行判状态 — 不让 Blueprint 端判
// 跳转走 SSOT = UUIViewService::ShowPanel(EUIPanel::LANRoom), 不重复造轮子
	ClaimButton->OnClicked.RemoveAll(this);
	ClaimButton->OnClicked.AddDynamic(this, &UTaskDetailWidget::HandleClaimButtonClickWrapper);

	// 存储参数到成员变量（用于包装器读取当前所属任务 ID）
	CurrentDayIdentifier = DayIdentifier;
	CurrentTaskIndex = TaskIndex;

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

	// 🔧【v216 重构】已领取状态: 走 ApplyClaimedUI() 单一职责函数
	// 大厂原则: 按钮置灰 (SetIsEnabled false) + ClaimHintText="已领取" + ClaimSuccessImage Visible
	if (ClaimStatus == 1)
	{
		UE_LOG(LogTemp, Log, TEXT("[HIDE_DEBUG] 🎯 已领取状态检测到! ClaimStatus=1, Day=%s, TaskIndex=%d"), *DayIdentifier, TaskIndex);
		ApplyClaimedUI();
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

			// [v217] 重复绑定已上移到顶部, 此处删除避免重复
			// ClaimButton->OnClicked.AddDynamic(this, &UTaskDetailWidget::HandleClaimButtonClickWrapper);

			UE_LOG(LogTemp, Log, TEXT("[HIDE_DEBUG] 🟡 按钮已启用（黄色），可点击"));
		}
		else
		{
			// ❌ 不满足条件: 按钮仍然可见且可点击（按下去什么都不发生）
			//       [v217 修复] 用户要求"未完成时可点击，但无后续操作"
			//       由 HandleClaimButtonClickWrapper 实时查 Subsystem 判定
			ClaimButton->SetVisibility(ESlateVisibility::Visible);
			ClaimButton->SetIsEnabled(true);
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

			UE_LOG(LogTemp, Log, TEXT("[HIDE_DEBUG] ⚪ 按钮未完成状态（灰色），可点击 → 点击跳转 LANRoom（%d/%d）"), CompleteCount, RequiredCount);
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
// 1.5 已领取 UI 应用 - v216 新增 (大厂单一职责)
// ==========================================

/**
 * UTaskDetailWidget::ApplyClaimedUI
 *
 * 职责: 集中管理"已领取"状态的 UI 视觉
 * 1. ClaimButton: SetIsEnabled(false) + 灰色 + Visible (保留布局, 满足 v216 需求)
 * 2. ClaimHintText: "已领取" + Visible (满足 v216 需求)
 * 3. ClaimSuccessImage: Visible
 *
 * @note 每个控件必须有指针, 否则 Log Error + 跳过 (大厂原则: 零兜底)
 */
void UTaskDetailWidget::ApplyClaimedUI()
{
	UE_LOG(LogTemp, Log, TEXT("[HIDE_DEBUG] 🎯 ApplyClaimedUI 被调用"));

	// 1. ClaimButton: 保留布局, 设为不可点击 + 灰色
	if (ClaimButton)
	{
		ClaimButton->SetVisibility(ESlateVisibility::Visible);
		ClaimButton->SetIsEnabled(false);

		// 设置按钮颜色为灰色 - 与"未完成条件"分支一致
		FButtonStyle GrayStyle = ClaimButton->GetStyle();
		GrayStyle.Normal.TintColor = FLinearColor::Gray;
		GrayStyle.Disabled.TintColor = FLinearColor::Gray;
		ClaimButton->SetStyle(GrayStyle);

		UE_LOG(LogTemp, Log, TEXT("[HIDE_DEBUG] 🎯 ClaimButton 已置灰且 SetIsEnabled(false)"));
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[HIDE_DEBUG] ❌ ClaimButton 为空指针! ApplyClaimedUI 失败"));
		return;
	}

	// 2. ClaimHintText: 显示"已领取"
	if (ClaimHintText)
	{
		ClaimHintText->SetText(FText::FromString(TEXT("已领取")));
		ClaimHintText->SetVisibility(ESlateVisibility::Visible);
		UE_LOG(LogTemp, Log, TEXT("[HIDE_DEBUG] 🎯 ClaimHintText 显示「已领取」"));
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[HIDE_DEBUG] ❌ ClaimHintText 为空指针! ApplyClaimedUI 部分失败"));
		// 不 return: ClaimButton 已经置灰, ClaimSuccessImage 还要显示
	}

	// 3. ClaimSuccessImage: 显示成功图标
	if (ClaimSuccessImage)
	{
		ClaimSuccessImage->SetVisibility(ESlateVisibility::Visible);
		UE_LOG(LogTemp, Log, TEXT("[HIDE_DEBUG] ✅ ClaimSuccessImage 设置为 Visible"));
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("[HIDE_DEBUG] ❌ ClaimSuccessImage 为空指针!"));
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
 *
 * ✅ 【v217 修复】未完成/已领取状态点了按钮 = 什么都不发生
 *    实时查 Subsystem 当前状态,非可领取则直接 return,不做任何业务
 *    大厂原则: 状态查实时源(SSOT),不依赖 SetupClick 时缓存的旧值
 */
void UTaskDetailWidget::HandleClaimButtonClickWrapper()
{
	// 防御链: 成员变量未初始化
	if (CurrentDayIdentifier.IsEmpty() || CurrentTaskIndex < 0)
	{
		UE_LOG(LogTemp, Warning, TEXT("UTaskDetailWidget: HandleClaimButtonClickWrapper - 成员变量未初始化, 不做任何操作"));
		return;
	}

	// 实时查 Subsystem 当前状态
	UGameInstance* GameInstance = GetGameInstance();
	UUpgradeActivitySubsystem* Subsystem = GameInstance ? GameInstance->GetSubsystem<UUpgradeActivitySubsystem>() : nullptr;
	if (!Subsystem)
	{
		UE_LOG(LogTemp, Error, TEXT("UTaskDetailWidget: HandleClaimButtonClickWrapper - 无法获取 UpgradeActivitySubsystem"));
		return;
	}

	int32 DayNumber = FCString::Atoi(*CurrentDayIdentifier.RightChop(3));
	const FUpgradeRewardSaveRecord* DayRecord = Subsystem->GetRecordByDate(DayNumber);
	if (!DayRecord || !DayRecord->TaskClaimStatus.IsValidIndex(CurrentTaskIndex))
	{
		UE_LOG(LogTemp, Warning, TEXT("UTaskDetailWidget: HandleClaimButtonClickWrapper - Data 越界, 不做任何操作"));
		return;
	}

	int32 ClaimStatus = DayRecord->TaskClaimStatus[CurrentTaskIndex];
	if (ClaimStatus == 1)
	{
		UE_LOG(LogTemp, Log, TEXT("UTaskDetailWidget: HandleClaimButtonClickWrapper - 已领取状态, 不做任何操作"));
		return;
	}

	// 实时查完成度
	const FDailyUpgradeRewardConfigRow* ConfigRow = Subsystem->GetConfigRowForDay(CurrentDayIdentifier);
	if (!ConfigRow || !ConfigRow->TaskRelatedValues.IsValidIndex(CurrentTaskIndex))
	{
		UE_LOG(LogTemp, Warning, TEXT("UTaskDetailWidget: HandleClaimButtonClickWrapper - Config 越界, 不做任何操作"));
		return;
	}

	const int32 CompleteCount = DayRecord->TaskCompleteCounts.IsValidIndex(CurrentTaskIndex) ? DayRecord->TaskCompleteCounts[CurrentTaskIndex] : 0;
	const int32 RequiredCount = ConfigRow->TaskRelatedValues[CurrentTaskIndex];
	if (CompleteCount < RequiredCount)
	{
		// ==========================================
		// 【v218 大厂架构 - SSOT 跳转】未完成 → 跳 LANRoom
		// ==========================================
		// 决策:
		//   - 走 UUIViewService::ShowPanel(EUIPanel::LANRoom), 自动注入 LANRoomPresenter, 自动隐藏活动页
		//   - 不直接 OpenLevel: 静态面板切换, 不需要切图
		//   - 不直接 CreateWidget<WBP_LANRoomPage>: 重复架构 (UIViewService 内部已处理)
		//   - Q3=B: 奖励进度保留 — UUpgradeActivitySubsystem 是 GameInstanceSubsystem, 跨 Widget 持久
		//     重进 DailyUpgradeRewardPage 时自动恢复进度, 无需额外代码
		//
		// 大厂原则:
		//   - SSOT: 跳转入口唯一 = UUIViewService
		//   - 0 兜底: GI/UIView null 必须 Log Error, 不静默
		// ==========================================
		UE_LOG(LogTemp, Log, TEXT("UTaskDetailWidget: HandleClaimButtonClickWrapper - 未完成状态 (%d/%d), 跳转 LANRoom"), CompleteCount, RequiredCount);

		UGameInstance* GI = GetGameInstance();
		if (!GI)
		{
			UE_LOG(LogTemp, Error,
				TEXT("UTaskDetailWidget: HandleClaimButtonClickWrapper - GameInstance 为空, 无法跳转 LANRoom. "
				     "【v229 零兜底】请检查 UTaskDetailWidget 是否在合法 World 中."));
			return;
		}

		// 【v229 重构】走 GameFlowSubsystem 状态机, 不再直接 ShowPanel(LANRoom)
		UGameFlowSubsystem* Flow = GI->GetSubsystem<UGameFlowSubsystem>();
		if (!Flow)
		{
			UE_LOG(LogTemp, Error,
				TEXT("UTaskDetailWidget: HandleClaimButtonClickWrapper - GameFlowSubsystem 不可用, 无法跳转 LANRoom. "
				     "【v229 零兜底】请检查 GameInstanceSubsystem 注册."));
			return;
		}

		Flow->TransitionToMainLobby();
		return;
	}

	// ✅ 唯一真正可领取分支: 弹 RewardOptionWidget
	this->HandleClaimButtonClicked(CurrentDayIdentifier, CurrentTaskIndex);
}


// ==========================================
// 3. 内部回调 - 存储到背包
// ==========================================

/**
 * UTaskDetailWidget::HandleRewardStore
 *
 * 🔧【v216 重构】大厂原则: 单一数据真相源 (Single Source of Truth)
 *
 * 新流程 (5 步):
 * 1. 防御链: GameInstance / UUpgradeActivitySubsystem
 * 2. 取 ActualTaskIndex = CurrentTaskIndexForReward (SetupClaimButton 时缓存)
 * 3. 解析 DayNumber = FCString::Atoi(CurrentDayIdentifier.RightChop(3))
 * 4. 调 Subsystem->ClaimTaskRewardForDay(DayNumber, ActualTaskIndex)
 *    ├─ Subsystem 内部: AddOrUpdateRecord + 同步 CurrentRecord + SaveStatus
 *    └─ 大厂原则: UI 只负责触发, 业务逻辑收敛到 Subsystem
 * 5. 成功 → ApplyClaimedUI() (按钮置灰 + "已领取" + 成功图标)
 *
 * 反模式 (v216 之前):
 *   - TaskDetailWidget 手动 AddOrUpdateRecord + 改 TaskClaimStatus + 同步 CurrentRecord
 *   - 重复了 Subsystem 已有 ClaimTaskReward 的逻辑
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

	// 🔧【v216 重构】调用 Subsystem 权威 API ClaimTaskRewardForDay
	// 大厂原则: 大厂原则: 单一数据真相源 (Single Source of Truth)
	//   - 反模式: TaskDetailWidget 手动 AddOrUpdateRecord + 改 TaskClaimStatus
	//   - 正解: TaskDetailWidget 委托给 Subsystem 统一处理
	// 收益:
	//   - 重复架构消除 (Subsystem 已有 ClaimTaskReward 系列)
	//   - 持久化行为一致 (ClaimTaskRewardForDay 内部 SaveStatus)
	//   - 业务逻辑收敛到 Subsystem, UI 只负责触发
	const int32 DayNumber = FCString::Atoi(*CurrentDayIdentifier.RightChop(3)); // 从 "day1" 提取数字 1
	const bool bClaimSuccess = Subsystem->ClaimTaskRewardForDay(DayNumber, ActualTaskIndex);

	if (bClaimSuccess)
	{
		UE_LOG(LogTemp, Log, TEXT("[HIDE_DEBUG] 🎯 领取成功! Day=%d, TaskIndex=%d"), DayNumber, ActualTaskIndex);

		// 🔧【v216 重构】走 ApplyClaimedUI() 单一职责函数 (与 SetupClaimButton 已领取分支共享)
		// 大厂原则: DRY - 不在多处重复"已领取 UI" 设置
		ApplyClaimedUI();
	}
	else
	{
		// 🔧【v216 重构】领取失败: 强制 Log Error 让用户/QA 立即看到
		// 大厂原则: 零兜底 - 不静默 return, 不"假装成功"
		UE_LOG(LogTemp, Error,
			TEXT("[HIDE_DEBUG] ❌ 领取任务奖励失败 - Day=%d, TaskIndex=%d (请检查 Subsystem 日志)"),
			DayNumber, ActualTaskIndex);
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
