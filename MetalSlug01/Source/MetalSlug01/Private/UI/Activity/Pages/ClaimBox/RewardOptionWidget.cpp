#include "UI/Activity/Pages/ClaimBox/RewardOptionWidget.h"
#include "Components/Button.h"
#include "UI/Activity/Data/DailyLoginConfig.h"
#include "UI/Activity/Core/UpgradeActivitySubsystem.h"

void URewardOptionWidget::NativeConstruct()
{
	Super::NativeConstruct();

	UE_LOG(LogTemp, Warning, TEXT("=== RewardOptionWidget NativeConstruct 开始 ==="));
	
	// 初始化防重复调用标志
	bStoreClickedHandled = false;
	bOpenClickedHandled = false;
	
	// 检查按钮是否存在
	if (!StoreBtn)
	{
		UE_LOG(LogTemp, Error, TEXT("RewardOptionWidget: StoreBtn 未绑定，请检查蓝图中的控件名称！"));
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("RewardOptionWidget: StoreBtn 指针有效: %s"), 
		       StoreBtn ? *StoreBtn->GetName() : TEXT("NULL"));
	}
	
	if (!OpenBtn)
	{
		UE_LOG(LogTemp, Error, TEXT("RewardOptionWidget: OpenBtn 未绑定，请检查蓝图中的控件名称！"));
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("RewardOptionWidget: OpenBtn 指针有效: %s"), 
		       OpenBtn ? *OpenBtn->GetName() : TEXT("NULL"));
	}

	// 清理可能存在的旧绑定，防止重复绑定
	if (StoreBtn)
	{
		StoreBtn->OnClicked.Clear();
		StoreBtn->OnClicked.AddDynamic(this, &URewardOptionWidget::HandleStoreClicked);
		UE_LOG(LogTemp, Warning, TEXT("RewardOptionWidget: 成功绑定 StoreBtn 事件"));
	}

	if (OpenBtn)
	{
		OpenBtn->OnClicked.Clear();
		OpenBtn->OnClicked.AddDynamic(this, &URewardOptionWidget::HandleOpenClicked);
		UE_LOG(LogTemp, Warning, TEXT("RewardOptionWidget: 成功绑定 OpenBtn 事件"));
	}
	
	UE_LOG(LogTemp, Warning, TEXT("=== RewardOptionWidget NativeConstruct 结束 ==="));
}

void URewardOptionWidget::NativeDestruct()
{
	UE_LOG(LogTemp, Warning, TEXT("RewardOptionWidget NativeDestruct 被调用"));
	Super::NativeDestruct();
}

void URewardOptionWidget::HandleStoreClicked()
{
	UE_LOG(LogTemp, Warning, TEXT("=== HandleStoreClicked 被调用 ==="));
	
	// 添加防重复调用保护
	if (bStoreClickedHandled)
	{
		UE_LOG(LogTemp, Warning, TEXT("HandleStoreClicked 已被调用过，忽略重复调用"));
		return;
	}
	bStoreClickedHandled = true;
	
	// 先广播事件再关闭UI，确保事件处理完成
	if (OnStoreToBag.IsBound())
	{
		UE_LOG(LogTemp, Warning, TEXT("OnStoreToBag 已绑定，准备广播事件，天数: %d"), CurrentDayIndex);
		OnStoreToBag.Broadcast(CurrentDayIndex); // 传递天数参数
		UE_LOG(LogTemp, Warning, TEXT("事件广播完成"));
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("OnStoreToBag 未绑定"));
	}
	
	// 直接强制刷新相关页面
	UGameInstance* GameInstance = GetGameInstance();
	if (GameInstance)
	{
		UUpgradeActivitySubsystem* Subsystem = GameInstance->GetSubsystem<UUpgradeActivitySubsystem>();
		if (Subsystem)
		{
			Subsystem->OnGlobalRefresh.Broadcast();
			UE_LOG(LogTemp, Warning, TEXT("✅ 已强制刷新所有相关页面"));
		}
	}
	
	// 确保只调用一次RemoveFromParent，并添加额外的安全检查
	if (IsValid(this))
	{
		UE_LOG(LogTemp, Warning, TEXT("Widget 有效，准备调用 RemoveFromParent"));
		RemoveFromParent();
		UE_LOG(LogTemp, Warning, TEXT("RemoveFromParent 执行完成"));
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("Widget 已经无效"));
	}
}

void URewardOptionWidget::HandleOpenClicked()
{
	UE_LOG(LogTemp, Warning, TEXT("=== 🎯 HandleOpenClicked 被调用 ==="));
	
	// 添加防重复调用保护
	if (bOpenClickedHandled)
	{
		UE_LOG(LogTemp, Warning, TEXT("HandleOpenClicked 已被调用过，忽略重复调用"));
		return;
	}
	bOpenClickedHandled = true;
	
	UE_LOG(LogTemp, Warning, TEXT("当前天数索引: %d"), CurrentDayIndex);
	
	// 先广播事件再关闭UI
	if (OnOpenNow.IsBound())
	{
		UE_LOG(LogTemp, Warning, TEXT("✅ OnOpenNow 已绑定，准备广播事件，天数: %d"), CurrentDayIndex);
		OnOpenNow.Broadcast(CurrentDayIndex);
		UE_LOG(LogTemp, Warning, TEXT("✅ 事件广播完成"));
	}
	else
	{
		UE_LOG(LogTemp, Error, TEXT("❌ OnOpenNow 未绑定！无法触发跳转"));
	}
	
	// 确保只调用一次RemoveFromParent，并添加额外的安全检查
	if (IsValid(this))
	{
		UE_LOG(LogTemp, Warning, TEXT("Widget 有效，准备调用 RemoveFromParent"));
		RemoveFromParent();
		UE_LOG(LogTemp, Warning, TEXT("RemoveFromParent 执行完成"));
	}
	else
	{
		UE_LOG(LogTemp, Warning, TEXT("Widget 已经无效"));
	}
}

void URewardOptionWidget::InitSelection(const TArray<FDailyLoginConfigRow>& Options)
{
	if (Options.Num() == 0) return;

	// 存储第一个奖励的天数信息（假设同一天的奖励天数相同）
	CurrentDayIndex = Options[0].DayIndex;
	UE_LOG(LogTemp, Warning, TEXT("RewardOptionWidget 初始化，天数: %d"), CurrentDayIndex);

	// 逻辑保持不变，Options 现在是对象的引用集合而非指针集合
	// 访问成员使用 . 而不是 ->
	// 例如：int32 ID = Options[0].RewardItemID;
}