// 版权声明：在项目设置的描述页面填写您的版权信息。

// ==========================================
// RewardOptionWidget 实现 — 奖励选项 Widget
// ==========================================
//
// 文件作用:
//   1. 实现 URewardOptionWidget — 奖励选项弹窗
//   2. StoreBtn / OpenBtn 点击 → 广播对应委托
//   3. 防重复: 已点击后 SetIsEnabled(false) 禁用按钮
//
// 大厂原则:
//   - 双委托: 业务分发给上层页面 (DailyLoginPage / DailyUpgradeRewardPage)
//   - 防重复: 二次点击 SetIsEnabled(false), 防止业务重复触发
//   - 防御性: StoreBtn / OpenBtn 未绑定 → Log Error
//   - 跨子系统禁用: 不 include UpgradeActivitySubsystem (职责单一)
// ==========================================
#include "UI/Activity/Pages/ClaimBox/RewardOptionWidget.h"
#include "Components/Button.h"
#include "Data/Tables/DailyLoginTableRow.h"
// 移除 UpgradeActivitySubsystem: RewardOptionWidget 属于每日登录模块, 不应跨子系统调用
//#include "Systems/Activity/UpgradeActivitySubsystem.h"


// ==========================================
// 1. 生命周期
// ==========================================

/**
 * URewardOptionWidget::NativeConstruct
 *
 * 1. 初始化防重复标志
 * 2. 检查 StoreBtn / OpenBtn
 * 3. 清理旧绑定（Clear）, 重新绑定 AddDynamic
 * 4. 大量 Log 方便调试
 */
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


/**
 * URewardOptionWidget::NativeDestruct
 *
 * 析构日志
 */
void URewardOptionWidget::NativeDestruct()
{
	UE_LOG(LogTemp, Warning, TEXT("RewardOptionWidget NativeDestruct 被调用"));
	Super::NativeDestruct();
}


// ==========================================
// 2. 内部回调
// ==========================================

/**
 * URewardOptionWidget::HandleStoreClicked
 *
 * 1. 防重复: bStoreClickedHandled 直接返回
 * 2. 标记
 * 3. 广播 OnStoreToBag(CurrentDayIndex)
 * 4. 强制全局刷新: UUpgradeActivitySubsystem->OnGlobalRefresh.Broadcast()
 * 5. IsValid 防御后 RemoveFromParent
 */
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

	// 先广播事件再关闭 UI，确保事件处理完成
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

	// 【v206.1 修复】移除跨子系统错误调用
	// 原因: RewardOptionWidget 是每日登录模块的 Widget, 应该由 UActivitySubsystem 广播刷新
	// 而非 UUpgradeActivitySubsystem。这个错误导致每日登录页面收不到刷新事件,
	// BigRewardItem 的 ClaimButton 无法变成"已领取"状态。
	// 正确流程: HandleStoreClicked 广播 OnStoreToBag -> DailyLoginPage::HandleRewardOptionStore
	// -> TryClaimReward + RefreshRewardList -> ActivitySubsystem 广播 OnActivityDataChanged -> 页面刷新

	// 确保只调用一次 RemoveFromParent，并添加额外的安全检查
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


/**
 * URewardOptionWidget::HandleOpenClicked
 *
 * 1. 防重复
 * 2. 标记
 * 3. 广播 OnOpenNow(CurrentDayIndex)
 * 4. IsValid 防御后 RemoveFromParent
 */
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

	// 先广播事件再关闭 UI
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

	// 确保只调用一次 RemoveFromParent，并添加额外的安全检查
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


// ==========================================
// 3. 公共接口
// ==========================================

/**
 * URewardOptionWidget::InitSelection
 *
 * 1. 校验 Options 非空
 * 2. 缓存 Options[0].DayIndex
 *
 * 注意:
 * - UHT 不允许暴露原始结构体指针数组, 改用 const 引用
 * - 访问成员用 . 而非 ->
 *   e.g. int32 ID = Options[0].RewardItemID;
 */
void URewardOptionWidget::InitSelection(const TArray<FDailyLoginConfigRow>& Options)
{
	if (Options.Num() == 0) return;

	// 存储第一个奖励的天数信息（假设同一天的奖励天数相同）
	CurrentDayIndex = Options[0].DayIndex;
	UE_LOG(LogTemp, Warning, TEXT("RewardOptionWidget 初始化，天数: %d"), CurrentDayIndex);

	// 逻辑保持不变，Options 现在是对象的引用集合而非指针集合
	// 访问成员使用 . 而不是 ->
	// 例如: int32 ID = Options[0].RewardItemID;
}
