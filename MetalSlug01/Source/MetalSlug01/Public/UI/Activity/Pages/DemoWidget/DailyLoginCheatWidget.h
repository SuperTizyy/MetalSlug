// 版权声明：在项目设置的描述页面填写您的版权信息。

#pragma once

// ==========================================
// DailyLoginCheatWidget 头文件 — 每日登录调试小工具
// ==========================================
//
// 文件作用:
//   1. 声明 UDailyLoginCheatWidget — 调试用每日登录小工具
//   2. 测试用: 手动设置当前登录天数
//   3. 输入框输入数字 + 点击确认 + 跳到指定天数
//
// 架构理念:
//   - 调试面板: 与正式 UI 分离, 发布版可剔除
//   - 主动拉刷新: NotifyMainPageRefresh 直接定位主页面
//   - 极简: 1 个输入框 + 1 个按钮
// ==========================================
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "DailyLoginCheatWidget.generated.h"


/**
 * @class UDailyLoginCheatWidget
 * @brief 每日登录调试小工具
 *
 * 职责说明:
 * - 测试用: 手动设置当前登录天数
 * - 输入框输入数字, 点击确认后跳到指定天数
 * - 自动通知主页面刷新
 *
 * 架构理念:
 * 1. 调试面板: 与正式 UI 分离, 发布版可剔除
 * 2. 主动拉刷新: NotifyMainPageRefresh 直接定位主页面
 * 3. 极简: 1 个输入框 + 1 个按钮
 *
 * 注意: 实际使用时应只在 Debug 构建中启用
 */
UCLASS()
class METALSLUG01_API UDailyLoginCheatWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// ==========================================
	// 1. UI 组件
	// ==========================================

	/**
	 * 调试用输入框（输入天数）
	 * BlueprintReadWrite 方便蓝图操作
	 */
	UPROPERTY(meta = (BindWidget), BlueprintReadWrite, Category = "Cheat")
	class UEditableText* DayInput;

	/**
	 * 调试用确认按钮
	 */
	UPROPERTY(meta = (BindWidget), BlueprintReadWrite, Category = "Cheat")
	class UButton* ApplyCheatBtn;

	// ==========================================
	// 2. 生命周期
	// ==========================================

	/**
	 * 组件初始化
	 * 1. 绑定 ApplyCheatBtn -> OnApplyClicked
	 * 2. 默认值填充
	 */
	virtual void NativeConstruct() override;

protected:
	// ==========================================
	// 3. 内部回调
	// ==========================================

	/**
	 * 点击按钮的回调逻辑
	 * 1. 读取 DayInput 文本
	 * 2. 转 int
	 * 3. NotifyMainPageRefresh
	 */
	UFUNCTION()
	void OnApplyClicked();

	// ==========================================
	// 4. 辅助函数
	// ==========================================

	/**
	 * 查找场景中的主界面并触发刷新
	 * @param NewDay 新设置的天数
	 * 流程: GetGameInstance -> UActivitySubsystem -> 找主页面 -> Cheat_SetDayAndRefresh
	 */
	void NotifyMainPageRefresh(int32 NewDay);
};
