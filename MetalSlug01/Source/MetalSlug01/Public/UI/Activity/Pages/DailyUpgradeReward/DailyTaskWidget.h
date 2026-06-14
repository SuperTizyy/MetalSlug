// 版权声明：在项目设置的描述页面填写您的版权信息。

#pragma once

// ==========================================
// 头文件包含说明
// ==========================================
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "DailyTaskWidget.generated.h"


/**
 * @class UDailyTaskWidget
 * @brief 每天任务 Widget
 *
 * 职责说明:
 * - 升级奖励页面的单天任务项
 * - 显示选中高亮框 + 天数按钮 + 天数文本 + 锁定图标
 * - 由父级（升级奖励页）动态创建和管理
 *
 * 架构理念:
 * 1. 极简: 只有 4 个 BindWidget, 状态由父级维护
 * 2. 复用: 每一天一个 UDailyTaskWidget 实例
 * 3. 数据驱动: 天数 / 选中状态 / 锁定状态都来自父级
 *
 * 关联:
 * - 上级: UDailyUpgradeRewardPage（升级奖励主页）
 */
UCLASS()
class METALSLUG01_API UDailyTaskWidget : public UUserWidget
{
	GENERATED_BODY()

public:
	// ==========================================
	// 1. UI 组件
	// ==========================================

	/** 选中高亮框（当前选中的任务会显示） */
	UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
	class UImage* SelectionHighlightImage;

	/** 天数按钮（点击切换到对应天） */
	UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
	class UButton* DayButton;

	/** 天数显示文本（"Day 1" / "Day 2" 等） */
	UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
	class UTextBlock* DayText;

	/** 锁定图标（未到日期时显示） */
	UPROPERTY(BlueprintReadWrite, meta = (BindWidget))
	class UImage* LockIconImage;
};
