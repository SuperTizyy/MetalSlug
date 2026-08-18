// 版权声明：在项目设置的描述页面填写您的版权信息。

// ==========================================
// DailyLoginCheatWidget 实现 — 每日登录调试小工具
// ==========================================
//
// 文件作用:
//   1. 实现 UDailyLoginCheatWidget — 调试用所有 UI 逻辑
//   2. 1 个输入框 + 1 个按钮 (极简)
//   3. 主动拉刷新: NotifyMainPageRefresh 直接定位主页面
//
// 大厂原则:
//   - 调试面板: 与正式 UI 分离, 发布版可剔除
//   - 主动拉刷新: NotifyMainPageRefresh
//   - 极简: 仅 1 个输入框 + 1 个按钮
// ==========================================
#include "UI/Activity/Pages/DemoWidget/DailyLoginCheatWidget.h"
#include "Components/EditableText.h"
#include "Components/Button.h"
#include "UI/Activity/Pages/DailyLogin/DailyLoginPage.h"
#include "Engine/GameInstance.h"
#include "Blueprint/UserWidget.h"
#include "Systems/Activity/ActivitySubsystem.h"


// ==========================================
// 1. 生命周期
// ==========================================

/**
 * UDailyLoginCheatWidget::NativeConstruct
 *
 * 1. 绑定 ApplyCheatBtn -> OnApplyClicked
 * 2. 默认值填充（如有）
 */
void UDailyLoginCheatWidget::NativeConstruct()
{
	Super::NativeConstruct();

	// 绑定确认按钮
	if (ApplyCheatBtn)
	{
		ApplyCheatBtn->OnClicked.AddDynamic(this, &UDailyLoginCheatWidget::OnApplyClicked);
	}
}


// ==========================================
// 2. 内部回调
// ==========================================

/**
 * UDailyLoginCheatWidget::OnApplyClicked
 *
 * 1. 从 DayInput 读取文本
 * 2. FCString::Atoi 转 int
 * 3. NotifyMainPageRefresh
 */
void UDailyLoginCheatWidget::OnApplyClicked()
{
	if (DayInput)
	{
		// 从输入框读取天数
		const FString& DayText = DayInput->GetText().ToString();
		int32 NewDay = FCString::Atoi(*DayText);

		// 通知主页面刷新
		NotifyMainPageRefresh(NewDay);
	}
}


// ==========================================
// 3. 辅助函数
// ==========================================

/**
 * UDailyLoginCheatWidget::NotifyMainPageRefresh
 *
 * 通过 Subsystem 单例拉取主页面引用, 避免遍历 World 中的 Actor
 * 流程:
 * 1. GetGameInstance 拿 GI
 * 2. GI->GetSubsystem<UActivitySubsystem>() 拿 Sub
 * 3. Sub->GetLoginPage() 拿主页面弱引用（Page 未注册或已销毁时返回 nullptr）
 * 4. 调用 Page->Cheat_SetDayAndRefresh(NewDay) 触发刷新
 * 5. 日志记录
 *
 * @param NewDay 新设置的天数
 *
 * 优势（相对旧版 TActorIterator<AActor> 遍历）:
 * 1. O(1) 拿引用, 不再 O(n) 遍历场景中所有 Actor
 * 2. 弱引用自动感知 Page 销毁, 不存在悬挂指针
 * 3. Widget 之间零耦合, 调试面板不知道主页面存在
 * 4. 编译不再依赖 EngineUtils.h 的 TActorIterator
 */
void UDailyLoginCheatWidget::NotifyMainPageRefresh(int32 NewDay)
{
	// 第一步: 获取 GameInstance（不再依赖 World, 因为 GameInstance 在关卡切换时仍存活）
	UGameInstance* GI = GetGameInstance();
	if (!GI)
	{
		UE_LOG(LogTemp, Error, TEXT("CheatWidget: 通知主页面刷新失败, GameInstance 为空"));
		return;
	}

	// 第二步: 获取 ActivitySubsystem（GameInstanceSubsystem 与 GI 同生命周期, 始终存在）
	UActivitySubsystem* ActivitySub = GI->GetSubsystem<UActivitySubsystem>();
	if (!ActivitySub)
	{
		UE_LOG(LogTemp, Error, TEXT("CheatWidget: 通知主页面刷新失败, ActivitySubsystem 未注册"));
		return;
	}

	// 第三步: 通过 Subsystem 拿主页面弱引用（Page 未注册或已销毁时 GetLoginPage() 返回 nullptr）
	UDailyLoginPage* MainPage = ActivitySub->GetLoginPage();
	if (!MainPage)
	{
		UE_LOG(LogTemp, Warning,
			TEXT("CheatWidget: 通知主页面刷新失败, 主页面未注册或已销毁, NewDay=%d"), NewDay);
		return;
	}

	// 第四步: 触发主页面刷新（Page 内部会调用 Subsystem->Cheat_JumpToDay 修改存档, 再 RefreshRewardList + ScrollToCurrentDay）
	MainPage->Cheat_SetDayAndRefresh(NewDay);

	UE_LOG(LogTemp, Log, TEXT("CheatWidget: 通知主页面刷新成功, NewDay=%d"), NewDay);
}
